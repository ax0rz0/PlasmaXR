// Row-style controls used by the native apps. Whole rows are hit targets, which matters when the
// pointer is a hand ray with a degree or two of jitter.
#pragma once

#include <string>
#include <vector>

#include "gfx/canvas.h"
#include "ui/ui.h"

namespace pxr {
namespace widgets {

constexpr float kRowHeight = 96;

// Label + hint on the left, on/off switch on the right. Returns true when toggled.
bool toggleRow(Canvas& cv, Ui& ui, uint32_t id, Rect row, const std::string& label,
               const std::string& hint, bool& value);

// Label + hint, a value readout and a slider. Returns true while the value is being changed.
bool sliderRow(Canvas& cv, Ui& ui, uint32_t id, Rect row, const std::string& label,
               const std::string& hint, float& value, float lo, float hi,
               const std::string& valueText);

// Mutually exclusive options. Returns the clicked option index, or -1.
int segmentedRow(Canvas& cv, Ui& ui, uint32_t id, Rect row, const std::string& label,
                 const std::string& hint, const std::vector<std::string>& options, int selected);

// Label + hint and a push button. Returns true when clicked.
bool buttonRow(Canvas& cv, Ui& ui, uint32_t id, Rect row, const std::string& label,
               const std::string& hint, const std::string& buttonText);

// Label on the left, value on the right; not interactive.
void infoRow(Canvas& cv, Rect row, const std::string& label, const std::string& value);

}  // namespace widgets
}  // namespace pxr
