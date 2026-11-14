#!/usr/bin/env python3
"""Configure/build BiteDj Android with Qt's Android toolchain and its single APK packaging path."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

ANDROID = Path(__file__).resolve().parents[1]
ROOT = ANDROID.parent
BUILD = ANDROID / "build"
APPLICATION_ID = "io.github.djoshi.bitedj"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--build-type", choices=("Debug", "RelWithDebInfo", "Release"),
                        default="RelWithDebInfo")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 12))
    args = parser.parse_args()
    lock = json.loads((ANDROID / "toolchain.lock.json").read_text())
    environment_file = BUILD / "environment.json"
    if not environment_file.exists():
        parser.error("Run android/tools/bootstrap.py first")
    paths = json.loads(environment_file.read_text())
    subprocess.run([sys.executable, str(ANDROID / "tools/build-qt-platform.py"),
                    "--jobs", str(args.jobs)], check=True)
    output = BUILD / "bitedj"
    package = output / "package"
    shutil.copytree(ANDROID / "package", package, dirs_exist_ok=True)
    manifest = ET.parse(package / "AndroidManifest.xml",
                        parser=ET.XMLParser(target=ET.TreeBuilder(insert_comments=True)))
    ET.register_namespace("android", "http://schemas.android.com/apk/res/android")
    manifest.getroot().set("package", APPLICATION_ID)
    manifest.find("application").set("{http://schemas.android.com/apk/res/android}label", "BiteDj Android (djoshi bootleg)")
    manifest.write(package / "AndroidManifest.xml", encoding="utf-8", xml_declaration=True)
    resources = package / "assets/bitedj-android-res"
    if resources.exists():
        shutil.rmtree(resources)
    resources.mkdir(parents=True)
    for name in ("controllers", "effects", "fonts", "images", "keyboard", "shaders", "skins"):
        shutil.copytree(ROOT / "res" / name, resources / name)
    shutil.copy2(ROOT / "res/schema.xml", resources / "schema.xml")
    digest = hashlib.sha256()
    for path in sorted(resources.rglob("*")):
        if path.is_file():
            digest.update(path.relative_to(resources).as_posix().encode() + b"\0")
            digest.update(path.read_bytes())
    (package / "assets/bitedj-android-res-version").write_text(digest.hexdigest())
    env = os.environ.copy()
    env.update(JAVA_HOME=paths["jdk"], ANDROID_SDK_ROOT=paths["sdk"],
               ANDROID_NDK_ROOT=paths["ndk"], GRADLE_USER_HOME=str(BUILD / "gradle"))
    dependencies = BUILD / "deps/arm64-v8a"
    env["PKG_CONFIG_LIBDIR"] = str(dependencies / "lib/pkgconfig")
    env["PKG_CONFIG_PATH"] = ""
    configure = [str(Path(paths["qt_android"]) / "bin/qt-cmake"),
                 "-S", str(ROOT),
                 "-B", str(output / "native"), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=" + args.build_type, "-DANDROID_ABI=" + lock["abi"],
                 "-DANDROID_PLATFORM=android-" + str(lock["native_api"]),
                 "-DANDROID_NDK=" + paths["ndk"], "-DANDROID_SDK_ROOT=" + paths["sdk"],
                 "-DQT_HOST_PATH=" + paths["qt_host"],
                 "-DQT_ANDROID_SDK_BUILD_TOOLS_REVISION=" + lock["build_tools"]["version"],
                 "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
                 "-DMIXXX_ANDROID_PACKAGE_DIR=" + str(package),
                 "-DCMAKE_PREFIX_PATH=" + str(dependencies),
                 "-DCMAKE_FIND_ROOT_PATH=" + str(dependencies),
                 "-DProtobuf_PROTOC_EXECUTABLE=" + str(BUILD / "deps/host/bin/protoc")]
    configure += ["-D" + flag + "=OFF" for flag in (
        "QML", "BUILD_TESTING", "BUILD_BENCH", "PORTMIDI", "BATTERY", "HID", "BULK",
        "HSS1394", "BROADCAST", "QTKEYCHAIN", "ENGINEPRIME", "KEYFINDER", "LILV",
        "VINYLCONTROL", "RUBBERBAND", "LOCALECOMPARE", "FAAD", "MAD", "OPUS", "MODPLUG",
        "WAVPACK", "MOLD_SUPPORT", "CCACHE_SUPPORT", "SCCACHE_SUPPORT", "LLD_SUPPORT",
        "INSTALL_USER_UDEV_RULES")]
    configure += ["-DFFMPEG=ON"]
    subprocess.run(configure, env=env, check=True)
    if not args.configure_only:
        subprocess.run(["cmake", "--build", str(output / "native"),
                        "--target", "apk", "--parallel", str(args.jobs)], env=env, check=True)


if __name__ == "__main__":
    main()
