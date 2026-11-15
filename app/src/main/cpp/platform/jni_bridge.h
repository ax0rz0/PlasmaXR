// Native side of org.plasmaxr.launcher.Bridge: installed apps, icons, text rasterisation,
// launching, status and preferences. Thread-safe as long as init() ran first.
#pragma once

#include <android_native_app_glue.h>
#include <jni.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pxr {

struct AppInfo {
    std::string pkg;
    std::string activity;
    std::string label;
    bool vr = false;      // immersive (OpenXR) app
    bool system = false;  // part of the OS image
    bool native = false;  // a Plasma XR app that opens in a shell window (pkg is its id)
    bool linuxApp = false;  // a Linux program in the rootfs (activity is its shell command)
    std::string glyph;    // native apps: icon glyph
    unsigned color = 0;   // native apps: tile colour as 0xRRGGBB
};

// CPU pixels copied out of an android.graphics.Bitmap.
struct PixelBuffer {
    int width = 0;
    int height = 0;
    int stride = 0;       // bytes per row
    bool alpha8 = false;  // true: 1 byte/pixel coverage; false: RGBA8888 premultiplied
    std::vector<uint8_t> data;
    bool valid() const { return width > 0 && height > 0 && !data.empty(); }
};

struct SystemStatus {
    int battery = -1;
    bool charging = false;
    bool wifi = false;
    std::string time;
    std::string date;
};

class Jni {
public:
    static void init(android_app* app);
    // Worker threads that touched JNI must call this before they exit.
    static void detachThread();

    static std::vector<AppInfo> listApps();
    static PixelBuffer loadAppIcon(const std::string& pkg, const std::string& activity, int sizePx);
    static bool launchApp(const std::string& pkg, const std::string& activity);

    // Linux apps: a Debian rootfs run under proot by the Java side.
    static std::string linuxRoot();
    static std::string linuxSocketDir();
    static bool linuxInstalled();
    static bool launchLinux(const std::string& shellCommand);

    static PixelBuffer renderText(const std::string& utf8, float sizePx, int weight, int maxWidth);
    static PixelBuffer renderGlyph(const std::string& name, int sizePx);

    // Debug: writes RGBA8 (premultiplied) pixels to <externalFilesDir>/snap/<name>.png.
    static bool savePng(const std::string& name, int width, int height, const uint8_t* rgba);

    static SystemStatus status();
    static std::string getPref(const std::string& key, const std::string& def);
    static void setPref(const std::string& key, const std::string& value);
    static void playSound(int id);
    static std::string pollDebugCommand();
};

enum Sound { kSoundHover = 0, kSoundPress = 1, kSoundRelease = 2, kSoundOpen = 3, kSoundClose = 4 };

}  // namespace pxr
