package org.plasmaxr.launcher;

import android.app.NativeActivity;
import android.os.Bundle;

/**
 * Thin NativeActivity host. All rendering, hand tracking and UI live in the native library
 * ("plasmaxr"); the Java side only exists to give native code access to Android APIs
 * (installed apps, icons, text rasterisation, launching intents) through {@link Bridge}.
 */
public class PlasmaActivity extends NativeActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // The native thread starts inside super.onCreate() and may call into Bridge right away.
        Bridge.attach(this);
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onDestroy() {
        Bridge.detach(this);
        super.onDestroy();
    }
}
