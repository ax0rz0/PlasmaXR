#include "app.h"

#include <chrono>
#include <cmath>
#include <sstream>

#include "core/log.h"
#include "platform/jni_bridge.h"

namespace pxr {

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

void App::handleCmd(int32_t cmd) {
    switch (cmd) {
        case APP_CMD_RESUME:
            LOGI("APP_CMD_RESUME");
            xr_.setResumed(true);
            break;
        case APP_CMD_PAUSE:
            LOGI("APP_CMD_PAUSE");
            xr_.setResumed(false);
            break;
        case APP_CMD_DESTROY:
            LOGI("APP_CMD_DESTROY");
            break;
        default:
            break;
    }
}

bool App::initScene() {
    const bool srgb = xr_.colorFormat() == GL_SRGB8_ALPHA8;
    if (!canvas_.init(srgb)) {
        LOGE("Canvas init failed");
        return false;
    }
    if (!overlay_.init(srgb)) {
        LOGE("Overlay init failed");
        return false;
    }
    env_.init(xr_);          // optional: the shell works without a wallpaper
    hands_.init(xr_);        // optional: controllers work without it
    controllers_.init(xr_);  // optional: hands work without it

    for (int eye = 0; eye < 2; ++eye) {
        if (!xr_.createSwapchain(xr_.eyeWidth(), xr_.eyeHeight(), eyeSc_[eye], true, 4)) {
            LOGE("Eye swapchain creation failed");
            return false;
        }
    }

    ShellActions actions;
    actions.recenter = [this] { recenterRequested_ = true; };
    actions.exitApp = [this] { ANativeActivity_finish(app_->activity); };
    actions.openHome = [] {
        Jni::launchApp("com.oculus.vrshell", "com.oculus.vrshell.HomeActivity");
    };
    actions.setPassthrough = [](bool) {};
    actions.passthroughOn = [] { return false; };
    actions.setRefreshRate = [this](int hz) { xr_.requestRefreshRate(static_cast<float>(hz)); };
    actions.forgetPanel = [this](Panel* p) { pointers_.forgetPanel(p); };
    if (!shell_.init(xr_, actions)) {
        LOGE("Shell init failed");
        return false;
    }
    sceneReady_ = true;
    LOGI("Scene ready");
    return true;
}

void App::shutdownScene() {
    shell_.shutdown(xr_);
    for (auto& sc : eyeSc_) xr_.destroySwapchain(sc);
    env_.shutdown(xr_);
    controllers_.shutdown(xr_);
    hands_.shutdown(xr_);
    overlay_.shutdown();
    canvas_.shutdown();
    sceneReady_ = false;
}

void App::run() {
    JNIEnv* env = nullptr;
    app_->activity->vm->AttachCurrentThread(&env, nullptr);
    app_->userData = this;
    app_->onAppCmd = [](android_app* a, int32_t cmd) {
        static_cast<App*>(a->userData)->handleCmd(cmd);
    };
    app_->onInputEvent = [](android_app* a, AInputEvent* ev) -> int32_t {
        if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_KEY) return 0;
        const int32_t action = AKeyEvent_getAction(ev);
        if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) return 0;
        // Android's auto-repeat is dropped: Linux clients repeat keys themselves.
        if (action == AKEY_EVENT_ACTION_DOWN && AKeyEvent_getRepeatCount(ev) > 0) return 0;
        return static_cast<App*>(a->userData)->handleKey(AKeyEvent_getKeyCode(ev), action == AKEY_EVENT_ACTION_DOWN) ? 1 : 0;
    };
    Jni::init(app_);

    LOGI("Plasma XR starting");
    const bool ok = xr_.init(app_) && xr_.createSession() && initScene();
    if (!ok) {
        LOGE("Initialisation failed");
        ANativeActivity_finish(app_->activity);
    }

    while (ok && !app_->destroyRequested && !xr_.exitRequested()) {
        int events = 0;
        android_poll_source* source = nullptr;
        const int timeout = xr_.sessionRunning() ? 0 : 100;
        while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app_, source);
            if (app_->destroyRequested) break;
        }
        xr_.pollEvents();
        processDebug();
        if (xr_.sessionRunning()) frame();
    }

    if (sceneReady_) shutdownScene();
    xr_.shutdown();
    app_->activity->vm->DetachCurrentThread();
    LOGI("Plasma XR exited");
}

// ---------------------------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------------------------

void App::updateAnchor(const Pose& head) {
    Vec3 f = head.forward();
    f.y = 0;
    if (f.length() > 0.2f) anchor_.yaw = std::atan2(-f.x, -f.z);
    anchor_.pos = head.p;
    anchorSet_ = true;
    LOGI("Anchor set: yaw=%.1f deg pos=(%.2f, %.2f, %.2f)", anchor_.yaw / kDegToRad, anchor_.pos.x,
         anchor_.pos.y, anchor_.pos.z);
}

void App::frame() {
    XrFrameState fs{XR_TYPE_FRAME_STATE};
    if (!xr_.beginFrame(&fs)) return;

    // Watchdog: report frames whose CPU work (everything after xrWaitFrame) overran badly.
    struct Watchdog {
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        ~Watchdog() {
            const float ms =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (ms > 40.0f) LOGW("slow frame: %.0f ms of CPU work", ms);
        }
    } watchdog;

    std::vector<const XrCompositionLayerBaseHeader*> layers;
    if (fs.shouldRender) {
        const XrTime now = fs.predictedDisplayTime;
        float dt = lastTime_ ? static_cast<float>(now - lastTime_) * 1e-9f : 0.011f;
        dt = clampf(dt, 0.001f, 0.1f);
        lastTime_ = now;
        elapsed_ += dt;

        Pose head;
        const bool haveHead = xr_.locateSpace(xr_.viewSpace(), xr_.appSpace(), now, &head);
        const bool recenter = xr_.recenterPending() || recenterRequested_;
        recenterRequested_ = false;
        if (haveHead && (!anchorSet_ || recenter)) updateAnchor(head);
        if (!haveHead) head = Pose{};

        // User settings that act on subsystems the shell does not own.
        const Settings& cfg = shell_.settings();
        pointers_.setPokeEnabled(cfg.poke);
        pointers_.setSmoothing(cfg.smoothing);
        style_.showMesh = cfg.handMesh;
        style_.showSkeleton = cfg.skeleton;
        if (!rateApplied_) {
            rateApplied_ = true;
            if (cfg.refreshRate > 0) xr_.requestRefreshRate(static_cast<float>(cfg.refreshRate));
        }

        updateSimulation(head);
        hands_.update(xr_, now, head, dt);
        controllers_.sync(xr_, now);
        pointers_.update(dt, hands_, controllers_, shell_.panels(), anchor_, xr_.focused());

        // Controller extras: haptic clicks, menu button = launcher, Y = recenter.
        for (int i = 0; i < 2; ++i) {
            const bool controller = pointers_.visual(i).controller;
            const bool down = controller && pointers_.isDown(i);
            if (controller && down != prevDown_[i]) {
                controllers_.haptic(xr_, i, down ? 0.45f : 0.25f, down ? 0.03f : 0.02f);
            }
            prevDown_[i] = down;
        }
        if (controllers_.state(kLeft).menuPressed) shell_.setKickoffOpen(!shell_.kickoffOpen());
        if (controllers_.state(kLeft).secondaryPressed) recenterRequested_ = true;

        ShellInfo info;
        info.refreshRate = xr_.refreshRate();
        info.runtime = xr_.runtimeName();
        info.system = xr_.systemName();
        info.eyeW = xr_.eyeWidth();
        info.eyeH = xr_.eyeHeight();
        info.handsAvailable = hands_.available();
        info.handMesh = hands_.hasMesh();
        info.controller[0] = controllers_.state(0).active;
        info.controller[1] = controllers_.state(1).active;
        info.passthroughAvailable = false;
        shell_.update(dt, info, hands_, pointers_, anchor_, head);
        shell_.renderPanels(xr_, canvas_, dt);

        if (const XrCompositionLayerBaseHeader* env = env_.layer(xr_, anchor_)) layers.push_back(env);
        shell_.collectLayers(xr_, anchor_, head.p, layers);

        const bool needOverlay = hands_.hand(0).active || hands_.hand(1).active ||
                                 controllers_.state(0).active || controllers_.state(1).active ||
                                 pointers_.visual(0).mode != PointerVisual::kNone ||
                                 pointers_.visual(1).mode != PointerVisual::kNone;
        XrView views[2];
        XrViewStateFlags vf = 0;
        if (needOverlay && xr_.locateViews(now, views, &vf) &&
            (vf & XR_VIEW_STATE_ORIENTATION_VALID_BIT) && (vf & XR_VIEW_STATE_POSITION_VALID_BIT)) {
            if (renderOverlay(views, elapsed_)) {
                layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projLayer_));
            }
        }
    }
    xr_.endFrame(fs.predictedDisplayTime, layers);
    if (sceneReady_) shell_.frameDone();
}

bool App::handleKey(int androidKeyCode, bool down) {
    return sceneReady_ && shell_.linuxHost().androidKey(androidKeyCode, down);
}

bool App::renderOverlay(const XrView views[2], float time) {
    for (int eye = 0; eye < 2; ++eye) {
        Swapchain& sc = eyeSc_[eye];
        if (!xr_.acquireImage(sc)) return false;
        glBindFramebuffer(GL_FRAMEBUFFER, sc.fbos[sc.acquired]);
        glViewport(0, 0, sc.width, sc.height);
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(0, 0, 0, 0);
        glClearDepthf(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const Pose vp = toPose(views[eye].pose);
        const XrFovf& f = views[eye].fov;
        overlay_.render(Mat4::fromPose(vp.inverse()),
                        Mat4::projection(f.angleLeft, f.angleRight, f.angleUp, f.angleDown, 0.05f,
                                         40.0f),
                        vp.p, hands_, controllers_, pointers_, style_, time);

        const GLenum discard[1] = {GL_DEPTH_ATTACHMENT};
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, discard);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        xr_.releaseImage(sc);

        projViews_[eye] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        projViews_[eye].pose = views[eye].pose;
        projViews_[eye].fov = views[eye].fov;
        projViews_[eye].subImage.swapchain = sc.handle;
        projViews_[eye].subImage.imageRect = {{0, 0}, {sc.width, sc.height}};
        projViews_[eye].subImage.imageArrayIndex = 0;
    }
    projLayer_ = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projLayer_.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    projLayer_.space = xr_.appSpace();
    projLayer_.viewCount = 2;
    projLayer_.views = projViews_;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Debug channel: `adb shell am broadcast -n org.plasmaxr.launcher/.DebugReceiver --es cmd "..."`
// ---------------------------------------------------------------------------------------------

// A right hand held in front of the head (so it is always in view) aiming at a panel point.
void App::updateSimulation(const Pose& head) {
    hands_.setSimulated(simOn_);
    if (!simOn_) return;
    Panel* p = shell_.panelByName(simPanel_);
    if (!p) return;
    Vec3 target, normal;
    p->pxToWorld({simUv_.x * p->pxW, simUv_.y * p->pxH}, anchor_, &target, &normal);
    const Vec3 handPos =
        head.p + head.forward() * 0.45f + head.right() * 0.14f + head.up() * -0.14f;
    const Vec3 dir = (target - handPos).normalized();
    hands_.setSimPose(kRight, {Quat::lookRotation(dir), handPos}, simPinch_);
}

void App::processDebug() {
    for (int i = 0; i < 16; ++i) {
        const std::string cmd = Jni::pollDebugCommand();
        if (cmd.empty()) break;
        handleDebug(cmd);
    }
}

void App::handleDebug(const std::string& cmd) {
    LOGI("debug command: %s", cmd.c_str());
    std::istringstream is(cmd);
    std::string verb;
    is >> verb;
    if (!sceneReady_) return;

    if (verb == "snap") {
        snapshotPanels();
    } else if (verb == "ptr") {
        std::string name;
        is >> name;
        if (name == "off") {
            pointers_.clearVirtualPointer();
        } else {
            float u = 0.5f, v = 0.5f;
            int down = 0;
            is >> u >> v >> down;
            if (Panel* p = shell_.panelByName(name)) {
                pointers_.setVirtualPointer(p, {u * p->pxW, v * p->pxH}, down != 0);
            }
        }
    } else if (verb == "sim") {
        std::string name;
        is >> name;
        if (name == "off") {
            simOn_ = false;
        } else {
            simPanel_ = name;
            simOn_ = true;
            is >> simUv_.x >> simUv_.y >> simPinch_;
        }
    } else if (verb == "kickoff") {
        std::string a;
        is >> a;
        shell_.setKickoffOpen(a == "open");
    } else if (verb == "tab") {
        int t = 0;
        is >> t;
        shell_.setKickoffTab(t);
    } else if (verb == "overlay") {
        std::string what;
        int on = 1;
        is >> what >> on;
        const bool v = on != 0;
        if (what == "mesh") style_.showMesh = v;
        else if (what == "skeleton") style_.showSkeleton = v;
        else if (what == "ray") style_.showRays = v;
        else if (what == "cursor") style_.showCursors = v;
        LOGI("overlay: mesh=%d skeleton=%d ray=%d cursor=%d", style_.showMesh, style_.showSkeleton,
             style_.showRays, style_.showCursors);
    } else if (verb == "linux") {  // linux <shell command>: start a program in the rootfs
        std::string rest;
        std::getline(is >> std::ws, rest);
        LOGI("linux launch: %s -> %d", rest.c_str(), shell_.linuxHost().launch(rest) ? 1 : 0);
    } else if (verb == "wltype") {  // wltype <text>: type into the focused Linux window
        std::string rest;
        std::getline(is >> std::ws, rest);
        shell_.linuxHost().typeText(rest);
    } else if (verb == "wlkey") {   // wlkey <evdev code> <0|1>
        int code = 0, down = 1;
        is >> code >> down;
        shell_.linuxHost().key(static_cast<uint32_t>(code), down != 0);
    } else if (verb == "wltrace") {  // wltrace <0|1>: log every Wayland message
        int on = 1;
        is >> on;
        shell_.linuxHost().server().setTrace(on != 0);
    } else if (verb == "linuxscale") {
        int n = 2;
        is >> n;
        shell_.linuxHost().setScale(n);
    } else if (verb == "recenter") {
        recenterRequested_ = true;
    } else if (verb == "quit") {
        ANativeActivity_finish(app_->activity);
    }
}

void App::snapshotPanels() {
    for (Panel* p : shell_.panels()) {
        std::vector<uint8_t> px;
        if (p->snapshot(canvas_, px)) {
            Jni::savePng(p->name, p->pxW, p->pxH, px.data());
        } else {
            LOGW("snapshot failed for %s", p->name.c_str());
        }
    }
}

}  // namespace pxr
