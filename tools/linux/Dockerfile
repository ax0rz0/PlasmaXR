# Debian arm64 userland for Plasma XR's Linux apps. It is unpacked on the headset and run under
# proot, so nothing here needs systemd, a kernel of its own, or root.
#
#   ./tools/linux/build-rootfs.sh            # builds build/linux/rootfs.tar.gz
#   EXTRA="konsole dolphin" ./tools/linux/build-rootfs.sh
FROM --platform=linux/arm64 debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive
ARG EXTRA=""

# Wayland clients for testing (foot, weston demos, wev), XWayland for X11 apps, and fonts/keymaps
# every toolkit expects. EXTRA adds more packages, e.g. KDE apps.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates \
      foot foot-terminfo \
      weston \
      wev \
      xwayland xterm x11-apps \
      libgl1-mesa-dri \
      xkb-data libxkbcommon0 \
      fonts-dejavu-core fontconfig \
      dbus-x11 procps less nano curl \
      ${EXTRA} \
 && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*

# Runtime directory the compositor's socket is bind-mounted into.
RUN mkdir -p /run/user/1000 /root \
 && chmod 700 /run/user/1000 \
 && echo 'plasmaxr' > /etc/hostname \
 && printf '127.0.0.1 localhost plasmaxr\n::1 localhost\n' > /etc/hosts \
 && printf 'nameserver 1.1.1.1\nnameserver 8.8.8.8\n' > /etc/resolv.conf

ENV HOME=/root \
    XDG_RUNTIME_DIR=/run/user/1000 \
    WAYLAND_DISPLAY=wayland-0 \
    LANG=C.UTF-8 \
    LIBGL_ALWAYS_SOFTWARE=1 \
    MOZ_ENABLE_WAYLAND=1 \
    QT_QPA_PLATFORM=wayland \
    GDK_BACKEND=wayland
