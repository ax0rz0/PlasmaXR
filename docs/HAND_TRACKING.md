# Hand tracking and pointer logic

## Runtime features used

| Extension | Used for |
| --- | --- |
| `XR_EXT_hand_tracking` | 26 joints per hand |
| `XR_FB_hand_tracking_aim` | aim pose, pinch strengths, pinch / system-gesture flags |
| `XR_FB_hand_tracking_mesh` | skinned hand mesh |

The manifest declares `oculus.software.handtracking` (optional) and `com.oculus.permission.HAND_TRACKING`,
and requests high-frequency tracking (`com.oculus.handtracking.frequency=HIGH`, `version=V2.0`).

If the runtime does not provide an aim pose, one is derived: a ray from an estimated shoulder through the
pinch point, with pinch strength from the thumb-tip to index-tip distance.

## Far interaction (ray + pinch)

1. The aim pose is intersected with every visible panel (cylinder or quad). The panel with the nearest hit
   wins; panels that are within 5 cm of each other in depth are ordered by `Panel::order`.
2. The hit is smoothed with a One Euro filter in panel pixels.
3. **Anti-slip latch.** Closing the fingers moves the aim ray. Once pinch strength passes 0.25 the cursor
   freezes at its last position; when the runtime reports an index pinch the press happens *there*, and
   afterwards the cursor moves relative to that point (so dragging still works).
4. While pressed, the pointer stays captured by the panel it started on, even if the ray leaves it.

## Near interaction (poke)

The index fingertip is projected onto each panel surface.

| Distance in front of the surface | Result |
| --- | --- |
| under 4.5 cm | poke mode starts (the far ray is hidden) |
| under 1.2 cm | press |
| over 3.2 cm | release |
| over 9 cm | poke mode ends |
| 10 cm behind | poke cancelled (reaching through the panel) |

These numbers are first guesses and want tuning on real hands.

## Things that are deliberately ignored

* The palm-up **system gesture** (and menu press) is reported by the runtime and never turned into UI input.
* Hands closer than about 12 cm to the eyes are faded out of the overlay.

## Feedback

Hands have no haptics. Hovering a target, pressing and releasing each play a short synthesised sound
(`Sounds.java`). The cursor ring tightens as the pinch closes and fills when pressed; the poke ring shrinks as
the fingertip approaches the surface.

## Simulating a hand

`sim` places a synthetic right hand in front of the head aimed at a point on a panel, so the whole input path
can be exercised without live tracking. See [DEBUGGING.md](DEBUGGING.md).
