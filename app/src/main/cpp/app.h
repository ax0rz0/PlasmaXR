// Top-level application: owns the OpenXR context, hand tracking, the shell and the renderers,
// and runs the Android event loop plus the per-frame update/render/submit sequence.
#pragma once

#include <android_native_app_glue.h>

#include <string>

#include "gfx/canvas.h"
#include "gfx/environment.h"
#include "gfx/overlay.h"
#include "input/controllers.h"
#include "input/hands.h"
#include "input/pointers.h"
#include "shell/shell.h"
#include "xr/panel.h"
#include "xr/xr_context.h"

namespace pxr {

class App {
public:
    explicit App(android_app* app) : app_(app) {}
    void run();
    void handleCmd(int32_t cmd);

private:
    bool initScene();
    void shutdownScene();
    void frame();
    bool renderOverlay(const XrView views[2], float time);
    void updateAnchor(const Pose& head);
    void updateSimulation(const Pose& head);
    void processDebug();
    void handleDebug(const std::string& cmd);
    void snapshotPanels();
    bool handleKey(int androidKeyCode, bool down);

    android_app* app_;
    XrContext xr_;
    Canvas canvas_;
    Overlay overlay_;
    Environment env_;
    HandTracking hands_;
    Controllers controllers_;
    bool prevDown_[2] = {false, false};
    PointerSystem pointers_;
    Shell shell_;
    OverlayStyle style_;

    Swapchain eyeSc_[2];
    XrCompositionLayerProjection projLayer_{};
    XrCompositionLayerProjectionView projViews_[2]{};

    Anchor anchor_;
    bool anchorSet_ = false;
    bool recenterRequested_ = false;
    bool rateApplied_ = false;
    bool sceneReady_ = false;
    XrTime lastTime_ = 0;
    float elapsed_ = 0;

    // Debug simulation of a right hand aiming at a panel (see docs/DEBUGGING.md).
    bool simOn_ = false;
    std::string simPanel_;
    Vec2 simUv_{0.5f, 0.5f};
    float simPinch_ = 0;
};

}  // namespace pxr
