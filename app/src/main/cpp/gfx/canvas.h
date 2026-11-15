// Batched 2D renderer used for every UI panel. Draws SDF rounded rectangles, strokes, soft
// shadows, textured icons and Android-rasterised text into the currently bound framebuffer.
// Coordinates are pixels with the origin at the top-left of the panel.
#pragma once

#include <GLES3/gl32.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "core/math.h"
#include "gfx/gl_util.h"

namespace pxr {

struct TextStyle {
    float size = 32;
    int weight = 400;
    Color color{1, 1, 1, 1};
    float maxWidth = 0;  // 0 = unbounded, otherwise truncated with an ellipsis
};

enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

class Canvas {
public:
    // `linearOutput` = the target is an sRGB framebuffer, so shader colours are linearised.
    bool init(bool linearOutput);
    void shutdown();
    void setLinearOutput(bool v) { linearOutput_ = v; }

    void begin(int width, int height);
    void end();

    void rect(Rect r, Color c, float radius = 0);
    void rectGradient(Rect r, Color top, Color bottom, float radius = 0);
    void stroke(Rect r, Color c, float thickness, float radius = 0);  // drawn inside the rect
    void shadow(Rect r, float radius, float blur, Color c, Vec2 offset = {});
    void circle(Vec2 center, float radius, Color c);
    void ring(Vec2 center, float radius, float thickness, Color c);
    void image(const Texture& t, Rect dst, Color tint = {1, 1, 1, 1}, float radius = 0);
    void mask(const Texture& t, Rect dst, Color c);
    void glyph(const std::string& name, Rect dst, Color c);

    Vec2 measureText(const std::string& s, float size, int weight = 400, float maxWidth = 0);
    // Draws with the top-left of the line box at `pos`; returns the logical size.
    Vec2 text(const std::string& s, Vec2 pos, const TextStyle& st);
    Vec2 textIn(const std::string& s, Rect box, const TextStyle& st, HAlign h = HAlign::Center,
                VAlign v = VAlign::Middle);

    void pushClip(Rect r);
    void popClip();
    Rect clip() const { return clips_.empty() ? Rect() : clips_.back(); }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    struct Vtx {
        float x, y, u, v;
        uint32_t color;
        float cx, cy, hw, hh;
        float radius, mode, aux;
    };
    struct TextEntry {
        Texture tex;
        uint64_t lastUse = 0;
    };

    void push(Rect pos, Rect uv, Color top, Color bottom, Vec2 center, Vec2 half, float radius,
              float mode, float aux, GLuint tex);
    void flush();
    void applyScissor();
    const TextEntry* textEntry(const std::string& s, float size, int weight, float maxWidth);
    const Texture* glyphTexture(const std::string& name, int size);

    GLuint program_ = 0;
    GLuint vao_ = 0, vbo_ = 0, ibo_ = 0;
    GLint uView_ = -1, uTex_ = -1, uLinear_ = -1;
    bool linearOutput_ = true;

    std::vector<Vtx> verts_;
    GLuint curTex_ = 0;
    std::vector<Rect> clips_;
    int width_ = 0, height_ = 0;
    uint64_t frame_ = 0;

    std::unordered_map<std::string, TextEntry> textCache_;
    std::unordered_map<std::string, Texture> glyphCache_;
};

}  // namespace pxr
