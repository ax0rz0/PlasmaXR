// Small, dependency-free vector math. Right-handed, +Y up, -Z forward (OpenXR convention).
// Matrices are column-major (OpenGL layout): m[col * 4 + row].
#pragma once

#include <algorithm>
#include <cmath>

namespace pxr {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float smoothstepf(float a, float b, float x) {
    float t = saturate((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

struct Vec2 {
    float x = 0, y = 0;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    float length() const { return std::sqrt(x * x + y * y); }
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    float dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(Vec3 o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const {
        float l = length();
        return l > 1e-8f ? *this / l : Vec3(0, 0, 0);
    }
};

inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline float distance(Vec3 a, Vec3 b) { return (a - b).length(); }

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat fromAxisAngle(Vec3 axis, float angle) {
        Vec3 a = axis.normalized();
        float s = std::sin(angle * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(angle * 0.5f)};
    }

    // Builds a rotation whose local X/Y/Z axes map onto the given (orthonormal) world vectors.
    static Quat fromBasis(Vec3 bx, Vec3 by, Vec3 bz) {
        float m00 = bx.x, m01 = by.x, m02 = bz.x;
        float m10 = bx.y, m11 = by.y, m12 = bz.y;
        float m20 = bx.z, m21 = by.z, m22 = bz.z;
        float tr = m00 + m11 + m22;
        Quat q;
        if (tr > 0) {
            float s = std::sqrt(tr + 1.0f) * 2.0f;
            q.w = 0.25f * s;
            q.x = (m21 - m12) / s;
            q.y = (m02 - m20) / s;
            q.z = (m10 - m01) / s;
        } else if (m00 > m11 && m00 > m22) {
            float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            q.w = (m21 - m12) / s;
            q.x = 0.25f * s;
            q.y = (m01 + m10) / s;
            q.z = (m02 + m20) / s;
        } else if (m11 > m22) {
            float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            q.w = (m02 - m20) / s;
            q.x = (m01 + m10) / s;
            q.y = 0.25f * s;
            q.z = (m12 + m21) / s;
        } else {
            float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            q.w = (m10 - m01) / s;
            q.x = (m02 + m20) / s;
            q.y = (m12 + m21) / s;
            q.z = 0.25f * s;
        }
        return q.normalized();
    }

    // Orientation whose -Z axis points along `forward` with `up` as the approximate up vector.
    static Quat lookRotation(Vec3 forward, Vec3 up = {0, 1, 0}) {
        Vec3 z = (-forward).normalized();
        Vec3 x = up.cross(z);
        if (x.length() < 1e-5f) x = Vec3(1, 0, 0).cross(z);
        x = x.normalized();
        Vec3 y = z.cross(x);
        return fromBasis(x, y, z);
    }

    Quat operator*(const Quat& o) const {
        return {w * o.x + x * o.w + y * o.z - z * o.y, w * o.y - x * o.z + y * o.w + z * o.x,
                w * o.z + x * o.y - y * o.x + z * o.w, w * o.w - x * o.x - y * o.y - z * o.z};
    }
    Quat conjugate() const { return {-x, -y, -z, w}; }
    float dot(const Quat& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
    Quat normalized() const {
        float l = std::sqrt(x * x + y * y + z * z + w * w);
        return l > 1e-8f ? Quat{x / l, y / l, z / l, w / l} : Quat{};
    }
    Vec3 rotate(Vec3 v) const {
        Vec3 q(x, y, z);
        Vec3 t = q.cross(v) * 2.0f;
        return v + t * w + q.cross(t);
    }
};

inline Quat slerp(Quat a, Quat b, float t) {
    float d = a.dot(b);
    if (d < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) {
        return Quat{lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t), lerpf(a.w, b.w, t)}
            .normalized();
    }
    float th = std::acos(d);
    float s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s;
    float wb = std::sin(t * th) / s;
    return Quat{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb}
        .normalized();
}

struct Pose {
    Quat q;
    Vec3 p;
    Vec3 apply(Vec3 v) const { return q.rotate(v) + p; }
    Vec3 rotate(Vec3 v) const { return q.rotate(v); }
    Pose inverse() const {
        Quat qi = q.conjugate();
        return {qi, qi.rotate(-p)};
    }
    Pose operator*(const Pose& o) const { return {(q * o.q).normalized(), apply(o.p)}; }
    Vec3 forward() const { return q.rotate({0, 0, -1}); }
    Vec3 right() const { return q.rotate({1, 0, 0}); }
    Vec3 up() const { return q.rotate({0, 1, 0}); }
};

struct Mat4 {
    float m[16];

    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
        return r;
    }
    static Mat4 translation(Vec3 t) {
        Mat4 r = identity();
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }
    static Mat4 scale(Vec3 s) {
        Mat4 r = identity();
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        return r;
    }
    static Mat4 fromQuat(const Quat& q) {
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        Mat4 r = identity();
        r.m[0] = 1 - 2 * (yy + zz);
        r.m[1] = 2 * (xy + wz);
        r.m[2] = 2 * (xz - wy);
        r.m[4] = 2 * (xy - wz);
        r.m[5] = 1 - 2 * (xx + zz);
        r.m[6] = 2 * (yz + wx);
        r.m[8] = 2 * (xz + wy);
        r.m[9] = 2 * (yz - wx);
        r.m[10] = 1 - 2 * (xx + yy);
        return r;
    }
    static Mat4 fromPose(const Pose& p) {
        Mat4 r = fromQuat(p.q);
        r.m[12] = p.p.x;
        r.m[13] = p.p.y;
        r.m[14] = p.p.z;
        return r;
    }
    static Mat4 trs(Vec3 t, const Quat& q, Vec3 s) {
        Mat4 r = fromQuat(q);
        for (int i = 0; i < 3; ++i) {
            r.m[i] *= s.x;
            r.m[4 + i] *= s.y;
            r.m[8 + i] *= s.z;
        }
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }
    // Asymmetric OpenXR field of view -> OpenGL clip space (z in [-1, 1]).
    static Mat4 projection(float angleLeft, float angleRight, float angleUp, float angleDown,
                           float nearZ, float farZ) {
        float l = std::tan(angleLeft), r = std::tan(angleRight);
        float u = std::tan(angleUp), d = std::tan(angleDown);
        float w = r - l, h = u - d;
        Mat4 o{};
        o.m[0] = 2.0f / w;
        o.m[5] = 2.0f / h;
        o.m[8] = (r + l) / w;
        o.m[9] = (u + d) / h;
        o.m[10] = -(farZ + nearZ) / (farZ - nearZ);
        o.m[11] = -1.0f;
        o.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
        return o;
    }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r{};
        for (int c = 0; c < 4; ++c) {
            for (int rI = 0; rI < 4; ++rI) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + rI] * b.m[c * 4 + k];
                r.m[c * 4 + rI] = s;
            }
        }
        return r;
    }
    Vec3 point(Vec3 v) const {
        return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12],
                m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
                m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]};
    }
};

struct Ray {
    Vec3 o;
    Vec3 d;  // unit length
    Vec3 at(float t) const { return o + d * t; }
};

// Returns true and the ray parameter if the ray hits the plane in front of its origin.
inline bool intersectPlane(const Ray& r, Vec3 point, Vec3 normal, float* t) {
    float denom = normal.dot(r.d);
    if (std::fabs(denom) < 1e-6f) return false;
    float tt = normal.dot(point - r.o) / denom;
    if (tt < 0) return false;
    *t = tt;
    return true;
}

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    bool contains(Vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    Rect expanded(float d) const { return inset(-d); }
    Rect intersect(const Rect& o) const {
        float nx = std::max(x, o.x), ny = std::max(y, o.y);
        float nr = std::min(right(), o.right()), nb = std::min(bottom(), o.bottom());
        return {nx, ny, std::max(0.0f, nr - nx), std::max(0.0f, nb - ny)};
    }
};

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
    static constexpr Color rgb(int r, int g, int b, float a = 1.0f) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a};
    }
    static constexpr Color hex(unsigned v, float a = 1.0f) {
        return rgb((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff, a);
    }
    constexpr Color withAlpha(float na) const { return {r, g, b, na}; }
    constexpr Color scaledAlpha(float s) const { return {r, g, b, a * s}; }
};

inline Color mix(Color a, Color b, float t) {
    return {lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t), lerpf(a.a, b.a, t)};
}

}  // namespace pxr
