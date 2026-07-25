# Android companion spike (mobile thin client)

Throwaway prototype de-risking an **Android companion app** (remote head:
discovery, connect, tune, RX audio, spectrum) before the aetherd RFC's
thin-client step lands. Lives in `prototypes/` on purpose — same pattern
as `prototypes/hl2`: an in-tree mobile client is architecture ahead of
the RFC (per `AGENTS.md`, maintainer-only), so the unknowns get proven
cheaply and disposably first. Self-contained: own CMake project, **does
not link `aethercore`**, root build untouched.

## Unknowns this spike proves

| Phase | Proves |
|---|---|
| 1 | ✅ Qt 6.11 for Android builds + deploys a C++20 Qt Quick APK |
| 2 | ✅* SmartSDR UDP :4992 discovery broadcast reception on Android WiFi (MulticastLock) |
| 3 | ✅ TCP :4992 command channel + slice tune from touch UI |
| 4 | ✅* RX audio via Qt Multimedia (AAudio) at usable latency |
| 5 | ✅* FFT packet ingest → drag-to-tune spectrum strip in Quick |
| 6 | ✅* Foreground service; RX survives screen lock |

All six phases are in this tree (\* = emulator-validated; real-phone
sign-off pending hardware). Protocol facts mirror
`src/core/RadioDiscovery.{h,cpp}`, `src/core/CommandParser.cpp`, and the
`client program` / `sub slice all` init order in
`src/models/RadioModel.cpp` (same project — no clean-room needed); the
spike code itself is original and minimal. The spike deliberately
registers as a **non-GUI client** (no `client gui`) so it can never
claim a GUI slot on a real radio; tunes use
`slice tune <id> <MHz> autopan=0` per `src/models/SliceModel.cpp`.

## Build (macOS host)

Prereqs: JDK 21, Android SDK (platform 35, build-tools 35, NDK 27.2),
Qt 6.11.1 `android_arm64_v8a` + macOS host kit installed via `aqt` under
`~/Qt`.

```bash
export ANDROID_SDK_ROOT=/usr/local/share/android-commandlinetools
~/Qt/6.11.1/android_arm64_v8a/bin/qt-cmake -S . -B build -G Ninja \
  -DQT_HOST_PATH=$HOME/Qt/6.11.1/macos \
  -DANDROID_SDK_ROOT=$ANDROID_SDK_ROOT \
  -DANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/27.2.12479018
cmake --build build --target apk
adb install build/android-build/build/outputs/apk/debug/android-build-debug.apk
```

Phone and radio must share a WiFi/LAN segment; the discovery list
populates from live broadcasts within ~1 s of a radio being present.

## Emulator validation (no phone / no radio)

Validated 2026-07-24 on the x86_64 emulator: build the
`android_x86_64` kit variant into `build-x86` (same configure line,
swap the kit path), then:

```bash
avdmanager create avd -n spike -k "system-images;android-35;google_apis;x86_64" -d pixel_7
emulator -avd spike -no-window -no-audio &
adb install -r build-x86/android-build/build/outputs/apk/debug/android-build-debug.apk
adb emu 'redir add udp:14992:4992'   # host 14992 → guest 4992
adb shell am start -n org.aethersdr.companion/org.qtproject.qt.android.bindings.QtActivity
```

Send synthetic discovery datagrams to `127.0.0.1:14992` (key=value
payload per `RadioDiscovery.cpp`) and the radio card appears. Host port
is 14992 because a running desktop AetherSDR already owns UDP 4992.

For the phase-3 command channel, run a fake radio TCP server on the
host (port 4993: send `V…`/`H…` on accept, reply `R<seq>|0|` to each
command, emit `S…|slice N in_use=1 RF_frequency=… mode=…` statuses) and
tunnel it into the guest with `adb reverse tcp:4993 tcp:4993`, then
advertise `ip=127.0.0.1 port=4993` in the discovery datagram. Note:
`10.0.2.2` (the classic host alias) is NOT routable from the API-35
emulator's WiFi network — use `adb reverse` + loopback instead. Tap the
radio card → slice cards appear; step buttons round-trip
`slice tune` through the server and the UI updates from the status
broadcast.

Phase-4 audio: the fake server answers
`stream create type=remote_audio_rx compression=none` with a stream id
and streams 600 Hz sine VITA packets (PCC 0x03E3, float32 stereo BE,
24 kHz, 256 samples/packet). The app binds UDP 14993
(`RxAudioStream::kLocalPort`); map it with
`adb emu redir add udp:24993:14993` and point the sender at host
port 24993. Toggle the speaker button on the slice page — the footer
counts packets/KiB fed to QAudioSink.

Phase-5 spectrum: on `display panafall create` the fake server replies
with a pan id, emits `display pan` status (center/bandwidth), and
streams FFT frames to the same UDP path — 512 u16 bins split across
two packets per frame (subheader per `PanadapterStream::decodeFFT`),
noise floor plus a peak that tracks slice 0. Toggle the chart button:
the strip renders at ~20 fps; tap/drag on it computes
`center − bw/2 + x/width·bw` and round-trips `slice tune`, moving both
the slice card and the peak.

Phase-6 background survival: starting RX audio starts
`RxForegroundService` (Java, `android/src/…`) via JNI — a
mediaPlayback-type foreground service holding a partial wake lock and
a high-perf WiFi lock. Verified in the emulator with
`dumpsys activity services` (`isForeground=true`, type 0x2) and by the
packet counter climbing 615 packets across a 45 s `KEYCODE_SLEEP`
screen-off interval.

Emulator proves parse + model + UI + sink consumption + screen-off
survival only. Still phone-only: real WiFi broadcast delivery /
MulticastLock (phase 2 sign-off), audible output + AAudio latency
(phase 4), touch drag feel (phase 5), Doze/OEM battery-killer behavior
over hours (phase 6) — untested until Android hardware is available.
