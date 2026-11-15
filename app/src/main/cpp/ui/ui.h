// Immediate-mode UI core. A Panel owns one Ui; its draw callback calls widget()/scroll() while it
// draws, and the Ui turns the two possible pointers (one per hand) into hover/press/click state.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/math.h"
#include "gfx/canvas.h"

namespace pxr {

constexpr int kMaxPointers = 2;

struct PointerSample {
    bool present = false;
    Vec2 pos;            // panel pixels
    bool down = false;   // pinch / touch is pressed
    bool poke = false;   // direct touch instead of a far ray
    float wheel = 0;     // scroll to apply this frame, in pixels (controller thumbstick)
};

struct WidgetState {
    bool hovered = false;
    bool pressed = false;   // held down by a pointer
    bool clicked = false;   // released over the widget after being pressed on it
    float hover = 0;        // smoothed 0..1
    float press = 0;        // smoothed 0..1
    Vec2 local;             // hovering pointer relative to the widget rect
    int pointer = -1;       // the pointer hovering or pressing it (-1 = none)
};

// A region that wants raw pointer events instead of widget semantics (a hosted app's surface).
struct RawPointer {
    int pointer = -1;       // the pointer driving the region (-1 = none over it and none captured)
    bool inside = false;
    Vec2 local;             // relative to the region's top-left
    bool down = false;
    bool pressEdge = false;     // went down inside the region this frame
    bool releaseEdge = false;   // a press that began inside was released this frame
    float wheel = 0;            // scroll in pixels (controller thumbstick)
};

class Ui {
public:
    void beginFrame(Canvas* canvas, float dt, const PointerSample in[kMaxPointers]);
    // Returns true when the panel needs to be redrawn again (animations, hover changes).
    bool endFrame();

    WidgetState widget(uint32_t id, Rect r, bool enabled = true);
    // Reports one pointer's position and button edges over `r`. A press that starts inside keeps
    // the pointer captured, so drags that leave the region still arrive until the release.
    RawPointer rawPointer(uint32_t id, Rect r);

    // Vertical scroll area: call scrollBegin, draw content offset by -returned value, scrollEnd.
    float scrollBegin(uint32_t id, Rect view, float contentHeight);
    void scrollEnd();
    void scrollBy(uint32_t id, float delta);
    float scrollOffset(uint32_t id) const;
    // The scroll area under pointer `p` (0 if none), for routing external scroll input.
    uint32_t scrollAreaUnder(int pointer) const;

    // Smoothly chases a target; keeps the panel redrawing while it moves.
    float animate(uint32_t id, float target, float rate = 16.0f);
    void requestRedraw() { needRedraw_ = true; }

    bool pointerPresent(int i) const { return in_[i].present; }
    bool pointerInside(int i, Rect r) const;
    bool anyPointerInside(Rect r) const;
    Vec2 pointerPos(int i) const { return in_[i].pos; }
    float dt() const { return dt_; }

    // Sounds queued by interaction this frame (see Sound in jni_bridge.h); drained by the shell.
    std::vector<int>& sounds() { return sounds_; }

    static uint32_t id(const char* s, int index = 0);
    static uint32_t id(uint32_t base, int index);

private:
    struct Anim {
        float hover = 0;
        float press = 0;
        float value = 0;
        bool init = false;
    };
    struct Scroll {
        float offset = 0;
        float velocity = 0;
        float viewH = 0;
        float contentH = 0;
        int pointer = -1;
        bool armed = false;
        bool dragging = false;
        Vec2 startPos;
        float startOffset = 0;
        Rect view;
        float externalDelta = 0;
    };

    Canvas* cv_ = nullptr;
    float dt_ = 0.016f;
    PointerSample in_[kMaxPointers];
    bool prevDown_[kMaxPointers] = {false, false};
    bool pressEdge_[kMaxPointers] = {false, false};
    bool releaseEdge_[kMaxPointers] = {false, false};
    uint32_t hot_[kMaxPointers] = {0, 0};
    uint32_t hotCand_[kMaxPointers] = {0, 0};
    uint32_t active_[kMaxPointers] = {0, 0};
    uint32_t rawCapture_[kMaxPointers] = {0, 0};
    uint32_t scrollUnder_[kMaxPointers] = {0, 0};
    uint32_t scrollUnderCand_[kMaxPointers] = {0, 0};
    std::unordered_map<uint32_t, Anim> anim_;
    std::unordered_map<uint32_t, Scroll> scrolls_;
    std::vector<uint32_t> scrollStack_;
    std::vector<int> sounds_;
    bool needRedraw_ = false;
    float lastHoverSound_ = -1;
    float clock_ = 0;
};

}  // namespace pxr
