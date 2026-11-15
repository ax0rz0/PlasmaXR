#include "input/controllers.h"

#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"

namespace pxr {

namespace {
const XrPosef kIdentity = {{0, 0, 0, 1}, {0, 0, 0}};
const XrSpaceLocationFlags kValid =
    XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
}  // namespace

bool Controllers::init(XrContext& xr) {
    const XrInstance inst = xr.instance();
    xrStringToPath(inst, "/user/hand/left", &hand_[0]);
    xrStringToPath(inst, "/user/hand/right", &hand_[1]);

    XrActionSetCreateInfo asi{XR_TYPE_ACTION_SET_CREATE_INFO};
    strncpy(asi.actionSetName, "plasma_shell", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    strncpy(asi.localizedActionSetName, "Plasma XR Shell", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    if (!xr.check(xrCreateActionSet(inst, &asi, &set_), "xrCreateActionSet")) return false;

    bool ok = true;
    auto make = [&](XrActionType type, const char* name, const char* localized, XrAction* out) {
        XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};
        ai.actionType = type;
        strncpy(ai.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
        strncpy(ai.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        ai.countSubactionPaths = 2;
        ai.subactionPaths = hand_;
        ok &= xr.check(xrCreateAction(set_, &ai, out), name);
    };
    make(XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim", &aim_);
    make(XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip", &grip_);
    make(XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", &trigger_);
    make(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Grip squeeze", &squeeze_);
    make(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", &stick_);
    make(XR_ACTION_TYPE_BOOLEAN_INPUT, "primary", "A / X", &primary_);
    make(XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary", "B / Y", &secondary_);
    make(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu", &menu_);
    make(XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Haptic", &haptic_);
    if (!ok) return false;

    auto path = [&](const std::string& s) {
        XrPath p = XR_NULL_PATH;
        xrStringToPath(inst, s.c_str(), &p);
        return p;
    };

    // Quest 2 / Touch controllers.
    {
        std::vector<XrActionSuggestedBinding> b;
        for (const char* side : {"left", "right"}) {
            const std::string base = std::string("/user/hand/") + side;
            b.push_back({aim_, path(base + "/input/aim/pose")});
            b.push_back({grip_, path(base + "/input/grip/pose")});
            b.push_back({trigger_, path(base + "/input/trigger/value")});
            b.push_back({squeeze_, path(base + "/input/squeeze/value")});
            b.push_back({stick_, path(base + "/input/thumbstick")});
            b.push_back({haptic_, path(base + "/output/haptic")});
        }
        b.push_back({primary_, path("/user/hand/left/input/x/click")});
        b.push_back({primary_, path("/user/hand/right/input/a/click")});
        b.push_back({secondary_, path("/user/hand/left/input/y/click")});
        b.push_back({secondary_, path("/user/hand/right/input/b/click")});
        b.push_back({menu_, path("/user/hand/left/input/menu/click")});
        XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        sb.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
        sb.suggestedBindings = b.data();
        sb.countSuggestedBindings = static_cast<uint32_t>(b.size());
        xr.check(xrSuggestInteractionProfileBindings(inst, &sb), "suggest touch bindings");
    }
    // Generic fallback for any other controller.
    {
        std::vector<XrActionSuggestedBinding> b;
        for (const char* side : {"left", "right"}) {
            const std::string base = std::string("/user/hand/") + side;
            b.push_back({aim_, path(base + "/input/aim/pose")});
            b.push_back({grip_, path(base + "/input/grip/pose")});
            b.push_back({primary_, path(base + "/input/select/click")});
            b.push_back({haptic_, path(base + "/output/haptic")});
        }
        b.push_back({menu_, path("/user/hand/left/input/menu/click")});
        XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        sb.interactionProfile = path("/interaction_profiles/khr/simple_controller");
        sb.suggestedBindings = b.data();
        sb.countSuggestedBindings = static_cast<uint32_t>(b.size());
        xr.check(xrSuggestInteractionProfileBindings(inst, &sb), "suggest simple bindings");
    }

    XrSessionActionSetsAttachInfo att{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    att.countActionSets = 1;
    att.actionSets = &set_;
    if (!xr.check(xrAttachSessionActionSets(xr.session(), &att), "xrAttachSessionActionSets")) {
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        XrActionSpaceCreateInfo si{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        si.subactionPath = hand_[i];
        si.poseInActionSpace = kIdentity;
        si.action = aim_;
        ok &= xr.check(xrCreateActionSpace(xr.session(), &si, &aimSpace_[i]), "aim space");
        si.action = grip_;
        ok &= xr.check(xrCreateActionSpace(xr.session(), &si, &gripSpace_[i]), "grip space");
    }
    LOGI("Controllers: action set ready (Touch + simple profiles)");
    return ok;
}

void Controllers::shutdown(XrContext&) {
    for (int i = 0; i < 2; ++i) {
        if (aimSpace_[i] != XR_NULL_HANDLE) xrDestroySpace(aimSpace_[i]);
        if (gripSpace_[i] != XR_NULL_HANDLE) xrDestroySpace(gripSpace_[i]);
        aimSpace_[i] = gripSpace_[i] = XR_NULL_HANDLE;
    }
    if (set_ != XR_NULL_HANDLE) xrDestroyActionSet(set_);
    set_ = XR_NULL_HANDLE;
}

void Controllers::sync(XrContext& xr, XrTime time) {
    for (int i = 0; i < 2; ++i) {
        st_[i].primaryPressed = st_[i].secondaryPressed = st_[i].menuPressed = false;
    }
    if (set_ == XR_NULL_HANDLE) return;

    XrActiveActionSet active{set_, XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;
    const XrResult sr = xrSyncActions(xr.session(), &si);
    const bool focused = sr == XR_SUCCESS;  // XR_SESSION_NOT_FOCUSED is a success code with no data

    for (int i = 0; i < 2; ++i) {
        ControllerState& s = st_[i];
        const bool prevPrimary = s.primary, prevSecondary = s.secondary, prevMenu = s.menu;
        bool active_ = false;
        s.gripValid = false;
        s.trigger = s.squeeze = 0;
        s.stick = {0, 0};
        s.primary = s.secondary = s.menu = false;

        if (focused) {
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.subactionPath = hand_[i];

            gi.action = aim_;
            XrActionStatePose ps{XR_TYPE_ACTION_STATE_POSE};
            if (XR_SUCCEEDED(xrGetActionStatePose(xr.session(), &gi, &ps)) && ps.isActive) {
                XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
                if (XR_SUCCEEDED(xrLocateSpace(aimSpace_[i], xr.appSpace(), time, &loc)) &&
                    (loc.locationFlags & kValid) == kValid) {
                    s.aim = toPose(loc.pose);
                    active_ = true;
                }
            }
            if (active_) {
                gi.action = grip_;
                XrSpaceLocation gl{XR_TYPE_SPACE_LOCATION};
                if (XR_SUCCEEDED(xrLocateSpace(gripSpace_[i], xr.appSpace(), time, &gl)) &&
                    (gl.locationFlags & kValid) == kValid) {
                    s.grip = toPose(gl.pose);
                    s.gripValid = true;
                }
            }

            XrActionStateFloat fs{XR_TYPE_ACTION_STATE_FLOAT};
            gi.action = trigger_;
            if (XR_SUCCEEDED(xrGetActionStateFloat(xr.session(), &gi, &fs)) && fs.isActive) s.trigger = fs.currentState;
            gi.action = squeeze_;
            if (XR_SUCCEEDED(xrGetActionStateFloat(xr.session(), &gi, &fs)) && fs.isActive) s.squeeze = fs.currentState;

            XrActionStateVector2f vs{XR_TYPE_ACTION_STATE_VECTOR2F};
            gi.action = stick_;
            if (XR_SUCCEEDED(xrGetActionStateVector2f(xr.session(), &gi, &vs)) && vs.isActive) {
                s.stick = {vs.currentState.x, vs.currentState.y};
            }

            XrActionStateBoolean bs{XR_TYPE_ACTION_STATE_BOOLEAN};
            gi.action = primary_;
            if (XR_SUCCEEDED(xrGetActionStateBoolean(xr.session(), &gi, &bs)) && bs.isActive) s.primary = bs.currentState;
            gi.action = secondary_;
            if (XR_SUCCEEDED(xrGetActionStateBoolean(xr.session(), &gi, &bs)) && bs.isActive) s.secondary = bs.currentState;
            gi.action = menu_;
            if (XR_SUCCEEDED(xrGetActionStateBoolean(xr.session(), &gi, &bs)) && bs.isActive) s.menu = bs.currentState;
        }
        s.active = active_;
        s.primaryPressed = s.primary && !prevPrimary;
        s.secondaryPressed = s.secondary && !prevSecondary;
        s.menuPressed = s.menu && !prevMenu;
        if (s.active != wasActive_[i]) {
            LOGI("Controller %s %s", i == 0 ? "left" : "right", s.active ? "active" : "inactive");
            wasActive_[i] = s.active;
        }
    }
}

void Controllers::haptic(XrContext& xr, int side, float amplitude, float seconds) {
    if (set_ == XR_NULL_HANDLE || !st_[side].active) return;
    XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};
    v.amplitude = amplitude;
    v.duration = static_cast<XrDuration>(seconds * 1e9);
    v.frequency = XR_FREQUENCY_UNSPECIFIED;
    XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
    hi.action = haptic_;
    hi.subactionPath = hand_[side];
    xrApplyHapticFeedback(xr.session(), &hi, reinterpret_cast<const XrHapticBaseHeader*>(&v));
}

}  // namespace pxr
