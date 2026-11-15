#include "shell/shell.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "core/log.h"
#include "shell/theme.h"

namespace pxr {

namespace {

constexpr float kPpm = 800.0f;
constexpr float kShellRadius = 2.0f;

using theme::kAccent;
using theme::kText;
using theme::kTextDim;

TextStyle ts(float size, Color color = theme::kText, int weight = 400, float maxW = 0) {
    TextStyle t;
    t.size = size;
    t.color = color;
    t.weight = weight;
    t.maxWidth = maxW;
    return t;
}

void glass(Canvas& cv, Rect r, float radius, Color fill = theme::kGlass) {
    cv.shadow(r, radius, 20, theme::kShadow, {0, 8});
    cv.rect(r, fill, radius);
    cv.stroke(r, Color(1, 1, 1, 0.10f), 2, radius);
}

Color hsv(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0, g = 0, b = 0;
    int sector = static_cast<int>(h * 6.0f) % 6;
    switch (sector) {
        case 0: r = c; g = x; break;
        case 1: r = x; g = c; break;
        case 2: g = c; b = x; break;
        case 3: g = x; b = c; break;
        case 4: r = x; b = c; break;
        default: r = c; b = x; break;
    }
    return {r + m, g + m, b + m, 1.0f};
}

uint32_t hashString(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

std::string firstGlyph(const std::string& s) {
    if (s.empty()) return "?";
    size_t n = 1;
    unsigned char c = s[0];
    if ((c >> 5) == 0x6) n = 2;
    else if ((c >> 4) == 0xE) n = 3;
    else if ((c >> 3) == 0x1E) n = 4;
    return s.substr(0, std::min(n, s.size()));
}

float fitSize(Canvas& cv, const std::string& s, float size, int weight, float maxW) {
    Vec2 m = cv.measureText(s, size, weight, 0);
    return (m.x > maxW && m.x > 0) ? std::floor(size * maxW / m.x) : size;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------------------------

Panel* Shell::addPanel(XrContext& xr, const char* name, float widthM, float heightM, float xM,
                       float yM, int order, std::function<void(Canvas&, Ui&)> draw) {
    auto p = std::make_unique<Panel>();
    p->name = name;
    p->widthM = widthM;
    p->heightM = heightM;
    p->pxW = static_cast<int>(std::lround(widthM * kPpm));
    p->pxH = static_cast<int>(std::lround(heightM * kPpm));
    p->radius = kShellRadius;
    p->yaw = -xM / kShellRadius;
    p->centerY = yM;
    p->order = order;
    p->draw = std::move(draw);
    if (!p->create(xr)) return nullptr;
    panels_.push_back(std::move(p));
    return panels_.back().get();
}

bool Shell::init(XrContext& xr, ShellActions actions) {
    actions_ = std::move(actions);
    settings_.load();
    apps_.init();

    WindowManager::Hooks hooks;
    hooks.onPanelDestroyed = actions_.forgetPanel;
    wm_.init(&xr, hooks);
    linux_.init(&wm_, xr.colorFormat() == GL_SRGB8_ALPHA8);  // optional: only needed for Linux apps
    appCtx_.settings = &settings_;
    appCtx_.info = &info_;
    appCtx_.setRefreshRate = actions_.setRefreshRate;
    appCtx_.recenter = actions_.recenter;

    desktop_ = addPanel(xr, "desktop", 1.90f, 0.82f, 0.00f, 0.08f, 0,
                        [this](Canvas& cv, Ui& ui) { drawDesktop(cv, ui); });
    clock_ = addPanel(xr, "clock", 0.62f, 0.34f, -1.27f, 0.62f, 1,
                      [this](Canvas& cv, Ui& ui) { drawClock(cv, ui); });
    system_ = addPanel(xr, "system", 0.62f, 0.30f, -1.27f, 0.20f, 1,
                       [this](Canvas& cv, Ui& ui) { drawSystem(cv, ui); });
    handsPanel_ = addPanel(xr, "hands", 0.80f, 0.52f, 1.27f, 0.42f, 1,
                           [this](Canvas& cv, Ui& ui) { drawHands(cv, ui); });
    taskbar_ = addPanel(xr, "taskbar", 1.90f, 0.15f, 0.00f, -0.62f, 2,
                        [this](Canvas& cv, Ui& ui) { drawTaskbar(cv, ui); });
    kickoff_ = addPanel(xr, "kickoff", 0.88f, 1.02f, -0.51f, 0.025f, 3,
                        [this](Canvas& cv, Ui& ui) { drawKickoff(cv, ui); });
    if (!desktop_ || !clock_ || !system_ || !handsPanel_ || !taskbar_ || !kickoff_) return false;

    // Only the clock and system widgets are purely informational.
    clock_->interactive = false;
    system_->interactive = false;
    handsPanel_->interactive = false;
    kickoff_->visible = false;
    kickoff_->interactive = false;
    kickoff_->opacity = 0;

    status_ = Jni::status();
    appsRevision_ = apps_.revision();
    return true;
}

void Shell::shutdown(XrContext& xr) {
    linux_.shutdown();
    wm_.shutdown();
    for (auto& p : panels_) p->destroy(xr);
    panels_.clear();
    apps_.shutdown();
}

std::vector<Panel*> Shell::panels() {
    std::vector<Panel*> out;
    for (auto& p : panels_) out.push_back(p.get());
    for (Panel* p : wm_.panels()) out.push_back(p);
    return out;
}

void Shell::openNative(const std::string& id, bool floating) {
    std::unique_ptr<WindowContent> content = createNativeApp(id, appCtx_);
    if (!content) {
        LOGW("Unknown native app: %s", id.c_str());
        return;
    }
    wm_.open(std::move(content), floating);
}

void Shell::launch(const AppInfo& app) {
    if (app.native) {
        openNative(app.pkg);
    } else if (app.linuxApp) {
        if (linux_.launch(app.activity)) apps_.noteLaunched(app.pkg);
        else LOGW("Could not start %s: is the Linux environment installed?", app.label.c_str());
    } else {
        apps_.launch(app);
    }
}

Panel* Shell::panelByName(const std::string& name) {
    for (auto& p : panels_) {
        if (p->name == name) return p.get();
    }
    return nullptr;
}

void Shell::markAllDirty() {
    for (Panel* p : panels()) p->dirty = true;
}

void Shell::setKickoffOpen(bool open) {
    if (open == kickoffOpen_) return;
    kickoffOpen_ = open;
    if (open) {
        kickoff_->visible = true;
        Jni::playSound(kSoundOpen);
    } else {
        Jni::playSound(kSoundClose);
    }
    kickoff_->dirty = true;
    taskbar_->dirty = true;
}

void Shell::setKickoffTab(int tab) {
    tab_ = tab;
    kickoff_->dirty = true;
}

// ---------------------------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------------------------

void Shell::update(float dt, const ShellInfo& info, const HandTracking& hands,
                   const PointerSystem& pointers, const Anchor& anchor, const Pose& head) {
    info_ = info;
    hands_ = &hands;
    pointers_ = &pointers;
    appCtx_.hands = &hands;
    appCtx_.pointers = &pointers;

    if (apps_.pumpUploads(2) || apps_.revision() != appsRevision_) {
        appsRevision_ = apps_.revision();
        desktop_->dirty = true;
        taskbar_->dirty = true;
        if (kickoff_->visible) kickoff_->dirty = true;
    }

    statusTimer_ -= dt;
    if (statusTimer_ <= 0) {
        statusTimer_ = 5.0f;
        SystemStatus s = Jni::status();
        if (s.time != status_.time || s.date != status_.date) {
            clock_->dirty = true;
            taskbar_->dirty = true;
        }
        if (s.battery != status_.battery || s.charging != status_.charging || s.wifi != status_.wifi) {
            system_->dirty = true;
            taskbar_->dirty = true;
        }
        status_ = s;
    }

    // The hand monitor only redraws when something it shows has visibly changed.
    uint32_t sig = 17;
    for (int i = 0; i < 2; ++i) {
        const HandState& hs = hands.hand(i);
        sig = sig * 31 + (hs.active ? 1u : 0u);
        sig = sig * 31 + static_cast<uint32_t>(std::lround(saturate(hs.pinch[0]) * 10.0f));
        sig = sig * 31 + static_cast<uint32_t>(pointers.visual(i).mode);
        sig = sig * 31 + static_cast<uint32_t>(hs.activeSeconds);
    }
    handAcc_ += dt;
    if (sig != handSig_ && handAcc_ > 0.06f) {
        handSig_ = sig;
        handAcc_ = 0;
        handsPanel_->dirty = true;
        taskbar_->dirty = true;
    }

    // Kickoff open/close animation (pose offset + layer fade, no redraw needed).
    const float target = kickoffOpen_ ? 1.0f : 0.0f;
    kickoffAnim_ += (target - kickoffAnim_) * (1.0f - std::exp(-dt * 14.0f));
    if (std::fabs(target - kickoffAnim_) < 0.01f) kickoffAnim_ = target;
    const float e = kickoffAnim_ * kickoffAnim_ * (3.0f - 2.0f * kickoffAnim_);
    kickoff_->opacity = e;
    kickoff_->offsetY = (1.0f - e) * -0.04f;
    kickoff_->visible = kickoffAnim_ > 0.01f;
    kickoff_->interactive = kickoffOpen_ && kickoffAnim_ > 0.5f;

    linux_.update(dt, pointers.pressEvents());
    wm_.update(dt, pointers, anchor, head);
    if (wm_.revision() != wmRevision_) {
        wmRevision_ = wm_.revision();
        taskbar_->dirty = true;
    }

    // Light dismiss: pressing empty space, the desktop or a window closes the menu.
    for (const PressEvent& pe : pointers.pressEvents()) {
        if (!kickoffOpen_) continue;
        if (pe.panel == nullptr || pe.panel == desktop_ || pe.panel == clock_ ||
            pe.panel == system_ || pe.panel == handsPanel_ || wm_.windowOf(pe.panel)) {
            setKickoffOpen(false);
        }
    }
    playSounds();
}

void Shell::playSounds() {
    bool press = false, release = false, hover = false;
    for (Panel* p : panels()) {
        for (int s : p->ui.sounds()) {
            if (s == kSoundPress) press = true;
            else if (s == kSoundRelease) release = true;
            else if (s == kSoundHover) hover = true;
        }
        p->ui.sounds().clear();
    }
    if (!settings_.sounds) return;
    const int id = press ? kSoundPress : (release ? kSoundRelease : (hover ? kSoundHover : -1));
    if (id < 0) return;
    const auto t0 = std::chrono::steady_clock::now();
    Jni::playSound(id);
    const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms > 15.0f) LOGW("playSound(%d) blocked the render thread for %.0f ms", id, ms);
}

void Shell::renderPanels(XrContext& xr, Canvas& canvas, float dt) {
    for (Panel* p : panels()) p->render(xr, canvas, dt);
}

void Shell::collectLayers(const XrContext& xr, const Anchor& anchor, Vec3 eye,
                          std::vector<const XrCompositionLayerBaseHeader*>& out) {
    anchor_ = anchor;
    std::vector<Panel*> sorted = panels();
    std::stable_sort(sorted.begin(), sorted.end(), [&](Panel* a, Panel* b) {
        float da = distance(a->center(anchor), eye);
        float db = distance(b->center(anchor), eye);
        if (std::fabs(da - db) > 0.02f) return da > db;  // far first
        return a->order < b->order;
    });
    for (Panel* p : sorted) {
        if (const XrCompositionLayerBaseHeader* l = p->buildLayer(xr, anchor)) out.push_back(l);
    }
}

// ---------------------------------------------------------------------------------------------
// Shared drawing helpers
// ---------------------------------------------------------------------------------------------

void Shell::drawAppIcon(Canvas& cv, const AppInfo& app, Rect r) {
    if (app.native || app.linuxApp) {  // Plasma XR and Linux apps: a tinted tile with a glyph
        const Color tint = Color::hex(app.color);
        cv.rectGradient(r, mix(tint, Color(1, 1, 1, 1), 0.18f), mix(tint, Color(0, 0, 0, 1), 0.35f),
                        r.w * 0.22f);
        cv.glyph(app.glyph, r.inset(r.w * 0.2f), Color(1, 1, 1, 0.96f));
        return;
    }
    if (const Texture* t = apps_.icon(app)) {
        cv.image(*t, r, Color(1, 1, 1, 1), r.w * 0.22f);
        return;
    }
    Color base = hsv((hashString(app.pkg) % 360) / 360.0f, 0.42f, 0.52f);
    cv.rect(r, base, r.w * 0.22f);
    cv.textIn(firstGlyph(app.label), r, ts(r.w * 0.5f, Color(1, 1, 1, 0.92f), 500));
}

WidgetState Shell::appTile(Canvas& cv, Ui& ui, const AppInfo& app, Rect r, uint32_t id,
                           float iconPx, float labelSize, bool labelShadow) {
    WidgetState w = ui.widget(id, r);
    if (w.hover > 0.005f || w.press > 0.005f) {
        cv.rect(r, kAccent.withAlpha(0.12f * w.hover + 0.12f * w.press), 20);
        cv.stroke(r, kAccent.withAlpha(0.75f * w.hover), 2.5f, 20);
    }
    const float s = 1.0f + 0.06f * w.hover - 0.05f * w.press;
    const float ip = iconPx * s;
    Rect icon(r.center().x - ip * 0.5f, r.y + 14 + (iconPx - ip) * 0.5f, ip, ip);
    drawAppIcon(cv, app, icon);
    if (apps_.isRecent(app.pkg)) cv.circle({icon.x + 8, icon.y + 8}, 7, kAccent);
    Rect label(r.x + 8, r.y + 14 + iconPx + 12, r.w - 16, labelSize + 10);
    if (labelShadow) {
        cv.textIn(app.label, Rect(label.x + 2, label.y + 2, label.w, label.h),
                  ts(labelSize, Color(0, 0, 0, 0.85f), 400), HAlign::Center, VAlign::Top);
    }
    cv.textIn(app.label, label, ts(labelSize, kText, 400), HAlign::Center, VAlign::Top);
    return w;
}

// ---------------------------------------------------------------------------------------------
// Desktop icons
// ---------------------------------------------------------------------------------------------

void Shell::drawDesktop(Canvas& cv, Ui& ui) {
    const float W = static_cast<float>(desktop_->pxW), H = static_cast<float>(desktop_->pxH);
    std::vector<const AppInfo*> pinned = apps_.pinnedApps();
    if (pinned.empty()) {
        cv.textIn("No apps pinned yet. Open the menu and tap the star on an app.",
                  Rect(0, 0, W, H), ts(theme::kFontBody, kTextDim), HAlign::Center);
        return;
    }
    const int cols = 6;
    const float cellW = 236, cellH = 232;
    const int count = static_cast<int>(std::min<size_t>(pinned.size(), 12));
    const int rows = (count + cols - 1) / cols;
    const float gridH = rows * cellH;
    const float y0 = (H - gridH) * 0.5f;
    for (int i = 0; i < count; ++i) {
        const int r = i / cols, c = i % cols;
        const int inRow = std::min(cols, count - r * cols);
        const float x0 = (W - inRow * cellW) * 0.5f;
        Rect tile(x0 + c * cellW + 6, y0 + r * cellH + 4, cellW - 12, cellH - 8);
        WidgetState w = appTile(cv, ui, *pinned[i], tile, Ui::id("desk.app", i), 116, 30, true);
        if (w.clicked) launch(*pinned[i]);
    }
}

// ---------------------------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------------------------

void Shell::drawClock(Canvas& cv, Ui&) {
    Rect card(16, 16, clock_->pxW - 32.0f, clock_->pxH - 32.0f);
    glass(cv, card, theme::kRadius);
    const std::string time = status_.time.empty() ? "--:--" : status_.time;
    const float size = fitSize(cv, time, theme::kFontClock, 300, card.w - 48);
    cv.textIn(time, Rect(card.x, card.y + 14, card.w, 150), ts(size, kText, 300));
    cv.rect(Rect(card.center().x - 36, card.y + 172, 72, 5), kAccent, 2.5f);
    cv.textIn(status_.date, Rect(card.x, card.y + 182, card.w, 56),
              ts(theme::kFontBody + 4, kTextDim, 400));
}

void Shell::drawSystem(Canvas& cv, Ui&) {
    Rect card(16, 16, system_->pxW - 32.0f, system_->pxH - 32.0f);
    glass(cv, card, theme::kRadius);
    const float lx = card.x + 28, rx = card.right() - 28;
    float y = card.y + 24;
    const float row = 52;

    // Battery
    cv.text("Battery", {lx, y + 4}, ts(theme::kFontSmall, kTextDim));
    {
        const int pct = std::max(0, status_.battery);
        Rect bar(rx - 190, y + 10, 130, 18);
        cv.rect(bar, Color(1, 1, 1, 0.12f), 9);
        Color c = status_.charging ? kAccent : (pct <= 15 ? theme::kNegative : (pct <= 30 ? theme::kNeutral : theme::kPositive));
        if (status_.battery >= 0) cv.rect(Rect(bar.x, bar.y, std::max(18.0f, bar.w * pct / 100.0f), bar.h), c, 9);
        cv.textIn(status_.battery >= 0 ? std::to_string(pct) + "%" : "--", Rect(rx - 54, y, 54, 40),
                  ts(theme::kFontSmall, kText, 500), HAlign::Right);
    }
    y += row;
    cv.text("Wi-Fi", {lx, y + 4}, ts(theme::kFontSmall, kTextDim));
    cv.textIn(status_.wifi ? "Connected" : "Offline", Rect(rx - 220, y, 220, 40),
              ts(theme::kFontSmall, status_.wifi ? kText : kTextDim, 500), HAlign::Right);
    y += row;
    cv.text("Display", {lx, y + 4}, ts(theme::kFontSmall, kTextDim));
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d Hz", static_cast<int>(info_.refreshRate + 0.5f));
        cv.textIn(info_.refreshRate > 0 ? buf : "--", Rect(rx - 220, y, 220, 40),
                  ts(theme::kFontSmall, kText, 500), HAlign::Right);
    }
}

void Shell::drawHands(Canvas& cv, Ui&) {
    Rect card(16, 16, handsPanel_->pxW - 32.0f, handsPanel_->pxH - 32.0f);
    glass(cv, card, theme::kRadius);
    const bool anyActive = hands_ && (hands_->hand(0).active || hands_->hand(1).active);

    cv.glyph("hand", Rect(card.x + 24, card.y + 20, 54, 54), kAccent);
    cv.text("Hand Tracking", {card.x + 94, card.y + 26}, ts(theme::kFontLarge - 6, kText, 500));
    if (!info_.handsAvailable) {
        cv.textIn("Hand tracking is not available on this device.",
                  Rect(card.x + 24, card.y + 110, card.w - 48, 120), ts(theme::kFontSmall, kTextDim),
                  HAlign::Left, VAlign::Top);
        return;
    }

    const float gap = 20;
    const float colW = (card.w - 24 * 2 - gap) * 0.5f;
    for (int i = 0; i < 2; ++i) {
        Rect col(card.x + 24 + i * (colW + gap), card.y + 96, colW, card.h - 96 - 76);
        cv.rect(col, theme::kView.withAlpha(0.75f), 18);
        const HandState& hs = hands_->hand(i);
        const bool ctl = info_.controller[i];
        const bool act = hs.active && !ctl;
        cv.circle({col.x + 30, col.y + 36}, 9, ctl ? kAccent : (act ? theme::kPositive : Color(1, 1, 1, 0.25f)));
        cv.text(i == kLeft ? "Left" : "Right", {col.x + 52, col.y + 18}, ts(32, kText, 500));
        cv.text(ctl ? "Controller" : (act ? "Tracked" : "Not visible"), {col.x + 24, col.y + 62},
                ts(26, ctl ? kAccent : (act ? theme::kPositive : kTextDim)));

        const float pinch = act ? saturate(hs.pinch[0]) : 0.0f;
        cv.text("Pinch", {col.x + 24, col.y + 104}, ts(24, kTextDim));
        Rect bar(col.x + 24, col.y + 138, col.w - 48, 16);
        cv.rect(bar, Color(1, 1, 1, 0.12f), 8);
        if (pinch > 0.02f) {
            cv.rect(Rect(bar.x, bar.y, std::max(16.0f, bar.w * pinch), bar.h),
                    hs.indexPinching ? Color(1, 1, 1, 1) : kAccent, 8);
        }
        const PointerVisual::Mode mode = pointers_->visual(i).mode;
        const char* modeName = mode == PointerVisual::kRay ? "Ray" : (mode == PointerVisual::kPoke ? "Poke" : "Idle");
        cv.text(std::string("Mode: ") + modeName, {col.x + 24, col.y + 170}, ts(24, kTextDim));
    }
    const bool anyController = info_.controller[0] || info_.controller[1];
    cv.textIn(anyController ? "Controllers are on: put them down to use your hands"
                            : (anyActive ? "Pinch to click, or poke a panel to touch it"
                                         : "Hold your hands up in front of the headset"),
              Rect(card.x + 24, card.bottom() - 62, card.w - 48, 44), ts(24, kTextDim),
              HAlign::Center);
}

// ---------------------------------------------------------------------------------------------
// Taskbar
// ---------------------------------------------------------------------------------------------

void Shell::drawTaskbar(Canvas& cv, Ui& ui) {
    const Rect bar(10, 10, taskbar_->pxW - 20.0f, taskbar_->pxH - 20.0f);
    glass(cv, bar, 30);
    const float cy = bar.center().y;

    // System tray, laid out from the right edge.
    float tx = bar.right() - 18;
    float trayLeft;
    {
        const float w = 170;
        Rect r(tx - w, bar.y + 8, w, bar.h - 16);
        tx -= w + 6;
        Vec2 tsz = cv.measureText(status_.time.empty() ? "--:--" : status_.time, 38, 500);
        float size = tsz.x > w - 8 ? 32 : 38;
        cv.textIn(status_.time.empty() ? "--:--" : status_.time, Rect(r.x, r.y + 4, r.w, 46),
                  ts(size, kText, 500), HAlign::Center, VAlign::Middle);
        cv.textIn(status_.date, Rect(r.x, r.y + 50, r.w, 30), ts(theme::kFontTiny, kTextDim),
                  HAlign::Center, VAlign::Middle);
    }
    {  // battery
        const float w = 124;
        Rect r(tx - w, cy - 30, w, 60);
        tx -= w + 6;
        const int pct = std::max(0, status_.battery);
        Color c = status_.charging ? kAccent : (pct <= 15 ? theme::kNegative : (pct <= 30 ? theme::kNeutral : kText));
        Rect body(r.x + 8, cy - 13, 46, 26);
        cv.stroke(body, kText.withAlpha(0.85f), 3, 7);
        if (status_.battery >= 0) cv.rect(Rect(body.x + 5, body.y + 5, std::max(3.0f, (body.w - 10) * pct / 100.0f), body.h - 10), c, 3);
        cv.rect(Rect(body.right() + 2, cy - 6, 4, 12), kText.withAlpha(0.85f), 2);
        cv.textIn(status_.battery >= 0 ? std::to_string(pct) + "%" : "--",
                  Rect(body.right() + 12, cy - 20, 56, 40), ts(theme::kFontSmall, kText, 500),
                  HAlign::Left);
    }
    {  // wifi
        const float w = 56;
        Rect r(tx - w, cy - 28, w, 56);
        tx -= w + 4;
        cv.glyph(status_.wifi ? "wifi" : "wifi_off", r.inset(8), status_.wifi ? kText : kTextDim);
    }
    if (info_.passthroughAvailable) {  // passthrough toggle
        const float w = 60;
        Rect r(tx - w, cy - 32, w, 64);
        tx -= w + 4;
        WidgetState wd = ui.widget(Ui::id("tb.pt"), r);
        const bool on = actions_.passthroughOn && actions_.passthroughOn();
        if (wd.hover > 0.01f || on) cv.rect(r, kAccent.withAlpha(on ? 0.28f : 0.18f * wd.hover), 18);
        cv.glyph("eye", r.inset(10), on ? kAccent : kText);
        if (wd.clicked && actions_.setPassthrough) {
            actions_.setPassthrough(!on);
            kickoff_->dirty = true;
        }
    }
    {  // hand status
        const float w = 92;
        Rect r(tx - w, cy - 28, w, 56);
        tx -= w + 4;
        for (int i = 0; i < 2; ++i) {
            const bool act = hands_ && hands_->hand(i).active;
            cv.glyph("hand", Rect(r.x + i * 46, r.y + 6, 44, 44), act ? kAccent : Color(1, 1, 1, 0.28f));
        }
    }
    trayLeft = tx;

    // Kickoff button.
    float x = bar.x + 14;
    {
        Rect b(x, cy - 42, 84, 84);
        WidgetState w = ui.widget(Ui::id("tb.kickoff"), b);
        const float a = std::max(w.hover * 0.22f, kickoffOpen_ ? 0.30f : 0.0f) + w.press * 0.15f;
        if (a > 0.001f) cv.rect(b, kAccent.withAlpha(a), 24);
        if (kickoffOpen_ || w.hover > 0.01f) {
            cv.stroke(b, kAccent.withAlpha(kickoffOpen_ ? 0.9f : 0.6f * w.hover), 2.5f, 24);
        }
        cv.glyph("plasma", b.inset(14), kickoffOpen_ ? kAccent : kText);
        if (w.clicked) setKickoffOpen(!kickoffOpen_);
        x += 84 + 16;
    }
    cv.rect(Rect(x, bar.y + 24, 2, bar.h - 48), theme::kSeparator, 1);
    x += 18;

    // Pinned apps.
    std::vector<const AppInfo*> pinned = apps_.pinnedApps();
    const float cell = 92;
    for (size_t i = 0; i < pinned.size(); ++i) {
        if (x + cell > trayLeft - 8) break;
        const AppInfo& a = *pinned[i];
        Rect r(x, cy - cell * 0.5f, cell, cell);
        WidgetState w = ui.widget(Ui::id("tb.app", static_cast<int>(i)), r);
        if (w.hover > 0.005f || w.press > 0.005f) {
            cv.rect(r, kAccent.withAlpha(0.14f * w.hover + 0.12f * w.press), 20);
            cv.stroke(r, kAccent.withAlpha(0.7f * w.hover), 2.5f, 20);
        }
        const float s = 1.0f + 0.07f * w.hover - 0.05f * w.press;
        const float ip = 62 * s;
        drawAppIcon(cv, a, Rect(r.center().x - ip * 0.5f, r.center().y - ip * 0.5f - 3, ip, ip));
        if (apps_.isRecent(a.pkg)) cv.rect(Rect(r.center().x - 15, r.bottom() - 9, 30, 5), kAccent, 2.5f);
        if (w.clicked) launch(a);
        x += cell + 2;
    }

    // Open windows, after the launchers.
    const auto& wins = wm_.windows();
    if (!wins.empty() && x + cell < trayLeft - 8) {
        cv.rect(Rect(x + 2, bar.y + 24, 2, bar.h - 48), theme::kSeparator, 1);
        x += 18;
    }
    for (const auto& wp : wins) {
        if (x + cell > trayLeft - 8) break;
        Window& win = *wp;
        if (win.closing) continue;
        Rect r(x, cy - cell * 0.5f, cell, cell);
        WidgetState w = ui.widget(Ui::id("tb.win", win.id), r);
        const bool focused = wm_.focused() == &win;
        if (w.hover > 0.005f || w.press > 0.005f || focused) {
            cv.rect(r, kAccent.withAlpha(0.14f * w.hover + 0.12f * w.press + (focused ? 0.10f : 0.0f)), 20);
            cv.stroke(r, kAccent.withAlpha(std::max(0.7f * w.hover, focused ? 0.45f : 0.0f)), 2.5f, 20);
        }
        const float s = 1.0f + 0.07f * w.hover - 0.05f * w.press;
        const float ip = 62 * s;
        const Rect icon(r.center().x - ip * 0.5f, r.center().y - ip * 0.5f - 3, ip, ip);
        const Color tint = Color::hex(win.content->color());
        const float a = win.minimized ? 0.45f : 1.0f;
        cv.rectGradient(icon, mix(tint, Color(1, 1, 1, 1), 0.18f).scaledAlpha(a),
                        mix(tint, Color(0, 0, 0, 1), 0.35f).scaledAlpha(a), ip * 0.22f);
        cv.glyph(win.content->glyph(), icon.inset(ip * 0.2f), Color(1, 1, 1, 0.96f * a));
        cv.rect(Rect(r.center().x - (focused ? 17 : 8), r.bottom() - 9, focused ? 34 : 16, 5),
                focused ? kAccent : Color(1, 1, 1, 0.45f), 2.5f);
        if (w.clicked) wm_.taskbarClick(&win);
        x += cell + 2;
    }
}

// ---------------------------------------------------------------------------------------------
// Kickoff
// ---------------------------------------------------------------------------------------------

void Shell::drawKickoff(Canvas& cv, Ui& ui) {
    const float W = static_cast<float>(kickoff_->pxW), H = static_cast<float>(kickoff_->pxH);
    const Rect body(14, 14, W - 28, H - 28);
    cv.shadow(body, 28, 24, theme::kShadow, {0, 10});
    cv.rect(body, theme::kWindow.withAlpha(0.97f), 28);
    cv.stroke(body, Color(1, 1, 1, 0.10f), 2, 28);

    const float px = body.x + 26, pw = body.w - 52;

    // Header: user + power.
    const float hy = body.y + 24;
    cv.circle({px + 34, hy + 34}, 34, kAccent.withAlpha(0.22f));
    cv.glyph("user", Rect(px + 10, hy + 10, 48, 48), kAccent);
    cv.text("Quest 2", {px + 84, hy + 2}, ts(36, kText, 500));
    cv.text("Plasma XR", {px + 84, hy + 44}, ts(theme::kFontTiny + 2, kTextDim));
    {
        Rect pb(px + pw - 68, hy, 68, 68);
        WidgetState w = ui.widget(Ui::id("kick.power"), pb);
        if (w.hover > 0.01f || w.press > 0.01f) cv.rect(pb, kAccent.withAlpha(0.2f * w.hover + 0.15f * w.press), 20);
        cv.glyph("power", pb.inset(14), w.hover > 0.2f ? kAccent : kText);
        if (w.clicked) setKickoffTab(kTabLeave);
    }

    // Search field (opens the full application list).
    Rect sf(px, hy + 86, pw, 70);
    {
        WidgetState w = ui.widget(Ui::id("kick.search"), sf);
        cv.rect(sf, theme::kView, 18);
        cv.stroke(sf, w.hover > 0.01f ? kAccent.withAlpha(0.3f + 0.7f * w.hover) : theme::kSeparator, 2, 18);
        cv.glyph("search", Rect(sf.x + 20, sf.y + 17, 36, 36), kTextDim);
        cv.text("Search applications...", {sf.x + 74, sf.y + 17}, ts(theme::kFontBody - 2, kTextDim));
        if (w.clicked) setKickoffTab(kTabApps);
    }

    // Tabs.
    Rect tabs(px, sf.bottom() + 16, pw, 62);
    {
        const char* names[3] = {"Favorites", "Applications", "Leave"};
        const char* glyphs[3] = {"star", "apps", "power"};
        const float tw = pw / 3.0f;
        for (int i = 0; i < 3; ++i) {
            Rect t(tabs.x + i * tw, tabs.y, tw, tabs.h);
            WidgetState w = ui.widget(Ui::id("kick.tab", i), t);
            const bool sel = tab_ == i;
            if (w.hover > 0.01f && !sel) cv.rect(t, Color(1, 1, 1, 0.07f * w.hover), 14);
            const Color c = sel ? kAccent : kText;
            Vec2 sz = cv.measureText(names[i], theme::kFontSmall + 2, sel ? 500 : 400);
            const float total = 34 + 10 + sz.x;
            const float gx = t.center().x - total * 0.5f;
            cv.glyph(glyphs[i], Rect(gx, t.center().y - 17, 34, 34), c);
            cv.text(names[i], {gx + 44, t.center().y - sz.y * 0.5f}, ts(theme::kFontSmall + 2, c, sel ? 500 : 400));
            if (sel) cv.rect(Rect(t.x + 14, t.bottom() - 4, t.w - 28, 4), kAccent, 2);
            if (w.clicked) setKickoffTab(i);
        }
        cv.rect(Rect(tabs.x, tabs.bottom() - 1, pw, 2), theme::kSeparator, 1);
    }

    Rect content(px - 6, tabs.bottom() + 14, pw + 12, body.bottom() - 62 - (tabs.bottom() + 14));
    if (tab_ == kTabFavorites) {
        std::vector<const AppInfo*> pinned = apps_.pinnedApps();
        if (pinned.empty()) {
            cv.textIn("Pin apps with the star on the Applications tab.", content,
                      ts(theme::kFontSmall, kTextDim), HAlign::Center, VAlign::Middle);
        } else {
            drawKickoffGrid(cv, ui, content, pinned, Ui::id("kick.scroll.fav"), 1000);
        }
    } else if (tab_ == kTabApps) {
        std::vector<const AppInfo*> all;
        for (const AppInfo& a : apps_.apps()) all.push_back(&a);
        drawKickoffGrid(cv, ui, content, all, Ui::id("kick.scroll.all"), 2000);
    } else {
        drawKickoffLeave(cv, ui, content);
    }

    // Footer.
    cv.rect(Rect(px, body.bottom() - 58, pw, 2), theme::kSeparator, 1);
    cv.text(std::to_string(apps_.apps().size()) + " applications", {px, body.bottom() - 42},
            ts(theme::kFontTiny, kTextDim));
    cv.textIn("Breeze Dark", Rect(px, body.bottom() - 46, pw, 30), ts(theme::kFontTiny, kTextDim),
              HAlign::Right);
}

void Shell::drawKickoffGrid(Canvas& cv, Ui& ui, Rect area, const std::vector<const AppInfo*>& list,
                            uint32_t scrollId, int idBase) {
    const int cols = 3;
    const float gap = 10, tileH = 196;
    const float tileW = (area.w - gap * (cols - 1)) / cols;
    const int rows = (static_cast<int>(list.size()) + cols - 1) / cols;
    const float contentH = rows * tileH + std::max(0, rows - 1) * gap + 8;
    const float off = ui.scrollBegin(scrollId, area, contentH);
    for (size_t i = 0; i < list.size(); ++i) {
        const int r = static_cast<int>(i) / cols, c = static_cast<int>(i) % cols;
        Rect t(area.x + c * (tileW + gap), area.y + r * (tileH + gap) - off, tileW, tileH);
        if (t.bottom() < area.y || t.y > area.bottom()) continue;
        const AppInfo& a = *list[i];
        WidgetState w = appTile(cv, ui, a, t, Ui::id("kick.app", idBase + static_cast<int>(i)), 96,
                                theme::kFontSmall - 2, false);
        bool consumed = false;
        const bool pinned = apps_.isPinned(a.pkg);
        if (w.hover > 0.3f || pinned) {
            Rect sb(t.right() - 46, t.y + 6, 40, 40);
            WidgetState sw = ui.widget(Ui::id("kick.pin", idBase + static_cast<int>(i)), sb);
            if (sw.hover > 0.01f) cv.rect(sb, Color(1, 1, 1, 0.14f * sw.hover), 12);
            cv.glyph("star", sb.inset(7), pinned ? Color::hex(0xfdbc4b) : Color(1, 1, 1, 0.45f + 0.4f * sw.hover));
            if (sw.clicked) {
                apps_.togglePin(a.pkg);
                consumed = true;
            }
        }
        if (w.clicked && !consumed) {
            launch(a);
            setKickoffOpen(false);
        }
    }
    ui.scrollEnd();
    if (contentH > area.h) {
        const float th = std::max(40.0f, area.h * area.h / contentH);
        const float ty = area.y + (area.h - th) * (off / (contentH - area.h));
        cv.rect(Rect(area.right() - 7, ty, 5, th), Color(1, 1, 1, 0.28f), 2.5f);
    }
}

void Shell::drawKickoffLeave(Canvas& cv, Ui& ui, Rect area) {
    struct Row {
        const char* glyph;
        std::string title;
        const char* subtitle;
        int action;
    };
    std::vector<Row> rows;
    rows.push_back({"recenter", "Recenter view", "Bring the desktop in front of you", 0});
    if (info_.passthroughAvailable) {
        const bool on = actions_.passthroughOn && actions_.passthroughOn();
        rows.push_back({"eye", on ? "Passthrough: on" : "Passthrough: off", "Show the room behind the desktop", 1});
    }
    rows.push_back({"exit", "Meta Home", "Return to the system Home", 2});
    rows.push_back({"power", "Quit Plasma XR", "Close the shell", 3});

    const float rowH = 98, gap = 10;
    for (size_t i = 0; i < rows.size(); ++i) {
        Rect r(area.x, area.y + i * (rowH + gap), area.w, rowH);
        WidgetState w = ui.widget(Ui::id("kick.leave", static_cast<int>(i)), r);
        cv.rect(r, Color(1, 1, 1, 0.05f + 0.07f * w.hover + 0.05f * w.press), 20);
        if (w.hover > 0.01f) cv.stroke(r, kAccent.withAlpha(0.7f * w.hover), 2.5f, 20);
        cv.glyph(rows[i].glyph, Rect(r.x + 24, r.center().y - 24, 48, 48), kAccent);
        cv.text(rows[i].title, {r.x + 94, r.y + 14}, ts(theme::kFontBody, kText, 500));
        cv.text(rows[i].subtitle, {r.x + 94, r.y + 56}, ts(theme::kFontTiny, kTextDim));
        if (w.clicked) {
            switch (rows[i].action) {
                case 0:
                    if (actions_.recenter) actions_.recenter();
                    setKickoffOpen(false);
                    break;
                case 1:
                    if (actions_.setPassthrough && actions_.passthroughOn) {
                        actions_.setPassthrough(!actions_.passthroughOn());
                    }
                    kickoff_->dirty = true;
                    taskbar_->dirty = true;
                    break;
                case 2:
                    if (actions_.openHome) actions_.openHome();
                    break;
                case 3:
                    if (actions_.exitApp) actions_.exitApp();
                    break;
            }
        }
    }
}

}  // namespace pxr
