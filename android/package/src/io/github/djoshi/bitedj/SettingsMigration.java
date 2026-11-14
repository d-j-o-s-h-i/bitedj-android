package io.github.djoshi.bitedj;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.LinkedHashMap;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** Copies old settings before BiteDJ opens its configuration or database. */
final class SettingsMigration {
    private static final String MARKER = ".android-migration-complete";

    private SettingsMigration() {}

    static String migrate(File destination, File currentLegacy, File requestedLegacy)
            throws IOException {
        if (Files.isSymbolicLink(destination.toPath())) throw new IOException("Settings path is a link");
        if (!destination.isDirectory() && !destination.mkdirs()) {
            throw new IOException("Cannot create " + destination);
        }
        File marker = new File(destination, MARKER);
        if (marker.exists() || Files.isSymbolicLink(marker.toPath())) {
            if (Files.isSymbolicLink(marker.toPath()) || !marker.isFile()) {
                throw new IOException("Invalid migration marker: " + marker);
            }
            checkWritable(destination);
            return "";
        }

        // The current release stored settings in files/.bitedj. An older requested
        // layout may have put them directly in the app's external data root.
        Map<String, File> sources = new LinkedHashMap<>();
        List<String> skipped = new ArrayList<>();
        collect(currentLegacy, "", sources, skipped, false);
        if (new File(requestedLegacy, "mixxx.cfg").isFile()
                || new File(requestedLegacy, "mixxxdb.sqlite").isFile()) {
            collect(requestedLegacy, "", sources, skipped, true);
        }
        if (sources.isEmpty() && skipped.isEmpty()) {
            checkWritable(destination);
            return "";
        }

        // Detect conflicts before making any changes. Existing destination files
        // always win; a differing file stops startup for manual resolution.
        for (Map.Entry<String, File> entry : sources.entrySet()) {
            File target = new File(destination, entry.getKey());
            checkParents(destination, target.getParentFile());
            if (Files.isSymbolicLink(target.toPath()) ||
                    (target.exists() && (!target.isFile() || !sameContent(entry.getValue(), target)))) {
                throw new IOException("Conflicting settings file: " + target);
            }
        }
        checkWritable(destination);
        for (Map.Entry<String, File> entry : sources.entrySet()) {
            File target = new File(destination, entry.getKey());
            if (target.exists()) continue;
            File parent = target.getParentFile();
            if (!parent.isDirectory() && !parent.mkdirs()) {
                throw new IOException("Cannot create settings directory: " + parent);
            }
            File partial = new File(parent, "." + target.getName() + ".migration-part");
            if (partial.exists() && !partial.delete()) throw new IOException("Cannot retry " + partial);
            try {
                copyAndSync(entry.getValue(), partial);
                if (!sameContent(entry.getValue(), partial)) {
                    throw new IOException("Copy verification failed: " + entry.getKey());
                }
                if (!partial.renameTo(target)) throw new IOException("Cannot publish " + target);
            } finally {
                partial.delete();
            }
        }
        for (Map.Entry<String, File> entry : sources.entrySet()) {
            if (!sameContent(entry.getValue(), new File(destination, entry.getKey()))) {
                throw new IOException("Migration verification failed: " + entry.getKey());
            }
        }
        File temporaryMarker = new File(destination, ".android-migration-marker-part");
        if (temporaryMarker.exists() && !temporaryMarker.delete()) {
            throw new IOException("Cannot retry migration marker");
        }
        try {
            try (FileOutputStream out = new FileOutputStream(temporaryMarker)) {
                String report = "Verified; legacy settings retained.\n";
                if (!skipped.isEmpty()) report += "Unreadable legacy folders retained for manual copy:\n"
                        + String.join("\n", skipped) + "\n";
                out.write(report.getBytes(java.nio.charset.StandardCharsets.UTF_8));
                out.getFD().sync();
            }
            if (!temporaryMarker.renameTo(marker)) throw new IOException("Cannot record migration completion");
        } finally {
            temporaryMarker.delete();
        }
        return String.join("\n", skipped);
    }

    private static void checkParents(File destination, File parent) throws IOException {
        for (File current = parent; current != null && !current.equals(destination);
                current = current.getParentFile()) {
            if (Files.isSymbolicLink(current.toPath()) ||
                    (current.exists() && !current.isDirectory())) {
                throw new IOException("Conflicting settings directory: " + current);
            }
        }
    }

    private static void collect(File directory, String prefix, Map<String, File> files,
            List<String> skipped,
            boolean skipAppDirectories) throws IOException {
        if (!directory.exists()) return;
        if (Files.isSymbolicLink(directory.toPath()) || !directory.isDirectory()) {
            throw new IOException("Invalid legacy settings directory: " + directory);
        }
        File[] children = directory.listFiles();
        if (children == null) {
            if (prefix.isEmpty()) throw new IOException("Cannot read " + directory);
            skipped.add(directory.getAbsolutePath());
            return;
        }
        for (File child : children) {
            if (skipAppDirectories && (child.getName().equals("files")
                    || child.getName().equals("cache"))) continue;
            String relative = prefix + child.getName();
            if (relative.equals(MARKER) || relative.endsWith(".migration-part")) continue;
            if (Files.isSymbolicLink(child.toPath())) {
                throw new IOException("Legacy settings contain a link: " + child);
            }
            if (child.isDirectory()) {
                collect(child, relative + "/", files, skipped, false);
            } else if (child.isFile()) {
                File previous = files.putIfAbsent(relative, child);
                if (previous != null && !sameContent(previous, child)) {
                    throw new IOException("Legacy settings conflict: " + relative);
                }
            } else {
                throw new IOException("Unsupported legacy settings entry: " + child);
            }
        }
    }

    private static void checkWritable(File directory) throws IOException {
        File probe = File.createTempFile(".bitedj-android-write-", ".tmp", directory);
        if (!probe.delete()) throw new IOException("Cannot clean write check: " + probe);
    }

    private static void copyAndSync(File source, File target) throws IOException {
        try (FileInputStream in = new FileInputStream(source);
                FileOutputStream out = new FileOutputStream(target)) {
            byte[] bytes = new byte[128 * 1024];
            int count;
            while ((count = in.read(bytes)) != -1) out.write(bytes, 0, count);
            out.getFD().sync();
        }
    }

    private static boolean sameContent(File left, File right) throws IOException {
        return left.length() == right.length()
                && java.util.Arrays.equals(sha256(left), sha256(right));
    }

    private static byte[] sha256(File file) throws IOException {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            try (FileInputStream in = new FileInputStream(file)) {
                byte[] bytes = new byte[128 * 1024];
                int count;
                while ((count = in.read(bytes)) != -1) digest.update(bytes, 0, count);
            }
            return digest.digest();
        } catch (NoSuchAlgorithmException e) {
            throw new IOException("SHA-256 unavailable", e);
        }
    }
}
