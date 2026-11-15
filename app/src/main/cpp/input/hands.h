// Hand tracking: per-frame joints, aim pose and pinch state for both hands (XR_EXT_hand_tracking
// + XR_FB_hand_tracking_aim), plus the runtime's skinned hand mesh (XR_FB_hand_tracking_mesh).
#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "xr/xr_context.h"

namespace pxr {

constexpr int kHandJointCount = XR_HAND_JOINT_COUNT_EXT;  // 26
enum HandSide { kLeft = 0, kRight = 1 };

struct HandState {
    bool active = false;          // the runtime is delivering valid joints this frame
    bool aimValid = false;        // aim/pinch came from the runtime
    bool aimComputed = false;     // aim/pinch were derived from joints (fallback)
    Pose joints[kHandJointCount];
    float radii[kHandJointCount] = {};
    bool jointValid[kHandJointCount] = {};
    Pose aim;                     // ray origin + orientation (-Z is the pointing direction)
    float pinch[4] = {};          // index, middle, ring, little (0..1)
    bool indexPinching = false;
    bool middlePinching = false;
    bool systemGesture = false;   // palm-up system gesture: must not be treated as UI input
    bool menuPressed = false;
    bool dominant = false;
    float activeSeconds = 0;      // how long tracking has been continuously active

    Vec3 indexTip() const { return joints[XR_HAND_JOINT_INDEX_TIP_EXT].p; }
    Vec3 thumbTip() const { return joints[XR_HAND_JOINT_THUMB_TIP_EXT].p; }
    Vec3 palm() const { return joints[XR_HAND_JOINT_PALM_EXT].p; }
    Vec3 wrist() const { return joints[XR_HAND_JOINT_WRIST_EXT].p; }
    Vec3 pinchPoint() const { return (indexTip() + thumbTip()) * 0.5f; }
};

struct HandMesh {
    bool valid = false;
    std::vector<Pose> invBind;            // per joint
    std::vector<Vec3> bindPos;
    std::vector<Vec3> bindNormal;
    std::vector<XrVector4sFB> blendIdx;
    std::vector<XrVector4f> blendW;
    std::vector<uint16_t> indices;
    std::vector<float> skinned;           // interleaved pos(3) + normal(3), app space
    bool skinnedValid = false;
};

class HandTracking {
public:
    bool init(XrContext& xr);
    void shutdown(XrContext& xr);

    // Locates both hands at `time` in the app space. `head` is only used by the aim fallback.
    void update(XrContext& xr, XrTime time, const Pose& head, float dt);

    const HandState& hand(int i) const { return hands_[i]; }
    const HandMesh& mesh(int i) const { return meshes_[i]; }
    bool available() const { return trackers_[0] != XR_NULL_HANDLE; }
    bool hasMesh() const { return meshes_[0].valid; }

    // Debug/test: replace live tracking with synthetic open hands (see the `sim` debug command).
    // Hands only appear once a pose has been set for them.
    void setSimulated(bool on);
    void setSimPose(int hand, const Pose& wristPose, float pinch);

private:
    bool loadMesh(XrContext& xr, int h);
    void skin(int h);
    void computeFallbackAim(HandState& hs, const Pose& head, int side);
    void applySimulation(int h);

    XrHandTrackerEXT trackers_[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    HandState hands_[2];
    HandMesh meshes_[2];
    bool loggedFailure_ = false;
    float diagClock_[2] = {0, 0};
    bool simulated_ = false;
    bool simActive_[2] = {false, false};
    Pose simPose_[2];
    float simPinch_[2] = {0, 0};
    std::vector<Pose> bindPoses_[2];
};

}  // namespace pxr
