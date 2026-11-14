package io.github.djoshi.bitedj;

import android.content.Intent;
import android.app.AlertDialog;
import android.net.Uri;
import android.os.Environment;
import android.os.Build;
import android.provider.Settings;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.os.Looper;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import org.qtproject.qt.android.bindings.QtActivity;

public class BiteDJActivity extends QtActivity {
    private static final int MUSIC_TREE_REQUEST = 4602;
    private static final int STORAGE_REQUEST = 4603;
    private static final String STORAGE_PREFS = "bitedj-android-storage";
    private boolean pickerPending;
    private volatile boolean settingsInitialized;
    private boolean storageSettingsLaunched;
    private CompletableFuture<Boolean> storageDecision;
    private CompletableFuture<Void> errorAcknowledged;

    boolean awaitStorageAccess() {
        if (Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager()) return true;
        if (Looper.myLooper() == Looper.getMainLooper()) return false;
        CompletableFuture<Boolean> decision = requestStorageDecision();
        try {
            boolean granted = decision.get();
            if (!granted) showStorageError("Storage access was not granted. BiteDj Android cannot safely open its settings or database.");
            return granted;
        } catch (Exception e) {
            Thread.currentThread().interrupt();
            return false;
        }
    }

    private synchronized CompletableFuture<Boolean> requestStorageDecision() {
        if (storageDecision != null && !storageDecision.isDone()) return storageDecision;
        CompletableFuture<Boolean> decision = new CompletableFuture<>();
        storageDecision = decision;
        runOnUiThread(() -> {
            if (storageDecision != decision || decision.isDone()) return;
            new AlertDialog.Builder(this)
                    .setTitle("BiteDj Android settings access")
                    .setMessage("BiteDj Android saves settings and its library database in /storage/emulated/0/bitedj-android. Allow access to all files in Android settings to continue.")
                    .setPositiveButton("Open settings", (dialog, which) -> {
                        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                Uri.parse("package:" + getPackageName()));
                        try {
                            storageSettingsLaunched = true;
                            startActivityForResult(intent, STORAGE_REQUEST);
                        } catch (RuntimeException first) {
                            try {
                                startActivityForResult(new Intent(
                                        Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION), STORAGE_REQUEST);
                            } catch (RuntimeException second) {
                                storageSettingsLaunched = false;
                                decision.complete(false);
                            }
                        }
                    })
                    .setNegativeButton("Exit", (dialog, which) -> denyStorageRequest(decision))
                    .setOnCancelListener(dialog -> denyStorageRequest(decision))
                    .show();
        });
        return decision;
    }

    private void denyStorageRequest(CompletableFuture<Boolean> decision) {
        decision.complete(false);
        if (getSharedPreferences(STORAGE_PREFS, MODE_PRIVATE).getBoolean("has_started", false)) {
            finish();
        }
    }

    void markSettingsInitialized() {
        settingsInitialized = true;
        getSharedPreferences(STORAGE_PREFS, MODE_PRIVATE).edit()
                .putBoolean("has_started", true).commit();
    }

    void showStorageError(String message) {
        showStorageMessage("BiteDj Android cannot start", message);
    }

    void showStorageWarning(String message) {
        showStorageMessage("Some settings need manual copying", message);
    }

    private void showStorageMessage(String title, String message) {
        if (Looper.myLooper() == Looper.getMainLooper()) return;
        CompletableFuture<Void> acknowledged = new CompletableFuture<>();
        runOnUiThread(() -> {
            errorAcknowledged = acknowledged;
            new AlertDialog.Builder(this).setTitle(title)
                    .setMessage(message).setPositiveButton("Close", (dialog, which) ->
                            acknowledged.complete(null))
                    .setOnCancelListener(dialog -> acknowledged.complete(null)).show();
        });
        try {
            acknowledged.get();
        } catch (Exception e) {
            Thread.currentThread().interrupt();
        }
    }

    public void finishAfterShutdown() {
        runOnUiThread(this::finish);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (storageSettingsLaunched && storageDecision != null) {
            // onResume runs after returning from the special-access Settings page.
            storageSettingsLaunched = false;
            storageDecision.complete(Environment.isExternalStorageManager());
        } else if (settingsInitialized && Build.VERSION.SDK_INT >= 30
                && !Environment.isExternalStorageManager()) {
            settingsInitialized = false;
            new AlertDialog.Builder(this).setTitle("BiteDj Android storage access was removed")
                    .setMessage("BiteDj Android will close to avoid writing settings without access.")
                    .setPositiveButton("Close", (dialog, which) -> PlatformBridge.requestQuit())
                    .setOnCancelListener(dialog -> PlatformBridge.requestQuit()).show();
        } else if (Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager()
                && getSharedPreferences(STORAGE_PREFS, MODE_PRIVATE)
                        .getBoolean("has_started", false)
                && (storageDecision == null || storageDecision.isDone())) {
            // Android may recreate the Activity or process when special access
            // is revoked. Show the prompt before native initialization resumes.
            requestStorageDecision();
        }
    }

    @Override
    protected void onDestroy() {
        if (storageDecision != null) storageDecision.complete(false);
        if (errorAcknowledged != null) errorAcknowledged.complete(null);
        super.onDestroy();
    }

    // Enabled explicitly by adb for coordinate diagnostics; silent in normal use.
    public boolean inputDiagnosticsEnabled() {
        return getIntent().getBooleanExtra("inputDiagnostics", false);
    }

    public boolean inputCoordinateTestEnabled() {
        return getIntent().getBooleanExtra("inputCoordinateTest", false);
    }

    public String inputContentRect() {
        CompletableFuture<String> result = new CompletableFuture<>();
        Runnable read = () -> {
            View content = findViewById(android.R.id.content);
            int[] position = new int[2];
            content.getLocationOnScreen(position);
            result.complete(position[0] + "," + position[1] + ","
                    + content.getWidth() + "," + content.getHeight());
        };
        if (Looper.myLooper() == Looper.getMainLooper()) read.run();
        else runOnUiThread(read);
        try {
            return result.get(2, TimeUnit.SECONDS);
        } catch (Exception e) {
            return "";
        }
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (inputDiagnosticsEnabled() && event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            Log.i("BiteDJInput", "Android down raw=" + event.getRawX() + "," + event.getRawY()
                    + " activity=" + event.getX() + "," + event.getY());
            logInputView(getWindow().getDecorView(), 0);
        }
        return super.dispatchTouchEvent(event);
    }

    private static void logInputView(View view, int depth) {
        if (!view.isShown() || depth > 5) return;
        int[] position = new int[2];
        view.getLocationOnScreen(position);
        Log.i("BiteDJInput", "view=" + view.getClass().getSimpleName()
                + " origin=" + position[0] + "," + position[1]
                + " size=" + view.getWidth() + "," + view.getHeight());
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); ++i) {
                logInputView(group.getChildAt(i), depth + 1);
            }
        }
    }

    public void selectMusicFolder() {
        runOnUiThread(() -> {
            if (pickerPending) return;
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
            pickerPending = true;
            try {
                startActivityForResult(intent, MUSIC_TREE_REQUEST);
            } catch (RuntimeException e) {
                pickerPending = false;
                PlatformBridge.reportError("Folder picker: " + e.getMessage());
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == STORAGE_REQUEST) {
            storageSettingsLaunched = false;
            if (storageDecision != null) {
                if (Environment.isExternalStorageManager()) storageDecision.complete(true);
                else denyStorageRequest(storageDecision);
            }
            return;
        }
        if (requestCode != MUSIC_TREE_REQUEST) return;
        pickerPending = false;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            PlatformBridge.reportError("Folder selection cancelled; previous selection retained.");
            return;
        }
        Uri uri = data.getData();
        int flags = data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION;
        try {
            if (flags == 0) throw new SecurityException("No read permission returned");
            getContentResolver().takePersistableUriPermission(uri, flags);
            getSharedPreferences("bitedj-android-media", MODE_PRIVATE).edit()
                    .putString("tree_uri", uri.toString()).apply();
            PlatformBridge.probeMusicFolder(getApplicationContext());
        } catch (RuntimeException e) {
            PlatformBridge.reportError("Folder permission: " + e.getMessage());
        }
    }
}
