package org.plasmaxr.launcher;

import android.content.Context;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Runs Linux programs from a Debian arm64 root filesystem under proot. They talk to the Wayland
 * compositor inside the shell through a socket in {@code <files>/linux/run}.
 *
 * <pre>
 *   files/linux/bin/proot            static-ish proot from Termux (see tools/linux)
 *   files/linux/lib, libexec         its libraries and loader
 *   files/linux/rootfs               the unpacked Debian tree
 *   files/linux/run                  becomes /run/user/1000 in the guest (Wayland socket)
 *   files/linux/shm                  becomes /dev/shm
 * </pre>
 *
 * tools/linux/install-device.sh puts these files in place.
 */
final class LinuxRuntime {
    private static final String TAG = "PlasmaXR/linux";
    private static LinuxRuntime instance;

    private final File root;
    private int launches;

    private LinuxRuntime(Context c) {
        root = new File(c.getFilesDir(), "linux");
    }

    static synchronized LinuxRuntime get(Context c) {
        if (instance == null) instance = new LinuxRuntime(c.getApplicationContext());
        return instance;
    }

    File root() {
        return root;
    }

    boolean installed() {
        return new File(root, "bin/proot").exists() && new File(root, "rootfs/usr/bin/env").exists();
    }

    /** Directory for the Wayland socket; created on demand. */
    String socketDir() {
        File d = new File(root, "run");
        if (!d.exists() && !d.mkdirs()) Log.w(TAG, "cannot create " + d);
        return d.getAbsolutePath();
    }

    /** Starts {@code command} (a shell command line) in the rootfs. Returns false if it cannot start. */
    boolean launch(String command) {
        if (!installed()) {
            Log.w(TAG, "Linux environment is not installed in " + root);
            return false;
        }
        final String rootPath = root.getAbsolutePath();
        new File(root, "tmp").mkdirs();
        new File(root, "shm").mkdirs();
        socketDir();

        List<String> cmd = new ArrayList<>();
        cmd.add(rootPath + "/bin/proot");
        cmd.add("-0");  // fake root: dpkg, apt and many launchers expect to be uid 0
        cmd.add("--link2symlink");
        cmd.add("-r");
        cmd.add(rootPath + "/rootfs");
        for (String b : new String[] {"/dev", "/proc", "/sys"}) {
            cmd.add("-b");
            cmd.add(b);
        }
        cmd.add("-b");
        cmd.add(rootPath + "/run:/run/user/1000");
        cmd.add("-b");
        cmd.add(rootPath + "/shm:/dev/shm");
        cmd.add("-w");
        cmd.add("/root");
        cmd.add("/usr/bin/env");
        cmd.add("-i");
        String[] guestEnv = {
            "HOME=/root", "USER=root", "LOGNAME=root", "SHELL=/bin/bash",
            "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games",
            "LANG=C.UTF-8", "TERM=xterm-256color", "XDG_RUNTIME_DIR=/run/user/1000",
            "WAYLAND_DISPLAY=wayland-0", "XDG_SESSION_TYPE=wayland",
            "GDK_BACKEND=wayland", "QT_QPA_PLATFORM=wayland", "MOZ_ENABLE_WAYLAND=1",
            "SDL_VIDEODRIVER=wayland", "LIBGL_ALWAYS_SOFTWARE=1",
            // Android has no D-Bus session; keep toolkits from waiting for one.
            "NO_AT_BRIDGE=1", "GTK_A11Y=none",
        };
        for (String e : guestEnv) cmd.add(e);
        cmd.add("/bin/sh");
        cmd.add("-c");
        cmd.add(command);

        ProcessBuilder pb = new ProcessBuilder(cmd);
        pb.directory(root);
        pb.redirectErrorStream(true);
        Map<String, String> env = pb.environment();
        env.put("LD_LIBRARY_PATH", rootPath + "/lib");
        env.put("PROOT_LOADER", rootPath + "/libexec/loader");
        env.put("PROOT_TMP_DIR", rootPath + "/tmp");
        env.put("PROOT_NO_SECCOMP", "1");

        final int id = ++launches;
        try {
            final Process p = pb.start();
            Log.i(TAG, "[" + id + "] started: " + command);
            Thread t = new Thread(() -> pump(id, p), "linux-" + id);
            t.setDaemon(true);
            t.start();
            return true;
        } catch (IOException e) {
            Log.e(TAG, "[" + id + "] could not start proot", e);
            return false;
        }
    }

    private static void pump(int id, Process p) {
        try (BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()))) {
            String line;
            while ((line = r.readLine()) != null) Log.i(TAG, "[" + id + "] " + line);
        } catch (IOException ignored) {
        }
        try {
            Log.i(TAG, "[" + id + "] exited with " + p.waitFor());
        } catch (InterruptedException ignored) {
        }
    }
}
