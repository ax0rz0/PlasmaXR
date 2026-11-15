// Touch controller input through the OpenXR action system: aim/grip poses, trigger, grip,
// thumbstick, face buttons and haptics. Each side feeds the pointer system when its controller is
// active; otherwise that side falls back to hand tracking.
#pragma once

#include "core/math.h"
#include "xr/xr_context.h"

namespace pxr {

struct ControllerState {
    bool active = false;       // aim pose is tracked this frame (and the session has input focus)
    bool gripValid = false;
    Pose aim;                  // -Z points along the pointing ray
    Pose grip;
    float trigger = 0;
    float squeeze = 0;
    Vec2 stick;
    bool primary = false;      // A / X
    bool secondary = false;    // B / Y
    bool menu = false;
    bool primaryPressed = false;    // went down this frame
    bool secondaryPressed = false;
    bool menuPressed = false;
};

class Controllers {
public:
    // Needs a session. Call once before the first frame.
    bool init(XrContext& xr);
    void shutdown(XrContext& xr);
    // xrSyncActions plus reading every action. Call once per frame before using state().
    void sync(XrContext& xr, XrTime time);

    const ControllerState& state(int side) const { return st_[side]; }
    bool available() const { return set_ != XR_NULL_HANDLE; }
    // Short vibration; amplitude 0..1.
    void haptic(XrContext& xr, int side, float amplitude, float seconds);

private:
    XrActionSet set_ = XR_NULL_HANDLE;
    XrAction aim_ = XR_NULL_HANDLE, grip_ = XR_NULL_HANDLE, trigger_ = XR_NULL_HANDLE,
             squeeze_ = XR_NULL_HANDLE, stick_ = XR_NULL_HANDLE, primary_ = XR_NULL_HANDLE,
             secondary_ = XR_NULL_HANDLE, menu_ = XR_NULL_HANDLE, haptic_ = XR_NULL_HANDLE;
    XrSpace aimSpace_[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    XrSpace gripSpace_[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    XrPath hand_[2] = {XR_NULL_PATH, XR_NULL_PATH};
    ControllerState st_[2];
    bool wasActive_[2] = {false, false};
};

}  // namespace pxr
