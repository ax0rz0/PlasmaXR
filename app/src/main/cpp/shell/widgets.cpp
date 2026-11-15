#include "shell/widgets.h"

#include <algorithm>
#include <cmath>

#include "shell/theme.h"

namespace pxr {
namespace widgets {

namespace {

using theme::kAccent;
using theme::kText;
using theme::kTextDim;

TextStyle style(float size, Color color, int weight = 400, float maxW = 0) {
    TextStyle t;
    t.size = size;
    t.color = color;
    t.weight = weight;
    t.maxWidth = maxW;
    return t;
}

void rowBackground(Canvas& cv, Rect row, const WidgetState* w) {
    float a = 0.045f;
    if (w) a += 0.05f * w->hover + 0.04f * w->press;
    cv.rect(row, Color(1, 1, 1, a), 20);
    if (w && w->hover > 0.01f) cv.stroke(row, kAccent.withAlpha(0.55f * w->hover), 2.5f, 20);
}

void labels(Canvas& cv, Rect row, const std::string& label, const std::string& hint, float maxW) {
    const float x = row.x + 28;
    if (hint.empty()) {
        cv.text(label, {x, row.center().y - 20}, style(theme::kFontBody, kText, 500, maxW));
    } else {
        cv.text(label, {x, row.y + 14}, style(theme::kFontBody, kText, 500, maxW));
        cv.text(hint, {x, row.y + 56}, style(theme::kFontTiny + 2, kTextDim, 400, maxW));
    }
}

}  // namespace

bool toggleRow(Canvas& cv, Ui& ui, uint32_t id, Rect row, const std::string& label,
               const std::string& hint, bool& value) {
    WidgetState w = ui.widget(id, row);
    rowBackground(cv, row, &w);
    labels(cv, row, label, hint, row.w - 190);

    Rect track(row.right() - 28 - 92, row.center().y - 24, 92, 48);
    const float t = ui.animate(id ^ 0x51ed270bu, value ? 1.0f : 0.0f, 20.0f);
    cv.rect(track, mix(Color(1, 1, 1, 0.16f), kAccent, t), 24);
    const float kx = track.x + 24 + t * (track.w - 48);
    cv.circle({kx, track.center().y}, 18 + 2 * w.press, Color(1, 1, 1, 1));
    if (w.clicked) {
        value = !value;
        return true;
    }
    return false;
}

bool sliderRow(Canvas& cv, Ui& ui, uint32_t id, Rect row, const std::string& label,
               const std::string& hint, float& value, float lo, float hi,
               const std::string& valueText) {
    const float trackW = 360;
    Rect track(row.right() - 40 - trackW, row.center().y - 6, trackW, 12);
    Rect hit(track.x - 24, row.y, track.w + 48, row.h);
    WidgetState w = ui.widget(id, hit);
    rowBackground(cv, row, &w);
    labels(cv, row, label, hint, row.w - trackW - 260);

    bool changed = false;
    if (w.pressed && w.pointer >= 0) {
        float t = saturate((ui.pointerPos(w.pointer).x - track.x) / track.w);
        float v = lo + t * (hi - lo);
        v = std::round(v * 20.0f) / 20.0f;  // 0.05 steps keep the readout tidy
        if (std::fabs(v - value) > 1e-4f) {
            value = clampf(v, lo, hi);
            changed = true;
        }
    }
    const float t = saturate((value - lo) / (hi - lo));
    cv.rect(track, Color(1, 1, 1, 0.16f), 6);
    cv.rect(Rect(track.x, track.y, std::max(12.0f, track.w * t), track.h), kAccent, 6);
    cv.circle({track.x + track.w * t, track.center().y}, 17 + 3 * w.press, Color(1, 1, 1, 1));
    cv.textIn(valueText, Rect(track.x - 150, row.y, 130, row.h), style(theme::kFontSmall, kTextDim, 500),
              HAlign::Right);
    return changed;
}

int segmentedRow(Canvas& cv, Ui& ui, uint32_t id, Rect row, const std::string& label,
                 const std::string& hint, const std::vector<std::string>& options, int selected) {
    rowBackground(cv, row, nullptr);
    labels(cv, row, label, hint, row.w * 0.42f);

    const float h = 60, gap = 8;
    std::vector<float> widths;
    float total = 0;
    for (const std::string& o : options) {
        float w = std::max(104.0f, cv.measureText(o, theme::kFontSmall, 500).x + 44);
        widths.push_back(w);
        total += w;
    }
    total += gap * (options.size() - 1);
    float x = row.right() - 28 - total;
    int clicked = -1;
    for (size_t i = 0; i < options.size(); ++i) {
        Rect r(x, row.center().y - h * 0.5f, widths[i], h);
        WidgetState w = ui.widget(Ui::id(id, static_cast<int>(i)), r);
        const bool sel = static_cast<int>(i) == selected;
        if (sel) cv.rect(r, kAccent, 16);
        else cv.rect(r, Color(1, 1, 1, 0.08f + 0.08f * w.hover + 0.06f * w.press), 16);
        if (!sel && w.hover > 0.01f) cv.stroke(r, kAccent.withAlpha(0.6f * w.hover), 2.5f, 16);
        cv.textIn(options[i], r, style(theme::kFontSmall, sel ? Color(1, 1, 1, 1) : kText, 500));
        if (w.clicked) clicked = static_cast<int>(i);
        x += widths[i] + gap;
    }
    return clicked;
}

bool buttonRow(Canvas& cv, Ui& ui, uint32_t id, Rect row, const std::string& label,
               const std::string& hint, const std::string& buttonText) {
    rowBackground(cv, row, nullptr);
    labels(cv, row, label, hint, row.w * 0.5f);
    const float bw = std::max(180.0f, cv.measureText(buttonText, theme::kFontSmall, 500).x + 56);
    Rect b(row.right() - 28 - bw, row.center().y - 30, bw, 60);
    WidgetState w = ui.widget(id, b);
    cv.rect(b, kAccent.withAlpha(0.20f + 0.25f * w.hover + 0.2f * w.press), 16);
    cv.stroke(b, kAccent.withAlpha(0.8f), 2.5f, 16);
    cv.textIn(buttonText, b, style(theme::kFontSmall, kText, 500));
    return w.clicked;
}

void infoRow(Canvas& cv, Rect row, const std::string& label, const std::string& value) {
    cv.rect(row, Color(1, 1, 1, 0.035f), 16);
    cv.text(label, {row.x + 24, row.center().y - 16}, style(theme::kFontSmall, kTextDim, 400, row.w * 0.4f));
    cv.textIn(value, Rect(row.x + row.w * 0.42f, row.y, row.w * 0.58f - 24, row.h),
              style(theme::kFontSmall, kText, 500), HAlign::Right);
}

}  // namespace widgets
}  // namespace pxr
