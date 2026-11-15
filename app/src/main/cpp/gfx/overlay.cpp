#include "gfx/overlay.h"

#include <cmath>
#include <vector>

#include "core/log.h"
#include "gfx/gl_util.h"

namespace pxr {

namespace {

const char* kSolidVs = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vN;
out vec3 vWorld;
out vec3 vLocal;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vN = mat3(uModel) * aNormal;
    vLocal = aPos;
    gl_Position = uViewProj * w;
}
)";

const char* kSolidFs = R"(#version 300 es
precision highp float;
in vec3 vN;
in vec3 vWorld;
in vec3 vLocal;
uniform vec3 uCam;
uniform vec4 uColor;
uniform float uRim;
uniform float uFade;
uniform float uLinear;
out vec4 oColor;
vec3 toLin(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
void main() {
    vec3 n = normalize(vN);
    vec3 v = normalize(uCam - vWorld);
    float fres = pow(1.0 - max(dot(n, v), 0.0), 3.5);
    float light = 0.70 + 0.30 * max(dot(n, normalize(vec3(0.35, 0.85, 0.4))), 0.0);
    vec3 col = uColor.rgb * light + vec3(0.45, 0.80, 1.0) * fres * uRim * 0.55;
    float a = clamp(uColor.a + fres * uRim * 0.28, 0.0, 1.0);
    if (uFade > 0.5) a *= smoothstep(0.0, 0.45, vLocal.z);
    if (uLinear > 0.5) col = toLin(col);
    oColor = vec4(col * a, a);
}
)";

const char* kDecalVs = R"(#version 300 es
layout(location = 0) in vec2 aPos;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec2 vUV;
void main() {
    vUV = aPos;
    gl_Position = uViewProj * uModel * vec4(aPos, 0.0, 1.0);
}
)";

const char* kDecalFs = R"(#version 300 es
precision highp float;
in vec2 vUV;
uniform vec4 uColor;
uniform float uRing;
uniform float uFillR;
uniform float uFill;
uniform float uLinear;
out vec4 oColor;
vec3 toLin(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
void main() {
    float r = length(vUV);
    float aa = max(fwidth(r) * 1.25, 0.01);
    float outer = 1.0 - smoothstep(1.0 - aa, 1.0, r);
    float inner = smoothstep(uRing - aa, uRing, r);
    float ring = outer * inner;
    float fill = (1.0 - smoothstep(uFillR - aa, uFillR, r)) * uFill;
    float a = clamp(max(ring, fill), 0.0, 1.0) * uColor.a;
    vec3 col = uLinear > 0.5 ? toLin(uColor.rgb) : uColor.rgb;
    oColor = vec4(col * a, a);
}
)";

}  // namespace

bool Overlay::init(bool linearOutput) {
    linear_ = linearOutput;
    solid_ = buildProgram(kSolidVs, kSolidFs, "overlay-solid");
    decal_ = buildProgram(kDecalVs, kDecalFs, "overlay-decal");
    if (!solid_ || !decal_) return false;
    sViewProj_ = glGetUniformLocation(solid_, "uViewProj");
    sModel_ = glGetUniformLocation(solid_, "uModel");
    sCam_ = glGetUniformLocation(solid_, "uCam");
    sColor_ = glGetUniformLocation(solid_, "uColor");
    sRim_ = glGetUniformLocation(solid_, "uRim");
    sFade_ = glGetUniformLocation(solid_, "uFade");
    sLinear_ = glGetUniformLocation(solid_, "uLinear");
    dViewProj_ = glGetUniformLocation(decal_, "uViewProj");
    dModel_ = glGetUniformLocation(decal_, "uModel");
    dColor_ = glGetUniformLocation(decal_, "uColor");
    dRing_ = glGetUniformLocation(decal_, "uRing");
    dFillR_ = glGetUniformLocation(decal_, "uFillR");
    dFill_ = glGetUniformLocation(decal_, "uFill");
    dLinear_ = glGetUniformLocation(decal_, "uLinear");
    buildSphere();
    buildCylinder();
    buildQuad();
    return true;
}

void Overlay::shutdown() {
    auto kill = [](Mesh& m) {
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.ibo) glDeleteBuffers(1, &m.ibo);
        if (m.vao) glDeleteVertexArrays(1, &m.vao);
        m = Mesh{};
    };
    kill(sphere_);
    kill(cylinder_);
    kill(quad_);
    for (auto& h : handGl_) {
        if (h.vbo) glDeleteBuffers(1, &h.vbo);
        if (h.ibo) glDeleteBuffers(1, &h.ibo);
        if (h.vao) glDeleteVertexArrays(1, &h.vao);
        h = HandGl{};
    }
    if (solid_) glDeleteProgram(solid_);
    if (decal_) glDeleteProgram(decal_);
    solid_ = decal_ = 0;
}

// ---------------------------------------------------------------------------------------------
// Primitive meshes: interleaved position(3) + normal(3), 16-bit indices
// ---------------------------------------------------------------------------------------------

namespace {

void uploadMesh(const std::vector<float>& v, const std::vector<uint16_t>& idx, GLuint* vao,
                GLuint* vbo, GLuint* ibo, GLsizei* count) {
    glGenVertexArrays(1, vao);
    glGenBuffers(1, vbo);
    glGenBuffers(1, ibo);
    glBindVertexArray(*vao);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint16_t), idx.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    *count = static_cast<GLsizei>(idx.size());
}

}  // namespace

void Overlay::buildSphere() {
    const int lat = 8, lon = 12;
    std::vector<float> v;
    std::vector<uint16_t> idx;
    for (int y = 0; y <= lat; ++y) {
        float t = static_cast<float>(y) / lat;
        float phi = t * kPi;
        for (int x = 0; x <= lon; ++x) {
            float theta = static_cast<float>(x) / lon * 2.0f * kPi;
            float nx = std::sin(phi) * std::cos(theta), ny = std::cos(phi),
                  nz = std::sin(phi) * std::sin(theta);
            v.insert(v.end(), {nx, ny, nz, nx, ny, nz});
        }
    }
    for (int y = 0; y < lat; ++y) {
        for (int x = 0; x < lon; ++x) {
            uint16_t a = static_cast<uint16_t>(y * (lon + 1) + x), b = a + 1,
                     c = static_cast<uint16_t>(a + lon + 1), d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    }
    uploadMesh(v, idx, &sphere_.vao, &sphere_.vbo, &sphere_.ibo, &sphere_.count);
}

// Unit-radius cylinder from z = 0 to z = 1.
void Overlay::buildCylinder() {
    const int seg = 12;
    std::vector<float> v;
    std::vector<uint16_t> idx;
    for (int i = 0; i <= seg; ++i) {
        float a = static_cast<float>(i) / seg * 2.0f * kPi;
        float x = std::cos(a), y = std::sin(a);
        v.insert(v.end(), {x, y, 0.0f, x, y, 0.0f});
        v.insert(v.end(), {x, y, 1.0f, x, y, 0.0f});
    }
    for (int i = 0; i < seg; ++i) {
        uint16_t a = static_cast<uint16_t>(i * 2), b = a + 1, c = a + 2, d = a + 3;
        idx.insert(idx.end(), {a, c, b, b, c, d});
    }
    uploadMesh(v, idx, &cylinder_.vao, &cylinder_.vbo, &cylinder_.ibo, &cylinder_.count);
}

void Overlay::buildQuad() {
    const float v[] = {-1, -1, 1, -1, 1, 1, -1, 1};
    const uint16_t idx[] = {0, 1, 2, 0, 2, 3};
    glGenVertexArrays(1, &quad_.vao);
    glGenBuffers(1, &quad_.vbo);
    glGenBuffers(1, &quad_.ibo);
    glBindVertexArray(quad_.vao);
    glBindBuffer(GL_ARRAY_BUFFER, quad_.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quad_.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    glBindVertexArray(0);
    quad_.count = 6;
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------

void Overlay::drawMesh(const Mesh& m, const Mat4& model, Color c, float rim, bool fade) {
    glUniformMatrix4fv(sModel_, 1, GL_FALSE, model.m);
    glUniform4f(sColor_, c.r, c.g, c.b, c.a);
    glUniform1f(sRim_, rim);
    glUniform1f(sFade_, fade ? 1.0f : 0.0f);
    glBindVertexArray(m.vao);
    glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_SHORT, nullptr);
}

void Overlay::drawCylinderBetween(Vec3 a, Vec3 b, float radius, Color c, float rim, bool fade) {
    Vec3 d = b - a;
    float len = d.length();
    if (len < 1e-5f) return;
    Quat q = Quat::lookRotation(-d / len);  // local +Z -> a..b
    drawMesh(cylinder_, Mat4::trs(a, q, {radius, radius, len}), c, rim, fade);
}

void Overlay::drawDecal(Vec3 pos, Vec3 normal, float radius, Color c, float ringInner,
                        float fillRadius, float fillAmount) {
    Vec3 up = std::fabs(normal.y) > 0.95f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
    Quat q = Quat::lookRotation(-normal, up);
    Mat4 model = Mat4::trs(pos + normal * 0.004f, q, {radius, radius, 1});
    glUniformMatrix4fv(dModel_, 1, GL_FALSE, model.m);
    glUniform4f(dColor_, c.r, c.g, c.b, c.a);
    glUniform1f(dRing_, ringInner);
    glUniform1f(dFillR_, fillRadius);
    glUniform1f(dFill_, fillAmount);
    glBindVertexArray(quad_.vao);
    glDrawElements(GL_TRIANGLES, quad_.count, GL_UNSIGNED_SHORT, nullptr);
}

void Overlay::drawHandMesh(int h, const HandTracking& hands, const OverlayStyle& style) {
    const HandMesh& m = hands.mesh(h);
    if (!m.valid || !m.skinnedValid) return;
    HandGl& g = handGl_[h];
    if (!g.vao) {
        glGenVertexArrays(1, &g.vao);
        glGenBuffers(1, &g.vbo);
        glGenBuffers(1, &g.ibo);
        glBindVertexArray(g.vao);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, m.skinned.size() * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.indices.size() * sizeof(uint16_t), m.indices.data(),
                     GL_STATIC_DRAW);
        g.indexCount = static_cast<GLsizei>(m.indices.size());
        g.vertexCount = m.bindPos.size();
    }
    glBindVertexArray(g.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, m.skinned.size() * sizeof(float), m.skinned.data());

    const Mat4 identity = Mat4::identity();
    glUniformMatrix4fv(sModel_, 1, GL_FALSE, identity.m);
    glUniform1f(sFade_, 0.0f);
    glUniform1f(sRim_, 1.0f);
    // Depth pre-pass so overlapping fingers do not blend twice.
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDrawElements(GL_TRIANGLES, g.indexCount, GL_UNSIGNED_SHORT, nullptr);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    Color c = style.handColor;
    glUniform4f(sColor_, c.r, c.g, c.b, c.a);
    glDrawElements(GL_TRIANGLES, g.indexCount, GL_UNSIGNED_SHORT, nullptr);
    glDepthMask(GL_TRUE);
}

void Overlay::drawSkeleton(const HandState& hs, const OverlayStyle& style) {
    static const int bones[][2] = {{1, 2},   {2, 3},   {3, 4},   {4, 5},   {1, 6},   {6, 7},
                                   {7, 8},   {8, 9},   {9, 10},  {1, 11},  {11, 12}, {12, 13},
                                   {13, 14}, {14, 15}, {1, 16},  {16, 17}, {17, 18}, {18, 19},
                                   {19, 20}, {1, 21},  {21, 22}, {22, 23}, {23, 24}, {24, 25},
                                   {6, 11},  {11, 16}, {16, 21}, {0, 1}};
    Color bone = style.handColor;
    bone.a = std::min(1.0f, bone.a + 0.25f);
    for (const auto& b : bones) {
        if (!hs.jointValid[b[0]] || !hs.jointValid[b[1]]) continue;
        float r = std::max(0.003f, (hs.radii[b[0]] + hs.radii[b[1]]) * 0.4f);
        drawCylinderBetween(hs.joints[b[0]].p, hs.joints[b[1]].p, r, bone, 0.8f, false);
    }
    for (int j = 0; j < kHandJointCount; ++j) {
        if (!hs.jointValid[j]) continue;
        float r = std::max(0.004f, hs.radii[j] * 0.9f);
        Color jc = (j == XR_HAND_JOINT_INDEX_TIP_EXT || j == XR_HAND_JOINT_THUMB_TIP_EXT)
                       ? Color(1, 1, 1, 0.95f)
                       : style.accent.withAlpha(0.9f);
        drawMesh(sphere_, Mat4::trs(hs.joints[j].p, Quat{}, {r, r, r}), jc, 0.6f, false);
    }
}

void Overlay::render(const Mat4& view, const Mat4& proj, const Vec3& eye, const HandTracking& hands,
                     const Controllers& controllers, const PointerSystem& pointers,
                     const OverlayStyle& style, float time) {
    viewProj_ = proj * view;
    eye_ = eye;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // ---- hands and rays (solid program) ---------------------------------------------------
    glUseProgram(solid_);
    glUniformMatrix4fv(sViewProj_, 1, GL_FALSE, viewProj_.m);
    glUniform3f(sCam_, eye.x, eye.y, eye.z);
    glUniform1f(sLinear_, linear_ ? 1.0f : 0.0f);

    // Rays are drawn first and without depth testing, so they stay visible through the
    // translucent hand rather than being cut by its depth.
    if (style.showRays) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        for (int i = 0; i < 2; ++i) {
            const PointerVisual& pv = pointers.visual(i);
            if (pv.mode != PointerVisual::kRay) continue;
            Color c = pv.down ? Color(1, 1, 1, 0.9f) : style.accent.withAlpha(0.55f + 0.3f * pv.pinch);
            drawCylinderBetween(pv.origin, pv.hit, pv.down ? 0.0022f : 0.0015f, c, 0.0f, true);
        }
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    }

    for (int h = 0; h < 2; ++h) {
        const HandState& hs = hands.hand(h);
        if (!hs.active) continue;
        // Fade out hands that are almost touching the eyes (e.g. while putting the headset on).
        const float fade = smoothstepf(0.12f, 0.28f, distance(eye, hs.palm()));
        if (fade < 0.02f) continue;
        OverlayStyle s = style;
        s.handColor = style.handColor.scaledAlpha(fade);
        if (style.showMesh && hands.mesh(h).valid) drawHandMesh(h, hands, s);
        if (style.showSkeleton || !hands.mesh(h).valid) drawSkeleton(hs, s);
    }

    // Controllers are drawn as a simple translucent handle with a tracking-ring ball.
    for (int side = 0; side < 2; ++side) {
        const ControllerState& cs = controllers.state(side);
        if (!cs.active) continue;
        const Pose g = cs.gripValid ? cs.grip : cs.aim;
        const Vec3 palm = g.apply({0, 0, 0.045f});
        const Vec3 ring = g.apply({0, 0, -0.09f});
        const Color body = style.accent.withAlpha(0.7f);
        drawCylinderBetween(palm, ring, 0.016f, body, 0.8f, false);
        drawMesh(sphere_, Mat4::trs(palm, Quat{}, {0.016f, 0.016f, 0.016f}), body, 0.8f, false);
        drawMesh(sphere_, Mat4::trs(ring, Quat{}, {0.026f, 0.026f, 0.026f}), body, 0.8f, false);
    }

    glDepthMask(GL_FALSE);

    // ---- cursors (decal program) ----------------------------------------------------------
    glUseProgram(decal_);
    glUniformMatrix4fv(dViewProj_, 1, GL_FALSE, viewProj_.m);
    glUniform1f(dLinear_, linear_ ? 1.0f : 0.0f);
    for (int i = 0; i < 2 && style.showCursors; ++i) {
        const PointerVisual& pv = pointers.visual(i);
        if (pv.mode == PointerVisual::kNone || !pv.hasTarget) continue;
        if (pv.mode == PointerVisual::kRay) {
            // Ring that tightens as the fingers close, with a solid dot once pressed.
            float r = lerpf(0.026f, 0.014f, saturate(pv.pinch));
            Color ring = pv.down ? Color(1, 1, 1, 1) : style.accent.withAlpha(0.95f);
            drawDecal(pv.hit, pv.normal, r, ring, 0.72f, 0.38f, pv.down ? 1.0f : saturate(pv.pinch) * 0.55f);
        } else {
            float depth = clampf(pv.pokeDepth, 0.0f, 0.06f);
            float r = 0.010f + depth * 0.45f;
            Color ring = pv.down ? Color(1, 1, 1, 1) : style.accent.withAlpha(0.95f);
            drawDecal(pv.hit, pv.normal, r, ring, 0.62f, 0.5f, pv.down ? 1.0f : 0.0f);
        }
    }
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    (void)time;
}

}  // namespace pxr
