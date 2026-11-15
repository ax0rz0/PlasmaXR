// Owns the OpenXR instance/session, the EGL/GLES context, reference spaces, swapchains and the
// frame loop plumbing. Everything higher level (hands, panels, shell) builds on top of this.
#pragma once

#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <jni.h>

#ifndef XR_USE_PLATFORM_ANDROID
#define XR_USE_PLATFORM_ANDROID
#endif
#ifndef XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_USE_GRAPHICS_API_OPENGL_ES
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "core/math.h"

namespace pxr {

// Which optional runtime features the headset reported (and we enabled).
struct XrFeatures {
    bool androidThreadSettings = false;
    bool cylinderLayer = false;
    bool equirect2Layer = false;
    bool handTracking = false;
    bool handAim = false;
    bool handMesh = false;
    bool handCapsules = false;
    bool handInteraction = false;
    bool passthrough = false;
    bool refreshRate = false;
    bool layerSettings = false;
    bool colorScaleBias = false;
    bool performanceSettings = false;
    bool eyeBufferMsaa = false;  // GL_EXT_multisampled_render_to_texture
};

// Function pointers for extension entry points (resolved after instance creation).
struct XrFns {
    PFN_xrGetOpenGLESGraphicsRequirementsKHR getGlesRequirements = nullptr;
    PFN_xrCreateHandTrackerEXT createHandTracker = nullptr;
    PFN_xrDestroyHandTrackerEXT destroyHandTracker = nullptr;
    PFN_xrLocateHandJointsEXT locateHandJoints = nullptr;
    PFN_xrGetHandMeshFB getHandMesh = nullptr;
    PFN_xrSetAndroidApplicationThreadKHR setAndroidThread = nullptr;
    PFN_xrEnumerateDisplayRefreshRatesFB enumRefreshRates = nullptr;
    PFN_xrGetDisplayRefreshRateFB getRefreshRate = nullptr;
    PFN_xrRequestDisplayRefreshRateFB requestRefreshRate = nullptr;
    PFN_xrPerfSettingsSetPerformanceLevelEXT setPerfLevel = nullptr;
    PFN_xrCreatePassthroughFB createPassthrough = nullptr;
    PFN_xrDestroyPassthroughFB destroyPassthrough = nullptr;
    PFN_xrPassthroughStartFB passthroughStart = nullptr;
    PFN_xrPassthroughPauseFB passthroughPause = nullptr;
    PFN_xrCreatePassthroughLayerFB createPassthroughLayer = nullptr;
    PFN_xrDestroyPassthroughLayerFB destroyPassthroughLayer = nullptr;
    PFN_xrPassthroughLayerPauseFB passthroughLayerPause = nullptr;
    PFN_xrPassthroughLayerResumeFB passthroughLayerResume = nullptr;
};

struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int width = 0;
    int height = 0;
    std::vector<GLuint> textures;
    std::vector<GLuint> fbos;
    GLuint depth = 0;  // optional depth renderbuffer shared by all images
    uint32_t acquired = 0;
    bool valid() const { return handle != XR_NULL_HANDLE; }
};

inline Pose toPose(const XrPosef& p) {
    return {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            {p.position.x, p.position.y, p.position.z}};
}
inline XrPosef toXr(const Pose& p) {
    XrPosef r;
    r.orientation = {p.q.x, p.q.y, p.q.z, p.q.w};
    r.position = {p.p.x, p.p.y, p.p.z};
    return r;
}

class XrContext {
public:
    ~XrContext() { shutdown(); }

    // Loader, instance, system, EGL. Does not need the headset to be awake.
    bool init(android_app* app);
    bool createSession();
    void destroySession();
    void shutdown();

    // Android lifecycle hooks.
    void setResumed(bool resumed) { resumed_ = resumed; }
    bool resumed() const { return resumed_; }

    // Drains the OpenXR event queue and drives session begin/end. Call once per loop iteration.
    void pollEvents();
    bool sessionRunning() const { return sessionRunning_; }
    bool exitRequested() const { return exitRequested_; }
    XrSessionState sessionState() const { return state_; }
    bool focused() const { return state_ == XR_SESSION_STATE_FOCUSED; }
    bool recenterPending() { bool r = recenter_; recenter_ = false; return r; }

    // Frame loop. beginFrame() = xrWaitFrame + xrBeginFrame.
    bool beginFrame(XrFrameState* state);
    bool endFrame(XrTime displayTime, const std::vector<const XrCompositionLayerBaseHeader*>& layers);
    bool locateViews(XrTime displayTime, XrView views[2], XrViewStateFlags* flags = nullptr);
    bool locateSpace(XrSpace space, XrSpace base, XrTime time, Pose* out,
                     XrSpaceLocationFlags* flagsOut = nullptr);

    // Swapchains (GLES). Images are linear-blended sRGB when the runtime offers it.
    bool createSwapchain(int width, int height, Swapchain& out, bool withDepth = false,
                         int msaaSamples = 1);
    bool acquireImage(Swapchain& sc);
    void releaseImage(Swapchain& sc);
    void destroySwapchain(Swapchain& sc);
    GLenum colorFormat() const { return static_cast<GLenum>(colorFormat_); }

    XrInstance instance() const { return instance_; }
    XrSession session() const { return session_; }
    XrSystemId systemId() const { return systemId_; }
    XrSpace appSpace() const { return appSpace_; }
    XrSpace viewSpace() const { return viewSpace_; }
    const XrFeatures& features() const { return features_; }
    const XrFns& fn() const { return fn_; }
    int eyeWidth() const { return eyeWidth_; }
    int eyeHeight() const { return eyeHeight_; }
    float refreshRate() const { return refreshRate_; }
    // Asks the runtime for a display rate in Hz (0 = system default). Switching can flash the display.
    bool requestRefreshRate(float hz);
    const std::string& runtimeName() const { return runtimeName_; }
    const std::string& systemName() const { return systemName_; }
    bool systemSupportsHands() const { return systemHands_; }
    bool hasExtension(const char* name) const { return supported_.count(name) > 0; }
    android_app* app() const { return app_; }

    bool check(XrResult r, const char* what) const;

private:
    bool initEgl();
    bool loadFunctions();
    void handleSessionState(const XrEventDataSessionStateChanged& e);
    void beginSessionIfReady();
    void applyPerformanceHints();

    android_app* app_ = nullptr;

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace appSpace_ = XR_NULL_HANDLE;
    XrSpace viewSpace_ = XR_NULL_HANDLE;

    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLConfig eglConfig_ = nullptr;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    EGLSurface eglSurface_ = EGL_NO_SURFACE;

    std::set<std::string> supported_;
    XrFeatures features_;
    XrFns fn_;

    XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
    bool resumed_ = false;
    bool sessionRunning_ = false;
    bool exitRequested_ = false;
    bool recenter_ = false;
    bool systemHands_ = false;

    int64_t colorFormat_ = GL_RGBA8;
    int eyeWidth_ = 0;
    int eyeHeight_ = 0;
    float refreshRate_ = 0;
    std::string runtimeName_;
    std::string systemName_;
};

}  // namespace pxr
