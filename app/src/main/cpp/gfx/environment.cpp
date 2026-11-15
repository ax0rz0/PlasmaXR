#include "gfx/environment.h"

#include "core/log.h"
#include "gfx/gl_util.h"

namespace pxr {

namespace {

const char* kVs = R"(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kFs = R"(#version 300 es
precision highp float;
uniform vec2 uRes;
uniform float uLinear;
out vec4 oColor;

vec3 toLin(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
float hash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.71, 0.113, 0.419));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

void main() {
    vec2 uv = gl_FragCoord.xy / uRes;
    float lon = (uv.x - 0.5) * 6.2831853;   // 0 = straight ahead (-Z)
    float lat = (uv.y - 0.5) * 3.1415927;
    float h = lat / 1.5707963;              // -1 (down) .. 1 (up)

    vec3 deep = vec3(0.016, 0.035, 0.070);
    vec3 glow = vec3(0.105, 0.420, 0.660);

    float sky = smoothstep(-0.20, 0.95, h);
    vec3 col = mix(glow * 0.50, deep, pow(sky, 0.75));
    col = mix(col, deep * 0.55, smoothstep(0.0, -0.85, h));

    float band = exp(-pow((h - 0.04) / 0.24, 2.0));
    col += glow * 0.30 * band;

    // Soft diagonal ribbons in the spirit of Plasma's default wallpapers.
    float r1 = sin(lon * 2.0 + h * 3.2 + 0.6) * 0.5 + 0.5;
    float r2 = sin(lon * 3.0 - h * 4.4 + 2.1) * 0.5 + 0.5;
    float r3 = sin(lon * 1.0 + h * 2.1 - 1.3) * 0.5 + 0.5;
    col += vec3(0.10, 0.36, 0.62) * 0.11 * smoothstep(0.55, 1.0, r1) * (0.35 + band);
    col += vec3(0.20, 0.52, 0.88) * 0.075 * smoothstep(0.62, 1.0, r2) * (0.30 + band);
    col += vec3(0.06, 0.22, 0.45) * 0.10 * smoothstep(0.50, 1.0, r3) * (0.5 + band * 0.5);

    // A luminous core behind the desktop.
    float d = length(vec2(lon, (lat - 0.06) * 1.7));
    col += vec3(0.24, 0.68, 0.91) * 0.20 * exp(-d * d * 1.3);

    // A few stars in the upper sky.
    vec3 dir = vec3(sin(lon) * cos(lat), sin(lat), -cos(lon) * cos(lat));
    float s = hash(floor(dir * 150.0));
    col += vec3(0.80, 0.90, 1.00) * step(0.9972, s) * smoothstep(0.05, 0.55, h) * 0.85;

    col = clamp(col, 0.0, 1.0);
    if (uLinear > 0.5) col = toLin(col);
    oColor = vec4(col, 1.0);
}
)";

constexpr int kWidth = 2048;
constexpr int kHeight = 1024;

}  // namespace

bool Environment::init(XrContext& xr) {
    if (!xr.features().equirect2Layer) {
        LOGW("Environment: equirect2 layers unsupported; skipping wallpaper");
        return false;
    }
    if (!xr.createSwapchain(kWidth, kHeight, sc_)) return false;
    GLuint prog = buildProgram(kVs, kFs, "environment");
    if (!prog) return false;

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    if (!xr.acquireImage(sc_)) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, sc_.fbos[sc_.acquired]);
    glViewport(0, 0, kWidth, kHeight);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(prog);
    glUniform2f(glGetUniformLocation(prog, "uRes"), static_cast<float>(kWidth),
                static_cast<float>(kHeight));
    glUniform1f(glGetUniformLocation(prog, "uLinear"),
                xr.colorFormat() == GL_SRGB8_ALPHA8 ? 1.0f : 0.0f);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glFlush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    xr.releaseImage(sc_);

    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(prog);
    ready_ = true;
    LOGI("Environment: wallpaper rendered (%dx%d)", kWidth, kHeight);
    return true;
}

void Environment::shutdown(XrContext& xr) {
    xr.destroySwapchain(sc_);
    ready_ = false;
}

const XrCompositionLayerBaseHeader* Environment::layer(const XrContext& xr, const Anchor& anchor) {
    if (!ready_) return nullptr;
    layer_ = {XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR};
    layer_.layerFlags = 0;
    layer_.space = xr.appSpace();
    layer_.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    layer_.subImage.swapchain = sc_.handle;
    layer_.subImage.imageRect = {{0, 0}, {kWidth, kHeight}};
    layer_.subImage.imageArrayIndex = 0;
    Pose p;
    p.q = Quat::fromAxisAngle({0, 1, 0}, anchor.yaw);
    p.p = anchor.pos;
    layer_.pose = toXr(p);
    layer_.radius = 0.0f;
    layer_.centralHorizontalAngle = 2.0f * kPi;
    layer_.upperVerticalAngle = kPi * 0.5f;
    layer_.lowerVerticalAngle = -kPi * 0.5f;
    return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer_);
}

}  // namespace pxr
