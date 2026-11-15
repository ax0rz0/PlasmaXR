# Plasma XR

A KDE&nbsp;Plasma-style desktop shell for the Meta Quest, built **hands-first**. Plasma in VR:
a curved desktop, a taskbar, a Kickoff-style launcher and widgets, all driven by hand tracking.

> Plasma XR is an independent fan project. It is not affiliated with or endorsed by KDE e.V.
> It borrows the Breeze Dark colour palette and general layout ideas; no KDE artwork or code is used.

## Status

Early. Developed and tested on a Quest 2 (Horizon OS, Android 14, OpenXR runtime 209).

| Area | State |
| --- | --- |
| OpenXR session, GLES renderer, curved UI layers | Working |
| Wallpaper (procedural sky), taskbar, Kickoff menu, desktop icons, widgets | Working |
| Real app icons and app list from the headset | Working |
| Hand tracking (joints, runtime aim pose, pinch, skinned mesh) | Working: both hands tracked with the runtime's aim pose; mesh renders correctly |
| Pinch-ray interaction | Verified end to end with a *simulated* hand (hover, pinch, release, click) |
| Touch controllers (aim ray, trigger, thumbstick scroll, haptics) | Implemented; hands pause while a controller is active |
| Poke (direct touch) interaction | Implemented, not yet exercised |
| Window manager (docked desktop windows + free-floating windows) | Written; verified with a Linux window on the headset (screenshot-checked) |
| Linux apps (Debian arm64 under proot, own Wayland compositor) | Working: foot and weston-simple-shm run on the headset and get shell windows; foot, GTK 3, wev and weston demos are also checked against the compositor in a container (rendering, pointer, keyboard, resize). See [docs/LINUX_APPS.md](docs/LINUX_APPS.md) |
| Replacing Meta's Home and HUD | In progress, staged and reversible: [docs/META_SHELL.md](docs/META_SHELL.md) |
| The real Plasma shell (plasmashell) on our compositor | Planned, see [ROADMAP](docs/ROADMAP.md) |
| Hosting Android apps in windows, on-screen keyboard, passthrough toggle | Planned |

## Interaction

* **Far:** point with either hand; a ray and cursor appear on the panel. **Pinch** (thumb + index) to click,
  hold and drag to scroll lists. The cursor stops following your hand as the pinch closes so clicks do not slip.
* **Near:** touch a panel with your index fingertip to **poke** it.
* The palm-up system gesture is ignored by the shell and left to Horizon OS.
* Hands have no haptics, so hover, press and release play short sounds.

See [docs/HAND_TRACKING.md](docs/HAND_TRACKING.md) for how input is derived.

## Build and run

Requirements: JDK 17 to 21, the Android SDK (platform 35, NDK r28c `28.2.13676358`, CMake 3.22.1), and `adb`.
Point Gradle at your SDK with `local.properties` (`sdk.dir=/path/to/sdk`) or `ANDROID_HOME`.

```sh
./scripts/run.sh            # build, install and launch on the connected headset
./scripts/logcat.sh         # follow the app log
./scripts/screenshot.sh     # capture what the headset is displaying to debug-out/
```

Gradle alone also works: `./gradlew assembleDebug` (APK in `app/build/outputs/apk/debug/`).

The headset needs developer mode and an authorised `adb` connection. Notes on wireless adb (the Quest has a
single USB-C port, so you cannot charge and keep a cable open at the same time) are in
[docs/DEVICE_SETUP.md](docs/DEVICE_SETUP.md).

## Layout

```
app/src/main/cpp/        native shell (C++17, OpenXR + OpenGL ES)
  xr/                    OpenXR context, swapchains, panels (cylinder / quad layers)
  gfx/                   2D canvas, 3D overlay (hands, rays), wallpaper
  input/                 hand tracking and the pointer system
  ui/                    immediate-mode UI core
  shell/                 Plasma shell: theme, app model, taskbar, Kickoff, widgets
  platform/              JNI bridge to Android
app/src/main/java/       thin Java side: apps, icons, text rasterising, sounds
app/src/debug/           debug-only adb command receiver
docs/                    architecture, hand tracking, debugging, device setup, roadmap
scripts/                 build / install / log / screenshot helpers
tools/linux, tools/wayland  Linux environment, compositor test harness and scenarios
tools/device             reversible control of Meta's system packages
```

## Documentation

* [Architecture](docs/ARCHITECTURE.md)
* [Hand tracking and pointer logic](docs/HAND_TRACKING.md)
* [Debugging over adb](docs/DEBUGGING.md)
* [Device setup](docs/DEVICE_SETUP.md)
* [Linux apps](docs/LINUX_APPS.md)
* [Replacing Meta's shell](docs/META_SHELL.md)
* [Roadmap](docs/ROADMAP.md)

## Licence

[MIT](LICENSE). Uses the Khronos OpenXR loader (Apache-2.0) from Maven Central.
