#include "gfx/surface_textures.h"

#include <cstring>
#include <vector>

namespace pxr {

void SurfaceTextures::update(uint32_t uid, int width, int height, int stride, bool hasAlpha,
                             const uint8_t* pixels, int x, int y, int w, int h, bool full) {
    Texture& t = tex_[uid];
    const bool fresh = t.id == 0;
    if (fresh) {
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, t.id);
    }
    // wl_shm ARGB8888 is B,G,R,A in memory; upload it as RGBA and swap red and blue when sampling.
    // XRGB8888 has no meaningful alpha byte, so force it opaque.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_GREEN);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, hasAlpha ? GL_ALPHA : GL_ONE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    const bool rowLengthOk = stride % 4 == 0;
    const bool realloc = fresh || full || t.w != width || t.h != height;

    if (rowLengthOk) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
        if (realloc) {
            glTexImage2D(GL_TEXTURE_2D, 0, srgb_ ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, pixels);
            t.w = width;
            t.h = height;
        } else {
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, x);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, y);
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    } else {
        // A stride that is not a whole number of pixels cannot be expressed to GL: repack.
        std::vector<uint8_t> packed(static_cast<size_t>(width) * height * 4);
        for (int row = 0; row < height; ++row) {
            memcpy(&packed[static_cast<size_t>(row) * width * 4], pixels + static_cast<size_t>(row) * stride,
                   static_cast<size_t>(width) * 4);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, srgb_ ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, packed.data());
        t.w = width;
        t.h = height;
    }
}

void SurfaceTextures::remove(uint32_t uid) {
    auto it = tex_.find(uid);
    if (it == tex_.end()) return;
    it->second.destroy();
    tex_.erase(it);
}

const Texture* SurfaceTextures::find(uint32_t uid) const {
    auto it = tex_.find(uid);
    return it == tex_.end() || !it->second.valid() ? nullptr : &it->second;
}

void SurfaceTextures::clear() {
    for (auto& kv : tex_) kv.second.destroy();
    tex_.clear();
}

}  // namespace pxr
