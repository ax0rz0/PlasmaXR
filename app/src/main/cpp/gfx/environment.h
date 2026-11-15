// The "wallpaper": a procedural Plasma-flavoured sky rendered once into an equirect layer that
// sits behind everything else. Costs nothing per frame after startup.
#pragma once

#include "xr/panel.h"
#include "xr/xr_context.h"

namespace pxr {

class Environment {
public:
    bool init(XrContext& xr);
    void shutdown(XrContext& xr);
    bool ready() const { return ready_; }
    const XrCompositionLayerBaseHeader* layer(const XrContext& xr, const Anchor& anchor);

private:
    Swapchain sc_;
    bool ready_ = false;
    XrCompositionLayerEquirect2KHR layer_{};
};

}  // namespace pxr
