#include "xr/xr_context.h"

#include <GLES2/gl2ext.h>
#include <unistd.h>

#include <cstring>

#include "core/log.h"

namespace pxr {

namespace {

PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC pglFramebufferTexture2DMultisampleEXT = nullptr;
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC pglRenderbufferStorageMultisampleEXT = nullptr;

const XrPosef kIdentityPose = {{0, 0, 0, 1}, {0, 0, 0}};

// The shell keeps whatever refresh rate the system picked. Switching display modes is visible to
// the user, so a lower rate should be an explicit setting rather than a startup default.
constexpr bool kRequestRefreshRate = false;

}  // namespace

bool XrContext::check(XrResult r, const char* what) const {
    if (XR_SUCCEEDED(r)) return true;
    char buf[XR_MAX_RESULT_STRING_SIZE] = "?";
    if (instance_ != XR_NULL_HANDLE) xrResultToString(instance_, r, buf);
    LOGE("OpenXR error in %s: %s (%d)", what, buf, static_cast<int>(r));
    return false;
}

// ---------------------------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------------------------

bool XrContext::init(android_app* app) {
    app_ = app;

    PFN_xrInitializeLoaderKHR initLoader = nullptr;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                          reinterpret_cast<PFN_xrVoidFunction*>(&initLoader));
    if (!initLoader) {
        LOGE("xrInitializeLoaderKHR is not exported by the loader");
        return false;
    }
    XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loaderInfo.applicationVM = app->activity->vm;
    loaderInfo.applicationContext = app->activity->clazz;
    if (!check(initLoader(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo)),
               "xrInitializeLoaderKHR")) {
        return false;
    }

    uint32_t extCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
    std::vector<XrExtensionProperties> props(extCount, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, props.data());
    std::string line;
    for (auto& p : props) {
        supported_.insert(p.extensionName);
        line += p.extensionName;
        line += ' ';
        if (line.size() > 700) {
            LOGI("OpenXR extensions: %s", line.c_str());
            line.clear();
        }
    }
    if (!line.empty()) LOGI("OpenXR extensions: %s", line.c_str());
    LOGI("OpenXR: runtime exposes %u instance extensions", extCount);

    std::vector<const char*> enabled;
    auto want = [&](const char* name) {
        if (supported_.count(name)) {
            enabled.push_back(name);
            return true;
        }
        LOGW("OpenXR extension not available: %s", name);
        return false;
    };

    if (!want("XR_KHR_android_create_instance") || !want("XR_KHR_opengl_es_enable")) {
        LOGE("Required OpenXR extensions are missing");
        return false;
    }
    features_.androidThreadSettings = want("XR_KHR_android_thread_settings");
    features_.cylinderLayer = want("XR_KHR_composition_layer_cylinder");
    features_.equirect2Layer = want("XR_KHR_composition_layer_equirect2");
    features_.handTracking = want("XR_EXT_hand_tracking");
    if (features_.handTracking) {
        features_.handAim = want("XR_FB_hand_tracking_aim");
        features_.handMesh = want("XR_FB_hand_tracking_mesh");
        features_.handCapsules = want("XR_FB_hand_tracking_capsules");
    }
    features_.passthrough = want("XR_FB_passthrough");
    features_.refreshRate = want("XR_FB_display_refresh_rate");
    features_.layerSettings = want("XR_FB_composition_layer_settings");
    features_.colorScaleBias = want("XR_KHR_composition_layer_color_scale_bias");
    features_.performanceSettings = want("XR_EXT_performance_settings");

    XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidInfo.applicationVM = app->activity->vm;
    androidInfo.applicationActivity = app->activity->clazz;

    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    ci.next = &androidInfo;
    strncpy(ci.applicationInfo.applicationName, "Plasma XR", XR_MAX_APPLICATION_NAME_SIZE - 1);
    ci.applicationInfo.applicationVersion = 1;
    strncpy(ci.applicationInfo.engineName, "PlasmaXR", XR_MAX_ENGINE_NAME_SIZE - 1);
    ci.applicationInfo.engineVersion = 1;
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
    ci.enabledExtensionNames = enabled.data();
    if (!check(xrCreateInstance(&ci, &instance_), "xrCreateInstance")) return false;

    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(instance_, &ip);
    runtimeName_ = ip.runtimeName;
    LOGI("OpenXR runtime: %s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
         XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

    XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!check(xrGetSystem(instance_, &sgi, &systemId_), "xrGetSystem")) return false;

    XrSystemHandTrackingPropertiesEXT handProps{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    if (features_.handTracking) sp.next = &handProps;
    xrGetSystemProperties(instance_, systemId_, &sp);
    systemName_ = sp.systemName;
    systemHands_ = features_.handTracking && handProps.supportsHandTracking;
    LOGI("OpenXR system: '%s' vendor=0x%x handTracking=%d maxLayers=%u", sp.systemName,
         sp.vendorId, systemHands_ ? 1 : 0, sp.graphicsProperties.maxLayerCount);

    if (!loadFunctions()) return false;
    if (!initEgl()) return false;

    XrGraphicsRequirementsOpenGLESKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (!check(fn_.getGlesRequirements(instance_, systemId_, &req), "GLES requirements")) {
        return false;
    }
    LOGI("GLES requirements: min %u.%u max %u.%u", XR_VERSION_MAJOR(req.minApiVersionSupported),
         XR_VERSION_MINOR(req.minApiVersionSupported), XR_VERSION_MAJOR(req.maxApiVersionSupported),
         XR_VERSION_MINOR(req.maxApiVersionSupported));

    uint32_t viewCount = 0;
    xrEnumerateViewConfigurationViews(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                      0, &viewCount, nullptr);
    std::vector<XrViewConfigurationView> views(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xrEnumerateViewConfigurationViews(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                      viewCount, &viewCount, views.data());
    if (viewCount < 2) {
        LOGE("Expected a stereo view configuration, got %u views", viewCount);
        return false;
    }
    eyeWidth_ = static_cast<int>(views[0].recommendedImageRectWidth);
    eyeHeight_ = static_cast<int>(views[0].recommendedImageRectHeight);
    LOGI("Recommended eye buffer: %dx%d (max %ux%u)", eyeWidth_, eyeHeight_,
         views[0].maxImageRectWidth, views[0].maxImageRectHeight);
    return true;
}

bool XrContext::loadFunctions() {
#define PXR_LOAD(member, name)                                                              \
    xrGetInstanceProcAddr(instance_, name, reinterpret_cast<PFN_xrVoidFunction*>(&fn_.member))

    PXR_LOAD(getGlesRequirements, "xrGetOpenGLESGraphicsRequirementsKHR");
    if (!fn_.getGlesRequirements) {
        LOGE("xrGetOpenGLESGraphicsRequirementsKHR missing");
        return false;
    }
    if (features_.handTracking) {
        PXR_LOAD(createHandTracker, "xrCreateHandTrackerEXT");
        PXR_LOAD(destroyHandTracker, "xrDestroyHandTrackerEXT");
        PXR_LOAD(locateHandJoints, "xrLocateHandJointsEXT");
        if (!fn_.createHandTracker || !fn_.destroyHandTracker || !fn_.locateHandJoints) {
            features_.handTracking = false;
        }
    }
    if (features_.handMesh) {
        PXR_LOAD(getHandMesh, "xrGetHandMeshFB");
        if (!fn_.getHandMesh) features_.handMesh = false;
    }
    if (features_.androidThreadSettings) {
        PXR_LOAD(setAndroidThread, "xrSetAndroidApplicationThreadKHR");
    }
    if (features_.refreshRate) {
        PXR_LOAD(enumRefreshRates, "xrEnumerateDisplayRefreshRatesFB");
        PXR_LOAD(getRefreshRate, "xrGetDisplayRefreshRateFB");
        PXR_LOAD(requestRefreshRate, "xrRequestDisplayRefreshRateFB");
    }
    if (features_.performanceSettings) {
        PXR_LOAD(setPerfLevel, "xrPerfSettingsSetPerformanceLevelEXT");
    }
    if (features_.passthrough) {
        PXR_LOAD(createPassthrough, "xrCreatePassthroughFB");
        PXR_LOAD(destroyPassthrough, "xrDestroyPassthroughFB");
        PXR_LOAD(passthroughStart, "xrPassthroughStartFB");
        PXR_LOAD(passthroughPause, "xrPassthroughPauseFB");
        PXR_LOAD(createPassthroughLayer, "xrCreatePassthroughLayerFB");
        PXR_LOAD(destroyPassthroughLayer, "xrDestroyPassthroughLayerFB");
        PXR_LOAD(passthroughLayerPause, "xrPassthroughLayerPauseFB");
        PXR_LOAD(passthroughLayerResume, "xrPassthroughLayerResumeFB");
        if (!fn_.createPassthrough || !fn_.createPassthroughLayer || !fn_.passthroughStart) {
            features_.passthrough = false;
        }
    }
#undef PXR_LOAD
    return true;
}

bool XrContext::initEgl() {
    eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay_ == EGL_NO_DISPLAY || !eglInitialize(eglDisplay_, nullptr, nullptr)) {
        LOGE("eglInitialize failed");
        return false;
    }
    const EGLint configAttribs[] = {EGL_RED_SIZE,        8,
                                    EGL_GREEN_SIZE,      8,
                                    EGL_BLUE_SIZE,       8,
                                    EGL_ALPHA_SIZE,      8,
                                    EGL_DEPTH_SIZE,      0,
                                    EGL_STENCIL_SIZE,    0,
                                    EGL_SAMPLES,         0,
                                    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                    EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
                                    EGL_NONE};
    EGLint numConfigs = 0;
    if (!eglChooseConfig(eglDisplay_, configAttribs, &eglConfig_, 1, &numConfigs) ||
        numConfigs < 1) {
        LOGE("eglChooseConfig failed");
        return false;
    }
    const EGLint ctxAttribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2,
                                 EGL_NONE};
    eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, ctxAttribs);
    if (eglContext_ == EGL_NO_CONTEXT) {
        const EGLint fallback[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, fallback);
    }
    if (eglContext_ == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed: 0x%x", eglGetError());
        return false;
    }
    const EGLint pbAttribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    eglSurface_ = eglCreatePbufferSurface(eglDisplay_, eglConfig_, pbAttribs);
    if (eglSurface_ == EGL_NO_SURFACE ||
        !eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
        LOGE("eglMakeCurrent failed: 0x%x", eglGetError());
        return false;
    }
    LOGI("GL: %s | %s | %s", glGetString(GL_VENDOR), glGetString(GL_RENDERER),
         glGetString(GL_VERSION));
    const char* exts = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (exts && strstr(exts, "GL_EXT_multisampled_render_to_texture")) {
        pglFramebufferTexture2DMultisampleEXT =
            reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC>(
                eglGetProcAddress("glFramebufferTexture2DMultisampleEXT"));
        pglRenderbufferStorageMultisampleEXT =
            reinterpret_cast<PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC>(
                eglGetProcAddress("glRenderbufferStorageMultisampleEXT"));
        features_.eyeBufferMsaa =
            pglFramebufferTexture2DMultisampleEXT && pglRenderbufferStorageMultisampleEXT;
    }
    LOGI("GL_EXT_multisampled_render_to_texture: %d", features_.eyeBufferMsaa ? 1 : 0);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------------------------

bool XrContext::createSession() {
    XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    binding.display = eglDisplay_;
    binding.config = eglConfig_;
    binding.context = eglContext_;

    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &binding;
    sci.systemId = systemId_;
    if (!check(xrCreateSession(instance_, &sci, &session_), "xrCreateSession")) return false;

    uint32_t formatCount = 0;
    xrEnumerateSwapchainFormats(session_, 0, &formatCount, nullptr);
    std::vector<int64_t> formats(formatCount);
    xrEnumerateSwapchainFormats(session_, formatCount, &formatCount, formats.data());
    std::string fmtLine;
    for (int64_t f : formats) fmtLine += std::to_string(f) + " ";
    LOGI("Swapchain formats: %s", fmtLine.c_str());
    colorFormat_ = formats.empty() ? GL_RGBA8 : formats[0];
    for (int64_t f : formats) {
        if (f == GL_SRGB8_ALPHA8) {
            colorFormat_ = f;
            break;
        }
    }
    LOGI("Using swapchain format 0x%llx", static_cast<long long>(colorFormat_));

    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.poseInReferenceSpace = kIdentityPose;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!check(xrCreateReferenceSpace(session_, &rs, &appSpace_), "create LOCAL space")) {
        return false;
    }
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!check(xrCreateReferenceSpace(session_, &rs, &viewSpace_), "create VIEW space")) {
        return false;
    }
    return true;
}

void XrContext::destroySession() {
    if (viewSpace_ != XR_NULL_HANDLE) xrDestroySpace(viewSpace_);
    if (appSpace_ != XR_NULL_HANDLE) xrDestroySpace(appSpace_);
    viewSpace_ = appSpace_ = XR_NULL_HANDLE;
    if (session_ != XR_NULL_HANDLE) {
        if (sessionRunning_) xrEndSession(session_);
        xrDestroySession(session_);
    }
    session_ = XR_NULL_HANDLE;
    sessionRunning_ = false;
}

void XrContext::shutdown() {
    destroySession();
    if (instance_ != XR_NULL_HANDLE) {
        xrDestroyInstance(instance_);
        instance_ = XR_NULL_HANDLE;
    }
    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglSurface_ != EGL_NO_SURFACE) eglDestroySurface(eglDisplay_, eglSurface_);
        if (eglContext_ != EGL_NO_CONTEXT) eglDestroyContext(eglDisplay_, eglContext_);
        eglTerminate(eglDisplay_);
    }
    eglDisplay_ = EGL_NO_DISPLAY;
    eglSurface_ = EGL_NO_SURFACE;
    eglContext_ = EGL_NO_CONTEXT;
}

// ---------------------------------------------------------------------------------------------
// Events and session lifecycle
// ---------------------------------------------------------------------------------------------

void XrContext::pollEvents() {
    if (instance_ == XR_NULL_HANDLE) return;
    for (;;) {
        XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult r = xrPollEvent(instance_, &ev);
        if (r == XR_EVENT_UNAVAILABLE) break;
        if (XR_FAILED(r)) {
            check(r, "xrPollEvent");
            break;
        }
        switch (ev.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                handleSessionState(*reinterpret_cast<XrEventDataSessionStateChanged*>(&ev));
                break;
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                LOGW("OpenXR instance loss pending");
                exitRequested_ = true;
                break;
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                LOGI("Reference space change pending (recenter)");
                recenter_ = true;
                break;
            case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB: {
                auto* e = reinterpret_cast<XrEventDataDisplayRefreshRateChangedFB*>(&ev);
                refreshRate_ = e->toDisplayRefreshRate;
                LOGI("Display refresh rate: %.1f -> %.1f Hz", e->fromDisplayRefreshRate,
                     e->toDisplayRefreshRate);
                break;
            }
            default:
                break;
        }
    }
    beginSessionIfReady();
}

void XrContext::handleSessionState(const XrEventDataSessionStateChanged& e) {
    static const char* names[] = {"UNKNOWN", "IDLE",    "READY",    "SYNCHRONIZED", "VISIBLE",
                                  "FOCUSED", "STOPPING", "LOSS_PENDING", "EXITING"};
    LOGI("Session state -> %s", e.state < 9 ? names[e.state] : "?");
    state_ = e.state;
    switch (e.state) {
        case XR_SESSION_STATE_STOPPING:
            if (sessionRunning_) {
                check(xrEndSession(session_), "xrEndSession");
                sessionRunning_ = false;
            }
            break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            exitRequested_ = true;
            break;
        default:
            break;
    }
}

void XrContext::beginSessionIfReady() {
    if (sessionRunning_ || session_ == XR_NULL_HANDLE) return;
    if (state_ != XR_SESSION_STATE_READY || !resumed_) return;
    XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    if (!check(xrBeginSession(session_, &bi), "xrBeginSession")) return;
    sessionRunning_ = true;
    LOGI("Session running");
    applyPerformanceHints();
}

void XrContext::applyPerformanceHints() {
    if (fn_.setAndroidThread) {
        pid_t tid = gettid();
        fn_.setAndroidThread(session_, XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, tid);
        fn_.setAndroidThread(session_, XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR, tid);
    }
    if (fn_.enumRefreshRates && fn_.requestRefreshRate) {
        uint32_t n = 0;
        fn_.enumRefreshRates(session_, 0, &n, nullptr);
        std::vector<float> rates(n);
        fn_.enumRefreshRates(session_, n, &n, rates.data());
        std::string line;
        float best = 0;
        for (float r : rates) {
            line += std::to_string(static_cast<int>(r + 0.5f)) + " ";
            // A UI shell does not need more than 72 Hz; lower rates save battery and heat.
            if (r <= 72.5f && r > best) best = r;
        }
        LOGI("Supported refresh rates: %s", line.c_str());
        if (kRequestRefreshRate && best > 0) {
            check(fn_.requestRefreshRate(session_, best), "request refresh rate");
        }
        if (fn_.getRefreshRate) fn_.getRefreshRate(session_, &refreshRate_);
        LOGI("Refresh rate: %.1f Hz", refreshRate_);
    }
}

bool XrContext::requestRefreshRate(float hz) {
    if (!fn_.requestRefreshRate || session_ == XR_NULL_HANDLE) return false;
    LOGI("Requesting display refresh rate: %.0f Hz", hz);
    return check(fn_.requestRefreshRate(session_, hz), "request refresh rate");
}

// ---------------------------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------------------------

bool XrContext::beginFrame(XrFrameState* fs) {
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    *fs = {XR_TYPE_FRAME_STATE};
    if (!check(xrWaitFrame(session_, &wi, fs), "xrWaitFrame")) return false;
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    return check(xrBeginFrame(session_, &bi), "xrBeginFrame");
}

bool XrContext::endFrame(XrTime displayTime,
                         const std::vector<const XrCompositionLayerBaseHeader*>& layers) {
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = displayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = static_cast<uint32_t>(layers.size());
    ei.layers = layers.data();
    const XrResult r = xrEndFrame(session_, &ei);
    if (r == XR_FRAME_DISCARDED) {  // a success code, so it would otherwise go unnoticed
        static int discarded = 0;
        if (discarded++ % 120 == 0) {
            std::string kinds;
            for (const auto* l : layers) kinds += std::to_string(static_cast<int>(l->type)) + " ";
            LOGW("xrEndFrame: frame DISCARDED by the runtime (%u layers, types: %s)", ei.layerCount,
                 kinds.c_str());
        }
    }
    return check(r, "xrEndFrame");
}

bool XrContext::locateViews(XrTime displayTime, XrView views[2], XrViewStateFlags* flags) {
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = displayTime;
    li.space = appSpace_;
    XrViewState vs{XR_TYPE_VIEW_STATE};
    views[0] = {XR_TYPE_VIEW};
    views[1] = {XR_TYPE_VIEW};
    uint32_t count = 0;
    if (!check(xrLocateViews(session_, &li, &vs, 2, &count, views), "xrLocateViews")) return false;
    if (flags) *flags = vs.viewStateFlags;
    return count == 2;
}

bool XrContext::locateSpace(XrSpace space, XrSpace base, XrTime time, Pose* out,
                            XrSpaceLocationFlags* flagsOut) {
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(space, base, time, &loc))) return false;
    if (flagsOut) *flagsOut = loc.locationFlags;
    const XrSpaceLocationFlags need =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((loc.locationFlags & need) != need) return false;
    *out = toPose(loc.pose);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Swapchains
// ---------------------------------------------------------------------------------------------

bool XrContext::createSwapchain(int width, int height, Swapchain& out, bool withDepth,
                                int msaaSamples) {
    XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    sci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    sci.format = colorFormat_;
    sci.sampleCount = 1;
    sci.width = static_cast<uint32_t>(width);
    sci.height = static_cast<uint32_t>(height);
    sci.faceCount = 1;
    sci.arraySize = 1;
    sci.mipCount = 1;
    if (!check(xrCreateSwapchain(session_, &sci, &out.handle), "xrCreateSwapchain")) return false;
    out.width = width;
    out.height = height;

    uint32_t count = 0;
    xrEnumerateSwapchainImages(out.handle, 0, &count, nullptr);
    std::vector<XrSwapchainImageOpenGLESKHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    xrEnumerateSwapchainImages(out.handle, count, &count,
                               reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));

    const bool msaa = msaaSamples > 1 && features_.eyeBufferMsaa;
    if (withDepth) {
        glGenRenderbuffers(1, &out.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, out.depth);
        if (msaa) {
            pglRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, msaaSamples,
                                                 GL_DEPTH_COMPONENT24, width, height);
        } else {
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        }
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }
    for (uint32_t i = 0; i < count; ++i) {
        GLuint tex = images[i].image;
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        if (msaa) {
            pglFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                  GL_TEXTURE_2D, tex, 0, msaaSamples);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        }
        if (withDepth) {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                      out.depth);
        }
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            LOGE("Swapchain FBO incomplete: 0x%x (%dx%d msaa=%d)", status, width, height,
                 msaa ? msaaSamples : 1);
        }
        out.textures.push_back(tex);
        out.fbos.push_back(fbo);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool XrContext::acquireImage(Swapchain& sc) {
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!check(xrAcquireSwapchainImage(sc.handle, &ai, &sc.acquired), "xrAcquireSwapchainImage")) {
        return false;
    }
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    return check(xrWaitSwapchainImage(sc.handle, &wi), "xrWaitSwapchainImage");
}

void XrContext::releaseImage(Swapchain& sc) {
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    check(xrReleaseSwapchainImage(sc.handle, &ri), "xrReleaseSwapchainImage");
}

void XrContext::destroySwapchain(Swapchain& sc) {
    if (!sc.valid()) return;
    for (GLuint fbo : sc.fbos) glDeleteFramebuffers(1, &fbo);
    if (sc.depth) glDeleteRenderbuffers(1, &sc.depth);
    xrDestroySwapchain(sc.handle);
    sc = Swapchain{};
}

}  // namespace pxr
