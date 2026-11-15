// A small Wayland compositor for Linux apps running in the proot rootfs. It speaks the wire
// protocol directly (see wire.h) and exposes each xdg_toplevel to the shell, which turns it into a
// window. It knows nothing about GL or OpenXR: pixels go to a Presenter and input comes from the
// shell. Everything runs on one thread: dispatch() is called once per frame.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "wayland/protocol_gen.h"
#include "wayland/wire.h"

namespace pxr {
namespace wl {

class Server;
class Client;
class SurfaceRes;
class XdgToplevelRes;

class Resource {
public:
    Resource(Client& c, uint32_t id, const char* ifaceName, uint32_t version);
    virtual ~Resource() = default;
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;

    virtual void request(uint16_t opcode, Args& args) = 0;
    void send(uint16_t event, Args args = {});

    Client& client;
    uint32_t id;
    const Interface* iface;
    uint32_t version;
    uint64_t seq = 0;  // creation order, so a dying client tears objects down newest first
};

// Receives surface pixels (wl_shm contents, already copied out of the client's memory).
class Presenter {
public:
    virtual ~Presenter() = default;
    // A surface's buffer changed. `pixels` points at the buffer's first row, `stride` is in bytes
    // and the format is ARGB8888 or XRGB8888 (little-endian, i.e. B,G,R,A in memory) with
    // premultiplied alpha. Only the box x,y,w,h changed unless `full` is set (new size).
    virtual void update(uint32_t surfaceUid, int width, int height, int stride, bool hasAlpha,
                        const uint8_t* pixels, int x, int y, int w, int h, bool full) = 0;
    virtual void remove(uint32_t surfaceUid) = 0;
};

// One drawable piece of a window: the main surface, a subsurface or a popup. Positions are in
// logical pixels relative to the toplevel's main surface.
struct Visual {
    uint32_t surfaceUid = 0;
    float x = 0, y = 0;
    float w = 0, h = 0;  // logical size
    int bufferW = 0, bufferH = 0;
    int scale = 1;
    bool hasAlpha = true;
    bool popup = false;
};

// What the shell sees of an xdg_toplevel. Owned by the protocol object; look it up by uid each
// frame rather than keeping the pointer.
struct Toplevel {
    uint32_t uid = 0;
    std::string title;
    std::string appId;
    bool mapped = false;   // the client has committed a buffer
    bool visible = true;   // set by the shell; hidden windows get no frame callbacks
    bool dirty = false;    // new pixels arrived since the shell last cleared this
    uint32_t surfaceUid = 0;
    int bufferW = 0, bufferH = 0;  // pixels
    int scale = 1;
    int width = 0, height = 0;     // logical size of the main surface
    int minW = 0, minH = 0, maxW = 0, maxH = 0;  // client hints, logical, 0 = none
    uint32_t parentUid = 0;        // set_parent target (dialogs), 0 = none
    bool wantsMaximize = false;    // the client asked via set_maximized since last cleared
    bool wantsMinimize = false;

    SurfaceRes* surface = nullptr;
    XdgToplevelRes* res = nullptr;
};

class Client {
public:
    Client(Server& server, int fd);
    ~Client();

    bool onReadable();  // false when the peer is gone
    void flush();
    Resource* get(uint32_t id) const;
    template <class T>
    T* getAs(uint32_t id) const {
        return dynamic_cast<T*>(get(id));
    }
    template <class T, class... A>
    T* create(uint32_t id, uint32_t version, A&&... a) {
        auto r = std::make_unique<T>(*this, id, version, std::forward<A>(a)...);
        T* raw = r.get();
        raw->seq = ++seq_;
        objects_[id] = std::move(r);
        return raw;
    }
    // Forgets the object and tells the client its id can be reused.
    void destroy(uint32_t id);
    void protocolError(Resource& r, uint32_t code, const std::string& msg);
    void sendMessage(Resource& r, uint16_t opcode, const Args& args);
    bool dead() const { return dead_; }
    int index() const { return index_; }  // for logs
    // Ids for objects the server creates (data offers) live above the client's range.
    uint32_t nextServerId() { return 0xff000000u + (++serverIds_); }
    std::vector<SurfaceRes*>& surfaces() { return surfaces_; }

    Server& server;

private:
    void dispatchAll();
    void queue(const std::vector<uint8_t>& bytes, const std::vector<int>& fds);

    struct OutFds {
        size_t offset;  // position in out_ of the message that carries the descriptors
        std::vector<int> fds;
    };

    int fd_;
    std::vector<uint8_t> in_;
    std::deque<int> inFds_;
    std::vector<uint8_t> out_;
    std::deque<OutFds> outFds_;
    std::unordered_map<uint32_t, std::unique_ptr<Resource>> objects_;
    std::vector<SurfaceRes*> surfaces_;
    uint64_t seq_ = 0;
    uint32_t serverIds_ = 0;
    int index_ = 0;
    bool dead_ = false;
};

struct Global {
    uint32_t name;
    const char* iface;
    uint32_t version;
    std::function<void(Client&, uint32_t id, uint32_t version)> bind;
};

class Server {
public:
    Server();
    ~Server();

    // Creates <dir>/<name> and starts listening; a stale socket is replaced.
    bool start(const std::string& dir, const std::string& name);
    void stop();
    bool running() const { return listenFd_ >= 0; }
    const std::string& socketPath() const { return path_; }

    void setPresenter(Presenter* p) { presenter_ = p; }
    Presenter* presenter() const { return presenter_; }
    // Logs every request and event (like WAYLAND_DEBUG=1).
    void setTrace(bool on) { trace_ = on; }
    bool trace() const { return trace_; }

    // The virtual monitor clients lay themselves out on. `scale` is the integer buffer scale they
    // are told to render at (2 keeps text sharp at the shell's pixel density).
    void setOutput(int logicalW, int logicalH, int scale);
    int outputScale() const;

    // Accepts connections, reads requests and flushes events. Call once per frame.
    void dispatch();
    // Fires the frame callbacks clients are waiting on (call after the frame was submitted).
    void frameDone();

    // --- the shell's view -----------------------------------------------------------------
    std::vector<Toplevel*> toplevels();
    Toplevel* findToplevel(uint32_t uid);
    void visuals(uint32_t toplevelUid, std::vector<Visual>& out);
    size_t clientCount() const { return clients_.size(); }
    uint64_t revision() const { return revision_; }  // bumps when toplevels appear, map or vanish
    void bumpRevision() { ++revision_; }

    void requestClose(uint32_t uid);
    // Asks the client to resize its window (logical pixels, 0 = client's choice).
    void configureToplevel(uint32_t uid, int width, int height, bool activated,
                           bool resizing = false, bool maximized = false);
    // Closes the menus a toplevel has open (the user clicked somewhere else).
    void dismissPopups(uint32_t toplevelUid);

    // --- input (one seat: a pointer and a keyboard) -------------------------------------------
    // Coordinates are logical pixels relative to the toplevel's main surface; popups and
    // subsurfaces are hit-tested here.
    void pointerMotion(uint32_t toplevelUid, double x, double y);
    void pointerLeave();
    void pointerButton(uint32_t evdevButton, bool pressed);
    void pointerAxis(double vertical, double horizontal = 0);
    void setKeyboardFocus(uint32_t toplevelUid);  // 0 = none
    void key(uint32_t evdevKey, bool pressed);
    uint32_t keyboardFocusUid() const;

    // --- internals shared with the resource implementations ---------------------------------
    uint32_t nextSerial() { return ++serial_; }
    uint32_t timeMs() const;
    const std::vector<Global>& globals() const { return globals_; }
    uint32_t nextToplevelUid() { return ++toplevelUid_; }
    uint32_t nextSurfaceUid() { return ++surfaceUid_; }
    void addToplevel(Toplevel* t);
    void removeToplevel(Toplevel* t);
    void registerSurface(SurfaceRes* s) { surfaces_.push_back(s); }
    void unregisterSurface(SurfaceRes* s);
    void forgetClient(Client* c);

    struct SeatState;
    SeatState& seat() { return *seat_; }

private:
    void registerGlobals();
    void acceptClients();

    int listenFd_ = -1;
    std::string path_;
    std::vector<std::unique_ptr<Client>> clients_;
    std::vector<Global> globals_;
    std::vector<Toplevel*> toplevels_;
    std::vector<SurfaceRes*> surfaces_;
    Presenter* presenter_ = nullptr;
    bool trace_ = false;
    uint32_t serial_ = 0;
    uint32_t toplevelUid_ = 0;
    uint32_t surfaceUid_ = 0;
    uint64_t revision_ = 1;
    std::unique_ptr<SeatState> seat_;
};

}  // namespace wl
}  // namespace pxr
