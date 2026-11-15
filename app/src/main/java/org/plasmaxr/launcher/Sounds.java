package org.plasmaxr.launcher;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Procedurally generated UI sounds. Hand tracking has no haptics, so short audible ticks and
 * clicks are the main confirmation that a pinch or poke registered.
 *
 * All audio work runs on its own thread: starting an AudioTrack takes several milliseconds, which
 * is a couple of dropped frames if done on the render thread.
 */
final class Sounds {
    static final int HOVER = 0;
    static final int PRESS = 1;
    static final int RELEASE = 2;
    static final int OPEN = 3;
    static final int CLOSE = 4;

    private static final int RATE = 44100;
    private static final AudioTrack[] tracks = new AudioTrack[5];
    private static final ExecutorService audio = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "plasmaxr-audio");
        t.setDaemon(true);
        return t;
    });
    private static boolean failed = false;

    private Sounds() {}

    private static short[] tone(double f0, double f1, double ms, double amp, double decay) {
        int n = (int) (RATE * ms / 1000.0);
        short[] pcm = new short[n];
        double phase = 0;
        for (int i = 0; i < n; i++) {
            double t = i / (double) n;
            double f = f0 + (f1 - f0) * t;
            phase += 2 * Math.PI * f / RATE;
            double env = Math.exp(-decay * t) * Math.min(1.0, i / (RATE * 0.002));
            pcm[i] = (short) (Math.sin(phase) * env * amp * 32767);
        }
        return pcm;
    }

    private static short[] build(int id) {
        switch (id) {
            case HOVER: return tone(2600, 2400, 14, 0.07, 5.0);
            case PRESS: return tone(1000, 620, 45, 0.20, 4.0);
            case RELEASE: return tone(1500, 1700, 38, 0.16, 4.5);
            case OPEN: return tone(500, 900, 120, 0.14, 3.0);
            default: return tone(900, 450, 110, 0.14, 3.0);
        }
    }

    // Audio thread only.
    private static AudioTrack track(int id) {
        if (failed) return null;
        if (tracks[id] == null) {
            try {
                short[] pcm = build(id);
                AudioTrack t = new AudioTrack.Builder()
                        .setAudioAttributes(new AudioAttributes.Builder()
                                .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
                                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                                .build())
                        .setAudioFormat(new AudioFormat.Builder()
                                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                .setSampleRate(RATE)
                                .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                                .build())
                        .setTransferMode(AudioTrack.MODE_STATIC)
                        .setBufferSizeInBytes(pcm.length * 2)
                        .build();
                t.write(pcm, 0, pcm.length);
                tracks[id] = t;
            } catch (Exception e) {
                failed = true;
                return null;
            }
        }
        return tracks[id];
    }

    /** Builds every sound ahead of time so the first hover does not pay for it. */
    static void warmUp() {
        audio.execute(() -> {
            for (int i = 0; i < tracks.length; i++) track(i);
        });
    }

    static void play(final int id) {
        if (id < 0 || id >= tracks.length) return;
        audio.execute(() -> {
            AudioTrack t = track(id);
            if (t == null) return;
            try {
                t.stop();
                t.reloadStaticData();
                t.play();
            } catch (Exception ignored) {
            }
        });
    }
}
