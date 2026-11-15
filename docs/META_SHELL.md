# Replacing Meta's shell

The goal is for Plasma XR to *be* the headset's shell: what you see at boot, on the Home button and when
you pull up a menu is ours. This page records how Horizon OS is put together, what has been changed on the
device so far, and how to undo every change.

## How Horizon OS shows its UI

| Piece | What it does | Can we remove it? |
| --- | --- | --- |
| `com.oculus.vrshell` | Home environment, and the panel apps that make up the HUD: control bar, display bar, library. It is what Android resolves as **Home** (`com.oculus.vrshell/.HomeActivity`). | Yes, once we replace it (stage 4) |
| `com.oculus.systemux`, `com.meta.systemui` | Universal menu (Meta button), quick settings, notifications | Yes, once we replace them (stage 4) |
| `com.oculus.panelapp.*` | Library, settings, calendar, pairing, kiosk panels hosted by vrshell | Yes |
| `com.oculus.vrruntimeservice` | The OpenXR runtime and compositor. Draws the boundary, passthrough and tracking-lost prompts. | **No.** Apps cannot render without it. |
| `com.oculus.vrguardianservice` | Boundary (Guardian) | Pause only (`debug.oculus.guardian_pause`) |
| `com.oculus.horizon` and friends | Platform service, identity, entitlements, device auth | No |

Our own app is already the foreground immersive app while it runs; vrshell sits underneath it. Replacing the
HUD means taking over Home and the Meta-button menu, and providing our own versions of everything those
panels do.

## Stages

| Stage | What | Status |
| --- | --- | --- |
| 0 | Reversible toolkit: `tools/device/meta.sh`, package groups in `tools/device/meta-groups.txt` | done |
| 1 | Disable the `optional` group (store, guidebook, gallery, media player, assistant, telemetry, ...) | done: 13 packages, about 130 MB more free RAM |
| 2 | Plasma XR as the default Home app | done (see below) |
| 3 | Our own replacements for system functions: Wi-Fi, volume, brightness, boundary, power menu, notifications, keyboard | needs a privileged channel (see below) |
| 4 | Disable the `hud` group (vrshell panels, universal menu, system UI) | after 3 |

Stages 1 to 4 only ever use `pm disable-user`, which Android stores per package and which survives reboots.
Nothing is deleted or modified on the system partition.

## Root, and how Home was switched

Magisk is installed on this headset, and it has granted root to the **Plasma XR app** (not to the adb
shell, which Magisk refuses). While a debug build is installed, the Mac can borrow that:

```sh
./tools/device/root.sh 'id'            # runs a command as root, through the app
```

The normal route to a different Home app, the HOME role, rejects Plasma XR on this firmware ("package does not
qualify", with no reason logged) and has no bypass for HOME. Android's Home is simply the highest-priority
enabled `HOME` activity, and there are only five of them: vrshell's `HomeActivity` (priority 3), ours (0) and
three of Meta's at -10 or lower. So Home was switched by disabling **only** vrshell's `HomeActivity`
component, which leaves the rest of vrshell (control bar, panels) running:

```sh
./tools/device/root.sh 'pm disable com.oculus.vrshell/.HomeActivity'    # Plasma XR is Home
./tools/device/root.sh 'pm enable  com.oculus.vrshell/.HomeActivity'    # Meta's Home is back
```

`HomeAlias` has to carry the same `android.app.lib_name` meta-data as `PlasmaActivity`: `NativeActivity` reads
it from the component that was launched, and without it the app crashes at start and Android relaunches Home
in a loop.

## Where Meta's HUD comes from

* Opening a normal 2D app: vrshell starts the app and then its own `ControlBarActivity`
  (category `com.oculus.intent.category.OVERLAY_LAUNCHER`). That is the bar used to move, resize and close the
  panel.
* The Meta button: `AnytimeUIActivity` in `com.oculus.systemux`. systemux also hosts `ActiveTaskBarActivity`,
  `AppMenuActivity`, `NotificationsActivity` and the library.
* How the Meta button reaches systemux has not been traced yet. Until Plasma XR can get you back from inside
  another app, these stay enabled.

## Undoing it

```sh
./tools/device/meta.sh status          # what is disabled, and free RAM
./tools/device/meta.sh restore         # re-enable everything this tool disabled
./tools/device/meta.sh restore hud     # or just one group / package
```

If the headset ends up with no usable UI, nothing needs the headset itself to recover:

* Wireless adb survives reboots (`persist.adb.tcp.port` is 5555) and USB adb is enabled, so run `restore`
  from the Mac.
* To make Meta's Home the default again:
  `adb shell cmd package set-home-activity com.oculus.vrshell/.HomeActivity`
  and `adb shell pm disable-user org.plasmaxr.launcher/.HomeAlias`.

The record of what was disabled lives in `debug-out/meta-disabled.txt`. It is git-ignored because it
describes one device.

## Privileged access for system controls

Apps cannot change Wi-Fi, brightness, the boundary or power state on their own. Options, in order of power:

1. **adb shell privileges** (what stages 0 to 2 use). Works from the Mac only.
2. **Shizuku.** Already installed on this headset but not started. It lets an app call system APIs with
   shell privileges. It has to be started over adb after every reboot unless the device has root.
3. **Root through Magisk.** Magisk is installed, and currently denies `su` to the adb shell. Granting it
   once in the Magisk app removes the restart problem and allows stage 4 to be made permanent and quiet.

Which one to use is a decision for stage 3.
