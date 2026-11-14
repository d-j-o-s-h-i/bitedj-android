#!/usr/bin/env python3
"""Build pinned native libraries with the Android NDK, without host libraries.

--resolve-hashes is the initial lock-generation operation. Normal builds reject
archives whose SHA256 differs from dependencies.lock.json and never update it.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ANDROID = Path(__file__).resolve().parents[1]
BUILD = ANDROID / "build"
LOCK_FILE = ANDROID / "dependencies.lock.json"
SOURCES = {
    "ogg": ("1.3.5", "https://codeload.github.com/xiph/ogg/tar.gz/refs/tags/v1.3.5"),
    "vorbis": ("1.3.7", "https://codeload.github.com/xiph/vorbis/tar.gz/refs/tags/v1.3.7"),
    "flac": ("1.4.3", "https://codeload.github.com/xiph/flac/tar.gz/refs/tags/1.4.3"),
    "sndfile": ("1.2.2", "https://codeload.github.com/libsndfile/libsndfile/tar.gz/refs/tags/1.2.2"),
    "taglib": ("1.13.1", "https://codeload.github.com/taglib/taglib/tar.gz/refs/tags/v1.13.1"),
    "ebur128": ("1.2.6", "https://codeload.github.com/jiixyj/libebur128/tar.gz/refs/tags/v1.2.6"),
    "soundtouch": ("2.3.3", "https://www.surina.net/soundtouch/soundtouch-2.3.3.tar.gz"),
    "lame": ("3.100", "https://deb.debian.org/debian/pool/main/l/lame/lame_3.100.orig.tar.gz"),
    "protobuf": ("21.12", "https://codeload.github.com/protocolbuffers/protobuf/tar.gz/refs/tags/v21.12"),
    "ffmpeg": ("7.1.2", "https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz"),
    "oboe": ("1.9.0", "https://codeload.github.com/google/oboe/tar.gz/refs/tags/1.9.0"),
    "opus": ("1.5.2", "https://codeload.github.com/xiph/opus/tar.gz/refs/tags/v1.5.2"),
    "fftw": ("3.3.10", "https://www.fftw.org/fftw-3.3.10.tar.gz"),
    "chromaprint": ("1.5.1", "https://codeload.github.com/acoustid/chromaprint/tar.gz/refs/tags/v1.5.1"),
    "gsl": ("4.0.0", "https://codeload.github.com/microsoft/GSL/tar.gz/refs/tags/v4.0.0"),
    "googletest": ("1.15.2", "https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.15.2"),
}


def run(command, *, cwd=None, env=None):
    print("Running:", " ".join(str(x) for x in command), flush=True)
    subprocess.run([str(x) for x in command], cwd=cwd, env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resolve-hashes", action="store_true")
    parser.add_argument("--fetch-only", action="store_true")
    parser.add_argument("--only", choices=tuple(SOURCES), help="Rebuild one dependency after its prerequisites exist")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 12))
    args = parser.parse_args()
    paths = json.loads((BUILD / "environment.json").read_text())
    locked = json.loads(LOCK_FILE.read_text()) if LOCK_FILE.exists() else {}
    downloads = BUILD / "downloads"
    downloads.mkdir(exist_ok=True)

    def fetch(item):
        name, (version, url) = item
        archive = downloads / f"{name}-{version}.tar"
        if not archive.exists():
            temporary = archive.with_suffix(".part")
            print("Downloading", name, version, flush=True)
            try:
                with urllib.request.urlopen(url, timeout=60) as src, temporary.open("wb") as dst:
                    shutil.copyfileobj(src, dst, 1024 * 1024)
            except OSError as e:
                raise RuntimeError(f"Download failed for {name}: {url}: {e}") from e
            temporary.replace(archive)
        with archive.open("rb") as f:
            sha256 = hashlib.file_digest(f, "sha256").hexdigest()
        record = {"version": version, "url": url, "sha256": sha256}
        if name in locked:
            if locked[name] != record:
                raise RuntimeError(f"Dependency lock mismatch: {name}")
        elif not args.resolve_hashes:
            raise RuntimeError(f"Missing lock entry: {name}; resolve the initial lock first")
        # Validate archive structure before treating it as a downloaded source.
        with tarfile.open(archive) as tar:
            members = tar.getmembers()
            roots = {Path(m.name).parts[0] for m in members if Path(m.name).parts}
            if len(roots) != 1:
                raise RuntimeError(f"Unexpected source archive layout: {name}")
        return name, record, archive

    with ThreadPoolExecutor(max_workers=5) as executor:
        fetched = list(executor.map(fetch, SOURCES.items()))
    records = {name: record for name, record, _ in fetched}
    if args.resolve_hashes:
        LOCK_FILE.write_text(json.dumps(records, indent=2) + "\n")
    if args.fetch_only:
        return
    prefix = BUILD / "deps/arm64-v8a"
    host = BUILD / "deps/host"
    prefix.mkdir(parents=True, exist_ok=True)
    sources = BUILD / "deps/sources"
    sources.mkdir(exist_ok=True)
    extracted = {}
    for name, record, archive in fetched:
        destination = sources / name
        stamp = destination / ".archive-sha256"
        if not stamp.exists():
            temporary = sources / (name + "-extract")
            temporary.mkdir(exist_ok=True)
            with tarfile.open(archive) as tar:
                tar.extractall(temporary, filter="data")
            children = list(temporary.iterdir())
            if len(children) != 1:
                raise RuntimeError(f"Unexpected extraction: {name}")
            children[0].rename(destination)
            temporary.rmdir()
            stamp.write_text(record["sha256"])
        elif stamp.read_text() != record["sha256"]:
            raise RuntimeError(f"Source checksum changed: {name}; remove its build directory")
        extracted[name] = destination
    ndk = Path(paths["ndk"])
    llvm = ndk / "toolchains/llvm/prebuilt/linux-x86_64"
    cc = llvm / "bin/aarch64-linux-android33-clang"
    cxx = llvm / "bin/aarch64-linux-android33-clang++"
    env = os.environ.copy()
    env.update(CC=str(cc), CXX=str(cxx), AR=str(llvm / "bin/llvm-ar"),
               RANLIB=str(llvm / "bin/llvm-ranlib"), STRIP=str(llvm / "bin/llvm-strip"),
               CFLAGS="-O2 -fPIC", CXXFLAGS="-O2 -fPIC",
               CPPFLAGS=f"-I{prefix}/include", LDFLAGS=f"-L{prefix}/lib -Wl,-z,max-page-size=16384",
               PKG_CONFIG_LIBDIR=str(prefix / "lib/pkgconfig"), PKG_CONFIG_PATH="")
    env.pop("PKG_CONFIG_SYSROOT_DIR", None)
    common = ["-DCMAKE_TOOLCHAIN_FILE=" + str(ndk / "build/cmake/android.toolchain.cmake"),
              "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-33",
              "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON", "-DCMAKE_BUILD_TYPE=Release",
              "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DBUILD_SHARED_LIBS=OFF",
              "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
              "-DCMAKE_PREFIX_PATH=" + str(prefix), "-DCMAKE_FIND_ROOT_PATH=" + str(prefix),
              "-DBUILD_TESTING=OFF"]

    def cmake(name, extra=(), *, host_build=False, source_subdir=""):
        if args.only and args.only != name:
            return
        output = BUILD / "deps/build" / (name + ("-host" if host_build else ""))
        options = ["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_PREFIX=" + str(host)] if host_build else common
        run(["cmake", "-S", extracted[name] / source_subdir, "-B", output,
             "-G", "Ninja", *options, *extra], env=os.environ if host_build else env)
        run(["cmake", "--build", output, "--parallel", args.jobs], env=os.environ if host_build else env)
        run(["cmake", "--install", output], env=os.environ if host_build else env)

    cmake("ogg")
    cmake("vorbis")
    cmake("opus", ["-DOPUS_BUILD_PROGRAMS=OFF", "-DOPUS_BUILD_TESTING=OFF"])
    cmake("flac", ["-DBUILD_CXXLIBS=OFF", "-DBUILD_PROGRAMS=OFF", "-DBUILD_EXAMPLES=OFF",
                    "-DBUILD_DOCS=OFF", "-DINSTALL_MANPAGES=OFF"])
    cmake("sndfile", ["-DENABLE_MPEG=OFF", "-DBUILD_PROGRAMS=OFF", "-DBUILD_EXAMPLES=OFF"])
    cmake("taglib", ["-DBUILD_TESTS=OFF", "-DBUILD_EXAMPLES=OFF"])
    cmake("ebur128", ["-DENABLE_INTERNAL_QUEUE_H=ON"])
    cmake("soundtouch", ["-DSOUNDSTRETCH=OFF", "-DSOUNDTOUCH_DLL=OFF"])
    cmake("oboe", ["-DOBOE_BUILD_TESTS=OFF"])
    cmake("fftw", ["-DENABLE_FLOAT=OFF", "-DBUILD_TESTS=OFF"])
    cmake("chromaprint", ["-DFFT_LIB=fftw3", "-DBUILD_TOOLS=OFF", "-DBUILD_TESTS=OFF"])
    cmake("gsl", ["-DGSL_TEST=OFF"])
    # gtest_prod.h is used by production headers for friend test declarations.
    cmake("googletest", ["-DBUILD_GMOCK=OFF", "-Dgtest_build_tests=OFF"])
    cmake("protobuf", ["-Dprotobuf_BUILD_TESTS=OFF", "-Dprotobuf_WITH_ZLIB=OFF"],
          host_build=True, source_subdir="cmake")
    cmake("protobuf", ["-Dprotobuf_BUILD_TESTS=OFF", "-Dprotobuf_WITH_ZLIB=OFF",
                       "-Dprotobuf_BUILD_PROTOC_BINARIES=OFF", "-Dprotobuf_BUILD_LIBPROTOC=OFF"],
          source_subdir="cmake")
    if not args.only or args.only == "lame":
        lame_build = BUILD / "deps/build/lame"
        lame_build.mkdir(exist_ok=True)
        run([extracted["lame"] / "configure", "--host=aarch64-linux-android",
             "--prefix=" + str(prefix), "--disable-shared", "--enable-static",
             "--disable-frontend"], cwd=lame_build, env=env)
        run(["make", "-j", args.jobs], cwd=lame_build, env=env)
        run(["make", "install"], cwd=lame_build, env=env)
    if not args.only or args.only == "ffmpeg":
        ffmpeg_build = BUILD / "deps/build/ffmpeg"
        ffmpeg_build.mkdir(exist_ok=True)
        run([extracted["ffmpeg"] / "configure", "--prefix=" + str(prefix),
             "--target-os=android", "--arch=aarch64", "--enable-cross-compile",
             "--cc=" + str(cc), "--cxx=" + str(cxx), "--ar=" + str(llvm / "bin/llvm-ar"),
             "--ranlib=" + str(llvm / "bin/llvm-ranlib"), "--strip=" + str(llvm / "bin/llvm-strip"),
             "--sysroot=" + str(llvm / "sysroot"), "--disable-shared", "--enable-static",
             "--enable-pic", "--disable-programs", "--disable-doc", "--disable-network",
             "--disable-autodetect", "--disable-everything", "--enable-avcodec",
             "--enable-avformat", "--enable-avutil", "--enable-swresample",
             "--enable-decoder=mp3,mp3float,flac,vorbis,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_f64le",
             "--enable-demuxer=mp3,wav,flac,ogg", "--enable-parser=mpegaudio,flac,vorbis",
             "--enable-protocol=file", "--extra-cflags=-O2 -fPIC",
             "--extra-ldflags=-Wl,-z,max-page-size=16384"], cwd=ffmpeg_build, env=env)
        run(["make", "-j", args.jobs], cwd=ffmpeg_build, env=env)
        run(["make", "install"], cwd=ffmpeg_build, env=env)
    print("Native dependencies ready:", prefix, flush=True)


if __name__ == "__main__":
    main()
