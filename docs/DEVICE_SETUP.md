# Device setup

## Prerequisites

* A Quest with developer mode enabled and `adb` authorised (USB debugging prompt accepted in the headset).
* OpenXR hand tracking turned on in the headset's settings (Movement tracking). Plasma XR works with
  whatever the runtime reports; it does not need an account or any Meta service beyond the OS itself.

## Install and launch

```sh
./scripts/run.sh
# or by hand
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.plasmaxr.launcher/.PlasmaActivity
```

It also appears in the headset's library under *Unknown sources*.

## Wireless adb (recommended)

The Quest has one USB-C port, so a USB cable and a charger compete for it. A weak USB data port
(500 mA) cannot keep up with the headset while it renders, and the battery drains even though it is
"charging". Charge with a proper USB-PD adapter and use `adb` over Wi-Fi instead:

```sh
adb tcpip 5555                      # once, while connected by USB
adb connect <headset-ip>:5555       # then unplug the cable and attach the charger
```

The Wi-Fi radio is off while the headset sleeps, so the connection drops when it does; wake the
headset and reconnect. Check the charger actually negotiated:

```sh
adb shell dumpsys battery           # want: AC powered: true, Weak Charger: false
```

On recent macOS, if `adb connect` fails with "No route to host" while `ping` works, restart the server
(`adb kill-server`) or grant your terminal the Local Network permission.

## Making Plasma XR the Home app (optional)

The manifest ships a **disabled** `HomeAlias`. Nothing changes until you enable it:

```sh
adb shell pm enable org.plasmaxr.launcher/.HomeAlias
```

Horizon OS decides what its own Oculus button and Home do, so check how your headset behaves before
relying on this. To undo it:

```sh
adb shell pm disable org.plasmaxr.launcher/.HomeAlias
adb shell cmd package set-home-activity com.oculus.vrshell/.HomeActivity
```

Plasma XR's *Leave > Meta Home* entry always takes you back to the system Home.

## Uninstall

```sh
adb uninstall org.plasmaxr.launcher
```
