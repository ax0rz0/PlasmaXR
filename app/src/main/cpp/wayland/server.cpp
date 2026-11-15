#include "wayland/server.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/log.h"
#include "wayland/internal.h"

namespace pxr {
namespace wl {

namespace {

constexpr size_t kMaxOutBytes = 4u << 20;  // a client that never reads is cut off
constexpr uint32_t kMaxMessage = 65536;
int gClientIndex = 0;

std::string formatArgs(const char* sig, const Args& args) {
    std::string out;
    char buf[64];
    for (size_t i = 0; i < args.size(); ++i) {
        const Value& v = args[i];
        if (i) out += ", ";
        switch (v.t) {
            case 'i': snprintf(buf, sizeof buf, "%d", v.i); out += buf; break;
            case 'u': snprintf(buf, sizeof buf, "%u", v.u); out += buf; break;
            case 'f': snprintf(buf, sizeof buf, "%.2f", v.f); out += buf; break;
            case 's': out += v.isNull ? "nil" : "\"" + v.s + "\""; break;
            case 'o': snprintf(buf, sizeof buf, "@%u", v.u); out += buf; break;
            case 'n': snprintf(buf, sizeof buf, "new id @%u", v.u); out += buf; break;
            case 'N':
                snprintf(buf, sizeof buf, " v%u new id @%u", v.version, v.u);
                out += v.s + buf;
                break;
            case 'a': snprintf(buf, sizeof buf, "array[%zu]", v.a.size()); out += buf; break;
            case 'h': out += "fd"; break;
            default: out += "?"; break;
        }
    }
    (void)sig;
    return out;
}

int makeKeymapFd(const char* text, const std::string& dir, uint32_t* sizeOut) {
    const size_t len = strlen(text) + 1;  // clients expect the terminating NUL to be included
    // A memfd, not ASharedMemory: on 4.19 kernels that is ashmem, which cannot be mapped MAP_PRIVATE
    // the way clients bound to wl_seat version 7 and up map the keymap (they crash or read zeros).
    int fd = static_cast<int>(syscall(SYS_memfd_create, "plasma-keymap", 1 /* MFD_CLOEXEC */));
    if (fd >= 0 && ftruncate(fd, static_cast<off_t>(len)) != 0) {
        close(fd);
        fd = -1;
    }
    if (fd < 0) {  // no memfd: an unlinked file next to the socket does the same job
        std::string tmpl = dir + "/.keymap-XXXXXX";
        std::vector<char> path(tmpl.begin(), tmpl.end());
        path.push_back('\0');
        fd = mkstemp(path.data());
        if (fd >= 0) {
            unlink(path.data());
            if (ftruncate(fd, static_cast<off_t>(len)) != 0) {
                close(fd);
                fd = -1;
            }
        }
    }
    if (fd < 0) return -1;
    void* p = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        close(fd);
        return -1;
    }
    memcpy(p, text, len);
    munmap(p, len);
    *sizeOut = static_cast<uint32_t>(len);
    return fd;
}

constexpr uint32_t kModShift = 1, kModLock = 2, kModCtrl = 4, kModAlt = 8, kModNum = 16, kModSuper = 64;

uint32_t heldModifiers(const std::set<uint32_t>& pressed) {
    uint32_t m = 0;
    for (uint32_t k : pressed) {
        if (k == 42 || k == 54) m |= kModShift;
        else if (k == 29 || k == 97) m |= kModCtrl;
        else if (k == 56 || k == 100) m |= kModAlt;
        else if (k == 125 || k == 126) m |= kModSuper;
    }
    return m;
}

std::vector<uint8_t> keyArray(const std::set<uint32_t>& pressed) {
    std::vector<uint8_t> a(pressed.size() * 4);
    size_t i = 0;
    for (uint32_t k : pressed) {
        memcpy(&a[i], &k, 4);
        i += 4;
    }
    return a;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Resource and Client
// ---------------------------------------------------------------------------------------------

Resource::Resource(Client& c, uint32_t id_, const char* ifaceName, uint32_t version_)
    : client(c), id(id_), iface(findInterface(ifaceName)), version(version_) {}

void Resource::send(uint16_t event, Args args) { client.sendMessage(*this, event, args); }

Client::Client(Server& s, int fd) : server(s), fd_(fd) {
    index_ = ++gClientIndex;
    create<DisplayRes>(1, 1);
}

Client::~Client() {
    // Newest objects first, so anything that points at an older object is gone before it is.
    std::vector<Resource*> order;
    order.reserve(objects_.size());
    for (auto& kv : objects_) order.push_back(kv.second.get());
    std::sort(order.begin(), order.end(), [](Resource* a, Resource* b) { return a->seq > b->seq; });
    for (Resource* r : order) objects_.erase(r->id);
    server.forgetClient(this);
    for (OutFds& of : outFds_) {
        for (int fd : of.fds) close(fd);
    }
    for (int fd : inFds_) close(fd);
    if (fd_ >= 0) close(fd_);
}

Resource* Client::get(uint32_t id) const {
    auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : it->second.get();
}

void Client::destroy(uint32_t id) {
    auto it = objects_.find(id);
    if (it == objects_.end()) return;
    objects_.erase(it);
    if (id < kServerIdBase && !dead_) {
        if (Resource* d = get(1)) d->send(wl_display::delete_id, {Value::U(id)});
    }
}

void Client::protocolError(Resource& r, uint32_t code, const std::string& msg) {
    if (dead_) return;
    LOGW("wayland[%d]: protocol error on %s@%u: %s", index_, r.iface ? r.iface->name : "?", r.id,
         msg.c_str());
    if (Resource* d = get(1)) {
        d->send(wl_display::error, {Value::O(r.id), Value::U(code), Value::S(msg)});
    }
    flush();
    dead_ = true;
}

void Client::sendMessage(Resource& r, uint16_t opcode, const Args& args) {
    if (dead_ || !r.iface || opcode >= r.iface->nEvents) return;
    const Message& m = r.iface->events[opcode];
    std::vector<uint8_t> bytes(8);
    std::vector<int> fds;
    encodeArgs(m.sig, args, bytes, fds);
    const uint32_t size = static_cast<uint32_t>(bytes.size());
    const uint32_t header = (size << 16) | opcode;
    memcpy(&bytes[0], &r.id, 4);
    memcpy(&bytes[4], &header, 4);
    if (server.trace()) {
        LOGD("wl[%d] -> %s@%u.%s(%s)", index_, r.iface->name, r.id, m.name,
             formatArgs(m.sig, args).c_str());
    }
    queue(bytes, fds);
}

void Client::queue(const std::vector<uint8_t>& bytes, const std::vector<int>& fds) {
    if (!fds.empty()) {
        OutFds of;
        of.offset = out_.size();
        for (int fd : fds) of.fds.push_back(fcntl(fd, F_DUPFD_CLOEXEC, 0));
        outFds_.push_back(std::move(of));
    }
    out_.insert(out_.end(), bytes.begin(), bytes.end());
    if (out_.size() > kMaxOutBytes) {
        LOGW("wayland[%d]: client is not reading events, disconnecting", index_);
        dead_ = true;
    }
}

void Client::flush() {
    size_t sent = 0;
    while (sent < out_.size() && !dead_) {
        size_t len = out_.size() - sent;
        std::vector<int> fds;
        bool carries = false;
        if (!outFds_.empty()) {
            if (outFds_.front().offset == sent) {
                carries = true;
                fds = outFds_.front().fds;
                if (outFds_.size() > 1) len = std::min(len, outFds_[1].offset - sent);
            } else {
                len = std::min(len, outFds_.front().offset - sent);
            }
        }
        iovec iov{out_.data() + sent, len};
        msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        std::vector<char> ctl;
        if (!fds.empty()) {
            ctl.assign(CMSG_SPACE(sizeof(int) * fds.size()), 0);
            msg.msg_control = ctl.data();
            msg.msg_controllen = ctl.size();
            cmsghdr* c = CMSG_FIRSTHDR(&msg);
            c->cmsg_level = SOL_SOCKET;
            c->cmsg_type = SCM_RIGHTS;
            c->cmsg_len = CMSG_LEN(sizeof(int) * fds.size());
            memcpy(CMSG_DATA(c), fds.data(), sizeof(int) * fds.size());
        }
        const ssize_t n = sendmsg(fd_, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) dead_ = true;
            break;
        }
        if (carries) {  // descriptors travel with the first byte of the chunk
            for (int fd : outFds_.front().fds) close(fd);
            outFds_.pop_front();
        }
        sent += static_cast<size_t>(n);
    }
    out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(sent));
    for (OutFds& of : outFds_) of.offset -= std::min(of.offset, sent);
}

bool Client::onReadable() {
    if (dead_) return false;
    for (;;) {
        uint8_t buf[4096];
        char ctl[CMSG_SPACE(sizeof(int) * 28)];
        iovec iov{buf, sizeof buf};
        msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ctl;
        msg.msg_controllen = sizeof ctl;
        const ssize_t n = recvmsg(fd_, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            dead_ = true;
            return false;
        }
        if (n == 0) {
            dead_ = true;
            return false;
        }
        for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
            if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) continue;
            const size_t count = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            const int* fds = reinterpret_cast<const int*>(CMSG_DATA(c));
            for (size_t i = 0; i < count; ++i) inFds_.push_back(fds[i]);
        }
        in_.insert(in_.end(), buf, buf + n);
        dispatchAll();
        if (dead_) return false;
    }
    return !dead_;
}

void Client::dispatchAll() {
    size_t pos = 0;
    while (!dead_ && in_.size() - pos >= 8) {
        uint32_t id, header;
        memcpy(&id, &in_[pos], 4);
        memcpy(&header, &in_[pos + 4], 4);
        const uint32_t size = header >> 16, opcode = header & 0xffff;
        Resource* display = get(1);
        if (size < 8 || (size & 3) != 0 || size > kMaxMessage) {
            protocolError(*display, wl_display::error_::invalid_method, "bad message size");
            break;
        }
        if (in_.size() - pos < size) break;
        const uint8_t* payload = &in_[pos + 8];
        const size_t payloadLen = size - 8;
        pos += size;

        Resource* r = get(id);
        if (!r) {
            protocolError(*display, wl_display::error_::invalid_object, "invalid object " + std::to_string(id));
            break;
        }
        if (opcode >= r->iface->nRequests) {
            protocolError(*r, wl_display::error_::invalid_method, "invalid method " + std::to_string(opcode));
            break;
        }
        const Message& m = r->iface->requests[opcode];
        if (m.since > r->version) {
            protocolError(*r, wl_display::error_::invalid_method,
                          std::string(m.name) + " needs a newer interface version");
            break;
        }
        Args args;
        if (!parseArgs(m.sig, payload, payloadLen, inFds_, args)) {
            protocolError(*r, wl_display::error_::invalid_method, std::string("bad arguments to ") + m.name);
            break;
        }
        if (server.trace()) {
            LOGD("wl[%d] <- %s@%u.%s(%s)", index_, r->iface->name, id, m.name,
                 formatArgs(m.sig, args).c_str());
        }
        r->request(opcode, args);
        for (Value& v : args) {  // descriptors the handler did not take
            if (v.t == 'h' && v.fd >= 0) close(v.fd);
        }
        if (m.destructor && !dead_) destroy(id);
    }
    in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(pos));
}

// ---------------------------------------------------------------------------------------------
// wl_display, wl_registry
// ---------------------------------------------------------------------------------------------

void DisplayRes::request(uint16_t op, Args& a) {
    switch (op) {
        case wl_display::sync: {
            const uint32_t cb = a[0].u;
            client.create<CallbackRes>(cb, 1)->send(wl_callback::done, {Value::U(client.server.nextSerial())});
            client.destroy(cb);
            break;
        }
        case wl_display::get_registry: {
            RegistryRes* reg = client.create<RegistryRes>(a[0].u, 1);
            for (const Global& g : client.server.globals()) {
                reg->send(wl_registry::global, {Value::U(g.name), Value::S(g.iface), Value::U(g.version)});
            }
            break;
        }
    }
}

void RegistryRes::request(uint16_t op, Args& a) {
    if (op != wl_registry::bind) return;
    const uint32_t name = a[0].u;
    const std::string& ifaceName = a[1].s;
    const uint32_t ver = a[1].version, newId = a[1].u;
    for (const Global& g : client.server.globals()) {
        if (g.name != name) continue;
        if (ifaceName != g.iface || ver == 0 || ver > g.version) {
            client.protocolError(*this, wl_display::error_::invalid_object,
                                 "cannot bind " + ifaceName + " at version " + std::to_string(ver));
            return;
        }
        g.bind(client, newId, ver);
        return;
    }
    client.protocolError(*this, wl_display::error_::invalid_object, "no global " + std::to_string(name));
}

// ---------------------------------------------------------------------------------------------
// Seat, pointer, keyboard, output
// ---------------------------------------------------------------------------------------------

SeatRes::SeatRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_seat", v) {
    send(wl_seat::capabilities, {Value::U(wl_seat::capability_::pointer | wl_seat::capability_::keyboard)});
    if (v >= 2) send(wl_seat::name, {Value::S("seat0")});
}

void SeatRes::request(uint16_t op, Args& a) {
    switch (op) {
        case wl_seat::get_pointer:
            client.create<PointerRes>(a[0].u, version);
            break;
        case wl_seat::get_keyboard:
            client.create<KeyboardRes>(a[0].u, version);
            break;
        case wl_seat::get_touch:
            client.protocolError(*this, wl_seat::error_::missing_capability, "this seat has no touch device");
            break;
        default:
            break;
    }
}

PointerRes::PointerRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_pointer", v) {
    c.server.seat().pointers.push_back(this);
}

PointerRes::~PointerRes() {
    auto& v = client.server.seat().pointers;
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
}

KeyboardRes::KeyboardRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_keyboard", v) {
    Server::SeatState& st = c.server.seat();
    st.keyboards.push_back(this);
    if (st.keymapFd >= 0) {
        send(wl_keyboard::keymap, {Value::U(wl_keyboard::keymap_format_::xkb_v1), Value::H(st.keymapFd),
                                   Value::U(st.keymapSize)});
    }
    if (v >= 4) send(wl_keyboard::repeat_info, {Value::I(25), Value::I(400)});
    if (st.keyboardFocus && &st.keyboardFocus->client == &c) {
        send(wl_keyboard::enter, {Value::U(c.server.nextSerial()), Value::O(st.keyboardFocus->id),
                                  Value::A(keyArray(st.pressed))});
        send(wl_keyboard::modifiers, {Value::U(c.server.nextSerial()), Value::U(heldModifiers(st.pressed)),
                                      Value::U(0), Value::U(st.modsLocked), Value::U(0)});
    }
}

KeyboardRes::~KeyboardRes() {
    auto& v = client.server.seat().keyboards;
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
}

OutputRes::OutputRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_output", v) {
    c.server.seat().outputs.push_back(this);
    sendInfo();
}

OutputRes::~OutputRes() {
    auto& v = client.server.seat().outputs;
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
}

void OutputRes::sendInfo() {
    const Server::SeatState& st = client.server.seat();
    const int w = st.outW * st.outScale, h = st.outH * st.outScale;
    // Report a physical size that works out to 96 dpi per logical pixel so toolkits keep their
    // default font sizes and rely on the integer scale for sharpness.
    const int mmW = static_cast<int>(std::lround(st.outW * 25.4 / 96.0));
    const int mmH = static_cast<int>(std::lround(st.outH * 25.4 / 96.0));
    send(wl_output::geometry, {Value::I(0), Value::I(0), Value::I(mmW), Value::I(mmH), Value::I(0),
                               Value::S("Plasma XR"), Value::S("Virtual display"), Value::I(0)});
    send(wl_output::mode, {Value::U(3), Value::I(w), Value::I(h), Value::I(60000)});
    if (version >= 2) send(wl_output::scale, {Value::I(st.outScale)});
    if (version >= 4) {
        send(wl_output::name, {Value::S("XR-1")});
        send(wl_output::description, {Value::S("Plasma XR virtual display")});
    }
    if (version >= 2) send(wl_output::done);
}

// ---------------------------------------------------------------------------------------------
// Clipboard (wl_data_device). Selection only; drag and drop is not supported.
// ---------------------------------------------------------------------------------------------

namespace {

void announceSelection(Server& srv) {
    Server::SeatState& st = srv.seat();
    if (!st.keyboardFocus) return;
    for (DataDeviceRes* d : st.dataDevices) {
        if (&d->client == &st.keyboardFocus->client) d->sendSelection(st.selection);
    }
}

}  // namespace

void DataDeviceManagerRes::request(uint16_t op, Args& a) {
    switch (op) {
        case wl_data_device_manager::create_data_source:
            client.create<DataSourceRes>(a[0].u, version);
            break;
        case wl_data_device_manager::get_data_device:
            client.create<DataDeviceRes>(a[0].u, version);
            break;
    }
}

DataSourceRes::~DataSourceRes() {
    for (DataOfferRes* o : offers) o->source = nullptr;
    Server::SeatState& st = client.server.seat();
    if (st.selection == this) {
        st.selection = nullptr;
        announceSelection(client.server);
    }
}

void DataSourceRes::request(uint16_t op, Args& a) {
    if (op == wl_data_source::offer) mimeTypes.push_back(a[0].s);
}

DataOfferRes::~DataOfferRes() {
    if (source) {
        auto& v = source->offers;
        v.erase(std::remove(v.begin(), v.end(), this), v.end());
    }
}

void DataOfferRes::request(uint16_t op, Args& a) {
    if (op != wl_data_offer::receive) return;
    if (source) source->send(wl_data_source::send, {Value::S(a[0].s), Value::H(a[1].fd)});
    if (source) source->client.flush();
    close(a[1].fd);  // the queue duplicated it for the source's client
    a[1].fd = -1;
}

DataDeviceRes::DataDeviceRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_data_device", v) {
    c.server.seat().dataDevices.push_back(this);
}

DataDeviceRes::~DataDeviceRes() {
    auto& v = client.server.seat().dataDevices;
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
}

void DataDeviceRes::sendSelection(DataSourceRes* src) {
    if (!src) {
        send(wl_data_device::selection, {Value::O(0)});
        return;
    }
    const uint32_t offerId = client.nextServerId();
    DataOfferRes* offer = client.create<DataOfferRes>(offerId, version);
    offer->source = src;
    src->offers.push_back(offer);
    send(wl_data_device::data_offer, {Value::N(offerId)});
    for (const std::string& mime : src->mimeTypes) offer->send(wl_data_offer::offer, {Value::S(mime)});
    send(wl_data_device::selection, {Value::O(offerId)});
}

void DataDeviceRes::request(uint16_t op, Args& a) {
    if (op != wl_data_device::set_selection) return;
    DataSourceRes* src = a[0].isNull ? nullptr : client.getAs<DataSourceRes>(a[0].u);
    Server::SeatState& st = client.server.seat();
    if (st.selection == src) return;
    if (st.selection) st.selection->send(wl_data_source::cancelled);
    st.selection = src;
    announceSelection(client.server);
}

// ---------------------------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------------------------

Server::Server() : seat_(std::make_unique<SeatState>()) {}

Server::~Server() { stop(); }

bool Server::start(const std::string& dir, const std::string& name) {
    stop();
    mkdir(dir.c_str(), 0777);
    path_ = dir + "/" + name;
    sockaddr_un addr{};
    if (path_.size() >= sizeof(addr.sun_path)) {
        LOGE("Wayland: socket path too long: %s", path_.c_str());
        return false;
    }
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        LOGE("Wayland: socket(): %s", strerror(errno));
        return false;
    }
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path_.c_str(), path_.size() + 1);
    unlink(path_.c_str());
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || listen(fd, 16) < 0) {
        LOGE("Wayland: cannot listen on %s: %s", path_.c_str(), strerror(errno));
        close(fd);
        return false;
    }
    chmod(path_.c_str(), 0777);
    listenFd_ = fd;

    seat_->keymapFd = makeKeymapFd(kKeymapUs, dir, &seat_->keymapSize);
    if (seat_->keymapFd < 0) LOGW("Wayland: could not create the keymap file; keyboards will not work");

    registerGlobals();
    LOGI("Wayland: listening on %s", path_.c_str());
    return true;
}

void Server::stop() {
    clients_.clear();
    globals_.clear();
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
        unlink(path_.c_str());
    }
    if (seat_ && seat_->keymapFd >= 0) {
        close(seat_->keymapFd);
        seat_->keymapFd = -1;
    }
}

void Server::registerGlobals() {
    uint32_t n = 0;
    auto add = [&](const char* iface, uint32_t version, std::function<void(Client&, uint32_t, uint32_t)> bind) {
        globals_.push_back({++n, iface, version, std::move(bind)});
    };
    add("wl_compositor", 5, [](Client& c, uint32_t id, uint32_t v) { c.create<CompositorRes>(id, v); });
    add("wl_subcompositor", 1, [](Client& c, uint32_t id, uint32_t v) { c.create<SubcompositorRes>(id, v); });
    add("wl_shm", 2, [](Client& c, uint32_t id, uint32_t v) { c.create<ShmRes>(id, v); });
    add("wl_output", 4, [](Client& c, uint32_t id, uint32_t v) { c.create<OutputRes>(id, v); });
    add("wl_seat", 9, [](Client& c, uint32_t id, uint32_t v) { c.create<SeatRes>(id, v); });
    add("wl_data_device_manager", 3,
        [](Client& c, uint32_t id, uint32_t v) { c.create<DataDeviceManagerRes>(id, v); });
    add("xdg_wm_base", 6, [](Client& c, uint32_t id, uint32_t v) { c.create<XdgWmBaseRes>(id, v); });
    add("zxdg_decoration_manager_v1", 1,
        [](Client& c, uint32_t id, uint32_t v) { c.create<DecorationManagerRes>(id, v); });
}

uint32_t Server::timeMs() const {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

void Server::acceptClients() {
    for (;;) {
        const int fd = accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0) break;
        clients_.push_back(std::make_unique<Client>(*this, fd));
        LOGI("Wayland: client %d connected", clients_.back()->index());
    }
}

void Server::dispatch() {
    if (listenFd_ < 0) return;
    acceptClients();
    for (size_t i = 0; i < clients_.size();) {
        Client& c = *clients_[i];
        const bool alive = c.onReadable();
        c.flush();
        if (!alive || c.dead()) {
            LOGI("Wayland: client %d disconnected", c.index());
            clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void Server::frameDone() {
    const uint32_t t = timeMs();
    for (SurfaceRes* s : surfaces_) s->fireFrameCallbacks(t);
    for (auto& c : clients_) c->flush();
}

void Server::forgetClient(Client*) {}

void Server::unregisterSurface(SurfaceRes* s) {
    surfaces_.erase(std::remove(surfaces_.begin(), surfaces_.end(), s), surfaces_.end());
}

void Server::setOutput(int logicalW, int logicalH, int scale) {
    seat_->outW = std::max(320, logicalW);
    seat_->outH = std::max(240, logicalH);
    seat_->outScale = std::max(1, std::min(4, scale));
    for (OutputRes* o : seat_->outputs) o->sendInfo();
    for (auto& c : clients_) c->flush();
}

int Server::outputScale() const { return seat_->outScale; }

// ---------------------------------------------------------------------------------------------
// The shell's view of toplevels
// ---------------------------------------------------------------------------------------------

void Server::addToplevel(Toplevel* t) {
    toplevels_.push_back(t);
    ++revision_;
}

void Server::removeToplevel(Toplevel* t) {
    toplevels_.erase(std::remove(toplevels_.begin(), toplevels_.end(), t), toplevels_.end());
    ++revision_;
}

std::vector<Toplevel*> Server::toplevels() { return toplevels_; }

Toplevel* Server::findToplevel(uint32_t uid) {
    for (Toplevel* t : toplevels_) {
        if (t->uid == uid) return t;
    }
    return nullptr;
}

void Server::requestClose(uint32_t uid) {
    Toplevel* t = findToplevel(uid);
    if (t && t->res) {
        t->res->send(xdg_toplevel::close);
        t->res->client.flush();
    }
}

void Server::configureToplevel(uint32_t uid, int width, int height, bool activated, bool resizing,
                               bool maximized) {
    Toplevel* t = findToplevel(uid);
    if (!t || !t->res) return;
    XdgToplevelRes* r = t->res;
    r->reqW = std::max(0, width);
    r->reqH = std::max(0, height);
    r->activated = activated;
    r->resizing = resizing;
    r->maximized = maximized;
    r->sendConfigure(false);
    r->client.flush();
}

// Surfaces of one toplevel with their offsets from the main surface, back to front.
namespace {

struct Placed {
    SurfaceRes* s;
    float x, y;
    bool popup;
};

void collectTree(SurfaceRes* root, float ox, float oy, bool popup, std::vector<Placed>& out) {
    for (SurfaceRes* c : root->below) {
        if (c->sub) collectTree(c, ox + c->sub->x, oy + c->sub->y, popup, out);
    }
    if (root->hasBuffer) out.push_back({root, ox, oy, popup});
    for (SurfaceRes* c : root->above) {
        if (c->sub) collectTree(c, ox + c->sub->x, oy + c->sub->y, popup, out);
    }
}

void collectToplevel(Server::SeatState& st, Toplevel* t, std::vector<Placed>& out) {
    if (!t || !t->surface) return;
    collectTree(t->surface, 0, 0, false, out);
    for (XdgPopupRes* p : st.popups) {
        if (p->dismissed || !p->xs->surface || !p->xs->surface->hasBuffer) continue;
        float ox = 0, oy = 0;
        if (popupOriginOf(*p, t->res, &ox, &oy)) collectTree(p->xs->surface, ox, oy, true, out);
    }
}

}  // namespace

void Server::visuals(uint32_t toplevelUid, std::vector<Visual>& out) {
    out.clear();
    std::vector<Placed> placed;
    collectToplevel(*seat_, findToplevel(toplevelUid), placed);
    for (const Placed& p : placed) {
        Visual v;
        v.surfaceUid = p.s->uid;
        v.x = p.x;
        v.y = p.y;
        v.w = static_cast<float>(p.s->logicalW());
        v.h = static_cast<float>(p.s->logicalH());
        v.bufferW = p.s->bufW;
        v.bufferH = p.s->bufH;
        v.scale = p.s->scale;
        v.hasAlpha = p.s->hasAlpha;
        v.popup = p.popup;
        out.push_back(v);
    }
}

void Server::dismissPopups(uint32_t toplevelUid) {
    Toplevel* t = findToplevel(toplevelUid);
    if (!t) return;
    std::vector<XdgPopupRes*> list = seat_->popups;
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        XdgPopupRes* p = *it;
        float ox, oy;
        if (p->grab && !p->dismissed && popupOriginOf(*p, t->res, &ox, &oy)) p->done();
    }
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

namespace {

void setPointerFocus(Server& srv, SurfaceRes* s, double lx, double ly) {
    Server::SeatState& st = srv.seat();
    if (st.pointerFocus == s) return;
    if (st.pointerFocus) {
        const uint32_t serial = srv.nextSerial();
        for (PointerRes* p : st.pointers) {
            if (&p->client != &st.pointerFocus->client) continue;
            p->send(wl_pointer::leave, {Value::U(serial), Value::O(st.pointerFocus->id)});
            p->frame();
        }
    }
    st.pointerFocus = s;
    st.lastSentX = st.lastSentY = -1e9;
    st.buttonsDown = s ? st.buttonsDown : 0;
    if (!s) return;
    const uint32_t serial = srv.nextSerial();
    for (PointerRes* p : st.pointers) {
        if (&p->client != &s->client) continue;
        p->send(wl_pointer::enter, {Value::U(serial), Value::O(s->id), Value::F(lx), Value::F(ly)});
        p->frame();
    }
    st.lastSentX = lx;
    st.lastSentY = ly;
}

}  // namespace

void Server::pointerMotion(uint32_t toplevelUid, double x, double y) {
    SeatState& st = *seat_;
    Toplevel* t = findToplevel(toplevelUid);
    std::vector<Placed> placed;
    collectToplevel(st, t, placed);
    const Placed* target = nullptr;
    if (st.buttonsDown > 0 && st.pointerFocus) {  // implicit grab while a button is held
        for (const Placed& p : placed) {
            if (p.s == st.pointerFocus) target = &p;
        }
        if (!target) return;
    } else {
        for (auto it = placed.rbegin(); it != placed.rend(); ++it) {
            const float w = static_cast<float>(it->s->logicalW()), h = static_cast<float>(it->s->logicalH());
            if (x >= it->x && y >= it->y && x < it->x + w && y < it->y + h) {
                target = &*it;
                break;
            }
        }
    }
    if (!target) {
        pointerLeave();
        return;
    }
    const double lx = x - target->x, ly = y - target->y;
    setPointerFocus(*this, target->s, lx, ly);
    if (std::fabs(lx - st.lastSentX) < 0.01 && std::fabs(ly - st.lastSentY) < 0.01) return;
    st.lastSentX = lx;
    st.lastSentY = ly;
    const uint32_t time = timeMs();
    for (PointerRes* p : st.pointers) {
        if (&p->client != &target->s->client) continue;
        p->send(wl_pointer::motion, {Value::U(time), Value::F(lx), Value::F(ly)});
        p->frame();
    }
}

void Server::pointerLeave() {
    SeatState& st = *seat_;
    st.buttonsDown = 0;
    setPointerFocus(*this, nullptr, 0, 0);
}

void Server::pointerButton(uint32_t button, bool pressed) {
    SeatState& st = *seat_;
    SurfaceRes* s = st.pointerFocus;
    if (!s) return;
    if (pressed) ++st.buttonsDown;
    else if (st.buttonsDown > 0) --st.buttonsDown;
    const uint32_t serial = nextSerial(), time = timeMs();
    for (PointerRes* p : st.pointers) {
        if (&p->client != &s->client) continue;
        p->send(wl_pointer::button, {Value::U(serial), Value::U(time), Value::U(button),
                                     Value::U(pressed ? wl_pointer::button_state_::pressed
                                                      : wl_pointer::button_state_::released)});
        p->frame();
    }
}

void Server::pointerAxis(double vertical, double horizontal) {
    SeatState& st = *seat_;
    SurfaceRes* s = st.pointerFocus;
    if (!s || (vertical == 0 && horizontal == 0)) return;
    const uint32_t time = timeMs();
    for (PointerRes* p : st.pointers) {
        if (&p->client != &s->client) continue;
        if (p->version >= 5) p->send(wl_pointer::axis_source, {Value::U(wl_pointer::axis_source_::continuous)});
        if (vertical != 0) {
            p->send(wl_pointer::axis, {Value::U(time), Value::U(wl_pointer::axis_::vertical_scroll), Value::F(vertical)});
        }
        if (horizontal != 0) {
            p->send(wl_pointer::axis, {Value::U(time), Value::U(wl_pointer::axis_::horizontal_scroll), Value::F(horizontal)});
        }
        p->frame();
    }
}

void Server::setKeyboardFocus(uint32_t toplevelUid) {
    SeatState& st = *seat_;
    Toplevel* t = toplevelUid ? findToplevel(toplevelUid) : nullptr;
    SurfaceRes* s = t ? t->surface : nullptr;
    st.keyboardToplevel = s ? toplevelUid : 0;
    if (s == st.keyboardFocus) return;
    if (st.keyboardFocus) {
        const uint32_t serial = nextSerial();
        for (KeyboardRes* k : st.keyboards) {
            if (&k->client == &st.keyboardFocus->client) {
                k->send(wl_keyboard::leave, {Value::U(serial), Value::O(st.keyboardFocus->id)});
            }
        }
    }
    st.keyboardFocus = s;
    if (!s) return;
    const uint32_t serial = nextSerial();
    for (KeyboardRes* k : st.keyboards) {
        if (&k->client != &s->client) continue;
        k->send(wl_keyboard::enter, {Value::U(serial), Value::O(s->id), Value::A(keyArray(st.pressed))});
        k->send(wl_keyboard::modifiers, {Value::U(nextSerial()), Value::U(heldModifiers(st.pressed)),
                                         Value::U(0), Value::U(st.modsLocked), Value::U(0)});
    }
    for (DataDeviceRes* d : st.dataDevices) {
        if (&d->client == &s->client) d->sendSelection(st.selection);
    }
    s->client.flush();
}

uint32_t Server::keyboardFocusUid() const { return seat_->keyboardToplevel; }

void Server::key(uint32_t evdevKey, bool pressed) {
    SeatState& st = *seat_;
    const uint32_t before = heldModifiers(st.pressed) | st.modsLocked;
    if (pressed) {
        const bool repeat = st.pressed.count(evdevKey) != 0;
        st.pressed.insert(evdevKey);
        if (!repeat) {
            if (evdevKey == 58) st.modsLocked ^= kModLock;      // Caps Lock
            else if (evdevKey == 69) st.modsLocked ^= kModNum;  // Num Lock
        }
    } else {
        st.pressed.erase(evdevKey);
    }
    SurfaceRes* s = st.keyboardFocus;
    if (!s) return;
    const uint32_t after = heldModifiers(st.pressed) | st.modsLocked;
    const uint32_t time = timeMs(), serial = nextSerial();
    for (KeyboardRes* k : st.keyboards) {
        if (&k->client != &s->client) continue;
        k->send(wl_keyboard::key, {Value::U(serial), Value::U(time), Value::U(evdevKey),
                                   Value::U(pressed ? wl_keyboard::key_state_::pressed
                                                    : wl_keyboard::key_state_::released)});
        if (after != before) {
            k->send(wl_keyboard::modifiers, {Value::U(nextSerial()), Value::U(heldModifiers(st.pressed)),
                                             Value::U(0), Value::U(st.modsLocked), Value::U(0)});
        }
    }
    s->client.flush();
}

}  // namespace wl
}  // namespace pxr
