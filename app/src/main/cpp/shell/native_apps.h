// Native Plasma XR apps: small apps drawn with the shell's own UI toolkit that live in windows.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "shell/window.h"

namespace pxr {

class HandTracking;
class PointerSystem;
class Settings;
struct ShellInfo;

// What a native app may touch. Pointers stay valid for the lifetime of the shell.
struct AppContext {
    Settings* settings = nullptr;
    const HandTracking* hands = nullptr;
    const PointerSystem* pointers = nullptr;
    const ShellInfo* info = nullptr;
    std::function<void(int)> setRefreshRate;  // Hz, 0 = leave to the system
    std::function<void()> recenter;
};

struct NativeAppDef {
    const char* id;
    const char* title;
    const char* glyph;
    unsigned color;  // 0xRRGGBB
};

const std::vector<NativeAppDef>& nativeApps();
std::unique_ptr<WindowContent> createNativeApp(const std::string& id, const AppContext& ctx);

}  // namespace pxr
