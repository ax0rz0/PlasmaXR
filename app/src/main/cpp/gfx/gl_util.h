#pragma once

#include <GLES3/gl32.h>

#include "platform/jni_bridge.h"

namespace pxr {

struct Texture {
    GLuint id = 0;
    int w = 0;
    int h = 0;
    bool valid() const { return id != 0; }
    void destroy() {
        if (id) glDeleteTextures(1, &id);
        id = 0;
    }
};

// Compiles and links a GLSL ES 3.00 program. Returns 0 (and logs) on failure.
GLuint buildProgram(const char* vertexSrc, const char* fragmentSrc, const char* name);

// A8 coverage masks (text, glyphs) -> GL_R8.
Texture uploadMask(const PixelBuffer& pb);
// RGBA8888 premultiplied pixels -> GL_SRGB8_ALPHA8 (or GL_RGBA8), optionally mipmapped.
Texture uploadRgba(const PixelBuffer& pb, bool srgb, bool mipmaps);

void glCheck(const char* where);

}  // namespace pxr
