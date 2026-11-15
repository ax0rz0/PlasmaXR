#include "ui/ui.h"

#include <cmath>

#include "platform/jni_bridge.h"

namespace pxr {

namespace {
constexpr float kDragThreshold = 14.0f;  // px of travel before a press becomes a scroll drag
constexpr float kSettle = 0.004f;
}  // namespace

uint32_t Ui::id(const char* s, int index) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    h = (h ^ static_cast<uint32_t>(index)) * 16777619u;
    return h ? h : 1u;
}

uint32_t Ui::id(uint32_t base, int index) {
    uint32_t h = (base ^ (static_cast<uint32_t>(index) + 0x9e3779b9u)) * 16777619u;
    return h ? h : 1u;
}

void Ui::beginFrame(Canvas* canvas, float dt, const PointerSample in[kMaxPointers]) {
    cv_ = canvas;
    dt_ = clampf(dt, 0.001f, 0.1f);
    clock_ += dt_;
    needRedraw_ = false;
    for (int i = 0; i < kMaxPointers; ++i) {
        in_[i] = in[i];
        bool down = in_[i].present && in_[i].down;
        pressEdge_[i] = down && !prevDown_[i];
        releaseEdge_[i] = !down && prevDown_[i];
        hotCand_[i] = 0;
        scrollUnderCand_[i] = 0;
        if (in_[i].present && in_[i].wheel != 0.0f && scrollUnder_[i] != 0) {
            auto it = scrolls_.find(scrollUnder_[i]);
            if (it != scrolls_.end()) it->second.externalDelta += in_[i].wheel;
        }
    }
}

bool Ui::endFrame() {
    for (int i = 0; i < kMaxPointers; ++i) {
        if (hotCand_[i] != hot_[i]) {
            needRedraw_ = true;
            if (hotCand_[i] != 0 && !in_[i].down && clock_ - lastHoverSound_ > 0.045f) {
                sounds_.push_back(kSoundHover);
                lastHoverSound_ = clock_;
            }
        }
        hot_[i] = hotCand_[i];
        scrollUnder_[i] = scrollUnderCand_[i];

        if (pressEdge_[i] && active_[i] == 0 && hotCand_[i] != 0) {
            active_[i] = hotCand_[i];
            sounds_.push_back(kSoundPress);
            needRedraw_ = true;
        }
        if (releaseEdge_[i]) {
            if (active_[i] != 0) sounds_.push_back(kSoundRelease);
            active_[i] = 0;
            needRedraw_ = true;
        }
        prevDown_[i] = in_[i].present && in_[i].down;
        if (!in_[i].present) active_[i] = 0;
    }
    return needRedraw_;
}

bool Ui::pointerInside(int i, Rect r) const {
    return in_[i].present && r.contains(in_[i].pos) && cv_ && cv_->clip().contains(in_[i].pos);
}

bool Ui::anyPointerInside(Rect r) const {
    for (int i = 0; i < kMaxPointers; ++i) {
        if (pointerInside(i, r)) return true;
    }
    return false;
}

WidgetState Ui::widget(uint32_t id, Rect r, bool enabled) {
    WidgetState s;
    for (int i = 0; i < kMaxPointers; ++i) {
        if (!in_[i].present) continue;
        bool inside = pointerInside(i, r);
        if (inside && enabled) {
            hotCand_[i] = id;
            s.local = Vec2(in_[i].pos.x - r.x, in_[i].pos.y - r.y);
            if (s.pointer < 0) s.pointer = i;
        }
        if (hot_[i] == id && enabled) {
            s.hovered = true;
            if (s.pointer < 0) s.pointer = i;
        }
        if (active_[i] == id) {
            s.pressed = true;
            s.pointer = i;  // the pressing pointer wins over a merely hovering one
            if (releaseEdge_[i] && inside && enabled) s.clicked = true;
        }
    }
    Anim& a = anim_[id];
    float rate = 1.0f - std::exp(-dt_ * 20.0f);
    float th = s.hovered ? 1.0f : 0.0f, tp = s.pressed ? 1.0f : 0.0f;
    a.hover += (th - a.hover) * rate;
    a.press += (tp - a.press) * rate;
    if (std::fabs(th - a.hover) < kSettle) a.hover = th; else needRedraw_ = true;
    if (std::fabs(tp - a.press) < kSettle) a.press = tp; else needRedraw_ = true;
    s.hover = a.hover;
    s.press = a.press;
    return s;
}

RawPointer Ui::rawPointer(uint32_t id, Rect r) {
    RawPointer out;
    for (int i = 0; i < kMaxPointers; ++i) {
        const bool captured = rawCapture_[i] == id;
        if (!in_[i].present) {
            if (captured) {  // the pointer vanished mid-press: end the press
                rawCapture_[i] = 0;
                if (out.pointer < 0) {
                    out.pointer = i;
                    out.releaseEdge = true;
                }
            }
            continue;
        }
        const bool inside = pointerInside(i, r);
        if (!inside && !captured) continue;
        if (out.pointer >= 0 && !captured) continue;  // one pointer drives the region
        out = RawPointer();
        out.pointer = i;
        out.inside = inside;
        out.local = Vec2(in_[i].pos.x - r.x, in_[i].pos.y - r.y);
        out.down = in_[i].down;
        out.wheel = in_[i].wheel;
        if (pressEdge_[i] && inside) {
            rawCapture_[i] = id;
            out.pressEdge = true;
        }
        if (releaseEdge_[i] && captured) {
            rawCapture_[i] = 0;
            out.releaseEdge = true;
        }
    }
    return out;
}

float Ui::animate(uint32_t id, float target, float rate) {
    Anim& a = anim_[id];
    if (!a.init) {
        a.value = target;
        a.init = true;
    }
    a.value += (target - a.value) * (1.0f - std::exp(-dt_ * rate));
    if (std::fabs(target - a.value) < 0.002f) a.value = target; else needRedraw_ = true;
    return a.value;
}

float Ui::scrollBegin(uint32_t id, Rect view, float contentHeight) {
    Scroll& s = scrolls_[id];
    s.view = view;
    s.viewH = view.h;
    s.contentH = contentHeight;
    float maxOff = std::max(0.0f, contentHeight - view.h);

    for (int i = 0; i < kMaxPointers; ++i) {
        if (!in_[i].present) continue;
        bool inside = pointerInside(i, view);
        if (inside) scrollUnderCand_[i] = id;
        if (pressEdge_[i] && inside) {
            s.pointer = i;
            s.armed = true;
            s.dragging = false;
            s.startPos = in_[i].pos;
            s.startOffset = s.offset;
            s.velocity = 0;
        }
        if (s.armed && s.pointer == i && in_[i].down) {
            float dy = in_[i].pos.y - s.startPos.y;
            if (!s.dragging && std::fabs(dy) > kDragThreshold) {
                s.dragging = true;
                active_[i] = id;  // steals the press from any button under the finger
            }
            if (s.dragging) {
                float target = clampf(s.startOffset - dy, 0, maxOff);
                s.velocity = (target - s.offset) / dt_;
                s.offset = target;
                needRedraw_ = true;
            }
        }
        if (releaseEdge_[i] && s.pointer == i) {
            s.armed = false;
            s.dragging = false;
            s.pointer = -1;
        }
    }
    if (s.externalDelta != 0) {
        s.offset = clampf(s.offset + s.externalDelta, 0, maxOff);
        s.externalDelta = 0;
        needRedraw_ = true;
    }
    if (!s.dragging && std::fabs(s.velocity) > 4.0f) {
        s.offset = clampf(s.offset + s.velocity * dt_, 0, maxOff);
        s.velocity *= std::exp(-dt_ * 4.5f);
        if (s.offset <= 0 || s.offset >= maxOff) s.velocity = 0;
        needRedraw_ = true;
    } else if (!s.dragging) {
        s.velocity = 0;
    }
    s.offset = clampf(s.offset, 0, maxOff);

    scrollStack_.push_back(id);
    cv_->pushClip(view);
    return s.offset;
}

void Ui::scrollEnd() {
    if (scrollStack_.empty()) return;
    scrollStack_.pop_back();
    cv_->popClip();
}

void Ui::scrollBy(uint32_t id, float delta) {
    auto it = scrolls_.find(id);
    if (it != scrolls_.end()) it->second.externalDelta += delta;
}

float Ui::scrollOffset(uint32_t id) const {
    auto it = scrolls_.find(id);
    return it == scrolls_.end() ? 0.0f : it->second.offset;
}

uint32_t Ui::scrollAreaUnder(int pointer) const { return scrollUnder_[pointer]; }

}  // namespace pxr
