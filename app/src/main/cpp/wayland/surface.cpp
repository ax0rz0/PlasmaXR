#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

#include "core/log.h"
#include "wayland/internal.h"

namespace pxr {
namespace wl {

// ---------------------------------------------------------------------------------------------
// Shared memory
// ---------------------------------------------------------------------------------------------

ShmPool::~ShmPool() {
    if (data) munmap(data, size);
    if (fd >= 0) close(fd);
}

bool ShmPool::map(size_t newSize) {
    if (data) {
        munmap(data, size);
        data = nullptr;
        size = 0;
    }
    void* p = mmap(nullptr, newSize, PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) return false;
    data = static_cast<uint8_t*>(p);
    size = newSize;
    return true;
}

ShmRes::ShmRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_shm", v) {
    send(wl_shm::format, {Value::U(wl_shm::format_::argb8888)});
    send(wl_shm::format, {Value::U(wl_shm::format_::xrgb8888)});
}

void ShmRes::request(uint16_t op, Args& a) {
    if (op != wl_shm::create_pool) return;
    const int size = a[2].i;
    auto pool = std::make_shared<ShmPool>();
    pool->fd = a[1].fd;  // the pool owns the descriptor from here on
    a[1].fd = -1;
    if (size <= 0 || !pool->map(static_cast<size_t>(size))) {
        client.protocolError(*this, wl_shm::error_::invalid_fd, "cannot map the shm pool");
        return;
    }
    client.create<ShmPoolRes>(a[0].u, version, pool);
}

void ShmPoolRes::request(uint16_t op, Args& a) {
    switch (op) {
        case wl_shm_pool::create_buffer: {
            auto d = std::make_shared<BufferData>();
            d->pool = pool_;
            d->offset = a[1].i;
            d->width = a[2].i;
            d->height = a[3].i;
            d->stride = a[4].i;
            d->format = a[5].u;
            d->resId = a[0].u;
            const bool fmt = d->format == kShmArgb8888 || d->format == kShmXrgb8888;
            const long long end = static_cast<long long>(d->offset) +
                                  static_cast<long long>(d->stride) * d->height;
            if (!fmt || d->width <= 0 || d->height <= 0 || d->offset < 0 ||
                d->stride < d->width * 4 || end > static_cast<long long>(pool_->size)) {
                client.protocolError(*this, wl_shm::error_::invalid_stride, "invalid buffer parameters");
                return;
            }
            client.create<BufferRes>(a[0].u, version, d);
            break;
        }
        case wl_shm_pool::resize:
            if (a[0].i < 0 || static_cast<size_t>(a[0].i) < pool_->size ||
                !pool_->map(static_cast<size_t>(a[0].i))) {
                client.protocolError(*this, wl_shm::error_::invalid_fd, "cannot resize the shm pool");
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------------------------
// wl_compositor and wl_surface
// ---------------------------------------------------------------------------------------------

void CompositorRes::request(uint16_t op, Args& a) {
    if (op == wl_compositor::create_surface) client.create<SurfaceRes>(a[0].u, version);
    else if (op == wl_compositor::create_region) client.create<RegionRes>(a[0].u, version);
}

SurfaceRes::SurfaceRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_surface", v) {
    uid = c.server.nextSurfaceUid();
    c.server.registerSurface(this);
    c.surfaces().push_back(this);
}

SurfaceRes::~SurfaceRes() {
    Server& srv = client.server;
    Server::SeatState& st = srv.seat();
    if (st.pointerFocus == this) {
        st.pointerFocus = nullptr;
        st.buttonsDown = 0;
    }
    if (st.keyboardFocus == this) {
        st.keyboardFocus = nullptr;
        st.keyboardToplevel = 0;
    }
    if (hasBuffer && srv.presenter()) srv.presenter()->remove(uid);
    for (SurfaceRes* c : below) {
        if (c->sub) c->sub->parent = nullptr;
    }
    for (SurfaceRes* c : above) {
        if (c->sub) c->sub->parent = nullptr;
    }
    if (xdg) xdg->surface = nullptr;
    if (sub) sub->surface = nullptr;
    srv.unregisterSurface(this);
    auto& mine = client.surfaces();
    mine.erase(std::remove(mine.begin(), mine.end(), this), mine.end());
}

Toplevel* SurfaceRes::toplevel(int depth) const {
    if (depth > 8) return nullptr;
    if (xdg && xdg->toplevel) return &xdg->toplevel->top;
    if (xdg && xdg->popup && xdg->popup->parent && xdg->popup->parent->surface) {
        return xdg->popup->parent->surface->toplevel(depth + 1);
    }
    if (sub && sub->parent) return sub->parent->toplevel(depth + 1);
    return nullptr;
}

bool SurfaceRes::visible() const {
    const Toplevel* t = toplevel();
    return t ? t->visible : true;
}

void SurfaceRes::fireFrameCallbacks(uint32_t timeMs) {
    if (frameCbs.empty() || !visible()) return;
    std::vector<uint32_t> ids;
    ids.swap(frameCbs);
    for (uint32_t cb : ids) {
        if (CallbackRes* r = client.getAs<CallbackRes>(cb)) r->send(wl_callback::done, {Value::U(timeMs)});
        client.destroy(cb);
    }
}

void SurfaceRes::enterOutputs() {
    enteredOutput = true;
    for (OutputRes* o : client.server.seat().outputs) {
        if (&o->client == &client) send(wl_surface::enter, {Value::O(o->id)});
    }
}

void touchToplevel(SurfaceRes& s) {
    if (Toplevel* t = s.toplevel()) t->dirty = true;
}

void SurfaceRes::request(uint16_t op, Args& a) {
    switch (op) {
        case wl_surface::attach: {
            pAttached = true;
            pBuffer.reset();
            if (!a[0].isNull) {
                if (BufferRes* b = client.getAs<BufferRes>(a[0].u)) pBuffer = b->data;
            }
            break;
        }
        case wl_surface::damage:
            pSurfaceDamage.push_back({a[0].i, a[1].i, a[2].i, a[3].i});
            break;
        case wl_surface::damage_buffer:
            pBufferDamage.push_back({a[0].i, a[1].i, a[2].i, a[3].i});
            break;
        case wl_surface::frame:
            client.create<CallbackRes>(a[0].u, 1);
            pFrames.push_back(a[0].u);
            break;
        case wl_surface::set_buffer_scale:
            if (a[0].i < 1) client.protocolError(*this, wl_surface::error_::invalid_scale, "buffer scale below 1");
            else pScale = a[0].i;
            break;
        case wl_surface::commit:
            commit();
            break;
        default:  // regions, transforms and offsets are not used
            break;
    }
}

bool SurfaceRes::upload(const BufferData& b) {
    Server& srv = client.server;
    const bool alpha = b.format == kShmArgb8888;
    if (!b.pool || !b.pool->data ||
        static_cast<long long>(b.offset) + static_cast<long long>(b.stride) * b.height >
            static_cast<long long>(b.pool->size)) {
        client.protocolError(*this, 0, "buffer lies outside its pool");
        return false;
    }
    const bool resized = !hasBuffer || b.width != bufW || b.height != bufH || alpha != hasAlpha;
    Box dmg;
    if (resized || (pSurfaceDamage.empty() && pBufferDamage.empty())) {
        dmg = {0, 0, b.width, b.height};
    } else {
        for (const Box& s : pSurfaceDamage) {
            const long long k = scale;
            auto c = [&](int v) { return static_cast<int>(std::max<long long>(-(1 << 24), std::min<long long>(1 << 24, v * k))); };
            Box bb{c(s.x), c(s.y), c(s.w), c(s.h)};
            dmg.unite(bb.clipped(b.width, b.height));
        }
        for (const Box& s : pBufferDamage) dmg.unite(s.clipped(b.width, b.height));
    }
    if (Presenter* pr = srv.presenter()) {
        if (!dmg.empty()) {
            pr->update(uid, b.width, b.height, b.stride, alpha, b.pool->data + b.offset, dmg.x, dmg.y,
                       dmg.w, dmg.h, resized);
        }
    }
    bufW = b.width;
    bufH = b.height;
    hasAlpha = alpha;
    hasBuffer = true;
    // The pixels were copied out, so the client can reuse its buffer straight away.
    if (BufferRes* br = client.getAs<BufferRes>(b.resId)) br->send(wl_buffer::release);
    return true;
}

void SurfaceRes::commit() {
    Server& srv = client.server;
    scale = pScale;
    bool changed = false;
    if (pAttached) {
        pAttached = false;
        std::shared_ptr<BufferData> buf = std::move(pBuffer);
        pBuffer.reset();
        if (!buf) {
            if (hasBuffer) {
                hasBuffer = false;
                bufW = bufH = 0;
                if (srv.presenter()) srv.presenter()->remove(uid);
                changed = true;
            }
        } else if (upload(*buf)) {
            changed = true;
        } else {
            return;  // protocol error; the client is going away
        }
    }
    frameCbs.insert(frameCbs.end(), pFrames.begin(), pFrames.end());
    pFrames.clear();
    pSurfaceDamage.clear();
    pBufferDamage.clear();

    if (!enteredOutput && (xdg || sub)) enterOutputs();
    if (sub) {  // subsurfaces behave as if desynchronised: their position applies at once
        sub->x = sub->pendingX;
        sub->y = sub->pendingY;
    }
    for (SurfaceRes* c : below) {
        if (c->sub) {
            c->sub->x = c->sub->pendingX;
            c->sub->y = c->sub->pendingY;
        }
    }
    for (SurfaceRes* c : above) {
        if (c->sub) {
            c->sub->x = c->sub->pendingX;
            c->sub->y = c->sub->pendingY;
        }
    }
    if (xdg) xdg->onCommit();
    if (changed) touchToplevel(*this);
}

// ---------------------------------------------------------------------------------------------
// Subsurfaces
// ---------------------------------------------------------------------------------------------

void SubcompositorRes::request(uint16_t op, Args& a) {
    if (op != wl_subcompositor::get_subsurface) return;
    SurfaceRes* surf = client.getAs<SurfaceRes>(a[1].u);
    SurfaceRes* parent = client.getAs<SurfaceRes>(a[2].u);
    if (!surf || !parent || surf == parent || surf->sub) {
        client.protocolError(*this, wl_subcompositor::error_::bad_surface, "invalid subsurface request");
        return;
    }
    client.create<SubsurfaceRes>(a[0].u, version, surf, parent);
}

SubsurfaceRes::SubsurfaceRes(Client& c, uint32_t id, uint32_t v, SurfaceRes* s, SurfaceRes* p)
    : Resource(c, id, "wl_subsurface", v), surface(s), parent(p) {
    surface->sub = this;
    parent->above.push_back(surface);
}

SubsurfaceRes::~SubsurfaceRes() {
    if (parent) {
        parent->above.erase(std::remove(parent->above.begin(), parent->above.end(), surface), parent->above.end());
        parent->below.erase(std::remove(parent->below.begin(), parent->below.end(), surface), parent->below.end());
    }
    if (surface) surface->sub = nullptr;
}

void SubsurfaceRes::request(uint16_t op, Args& a) {
    switch (op) {
        case wl_subsurface::set_position:
            pendingX = a[0].i;
            pendingY = a[1].i;
            break;
        case wl_subsurface::place_above:
        case wl_subsurface::place_below: {
            if (!parent || !surface) break;
            SurfaceRes* sibling = client.getAs<SurfaceRes>(a[0].u);
            if (!sibling) break;
            auto& above = parent->above;
            auto& below = parent->below;
            above.erase(std::remove(above.begin(), above.end(), surface), above.end());
            below.erase(std::remove(below.begin(), below.end(), surface), below.end());
            const bool wantAbove = op == wl_subsurface::place_above;
            if (sibling == parent) {
                if (wantAbove) above.insert(above.begin(), surface);
                else below.push_back(surface);
                break;
            }
            for (auto* list : {&above, &below}) {
                auto it = std::find(list->begin(), list->end(), sibling);
                if (it == list->end()) continue;
                list->insert(wantAbove ? it + 1 : it, surface);
                return;
            }
            above.push_back(surface);  // not a sibling: leave it on top
            break;
        }
        default:  // set_sync / set_desync: all subsurfaces are treated as desynchronised
            break;
    }
}

// ---------------------------------------------------------------------------------------------
// xdg-shell
// ---------------------------------------------------------------------------------------------

void XdgWmBaseRes::request(uint16_t op, Args& a) {
    switch (op) {
        case xdg_wm_base::create_positioner:
            client.create<PositionerRes>(a[0].u, version);
            break;
        case xdg_wm_base::get_xdg_surface: {
            SurfaceRes* s = client.getAs<SurfaceRes>(a[1].u);
            if (!s) {
                client.protocolError(*this, xdg_wm_base::error_::role, "not a wl_surface");
                return;
            }
            if (s->xdg) {
                client.protocolError(*this, xdg_wm_base::error_::role, "wl_surface already has an xdg_surface");
                return;
            }
            client.create<XdgSurfaceRes>(a[0].u, version, s);
            break;
        }
        default:  // pong
            break;
    }
}

void PositionerRes::request(uint16_t op, Args& a) {
    switch (op) {
        case xdg_positioner::set_size:
            d.w = a[0].i;
            d.h = a[1].i;
            break;
        case xdg_positioner::set_anchor_rect:
            d.anchorRect = {a[0].i, a[1].i, a[2].i, a[3].i};
            break;
        case xdg_positioner::set_anchor:
            d.anchor = a[0].u;
            break;
        case xdg_positioner::set_gravity:
            d.gravity = a[0].u;
            break;
        case xdg_positioner::set_constraint_adjustment:
            d.adjust = a[0].u;
            break;
        case xdg_positioner::set_offset:
            d.offX = a[0].i;
            d.offY = a[1].i;
            break;
        default:
            break;
    }
}

XdgSurfaceRes::XdgSurfaceRes(Client& c, uint32_t id, uint32_t v, SurfaceRes* s)
    : Resource(c, id, "xdg_surface", v), surface(s) {
    surface->xdg = this;
}

XdgSurfaceRes::~XdgSurfaceRes() {
    if (surface) {
        surface->xdg = nullptr;
        if (surface->hasBuffer && !surface->sub) {
            // Losing the shell role unmaps the surface.
            surface->hasBuffer = false;
            if (client.server.presenter()) client.server.presenter()->remove(surface->uid);
        }
    }
}

void XdgSurfaceRes::sendConfigure() { send(xdg_surface::configure, {Value::U(client.server.nextSerial())}); }

void XdgSurfaceRes::request(uint16_t op, Args& a) {
    switch (op) {
        case xdg_surface::get_toplevel:
            if (toplevel || popup) {
                client.protocolError(*this, xdg_wm_base::error_::role, "xdg_surface already has a role");
                return;
            }
            toplevel = client.create<XdgToplevelRes>(a[0].u, version, this);
            break;
        case xdg_surface::get_popup: {
            if (toplevel || popup) {
                client.protocolError(*this, xdg_wm_base::error_::role, "xdg_surface already has a role");
                return;
            }
            XdgSurfaceRes* par = a[1].isNull ? nullptr : client.getAs<XdgSurfaceRes>(a[1].u);
            PositionerRes* pos = client.getAs<PositionerRes>(a[2].u);
            if (!par || !pos) {
                client.protocolError(*this, xdg_wm_base::error_::invalid_positioner, "popup needs a parent and a positioner");
                return;
            }
            popup = client.create<XdgPopupRes>(a[0].u, version, this, par, pos->d);
            break;
        }
        case xdg_surface::set_window_geometry:
            geometry = {a[0].i, a[1].i, a[2].i, a[3].i};
            hasGeometry = !geometry.empty();
            break;
        default:  // ack_configure
            break;
    }
}

void XdgSurfaceRes::onCommit() {
    if (!surface || (!toplevel && !popup)) return;
    if (!configured) {
        configured = true;
        if (toplevel) toplevel->sendConfigure(true);
        else popup->sendConfigure();
    }
    if (toplevel) {
        Toplevel& t = toplevel->top;
        t.bufferW = surface->bufW;
        t.bufferH = surface->bufH;
        t.scale = surface->scale;
        t.width = surface->logicalW();
        t.height = surface->logicalH();
        if (surface->hasBuffer != t.mapped) {
            t.mapped = surface->hasBuffer;
            LOGI("Wayland: toplevel %u \"%s\" (%s) %s, %dx%d px at scale %d", t.uid, t.title.c_str(),
                 t.appId.c_str(), t.mapped ? "mapped" : "unmapped", t.bufferW, t.bufferH, t.scale);
            client.server.bumpRevision();
        }
    }
    mapped = surface->hasBuffer;
}

XdgToplevelRes::XdgToplevelRes(Client& c, uint32_t id, uint32_t v, XdgSurfaceRes* x)
    : Resource(c, id, "xdg_toplevel", v), xs(x) {
    top.uid = c.server.nextToplevelUid();
    top.surface = xs->surface;
    top.surfaceUid = xs->surface->uid;
    top.res = this;
    c.server.addToplevel(&top);
}

XdgToplevelRes::~XdgToplevelRes() {
    Server& srv = client.server;
    Server::SeatState& st = srv.seat();
    if (st.keyboardToplevel == top.uid) {
        st.keyboardToplevel = 0;
        st.keyboardFocus = nullptr;
    }
    if (st.pointerFocus && st.pointerFocus == top.surface) {
        st.pointerFocus = nullptr;
        st.buttonsDown = 0;
    }
    srv.removeToplevel(&top);
    if (xs) xs->toplevel = nullptr;
}

void XdgToplevelRes::sendConfigure(bool force) {
    const uint32_t states = (activated ? 1u : 0u) | (resizing ? 2u : 0u) | (maximized ? 4u : 0u);
    if (!force && reqW == lastW && reqH == lastH && states == lastStates) return;
    lastW = reqW;
    lastH = reqH;
    lastStates = states;

    auto pack = [](std::initializer_list<uint32_t> values) {
        std::vector<uint8_t> out;
        for (uint32_t s : values) {
            const uint8_t* b = reinterpret_cast<const uint8_t*>(&s);
            out.insert(out.end(), b, b + 4);
        }
        return out;
    };
    if (version >= 5 && !capabilitiesSent) {
        capabilitiesSent = true;
        send(xdg_toplevel::wm_capabilities,
             {Value::A(pack({xdg_toplevel::wm_capabilities_::maximize, xdg_toplevel::wm_capabilities_::minimize}))});
    }
    std::vector<uint32_t> list;
    if (maximized) list.push_back(xdg_toplevel::state_::maximized);
    if (resizing) list.push_back(xdg_toplevel::state_::resizing);
    if (activated) list.push_back(xdg_toplevel::state_::activated);
    std::vector<uint8_t> arr;
    for (uint32_t s : list) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&s);
        arr.insert(arr.end(), b, b + 4);
    }
    send(xdg_toplevel::configure, {Value::I(reqW), Value::I(reqH), Value::A(arr)});
    if (decorationId) {
        if (DecorationRes* d = client.getAs<DecorationRes>(decorationId)) d->configure();
    }
    xs->sendConfigure();
}

void XdgToplevelRes::request(uint16_t op, Args& a) {
    switch (op) {
        case xdg_toplevel::set_parent: {
            top.parentUid = 0;
            if (!a[0].isNull) {
                if (XdgToplevelRes* p = client.getAs<XdgToplevelRes>(a[0].u)) top.parentUid = p->top.uid;
            }
            break;
        }
        case xdg_toplevel::set_title:
            top.title = a[0].s;
            break;
        case xdg_toplevel::set_app_id:
            top.appId = a[0].s;
            break;
        case xdg_toplevel::set_max_size:
            top.maxW = std::max(0, a[0].i);
            top.maxH = std::max(0, a[1].i);
            break;
        case xdg_toplevel::set_min_size:
            top.minW = std::max(0, a[0].i);
            top.minH = std::max(0, a[1].i);
            break;
        case xdg_toplevel::set_maximized:
        case xdg_toplevel::set_fullscreen:
            top.wantsMaximize = true;
            break;
        case xdg_toplevel::set_minimized:
            top.wantsMinimize = true;
            break;
        default:  // move, resize and the window menu are handled by the shell's own frame
            break;
    }
}

// ---- popups -----------------------------------------------------------------------------------

namespace {

uint32_t flipHorizontal(uint32_t v) {
    using A = xdg_positioner::anchor_;
    switch (v) {
        case A::left: return A::right;
        case A::right: return A::left;
        case A::top_left: return A::top_right;
        case A::top_right: return A::top_left;
        case A::bottom_left: return A::bottom_right;
        case A::bottom_right: return A::bottom_left;
        default: return v;
    }
}

uint32_t flipVertical(uint32_t v) {
    using A = xdg_positioner::anchor_;
    switch (v) {
        case A::top: return A::bottom;
        case A::bottom: return A::top;
        case A::top_left: return A::bottom_left;
        case A::bottom_left: return A::top_left;
        case A::top_right: return A::bottom_right;
        case A::bottom_right: return A::top_right;
        default: return v;
    }
}

Box placePopup(const PositionerData& p, uint32_t anchor, uint32_t gravity) {
    using A = xdg_positioner::anchor_;
    const Box& r = p.anchorRect;
    int ax = r.x + r.w / 2, ay = r.y + r.h / 2;
    if (anchor == A::top || anchor == A::top_left || anchor == A::top_right) ay = r.y;
    if (anchor == A::bottom || anchor == A::bottom_left || anchor == A::bottom_right) ay = r.y + r.h;
    if (anchor == A::left || anchor == A::top_left || anchor == A::bottom_left) ax = r.x;
    if (anchor == A::right || anchor == A::top_right || anchor == A::bottom_right) ax = r.x + r.w;
    const int w = std::max(1, p.w), h = std::max(1, p.h);
    int x = ax - w / 2, y = ay - h / 2;
    if (gravity == A::top || gravity == A::top_left || gravity == A::top_right) y = ay - h;
    if (gravity == A::bottom || gravity == A::bottom_left || gravity == A::bottom_right) y = ay;
    if (gravity == A::left || gravity == A::top_left || gravity == A::bottom_left) x = ax - w;
    if (gravity == A::right || gravity == A::top_right || gravity == A::bottom_right) x = ax;
    return {x + p.offX, y + p.offY, w, h};
}

// Places a popup, then keeps it inside `bounds` (expressed in the parent's coordinates) using
// the adjustments the client allowed.
Box constrainPopup(const PositionerData& p, bool haveBounds, const Box& bounds) {
    using C = xdg_positioner::constraint_adjustment_;
    Box r = placePopup(p, p.anchor, p.gravity);
    if (!haveBounds) return r;
    auto outX = [&](const Box& b) { return b.x < bounds.x || b.x + b.w > bounds.x + bounds.w; };
    auto outY = [&](const Box& b) { return b.y < bounds.y || b.y + b.h > bounds.y + bounds.h; };
    if ((p.adjust & C::flip_x) && outX(r)) {
        const Box f = placePopup(p, flipHorizontal(p.anchor), flipHorizontal(p.gravity));
        if (!outX(f)) r.x = f.x;
    }
    if ((p.adjust & C::flip_y) && outY(r)) {
        const Box f = placePopup(p, flipVertical(p.anchor), flipVertical(p.gravity));
        if (!outY(f)) r.y = f.y;
    }
    if (p.adjust & C::slide_x) {
        if (r.x + r.w > bounds.x + bounds.w) r.x = bounds.x + bounds.w - r.w;
        if (r.x < bounds.x) r.x = bounds.x;
    }
    if (p.adjust & C::slide_y) {
        if (r.y + r.h > bounds.y + bounds.h) r.y = bounds.y + bounds.h - r.h;
        if (r.y < bounds.y) r.y = bounds.y;
    }
    if (p.adjust & C::resize_x) {
        const int x2 = std::min(r.x + r.w, bounds.x + bounds.w);
        r.x = std::max(r.x, bounds.x);
        r.w = std::max(1, x2 - r.x);
    }
    if (p.adjust & C::resize_y) {
        const int y2 = std::min(r.y + r.h, bounds.y + bounds.h);
        r.y = std::max(r.y, bounds.y);
        r.h = std::max(1, y2 - r.y);
    }
    return r;
}

}  // namespace

bool popupOriginOf(const XdgPopupRes& p, const XdgToplevelRes* top, float* ox, float* oy, int depth) {
    if (depth > 8 || !p.parent || !p.parent->surface) return false;
    const XdgSurfaceRes* par = p.parent;
    float px = 0, py = 0;
    if (par->toplevel) {
        if (par->toplevel != top) return false;
    } else if (par->popup) {
        if (!popupOriginOf(*par->popup, top, &px, &py, depth + 1)) return false;
    } else {
        return false;
    }
    const float gx = par->hasGeometry ? static_cast<float>(par->geometry.x) : 0.0f;
    const float gy = par->hasGeometry ? static_cast<float>(par->geometry.y) : 0.0f;
    const float mx = p.xs->hasGeometry ? static_cast<float>(p.xs->geometry.x) : 0.0f;
    const float my = p.xs->hasGeometry ? static_cast<float>(p.xs->geometry.y) : 0.0f;
    *ox = px + gx + static_cast<float>(p.rect.x) - mx;
    *oy = py + gy + static_cast<float>(p.rect.y) - my;
    return true;
}

void XdgPopupRes::place() {
    // Bounds: the whole toplevel the popup hangs from, in the parent's geometry coordinates.
    bool have = false;
    Box bounds;
    if (parent && parent->surface) {
        if (const Toplevel* t = parent->surface->toplevel()) {
            if (t->width > 0 && t->height > 0) {
                float ox = 0, oy = 0;
                bool ok = true;
                if (parent->popup) ok = popupOriginOf(*parent->popup, t->res, &ox, &oy);
                const int gx = parent->hasGeometry ? parent->geometry.x : 0;
                const int gy = parent->hasGeometry ? parent->geometry.y : 0;
                if (ok) {
                    bounds = {-static_cast<int>(ox) - gx, -static_cast<int>(oy) - gy, t->width, t->height};
                    have = true;
                }
            }
        }
    }
    rect = constrainPopup(pos, have, bounds);
}

XdgPopupRes::XdgPopupRes(Client& c, uint32_t id, uint32_t v, XdgSurfaceRes* x, XdgSurfaceRes* par,
                         const PositionerData& p)
    : Resource(c, id, "xdg_popup", v), xs(x), parent(par), pos(p) {
    place();
    c.server.seat().popups.push_back(this);
}

XdgPopupRes::~XdgPopupRes() {
    auto& v = client.server.seat().popups;
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
    if (xs) {
        xs->popup = nullptr;
        if (xs->surface && xs->surface->hasBuffer) {
            xs->surface->hasBuffer = false;
            if (client.server.presenter()) client.server.presenter()->remove(xs->surface->uid);
        }
    }
}

void XdgPopupRes::sendConfigure() {
    send(xdg_popup::configure, {Value::I(rect.x), Value::I(rect.y), Value::I(rect.w), Value::I(rect.h)});
    xs->sendConfigure();
}

void XdgPopupRes::done() {
    if (dismissed) return;
    dismissed = true;
    send(xdg_popup::popup_done);
    client.flush();
}

void XdgPopupRes::request(uint16_t op, Args& a) {
    switch (op) {
        case xdg_popup::grab:
            grab = true;
            break;
        case xdg_popup::reposition: {
            if (PositionerRes* p = client.getAs<PositionerRes>(a[0].u)) {
                pos = p->d;
                place();
                send(xdg_popup::repositioned, {Value::U(a[1].u)});
                sendConfigure();
            }
            break;
        }
        default:
            break;
    }
}

// ---- server-side decorations -----------------------------------------------------------------------

void DecorationManagerRes::request(uint16_t op, Args& a) {
    if (op != zxdg_decoration_manager_v1::get_toplevel_decoration) return;
    XdgToplevelRes* tl = client.getAs<XdgToplevelRes>(a[1].u);
    if (!tl) {
        client.protocolError(*this, 0, "not an xdg_toplevel");
        return;
    }
    client.create<DecorationRes>(a[0].u, version, tl->id);
    tl->decorationId = a[0].u;
    if (tl->xs->configured) tl->sendConfigure(true);  // otherwise it rides on the initial configure
}

DecorationRes::~DecorationRes() {
    if (XdgToplevelRes* tl = client.getAs<XdgToplevelRes>(toplevelId_)) {
        if (tl->decorationId == id) tl->decorationId = 0;
    }
}

void DecorationRes::configure() {
    // The window frame is drawn by the shell, so clients should not draw their own.
    send(zxdg_toplevel_decoration_v1::configure, {Value::U(zxdg_toplevel_decoration_v1::mode_::server_side)});
}

void DecorationRes::request(uint16_t op, Args&) {
    if (op == zxdg_toplevel_decoration_v1::set_mode || op == zxdg_toplevel_decoration_v1::unset_mode) {
        if (XdgToplevelRes* tl = client.getAs<XdgToplevelRes>(toplevelId_)) tl->sendConfigure(true);
    }
}

}  // namespace wl
}  // namespace pxr
