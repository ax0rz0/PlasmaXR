package org.plasmaxr.launcher;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.graphics.drawable.AdaptiveIconDrawable;
import android.graphics.drawable.Drawable;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.BatteryManager;
import android.text.TextPaint;
import android.text.TextUtils;
import android.text.format.DateFormat;
import android.util.Log;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentLinkedQueue;

/**
 * Static helpers called from the native layer over JNI. Every method here is safe to call from
 * the native render thread. Keep signatures in sync with cpp/platform/jni_bridge.cpp.
 */
public final class Bridge {
    private static final String TAG = "PlasmaXR";
    /** Separator for packed records returned to native code (ASCII unit separator). */
    private static final String SEP = "\u001f";

    private static volatile Activity activity;
    private static final ConcurrentLinkedQueue<String> debugCommands = new ConcurrentLinkedQueue<>();

    private Bridge() {}

    static void attach(Activity a) {
        activity = a;
        Sounds.warmUp();
    }

    static void detach(Activity a) {
        if (activity == a) activity = null;
    }

    private static Context ctx() {
        Activity a = activity;
        return a != null ? a : null;
    }

    // ---------------------------------------------------------------------------------------
    // Debug command channel (fed by the debug-only DebugReceiver)
    // ---------------------------------------------------------------------------------------

    public static void postDebugCommand(String cmd) {
        if (cmd != null) debugCommands.add(cmd);
    }

    public static String pollDebugCommand() {
        return debugCommands.poll();
    }

    // ---------------------------------------------------------------------------------------
    // Linux apps (Debian rootfs under proot, see LinuxRuntime)
    // ---------------------------------------------------------------------------------------

    public static String linuxRoot() {
        Context c = ctx();
        return c == null ? "" : LinuxRuntime.get(c).root().getAbsolutePath();
    }

    public static String linuxSocketDir() {
        Context c = ctx();
        return c == null ? "" : LinuxRuntime.get(c).socketDir();
    }

    public static boolean linuxInstalled() {
        Context c = ctx();
        return c != null && LinuxRuntime.get(c).installed();
    }

    public static boolean launchLinux(String command) {
        Context c = ctx();
        return c != null && LinuxRuntime.get(c).launch(command);
    }

    // ---------------------------------------------------------------------------------------
    // Installed apps
    // ---------------------------------------------------------------------------------------

    /**
     * Returns one record per launchable app: package, activity class, label, flags.
     * flags: bit0 = immersive VR app, bit1 = system app.
     */
    public static String[] listApps() {
        Context c = ctx();
        if (c == null) return new String[0];
        PackageManager pm = c.getPackageManager();
        Map<String, String[]> byPackage = new HashMap<>();

        String[] categories = {
            Intent.CATEGORY_LAUNCHER,
            "com.oculus.intent.category.VR",
            "org.khronos.openxr.intent.category.IMMERSIVE_HMD",
        };
        for (int i = 0; i < categories.length; i++) {
            Intent q = new Intent(Intent.ACTION_MAIN);
            q.addCategory(categories[i]);
            List<ResolveInfo> found;
            try {
                found = pm.queryIntentActivities(q, 0);
            } catch (Exception e) {
                Log.w(TAG, "queryIntentActivities failed", e);
                continue;
            }
            for (ResolveInfo ri : found) {
                if (ri.activityInfo == null) continue;
                String pkg = ri.activityInfo.packageName;
                String[] rec = byPackage.get(pkg);
                boolean immersive = i > 0;
                if (rec == null) {
                    CharSequence label = ri.loadLabel(pm);
                    int flags = 0;
                    if ((ri.activityInfo.applicationInfo.flags & ApplicationInfo.FLAG_SYSTEM) != 0) {
                        flags |= 2;
                    }
                    rec = new String[] {
                        pkg, ri.activityInfo.name, label != null ? label.toString() : pkg,
                        Integer.toString(flags | (immersive ? 1 : 0))
                    };
                    byPackage.put(pkg, rec);
                } else if (immersive) {
                    rec[3] = Integer.toString(Integer.parseInt(rec[3]) | 1);
                }
            }
        }
        List<String[]> all = new ArrayList<>(byPackage.values());
        Collections.sort(all, new Comparator<String[]>() {
            @Override
            public int compare(String[] a, String[] b) {
                return a[2].compareToIgnoreCase(b[2]);
            }
        });
        String[] out = new String[all.size()];
        for (int i = 0; i < out.length; i++) {
            String[] r = all.get(i);
            out[i] = r[0] + SEP + r[1] + SEP + r[2] + SEP + r[3];
        }
        return out;
    }

    /** Square icon with a rounded-rect mask, ARGB_8888 (premultiplied). */
    public static Bitmap loadAppIcon(String pkg, String activityName, int size) {
        Context c = ctx();
        if (c == null) return null;
        PackageManager pm = c.getPackageManager();
        Drawable d;
        try {
            if (activityName != null && !activityName.isEmpty()) {
                d = pm.getActivityIcon(new ComponentName(pkg, activityName));
            } else {
                d = pm.getApplicationIcon(pkg);
            }
        } catch (Exception e) {
            return null;
        }
        if (d == null) return null;
        Bitmap out = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas cv = new Canvas(out);
        Path clip = new Path();
        float r = size * 0.22f;
        clip.addRoundRect(0, 0, size, size, r, r, Path.Direction.CW);
        cv.clipPath(clip);
        try {
            if (d instanceof AdaptiveIconDrawable) {
                // Adaptive layers are 108dp with a 72dp visible window: scale so the window fills the tile.
                AdaptiveIconDrawable a = (AdaptiveIconDrawable) d;
                int full = Math.round(size * 108f / 72f);
                int off = (full - size) / 2;
                Drawable bg = a.getBackground();
                Drawable fg = a.getForeground();
                if (bg != null) {
                    bg.setBounds(-off, -off, full - off, full - off);
                    bg.draw(cv);
                }
                if (fg != null) {
                    fg.setBounds(-off, -off, full - off, full - off);
                    fg.draw(cv);
                }
            } else {
                d.setBounds(0, 0, size, size);
                d.draw(cv);
            }
        } catch (Exception e) {
            Log.w(TAG, "icon draw failed for " + pkg, e);
            return null;
        }
        return out;
    }

    public static boolean launch(String pkg, String activityName) {
        Activity a = activity;
        if (a == null) return false;
        try {
            Intent i;
            if (activityName != null && !activityName.isEmpty()) {
                i = new Intent(Intent.ACTION_MAIN);
                i.addCategory(Intent.CATEGORY_LAUNCHER);
                i.setComponent(new ComponentName(pkg, activityName));
            } else {
                i = a.getPackageManager().getLaunchIntentForPackage(pkg);
            }
            if (i == null) return false;
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
            a.startActivity(i);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "launch failed for " + pkg, e);
            return false;
        }
    }

    // ---------------------------------------------------------------------------------------
    // Text and glyph rasterisation (A8 coverage masks, tinted on the GPU)
    // ---------------------------------------------------------------------------------------

    private static final Map<Integer, TextPaint> paints = new HashMap<>();

    private static TextPaint paintFor(float size, int weight) {
        int key = Math.round(size * 4) * 1000 + weight;
        TextPaint p = paints.get(key);
        if (p == null) {
            p = new TextPaint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
            p.setTypeface(Typeface.create(Typeface.DEFAULT, weight, false));
            p.setTextSize(size);
            p.setColor(0xFFFFFFFF);
            paints.put(key, p);
        }
        return p;
    }

    /** Single-line text as an ALPHA_8 bitmap; width is clamped to maxWidth (0 = unlimited). */
    public static synchronized Bitmap renderText(String text, float size, int weight, int maxWidth) {
        TextPaint p = paintFor(size, weight);
        final int pad = 2;
        CharSequence s = text;
        float w = p.measureText(text, 0, text.length());
        if (maxWidth > 0 && w > maxWidth - 2 * pad) {
            s = TextUtils.ellipsize(text, p, Math.max(1, maxWidth - 2 * pad), TextUtils.TruncateAt.END);
            w = p.measureText(s, 0, s.length());
        }
        Paint.FontMetrics fm = p.getFontMetrics();
        int bw = Math.max(1, (int) Math.ceil(w) + pad * 2);
        int bh = Math.max(1, (int) Math.ceil(fm.descent - fm.ascent) + pad * 2);
        Bitmap bmp = Bitmap.createBitmap(bw, bh, Bitmap.Config.ALPHA_8);
        Canvas cv = new Canvas(bmp);
        cv.drawText(s, 0, s.length(), pad, pad - fm.ascent, p);
        return bmp;
    }

    public static Bitmap renderGlyph(String name, int size) {
        Bitmap bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ALPHA_8);
        Glyphs.draw(name, new Canvas(bmp), size);
        return bmp;
    }

    // ---------------------------------------------------------------------------------------
    // Debug: panel snapshots
    // ---------------------------------------------------------------------------------------

    /**
     * Writes premultiplied RGBA8 pixels to {@code <externalFilesDir>/snap/<name>.png}, composited
     * over a dark backdrop so translucent panels are legible when viewed on a desktop.
     */
    public static boolean savePng(String name, int w, int h, byte[] rgba) {
        Context c = ctx();
        if (c == null || rgba == null || rgba.length < w * h * 4) return false;
        try {
            java.io.File dir = new java.io.File(c.getExternalFilesDir(null), "snap");
            if (!dir.exists() && !dir.mkdirs()) return false;
            int[] px = new int[w * h];
            for (int i = 0; i < px.length; i++) {
                int r = rgba[i * 4] & 0xff, g = rgba[i * 4 + 1] & 0xff;
                int b = rgba[i * 4 + 2] & 0xff, a = rgba[i * 4 + 3] & 0xff;
                // Backdrop roughly matches the shell wallpaper; source is premultiplied.
                int inv = 255 - a;
                int or = Math.min(255, r + 14 * inv / 255);
                int og = Math.min(255, g + 40 * inv / 255);
                int ob = Math.min(255, b + 72 * inv / 255);
                px[i] = 0xFF000000 | (or << 16) | (og << 8) | ob;
            }
            Bitmap bmp = Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888);
            java.io.File out = new java.io.File(dir, name + ".png");
            try (java.io.FileOutputStream fo = new java.io.FileOutputStream(out)) {
                bmp.compress(Bitmap.CompressFormat.PNG, 100, fo);
            }
            Log.i(TAG, "snapshot written: " + out.getAbsolutePath());
            return true;
        } catch (Exception e) {
            Log.w(TAG, "savePng failed", e);
            return false;
        }
    }

    // ---------------------------------------------------------------------------------------
    // System status
    // ---------------------------------------------------------------------------------------

    /** "battery|charging|wifi|time|date" with `|` as SEP. */
    public static String status() {
        Context c = ctx();
        if (c == null) return "";
        int battery = -1;
        boolean charging = false;
        try {
            Intent b = c.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (b != null) {
                int level = b.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
                int scale = b.getIntExtra(BatteryManager.EXTRA_SCALE, 100);
                if (level >= 0 && scale > 0) battery = Math.round(level * 100f / scale);
                int st = b.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
                charging = st == BatteryManager.BATTERY_STATUS_CHARGING
                        || st == BatteryManager.BATTERY_STATUS_FULL;
            }
        } catch (Exception ignored) {
        }
        boolean wifi = false;
        try {
            ConnectivityManager cm = (ConnectivityManager) c.getSystemService(Context.CONNECTIVITY_SERVICE);
            Network n = cm.getActiveNetwork();
            if (n != null) {
                NetworkCapabilities caps = cm.getNetworkCapabilities(n);
                wifi = caps != null && caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI);
            }
        } catch (Exception ignored) {
        }
        Date now = new Date();
        String time = DateFormat.getTimeFormat(c).format(now);
        String date = new java.text.SimpleDateFormat("EEE d MMM", Locale.getDefault()).format(now);
        return battery + SEP + (charging ? "1" : "0") + SEP + (wifi ? "1" : "0") + SEP + time + SEP + date;
    }

    // ---------------------------------------------------------------------------------------
    // Preferences
    // ---------------------------------------------------------------------------------------

    private static SharedPreferences prefs() {
        Context c = ctx();
        return c == null ? null : c.getSharedPreferences("plasmaxr", Context.MODE_PRIVATE);
    }

    public static String getPref(String key, String def) {
        SharedPreferences p = prefs();
        return p == null ? def : p.getString(key, def);
    }

    public static void setPref(String key, String value) {
        SharedPreferences p = prefs();
        if (p != null) p.edit().putString(key, value).apply();
    }

    // ---------------------------------------------------------------------------------------
    // UI sounds: hands have no haptics, so audio is the feedback channel.
    // ---------------------------------------------------------------------------------------

    public static void playSound(int id) {
        Sounds.play(id);
    }
}
