#include "shell/linux_host.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "platform/jni_bridge.h"
#include "shell/theme.h"
#include "shell/window_manager.h"

namespace pxr {

namespace {

constexpr uint32_t kBtnLeft = 0x110;

// The window content for one Wayland toplevel.
class LinuxWindow : public WindowContent {
public:
    LinuxWindow(LinuxHost& host, uint32_t uid) : host_(host), uid_(uid) {}

    uint32_t uid() const { return uid_; }

    std::string appId() const override { return "wl:" + std::to_string(uid_); }

    std::string title() const override {
        const wl::Toplevel* t = top();
        if (!t) return "Linux app";
        if (!t->title.empty()) return t->title;
        if (!t->appId.empty()) return t->appId;
        return "Linux app";
    }

    const char* glyph() const override {
        const wl::Toplevel* t = top();
        const std::string id = t ? t->appId + " " + t->title : "";
        for (const char* k : {"foot", "term", "konsole", "xterm", "kitty", "alacritty"}) {
            if (id.find(k) != std::string::npos) return "terminal";
        }
        return "apps";
    }

    unsigned color() const override { return 0x3daee9; }

    Vec2 defaultSize() const override {
        const wl::Toplevel* t = top();
        if (!t || t->bufferW <= 0) return {960, 640};
        return {std::max(320.0f, static_cast<float>(t->bufferW)), std::max(200.0f, static_cast<float>(t->bufferH))};
    }

    Vec2 minSize() const override {
        const wl::Toplevel* t = top();
        const float s = t ? static_cast<float>(std::max(1, t->scale)) : 1.0f;
        return {std::max(320.0f, t ? t->minW * s : 0.0f), std::max(200.0f, t ? t->minH * s : 0.0f)};
    }

    bool wantsRedraw() override {
        wl::Toplevel* t = top();
        if (!t) return false;
        const std::string title = this->title();
        const bool titleChanged = title != lastTitle_;
        lastTitle_ = title;
        return t->dirty || titleChanged;
    }

    bool requestClose() override {
        if (!top()) return true;  // the client is already gone
        host_.server().requestClose(uid_);
        return false;             // the client decides (it may ask to save first)
    }

    void draw(Canvas& cv, Ui& ui, Rect area, float dt) override {
        wl::Server& srv = host_.server();
        wl::Toplevel* t = top();
        cv.rect(area, Color::hex(0x1b1e20), 0);
        if (!t || !t->mapped) {
            cv.textIn("Waiting for the application...", area,
                      textStyle(30, theme::kTextDim));
            return;
        }

        const float S = static_cast<float>(std::max(1, t->scale));
        srv.visuals(uid_, vis_);
        for (const wl::Visual& v : vis_) {
            const Texture* tex = host_.textures().find(v.surfaceUid);
            if (!tex) continue;
            // Pixel-exact when the client renders at our scale (it does); otherwise stretch.
            const float w = v.scale == static_cast<int>(S) ? static_cast<float>(v.bufferW) : v.w * S;
            const float h = v.scale == static_cast<int>(S) ? static_cast<float>(v.bufferH) : v.h * S;
            const Rect dst(area.x + std::round(v.x * S), area.y + std::round(v.y * S), w, h);
            cv.image(*tex, dst, Color(1, 1, 1, 1), 0);
        }
        t->dirty = false;

        // Pointer: one cursor drives the client, like a real mouse.
        const RawPointer rp = ui.rawPointer(Ui::id("lx.surface", static_cast<int>(uid_)), area);
        if (rp.pointer >= 0) {
            const double lx = rp.local.x / S, ly = rp.local.y / S;
            if (rp.pressEdge) {
                bool anyPopup = false, inPopup = false;
                for (const wl::Visual& v : vis_) {
                    if (!v.popup) continue;
                    anyPopup = true;
                    if (lx >= v.x && ly >= v.y && lx < v.x + v.w && ly < v.y + v.h) inPopup = true;
                }
                if (anyPopup && !inPopup) {  // a click outside an open menu only closes it
                    srv.dismissPopups(uid_);
                    swallow_ = true;
                }
            }
            if (!swallow_) {
                srv.pointerMotion(uid_, lx, ly);
                host_.setPointerOwner(uid_);
                if (rp.pressEdge) srv.pointerButton(kBtnLeft, true);
                if (rp.releaseEdge) srv.pointerButton(kBtnLeft, false);
                if (rp.wheel != 0) srv.pointerAxis(rp.wheel / S);
            }
            if (rp.releaseEdge) swallow_ = false;
        } else if (host_.pointerOwner() == uid_) {
            srv.pointerLeave();
            host_.setPointerOwner(0);
        }
    }

private:
    static TextStyle textStyle(float size, Color c) {
        TextStyle s;
        s.size = size;
        s.color = c;
        return s;
    }
    wl::Toplevel* top() const { return host_.server().findToplevel(uid_); }

    LinuxHost& host_;
    uint32_t uid_;
    std::vector<wl::Visual> vis_;
    bool swallow_ = false;
    std::string lastTitle_;
};

}  // namespace

// ---------------------------------------------------------------------------------------------

bool LinuxHost::init(WindowManager* wm, bool srgb) {
    wm_ = wm;
    textures_.setSrgb(srgb);
    server_.setPresenter(&textures_);
    scale_ = std::max(1, std::min(3, atoi(Jni::getPref("linux.scale", "2").c_str())));
    server_.setOutput(1280, 800, scale_);
    const std::string dir = Jni::linuxSocketDir();
    if (dir.empty() || !server_.start(dir, "wayland-0")) {
        LOGW("Linux apps: the Wayland compositor could not start");
        return false;
    }
    LOGI("Linux apps: compositor ready (%s), rootfs %s", dir.c_str(),
         Jni::linuxInstalled() ? "installed" : "not installed");
    return true;
}

void LinuxHost::shutdown() {
    server_.stop();  // clients go first so their textures are released through the presenter
    textures_.clear();
    tracked_.clear();
}

bool LinuxHost::installed() const { return Jni::linuxInstalled(); }

bool LinuxHost::launch(const std::string& shellCommand) {
    if (!server_.running()) return false;
    return Jni::launchLinux(shellCommand);
}

void LinuxHost::setScale(int scale) {
    scale = std::max(1, std::min(3, scale));
    if (scale == scale_) return;
    scale_ = scale;
    Jni::setPref("linux.scale", std::to_string(scale));
    server_.setOutput(1280, 800, scale);
}

void LinuxHost::update(float dt, const std::vector<PressEvent>& presses) {
    if (!server_.running()) return;
    server_.dispatch();

    // A new mapped toplevel gets a window.
    for (wl::Toplevel* t : server_.toplevels()) {
        if (!t->mapped || tracked_.count(t->uid)) continue;
        Window* w = wm_->open(std::make_unique<LinuxWindow>(*this, t->uid), false);
        if (w) {
            tracked_[t->uid] = Track{t->bufferW, t->bufferH};
            LOGI("Linux apps: window for \"%s\" (%s)", t->title.c_str(), t->appId.c_str());
        }
    }

    // Existing windows follow their toplevel.
    for (auto it = tracked_.begin(); it != tracked_.end();) {
        Window* w = wm_->find("wl:" + std::to_string(it->first));
        wl::Toplevel* t = server_.findToplevel(it->first);
        if (!w) {  // the window is gone
            if (pointerOwner_ == it->first) {
                server_.pointerLeave();
                pointerOwner_ = 0;
            }
            it = tracked_.erase(it);
            continue;
        }
        if (!t) {  // the client went away
            wm_->close(w);
        } else if (t->mapped) {
            sync(*w, *t, it->second);
        }
        ++it;
    }

    // Keyboard focus follows the focused window.
    uint32_t focus = 0;
    if (Window* f = wm_->focused()) {
        if (auto* lw = dynamic_cast<LinuxWindow*>(f->content.get())) focus = lw->uid();
    }
    if (focus != focusUid_) {
        focusUid_ = focus;
        server_.setKeyboardFocus(focus);
    }

    // A press anywhere else closes the menus a client has open.
    for (const PressEvent& e : presses) {
        const Window* w = wm_->windowOf(e.panel);
        const auto* lw = w ? dynamic_cast<const LinuxWindow*>(w->content.get()) : nullptr;
        for (const auto& kv : tracked_) {
            if (!lw || lw->uid() != kv.first) server_.dismissPopups(kv.first);
        }
    }
}

void LinuxHost::sync(Window& w, wl::Toplevel& t, Track& tr) {
    const int S = std::max(1, server_.outputScale());
    const bool focused = wm_->focused() == &w;
    t.visible = !w.minimized && !w.closing && w.appear > 0.05f;
    if (t.wantsMinimize) {
        t.wantsMinimize = false;
        w.pendingMinimize = true;
    }
    if (t.wantsMaximize) {
        t.wantsMaximize = false;
        if (!w.maximized && !w.floating) w.pendingMaximize = true;
    }

    // The client changed its own size (a dialog, or it ignored our request): follow it, unless the
    // user is in the middle of reshaping the window.
    if (t.bufferW != tr.seenW || t.bufferH != tr.seenH) {
        tr.seenW = t.bufferW;
        tr.seenH = t.bufferH;
        const bool fits = std::abs(t.bufferW - static_cast<int>(w.contentPx.x)) <= S &&
                          std::abs(t.bufferH - static_cast<int>(w.contentPx.y)) <= S;
        if (!fits && w.reshapeAge > 0.8f && !wm_->isResizing(&w) && !w.maximized) {
            wm_->resizeContent(&w, {static_cast<float>(t.bufferW), static_cast<float>(t.bufferH)});
        }
    }

    // The size the window has is the size the client should have.
    const int lw = std::max(1, static_cast<int>(w.contentPx.x) / S);
    const int lh = std::max(1, static_cast<int>(w.contentPx.y) / S);
    server_.configureToplevel(t.uid, lw, lh, focused, wm_->isResizing(&w), w.maximized);
}

// ---------------------------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------------------------

void LinuxHost::key(uint32_t evdevKey, bool down) {
    if (server_.running()) server_.key(evdevKey, down);
}

void LinuxHost::typeText(const std::string& text) {
    static const char* rows[] = {"1234567890-=", "qwertyuiop[]", "asdfghjkl;'", "zxcvbnm,./"};
    static const uint32_t starts[] = {2, 16, 30, 44};
    static const char* shifted[] = {"!@#$%^&*()_+", "QWERTYUIOP{}", "ASDFGHJKL:\"", "ZXCVBNM<>?"};
    for (char ch : text) {
        uint32_t code = 0;
        bool shift = false;
        if (ch == ' ') code = 57;
        else if (ch == '\n') code = 28;
        else if (ch == '\t') code = 15;
        else {
            for (int r = 0; r < 4 && !code; ++r) {
                const char* a = strchr(rows[r], ch);
                if (a && ch) code = starts[r] + static_cast<uint32_t>(a - rows[r]);
                const char* b = strchr(shifted[r], ch);
                if (!code && b && ch) {
                    code = starts[r] + static_cast<uint32_t>(b - shifted[r]);
                    shift = true;
                }
            }
        }
        if (!code) continue;
        if (shift) key(42, true);
        key(code, true);
        key(code, false);
        if (shift) key(42, false);
    }
}

bool LinuxHost::androidKey(int k, bool down) {
    if (!wantsKeyboard()) return false;
    uint32_t code = 0;
    static const uint32_t letters[26] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                                         49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
    if (k >= 29 && k <= 54) code = letters[k - 29];                     // AKEYCODE_A..Z
    else if (k >= 8 && k <= 16) code = 2 + static_cast<uint32_t>(k - 8);  // 1..9
    else if (k == 7) code = 11;                                         // 0
    else if (k >= 131 && k <= 140) code = 59 + static_cast<uint32_t>(k - 131);  // F1..F10
    else if (k == 141) code = 87;                                       // F11
    else if (k == 142) code = 88;                                       // F12
    else {
        switch (k) {
            case 55: code = 51; break;    // comma
            case 56: code = 52; break;    // period
            case 57: code = 56; break;    // alt left
            case 58: code = 100; break;   // alt right
            case 59: code = 42; break;    // shift left
            case 60: code = 54; break;    // shift right
            case 61: code = 15; break;    // tab
            case 62: code = 57; break;    // space
            case 66: code = 28; break;    // enter
            case 67: code = 14; break;    // backspace
            case 68: code = 41; break;    // grave
            case 69: code = 12; break;    // minus
            case 70: code = 13; break;    // equals
            case 71: code = 26; break;    // [
            case 72: code = 27; break;    // ]
            case 73: code = 43; break;    // backslash
            case 74: code = 39; break;    // semicolon
            case 75: code = 40; break;    // apostrophe
            case 76: code = 53; break;    // slash
            case 111: code = 1; break;    // escape
            case 112: code = 111; break;  // delete
            case 113: code = 29; break;   // ctrl left
            case 114: code = 97; break;   // ctrl right
            case 115: code = 58; break;   // caps lock
            case 117: code = 125; break;  // super left
            case 118: code = 126; break;  // super right
            case 122: code = 102; break;  // home
            case 123: code = 107; break;  // end
            case 92: code = 104; break;   // page up
            case 93: code = 109; break;   // page down
            case 124: code = 110; break;  // insert
            case 19: code = 103; break;   // up
            case 20: code = 108; break;   // down
            case 21: code = 105; break;   // left
            case 22: code = 106; break;   // right
            default: break;
        }
    }
    if (!code) return false;
    server_.key(code, down);
    return true;
}

}  // namespace pxr
