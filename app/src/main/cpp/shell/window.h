// Windows: a Panel plus Breeze-style decorations (title bar, buttons, resize grip) around content
// that is either a native Plasma XR app (drawn with the UI toolkit) or, later, a hosted Android app.
#pragma once

#include <memory>
#include <string>

#include "core/math.h"
#include "gfx/canvas.h"
#include "ui/ui.h"
#include "xr/panel.h"

namespace pxr {

// A native app's contents. The window draws the frame; the app draws inside `area`.
class WindowContent {
public:
    virtual ~WindowContent() = default;
    virtual std::string appId() const = 0;
    virtual std::string title() const = 0;
    virtual const char* glyph() const { return "dot"; }
    virtual unsigned color() const { return 0x3daee9; }
    virtual Vec2 defaultSize() const { return {960, 640}; }
    virtual Vec2 minSize() const { return {560, 360}; }
    virtual void draw(Canvas& cv, Ui& ui, Rect area, float dt) = 0;
    // True if the contents change continuously (redrawn at about 20 Hz while visible).
    virtual bool continuous() const { return false; }
    // Polled every frame: true when the window must be redrawn now (a hosted app sent new pixels).
    virtual bool wantsRedraw() { return false; }
    // The user pressed close. Return true to close the window now; return false when the content
    // will end it itself (a hosted app that may ask "save changes?" first).
    virtual bool requestClose() { return true; }
};

namespace winframe {
constexpr float kMargin = 28;    // transparent border around the frame that holds the shadow
constexpr float kTitleH = 68;
constexpr float kBorder = 3;
constexpr float kRadius = 24;
constexpr float kButton = 52;
constexpr float kButtonGap = 6;
constexpr float kGrip = 52;
}  // namespace winframe

struct WindowLayout {
    Rect frame, title, content, grip, dragArea;
    Rect dock, minimize, maximize, close;  // maximize is empty for floating windows
};

WindowLayout windowLayout(int pxW, int pxH, bool showMaximize);

struct Window {
    int id = 0;
    std::unique_ptr<Panel> panel;
    std::unique_ptr<WindowContent> content;

    bool floating = false;
    bool minimized = false;
    bool maximized = false;
    bool closing = false;

    Vec2 contentPx{0, 0};  // content area size in pixels
    float ppm = 800;       // panel pixels per metre (changes when moving between desktop and space)
    float appear = 0;      // 0..1 open / minimise / close fade
    float reshapeAge = 10; // seconds since the shell last changed the window's size
    float redrawAcc = 0;

    // Restore state for un-maximising a docked window.
    float restoreYaw = 0;
    float restoreY = 0;
    Vec2 restoreContent{0, 0};

    // Requests raised from inside the window's own draw callback and applied on the next update, so
    // a window never destroys or reshapes itself while it is being drawn.
    bool pendingClose = false;
    bool pendingMinimize = false;
    bool pendingMaximize = false;
    bool pendingFloat = false;
};

}  // namespace pxr
