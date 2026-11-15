# Debugging over adb

## Logs

```sh
./scripts/logcat.sh          # tag PlasmaXR plus crashes
```

At startup the app logs the OpenXR runtime, the extension list, the GL renderer, the swapchain format,
hand tracker creation, panel sizes, and session state transitions. Hand state changes are logged
(`Left hand tracked (aim=runtime)`, `... lost`).

## See what the headset shows

```sh
./scripts/screenshot.sh      # adb screencap: the composited stereo output, both eyes
```

`adb screencap` returns the real compositor output (lens-distorted, side by side), which is the quickest
way to check layer placement, orientation and text clarity.

## Debug commands (debug builds only)

The debug build includes a receiver that forwards commands into the render loop:

```sh
./scripts/debug.sh <command> [args...]
# by hand (the inner single quotes matter: the headset's shell re-splits the string otherwise)
adb shell am broadcast -n org.plasmaxr.launcher/.DebugReceiver --es cmd "'sim taskbar 0.5 0.5 1'"
```

| Command | Effect |
| --- | --- |
| `snap` | render every panel offscreen and save PNGs (see below) |
| `kickoff open` / `kickoff close` | open or close the launcher menu |
| `tab 0|1|2` | Kickoff tab: Favorites, Applications, Leave |
| `ptr <panel> <u> <v> <down>` | virtual pointer on a panel at normalised (u, v); `down` is 0 or 1 |
| `ptr off` | remove the virtual pointer |
| `sim <panel> <u> <v> <pinch>` | simulated right hand aiming at (u, v) on a panel, pinch 0..1 |
| `sim off` | back to live hand tracking |
| `overlay mesh|skeleton|ray|cursor 0|1` | toggle hand mesh, joint skeleton, pointer rays or cursors |
| `recenter` | re-anchor the shell in front of you |
| `quit` | finish the activity |

A typical pointer test, aiming a simulated hand at the Kickoff button (normalised coordinates on the taskbar):

```sh
./scripts/debug.sh sim taskbar 0.043 0.5 0    # hover
./scripts/debug.sh sim taskbar 0.043 0.5 1    # pinch: press
./scripts/debug.sh sim taskbar 0.043 0.5 0    # release: click, the menu opens
./scripts/debug.sh sim off
```

Panels: `desktop`, `clock`, `system`, `hands`, `taskbar`, `kickoff`.

The receiver is declared in `app/src/debug/`, so release builds do not accept these commands.

## Panel snapshots

```sh
./scripts/snap.sh            # runs `snap`, then pulls the PNGs into debug-out/snap/
```

Snapshots are drawn with the same code as the headset but into an offscreen buffer, over a dark backdrop, so
they work as a design preview even while the headset is idle.

## Known quirk: black screenshots on a motionless headset

If the headset is sitting perfectly still (on a desk, charging), `screenshot.sh` returns an all-black image
roughly 8 to 14 seconds after the app launches, then recovers. The app is not involved as far as can be
told: the session stays `FOCUSED`, there are no slow or discarded frames, focus never leaves the activity,
and it happens identically with no simulation and no refresh-rate request. The system log at that moment
shows Horizon's shell changing its user-presence state while the tracking service reports the headset
"idle due to very close rotation for an extended time". Wait about 20 seconds after launch before
capturing, or move the headset.
