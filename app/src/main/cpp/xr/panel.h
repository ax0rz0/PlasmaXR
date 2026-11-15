// A Panel is one UI surface in the world. It renders into its own OpenXR swapchain and is
// submitted as a compositor layer (cylinder section or flat quad), so text stays sharp instead of
// being resampled through the eye buffers. Also provides the ray / fingertip maths used for input.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/math.h"
#include "gfx/canvas.h"
#include "ui/ui.h"
#include "xr/xr_context.h"

namespace pxr {

// Where the shell sits in the world: the centre axis of the UI cylinder and its reference yaw.
struct Anchor {
    Vec3 pos;
    float yaw = 0;  // radians, rotation about +Y (positive = counter-clockwise from above)
};

struct PanelHit {
    bool hit = false;
    bool inBounds = false;
    float dist = 0;
    Vec2 px;
    Vec3 point;
    Vec3 normal;  // facing the viewer
};

class Panel {
public:
    // --- configuration ---------------------------------------------------------------------
    std::string name;
    int pxW = 0;
    int pxH = 0;
    float widthM = 1.0f;
    float heightM = 1.0f;
    float yaw = 0;       // radians offset from the anchor yaw (positive = to the left)
    float centerY = 0;   // metres relative to the anchor height
    float radius = 2.0f; // cylinder radius / flat panel distance
    bool curved = true;
    int order = 0;       // draw order among panels at equal distance (higher = in front)

    // Free-floating placement (windows in space): a flat quad at an arbitrary pose, facing +Z.
    // Overrides the cylinder placement above while `free` is set.
    bool free = false;
    Pose freePose;

    // Pixels around the edge that are drawn but ignored by pointing (soft shadows).
    float hitMargin = 0;
    // Swapchain size to allocate up front (0 = the panel size); lets a window grow without realloc.
    int reservePxW = 0;
    int reservePxH = 0;

    // --- runtime state ---------------------------------------------------------------------
    bool visible = true;
    bool interactive = true;
    float opacity = 1.0f;
    float offsetY = 0;      // animation offset in metres
    float offsetRadius = 0; // animation offset towards/away from the user
    bool dirty = true;
    PointerSample pointer[kMaxPointers];
    PointerSample prevPointer[kMaxPointers];
    Ui ui;
    std::function<void(Canvas&, Ui&)> draw;

    bool create(XrContext& xr);
    void destroy(XrContext& xr);
    // Changes the logical size. The swapchain is only reallocated when it has to grow.
    bool resize(XrContext& xr, int newPxW, int newPxH, float newWidthM, float newHeightM);

    // Re-renders the swapchain if dirty. Returns true if a frame was drawn.
    bool render(XrContext& xr, Canvas& canvas, float dt);
    // Debug: draws the panel into an offscreen RGBA8 buffer (premultiplied, top row first).
    bool snapshot(Canvas& canvas, std::vector<uint8_t>& rgba);

    // Layer for xrEndFrame, or nullptr if the panel should not be submitted.
    const XrCompositionLayerBaseHeader* buildLayer(const XrContext& xr, const Anchor& a);

    Pose axisPose(const Anchor& a) const;
    Vec3 center(const Anchor& a) const;
    PanelHit intersect(const Ray& ray, const Anchor& a, bool allowOutside = false) const;
    // Signed distance of a point in front of the surface (positive = on the viewer's side).
    float distanceInFront(Vec3 p, const Anchor& a, Vec2* px, Vec3* projected, bool* inBounds,
                          Vec3* normal = nullptr) const;
    // World position (and viewer-facing normal) of a pixel on the panel surface.
    void pxToWorld(Vec2 px, const Anchor& a, Vec3* point, Vec3* normal) const;
    Vec2 pxToMeters(Vec2 px) const { return {px.x / pxW * widthM, px.y / pxH * heightM}; }
    float ppm() const { return pxW / widthM; }

private:
    float effRadius() const { return free ? 0.0f : radius + offsetRadius; }
    bool cylinder() const { return curved && !free && cylinderSupported_; }

    bool insideMargin(Vec2 px) const {
        return px.x >= hitMargin && px.y >= hitMargin && px.x <= pxW - hitMargin &&
               px.y <= pxH - hitMargin;
    }

    Swapchain sc_;
    int allocW_ = 0;
    int allocH_ = 0;
    bool hasImage_ = false;
    bool cylinderSupported_ = false;
    XrCompositionLayerQuad quad_{};
    XrCompositionLayerCylinderKHR cyl_{};
    XrCompositionLayerColorScaleBiasKHR csb_{};
    XrCompositionLayerSettingsFB settings_{};
};

}  // namespace pxr
