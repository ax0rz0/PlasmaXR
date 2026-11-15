# Roadmap

## Done

* OpenXR + GLES renderer, curved UI layers, wallpaper
* Plasma-style taskbar, Kickoff menu, desktop icons, widgets
* Hand tracking (ray + pinch, poke), hand mesh, Touch controllers, UI sounds
* Window system: docked and floating windows, move / resize / maximise / minimise, native apps
* **Linux apps.** A Debian arm64 root filesystem runs under proot, and a Wayland compositor inside the shell
  turns each Linux window into a shell window. foot and weston-simple-shm run on the headset; GTK 3, wev and
  weston demos were checked in a container (see [LINUX_APPS.md](LINUX_APPS.md))
* Reversible tooling for turning off Meta's own apps (see [META_SHELL.md](META_SHELL.md))
* adb debug channel, offscreen panel snapshots

## Now

1. **Replace Meta's shell.** Plasma XR becomes the Home app and takes over what the HUD does. Staged and
   reversible; the plan is in [META_SHELL.md](META_SHELL.md).
2. **The real Plasma shell.** Run Debian's own `plasmashell` (panel, Task Manager, Kickoff, system tray,
   Breeze) in the Linux environment and make our compositor play KWin's part: implement KDE's
   `plasma-shell` and `plasma-window-management` protocols so Plasma's panels and popups become XR panels and
   every window shows up in the real taskbar. This is how Plasma Mobile works, with a different compositor.
   A container image with Plasma 6.3 for testing the protocols without a headset is in `tools/wayland`.
3. **On-screen keyboard** and a way to scroll Linux windows by hand.

## Later

* Steam (ARM64 client, with FEX for x86 games) and SteamVR. Memory is the limit: the headset has 6 GB, and
  Plasma, Steam and Meta's services all want it.
* XWayland, so X11 programs get their own windows
* Android apps hosted in windows (virtual display per window, needs a privileged helper)
* Passthrough toggle (`XR_FB_passthrough`)
* Hand microgestures for scrolling (`XR_META_hand_tracking_microgestures`)
* Settings app: pointer smoothing, pinch sensitivity, sound, UI scale, Linux app scale
