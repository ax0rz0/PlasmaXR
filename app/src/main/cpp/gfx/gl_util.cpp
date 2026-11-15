#include "gfx/gl_util.h"

#include <vector>

#include "core/log.h"

namespace pxr {

namespace {

GLuint compile(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("Shader compile failed (%s, %s): %s", name, type == GL_VERTEX_SHADER ? "vs" : "fs",
             log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

}  // namespace

GLuint buildProgram(const char* vertexSrc, const char* fragmentSrc, const char* name) {
    GLuint vs = compile(GL_VERTEX_SHADER, vertexSrc, name);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSrc, name);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        LOGE("Program link failed (%s): %s", name, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

Texture uploadMask(const PixelBuffer& pb) {
    Texture t;
    if (!pb.valid()) return t;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pb.stride);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, pb.width, pb.height, 0, GL_RED, GL_UNSIGNED_BYTE,
                 pb.data.data());
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    t.w = pb.width;
    t.h = pb.height;
    return t;
}

Texture uploadRgba(const PixelBuffer& pb, bool srgb, bool mipmaps) {
    Texture t;
    if (!pb.valid() || pb.alpha8) return t;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pb.stride / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, pb.width, pb.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pb.data.data());
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if (mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    t.w = pb.width;
    t.h = pb.height;
    return t;
}

void glCheck(const char* where) {
    GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR) LOGE("GL error 0x%x at %s", e, where);
}

}  // namespace pxr
