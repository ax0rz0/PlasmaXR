#include "input/pointers.h"

#include <cmath>

#include "core/log.h"

namespace pxr {

namespace {

constexpr float kPinchPress = 0.92f;    // raw strength that counts as a pinch even without the flag
constexpr float kPinchHold = 0.70f;     // an existing pinch holds until strength drops below this
constexpr float kPressLag = 0.12f;      // a press lands where the pointer was this long before it
constexpr float kReleaseLag = 0.10f;    // a release lands where the pointer was this long before it
constexpr float kPokeEnter = 0.045f;    // fingertip within 4.5 cm of a surface starts a poke
constexpr float kPokeExit = 0.09f;
constexpr float kPokeBehind = 0.10f;    // pushed this far through the surface cancels the poke
constexpr float kPokePress = 0.012f;
constexpr float kPokeRelease = 0.032f;

float alphaFor(float cutoff, float dt) {
    float tau = 1.0f / (2.0f * kPi * cutoff);
    return 1.0f / (1.0f + tau / dt);
}

bool sameSample(const PointerSample& a, const PointerSample& b) {
    if (a.present != b.present) return false;
    if (!a.present) return true;
    return a.down == b.down && a.poke == b.poke && a.wheel == b.wheel &&
           std::fabs(a.pos.x - b.pos.x) < 0.4f && std::fabs(a.pos.y - b.pos.y) < 0.4f;
}

}  // namespace

Vec2 PointerSystem::OneEuro::filter(Vec2 v, float dt, float minCutoff, float beta) {
    if (!init) {
        init = true;
        x = v;
        dx = {0, 0};
        return v;
    }
    Vec2 rawD = (v - x) * (1.0f / dt);
    float ad = alphaFor(1.0f, dt);
    dx = dx + (rawD - dx) * ad;
    float cutoff = minCutoff + beta * dx.length();
    float a = alphaFor(cutoff, dt);
    x = x + (v - x) * a;
    return x;
}

void PointerSystem::releaseState(HandPtr& s) {
    s.down = false;
    s.capture = nullptr;
    s.hist.clear();
    s.downHist.clear();
    s.filter.reset();
    s.filterPanel = nullptr;
    s.mode = PointerVisual::kNone;
}

void PointerSystem::forgetPanel(Panel* p) {
    for (HandPtr& s : hp_) {
        if (s.capture == p) {
            s.capture = nullptr;
            s.down = false;
        }
        if (s.filterPanel == p) {
            s.filterPanel = nullptr;
            s.filter.reset();
        }
        for (Sample& smp : s.hist) {
            if (smp.panel == p) smp.panel = nullptr;
        }
        for (Sample& smp : s.downHist) {
            if (smp.panel == p) smp.panel = nullptr;
        }
    }
    if (virtualPanel_ == p) {
        virtualPanel_ = nullptr;
        virtualOn_ = false;
    }
    for (PressEvent& e : presses_) {
        if (e.panel == p) e.panel = nullptr;
    }
}

// Returns the newest sample at or before `t` (or the oldest we have if the history is shorter than
// the lag). With `needPanel`, a sample that was over no panel is replaced by the nearest one that was.
PointerSystem::Sample PointerSystem::sampleAt(const std::deque<Sample>& h, float t, bool needPanel) {
    Sample best;
    bool found = false;
    for (auto it = h.rbegin(); it != h.rend(); ++it) {
        if (it->t <= t) {
            best = *it;
            found = true;
            break;
        }
    }
    if (!found && !h.empty()) best = h.front();
    if (needPanel && !best.panel) {
        for (const Sample& smp : h) {
            if (smp.t >= best.t && smp.panel) return smp;
        }
        for (auto it = h.rbegin(); it != h.rend(); ++it) {
            if (it->panel) return *it;
        }
    }
    return best;
}

void PointerSystem::setVirtualPointer(Panel* panel, Vec2 px, bool down) {
    virtualOn_ = panel != nullptr;
    virtualPanel_ = panel;
    virtualPx_ = px;
    virtualDown_ = down;
}

void PointerSystem::update(float dt, const HandTracking& hands, const Controllers& controllers,
                           const std::vector<Panel*>& panels, const Anchor& anchor,
                           bool inputEnabled) {
    presses_.clear();
    std::vector<Assign> assigns;
    dt = clampf(dt, 0.002f, 0.1f);
    clock_ += dt;

    // Nearest interactive panel hit by a ray; panels within 5 cm of each other in depth are ordered
    // by Panel::order. While a press is held the captured panel keeps the pointer.
    auto pickPanel = [&](const Ray& ray, Panel* captured, Panel** outPanel, PanelHit* outHit) {
        *outPanel = nullptr;
        *outHit = PanelHit{};
        if (captured) {
            PanelHit h = captured->intersect(ray, anchor, true);
            if (h.hit) {
                *outPanel = captured;
                *outHit = h;
            }
            return;
        }
        float nearest = 1e9f;
        for (Panel* p : panels) {
            if (!p->visible || !p->interactive) continue;
            PanelHit h = p->intersect(ray, anchor);
            if (!h.hit) continue;
            if (h.dist < nearest - 0.05f) {
                nearest = h.dist;
                *outHit = h;
                *outPanel = p;
            } else if (h.dist < nearest + 0.05f && *outPanel && p->order > (*outPanel)->order) {
                nearest = std::min(nearest, h.dist);
                *outHit = h;
                *outPanel = p;
            }
        }
    };

    for (int i = 0; i < 2; ++i) {
        PointerVisual& vis = visuals_[i];
        vis = PointerVisual{};
        HandPtr& s = hp_[i];
        const HandState& hs = hands.hand(i);

        // ---- debug virtual pointer drives pointer 0 -----------------------------------
        if (virtualOn_ && i == 0 && virtualPanel_) {
            s.mode = PointerVisual::kRay;
            if (virtualDown_ && !s.down) presses_.push_back({virtualPanel_, virtualPx_, i});
            s.down = virtualDown_;
            Vec3 hit, normal;
            virtualPanel_->pxToWorld(virtualPx_, anchor, &hit, &normal);
            vis.mode = PointerVisual::kRay;
            vis.hasTarget = true;
            vis.origin = anchor.pos + Vec3(0.25f, -0.35f, -0.25f);
            vis.rayOrigin = vis.origin;
            vis.rayDir = (hit - vis.origin).normalized();
            vis.hit = hit;
            vis.normal = normal;
            vis.pinch = s.down ? 1.0f : 0.0f;
            vis.down = s.down;
            assigns.push_back({virtualPanel_, i, {true, virtualPx_, s.down, false}});
            continue;
        }

        // ---- controller: aim ray, trigger to click, thumbstick to scroll ----------------
        const ControllerState& cs = controllers.state(i);
        if (inputEnabled && cs.active) {
            if (!s.prevController) {
                releaseState(s);
                s.prevController = true;
            }
            s.mode = PointerVisual::kRay;
            const Ray ray{cs.aim.p, cs.aim.forward()};
            Panel* hitPanel = nullptr;
            PanelHit hit;
            pickPanel(ray, s.down ? s.capture : nullptr, &hitPanel, &hit);

            Vec2 live = s.lastLive;
            if (hitPanel) {
                if (s.filterPanel != hitPanel) {
                    s.filter.reset();
                    s.filterPanel = hitPanel;
                }
                live = s.filter.filter(hit.px, dt, 4.0f / std::max(0.2f, smoothing_), 0.02f);
                s.lastLive = live;
            } else {
                s.filter.reset();
                s.filterPanel = nullptr;
            }

            const bool trigger = s.down ? cs.trigger > 0.35f : cs.trigger > 0.6f;
            Panel* target = hitPanel;
            Vec2 px = live;
            if (!s.down && trigger) {
                s.down = true;
                s.capture = hitPanel;
                presses_.push_back({hitPanel, live, i});
                LOGI("ptr[%d] CONTROLLER PRESS panel=%s px=(%.0f,%.0f) trigger=%.2f", i,
                     hitPanel ? hitPanel->name.c_str() : "-", live.x, live.y, cs.trigger);
            } else if (s.down && !trigger) {
                LOGI("ptr[%d] CONTROLLER RELEASE panel=%s", i, s.capture ? s.capture->name.c_str() : "-");
                target = s.capture;
                s.down = false;
                s.capture = nullptr;
            } else if (s.down) {
                target = s.capture;
            }
            vis.mode = PointerVisual::kRay;
            vis.controller = true;
            vis.origin = cs.aim.p;
            vis.rayOrigin = cs.aim.p;
            vis.rayDir = cs.aim.forward();
            vis.pinch = cs.trigger;
            vis.down = s.down;
            if (target) {
                Vec3 point, normal;
                target->pxToWorld(px, anchor, &point, &normal);
                vis.hasTarget = true;
                vis.hit = point;
                vis.normal = normal;
                PointerSample sample{true, px, s.down, false};
                if (std::fabs(cs.stick.y) > 0.2f) sample.wheel = -cs.stick.y * 1100.0f * dt;
                assigns.push_back({target, i, sample});
            } else {
                vis.hit = cs.aim.p + cs.aim.forward() * 1.4f;
                vis.normal = -cs.aim.forward();
            }
            continue;
        }
        if (s.prevController) {  // controller put down: hand over to hand tracking cleanly
            releaseState(s);
            s.prevController = false;
        }

        const bool usable = inputEnabled && hs.active && !hs.systemGesture &&
                            (hs.aimValid || hs.aimComputed);
        if (usable != s.prevUsable) {
            LOGI("ptr[%d] %s (input=%d active=%d systemGesture=%d)", i, usable ? "usable" : "unusable",
                 inputEnabled ? 1 : 0, hs.active ? 1 : 0, hs.systemGesture ? 1 : 0);
            s.prevUsable = usable;
        }
        if (!usable) {
            releaseState(s);
            continue;
        }

        // ---- poke: index fingertip near a panel surface -------------------------------
        Panel* pokePanel = nullptr;
        float pokeD = 1e9f, bestScore = 1e9f;
        Vec2 pokePx;
        Vec3 pokeProj, pokeNormal;
        if (pokeEnabled_ && hs.jointValid[XR_HAND_JOINT_INDEX_TIP_EXT]) {
            const Vec3 tip = hs.indexTip();
            for (Panel* p : panels) {
                if (!p->visible || !p->interactive) continue;
                Vec2 px;
                Vec3 proj, n;
                bool inb = false;
                float d = p->distanceInFront(tip, anchor, &px, &proj, &inb, &n);
                const bool heldHere = s.down && s.capture == p;
                if (!inb && !heldHere) continue;
                if (d > 0.12f || d < -0.12f) continue;
                float score = std::fabs(d) - p->order * 0.0001f;
                if (score < bestScore) {
                    bestScore = score;
                    pokePanel = p;
                    pokeD = d;
                    pokePx = px;
                    pokeProj = proj;
                    pokeNormal = n;
                }
            }
        }
        bool poke;
        if (s.mode == PointerVisual::kPoke) {
            poke = pokePanel && pokeD < kPokeExit && pokeD > -kPokeBehind;
        } else {
            poke = pokePanel && pokeD < kPokeEnter && pokeD > -0.015f && !hs.indexPinching;
        }
        if (s.mode == PointerVisual::kRay && s.down) poke = false;

        if (poke) {
            if (s.mode != PointerVisual::kPoke) s.filter.reset();
            s.mode = PointerVisual::kPoke;
            bool down = s.down ? (pokeD < kPokeRelease) : (pokeD < kPokePress);
            if (down && !s.down) {
                s.capture = pokePanel;
                presses_.push_back({pokePanel, pokePx, i});
            }
            if (!down) s.capture = nullptr;
            s.down = down;
            vis.mode = PointerVisual::kPoke;
            vis.hasTarget = true;
            vis.origin = hs.indexTip();
            vis.rayOrigin = hs.indexTip() + pokeNormal * 0.30f;
            vis.rayDir = -pokeNormal;
            vis.hit = pokeProj;
            vis.normal = pokeNormal;
            vis.pinch = 0;
            vis.down = s.down;
            vis.pokeDepth = pokeD;
            assigns.push_back({pokePanel, i, {true, pokePx, s.down, true}});
            continue;
        }
        if (s.mode == PointerVisual::kPoke) {  // left poke range: release cleanly
            s.down = false;
            s.capture = nullptr;
        }

        // ---- far ray ---------------------------------------------------------------------
        s.mode = PointerVisual::kRay;
        const Ray ray{hs.aim.p, hs.aim.forward()};

        Panel* hitPanel = nullptr;
        PanelHit hit;
        if (s.down && s.capture) {
            hit = s.capture->intersect(ray, anchor, true);
            if (hit.hit) hitPanel = s.capture;
        } else {
            float nearest = 1e9f;
            for (Panel* p : panels) {
                if (!p->visible || !p->interactive) continue;
                PanelHit h = p->intersect(ray, anchor);
                if (h.hit && h.dist < nearest - 0.05f) {
                    nearest = h.dist;
                    hit = h;
                    hitPanel = p;
                } else if (h.hit && h.dist < nearest + 0.05f && hitPanel && p->order > hitPanel->order) {
                    nearest = std::min(nearest, h.dist);
                    hit = h;
                    hitPanel = p;
                }
            }
        }

        Vec2 live = s.lastLive;
        if (hitPanel) {
            if (s.filterPanel != hitPanel) {
                s.filter.reset();
                s.filterPanel = hitPanel;
            }
            const float cutoff = 3.0f / std::max(0.2f, smoothing_);
            live = s.filter.filter(hit.px, dt, cutoff, 0.02f);
            s.lastLive = live;
        } else {
            s.filter.reset();
            s.filterPanel = nullptr;
        }

        // The runtime's pinch flag is the main signal; a very high raw strength also counts, and a
        // lower strength keeps an existing pinch alive so it cannot flicker.
        const float strength = hs.pinch[0];
        const bool pinching = s.down ? (hs.indexPinching || strength >= kPinchHold)
                                     : (hs.indexPinching || strength >= kPinchPress);
        if (!s.down) {
            s.hist.push_back({clock_, hitPanel, live});
            while (!s.hist.empty() && s.hist.front().t < clock_ - 0.6f) s.hist.pop_front();
        }

        Panel* target = nullptr;
        Vec2 px;
        bool downOut = false;

        if (!s.down) {
            target = hitPanel;  // the cursor simply follows the hand
            px = live;
            if (pinching) {
                // Closing the fingers drags the aim ray with it, so the press lands where the ray was
                // just before the pinch began; later motion is relative to the ray right now.
                const Sample past = sampleAt(s.hist, clock_ - kPressLag, true);
                s.capture = past.panel ? past.panel : hitPanel;
                s.pressPx = past.panel ? past.px : live;
                Vec2 anchorPx = live;
                if (s.capture && s.capture != hitPanel) {
                    const PanelHit h2 = s.capture->intersect(ray, anchor, true);
                    if (h2.hit) anchorPx = h2.px;
                }
                s.anchorLive = anchorPx;
                s.down = true;
                s.downHist.clear();
                presses_.push_back({s.capture, s.pressPx, i});
                target = s.capture;
                px = s.pressPx;
                downOut = true;
                LOGI("ptr[%d] PRESS panel=%s px=(%.0f,%.0f) strength=%.2f flag=%d", i,
                     target ? target->name.c_str() : "-", px.x, px.y, strength,
                     hs.indexPinching ? 1 : 0);
            }
        } else {
            target = s.capture;
            px = target ? s.pressPx + (live - s.anchorLive) : Vec2();
            s.downHist.push_back({clock_, target, px});
            while (!s.downHist.empty() && s.downHist.front().t < clock_ - 0.4f) s.downHist.pop_front();
            if (!pinching) {
                // Opening the fingers moves the ray as well: release where it was just before that.
                const Sample past = sampleAt(s.downHist, clock_ - kReleaseLag, false);
                if (target) px = past.px;
                LOGI("ptr[%d] RELEASE panel=%s px=(%.0f,%.0f) strength=%.2f flag=%d", i,
                     target ? target->name.c_str() : "-", px.x, px.y, strength,
                     hs.indexPinching ? 1 : 0);
                s.down = false;
                s.capture = nullptr;
                s.hist.clear();
                downOut = false;
            } else {
                downOut = true;
            }
        }

        // Telemetry: peaks over the interval, because a pinch is shorter than the log period.
        s.maxStrength = std::max(s.maxStrength, strength);
        s.anyFlag = s.anyFlag || hs.indexPinching;
        s.minTipDist = std::min(s.minTipDist, distance(hs.indexTip(), hs.thumbTip()));
        {
            const Vec3 fwd = hs.aim.forward();
            for (Panel* p : panels) {
                if (!p->visible || !p->interactive) continue;
                const Vec3 to = (p->center(anchor) - hs.aim.p).normalized();
                const float ang = std::acos(clampf(fwd.dot(to), -1.0f, 1.0f));
                if (ang < s.bestAngle) {
                    s.bestAngle = ang;
                    s.bestPanel = p->name;
                }
            }
        }
        if (clock_ - s.lastLog > 2.0f) {
            s.lastLog = clock_;
            LOGI("ptr[%d] over=%s | peak strength=%.2f flag=%d tipGap=%.1fcm | aim %.0fdeg off '%s' | sys=%d down=%d",
                 i, hitPanel ? hitPanel->name.c_str() : "-", s.maxStrength, s.anyFlag ? 1 : 0,
                 s.minTipDist * 100.0f, s.bestAngle / kDegToRad, s.bestPanel.c_str(),
                 hs.systemGesture ? 1 : 0, s.down ? 1 : 0);
            s.maxStrength = 0;
            s.minTipDist = 9;
            s.bestAngle = 9;
            s.anyFlag = false;
        }

        vis.mode = PointerVisual::kRay;
        vis.origin = hs.aim.p;
        vis.rayOrigin = hs.aim.p;
        vis.rayDir = hs.aim.forward();
        vis.pinch = strength;
        vis.down = downOut;
        if (target) {
            Vec3 point, normal;
            target->pxToWorld(px, anchor, &point, &normal);
            vis.hasTarget = true;
            vis.hit = point;
            vis.normal = normal;
            assigns.push_back({target, i, {true, px, downOut, false}});
        } else {
            vis.hasTarget = false;
            vis.hit = hs.aim.p + hs.aim.forward() * 1.4f;
            vis.normal = -hs.aim.forward();
        }
    }

    for (Panel* p : panels) {
        for (int i = 0; i < kMaxPointers; ++i) {
            PointerSample ns;
            for (const Assign& a : assigns) {
                if (a.panel == p && a.pointer == i) ns = a.sample;
            }
            if (!sameSample(ns, p->prevPointer[i])) p->dirty = true;
            p->pointer[i] = ns;
            p->prevPointer[i] = ns;
        }
    }
}

}  // namespace pxr
