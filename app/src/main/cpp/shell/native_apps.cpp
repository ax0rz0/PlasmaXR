#include "shell/native_apps.h"

#include <cstdio>

#include "input/hands.h"
#include "input/pointers.h"
#include "shell/settings.h"
#include "shell/shell.h"
#include "shell/theme.h"
#include "shell/widgets.h"

namespace pxr {

namespace {

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

std::string fmt(const char* f, float v) {
    char buf[48];
    snprintf(buf, sizeof(buf), f, v);
    return buf;
}

// ---------------------------------------------------------------------------------------------
// System Settings
// ---------------------------------------------------------------------------------------------

class SettingsApp : public WindowContent {
public:
    explicit SettingsApp(AppContext ctx) : ctx_(std::move(ctx)) {}

    std::string appId() const override { return "plasmaxr.settings"; }
    std::string title() const override { return "System Settings"; }
    const char* glyph() const override { return "settings"; }
    unsigned color() const override { return 0x3daee9; }
    Vec2 defaultSize() const override { return {1000, 700}; }
    Vec2 minSize() const override { return {800, 520}; }

    void draw(Canvas& cv, Ui& ui, Rect area, float) override {
        const float side = 270;
        Rect sidebar(area.x, area.y, side, area.h);
        Rect page(area.x + side, area.y, area.w - side, area.h);
        cv.rect(sidebar, theme::kView.withAlpha(0.6f), 0);
        cv.rect(Rect(sidebar.right() - 2, sidebar.y, 2, sidebar.h), theme::kSeparator, 0);

        static const char* names[3] = {"Hand Tracking", "Display", "About"};
        static const char* glyphs[3] = {"hand", "eye", "plasma"};
        for (int i = 0; i < 3; ++i) {
            Rect r(sidebar.x + 14, sidebar.y + 18 + i * 82, side - 28, 70);
            WidgetState w = ui.widget(Ui::id("set.nav", i), r);
            const bool sel = page_ == i;
            if (sel) cv.rect(r, kAccent.withAlpha(0.22f), 16);
            else if (w.hover > 0.01f) cv.rect(r, Color(1, 1, 1, 0.07f * w.hover + 0.05f * w.press), 16);
            if (sel) cv.rect(Rect(r.x, r.y + 14, 5, r.h - 28), kAccent, 2.5f);
            cv.glyph(glyphs[i], Rect(r.x + 22, r.center().y - 17, 34, 34), sel ? kAccent : kText);
            cv.text(names[i], {r.x + 72, r.center().y - 19}, ts(theme::kFontBody - 2, sel ? kText : kText, sel ? 500 : 400));
            if (w.clicked) page_ = i;
        }

        cv.text(names[page_], {page.x + 40, page.y + 22}, ts(theme::kFontLarge, kText, 500));
        Rect view(page.x, page.y + 92, page.w, page.h - 92);
        const float gap = 14;
        const float rowStep = widgets::kRowHeight + gap;
        int rows = page_ == 0 ? 5 : (page_ == 1 ? 3 : 7);
        const float contentH = rows * rowStep + 30 + (page_ == 2 ? 70 : 0);
        const float off = ui.scrollBegin(Ui::id("set.scroll", page_), view, contentH);
        float y = view.y + 6 - off;
        auto nextRow = [&](float h = widgets::kRowHeight) {
            Rect r(page.x + 40, y, page.w - 80, h);
            y += h + gap;
            return r;
        };
        if (page_ == 0) pageHands(cv, ui, nextRow);
        else if (page_ == 1) pageDisplay(cv, ui, nextRow);
        else pageAbout(cv, nextRow);
        ui.scrollEnd();
        if (contentH > view.h) {
            const float th = std::max(40.0f, view.h * view.h / contentH);
            const float ty = view.y + (view.h - th) * (off / (contentH - view.h));
            cv.rect(Rect(view.right() - 10, ty, 5, th), Color(1, 1, 1, 0.28f), 2.5f);
        }
    }

private:
    template <typename NextRow>
    void pageHands(Canvas& cv, Ui& ui, NextRow nextRow) {
        Settings& s = *ctx_.settings;
        bool changed = false;
        changed |= widgets::toggleRow(cv, ui, Ui::id("set.poke"), nextRow(), "Direct touch",
                                      "Poke panels with your fingertip", s.poke);
        float sm = s.smoothing;
        if (widgets::sliderRow(cv, ui, Ui::id("set.smooth"), nextRow(), "Pointer smoothing",
                               "Steadier pointer, a little more lag", sm, 0.5f, 2.0f, fmt("%.2fx", sm))) {
            s.smoothing = sm;
            changed = true;
        }
        changed |= widgets::toggleRow(cv, ui, Ui::id("set.mesh"), nextRow(), "Hand model",
                                      "Draw a translucent hand", s.handMesh);
        changed |= widgets::toggleRow(cv, ui, Ui::id("set.skel"), nextRow(), "Joint skeleton",
                                      "Draw the tracked joints", s.skeleton);
        changed |= widgets::toggleRow(cv, ui, Ui::id("set.snd"), nextRow(), "UI sounds",
                                      "Hover and click sounds (hands have no haptics)", s.sounds);
        if (changed) s.commit();
    }

    template <typename NextRow>
    void pageDisplay(Canvas& cv, Ui& ui, NextRow nextRow) {
        Settings& s = *ctx_.settings;
        static const int rates[4] = {0, 72, 90, 120};
        int selected = 0;
        for (int i = 0; i < 4; ++i) {
            if (s.refreshRate == rates[i]) selected = i;
        }
        int pick = widgets::segmentedRow(cv, ui, Ui::id("set.rate"), nextRow(), "Refresh rate",
                                         "Changing it can flash the display briefly",
                                         {"System", "72 Hz", "90 Hz", "120 Hz"}, selected);
        if (pick >= 0 && rates[pick] != s.refreshRate) {
            s.refreshRate = rates[pick];
            s.commit();
            if (ctx_.setRefreshRate) ctx_.setRefreshRate(rates[pick]);
        }
        const float now = ctx_.info ? ctx_.info->refreshRate : 0.0f;
        widgets::infoRow(cv, nextRow(), "Current refresh rate",
                         now > 0 ? fmt("%.0f Hz", now) : std::string("unknown"));
        if (widgets::buttonRow(cv, ui, Ui::id("set.recenter"), nextRow(), "Recenter view",
                               "Bring the desktop back in front of you", "Recenter") &&
            ctx_.recenter) {
            ctx_.recenter();
        }
    }

    template <typename NextRow>
    void pageAbout(Canvas& cv, NextRow nextRow) {
        const ShellInfo* in = ctx_.info;
        widgets::infoRow(cv, nextRow(70), "Plasma XR", "0.1.0 (early preview)");
        widgets::infoRow(cv, nextRow(70), "OpenXR runtime", in ? in->runtime : "");
        widgets::infoRow(cv, nextRow(70), "Headset", in ? in->system : "");
        widgets::infoRow(cv, nextRow(70), "Eye buffer",
                         in ? std::to_string(in->eyeW) + " x " + std::to_string(in->eyeH) : "");
        std::string ht = "unavailable";
        if (in && in->handsAvailable) ht = in->handMesh ? "joints, aim pose, hand mesh" : "joints, aim pose";
        widgets::infoRow(cv, nextRow(70), "Hand tracking", ht);
        widgets::infoRow(cv, nextRow(70), "Display",
                         in && in->refreshRate > 0 ? fmt("%.0f Hz", in->refreshRate) : std::string("unknown"));
        const Rect note = nextRow(40);
        cv.text("An independent fan project, not affiliated with or endorsed by KDE e.V.",
                {note.x + 4, note.y + 4}, ts(theme::kFontTiny + 2, kTextDim));
    }

    AppContext ctx_;
    int page_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Hand Tracking monitor
// ---------------------------------------------------------------------------------------------

class HandsApp : public WindowContent {
public:
    explicit HandsApp(AppContext ctx) : ctx_(std::move(ctx)) {}

    std::string appId() const override { return "plasmaxr.hands"; }
    std::string title() const override { return "Hand Tracking"; }
    const char* glyph() const override { return "hand"; }
    unsigned color() const override { return 0x27ae60; }
    Vec2 defaultSize() const override { return {860, 620}; }
    Vec2 minSize() const override { return {700, 520}; }
    bool continuous() const override { return true; }

    void draw(Canvas& cv, Ui&, Rect area, float) override {
        if (!ctx_.hands || !ctx_.info || !ctx_.info->handsAvailable) {
            cv.textIn("Hand tracking is not available on this device.", area,
                      ts(theme::kFontBody, kTextDim), HAlign::Center);
            return;
        }
        cv.text("Live data from the OpenXR hand tracker", {area.x + 36, area.y + 22},
                ts(theme::kFontSmall, kTextDim));
        const float gap = 24;
        const float colW = (area.w - 72 - gap) * 0.5f;
        static const char* fingers[4] = {"Index", "Middle", "Ring", "Little"};
        for (int i = 0; i < 2; ++i) {
            Rect col(area.x + 36 + i * (colW + gap), area.y + 74, colW, area.h - 74 - 84);
            cv.rect(col, theme::kView.withAlpha(0.75f), 20);
            const HandState& hs = ctx_.hands->hand(i);
            const bool act = hs.active;
            cv.circle({col.x + 34, col.y + 40}, 10, act ? theme::kPositive : Color(1, 1, 1, 0.25f));
            cv.text(i == kLeft ? "Left hand" : "Right hand", {col.x + 58, col.y + 18}, ts(34, kText, 500));
            cv.text(act ? "Tracked for " + std::to_string(static_cast<int>(hs.activeSeconds)) + " s" : "Not visible",
                    {col.x + 28, col.y + 64}, ts(theme::kFontSmall, act ? theme::kPositive : kTextDim));

            for (int f = 0; f < 4; ++f) {
                const float y = col.y + 116 + f * 52;
                const float v = act ? saturate(hs.pinch[f]) : 0.0f;
                cv.text(fingers[f], {col.x + 28, y + 4}, ts(theme::kFontSmall, kTextDim));
                Rect bar(col.x + 150, y + 10, col.w - 150 - 110, 16);
                cv.rect(bar, Color(1, 1, 1, 0.12f), 8);
                if (v > 0.02f) cv.rect(Rect(bar.x, bar.y, std::max(16.0f, bar.w * v), bar.h), f == 0 && hs.indexPinching ? Color(1, 1, 1, 1) : kAccent, 8);
                cv.textIn(act ? std::to_string(static_cast<int>(v * 100 + 0.5f)) + "%" : "-",
                          Rect(bar.right() + 10, y, 90, 40), ts(theme::kFontSmall, kText, 500), HAlign::Right);
            }

            const PointerVisual::Mode mode = ctx_.pointers ? ctx_.pointers->visual(i).mode : PointerVisual::kNone;
            const char* modeName = mode == PointerVisual::kRay ? "Ray" : (mode == PointerVisual::kPoke ? "Poke" : "Idle");
            float y = col.y + 116 + 4 * 52 + 12;
            auto line = [&](const char* k, const std::string& v) {
                cv.text(k, {col.x + 28, y}, ts(theme::kFontSmall, kTextDim));
                cv.textIn(v, Rect(col.x + 150, y - 4, col.w - 150 - 28, 40), ts(theme::kFontSmall, kText, 500), HAlign::Right);
                y += 42;
            };
            line("Aim", act ? (hs.aimValid ? "runtime" : (hs.aimComputed ? "computed" : "none")) : "-");
            line("Pointer", modeName);
            line("System gesture", act ? (hs.systemGesture ? "yes" : "no") : "-");
        }
        cv.textIn("The system gesture (palm-up pinch) is left to Horizon OS.",
                  Rect(area.x + 36, area.bottom() - 70, area.w - 72, 44), ts(theme::kFontTiny + 2, kTextDim),
                  HAlign::Center);
    }

private:
    AppContext ctx_;
};

}  // namespace

const std::vector<NativeAppDef>& nativeApps() {
    static const std::vector<NativeAppDef> apps = {
        {"plasmaxr.settings", "System Settings", "settings", 0x3daee9},
        {"plasmaxr.hands", "Hand Tracking", "hand", 0x27ae60},
    };
    return apps;
}

std::unique_ptr<WindowContent> createNativeApp(const std::string& id, const AppContext& ctx) {
    if (id == "plasmaxr.settings") return std::make_unique<SettingsApp>(ctx);
    if (id == "plasmaxr.hands") return std::make_unique<HandsApp>(ctx);
    return nullptr;
}

}  // namespace pxr
