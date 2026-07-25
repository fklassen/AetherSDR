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

## Post-spike features (also emulator-validated)

- **Manual IP connect** — text field on the discovery page footer;
  connects to `<ip>:4992` directly. Fallback for when discovery
  broadcasts don't arrive (harness: `adb reverse tcp:4992 tcp:4993`).
- **Per-slice S-meter** — `sub meter all`, SLC/LEVEL meter defs mapped
  by index, VITA PCC 0x8002 (u16 id + s16 raw pairs, dBm = raw/128),
  shown as S-units + dBm on each slice card (S9 = −73 dBm, 6 dB/unit).
- **Opus RX audio** — run `tools/setup-opus-android.sh` once before
  cmake (DFNR-style setup: downloads opus 1.5.2, NDK-builds static
  libopus per ABI into gitignored `third_party/`; configure prints
  "Opus decode enabled/disabled"). The slice-page OPUS/PCM toggle picks
  `compression=opus` vs `none` on stream create; VitaStream decodes
  PCC 0x8005 (one 240-sample 24 kHz frame per packet, facts per
  `src/core/OpusCodec.{h,cpp}`) through libopus into the same sink.
  The fake radio encodes with python `opuslib` when the app requests
  opus (venv + `pip install opuslib`, needs a host libopus, e.g.
  `DYLD_LIBRARY_PATH=/usr/local/opt/opus/lib`); PCM mode needs neither.

- **Waterfall** — VITA PCC 0x8004 tiles (36-byte subheader:
  VitaFrequency i64 lowFreq/binBw at Hz·2²⁰, width/height, timecode,
  autoBlack, totalBins/firstBin; u16 BE bins, dBm = int16/128;
  timecode-keyed frame assembly per
  `PanadapterStream::decodeWaterfallTile` incl. the
  GHSA-7gvg-x594-pprq reset guard). Waterfall stream id is the second
  comma field of the panafall create reply. Renders as a scrolling
  history strip under the spectrum (black→blue→yellow→white ramp over
  the pan dBm range); tap it to tune, same mapping as the spectrum.
- **SmartLink (WAN)** — Auth0 ROPC login (facts per
  `src/core/SmartLinkClient.{h,cpp}`: password-realm grant, public
  client id, `offline_access` scope), TLS broker line protocol
  (`application register token=<jwt>` → radio list → `application
  connect serial=… hole_punch_port=…` → `radio connect_ready
  handle=…`), then TLS to the radio's public ip/port with
  `wan validate handle=<h>` as the first command (per
  `src/core/WanConnection.cpp`). Requires
  `tools/setup-openssl-android.sh` once (KDAB android_openssl
  prebuilts — Qt on Android has no TLS backend without them).
  Spike security posture: no credential persistence, tokens in memory
  only; radio/broker certs are trust-on-connect **only** in harness
  mode / WAN spike — desktop pins fingerprints (GHSA-wfx7-w6p8-4jr2)
  and that is a must-fix before graduation. Emulator harness:
  `tools/fake_smartlink.py` (self-signed TLS broker :14443 + TLS radio
  front :14994 proxying to `fake_radio_tcp.py`); enter
  `127.0.0.1:14443` in the SmartLink email field (harness mode skips
  Auth0). Real-account + real-radio WAN sign-off pending, alongside
  the phone list.

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

The fake-radio harness lives in `tools/`:

```bash
adb reverse tcp:4993 tcp:4993          # guest → host command channel
adb emu 'redir add udp:24993:14993'    # host → guest VITA (audio + FFT)
python3 tools/fake_radio_tcp.py &      # TCP server + VITA senders
python3 tools/fake_radio.py            # discovery datagrams (30 × 1 s)
```

`fake_radio.py` sends discovery datagrams to host `127.0.0.1:14992`
advertising `ip=127.0.0.1 port=4993` — the radio card appears within a
second. `fake_radio_tcp.py` answers the command channel and streams
audio + FFT (docstrings have the per-phase detail). Note: `10.0.2.2`
(the classic host alias) is NOT routable from the API-35 emulator's
WiFi network — hence `adb reverse` + loopback. Tap the radio card →
slice cards appear; step buttons round-trip `slice tune` through the
server and the UI updates from the status broadcast.

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
