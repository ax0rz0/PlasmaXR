// Hosts Linux programs: owns the Wayland compositor and turns each client window into a shell
// window (docked or floating like any other), forwards pointer and keyboard input, and keeps the
// client's size in step with the window.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx/surface_textures.h"
#include "input/pointers.h"
#include "wayland/server.h"

namespace pxr {

class WindowManager;
struct Window;

class LinuxHost {
public:
    bool init(WindowManager* wm, bool srgb);
    void shutdown();

    // Once per frame, before the panels are drawn: accepts clients, opens and closes windows for
    // their toplevels and syncs focus and sizes.
    void update(float dt, const std::vector<PressEvent>& presses);
    // After the frame was submitted: lets clients draw their next frame.
    void frameDone() { server_.frameDone(); }

    // Starts a program in the rootfs. It connects to the compositor on its own.
    bool launch(const std::string& shellCommand);
    bool installed() const;

    // Integer buffer scale clients render at (1-3). Higher = sharper but heavier.
    void setScale(int scale);
    int scale() const { return scale_; }

    // Keyboard input goes to the focused Linux window.
    bool wantsKeyboard() const { return server_.keyboardFocusUid() != 0; }
    void key(uint32_t evdevKey, bool down);
    void typeText(const std::string& text);
    // Translates an Android key event. Returns true if a Linux window took it.
    bool androidKey(int androidKeyCode, bool down);

    wl::Server& server() { return server_; }
    SurfaceTextures& textures() { return textures_; }
    bool running() const { return server_.running(); }
    size_t windowCount() const { return tracked_.size(); }

    // Which toplevel the compositor's pointer is currently over (used by the window contents so only
    // the window that lost the pointer sends the leave).
    uint32_t pointerOwner() const { return pointerOwner_; }
    void setPointerOwner(uint32_t uid) { pointerOwner_ = uid; }

private:
    struct Track {
        int seenW = 0, seenH = 0;  // last buffer size seen from the client
    };
    void sync(Window& w, wl::Toplevel& t, Track& tr);

    WindowManager* wm_ = nullptr;
    wl::Server server_;
    SurfaceTextures textures_;
    int scale_ = 2;
    uint32_t pointerOwner_ = 0;
    uint32_t focusUid_ = 0;
    std::unordered_map<uint32_t, Track> tracked_;
};

}  // namespace pxr
