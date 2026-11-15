// Protocol objects shared by server.cpp (display, seat, output, data device) and surface.cpp
// (compositor, shm, xdg-shell, subsurfaces). Not part of the public interface.
#pragma once

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "wayland/server.h"

namespace pxr {
namespace wl {

constexpr uint32_t kBtnLeft = 0x110;
constexpr uint32_t kServerIdBase = 0xff000000u;

// The US keymap clients are given (generated into keymap_us.cpp by tools/wayland/gen.py).
extern const char* const kKeymapUs;

enum ShmFormat : uint32_t { kShmArgb8888 = 0, kShmXrgb8888 = 1 };

struct Box {
    int x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
    void unite(const Box& o) {
        if (o.empty()) return;
        if (empty()) {
            *this = o;
            return;
        }
        const int x1 = std::min(x, o.x), y1 = std::min(y, o.y);
        const int x2 = std::max(x + w, o.x + o.w), y2 = std::max(y + h, o.y + o.h);
        x = x1;
        y = y1;
        w = x2 - x1;
        h = y2 - y1;
    }
    Box clipped(int maxW, int maxH) const {
        Box b;
        b.x = std::max(0, x);
        b.y = std::max(0, y);
        b.w = std::min(maxW, x + w) - b.x;
        b.h = std::min(maxH, y + h) - b.y;
        return b;
    }
};

// A memory-mapped wl_shm pool, shared by the pool object and every buffer cut from it.
struct ShmPool {
    uint8_t* data = nullptr;
    size_t size = 0;
    int fd = -1;
    ~ShmPool();
    bool map(size_t newSize);
};

struct BufferData {
    std::shared_ptr<ShmPool> pool;
    int32_t offset = 0, width = 0, height = 0, stride = 0;
    uint32_t format = 0;
    uint32_t resId = 0;  // the wl_buffer object, for the release event
};

// ---- core -------------------------------------------------------------------------------------

class CallbackRes : public Resource {
public:
    CallbackRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_callback", v) {}
    void request(uint16_t, Args&) override {}
};

class DisplayRes : public Resource {
public:
    DisplayRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_display", v) {}
    void request(uint16_t op, Args& a) override;
};

class RegistryRes : public Resource {
public:
    RegistryRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_registry", v) {}
    void request(uint16_t op, Args& a) override;
};

// ---- surfaces and buffers -----------------------------------------------------------------------

class CompositorRes : public Resource {
public:
    CompositorRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_compositor", v) {}
    void request(uint16_t op, Args& a) override;
};

class RegionRes : public Resource {
public:
    RegionRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_region", v) {}
    void request(uint16_t, Args&) override {}  // input and opaque regions are not used
};

class ShmRes : public Resource {
public:
    ShmRes(Client& c, uint32_t id, uint32_t v);
    void request(uint16_t op, Args& a) override;
};

class ShmPoolRes : public Resource {
public:
    ShmPoolRes(Client& c, uint32_t id, uint32_t v, std::shared_ptr<ShmPool> pool)
        : Resource(c, id, "wl_shm_pool", v), pool_(std::move(pool)) {}
    void request(uint16_t op, Args& a) override;

private:
    std::shared_ptr<ShmPool> pool_;
};

class BufferRes : public Resource {
public:
    BufferRes(Client& c, uint32_t id, uint32_t v, std::shared_ptr<BufferData> d)
        : Resource(c, id, "wl_buffer", v), data(std::move(d)) {}
    void request(uint16_t, Args&) override {}
    std::shared_ptr<BufferData> data;
};

class XdgSurfaceRes;
class XdgPopupRes;
class SubsurfaceRes;

class SurfaceRes : public Resource {
public:
    SurfaceRes(Client& c, uint32_t id, uint32_t v);
    ~SurfaceRes() override;
    void request(uint16_t op, Args& a) override;
    void commit();

    // Logical size (buffer size divided by the buffer scale, rounded up).
    int logicalW() const { return (bufW + scale - 1) / std::max(1, scale); }
    int logicalH() const { return (bufH + scale - 1) / std::max(1, scale); }
    bool visible() const;
    Toplevel* toplevel(int depth = 0) const;  // the toplevel this surface belongs to (via popups, parents)
    void fireFrameCallbacks(uint32_t timeMs);

private:
    bool upload(const BufferData& b);
    void enterOutputs();

public:

    uint32_t uid;

    // Pending (double-buffered) state.
    bool pAttached = false;
    std::shared_ptr<BufferData> pBuffer;
    int pScale = 1;
    std::vector<Box> pSurfaceDamage;
    std::vector<Box> pBufferDamage;
    std::vector<uint32_t> pFrames;

    // Current state.
    bool hasBuffer = false;
    int bufW = 0, bufH = 0, scale = 1;
    bool hasAlpha = true;
    std::vector<uint32_t> frameCbs;
    bool enteredOutput = false;

    // Roles.
    XdgSurfaceRes* xdg = nullptr;
    SubsurfaceRes* sub = nullptr;

    // Subsurface children, back to front, plus those placed below this surface.
    std::vector<SurfaceRes*> below, above;
};

// ---- xdg-shell ------------------------------------------------------------------------------------

class XdgWmBaseRes : public Resource {
public:
    XdgWmBaseRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "xdg_wm_base", v) {}
    void request(uint16_t op, Args& a) override;
};

// How a popup wants to be placed relative to its parent (xdg_positioner state).
struct PositionerData {
    int w = 0, h = 0;
    Box anchorRect;
    uint32_t anchor = 0, gravity = 0, adjust = 0;
    int offX = 0, offY = 0;
};

class PositionerRes : public Resource {
public:
    PositionerRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "xdg_positioner", v) {}
    void request(uint16_t op, Args& a) override;
    PositionerData d;
};

class XdgToplevelRes;

class XdgSurfaceRes : public Resource {
public:
    XdgSurfaceRes(Client& c, uint32_t id, uint32_t v, SurfaceRes* s);
    ~XdgSurfaceRes() override;
    void request(uint16_t op, Args& a) override;
    void onCommit();
    void sendConfigure();  // plain xdg_surface.configure with a fresh serial

    SurfaceRes* surface;
    XdgToplevelRes* toplevel = nullptr;
    XdgPopupRes* popup = nullptr;
    bool configured = false;   // the initial configure has gone out
    bool mapped = false;
    Box geometry;              // set_window_geometry, logical; empty = whole surface
    bool hasGeometry = false;
};

class XdgToplevelRes : public Resource {
public:
    XdgToplevelRes(Client& c, uint32_t id, uint32_t v, XdgSurfaceRes* xs);
    ~XdgToplevelRes() override;
    void request(uint16_t op, Args& a) override;
    // Sends configure + xdg_surface.configure unless nothing changed since the last one.
    void sendConfigure(bool force);

    Toplevel top;
    XdgSurfaceRes* xs;
    int reqW = 0, reqH = 0;
    bool activated = false, resizing = false, maximized = false;
    uint32_t decorationId = 0;  // zxdg_toplevel_decoration_v1 object, if any
    int lastW = -1, lastH = -1;
    uint32_t lastStates = ~0u;
    bool capabilitiesSent = false;
};

class XdgPopupRes : public Resource {
public:
    XdgPopupRes(Client& c, uint32_t id, uint32_t v, XdgSurfaceRes* xs, XdgSurfaceRes* parent,
                const PositionerData& pos);
    ~XdgPopupRes() override;
    void request(uint16_t op, Args& a) override;
    void sendConfigure();
    void done();  // popup_done: the compositor dismisses it

    XdgSurfaceRes* xs;
    XdgSurfaceRes* parent;
    Box rect;            // position and size relative to the parent's window geometry, logical
    PositionerData pos;  // snapshot of the positioner
    bool grab = false;
    bool dismissed = false;

private:
    void place();
};

// Where a popup's surface origin lies relative to the main surface of `top`. False if the popup
// does not hang from that toplevel.
bool popupOriginOf(const XdgPopupRes& p, const XdgToplevelRes* top, float* ox, float* oy, int depth = 0);

class DecorationManagerRes : public Resource {
public:
    DecorationManagerRes(Client& c, uint32_t id, uint32_t v)
        : Resource(c, id, "zxdg_decoration_manager_v1", v) {}
    void request(uint16_t op, Args& a) override;
};

class DecorationRes : public Resource {
public:
    DecorationRes(Client& c, uint32_t id, uint32_t v, uint32_t toplevelId)
        : Resource(c, id, "zxdg_toplevel_decoration_v1", v), toplevelId_(toplevelId) {}
    ~DecorationRes() override;
    void request(uint16_t op, Args& a) override;
    void configure();

private:
    uint32_t toplevelId_;
};

class SubcompositorRes : public Resource {
public:
    SubcompositorRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_subcompositor", v) {}
    void request(uint16_t op, Args& a) override;
};

class SubsurfaceRes : public Resource {
public:
    SubsurfaceRes(Client& c, uint32_t id, uint32_t v, SurfaceRes* surface, SurfaceRes* parent);
    ~SubsurfaceRes() override;
    void request(uint16_t op, Args& a) override;

    SurfaceRes* surface;
    SurfaceRes* parent;
    int x = 0, y = 0;
    int pendingX = 0, pendingY = 0;
    bool belowParent = false;
};

// ---- seat, output, clipboard ------------------------------------------------------------------------

class SeatRes : public Resource {
public:
    SeatRes(Client& c, uint32_t id, uint32_t v);
    void request(uint16_t op, Args& a) override;
};

class PointerRes : public Resource {
public:
    PointerRes(Client& c, uint32_t id, uint32_t v);
    ~PointerRes() override;
    void request(uint16_t, Args&) override {}  // set_cursor is ignored; the shell draws the cursor
    void frame() {
        if (version >= 5) send(wl_pointer::frame);
    }
};

class KeyboardRes : public Resource {
public:
    KeyboardRes(Client& c, uint32_t id, uint32_t v);
    ~KeyboardRes() override;
    void request(uint16_t, Args&) override {}
};

class OutputRes : public Resource {
public:
    OutputRes(Client& c, uint32_t id, uint32_t v);
    ~OutputRes() override;
    void request(uint16_t, Args&) override {}
    void sendInfo();
};

class DataOfferRes;

class DataSourceRes : public Resource {
public:
    DataSourceRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_data_source", v) {}
    ~DataSourceRes() override;
    void request(uint16_t op, Args& a) override;
    std::vector<std::string> mimeTypes;
    std::vector<DataOfferRes*> offers;  // offers made from this source (possibly in other clients)
};

class DataOfferRes : public Resource {
public:
    DataOfferRes(Client& c, uint32_t id, uint32_t v) : Resource(c, id, "wl_data_offer", v) {}
    ~DataOfferRes() override;
    void request(uint16_t op, Args& a) override;
    DataSourceRes* source = nullptr;  // cleared when the source goes away
};

class DataDeviceRes : public Resource {
public:
    DataDeviceRes(Client& c, uint32_t id, uint32_t v);
    ~DataDeviceRes() override;
    void request(uint16_t op, Args& a) override;
    void sendSelection(DataSourceRes* src);
};

class DataDeviceManagerRes : public Resource {
public:
    DataDeviceManagerRes(Client& c, uint32_t id, uint32_t v)
        : Resource(c, id, "wl_data_device_manager", v) {}
    void request(uint16_t op, Args& a) override;
};

// ---- seat state ------------------------------------------------------------------------------------

struct Server::SeatState {
    std::vector<PointerRes*> pointers;
    std::vector<KeyboardRes*> keyboards;
    std::vector<OutputRes*> outputs;
    std::vector<DataDeviceRes*> dataDevices;
    std::vector<XdgPopupRes*> popups;  // every live popup, oldest first

    SurfaceRes* pointerFocus = nullptr;
    double lastSentX = -1e9, lastSentY = -1e9;  // last pointer position told to the focus
    int buttonsDown = 0;                        // buttons held; they keep the pointer's focus
    SurfaceRes* keyboardFocus = nullptr;
    uint32_t keyboardToplevel = 0;
    std::set<uint32_t> pressed;
    uint32_t modsDepressed = 0, modsLocked = 0;

    DataSourceRes* selection = nullptr;

    int keymapFd = -1;
    uint32_t keymapSize = 0;

    int outW = 1280, outH = 800, outScale = 2;
};

// Marks a toplevel's pixels as changed (called after a commit on any of its surfaces).
void touchToplevel(SurfaceRes& s);

}  // namespace wl
}  // namespace pxr
