# Roadmap

## Done

* OpenXR + GLES renderer, curved UI layers, wallpaper
* Plasma-style taskbar, Kickoff menu, desktop icons, widgets
* Hand tracking input (ray + pinch, poke), hand mesh, UI sounds
* adb debug channel, offscreen panel snapshots

## Next: windows

Plasma is a window system as much as a launcher. The aim is two ways of presenting an app:

* **Docked windows** on the curved desktop, like windows on a monitor: title bar with minimise / maximise /
  close, move and resize, snapping, z-order, and the taskbar listing them.
* **Floating windows** anywhere in space: grab the title bar to move in 3D, face-the-user behaviour, resize
  from the corners, and a button to dock or undock a window.

`Panel` already supports a free pose (`Panel::free`), and the taskbar is structured to list windows. What is
missing is the window manager itself (decorations, drag and resize, focus, minimise, per-desktop sets).

## What goes inside a window

Open question, in order of effort:

1. **Native Plasma XR apps** (settings, file browser, hand-tracking tools) drawn with the existing UI toolkit.
   No extra setup.
2. **Android apps hosted in windows.** Needs a virtual display per window, launching activities onto it and
   injecting touch input, which requires a privileged helper (adb shell, Shizuku or root). Whether Horizon OS
   permits this needs to be tested on the device first. `XR_KHR_android_surface_swapchain` is available, so
   frames could go straight to the compositor.
3. **Remote desktop windows** (VNC / RDP to a PC). Needs a server on the PC and a video path into a layer.

## Also planned

* Passthrough toggle (`XR_FB_passthrough`)
* On-screen keyboard and search in Kickoff
* Hand microgestures for scrolling (`XR_META_hand_tracking_microgestures`)
* Settings app: pointer smoothing, pinch sensitivity, sound, UI scale
* Real task list via usage stats instead of "launched this session"
* Controller support as a second pointer source
