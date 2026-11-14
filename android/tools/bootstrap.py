#!/usr/bin/env python3
"""Install the pinned Linux Android toolchain inside android/build.

Qt archives are verified by aqt against the official archive checksums. Google
archives are checked against the checksums in toolchain.lock.json before unzip.
An existing Android Studio SDK supplies platforms, platform-tools and licenses
through read-only use; downloads and generated files stay in this workspace.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import venv

ANDROID = Path(__file__).resolve().parents[1]
BUILD = ANDROID / "build"
LOCK = json.loads((ANDROID / "toolchain.lock.json").read_text())


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def download(package):
    archives = BUILD / "downloads"
    archives.mkdir(parents=True, exist_ok=True)
    path = archives / package["url"].rsplit("/", 1)[1]
    if not path.exists():
        temporary = path.with_suffix(path.suffix + ".part")
        print("Downloading", path.name, flush=True)
        with urllib.request.urlopen(package["url"], timeout=60) as src, temporary.open("wb") as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
        temporary.replace(path)
    with path.open("rb") as f:
        digest = hashlib.file_digest(f, "sha1").hexdigest()
    if digest != package["sha1"]:
        raise RuntimeError(f"Checksum mismatch: {path}; remove this archive and retry")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=Path.home() / "Android/Sdk")
    system_jdk = Path("/usr/lib/jvm/java-21-openjdk-amd64")
    default_jdk = system_jdk if (system_jdk / "bin/javac").is_file() else Path.home() / "apps/android-studio/jbr"
    parser.add_argument("--jdk", type=Path, default=default_jdk)
    args = parser.parse_args()
    source_sdk = args.sdk.resolve()
    jdk = args.jdk.resolve()
    if not (source_sdk / "platforms/android-35/android.jar").is_file():
        parser.error("Existing SDK must contain the Android 35 platform")
    if not (jdk / "bin/javac").is_file():
        parser.error("Provide a full JDK 21 directory (including javac) with --jdk")
    BUILD.mkdir(parents=True, exist_ok=True)
    python_env = BUILD / "python"
    if not (python_env / "bin/python").exists():
        venv.create(python_env, with_pip=True)
    python = python_env / "bin/python"
    if subprocess.run([str(python), "-c", "import aqt"],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        run(python, "-m", "pip", "install", LOCK["qt"]["installer"])
    qt_root = BUILD / "qt"
    qt_version = LOCK["qt"]["version"]
    qt_android = qt_root / qt_version / "android_arm64_v8a"
    qt_host = qt_root / qt_version / "gcc_64"
    if not (qt_android / "bin/qt-cmake").is_file() or not (qt_host / "libexec/moc").is_file():
        run(python, "-m", "aqt", "install-qt", "linux", "android", qt_version,
            "android_arm64_v8a", "--autodesktop", "-O", qt_root,
            "--timeout", "30", "-m", *LOCK["qt"]["modules"])
    toolchains = BUILD / "toolchains"
    toolchains.mkdir(exist_ok=True)
    ndk = toolchains / LOCK["ndk"]["directory"]
    if not (ndk / "build/cmake/android.toolchain.cmake").is_file():
        run("unzip", "-q", "-o", download(LOCK["ndk"]), "-d", toolchains)
    sdk = BUILD / "sdk"
    sdk.mkdir(exist_ok=True)
    for name in ("platform-tools", "licenses"):
        link = sdk / name
        if not link.exists():
            link.symlink_to(source_sdk / name, target_is_directory=True)
    platforms = sdk / "platforms"
    if platforms.is_symlink():
        platforms.unlink()
    platforms.mkdir(exist_ok=True)
    platform = platforms / "android-35"
    if not platform.exists():
        platform.symlink_to(source_sdk / "platforms/android-35", target_is_directory=True)
    build_tools = sdk / "build-tools" / LOCK["build_tools"]["version"]
    if not (build_tools / "aapt2").is_file():
        unpack = BUILD / "build-tools-unpack"
        unpack.mkdir(exist_ok=True)
        run("unzip", "-q", "-o", download(LOCK["build_tools"]), "-d", unpack)
        extracted = [p for p in unpack.iterdir() if (p / "aapt2").exists()]
        if len(extracted) != 1:
            raise RuntimeError("Unexpected Android build-tools archive layout")
        build_tools.parent.mkdir(exist_ok=True)
        shutil.move(str(extracted[0]), build_tools)
    config = {"qt_android": str(qt_android), "qt_host": str(qt_host),
              "ndk": str(ndk), "sdk": str(sdk), "jdk": str(jdk)}
    (BUILD / "environment.json").write_text(json.dumps(config, indent=2) + "\n")
    print("Toolchain ready:", BUILD / "environment.json", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as e:
        print(str(e), file=sys.stderr)
        sys.exit(1)
