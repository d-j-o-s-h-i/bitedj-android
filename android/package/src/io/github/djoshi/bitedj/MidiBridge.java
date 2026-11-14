package io.github.djoshi.bitedj;

import android.content.Context;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiManager;
import android.os.Handler;
import android.os.HandlerThread;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

public final class MidiBridge {
    private MidiBridge() {}
    private static final HandlerThread CALLBACKS = new HandlerThread("BiteDJ MIDI open");
    static { CALLBACKS.start(); }
    private static final Handler HANDLER = new Handler(CALLBACKS.getLooper());

    // Called only on BiteDJ's controller management thread, never its audio/UI
    // thread. MidiManager's async callback uses an independent HandlerThread.
    public static MidiDevice openDevice(Context context, int id) {
        MidiManager manager = context.getSystemService(MidiManager.class);
        if (manager == null) return null;
        for (MidiDeviceInfo info : manager.getDevicesForTransport(MidiManager.TRANSPORT_MIDI_BYTE_STREAM)) {
            if (info.getId() != id || info.getType() != MidiDeviceInfo.TYPE_USB) continue;
            CompletableFuture<MidiDevice> opened = new CompletableFuture<>();
            try {
                manager.openDevice(info, device -> {
                    if (!opened.complete(device) && device != null) closeDevice(device);
                }, HANDLER);
                return opened.get(3, TimeUnit.SECONDS);
            } catch (Exception e) {
                opened.complete(null);
                MidiDevice late = opened.getNow(null);
                if (late != null) closeDevice(late);
                if (e instanceof InterruptedException) Thread.currentThread().interrupt();
                android.util.Log.e("BiteDJ", "MIDI open failed", e);
                return null;
            }
        }
        return null;
    }

    public static void closeDevice(MidiDevice device) {
        if (device == null) return;
        try { device.close(); }
        catch (java.io.IOException e) { android.util.Log.w("BiteDJ", "MIDI close failed", e); }
    }
}
