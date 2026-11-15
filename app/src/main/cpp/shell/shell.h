// The Plasma-style shell: desktop icons, widgets, the taskbar and the Kickoff menu, each a Panel
// placed on a cylinder around the user. Owns the app catalogue and all panel drawing/logic.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gfx/canvas.h"
#include "input/hands.h"
#include "input/pointers.h"
#include "platform/jni_bridge.h"
#include "shell/app_model.h"
#include "shell/linux_host.h"
#include "shell/native_apps.h"
#include "shell/settings.h"
#include "shell/window_manager.h"
#include "xr/panel.h"

namespace pxr {

struct ShellActions {
    std::function<void()> recenter;
    std::function<void()> exitApp;
    std::function<void()> openHome;
    std::function<void(bool)> setPassthrough;
    std::function<bool()> passthroughOn;
    std::function<void(int)> setRefreshRate;   // Hz, 0 = system default
    std::function<void(Panel*)> forgetPanel;   // drop pointer references before a panel dies
};

// Read-only facts about the XR runtime that widgets display.
struct ShellInfo {
    float refreshRate = 0;
    std::string runtime;
    std::string system;
    int eyeW = 0;
    int eyeH = 0;
    bool passthroughAvailable = false;
    bool handsAvailable = false;
    bool handMesh = false;
    bool controller[2] = {false, false};  // a controller is active on this side (hands pause)
};

class Shell {
public:
    bool init(XrContext& xr, ShellActions actions);
    void shutdown(XrContext& xr);

    void update(float dt, const ShellInfo& info, const HandTracking& hands,
                const PointerSystem& pointers, const Anchor& anchor, const Pose& head);
    void renderPanels(XrContext& xr, Canvas& canvas, float dt);
    // Appends this frame's panel layers, back to front as seen from `eye`.
    void collectLayers(const XrContext& xr, const Anchor& anchor, Vec3 eye,
                       std::vector<const XrCompositionLayerBaseHeader*>& out);

    std::vector<Panel*> panels();  // shell panels and windows
    Panel* panelByName(const std::string& name);
    AppModel& apps() { return apps_; }
    Settings& settings() { return settings_; }
    WindowManager& windows() { return wm_; }
    LinuxHost& linuxHost() { return linux_; }
    // After the frame was submitted: lets Linux clients draw their next frame.
    void frameDone() { linux_.frameDone(); }
    // Opens a native app in a window or starts an installed Android app.
    void launch(const AppInfo& app);
    void openNative(const std::string& id, bool floating = false);

    void setKickoffOpen(bool open);
    bool kickoffOpen() const { return kickoffOpen_; }
    void setKickoffTab(int tab);
    void markAllDirty();

private:
    enum Tab { kTabFavorites = 0, kTabApps = 1, kTabLeave = 2 };

    Panel* addPanel(XrContext& xr, const char* name, float widthM, float heightM, float xM,
                    float yM, int order, std::function<void(Canvas&, Ui&)> draw);

    void drawDesktop(Canvas& cv, Ui& ui);
    void drawClock(Canvas& cv, Ui& ui);
    void drawSystem(Canvas& cv, Ui& ui);
    void drawHands(Canvas& cv, Ui& ui);
    void drawTaskbar(Canvas& cv, Ui& ui);
    void drawKickoff(Canvas& cv, Ui& ui);
    void drawKickoffGrid(Canvas& cv, Ui& ui, Rect area, const std::vector<const AppInfo*>& list,
                         uint32_t scrollId, int idBase);
    void drawKickoffLeave(Canvas& cv, Ui& ui, Rect area);

    void drawAppIcon(Canvas& cv, const AppInfo& app, Rect r);
    WidgetState appTile(Canvas& cv, Ui& ui, const AppInfo& app, Rect r, uint32_t id, float iconPx,
                        float labelSize, bool labelShadow);
    void playSounds();

    ShellActions actions_;
    ShellInfo info_;
    AppModel apps_;
    Settings settings_;
    WindowManager wm_;
    LinuxHost linux_;
    AppContext appCtx_;
    uint32_t wmRevision_ = 0;
    const HandTracking* hands_ = nullptr;
    const PointerSystem* pointers_ = nullptr;
    Anchor anchor_;

    std::vector<std::unique_ptr<Panel>> panels_;
    Panel* desktop_ = nullptr;
    Panel* clock_ = nullptr;
    Panel* system_ = nullptr;
    Panel* handsPanel_ = nullptr;
    Panel* taskbar_ = nullptr;
    Panel* kickoff_ = nullptr;

    SystemStatus status_;
    float statusTimer_ = 0;
    bool kickoffOpen_ = false;
    float kickoffAnim_ = 0;
    int tab_ = kTabFavorites;
    uint32_t appsRevision_ = 0;
    uint32_t handSig_ = 0;
    float handAcc_ = 0;
};

}  // namespace pxr
