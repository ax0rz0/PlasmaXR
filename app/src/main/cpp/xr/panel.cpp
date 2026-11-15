#include "xr/panel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/log.h"

namespace pxr {

bool Panel::create(XrContext& xr) {
    cylinderSupported_ = xr.features().cylinderLayer;
    allocW_ = std::max(pxW, reservePxW);
    allocH_ = std::max(pxH, reservePxH);
    if (!xr.createSwapchain(allocW_, allocH_, sc_)) {
        LOGE("Panel '%s': swapchain creation failed", name.c_str());
        return false;
    }
    LOGI("Panel '%s': %dx%d px (swapchain %dx%d), %.2fx%.2f m, %s", name.c_str(), pxW, pxH, allocW_,
         allocH_, widthM, heightM, cylinder() ? "cylinder" : "quad");
    dirty = true;
    return true;
}

void Panel::destroy(XrContext& xr) {
    xr.destroySwapchain(sc_);
    hasImage_ = false;
    allocW_ = allocH_ = 0;
}

bool Panel::resize(XrContext& xr, int newPxW, int newPxH, float newWidthM, float newHeightM) {
    if (newPxW > allocW_ || newPxH > allocH_) {
        // Grow with headroom so dragging a window larger does not reallocate on every frame.
        const int aw = std::max(newPxW, std::max(allocW_, std::min(2304, newPxW + newPxW / 4)));
        const int ah = std::max(newPxH, std::max(allocH_, std::min(1400, newPxH + newPxH / 4)));
        xr.destroySwapchain(sc_);
        if (!xr.createSwapchain(aw, ah, sc_)) {
            LOGE("Panel '%s': swapchain regrow to %dx%d failed", name.c_str(), aw, ah);
            allocW_ = allocH_ = 0;
            hasImage_ = false;
            return false;
        }
        allocW_ = aw;
        allocH_ = ah;
        hasImage_ = false;  // not submitted again until redrawn into the new swapchain
    }
    pxW = newPxW;
    pxH = newPxH;
    widthM = newWidthM;
    heightM = newHeightM;
    dirty = true;
    return true;
}

bool Panel::render(XrContext& xr, Canvas& canvas, float dt) {
    if (!dirty || !visible || !sc_.valid() || !draw) return false;
    dirty = false;
    if (!xr.acquireImage(sc_)) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, sc_.fbos[sc_.acquired]);
    glViewport(0, 0, pxW, pxH);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    canvas.begin(pxW, pxH);
    ui.beginFrame(&canvas, dt, pointer);
    draw(canvas, ui);
    bool again = ui.endFrame();
    canvas.end();
    glFlush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    xr.releaseImage(sc_);
    hasImage_ = true;
    if (again) dirty = true;
    return true;
}

bool Panel::snapshot(Canvas& canvas, std::vector<uint8_t>& rgba) {
    if (!draw) return false;
    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pxW, pxH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glViewport(0, 0, pxW, pxH);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        canvas.setLinearOutput(false);  // plain RGBA8 target: write display-referred values
        canvas.begin(pxW, pxH);
        ui.beginFrame(&canvas, 0.016f, pointer);
        draw(canvas, ui);
        ui.endFrame();
        canvas.end();
        canvas.setLinearOutput(true);
        ui.sounds().clear();
        std::vector<uint8_t> bottomUp(static_cast<size_t>(pxW) * pxH * 4);
        glReadPixels(0, 0, pxW, pxH, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());
        rgba.resize(bottomUp.size());
        const size_t row = static_cast<size_t>(pxW) * 4;
        for (int y = 0; y < pxH; ++y) {
            memcpy(&rgba[static_cast<size_t>(y) * row], &bottomUp[static_cast<size_t>(pxH - 1 - y) * row], row);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    dirty = true;
    return ok;
}

Pose Panel::axisPose(const Anchor& a) const {
    if (free) return freePose;
    Pose p;
    p.q = Quat::fromAxisAngle({0, 1, 0}, a.yaw + yaw);
    p.p = a.pos + Vec3(0, centerY + offsetY, 0);
    return p;
}

Vec3 Panel::center(const Anchor& a) const {
    Pose f = axisPose(a);
    return f.p + f.forward() * effRadius();
}

PanelHit Panel::intersect(const Ray& ray, const Anchor& a, bool allowOutside) const {
    PanelHit h;
    Pose f = axisPose(a);
    Pose inv = f.inverse();
    Vec3 o = inv.apply(ray.o);
    Vec3 d = inv.rotate(ray.d);
    const float R = effRadius();

    if (cylinder()) {
        float A = d.x * d.x + d.z * d.z;
        if (A < 1e-8f) return h;
        float B = 2.0f * (o.x * d.x + o.z * d.z);
        float C = o.x * o.x + o.z * o.z - R * R;
        float disc = B * B - 4.0f * A * C;
        if (disc < 0) return h;
        float sq = std::sqrt(disc);
        float t = C < 0 ? (-B + sq) / (2.0f * A) : (-B - sq) / (2.0f * A);
        if (t < 0) return h;
        Vec3 p = o + d * t;
        float phi = std::atan2(p.x, -p.z);
        if (std::cos(phi) <= 0.05f) return h;
        float arc = phi * R;
        const Vec2 pxv((0.5f + arc / widthM) * pxW, (0.5f - p.y / heightM) * pxH);
        bool inb = std::fabs(arc) <= widthM * 0.5f && std::fabs(p.y) <= heightM * 0.5f &&
                   insideMargin(pxv);
        if (!inb && !allowOutside) return h;
        h.hit = true;
        h.inBounds = inb;
        h.dist = t;
        h.px = pxv;
        h.point = f.apply(p);
        h.normal = f.rotate(Vec3(-p.x, 0, -p.z).normalized());
    } else {
        if (d.z > -1e-6f) return h;
        float t = (-R - o.z) / d.z;
        if (t < 0) return h;
        Vec3 p = o + d * t;
        const Vec2 pxv((0.5f + p.x / widthM) * pxW, (0.5f - p.y / heightM) * pxH);
        bool inb = std::fabs(p.x) <= widthM * 0.5f && std::fabs(p.y) <= heightM * 0.5f &&
                   insideMargin(pxv);
        if (!inb && !allowOutside) return h;
        h.hit = true;
        h.inBounds = inb;
        h.dist = t;
        h.px = pxv;
        h.point = f.apply(p);
        h.normal = f.rotate({0, 0, 1});
    }
    return h;
}

float Panel::distanceInFront(Vec3 p, const Anchor& a, Vec2* px, Vec3* projected, bool* inBounds,
                             Vec3* normal) const {
    Pose f = axisPose(a);
    Vec3 l = f.inverse().apply(p);
    const float R = effRadius();
    float dist;
    Vec2 uv;
    Vec3 proj;
    Vec3 n;
    bool inb;
    if (cylinder()) {
        float r = std::sqrt(l.x * l.x + l.z * l.z);
        float phi = std::atan2(l.x, -l.z);
        float arc = phi * R;
        dist = R - r;
        uv = {0.5f + arc / widthM, 0.5f - l.y / heightM};
        proj = {R * std::sin(phi), l.y, -R * std::cos(phi)};
        n = {-std::sin(phi), 0, std::cos(phi)};
        inb = std::cos(phi) > 0.05f && std::fabs(arc) <= widthM * 0.5f &&
              std::fabs(l.y) <= heightM * 0.5f;
    } else {
        dist = l.z + R;
        uv = {0.5f + l.x / widthM, 0.5f - l.y / heightM};
        proj = {l.x, l.y, -R};
        n = {0, 0, 1};
        inb = std::fabs(l.x) <= widthM * 0.5f && std::fabs(l.y) <= heightM * 0.5f;
    }
    const Vec2 pxv(uv.x * pxW, uv.y * pxH);
    inb = inb && insideMargin(pxv);
    if (px) *px = pxv;
    if (projected) *projected = f.apply(proj);
    if (normal) *normal = f.rotate(n);
    if (inBounds) *inBounds = inb;
    return dist;
}

void Panel::pxToWorld(Vec2 px, const Anchor& a, Vec3* point, Vec3* normal) const {
    Pose f = axisPose(a);
    const float R = effRadius();
    const float x = (px.x / pxW - 0.5f) * widthM;
    const float y = (0.5f - px.y / pxH) * heightM;
    Vec3 local, n;
    if (cylinder()) {
        float phi = x / R;
        local = {R * std::sin(phi), y, -R * std::cos(phi)};
        n = {-std::sin(phi), 0, std::cos(phi)};
    } else {
        local = {x, y, -R};
        n = {0, 0, 1};
    }
    if (point) *point = f.apply(local);
    if (normal) *normal = f.rotate(n);
}

const XrCompositionLayerBaseHeader* Panel::buildLayer(const XrContext& xr, const Anchor& a) {
    if (!visible || !hasImage_ || !sc_.valid() || opacity <= 0.003f) return nullptr;

    void* chain = nullptr;
    if (xr.features().colorScaleBias && opacity < 0.999f) {
        csb_ = {XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};
        csb_.colorScale = {opacity, opacity, opacity, opacity};
        csb_.colorBias = {0, 0, 0, 0};
        csb_.next = chain;
        chain = &csb_;
    }
    if (xr.features().layerSettings) {
        settings_ = {XR_TYPE_COMPOSITION_LAYER_SETTINGS_FB};
        settings_.layerFlags = XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SUPER_SAMPLING_BIT_FB |
                               XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SHARPENING_BIT_FB;
        settings_.next = chain;
        chain = &settings_;
    }

    XrSwapchainSubImage sub{};
    sub.swapchain = sc_.handle;
    sub.imageRect = {{0, 0}, {pxW, pxH}};
    sub.imageArrayIndex = 0;

    Pose f = axisPose(a);
    const XrCompositionLayerFlags flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    if (cylinder()) {
        cyl_ = {XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
        cyl_.next = chain;
        cyl_.layerFlags = flags;
        cyl_.space = xr.appSpace();
        cyl_.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        cyl_.subImage = sub;
        cyl_.pose = toXr(f);
        cyl_.radius = effRadius();
        cyl_.centralAngle = widthM / effRadius();
        cyl_.aspectRatio = widthM / heightM;
        return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cyl_);
    }
    quad_ = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad_.next = chain;
    quad_.layerFlags = flags;
    quad_.space = xr.appSpace();
    quad_.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad_.subImage = sub;
    Pose qp = f;
    qp.p = f.p + f.forward() * effRadius();
    quad_.pose = toXr(qp);
    quad_.size = {widthM, heightM};
    return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad_);
}

}  // namespace pxr
