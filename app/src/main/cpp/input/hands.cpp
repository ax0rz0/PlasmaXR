#include "input/hands.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/log.h"

namespace pxr {

namespace {

const XrSpaceLocationFlags kValidBits =
    XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;

}  // namespace

bool HandTracking::init(XrContext& xr) {
    if (!xr.features().handTracking || !xr.systemSupportsHands()) {
        LOGW("Hand tracking unavailable (ext=%d system=%d)", xr.features().handTracking ? 1 : 0,
             xr.systemSupportsHands() ? 1 : 0);
        return false;
    }
    for (int h = 0; h < 2; ++h) {
        XrHandTrackerCreateInfoEXT ci{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
        ci.hand = h == kLeft ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
        ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        if (!xr.check(xr.fn().createHandTracker(xr.session(), &ci, &trackers_[h]),
                      "xrCreateHandTrackerEXT")) {
            shutdown(xr);
            return false;
        }
    }
    LOGI("Hand trackers created (aim=%d mesh=%d capsules=%d)", xr.features().handAim ? 1 : 0,
         xr.features().handMesh ? 1 : 0, xr.features().handCapsules ? 1 : 0);
    if (xr.features().handMesh) {
        for (int h = 0; h < 2; ++h) loadMesh(xr, h);
    }
    return true;
}

void HandTracking::shutdown(XrContext& xr) {
    for (int h = 0; h < 2; ++h) {
        if (trackers_[h] != XR_NULL_HANDLE && xr.fn().destroyHandTracker) {
            xr.fn().destroyHandTracker(trackers_[h]);
        }
        trackers_[h] = XR_NULL_HANDLE;
        hands_[h] = HandState{};
        meshes_[h] = HandMesh{};
    }
}

bool HandTracking::loadMesh(XrContext& xr, int h) {
    XrHandTrackingMeshFB mesh{XR_TYPE_HAND_TRACKING_MESH_FB};
    if (!xr.check(xr.fn().getHandMesh(trackers_[h], &mesh), "xrGetHandMeshFB (sizes)")) {
        return false;
    }
    std::vector<XrPosef> bind(mesh.jointCountOutput);
    std::vector<float> radii(mesh.jointCountOutput);
    std::vector<XrHandJointEXT> parents(mesh.jointCountOutput);
    std::vector<XrVector3f> pos(mesh.vertexCountOutput);
    std::vector<XrVector3f> nrm(mesh.vertexCountOutput);
    std::vector<XrVector2f> uv(mesh.vertexCountOutput);
    std::vector<XrVector4sFB> bi(mesh.vertexCountOutput);
    std::vector<XrVector4f> bw(mesh.vertexCountOutput);
    std::vector<int16_t> idx(mesh.indexCountOutput);

    mesh.jointCapacityInput = mesh.jointCountOutput;
    mesh.jointBindPoses = bind.data();
    mesh.jointRadii = radii.data();
    mesh.jointParents = parents.data();
    mesh.vertexCapacityInput = mesh.vertexCountOutput;
    mesh.vertexPositions = pos.data();
    mesh.vertexNormals = nrm.data();
    mesh.vertexUVs = uv.data();
    mesh.vertexBlendIndices = bi.data();
    mesh.vertexBlendWeights = bw.data();
    mesh.indexCapacityInput = mesh.indexCountOutput;
    mesh.indices = idx.data();
    if (!xr.check(xr.fn().getHandMesh(trackers_[h], &mesh), "xrGetHandMeshFB (data)")) return false;

    HandMesh& m = meshes_[h];
    m = HandMesh{};
    m.invBind.resize(mesh.jointCountOutput);
    for (uint32_t j = 0; j < mesh.jointCountOutput; ++j) m.invBind[j] = toPose(bind[j]).inverse();
    m.bindPos.resize(mesh.vertexCountOutput);
    m.bindNormal.resize(mesh.vertexCountOutput);
    for (uint32_t v = 0; v < mesh.vertexCountOutput; ++v) {
        m.bindPos[v] = {pos[v].x, pos[v].y, pos[v].z};
        m.bindNormal[v] = {nrm[v].x, nrm[v].y, nrm[v].z};
    }
    m.blendIdx = bi;
    m.blendW = bw;
    m.indices.resize(mesh.indexCountOutput);
    for (uint32_t i = 0; i < mesh.indexCountOutput; ++i) m.indices[i] = static_cast<uint16_t>(idx[i]);
    m.skinned.assign(static_cast<size_t>(mesh.vertexCountOutput) * 6, 0.0f);
    m.valid = mesh.jointCountOutput == static_cast<uint32_t>(kHandJointCount) &&
              mesh.vertexCountOutput > 0 && mesh.indexCountOutput > 0;
    bindPoses_[h].resize(mesh.jointCountOutput);
    for (uint32_t j = 0; j < mesh.jointCountOutput; ++j) bindPoses_[h][j] = toPose(bind[j]);
    LOGI("Hand mesh %d: %u joints, %u verts, %u indices", h, mesh.jointCountOutput,
         mesh.vertexCountOutput, mesh.indexCountOutput);
    {
        std::string par;
        for (uint32_t j = 0; j < mesh.jointCountOutput; ++j) par += std::to_string(static_cast<int>(parents[j])) + " ";
        LOGI("Hand mesh %d joint parents: %s", h, par.c_str());
        const int probe[] = {XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_WRIST_EXT, XR_HAND_JOINT_THUMB_TIP_EXT,
                             XR_HAND_JOINT_INDEX_PROXIMAL_EXT, XR_HAND_JOINT_INDEX_TIP_EXT,
                             XR_HAND_JOINT_LITTLE_TIP_EXT};
        for (int j : probe) {
            const XrPosef& b = bind[j];
            LOGI("Hand mesh %d bind[%d]: p=(%.3f %.3f %.3f) q=(%.3f %.3f %.3f %.3f)", h, j,
                 b.position.x, b.position.y, b.position.z, b.orientation.x, b.orientation.y,
                 b.orientation.z, b.orientation.w);
        }
        Vec3 mn = m.bindPos[0], mx = m.bindPos[0];
        for (const Vec3& v : m.bindPos) {
            mn = {std::min(mn.x, v.x), std::min(mn.y, v.y), std::min(mn.z, v.z)};
            mx = {std::max(mx.x, v.x), std::max(mx.y, v.y), std::max(mx.z, v.z)};
        }
        LOGI("Hand mesh %d bind vertex bbox: (%.3f %.3f %.3f) - (%.3f %.3f %.3f)", h, mn.x, mn.y,
             mn.z, mx.x, mx.y, mx.z);
    }
    return m.valid;
}

void HandTracking::update(XrContext& xr, XrTime time, const Pose& head, float dt) {
    for (int h = 0; h < 2; ++h) {
        HandState& hs = hands_[h];
        const bool wasActive = hs.active;

        if (simulated_) {
            applySimulation(h);
        } else {
            hs.active = false;
            hs.aimValid = false;
            hs.aimComputed = false;
            hs.indexPinching = hs.middlePinching = hs.systemGesture = hs.menuPressed = false;
            if (trackers_[h] == XR_NULL_HANDLE) continue;

            XrHandJointLocationEXT locs[kHandJointCount];
            XrHandTrackingAimStateFB aim{XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
            XrHandJointLocationsEXT jl{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
            jl.jointCount = kHandJointCount;
            jl.jointLocations = locs;
            if (xr.features().handAim) jl.next = &aim;

            XrHandJointsLocateInfoEXT li{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
            li.baseSpace = xr.appSpace();
            li.time = time;
            XrResult r = xr.fn().locateHandJoints(trackers_[h], &li, &jl);
            if (XR_FAILED(r)) {
                if (!loggedFailure_) {
                    xr.check(r, "xrLocateHandJointsEXT");
                    loggedFailure_ = true;
                }
                continue;
            }
            diagClock_[h] += dt;
            const bool logNow = diagClock_[h] > 2.0f;
            if (logNow) diagClock_[h] = 0;
            int validJoints = 0;
            if (jl.isActive) {
                for (int j = 0; j < kHandJointCount; ++j) {
                    hs.jointValid[j] = (locs[j].locationFlags & kValidBits) == kValidBits;
                    hs.joints[j] = toPose(locs[j].pose);
                    hs.radii[j] = locs[j].radius;
                    if (hs.jointValid[j]) ++validJoints;
                }
                // The runtime can report a tracker as active while giving no usable joints; only
                // treat the hand as present when the wrist and most joints are really located.
                hs.active = hs.jointValid[XR_HAND_JOINT_WRIST_EXT] && validJoints >= 20;
            }
            if (logNow) {
                const XrHandJointLocationEXT& w = locs[XR_HAND_JOINT_WRIST_EXT];
                const XrHandJointLocationEXT& it = locs[XR_HAND_JOINT_INDEX_TIP_EXT];
                LOGI("hand[%d] runtime isActive=%d valid=%d/26 wristFlags=0x%x wrist=(%.3f %.3f %.3f) "
                     "indexTip=(%.3f %.3f %.3f) aimStatus=0x%x pinch=%.2f -> %s",
                     h, jl.isActive ? 1 : 0, validJoints, static_cast<unsigned>(w.locationFlags),
                     w.pose.position.x, w.pose.position.y, w.pose.position.z, it.pose.position.x,
                     it.pose.position.y, it.pose.position.z, static_cast<unsigned>(aim.status),
                     aim.pinchStrengthIndex, hs.active ? "ACTIVE" : "ignored");
            }
            if (hs.active) {
                if (xr.features().handAim) {
                    const XrHandTrackingAimFlagsFB st = aim.status;
                    hs.aimValid = (st & XR_HAND_TRACKING_AIM_VALID_BIT_FB) != 0 &&
                                  (st & XR_HAND_TRACKING_AIM_COMPUTED_BIT_FB) != 0;
                    if (hs.aimValid) {
                        hs.aim = toPose(aim.aimPose);
                        hs.pinch[0] = aim.pinchStrengthIndex;
                        hs.pinch[1] = aim.pinchStrengthMiddle;
                        hs.pinch[2] = aim.pinchStrengthRing;
                        hs.pinch[3] = aim.pinchStrengthLittle;
                        hs.indexPinching = (st & XR_HAND_TRACKING_AIM_INDEX_PINCHING_BIT_FB) != 0;
                        hs.middlePinching = (st & XR_HAND_TRACKING_AIM_MIDDLE_PINCHING_BIT_FB) != 0;
                        hs.systemGesture = (st & XR_HAND_TRACKING_AIM_SYSTEM_GESTURE_BIT_FB) != 0;
                        hs.menuPressed = (st & XR_HAND_TRACKING_AIM_MENU_PRESSED_BIT_FB) != 0;
                        hs.dominant = (st & XR_HAND_TRACKING_AIM_DOMINANT_HAND_BIT_FB) != 0;
                    }
                }
                if (!hs.aimValid) computeFallbackAim(hs, head, h);
            }
        }

        hs.activeSeconds = hs.active ? (wasActive ? hs.activeSeconds + dt : 0.0f) : 0.0f;
        if (hs.active != wasActive) {
            LOGI("%s hand %s (aim=%s)", h == kLeft ? "Left" : "Right",
                 hs.active ? "tracked" : "lost",
                 hs.aimValid ? "runtime" : (hs.aimComputed ? "computed" : "none"));
        }
        if (hs.active && meshes_[h].valid) skin(h);
        else meshes_[h].skinnedValid = false;
    }
}

// If the runtime gives no aim pose, aim from a virtual shoulder through the pinch point and
// derive pinch strength from the thumb/index tip distance.
void HandTracking::computeFallbackAim(HandState& hs, const Pose& head, int side) {
    const float dist = distance(hs.indexTip(), hs.thumbTip());
    const float strength = 1.0f - smoothstepf(0.015f, 0.065f, dist);
    const bool wasPinching = hs.indexPinching;
    hs.pinch[0] = strength;
    hs.indexPinching = strength > (wasPinching ? 0.55f : 0.8f);

    const Vec3 headRight = head.right();
    const Vec3 shoulder = head.p + headRight * (side == kRight ? 0.17f : -0.17f) + Vec3(0, -0.17f, 0) +
                          head.forward() * -0.04f;
    const Vec3 origin = hs.pinchPoint();
    const Vec3 dir = (origin - shoulder).normalized();
    hs.aim = {Quat::lookRotation(dir), origin};
    hs.aimComputed = true;
}

void HandTracking::skin(int h) {
    HandMesh& m = meshes_[h];
    const HandState& hs = hands_[h];
    Pose skinPose[kHandJointCount];
    for (int j = 0; j < kHandJointCount; ++j) skinPose[j] = hs.joints[j] * m.invBind[j];

    float* out = m.skinned.data();
    for (size_t v = 0; v < m.bindPos.size(); ++v) {
        const XrVector4sFB& bi = m.blendIdx[v];
        const XrVector4f& bw = m.blendW[v];
        const int16_t idx[4] = {bi.x, bi.y, bi.z, bi.w};
        const float w[4] = {bw.x, bw.y, bw.z, bw.w};
        Vec3 p(0, 0, 0), n(0, 0, 0);
        float total = 0;
        for (int k = 0; k < 4; ++k) {
            if (w[k] <= 0.0f || idx[k] < 0 || idx[k] >= kHandJointCount) continue;
            p += skinPose[idx[k]].apply(m.bindPos[v]) * w[k];
            n += skinPose[idx[k]].rotate(m.bindNormal[v]) * w[k];
            total += w[k];
        }
        if (total > 1e-5f) {
            p = p / total;
            n = n.normalized();
        }
        out[v * 6 + 0] = p.x;
        out[v * 6 + 1] = p.y;
        out[v * 6 + 2] = p.z;
        out[v * 6 + 3] = n.x;
        out[v * 6 + 4] = n.y;
        out[v * 6 + 5] = n.z;
    }
    m.skinnedValid = true;
}

// --- simulation (debug) --------------------------------------------------------------------

void HandTracking::setSimulated(bool on) {
    simulated_ = on;
    if (!on) simActive_[0] = simActive_[1] = false;
}

void HandTracking::setSimPose(int hand, const Pose& wristPose, float pinch) {
    simPose_[hand] = wristPose;
    simPinch_[hand] = pinch;
    simActive_[hand] = true;
}

// Poses the mesh's bind skeleton at simPose_ so rendering and input can be exercised without
// live tracking. The simulated hand points along its local -Z.
void HandTracking::applySimulation(int h) {
    HandState& hs = hands_[h];
    const auto& bind = bindPoses_[h];
    hs.active = simActive_[h] && !bind.empty();
    hs.aimValid = false;
    hs.aimComputed = hs.active;
    if (!hs.active) return;
    // Bind poses are modelled for an open hand lying in the mesh's local space; re-seat it so the
    // wrist is at simPose_ and fingers extend along its -Z.
    const Pose wristBind = bind[XR_HAND_JOINT_WRIST_EXT];
    const Pose rel = simPose_[h] * wristBind.inverse();
    for (int j = 0; j < kHandJointCount; ++j) {
        hs.joints[j] = rel * bind[j];
        hs.radii[j] = 0.008f;
        hs.jointValid[j] = true;
    }
    hs.pinch[0] = simPinch_[h];
    hs.indexPinching = simPinch_[h] > 0.8f;
    hs.systemGesture = false;
    const Vec3 origin = hs.indexTip();
    hs.aim = {simPose_[h].q, origin};
}

}  // namespace pxr
