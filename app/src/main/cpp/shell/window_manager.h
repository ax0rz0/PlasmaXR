// Owns every shell window: placement (docked on the curved desktop or floating in space), focus and
// z-order, moving and resizing by hand, minimise / maximise / close, and open/close animation.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "input/pointers.h"
#include "shell/window.h"
#include "xr/xr_context.h"

namespace pxr {

class WindowManager {
public:
    struct Hooks {
        // Called just before a window's panel is destroyed so pointers can drop references to it.
        std::function<void(Panel*)> onPanelDestroyed;
    };

    void init(XrContext* xr, Hooks hooks);
    void shutdown();

    // Opens a window (or focuses the existing one for the same app id).
    Window* open(std::unique_ptr<WindowContent> content, bool floating = false);
    Window* find(const std::string& appId) const;
    void close(Window* w);
    void focus(Window* w);
    // Taskbar entry behaviour: restore if minimised, minimise if focused, otherwise focus.
    void taskbarClick(Window* w);
    // Sets the content area size (pixels), e.g. when a hosted app resizes itself.
    void resizeContent(Window* w, Vec2 contentPx);
    bool isResizing(const Window* w) const;

    void update(float dt, const PointerSystem& pointers, const Anchor& anchor, const Pose& head);

    std::vector<Panel*> panels() const;
    const std::vector<std::unique_ptr<Window>>& windows() const { return windows_; }
    Window* focused() const { return focused_; }
    Window* windowOf(const Panel* p) const;
    // Bumps whenever the set of windows, focus or minimised state changes (taskbar redraw trigger).
    uint32_t revision() const { return revision_; }

private:
    struct Drag {
        Window* win = nullptr;
        int pointer = -1;
        bool resize = false;
        // docked move
        float grabYaw = 0, grabY = 0;
        // floating move
        float grabDist = 0;
        Vec3 grabLocal;
        // docked resize: the frame's fixed top-left corner on the cylinder
        float anchorYawLeft = 0, anchorTopY = 0;
        // floating resize: the frame's fixed top-left corner in the world
        Vec3 anchorTL, axisRight, axisUp;
    };

    void drawWindow(Window& w, Canvas& cv, Ui& ui);
    void processPending(Window& w);
    void removeClosed();
    void handlePress(const PressEvent& e, const PointerSystem& ptrs);
    void beginDrag(Window& w, int pointer, bool resize, const PointerSystem& ptrs);
    void updateDrag(int i, const PointerSystem& ptrs);
    void applyLayout(Window& w);
    void setContentFromFrame(Window& w, float frameWm, float frameHm);
    void setFloating(Window& w, bool floating);
    void toggleMaximize(Window& w);
    void minimize(Window& w);
    void focusTopmost();
    void raise(Window& w);
    bool rayCylinder(const Ray& ray, float* yaw, float* y) const;

    XrContext* xr_ = nullptr;
    Hooks hooks_;
    std::vector<std::unique_ptr<Window>> windows_;
    Window* focused_ = nullptr;
    Drag drag_[kMaxPointers];
    Anchor anchor_;
    Pose head_;
    int nextId_ = 1;
    int zNext_ = 0;
    uint32_t revision_ = 1;
};

}  // namespace pxr
