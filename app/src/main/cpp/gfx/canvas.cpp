#include "gfx/canvas.h"

#include <cmath>
#include <cstddef>

#include "core/log.h"

namespace pxr {

namespace {

constexpr float kModeFill = 0, kModeMask = 1, kModeImage = 2, kModeStroke = 3, kModeShadow = 4;
constexpr int kTextPad = 2;  // padding baked into rasterised text bitmaps (see Bridge.renderText)
constexpr size_t kMaxQuads = 8000;

const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aRect;
layout(location = 4) in vec3 aMisc;
uniform vec2 uView;
out vec2 vPos;
out vec2 vUV;
out vec4 vColor;
flat out vec4 vRect;
flat out vec3 vMisc;
void main() {
    vPos = aPos;
    vUV = aUV;
    vColor = aColor;
    vRect = aRect;
    vMisc = aMisc;
    gl_Position = vec4(aPos.x / uView.x * 2.0 - 1.0, 1.0 - aPos.y / uView.y * 2.0, 0.0, 1.0);
}
)";

const char* kFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vPos;
in vec2 vUV;
in vec4 vColor;
flat in vec4 vRect;
flat in vec3 vMisc;
uniform sampler2D uTex;
uniform float uLinear;
out vec4 oColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

float sdRoundRect(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + vec2(r);
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    int mode = int(vMisc.y + 0.5);
    float cover = 1.0;
    vec4 tex = vec4(1.0);
    if (mode == 1) {
        cover = texture(uTex, vUV).r;
    } else {
        float d = sdRoundRect(vPos - vRect.xy, vRect.zw, vMisc.x);
        if (mode == 0) {
            cover = clamp(0.5 - d, 0.0, 1.0);
        } else if (mode == 2) {
            tex = texture(uTex, vUV);
            cover = clamp(0.5 - d, 0.0, 1.0);
        } else if (mode == 3) {
            cover = clamp(0.5 - d, 0.0, 1.0) * clamp(d + vMisc.z + 0.5, 0.0, 1.0);
        } else {
            cover = 1.0 - smoothstep(-vMisc.z, vMisc.z, d);
        }
    }
    vec3 base = uLinear > 0.5 ? srgbToLinear(vColor.rgb) : vColor.rgb;
    float alpha = vColor.a * cover;
    if (mode == 2) {
        oColor = vec4(tex.rgb * base, tex.a) * alpha;
    } else {
        oColor = vec4(base * alpha, alpha);
    }
}
)";

uint32_t pack(Color c) {
    auto q = [](float v) { return static_cast<uint32_t>(saturate(v) * 255.0f + 0.5f); };
    return q(c.r) | (q(c.g) << 8) | (q(c.b) << 16) | (q(c.a) << 24);
}

}  // namespace

bool Canvas::init(bool linearOutput) {
    linearOutput_ = linearOutput;
    program_ = buildProgram(kVertexSrc, kFragmentSrc, "canvas");
    if (!program_) return false;
    uView_ = glGetUniformLocation(program_, "uView");
    uTex_ = glGetUniformLocation(program_, "uTex");
    uLinear_ = glGetUniformLocation(program_, "uLinear");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    const GLsizei stride = sizeof(Vtx);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vtx, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vtx, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)offsetof(Vtx, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vtx, cx));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vtx, radius));

    std::vector<uint16_t> indices(kMaxQuads * 6);
    for (size_t q = 0; q < kMaxQuads; ++q) {
        uint16_t b = static_cast<uint16_t>(q * 4);
        uint16_t* i = &indices[q * 6];
        i[0] = b;
        i[1] = b + 1;
        i[2] = b + 2;
        i[3] = b;
        i[4] = b + 2;
        i[5] = b + 3;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t), indices.data(),
                 GL_STATIC_DRAW);
    glBindVertexArray(0);
    verts_.reserve(4096);
    return true;
}

void Canvas::shutdown() {
    for (auto& kv : textCache_) kv.second.tex.destroy();
    for (auto& kv : glyphCache_) kv.second.destroy();
    textCache_.clear();
    glyphCache_.clear();
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    vbo_ = ibo_ = vao_ = program_ = 0;
}

void Canvas::begin(int width, int height) {
    width_ = width;
    height_ = height;
    ++frame_;
    if (frame_ % 300 == 0) {  // drop text that has not been drawn for a while
        for (auto it = textCache_.begin(); it != textCache_.end();) {
            if (it->second.lastUse + 900 < frame_) {
                it->second.tex.destroy();
                it = textCache_.erase(it);
            } else {
                ++it;
            }
        }
    }
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    clips_.clear();
    clips_.push_back(Rect(0, 0, static_cast<float>(width), static_cast<float>(height)));
    applyScissor();
    glUseProgram(program_);
    glUniform2f(uView_, static_cast<float>(width), static_cast<float>(height));
    glUniform1i(uTex_, 0);
    glUniform1f(uLinear_, linearOutput_ ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    curTex_ = 0;
    verts_.clear();
}

void Canvas::end() {
    flush();
    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
}

void Canvas::flush() {
    if (verts_.empty()) return;
    glBindTexture(GL_TEXTURE_2D, curTex_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts_.size() * sizeof(Vtx), verts_.data(), GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(verts_.size() / 4 * 6), GL_UNSIGNED_SHORT,
                   nullptr);
    verts_.clear();
}

void Canvas::applyScissor() {
    const Rect& c = clips_.back();
    glScissor(static_cast<GLint>(std::floor(c.x)),
              static_cast<GLint>(std::floor(height_ - (c.y + c.h))),
              static_cast<GLsizei>(std::ceil(c.w)), static_cast<GLsizei>(std::ceil(c.h)));
}

void Canvas::pushClip(Rect r) {
    flush();
    clips_.push_back(clips_.back().intersect(r));
    applyScissor();
}

void Canvas::popClip() {
    if (clips_.size() <= 1) return;
    flush();
    clips_.pop_back();
    applyScissor();
}

void Canvas::push(Rect pos, Rect uv, Color top, Color bottom, Vec2 center, Vec2 half, float radius,
                  float mode, float aux, GLuint tex) {
    if (tex != curTex_ || verts_.size() / 4 >= kMaxQuads) {
        flush();
        curTex_ = tex;
    }
    uint32_t ct = pack(top), cb = pack(bottom);
    float x0 = pos.x, y0 = pos.y, x1 = pos.right(), y1 = pos.bottom();
    float u0 = uv.x, v0 = uv.y, u1 = uv.right(), v1 = uv.bottom();
    verts_.push_back({x0, y0, u0, v0, ct, center.x, center.y, half.x, half.y, radius, mode, aux});
    verts_.push_back({x1, y0, u1, v0, ct, center.x, center.y, half.x, half.y, radius, mode, aux});
    verts_.push_back({x1, y1, u1, v1, cb, center.x, center.y, half.x, half.y, radius, mode, aux});
    verts_.push_back({x0, y1, u0, v1, cb, center.x, center.y, half.x, half.y, radius, mode, aux});
}

void Canvas::rect(Rect r, Color c, float radius) { rectGradient(r, c, c, radius); }

void Canvas::rectGradient(Rect r, Color top, Color bottom, float radius) {
    if (r.w <= 0 || r.h <= 0) return;
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    push(r.expanded(1.0f), Rect(0, 0, 1, 1), top, bottom, r.center(), {r.w * 0.5f, r.h * 0.5f},
         radius, kModeFill, 0, 0);
}

void Canvas::stroke(Rect r, Color c, float thickness, float radius) {
    if (r.w <= 0 || r.h <= 0) return;
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    push(r.expanded(1.0f), Rect(0, 0, 1, 1), c, c, r.center(), {r.w * 0.5f, r.h * 0.5f}, radius,
         kModeStroke, thickness, 0);
}

void Canvas::shadow(Rect r, float radius, float blur, Color c, Vec2 offset) {
    Rect s(r.x + offset.x, r.y + offset.y, r.w, r.h);
    push(s.expanded(blur * 1.5f + 1), Rect(0, 0, 1, 1), c, c, s.center(), {s.w * 0.5f, s.h * 0.5f},
         radius, kModeShadow, blur, 0);
}

void Canvas::circle(Vec2 center, float radius, Color c) {
    rect(Rect(center.x - radius, center.y - radius, radius * 2, radius * 2), c, radius);
}

void Canvas::ring(Vec2 center, float radius, float thickness, Color c) {
    stroke(Rect(center.x - radius, center.y - radius, radius * 2, radius * 2), c, thickness,
           radius);
}

void Canvas::image(const Texture& t, Rect dst, Color tint, float radius) {
    if (!t.valid() || dst.w <= 0 || dst.h <= 0) return;
    radius = std::min(radius, std::min(dst.w, dst.h) * 0.5f);
    push(dst.expanded(1.0f), Rect(-1.0f / dst.w, -1.0f / dst.h, 1 + 2.0f / dst.w, 1 + 2.0f / dst.h),
         tint, tint, dst.center(), {dst.w * 0.5f, dst.h * 0.5f}, radius, kModeImage, 0, t.id);
}

void Canvas::mask(const Texture& t, Rect dst, Color c) {
    if (!t.valid()) return;
    push(dst, Rect(0, 0, 1, 1), c, c, dst.center(), {dst.w * 0.5f, dst.h * 0.5f}, 0, kModeMask, 0,
         t.id);
}

const Texture* Canvas::glyphTexture(const std::string& name, int size) {
    std::string key = name + "#" + std::to_string(size);
    auto it = glyphCache_.find(key);
    if (it != glyphCache_.end()) return it->second.valid() ? &it->second : nullptr;
    Texture t = uploadMask(Jni::renderGlyph(name, size));
    // The upload above unbinds our batch texture assumptions; restore on next push via curTex_.
    glBindTexture(GL_TEXTURE_2D, curTex_);
    auto ins = glyphCache_.emplace(key, t);
    return ins.first->second.valid() ? &ins.first->second : nullptr;
}

void Canvas::glyph(const std::string& name, Rect dst, Color c) {
    float side = std::min(dst.w, dst.h);
    int px = std::max(8, static_cast<int>(std::lround(side)));
    const Texture* t = glyphTexture(name, px);
    if (!t) return;
    Rect sq(dst.center().x - px * 0.5f, dst.center().y - px * 0.5f, static_cast<float>(px),
            static_cast<float>(px));
    mask(*t, sq, c);
}

const Canvas::TextEntry* Canvas::textEntry(const std::string& s, float size, int weight,
                                           float maxWidth) {
    std::string key = std::to_string(static_cast<int>(size * 4)) + ':' + std::to_string(weight) +
                      ':' + std::to_string(static_cast<int>(maxWidth)) + ':' + s;
    auto it = textCache_.find(key);
    if (it != textCache_.end()) {
        it->second.lastUse = frame_;
        return it->second.tex.valid() ? &it->second : nullptr;
    }
    TextEntry e;
    e.tex = uploadMask(Jni::renderText(s, size, weight, static_cast<int>(maxWidth)));
    glBindTexture(GL_TEXTURE_2D, curTex_);
    e.lastUse = frame_;
    auto ins = textCache_.emplace(std::move(key), std::move(e));
    return ins.first->second.tex.valid() ? &ins.first->second : nullptr;
}

Vec2 Canvas::measureText(const std::string& s, float size, int weight, float maxWidth) {
    if (s.empty()) return {0, 0};
    const TextEntry* e = textEntry(s, size, weight, maxWidth);
    if (!e) return {0, 0};
    return {static_cast<float>(e->tex.w - 2 * kTextPad), static_cast<float>(e->tex.h - 2 * kTextPad)};
}

Vec2 Canvas::text(const std::string& s, Vec2 pos, const TextStyle& st) {
    if (s.empty()) return {0, 0};
    const TextEntry* e = textEntry(s, st.size, st.weight, st.maxWidth);
    if (!e) return {0, 0};
    Rect dst(std::round(pos.x) - kTextPad, std::round(pos.y) - kTextPad,
             static_cast<float>(e->tex.w), static_cast<float>(e->tex.h));
    mask(e->tex, dst, st.color);
    return {static_cast<float>(e->tex.w - 2 * kTextPad), static_cast<float>(e->tex.h - 2 * kTextPad)};
}

Vec2 Canvas::textIn(const std::string& s, Rect box, const TextStyle& st, HAlign h, VAlign v) {
    TextStyle style = st;
    if (style.maxWidth <= 0) style.maxWidth = box.w;
    Vec2 sz = measureText(s, style.size, style.weight, style.maxWidth);
    float x = box.x;
    if (h == HAlign::Center) x = box.x + (box.w - sz.x) * 0.5f;
    if (h == HAlign::Right) x = box.right() - sz.x;
    float y = box.y;
    if (v == VAlign::Middle) y = box.y + (box.h - sz.y) * 0.5f;
    if (v == VAlign::Bottom) y = box.bottom() - sz.y;
    return text(s, {x, y}, style);
}

}  // namespace pxr
