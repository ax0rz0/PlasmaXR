// 3D pass for everything that must draw on top of the UI panels: hands, pointer rays and
// cursors. Rendered into a transparent projection layer submitted after the panel layers.
#pragma once

#include <GLES3/gl32.h>

#include "core/math.h"
#include "input/controllers.h"
#include "input/hands.h"
#include "input/pointers.h"

namespace pxr {

struct OverlayStyle {
    bool showMesh = true;
    bool showSkeleton = false;
    bool showRays = true;
    bool showCursors = true;
    Color handColor = Color::hex(0x3daee9, 0.46f);
    Color accent = Color::hex(0x3daee9);
};

class Overlay {
public:
    bool init(bool linearOutput);
    void shutdown();

    void render(const Mat4& view, const Mat4& proj, const Vec3& eye, const HandTracking& hands,
                const Controllers& controllers, const PointerSystem& pointers,
                const OverlayStyle& style, float time);

private:
    struct Mesh {
        GLuint vao = 0, vbo = 0, ibo = 0;
        GLsizei count = 0;
    };
    struct HandGl {
        GLuint vao = 0, vbo = 0, ibo = 0;
        GLsizei indexCount = 0;
        size_t vertexCount = 0;
    };

    void buildSphere();
    void buildCylinder();
    void buildQuad();
    void drawMesh(const Mesh& m, const Mat4& model, Color c, float rim, bool fade);
    void drawHandMesh(int h, const HandTracking& hands, const OverlayStyle& style);
    void drawSkeleton(const HandState& hs, const OverlayStyle& style);
    void drawCylinderBetween(Vec3 a, Vec3 b, float radius, Color c, float rim, bool fade);
    void drawDecal(Vec3 pos, Vec3 normal, float radius, Color c, float ringInner, float fillRadius,
                   float fillAmount);

    GLuint solid_ = 0, decal_ = 0;
    GLint sViewProj_ = -1, sModel_ = -1, sCam_ = -1, sColor_ = -1, sRim_ = -1, sFade_ = -1,
          sLinear_ = -1;
    GLint dViewProj_ = -1, dModel_ = -1, dColor_ = -1, dRing_ = -1, dFillR_ = -1, dFill_ = -1,
          dLinear_ = -1;
    Mesh sphere_, cylinder_, quad_;
    HandGl handGl_[2];
    bool linear_ = true;
    Mat4 viewProj_ = Mat4::identity();
    Vec3 eye_;
};

}  // namespace pxr
