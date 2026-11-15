#include "shell/settings.h"

#include <cstdlib>

#include "core/math.h"
#include "platform/jni_bridge.h"

namespace pxr {

namespace {

bool getBool(const char* key, bool def) {
    std::string v = Jni::getPref(key, def ? "1" : "0");
    return v == "1";
}

float getFloat(const char* key, float def) {
    std::string v = Jni::getPref(key, "");
    return v.empty() ? def : static_cast<float>(atof(v.c_str()));
}

int getInt(const char* key, int def) {
    std::string v = Jni::getPref(key, "");
    return v.empty() ? def : atoi(v.c_str());
}

}  // namespace

void Settings::load() {
    poke = getBool("set.poke", true);
    smoothing = clampf(getFloat("set.smoothing", 1.0f), 0.5f, 2.0f);
    handMesh = getBool("set.handMesh", true);
    skeleton = getBool("set.skeleton", false);
    sounds = getBool("set.sounds", true);
    refreshRate = getInt("set.refreshRate", 0);
}

void Settings::commit() {
    Jni::setPref("set.poke", poke ? "1" : "0");
    Jni::setPref("set.smoothing", std::to_string(smoothing));
    Jni::setPref("set.handMesh", handMesh ? "1" : "0");
    Jni::setPref("set.skeleton", skeleton ? "1" : "0");
    Jni::setPref("set.sounds", sounds ? "1" : "0");
    Jni::setPref("set.refreshRate", std::to_string(refreshRate));
    ++revision_;
}

}  // namespace pxr
