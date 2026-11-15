// One GL texture per Wayland surface, fed by the compositor (wl_shm pixels are uploaded as they
// are committed, damage rectangle only).
#pragma once

#include <unordered_map>

#include "gfx/gl_util.h"
#include "wayland/server.h"

namespace pxr {

class SurfaceTextures : public wl::Presenter {
public:
    ~SurfaceTextures() { clear(); }

    // Match the framebuffer the canvas draws into: an sRGB target needs sRGB-decoded textures.
    void setSrgb(bool srgb) { srgb_ = srgb; }

    void update(uint32_t uid, int width, int height, int stride, bool hasAlpha, const uint8_t* pixels,
                int x, int y, int w, int h, bool full) override;
    void remove(uint32_t uid) override;

    // Premultiplied RGBA, rows top to bottom. Null if the surface has no pixels.
    const Texture* find(uint32_t uid) const;
    void clear();

private:
    bool srgb_ = true;
    std::unordered_map<uint32_t, Texture> tex_;
};

}  // namespace pxr
