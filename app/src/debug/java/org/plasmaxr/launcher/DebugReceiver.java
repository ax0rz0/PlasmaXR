package org.plasmaxr.launcher;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * Debug-only entry point: {@code adb shell am broadcast -n org.plasmaxr.launcher/.DebugReceiver
 * --es cmd "snap"}. Commands are queued for the native render loop to consume.
 */
public class DebugReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        Bridge.postDebugCommand(intent.getStringExtra("cmd"));
    }
}
