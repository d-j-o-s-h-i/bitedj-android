# BiteDj for Android

**[Download the APK](../../releases/latest/download/BiteDj-Android-djoshi-1.0-5.apk)** (very alpha, unofficial port of [BiteDJ](https://github.com/TeamDeckshark/bitedj))

Experimental Android port of [BiteDJ](https://github.com/TeamDeckshark/bitedj) by
[Deckshark](https://github.com/TeamDeckshark), a DJ app built on
[Mixxx](https://github.com/mixxxdj/mixxx) with a widgets-only appliance UI.

The goal: **turn a DJ controller into a standalone unit, with a phone as its
brain.** No laptop and no Raspberry on the booth. The phone/tablet runs the whole engine and the interface,
the controller stays the surface you touch.

Expect it to be rough, to break, and to change without notice. Don't play a set
you care about on it, and pls don't send its bugs to BiteDJ or Mixxx... they didn't
write this port.

## Tested on

- Motorola Edge 40 Neo, Android 15, arm64-v8a, 2400×1080.

Nothing else. Other Android 13+ arm64 phones with USB host support should work,
but assume you will have to adjust something.

## What works today

- Installs, starts and renders the full BiteDJ skin.
- Audio out through AAudio/Oboe (a single output stream).
- Plays tracks: load, play/cue, tempo, waveform and BPM/key analysis.
- MIDI controllers over USB — the bundled DDJ-FLX4 mapping is what was tested.
- Settings and database on shared storage: `/storage/emulated/0/bitedj-android`.

## What is missing or rough

- Waveforms are software-rendered. The `QOpenGLWindow` path has no surface in the
  Android platform plugin, so the app starts without OpenGL.
- One output stream only: no separate headphone/cue device, no recording.
- The skin is a 960×540 desktop-appliance layout, so it is clipped on a tall
  phone until you set a scale factor (see below).
- No local library browsing yet. BiteDJ is built around USB drives and this port
  has no "add music folder" UI, so the library starts empty.
- No CDJ/XDJ mappings.

## Build

Needs Linux (WSL2 is fine), JDK 21 and a few GB of disk for Qt, the NDK and the
native libraries. Versions are pinned in `android/toolchain.lock.json` and
`android/dependencies.lock.json`.

```bash
python3 android/tools/bootstrap.py    # JDK / SDK / NDK / Qt toolchain
python3 android/tools/build-deps.py   # pinned native libraries
python3 android/tools/build.py        # configure and pack the APK
```

The APK is left unsigned at
`android/build/bitedj/native/android-build/mixxx.apk`. Sign it before installing:

```bash
apksigner sign --ks your.keystore --out bitedj-android.apk \
    android/build/bitedj/native/android-build/mixxx.apk
```

## Making it run on your device

- **UI clipped or too large:** add `ScaleFactor 0.8` under `[Config]` in
  `/storage/emulated/0/bitedj-android/mixxx.cfg` (that value fitted my Edge 40
  Neo; try 0.7–0.9).
- **No sound, or it refuses the device:** Settings → Audio. The Android backend
  opens one output clock and rejects format or rate conversion, so pick what your
  hardware really supports (44.1 or 48 kHz).
- **Different toolchain:** edit `android/toolchain.lock.json` (Qt, NDK,
  build-tools, SDK levels). Changing Qt means re-checking the patches in
  `android/qt-platform/`.
- **Your own app identity:** `APPLICATION_ID` and the label live in
  `android/tools/build.py`, the Java package in `android/package/src/`. Use your
  own ID if you publish builds.
- **No controller attached?** Add `VirtualMidiPort 7700` under `[Controller]` in
  the same `mixxx.cfg`, run `adb forward tcp:7700 tcp:7700`, then drive the app
  with `android/tools/midi-drive.py`, which speaks the FLX4 mapping.

## Credits

- **BiteDJ** by Deckshark / TeamDeckshark — <https://github.com/TeamDeckshark/bitedj>
- **Mixxx** — <https://github.com/mixxxdj/mixxx>
- Android platform glue adapted from the community Mixxx Android port.

BiteDJ's own documentation is kept in [README-BiteDJ.md](README-BiteDJ.md).

thxxx <3

## License

GNU General Public License v2.0 — see [LICENSE](LICENSE). This is an unofficial
fork, so all upstream copyright and attribution apply unchanged. It is not
affiliated with Deckshark or the Mixxx project.
