package io.github.djoshi.bitedj;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.database.Cursor;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiManager;
import android.net.Uri;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.DocumentsContract;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayDeque;
import java.util.HashSet;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.json.JSONArray;
import org.json.JSONObject;

public final class PlatformBridge {
    private PlatformBridge() {}
    private static final ExecutorService MEDIA_WORKER = Executors.newSingleThreadExecutor();
    private static native void nativeDocumentResult(String metadata, int ownedFd,
            long startOffset, long length, String error);

    public static String settingsPath(Context context) {
        // Keep the platform probe independent of BiteDJ's shared settings.
        if (context.getPackageName().endsWith(".probe")) {
            File external = context.getExternalFilesDir(null);
            return external == null ? "" : new File(external, ".bitedj").getAbsolutePath();
        }
        if (!(context instanceof BiteDJActivity)
                || !((BiteDJActivity) context).awaitStorageAccess()) return "";
        File root = android.os.Environment.getExternalStorageDirectory();
        File destination = new File(root, "bitedj-android");
        try {
            String skipped = SettingsMigration.migrate(destination,
                    new File(root, "Android/data/io.github.djoshi.bitedj/files/.bitedj"),
                    new File(root, "Android/data/io.github.djoshi.bitedj"));
            if (!skipped.isEmpty()) {
                ((BiteDJActivity) context).showStorageWarning(
                        "Some old folders could not be read and remain at their original location. "
                        + "Copy them manually if needed:\n" + skipped);
            }
            ((BiteDJActivity) context).markSettingsInitialized();
            return destination.getAbsolutePath();
        } catch (java.io.IOException e) {
            android.util.Log.e("BiteDJ", "Settings migration failed", e);
            ((BiteDJActivity) context).showStorageError(
                    "BiteDj Android settings could not be prepared: " + e.getMessage());
            return "";
        }
    }

    private static native void nativeRequestQuit();

    static void requestQuit() {
        nativeRequestQuit();
    }

    public static String diagnosticsPath(Context context) {
        return context.getFilesDir().getAbsolutePath();
    }

    // The recording writer uses local files. List mounted volumes through the
    // storage service: /storage itself need not be readable by the application.
    public static String storagePaths(Context context) {
        JSONArray paths = new JSONArray();
        StorageManager manager = context.getSystemService(StorageManager.class);
        if (manager != null) {
            for (StorageVolume volume : manager.getStorageVolumes()) {
                File directory = volume.getDirectory();
                if (directory != null && directory.isDirectory() && directory.canRead()) {
                    paths.put(directory.getAbsolutePath());
                }
            }
        }
        return paths.toString();
    }

    // Only immutable application resources are extracted. Original music is
    // always opened through ContentResolver, never copied into this directory.
    public static String resourcesPath(Context context) {
        File temporary = null;
        try {
            String version;
            try (InputStream input = context.getAssets().open("bitedj-android-res-version")) {
                version = new String(input.readAllBytes(), java.nio.charset.StandardCharsets.UTF_8).trim();
            }
            if (!version.matches("[0-9a-f]{64}")) throw new IllegalStateException("Invalid resource version");
            File parent = new File(context.getFilesDir(), "resources");
            if (!parent.isDirectory() && !parent.mkdirs()) throw new IllegalStateException("Resource directory unavailable");
            File destination = new File(parent, version);
            if (new File(destination, ".complete").isFile()) return destination.getAbsolutePath();
            temporary = new File(parent, version + ".partial");
            deleteTree(temporary);
            copyAssets(context, "bitedj-android-res", temporary);
            if (!new File(temporary, "skins").isDirectory() ||
                    !new File(temporary, "controllers").isDirectory()) {
                throw new IllegalStateException("Incomplete packaged resources");
            }
            if (!new File(temporary, ".complete").createNewFile()) {
                throw new IllegalStateException("Resource marker failed");
            }
            deleteTree(destination);
            if (!temporary.renameTo(destination)) throw new IllegalStateException("Resource publish failed");
            return destination.getAbsolutePath();
        } catch (Exception e) {
            android.util.Log.e("BiteDJ", "Resource extraction failed", e);
            if (temporary != null) deleteTree(temporary);
            return "";
        }
    }

    private static void copyAssets(Context context, String asset, File destination) throws java.io.IOException {
        String[] children = context.getAssets().list(asset);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs()) throw new java.io.IOException("mkdir " + destination);
            for (String name : children) copyAssets(context, asset + "/" + name, new File(destination, name));
        } else {
            try (InputStream input = context.getAssets().open(asset);
                    FileOutputStream output = new FileOutputStream(destination)) {
                input.transferTo(output);
            }
        }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        file.delete();
    }

    public static void keepScreenOn(Context context, boolean enabled) {
        if (!(context instanceof android.app.Activity)) return;
        android.app.Activity activity = (android.app.Activity) context;
        activity.runOnUiThread(() -> {
            if (enabled) activity.getWindow().addFlags(
                    android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            else activity.getWindow().clearFlags(
                    android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        });
    }

    private static JSONArray integers(int[] values) {
        JSONArray result = new JSONArray();
        for (int value : values) result.put(value);
        return result;
    }

    public static String snapshot(Context context) {
        try {
            JSONObject result = new JSONObject();
            result.put("manufacturer", android.os.Build.MANUFACTURER);
            result.put("model", android.os.Build.MODEL);
            result.put("api", android.os.Build.VERSION.SDK_INT);
            result.put("settingsPath", settingsPath(context));
            if (context instanceof android.app.Activity) {
                result.put("probeUsbFourChannel", ((android.app.Activity) context)
                        .getIntent().getBooleanExtra("probeUsbFourChannel", false));
            }
            result.put("persistedUriPermissions",
                    context.getContentResolver().getPersistedUriPermissions().size());
            JSONArray audio = new JSONArray();
            AudioManager audioManager = context.getSystemService(AudioManager.class);
            for (AudioDeviceInfo device : audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
                JSONObject info = new JSONObject();
                info.put("id", device.getId());
                info.put("type", device.getType());
                info.put("name", device.getProductName().toString());
                info.put("address", device.getAddress());
                info.put("channels", integers(device.getChannelCounts()));
                info.put("sampleRates", integers(device.getSampleRates()));
                info.put("encodings", integers(device.getEncodings()));
                audio.put(info);
            }
            result.put("audioOutputs", audio);
            JSONArray midi = new JSONArray();
            MidiManager manager = context.getSystemService(MidiManager.class);
            if (manager != null) {
                for (MidiDeviceInfo device : manager.getDevicesForTransport(
                        MidiManager.TRANSPORT_MIDI_BYTE_STREAM)) {
                    JSONObject info = new JSONObject();
                    info.put("id", device.getId());
                    info.put("type", device.getType());
                    info.put("name", device.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME, ""));
                    info.put("inputPorts", device.getInputPortCount());
                    info.put("outputPorts", device.getOutputPortCount());
                    midi.put(info);
                }
            }
            result.put("midiDevices", midi);
            return result.toString();
        } catch (Exception e) {
            return "{\"error\":\"Platform snapshot failed\"}";
        }
    }

    static void reportError(String error) {
        nativeDocumentResult("{}", -1, 0, -1, error);
    }

    // Each decoder/tag reader owns a separate original descriptor. Never copy
    // music, and retain the provider's asset bounds for sliced documents.
    public static long[] openDocument(Context context, String location) {
        try {
            Uri uri = Uri.parse(location);
            if (!"content".equals(uri.getScheme())) return null;
            try (AssetFileDescriptor asset = context.getContentResolver()
                    .openAssetFileDescriptor(uri, "r")) {
                if (asset == null) return null;
                long[] result = { -1, asset.getStartOffset(), asset.getDeclaredLength() };
                result[0] = asset.getParcelFileDescriptor().detachFd();
                return result;
            }
        } catch (Exception e) {
            android.util.Log.e("BiteDJ", "Document open failed: " + e.getClass().getSimpleName());
            return null;
        }
    }

    public static void probeMusicFolder(Context context) {
        Context app = context.getApplicationContext();
        MEDIA_WORKER.execute(() -> {
            try {
                String stored = app.getSharedPreferences("bitedj-android-media", Context.MODE_PRIVATE)
                        .getString("tree_uri", "");
                if (stored.isEmpty()) throw new IllegalStateException("Select a music folder first");
                Uri tree = Uri.parse(stored);
                ArrayDeque<String> directories = new ArrayDeque<>();
                HashSet<String> visited = new HashSet<>();
                directories.add(DocumentsContract.getTreeDocumentId(tree));
                int count = 0;
                while (!directories.isEmpty()) {
                    String parent = directories.removeFirst();
                    if (!visited.add(parent)) continue;
                    Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent);
                    String[] projection = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE,
                            DocumentsContract.Document.COLUMN_SIZE};
                    try (Cursor cursor = app.getContentResolver().query(
                            children, projection, null, null, null)) {
                        if (cursor == null) throw new IllegalStateException("Provider unavailable");
                        while (cursor.moveToNext()) {
                            if (++count > 10000) throw new IllegalStateException("Probe scan limit: 10000 documents");
                            String id = cursor.getString(0);
                            String name = cursor.getString(1);
                            String mime = cursor.getString(2);
                            if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                                directories.add(id);
                            } else if ("audio/mpeg".equals(mime) ||
                                    (name != null && name.toLowerCase(java.util.Locale.ROOT).endsWith(".mp3"))) {
                                Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                                JSONObject metadata = new JSONObject();
                                metadata.put("name", name);
                                metadata.put("documentId", id);
                                metadata.put("uri", document.toString());
                                metadata.put("provider", tree.getAuthority());
                                metadata.put("providerSize", cursor.isNull(3) ? -1 : cursor.getLong(3));
                                try (AssetFileDescriptor asset = app.getContentResolver()
                                        .openAssetFileDescriptor(document, "r")) {
                                    if (asset == null) throw new IllegalStateException("Provider open returned null");
                                    long start = asset.getStartOffset();
                                    long length = asset.getDeclaredLength();
                                    int ownedFd = asset.getParcelFileDescriptor().detachFd();
                                    nativeDocumentResult(metadata.toString(), ownedFd, start, length, "");
                                }
                                return;
                            }
                        }
                    }
                }
                reportError("No MP3 found in the selected folder");
            } catch (Exception e) {
                reportError("Original document: " + e.getClass().getSimpleName() + ": " + e.getMessage());
            }
        });
    }
}
