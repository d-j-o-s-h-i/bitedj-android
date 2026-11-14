#!/usr/bin/env python3
"""Fix Qt's Android input and raster-flush coordinates without changing its ABI.

The official Qt 6.9.3 implementation removes the Java window-origin addition
and maps native pixel coordinates via QPlatformWindow rather than QWindow.
The raster flush adapter also converts native dirty regions back to logical
coordinates before QRhi scales them. Only the matching 6.8.3 Android platform
plugin and QtWindow Java classes are rebuilt. Qt Core/Gui and all other plugins
remain the pinned official binaries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile

ANDROID = Path(__file__).resolve().parents[1]
BUILD = ANDROID / "build"
PATCHES = tuple(ANDROID / "qt-platform" / name for name in (
    "touch-coordinates.patch", "raster-flush-coordinates.patch", "touch-gestures.patch",
    "no-android-font-icons.patch"))


def run(*args, env=None):
    subprocess.run([str(arg) for arg in args], env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 12))
    args = parser.parse_args()
    lock = json.loads((ANDROID / "toolchain.lock.json").read_text())
    paths = json.loads((BUILD / "environment.json").read_text())
    if lock["qt"]["version"] != "6.8.3":
        parser.error("Review/remove this backport before changing the Qt version")
    qt = Path(paths["qt_android"])
    output = BUILD / "qt-platform"
    output.mkdir(parents=True, exist_ok=True)
    plugin = qt / "plugins/platforms/libplugins_platforms_qtforandroid_arm64-v8a.so"
    jar = qt / "jar/Qt6Android.jar"
    stamp = output / "installed.json"
    recipe_digest = hashlib.sha256(b"".join(patch.read_bytes() for patch in PATCHES)
            + Path(__file__).read_bytes()
            + (ANDROID / "qt-platform/CMakeLists.txt").read_bytes()).hexdigest()
    if stamp.exists():
        installed = json.loads(stamp.read_text())
        if installed.get("recipe") == recipe_digest and all(
                hashlib.file_digest(path.open("rb"), "sha256").hexdigest()
                == installed.get(key) for key, path in (("plugin", plugin), ("jar", jar))):
            return

    package = lock["qt"]["base_source"]
    archive = BUILD / "downloads" / package["url"].rsplit("/", 1)[1]
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix(".part")
        with urllib.request.urlopen(package["url"], timeout=60) as src, temporary.open("wb") as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
        temporary.replace(archive)
    if hashlib.file_digest(archive.open("rb"), "sha256").hexdigest() != package["sha256"]:
        raise RuntimeError("Qt base source checksum mismatch")
    source = output / "source" / "qtbase-everywhere-src-6.8.3"
    if source.parent.exists():
        shutil.rmtree(source.parent)
    with tarfile.open(archive) as tar:
        tar.extractall(source.parent, filter="data")
    for patch in PATCHES:
        run("patch", "--batch", "--fuzz=0", "-p1", "-d", source, "-i", patch)

    env = os.environ.copy()
    env.update(JAVA_HOME=paths["jdk"], ANDROID_SDK_ROOT=paths["sdk"],
               ANDROID_NDK_ROOT=paths["ndk"])
    native = output / "native"
    run(qt / "bin/qt-cmake", "-S", ANDROID / "qt-platform", "-B", native, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DQTBASE_SOURCE_DIR=" + str(source),
        "-DANDROID_ABI=" + lock["abi"], "-DANDROID_PLATFORM=android-" + str(lock["native_api"]),
        "-DANDROID_NDK=" + paths["ndk"], "-DANDROID_SDK_ROOT=" + paths["sdk"],
        "-DQT_HOST_PATH=" + paths["qt_host"], "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON", env=env)
    run("cmake", "--build", native, "--parallel", args.jobs, env=env)

    # Keep the original jar so repeat builds never depend on a patched class.
    original_jar = output / "Qt6Android.original.jar"
    if not original_jar.exists():
        shutil.copy2(jar, original_jar)
    classes = output / "classes"
    if classes.exists():
        shutil.rmtree(classes)
    classes.mkdir()
    run(Path(paths["jdk"]) / "bin/javac", "-source", "8", "-target", "8",
        "-classpath", str(original_jar) + os.pathsep
        + str(Path(paths["sdk"]) / "platforms/android-35/android.jar"),
        "-d", classes, source / "src/android/jar/src/org/qtproject/qt/android/QtWindow.java", env=env)
    patched_jar = output / "Qt6Android.jar"
    replacements = {path.relative_to(classes).as_posix(): path
                    for path in classes.rglob("*.class")}
    with zipfile.ZipFile(original_jar) as original, zipfile.ZipFile(patched_jar, "w") as patched:
        for item in original.infolist():
            if item.filename not in replacements:
                patched.writestr(item, original.read(item.filename))
        for name, path in replacements.items():
            patched.write(path, name)
    original_plugin = output / "qtforandroid.original.so"
    if not original_plugin.exists():
        shutil.copy2(plugin, original_plugin)
    shutil.copy2(native / plugin.name, plugin)
    shutil.copy2(patched_jar, jar)
    stamp.write_text(json.dumps({"recipe": recipe_digest,
        "plugin": hashlib.file_digest(plugin.open("rb"), "sha256").hexdigest(),
        "jar": hashlib.file_digest(jar.open("rb"), "sha256").hexdigest()}, indent=2) + "\n")


if __name__ == "__main__":
    main()
