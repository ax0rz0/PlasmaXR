# Linux apps

Plasma XR runs ordinary Linux programs next to its own apps. Each program's window becomes a shell window
that can be docked on the desktop or floated in space, with the same title bar, move and resize as every
other window.

## How it works

```
Debian arm64 rootfs --proot--> Linux program  --Wayland socket-->  compositor in the shell
 (files/linux/rootfs)          (no root needed)                     (app/src/main/cpp/wayland)
                                                                         |
                                       pixels -> GL texture -> window panel (XR swapchain layer)
                                       pointer and keyboard <- hands / controllers / key events
```

* **proot** (Termux's build) runs the Debian userland as the app's own user. It fakes root so `apt` works,
  and needs no kernel support beyond `ptrace`.
* **The compositor** (`app/src/main/cpp/wayland`) speaks the Wayland wire protocol itself, without
  libwayland. The protocol tables are generated from the official XML by `tools/wayland/gen.py`.
  It implements wl_compositor, subcompositor, shm, output, seat (pointer + keyboard), data device
  (clipboard), xdg-shell with popups and positioners, and xdg-decoration (so clients leave the window frame
  to the shell).
* **Scale.** Clients are told the output is scale 2, so their text is rendered at twice the density and stays
  sharp in the headset. `linuxscale <1-3>` changes it.
* **Windows.** `LinuxHost` (`shell/linux_host.cpp`) opens a window per toplevel, keeps the client's size in
  step with the window, sends keyboard focus with window focus and forwards pointer input.

## Installing it on a headset

```sh
./tools/linux/fetch-proot.sh      # proot from Termux's repository -> build/linux/proot
./tools/linux/build-rootfs.sh     # Debian rootfs via Docker/Colima -> build/linux/rootfs.tar.gz
./tools/linux/install-device.sh   # pushes both and unpacks them into the app's data
```

The app must be the debug build (`run-as` is used to unpack as the app's user). The rootfs is about
600 MB unpacked. Extra packages: `EXTRA="konsole dolphin" ./tools/linux/build-rootfs.sh`.

Linux programs found in the rootfs's `.desktop` files appear in Kickoff. From the Mac:

```sh
./tools/linux/run.sh 'foot --version'          # run a command in the guest as the app's user
./scripts/debug.sh "linux foot"                # start a program in a shell window
./scripts/debug.sh "wltype echo hello"         # type into the focused Linux window
./scripts/debug.sh "wltrace 1"                 # log every Wayland message
```

## Testing the compositor without a headset

`tools/wayland/host_test.cpp` builds the same compositor for Linux and runs it against real clients in a
container, writing screenshots:

```sh
./tools/wayland/test.sh              # all scenarios in tools/wayland/scenarios
./tools/wayland/test.sh 03_foot      # one of them; output in build/wayland-test/
```

## Known limits

* No GPU acceleration: clients render in software and hand the compositor shared-memory buffers.
* No XWayland yet, so X11-only programs do not work.
* wl_seat has no touch, and there is no text-input (IME) or primary selection.
* Android's seccomp filter and the 4.19 kernel are the usual suspects when something crashes under proot;
  `tools/linux/run.sh` reproduces a launch with its output visible.
