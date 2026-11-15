#include "platform/jni_bridge.h"

#include <android/bitmap.h>

#include "core/log.h"

namespace pxr {

namespace {

JavaVM* g_vm = nullptr;
jobject g_activity = nullptr;
jclass g_bridge = nullptr;

jmethodID m_linuxRoot, m_linuxSocketDir, m_linuxInstalled, m_launchLinux;
jmethodID m_listApps, m_loadAppIcon, m_launch, m_renderText, m_renderGlyph, m_status, m_getPref,
    m_setPref, m_playSound, m_pollDebug, m_savePng;

JNIEnv* env() {
    JNIEnv* e = nullptr;
    if (!g_vm) return nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_EDETACHED) {
        g_vm->AttachCurrentThread(&e, nullptr);
    }
    return e;
}

// Local references pile up forever on a thread that never returns to Java, so every call scopes
// its references in a frame.
struct LocalFrame {
    JNIEnv* e;
    explicit LocalFrame(JNIEnv* env_, int capacity = 48) : e(env_) { e->PushLocalFrame(capacity); }
    ~LocalFrame() { e->PopLocalFrame(nullptr); }
};

bool clearException(JNIEnv* e, const char* what) {
    if (!e->ExceptionCheck()) return false;
    LOGE("Java exception in %s", what);
    e->ExceptionDescribe();
    e->ExceptionClear();
    return true;
}

// Modified-UTF-8 (NewStringUTF) mangles 4-byte sequences, so convert via UTF-16 ourselves.
std::u16string utf8ToUtf16(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        uint32_t cp = 0xFFFD;
        int len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6 && i + 1 < s.size()) {
            cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
            len = 2;
        } else if ((c >> 4) == 0xE && i + 2 < s.size()) {
            cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
            len = 3;
        } else if ((c >> 3) == 0x1E && i + 3 < s.size()) {
            cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) |
                 (s[i + 3] & 0x3F);
            len = 4;
        }
        i += len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

std::string utf16ToUtf8(const jchar* s, jsize n) {
    std::string out;
    out.reserve(n);
    for (jsize i = 0; i < n; ++i) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            ++i;
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

jstring toJString(JNIEnv* e, const std::string& s) {
    std::u16string u = utf8ToUtf16(s);
    return e->NewString(reinterpret_cast<const jchar*>(u.data()), static_cast<jsize>(u.size()));
}

std::string toStdString(JNIEnv* e, jstring js) {
    if (!js) return {};
    const jchar* chars = e->GetStringChars(js, nullptr);
    std::string out = utf16ToUtf8(chars, e->GetStringLength(js));
    e->ReleaseStringChars(js, chars);
    return out;
}

PixelBuffer readBitmap(JNIEnv* e, jobject bmp) {
    PixelBuffer pb;
    if (!bmp) return pb;
    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(e, bmp, &info) != ANDROID_BITMAP_RESULT_SUCCESS) return pb;
    if (info.format != ANDROID_BITMAP_FORMAT_A_8 && info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGW("Unsupported bitmap format %d", info.format);
        return pb;
    }
    void* px = nullptr;
    if (AndroidBitmap_lockPixels(e, bmp, &px) != ANDROID_BITMAP_RESULT_SUCCESS || !px) return pb;
    pb.width = static_cast<int>(info.width);
    pb.height = static_cast<int>(info.height);
    pb.stride = static_cast<int>(info.stride);
    pb.alpha8 = info.format == ANDROID_BITMAP_FORMAT_A_8;
    const uint8_t* src = static_cast<const uint8_t*>(px);
    pb.data.assign(src, src + static_cast<size_t>(info.stride) * info.height);
    AndroidBitmap_unlockPixels(e, bmp);
    return pb;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

constexpr char kSep = '\x1f';

}  // namespace

void Jni::init(android_app* app) {
    g_vm = app->activity->vm;
    JNIEnv* e = env();
    g_activity = e->NewGlobalRef(app->activity->clazz);

    jclass actCls = e->GetObjectClass(g_activity);
    jmethodID getLoader = e->GetMethodID(actCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = e->CallObjectMethod(g_activity, getLoader);
    jclass loaderCls = e->FindClass("java/lang/ClassLoader");
    jmethodID loadClass =
        e->GetMethodID(loaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = e->NewStringUTF("org.plasmaxr.launcher.Bridge");
    jclass local = static_cast<jclass>(e->CallObjectMethod(loader, loadClass, name));
    if (clearException(e, "load Bridge") || !local) {
        LOGE("Could not load org.plasmaxr.launcher.Bridge");
        return;
    }
    g_bridge = static_cast<jclass>(e->NewGlobalRef(local));

#define PXR_STATIC(var, nm, sig) var = e->GetStaticMethodID(g_bridge, nm, sig)
    PXR_STATIC(m_linuxRoot, "linuxRoot", "()Ljava/lang/String;");
    PXR_STATIC(m_linuxSocketDir, "linuxSocketDir", "()Ljava/lang/String;");
    PXR_STATIC(m_linuxInstalled, "linuxInstalled", "()Z");
    PXR_STATIC(m_launchLinux, "launchLinux", "(Ljava/lang/String;)Z");
    PXR_STATIC(m_listApps, "listApps", "()[Ljava/lang/String;");
    PXR_STATIC(m_loadAppIcon, "loadAppIcon",
               "(Ljava/lang/String;Ljava/lang/String;I)Landroid/graphics/Bitmap;");
    PXR_STATIC(m_launch, "launch", "(Ljava/lang/String;Ljava/lang/String;)Z");
    PXR_STATIC(m_renderText, "renderText", "(Ljava/lang/String;FII)Landroid/graphics/Bitmap;");
    PXR_STATIC(m_renderGlyph, "renderGlyph", "(Ljava/lang/String;I)Landroid/graphics/Bitmap;");
    PXR_STATIC(m_status, "status", "()Ljava/lang/String;");
    PXR_STATIC(m_getPref, "getPref", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    PXR_STATIC(m_setPref, "setPref", "(Ljava/lang/String;Ljava/lang/String;)V");
    PXR_STATIC(m_playSound, "playSound", "(I)V");
    PXR_STATIC(m_pollDebug, "pollDebugCommand", "()Ljava/lang/String;");
    PXR_STATIC(m_savePng, "savePng", "(Ljava/lang/String;II[B)Z");
#undef PXR_STATIC
    clearException(e, "resolve Bridge methods");
    e->DeleteLocalRef(actCls);
    e->DeleteLocalRef(loader);
    e->DeleteLocalRef(loaderCls);
    e->DeleteLocalRef(name);
    e->DeleteLocalRef(local);
    LOGI("JNI bridge ready");
}

void Jni::detachThread() {
    if (g_vm) g_vm->DetachCurrentThread();
}

std::vector<AppInfo> Jni::listApps() {
    std::vector<AppInfo> apps;
    JNIEnv* e = env();
    if (!e || !g_bridge) return apps;
    LocalFrame frame(e, 512);
    jobjectArray arr = static_cast<jobjectArray>(e->CallStaticObjectMethod(g_bridge, m_listApps));
    if (clearException(e, "listApps") || !arr) return apps;
    jsize n = e->GetArrayLength(arr);
    for (jsize i = 0; i < n; ++i) {
        jstring js = static_cast<jstring>(e->GetObjectArrayElement(arr, i));
        std::vector<std::string> f = split(toStdString(e, js), kSep);
        e->DeleteLocalRef(js);
        if (f.size() < 4) continue;
        AppInfo a;
        a.pkg = f[0];
        a.activity = f[1];
        a.label = f[2];
        int flags = atoi(f[3].c_str());
        a.vr = (flags & 1) != 0;
        a.system = (flags & 2) != 0;
        apps.push_back(std::move(a));
    }
    return apps;
}

PixelBuffer Jni::loadAppIcon(const std::string& pkg, const std::string& activity, int sizePx) {
    JNIEnv* e = env();
    if (!e || !g_bridge) return {};
    LocalFrame frame(e);
    jstring jp = toJString(e, pkg);
    jstring ja = toJString(e, activity);
    jobject bmp = e->CallStaticObjectMethod(g_bridge, m_loadAppIcon, jp, ja, sizePx);
    if (clearException(e, "loadAppIcon")) return {};
    return readBitmap(e, bmp);
}

bool Jni::launchApp(const std::string& pkg, const std::string& activity) {
    JNIEnv* e = env();
    if (!e || !g_bridge) return false;
    LocalFrame frame(e);
    jstring jp = toJString(e, pkg);
    jstring ja = toJString(e, activity);
    jboolean ok = e->CallStaticBooleanMethod(g_bridge, m_launch, jp, ja);
    if (clearException(e, "launch")) return false;
    return ok == JNI_TRUE;
}

std::string Jni::linuxRoot() {
    JNIEnv* e = env();
    if (!e || !g_bridge) return {};
    LocalFrame frame(e);
    jstring js = static_cast<jstring>(e->CallStaticObjectMethod(g_bridge, m_linuxRoot));
    if (clearException(e, "linuxRoot") || !js) return {};
    return toStdString(e, js);
}

std::string Jni::linuxSocketDir() {
    JNIEnv* e = env();
    if (!e || !g_bridge) return {};
    LocalFrame frame(e);
    jstring js = static_cast<jstring>(e->CallStaticObjectMethod(g_bridge, m_linuxSocketDir));
    if (clearException(e, "linuxSocketDir") || !js) return {};
    return toStdString(e, js);
}

bool Jni::linuxInstalled() {
    JNIEnv* e = env();
    if (!e || !g_bridge) return false;
    jboolean ok = e->CallStaticBooleanMethod(g_bridge, m_linuxInstalled);
    if (clearException(e, "linuxInstalled")) return false;
    return ok == JNI_TRUE;
}

bool Jni::launchLinux(const std::string& shellCommand) {
    JNIEnv* e = env();
    if (!e || !g_bridge) return false;
    LocalFrame frame(e);
    jstring jc = toJString(e, shellCommand);
    jboolean ok = e->CallStaticBooleanMethod(g_bridge, m_launchLinux, jc);
    if (clearException(e, "launchLinux")) return false;
    return ok == JNI_TRUE;
}

PixelBuffer Jni::renderText(const std::string& utf8, float sizePx, int weight, int maxWidth) {
    JNIEnv* e = env();
    if (!e || !g_bridge) return {};
    LocalFrame frame(e);
    jstring js = toJString(e, utf8);
    jobject bmp = e->CallStaticObjectMethod(g_bridge, m_renderText, js, sizePx, weight, maxWidth);
    if (clearException(e, "renderText")) return {};
    return readBitmap(e, bmp);
}

PixelBuffer Jni::renderGlyph(const std::string& name, int sizePx) {
    JNIEnv* e = env();
    if (!e || !g_bridge) return {};
    LocalFrame frame(e);
    jstring js = toJString(e, name);
    jobject bmp = e->CallStaticObjectMethod(g_bridge, m_renderGlyph, js, sizePx);
    if (clearException(e, "renderGlyph")) return {};
    return readBitmap(e, bmp);
}

SystemStatus Jni::status() {
    SystemStatus st;
    JNIEnv* e = env();
    if (!e || !g_bridge) return st;
    LocalFrame frame(e);
    jstring js = static_cast<jstring>(e->CallStaticObjectMethod(g_bridge, m_status));
    if (clearException(e, "status") || !js) return st;
    std::vector<std::string> f = split(toStdString(e, js), kSep);
    if (f.size() >= 5) {
        st.battery = atoi(f[0].c_str());
        st.charging = f[1] == "1";
        st.wifi = f[2] == "1";
        st.time = f[3];
        st.date = f[4];
    }
    return st;
}

std::string Jni::getPref(const std::string& key, const std::string& def) {
    JNIEnv* e = env();
    if (!e || !g_bridge) return def;
    LocalFrame frame(e);
    jstring jk = toJString(e, key);
    jstring jd = toJString(e, def);
    jstring r = static_cast<jstring>(e->CallStaticObjectMethod(g_bridge, m_getPref, jk, jd));
    if (clearException(e, "getPref") || !r) return def;
    return toStdString(e, r);
}

void Jni::setPref(const std::string& key, const std::string& value) {
    JNIEnv* e = env();
    if (!e || !g_bridge) return;
    LocalFrame frame(e);
    jstring jk = toJString(e, key);
    jstring jv = toJString(e, value);
    e->CallStaticVoidMethod(g_bridge, m_setPref, jk, jv);
    clearException(e, "setPref");
}

void Jni::playSound(int id) {
    JNIEnv* e = env();
    if (!e || !g_bridge) return;
    e->CallStaticVoidMethod(g_bridge, m_playSound, id);
    clearException(e, "playSound");
}

bool Jni::savePng(const std::string& name, int width, int height, const uint8_t* rgba) {
    JNIEnv* e = env();
    if (!e || !g_bridge) return false;
    LocalFrame frame(e);
    jstring jn = toJString(e, name);
    const jsize bytes = static_cast<jsize>(width) * height * 4;
    jbyteArray arr = e->NewByteArray(bytes);
    e->SetByteArrayRegion(arr, 0, bytes, reinterpret_cast<const jbyte*>(rgba));
    jboolean ok = e->CallStaticBooleanMethod(g_bridge, m_savePng, jn, width, height, arr);
    if (clearException(e, "savePng")) return false;
    return ok == JNI_TRUE;
}

std::string Jni::pollDebugCommand() {
    JNIEnv* e = env();
    if (!e || !g_bridge) return {};
    LocalFrame frame(e);
    jstring js = static_cast<jstring>(e->CallStaticObjectMethod(g_bridge, m_pollDebug));
    if (clearException(e, "pollDebugCommand") || !js) return {};
    return toStdString(e, js);
}

}  // namespace pxr
