#include "shell/window_manager.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "shell/theme.h"

namespace pxr {

namespace {

constexpr float kDockRadius = 2.0f;       // matches the shell cylinder
constexpr float kDockPpm = 800.0f;
constexpr float kFloatDistance = 1.15f;   // where a new floating window appears
constexpr float kMaxContentW = 2100.0f;
constexpr float kMaxContentH = 1100.0f;
constexpr float kYawLimit = 1.1f;         // docked windows stay within about +-63 degrees
constexpr float kYMin = -0.6f;
constexpr float kYMax = 1.1f;

TextStyle ts(float size, Color color, int weight = 400, float maxW = 0) {
    TextStyle t;
    t.size = size;
    t.color = color;
    t.weight = weight;
    t.maxWidth = maxW;
    return t;
}

}  // namespace

WindowLayout windowLayout(int pxW, int pxH, bool showMaximize) {
    using namespace winframe;
    WindowLayout L;
    L.frame = Rect(kMargin, kMargin, pxW - 2 * kMargin, pxH - 2 * kMargin);
    L.title = Rect(L.frame.x, L.frame.y, L.frame.w, kTitleH);
    L.content = Rect(L.frame.x + kBorder, L.frame.y + kTitleH, L.frame.w - 2 * kBorder,
                     L.frame.h - kTitleH - kBorder);
    float bx = L.title.right() - 14 - kButton;
    const float by = L.title.y + (kTitleH - kButton) * 0.5f;
    L.close = Rect(bx, by, kButton, kButton);
    bx -= kButton + kButtonGap;
    if (showMaximize) {
        L.maximize = Rect(bx, by, kButton, kButton);
        bx -= kButton + kButtonGap;
    }
    L.minimize = Rect(bx, by, kButton, kButton);
    bx -= kButton + kButtonGap;
    L.dock = Rect(bx, by, kButton, kButton);
    L.dragArea = Rect(L.title.x, L.title.y, bx - L.title.x - 4, kTitleH);
    L.grip = Rect(L.frame.right() - kGrip, L.frame.bottom() - kGrip, kGrip, kGrip);
    return L;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

void WindowManager::init(XrContext* xr, Hooks hooks) {
    xr_ = xr;
    hooks_ = std::move(hooks);
}

void WindowManager::shutdown() {
    for (auto& w : windows_) {
        if (hooks_.onPanelDestroyed) hooks_.onPanelDestroyed(w->panel.get());
        w->panel->destroy(*xr_);
    }
    windows_.clear();
    focused_ = nullptr;
    for (Drag& d : drag_) d.win = nullptr;
}

Window* WindowManager::find(const std::string& appId) const {
    for (const auto& w : windows_) {
        if (!w->closing && w->content->appId() == appId) return w.get();
    }
    return nullptr;
}

Window* WindowManager::windowOf(const Panel* p) const {
    if (!p) return nullptr;
    for (const auto& w : windows_) {
        if (w->panel.get() == p) return w.get();
    }
    return nullptr;
}

std::vector<Panel*> WindowManager::panels() const {
    std::vector<Panel*> out;
    for (const auto& w : windows_) out.push_back(w->panel.get());
    return out;
}

Window* WindowManager::open(std::unique_ptr<WindowContent> content, bool floating) {
    if (Window* existing = find(content->appId())) {
        focus(existing);
        return existing;
    }
    using namespace winframe;
    auto w = std::make_unique<Window>();
    w->id = nextId_++;
    w->content = std::move(content);
    w->contentPx = w->content->defaultSize();
    w->floating = floating;
    // A floating window is closer, so it gets a higher pixel density to look the same size.
    w->ppm = floating ? kDockPpm * (kDockRadius / kFloatDistance) : kDockPpm;

    auto p = std::make_unique<Panel>();
    p->name = "win:" + w->content->appId();
    p->radius = kDockRadius;
    p->curved = true;
    p->hitMargin = kMargin;
    p->opacity = 0;
    p->order = 10;
    Window* raw = w.get();
    p->draw = [this, raw](Canvas& cv, Ui& ui) { drawWindow(*raw, cv, ui); };

    const float frameW = w->contentPx.x + 2 * kBorder;
    const float frameH = w->contentPx.y + kTitleH + kBorder;
    p->pxW = static_cast<int>(std::lround(frameW + 2 * kMargin));
    p->pxH = static_cast<int>(std::lround(frameH + 2 * kMargin));
    p->widthM = p->pxW / w->ppm;
    p->heightM = p->pxH / w->ppm;

    if (floating) {
        Vec3 f = head_.forward();
        f.y = 0;
        f = f.length() > 0.1f ? f.normalized() : Vec3(0, 0, -1);
        p->free = true;
        p->freePose = {Quat::lookRotation(f), head_.p + f * kFloatDistance + Vec3(0, -0.05f, 0)};
    } else {
        const int n = static_cast<int>(windows_.size());
        p->yaw = -(-0.30f + 0.30f * (n % 3)) / kDockRadius;
        p->centerY = 0.22f - 0.07f * (n % 3);
    }
    if (!p->create(*xr_)) return nullptr;
    w->panel = std::move(p);
    windows_.push_back(std::move(w));
    focus(raw);
    ++revision_;
    LOGI("Window opened: %s (%s)", raw->content->appId().c_str(), floating ? "floating" : "docked");
    return raw;
}

void WindowManager::close(Window* w) {
    if (w) w->pendingClose = true;
}

void WindowManager::raise(Window& w) { w.panel->order = 10 + zNext_++; }

void WindowManager::focus(Window* w) {
    if (!w || w->closing) return;
    Window* old = focused_;
    w->minimized = false;
    focused_ = w;
    raise(*w);
    w->panel->dirty = true;
    if (old && old != w) old->panel->dirty = true;
    ++revision_;
}

void WindowManager::focusTopmost() {
    Window* best = nullptr;
    for (const auto& w : windows_) {
        if (w->closing || w->minimized) continue;
        if (!best || w->panel->order > best->panel->order) best = w.get();
    }
    Window* old = focused_;
    focused_ = best;
    if (old != best) {
        if (old) old->panel->dirty = true;
        if (best) best->panel->dirty = true;
        ++revision_;
    }
}

void WindowManager::taskbarClick(Window* w) {
    if (!w || w->closing) return;
    if (w->minimized) focus(w);
    else if (focused_ == w) w->pendingMinimize = true;
    else focus(w);
}

void WindowManager::minimize(Window& w) {
    if (w.minimized) return;
    w.minimized = true;
    if (focused_ == &w) focusTopmost();
    ++revision_;
}

// ---------------------------------------------------------------------------------------------
// Layout and placement
// ---------------------------------------------------------------------------------------------

void WindowManager::applyLayout(Window& w) {
    using namespace winframe;
    w.reshapeAge = 0;
    const float frameW = w.contentPx.x + 2 * kBorder;
    const float frameH = w.contentPx.y + kTitleH + kBorder;
    const int pxW = static_cast<int>(std::lround(frameW + 2 * kMargin));
    const int pxH = static_cast<int>(std::lround(frameH + 2 * kMargin));
    w.panel->resize(*xr_, pxW, pxH, pxW / w.ppm, pxH / w.ppm);
}

void WindowManager::setContentFromFrame(Window& w, float frameWm, float frameHm) {
    using namespace winframe;
    const Vec2 mn = w.content->minSize();
    const float cw = clampf(frameWm * w.ppm - 2 * kBorder, mn.x, kMaxContentW);
    const float ch = clampf(frameHm * w.ppm - kTitleH - kBorder, mn.y, kMaxContentH);
    w.contentPx = {std::round(cw), std::round(ch)};
    applyLayout(w);
}

void WindowManager::setFloating(Window& w, bool floating) {
    if (w.floating == floating) return;
    Panel& p = *w.panel;
    if (floating) {
        // Pull the window toward you along the line to its current position.
        const Vec3 c = p.center(anchor_);
        const Vec3 dir = c - head_.p;
        const float len = std::max(0.3f, dir.length());
        const Vec3 pos = head_.p + dir * (kFloatDistance / len);
        Vec3 toHead = head_.p - pos;
        toHead.y = 0;
        const Quat q = toHead.length() > 0.05f ? Quat::lookRotation(-toHead.normalized())
                                               : p.axisPose(anchor_).q;
        w.ppm *= len / kFloatDistance;  // keep the apparent size
        w.maximized = false;
        p.free = true;
        p.freePose = {q, pos};
    } else {
        const Pose frame{Quat::fromAxisAngle({0, 1, 0}, anchor_.yaw), anchor_.pos};
        const Vec3 l = frame.inverse().apply(p.freePose.p);
        const float dist = std::max(0.3f, std::sqrt(l.x * l.x + l.z * l.z));
        w.ppm *= dist / kDockRadius;
        p.free = false;
        p.curved = true;
        p.yaw = clampf(std::atan2(-l.x, -l.z), -kYawLimit, kYawLimit);
        p.centerY = clampf(l.y, kYMin, kYMax);
    }
    w.floating = floating;
    applyLayout(w);
    ++revision_;
}

void WindowManager::toggleMaximize(Window& w) {
    using namespace winframe;
    if (w.floating) return;
    Panel& p = *w.panel;
    if (!w.maximized) {
        w.restoreYaw = p.yaw;
        w.restoreY = p.centerY;
        w.restoreContent = w.contentPx;
        const float frameW = 2.6f, frameH = 1.28f;  // fills the desktop area
        w.contentPx = {std::round(frameW * w.ppm - 2 * kBorder),
                       std::round(frameH * w.ppm - kTitleH - kBorder)};
        p.yaw = 0;
        p.centerY = 0.08f;
        w.maximized = true;
    } else {
        w.contentPx = w.restoreContent;
        p.yaw = w.restoreYaw;
        p.centerY = w.restoreY;
        w.maximized = false;
    }
    applyLayout(w);
    ++revision_;
}

void WindowManager::resizeContent(Window* w, Vec2 contentPx) {
    if (!w || w->closing) return;
    const Vec2 mn = w->content->minSize();
    w->contentPx = {std::round(clampf(contentPx.x, mn.x, kMaxContentW)),
                    std::round(clampf(contentPx.y, mn.y, kMaxContentH))};
    applyLayout(*w);
    ++revision_;
}

bool WindowManager::isResizing(const Window* w) const {
    for (const Drag& d : drag_) {
        if (d.win == w && d.resize) return true;
    }
    return false;
}

void WindowManager::processPending(Window& w) {
    if (w.pendingClose) {
        w.pendingClose = false;
        if (!w.closing && w.content->requestClose()) {
            w.closing = true;
            if (focused_ == &w) focusTopmost();
            ++revision_;
        }
    }
    if (w.pendingMinimize) {
        w.pendingMinimize = false;
        minimize(w);
    }
    if (w.pendingMaximize) {
        w.pendingMaximize = false;
        toggleMaximize(w);
    }
    if (w.pendingFloat) {
        w.pendingFloat = false;
        setFloating(w, !w.floating);
    }
}

void WindowManager::removeClosed() {
    for (auto it = windows_.begin(); it != windows_.end();) {
        Window& w = **it;
        if (w.closing && w.appear <= 0.0f) {
            for (Drag& d : drag_) {
                if (d.win == &w) d.win = nullptr;
            }
            if (focused_ == &w) focused_ = nullptr;
            if (hooks_.onPanelDestroyed) hooks_.onPanelDestroyed(w.panel.get());
            LOGI("Window closed: %s", w.content->appId().c_str());
            w.panel->destroy(*xr_);
            it = windows_.erase(it);
            ++revision_;
        } else {
            ++it;
        }
    }
    if (!focused_) focusTopmost();
}

// ---------------------------------------------------------------------------------------------
// Per-frame update and dragging
// ---------------------------------------------------------------------------------------------

void WindowManager::update(float dt, const PointerSystem& ptrs, const Anchor& anchor,
                           const Pose& head) {
    anchor_ = anchor;
    head_ = head;
    for (auto& w : windows_) processPending(*w);
    removeClosed();
    for (const PressEvent& e : ptrs.pressEvents()) handlePress(e, ptrs);
    for (int i = 0; i < kMaxPointers; ++i) updateDrag(i, ptrs);

    for (auto& wp : windows_) {
        Window& w = *wp;
        Panel& p = *w.panel;
        w.reshapeAge += dt;
        const float target = (w.closing || w.minimized) ? 0.0f : 1.0f;
        w.appear += (target - w.appear) * (1.0f - std::exp(-dt * 16.0f));
        if (std::fabs(target - w.appear) < 0.01f) w.appear = target;
        p.opacity = w.appear * w.appear * (3.0f - 2.0f * w.appear);
        const bool visible = w.appear > 0.01f;
        if (visible && !p.visible) p.dirty = true;
        p.visible = visible;
        p.interactive = !w.closing && !w.minimized && w.appear > 0.5f;
        if (p.visible && w.content->wantsRedraw()) p.dirty = true;
        if (w.content->continuous() && p.visible) {
            w.redrawAcc += dt;
            if (w.redrawAcc >= 0.05f) {
                p.dirty = true;
                w.redrawAcc = 0;
            }
        }
    }
}

// Intersects a ray with the desktop cylinder and returns the hit as (yaw, height) in the anchor frame.
bool WindowManager::rayCylinder(const Ray& ray, float* yaw, float* y) const {
    const Pose frame{Quat::fromAxisAngle({0, 1, 0}, anchor_.yaw), anchor_.pos};
    const Pose inv = frame.inverse();
    const Vec3 o = inv.apply(ray.o), d = inv.rotate(ray.d);
    const float a = d.x * d.x + d.z * d.z;
    if (a < 1e-8f) return false;
    const float b = 2.0f * (o.x * d.x + o.z * d.z);
    const float c = o.x * o.x + o.z * o.z - kDockRadius * kDockRadius;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0) return false;
    const float t = (-b + std::sqrt(disc)) / (2.0f * a);
    if (t < 0) return false;
    const Vec3 p = o + d * t;
    *yaw = std::atan2(-p.x, -p.z);
    *y = p.y;
    return true;
}

void WindowManager::handlePress(const PressEvent& e, const PointerSystem& ptrs) {
    Window* w = windowOf(e.panel);
    if (!w || w->closing || w->minimized) return;
    if (focused_ != w) focus(w);
    const WindowLayout L = windowLayout(w->panel->pxW, w->panel->pxH, !w->floating);
    if (L.dragArea.contains(e.px)) beginDrag(*w, e.pointer, false, ptrs);
    else if (!w->maximized && L.grip.contains(e.px)) beginDrag(*w, e.pointer, true, ptrs);
}

void WindowManager::beginDrag(Window& w, int pointer, bool resize, const PointerSystem& ptrs) {
    const PointerVisual& v = ptrs.visual(pointer);
    const Ray ray{v.rayOrigin, v.rayDir};
    Drag& d = drag_[pointer];
    d = Drag{};
    d.win = &w;
    d.pointer = pointer;
    d.resize = resize;
    Panel& p = *w.panel;
    const float mm = winframe::kMargin / w.ppm;  // shadow margin in metres

    if (!w.floating) {
        float yaw = 0, y = 0;
        if (!rayCylinder(ray, &yaw, &y)) {
            d.win = nullptr;
            return;
        }
        if (!resize) {
            d.grabYaw = yaw - p.yaw;
            d.grabY = y - p.centerY;
        } else {
            d.anchorYawLeft = p.yaw + (p.widthM * 0.5f - mm) / kDockRadius;
            d.anchorTopY = p.centerY + p.heightM * 0.5f - mm;
        }
    } else if (!resize) {
        d.grabDist = std::max(0.2f, (v.hit - ray.o).length());
        d.grabLocal = p.freePose.inverse().apply(v.hit);
    } else {
        const Pose f = p.freePose;
        d.axisRight = f.right();
        d.axisUp = f.up();
        d.anchorTL = f.p + d.axisRight * (-(p.widthM * 0.5f) + mm) + d.axisUp * (p.heightM * 0.5f - mm);
    }
}

void WindowManager::updateDrag(int i, const PointerSystem& ptrs) {
    Drag& d = drag_[i];
    if (!d.win) return;
    Window& w = *d.win;
    Panel& p = *w.panel;
    if (!ptrs.isDown(i) || ptrs.captured(i) != &p) {  // released (or the pointer left the window)
        d.win = nullptr;
        return;
    }
    const PointerVisual& v = ptrs.visual(i);
    const Ray ray{v.rayOrigin, v.rayDir};
    const float mm = winframe::kMargin / w.ppm;

    if (!d.resize) {
        if (!w.floating) {
            float yaw = 0, y = 0;
            if (rayCylinder(ray, &yaw, &y)) {
                p.yaw = clampf(yaw - d.grabYaw, -kYawLimit, kYawLimit);
                p.centerY = clampf(y - d.grabY, kYMin, kYMax);
            }
        } else {
            const Vec3 target = ray.o + ray.d * d.grabDist;
            Vec3 toHead = head_.p - target;
            toHead.y = 0;
            Quat q = p.freePose.q;
            if (toHead.length() > 0.05f) q = Quat::lookRotation(-toHead.normalized());
            p.freePose.q = q;
            p.freePose.p = target - q.rotate(d.grabLocal);
        }
        return;
    }

    if (!w.floating) {
        float yaw = 0, y = 0;
        if (!rayCylinder(ray, &yaw, &y)) return;
        setContentFromFrame(w, (d.anchorYawLeft - yaw) * kDockRadius, d.anchorTopY - y);
        p.yaw = d.anchorYawLeft - (p.widthM * 0.5f - mm) / kDockRadius;
        p.centerY = d.anchorTopY - (p.heightM * 0.5f - mm);
    } else {
        float t = 0;
        if (!intersectPlane(ray, d.anchorTL, d.axisRight.cross(d.axisUp), &t)) return;
        const Vec3 delta = ray.at(t) - d.anchorTL;
        setContentFromFrame(w, delta.dot(d.axisRight), -delta.dot(d.axisUp));
        p.freePose.p = d.anchorTL + d.axisRight * (p.widthM * 0.5f - mm) - d.axisUp * (p.heightM * 0.5f - mm);
    }
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------

void WindowManager::drawWindow(Window& w, Canvas& cv, Ui& ui) {
    using namespace winframe;
    using theme::kAccent;
    using theme::kText;
    using theme::kTextDim;
    const Panel& p = *w.panel;
    const bool focused = focused_ == &w;
    const WindowLayout L = windowLayout(p.pxW, p.pxH, !w.floating);
    const Color titleBg = focused ? Color::hex(0x2b3036) : Color::hex(0x25292d);

    cv.shadow(L.frame, kRadius, focused ? 32.0f : 22.0f, Color(0, 0, 0, focused ? 0.58f : 0.38f), {0, 12});
    cv.rect(L.frame, theme::kWindow.withAlpha(0.98f), kRadius);
    cv.rect(L.title, titleBg, kRadius);
    cv.rect(Rect(L.title.x, L.title.y + kRadius, L.title.w, L.title.h - kRadius), titleBg, 0);
    cv.rect(Rect(L.title.x, L.title.bottom() - 2, L.title.w, 2),
            focused ? kAccent.withAlpha(0.85f) : Color(1, 1, 1, 0.08f), 0);
    cv.stroke(L.frame, Color(1, 1, 1, focused ? 0.18f : 0.09f), 2, kRadius);

    // Icon and title.
    const Color tint = Color::hex(w.content->color());
    Rect icon(L.title.x + 18, L.title.y + (kTitleH - 42) * 0.5f, 42, 42);
    cv.rect(icon, tint.withAlpha(focused ? 0.28f : 0.16f), 12);
    cv.glyph(w.content->glyph(), icon.inset(7), focused ? tint : kTextDim);
    const float tx = icon.right() + 16;
    cv.text(w.content->title(), {tx, L.title.y + (kTitleH - 36) * 0.5f},
            ts(30, focused ? kText : kTextDim, 500, L.dragArea.right() - tx - 8));

    // Buttons.
    auto button = [&](Rect r, const char* glyph, uint32_t id, bool danger) {
        WidgetState s = ui.widget(id, r);
        const Color bg = danger ? theme::kNegative : kAccent;
        if (s.hover > 0.01f || s.press > 0.01f) cv.rect(r, bg.withAlpha(0.30f * s.hover + 0.25f * s.press), 14);
        cv.glyph(glyph, r.inset(12), (danger && s.hover > 0.2f) ? Color(1, 1, 1, 1) : kText);
        return s.clicked;
    };
    if (button(L.dock, w.floating ? "dock" : "float", Ui::id("win.dock"), false)) w.pendingFloat = true;
    if (button(L.minimize, "minimize", Ui::id("win.min"), false)) w.pendingMinimize = true;
    if (!w.floating && button(L.maximize, w.maximized ? "restore" : "maximize", Ui::id("win.max"), false)) {
        w.pendingMaximize = true;
    }
    if (button(L.close, "close", Ui::id("win.close"), true)) w.pendingClose = true;

    // Contents.
    cv.pushClip(L.content);
    w.content->draw(cv, ui, L.content, ui.dt());
    cv.popClip();

    // Resize grip: three dots along the bottom-right corner.
    if (!w.maximized) {
        const Color g(1, 1, 1, 0.35f);
        const Vec2 c(L.frame.right() - 20, L.frame.bottom() - 20);
        cv.circle({c.x, c.y}, 3.5f, g);
        cv.circle({c.x - 13, c.y}, 3.5f, g);
        cv.circle({c.x, c.y - 13}, 3.5f, g);
    }
}

}  // namespace pxr
