#!/system/bin/sh
# Device-side helper for tools/linux/run.sh: runs a command inside the Debian rootfs exactly like
# LinuxRuntime.java does, as the app's user (it is started through `run-as`).
R=/data/data/org.plasmaxr.launcher/files/linux
export LD_LIBRARY_PATH=$R/lib PROOT_LOADER=$R/libexec/loader PROOT_TMP_DIR=$R/tmp
: "${PROOT_NO_SECCOMP=1}"
export PROOT_NO_SECCOMP
exec $R/bin/proot -0 --link2symlink -r $R/rootfs \
    -b /dev -b /proc -b /sys -b $R/run:/run/user/1000 -b $R/shm:/dev/shm \
    -w /root /usr/bin/env -i HOME=/root USER=root LOGNAME=root SHELL=/bin/bash \
    PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games \
    LANG=C.UTF-8 TERM=xterm-256color XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
    XDG_SESSION_TYPE=wayland GDK_BACKEND=wayland QT_QPA_PLATFORM=wayland MOZ_ENABLE_WAYLAND=1 \
    SDL_VIDEODRIVER=wayland LIBGL_ALWAYS_SOFTWARE=1 NO_AT_BRIDGE=1 GTK_A11Y=none "$@"
