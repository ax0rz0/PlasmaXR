// User-adjustable shell settings, persisted through SharedPreferences (via the JNI bridge).
#pragma once

#include <cstdint>
#include <string>

namespace pxr {

class Settings {
public:
    void load();
    // Persists everything and bumps the revision. Call after changing a field.
    void commit();
    uint32_t revision() const { return revision_; }

    bool poke = true;         // direct fingertip touch on panels
    float smoothing = 1.0f;   // pointer smoothing multiplier (higher = steadier, laggier)
    bool handMesh = true;
    bool skeleton = false;
    bool sounds = true;
    int refreshRate = 0;      // requested display rate in Hz, 0 = leave to the system

private:
    uint32_t revision_ = 1;
};

}  // namespace pxr
