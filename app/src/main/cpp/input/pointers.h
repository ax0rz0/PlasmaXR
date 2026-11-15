// Turns hand tracking into UI pointers. Each hand is either a far-field ray (aim pose, pinch to
// click) or a direct-touch poke (index fingertip against a panel surface), and produces
// PointerSamples for the panel it is over plus the visuals the overlay draws.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "input/controllers.h"
#include "input/hands.h"
#include "xr/panel.h"

namespace pxr {

struct PointerVisual {
    enum Mode { kNone, kRay, kPoke };
    Mode mode = kNone;
    bool hasTarget = false;  // over a panel surface
    Vec3 origin;             // ray start (aim origin or fingertip)
    Vec3 rayOrigin;          // the pointing ray itself (for poke: a ray into the surface)
    Vec3 rayDir;
    Vec3 hit;                // cursor position in the world
    Vec3 normal;             // surface normal facing the viewer
    float pinch = 0;         // 0..1 (pinch strength, or trigger pull for a controller)
    bool down = false;
    float pokeDepth = 0;     // fingertip distance from the surface in metres
    bool controller = false; // driven by a controller instead of a hand
};

struct PressEvent {
    Panel* panel = nullptr;  // nullptr = pressed on empty space (used for light-dismiss)
    Vec2 px;
    int pointer = 0;
};

class PointerSystem {
public:
    void update(float dt, const HandTracking& hands, const Controllers& controllers,
                const std::vector<Panel*>& panels, const Anchor& anchor, bool inputEnabled);

    const PointerVisual& visual(int i) const { return visuals_[i]; }
    const std::vector<PressEvent>& pressEvents() const { return presses_; }
    bool isDown(int i) const { return hp_[i].down; }
    Panel* captured(int i) const { return hp_[i].capture; }

    // Must be called before a panel is destroyed so no pointer keeps a dangling reference.
    void forgetPanel(Panel* p);

    void setPokeEnabled(bool v) { pokeEnabled_ = v; }
    void setSmoothing(float s) { smoothing_ = s; }

    // Debug: drive pointer 0 without a hand (see docs/DEBUGGING.md).
    void setVirtualPointer(Panel* panel, Vec2 px, bool down);
    void clearVirtualPointer() { virtualOn_ = false; }

private:
    struct OneEuro {
        bool init = false;
        Vec2 x, dx;
        Vec2 filter(Vec2 v, float dt, float minCutoff, float beta);
        void reset() { init = false; }
    };
    // One pointer position at a point in time, kept briefly so a click can land where the pointer
    // was *before* the fingers moved the aim ray.
    struct Sample {
        float t = 0;
        Panel* panel = nullptr;
        Vec2 px;
    };
    struct HandPtr {
        PointerVisual::Mode mode = PointerVisual::kNone;
        bool down = false;
        Panel* capture = nullptr;
        Vec2 pressPx;       // where the press landed
        Vec2 anchorLive;    // the live hit on the captured panel at the moment of the press
        Vec2 lastLive;
        OneEuro filter;
        Panel* filterPanel = nullptr;
        std::deque<Sample> hist;      // live hits while not pressed
        std::deque<Sample> downHist;  // pointer positions while pressed
        bool prevPinching = false;
        bool prevController = false;
        bool prevUsable = true;
        float lastLog = -10;
        // Telemetry accumulated between log lines.
        float maxStrength = 0;
        float minTipDist = 9;
        float bestAngle = 9;
        std::string bestPanel;
        bool anyFlag = false;
    };
    struct Assign {
        Panel* panel;
        int pointer;
        PointerSample sample;
    };

    void releaseState(HandPtr& s);
    static Sample sampleAt(const std::deque<Sample>& h, float t, bool needPanel);

    float clock_ = 0;
    HandPtr hp_[2];
    PointerVisual visuals_[2];
    std::vector<PressEvent> presses_;
    bool pokeEnabled_ = true;
    float smoothing_ = 1.0f;

    bool virtualOn_ = false;
    Panel* virtualPanel_ = nullptr;
    Vec2 virtualPx_;
    bool virtualDown_ = false;
};

}  // namespace pxr
