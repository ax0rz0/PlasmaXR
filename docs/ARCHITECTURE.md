# Architecture

Plasma XR is a native OpenXR app. A `NativeActivity` subclass hosts a C++ render loop; a small Java
`Bridge` gives native code access to Android APIs.

## Frame

```
App::frame()
  xrWaitFrame / xrBeginFrame
  locate head, set the world anchor (first frame and on recenter)
  HandTracking::update      joints, aim pose, pinch, skinned mesh
  PointerSystem::update     hands -> ray / poke pointers -> per-panel pointer samples
  Shell::update             animation, status polling, light dismiss, UI sounds
  Shell::renderPanels       redraw only panels that are dirty
  layers = [ wallpaper, panels (far to near), hand overlay ]
  xrEndFrame
```

## Why layers

Every UI surface is a `Panel` with its own swapchain, submitted as a compositor **cylinder** (or quad)
layer. The compositor samples the texture directly, so text stays sharp instead of being resampled through
the eye buffers. Panels redraw only when dirty (hover change, animation, data change); an idle shell costs
almost nothing.

Hands, rays and cursors need to appear *in front of* the UI, but compositor layers do not depth-test against
each other. They are rendered into a separate transparent projection layer that is submitted last.

## World model

The shell sits on a cylinder of radius 2 m around an **anchor** (the user's head position and yaw at
launch, refreshed on recenter). A panel is placed with `(yaw, centerY)` on that cylinder. Panels can also be
`free`: a flat quad at an arbitrary pose, which is the basis for floating windows.

Panel texture density is about 800 px/m, which is roughly 1.4x oversampled at 2 m on a Quest 2.

## Rendering

* `Canvas` batches SDF rounded rectangles, strokes, soft shadows, icon quads and text into one draw call per
  texture. Colours are specified in sRGB and linearised in the shader for the sRGB swapchain.
* Text and glyphs are rasterised by Android (`Paint`/`Canvas` into `ALPHA_8` bitmaps) and cached as `R8`
  textures, so Unicode, font fallback and shaping come for free.
* App icons are decoded on a worker thread and uploaded a couple per frame.
* `Overlay` draws the hand mesh (CPU-skinned from `XR_FB_hand_tracking_mesh`), joint skeleton fallback,
  pointer rays and cursors, with 4x MSAA via `GL_EXT_multisampled_render_to_texture`.

## UI

`Ui` is immediate mode. A panel's draw callback calls `ui.widget(id, rect)` while it draws and gets
hover / press / click state for up to two pointers (one per hand), plus smoothed hover and press
animations. `scrollBegin/End` provides drag-to-scroll with momentum; a drag steals the press from buttons
underneath so scrolling never triggers a click.

## Java bridge

`Bridge` (static methods, called from native over JNI): installed apps, icons, text rasterisation,
launching, battery/Wi-Fi/time, preferences, UI sounds, and the debug command queue. Local references are
scoped per call because the native thread never returns to Java.
