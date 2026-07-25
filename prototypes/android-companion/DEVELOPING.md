# Developing the Android companion spike

Everything needed to build, run, and exercise the spike from a clean
machine. This covers the toolchain and the emulator loop; for what the
spike *is* and which phases it proves, see [`README.md`](README.md).

Nothing here touches the main AetherSDR build. The spike is a
self-contained CMake project that does not link `aethercore`, and the
root `CMakeLists.txt` is untouched.

Written against macOS (the machine it was developed on is an Intel Mac,
so the NDK/aqt host packages below are the `x86_64` ones). On Linux the
same steps apply with the Linux host packages substituted.

---

## 1. Prerequisites

### Java + Android SDK

```bash
brew install --cask temurin@21 android-commandlinetools
```

Gradle needs a JDK; 21 is what this was built and tested against.

Set the SDK root — put this in your shell profile, since every command
below assumes it:

```bash
export ANDROID_SDK_ROOT=/usr/local/share/android-commandlinetools
export PATH="$PATH:$ANDROID_SDK_ROOT/platform-tools:$ANDROID_SDK_ROOT/emulator"
```

Accept licences and install the packages:

```bash
yes | sdkmanager --licenses
sdkmanager \
  "platform-tools" \
  "platforms;android-35" "build-tools;35.0.0" \
  "platforms;android-36" "build-tools;36.0.0" \
  "ndk;27.2.12479018" \
  "emulator" \
  "system-images;android-35;google_apis;x86_64"
```

Why both 35 and 36: the app targets SDK 35, but the Android Gradle
Plugin that Qt 6.11 drives **requires `compileSdk` 36** and fails the
build without it. Platform 36 is only there to satisfy the compile
step.

### Qt for Android

Qt does not ship Android kits with the Homebrew build, so use
[`aqtinstall`](https://github.com/miurahr/aqtinstall). A host (desktop)
kit is required alongside the target kits — the Android build needs the
host `moc`/`qmlcachegen`/`androiddeployqt` binaries.

```bash
pipx install aqtinstall

# Target kits (add -m qtmultimedia; it is not in the base kit)
aqt install-qt all_os android 6.11.1 android_arm64_v8a -m qtmultimedia -O ~/Qt
aqt install-qt all_os android 6.11.1 android_x86_64  -m qtmultimedia -O ~/Qt

# Host kit used as QT_HOST_PATH
aqt install-qt mac desktop 6.11.1 -O ~/Qt \
  --archives qtbase qtdeclarative qtshadertools qttools qtmultimedia qtwebsockets qtsvg
```

Result: `~/Qt/6.11.1/{android_arm64_v8a,android_x86_64,macos}`.

Use **arm64-v8a for a real phone** and **x86_64 for the emulator** —
an arm64 APK will not run on the x86_64 emulator image.

### Native dependencies (run before cmake)

Two setup scripts follow the main tree's DFNR pattern: download or
build into a gitignored `third_party/`, and let CMake auto-detect the
result. Both are optional in the sense that the app still builds
without them, but each disables a feature.

```bash
./tools/setup-openssl-android.sh   # REQUIRED for SmartLink/WAN
./tools/setup-opus-android.sh      # optional: Opus RX audio
```

- **OpenSSL** — Qt on Android ships **no TLS backend**. Without this,
  every `QSslSocket` fails with "TLS initialization failed" and the
  entire SmartLink path is dead. The script fetches KDAB's
  `android_openssl` prebuilts; CMake bundles them via
  `QT_ANDROID_EXTRA_LIBS`.
- **Opus** — sha256-pinned opus 1.5.2, cross-compiled static per ABI.
  Without it the OPUS/PCM toggle is hidden and audio stays
  uncompressed.

Configure prints which of the two it found:

```
-- Android OpenSSL bundled (…/third_party/android_openssl/ssl_3/x86_64)
-- Opus decode enabled (…/third_party/opus/x86_64)
```

### Optional: Opus in the test harness

Only needed if you want the fake radio to send *real* Opus frames.
PCM mode needs none of this.

```bash
brew install opus                       # host libopus for ctypes
python3 -m venv /tmp/spike-venv
/tmp/spike-venv/bin/pip install opuslib
# run the harness with the library visible:
DYLD_LIBRARY_PATH=/usr/local/opt/opus/lib /tmp/spike-venv/bin/python tools/fake_radio_tcp.py
```

---

## 2. Build

Emulator build (x86_64):

```bash
~/Qt/6.11.1/android_x86_64/bin/qt-cmake -S . -B build-x86 -G Ninja \
  -DQT_HOST_PATH=$HOME/Qt/6.11.1/macos \
  -DANDROID_SDK_ROOT=$ANDROID_SDK_ROOT \
  -DANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/27.2.12479018
cmake --build build-x86 --target apk
```

Phone build (arm64-v8a) — identical, swapping the kit and build dir:

```bash
~/Qt/6.11.1/android_arm64_v8a/bin/qt-cmake -S . -B build -G Ninja \
  -DQT_HOST_PATH=$HOME/Qt/6.11.1/macos \
  -DANDROID_SDK_ROOT=$ANDROID_SDK_ROOT \
  -DANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/27.2.12479018
cmake --build build --target apk
```

APK lands at:

```
build-x86/android-build/build/outputs/apk/debug/android-build-debug.apk
```

---

## 3. Emulator

### Create the AVD (once)

```bash
avdmanager create avd -n spike \
  -k "system-images;android-35;google_apis;x86_64" -d pixel_7
```

### Run it

```bash
emulator -avd spike -gpu swiftshader_indirect            # windowed, with audio
emulator -avd spike -no-window -no-audio -gpu swiftshader_indirect &   # headless/CI
```

Use the **windowed** form when you want to see and hear the app; audio
comes out of the host. Use headless for scripted checks.

Wait for boot rather than guessing:

```bash
adb wait-for-device
until [ "$(adb shell getprop sys.boot_completed | tr -d '\r')" = "1" ]; do sleep 3; done
```

### Install and launch

```bash
adb install -r build-x86/android-build/build/outputs/apk/debug/android-build-debug.apk
adb shell am start -n org.aethersdr.companion/org.qtproject.qt.android.bindings.QtActivity
```

---

## 4. Harness wiring

The emulator is NAT'd, so host and guest are bridged explicitly.
`adb reverse` sends guest→host (TCP); `adb emu redir` sends host→guest
(UDP).

```bash
# guest -> host (TCP): command channel, SmartLink broker, TLS radio front
adb reverse tcp:4992  tcp:4993      # manual-connect path
adb reverse tcp:14443 tcp:14443     # SmartLink broker
adb reverse tcp:14994 tcp:14994     # TLS radio front

# host -> guest (UDP): discovery datagrams and the VITA stream
adb emu 'redir add udp:14992:4992'  # discovery  (host 14992 -> guest 4992)
adb emu 'redir add udp:24993:14993' # VITA audio/FFT/waterfall/meters
```

Then start the fakes (from this directory):

```bash
python3 tools/fake_radio_tcp.py &   # command channel + VITA senders
python3 tools/fake_smartlink.py &   # TLS broker + TLS radio front (WAN)
python3 tools/fake_radio.py         # discovery datagrams, 30 × 1 s
```

In the app:

- **Discovery** — the FLEX-6600 card appears within a second of
  `fake_radio.py` running; tap it.
- **Manual** — type `127.0.0.1`, tap Connect.
- **SmartLink/WAN** — type `127.0.0.1:14443` in the SmartLink *email*
  field and tap Login. A `host:port` value there switches the app into
  harness mode, skipping Auth0. Then Connect on the WAN card.

### Exercising the certificate-pin mismatch

The harness caches its self-signed cert, so the fingerprint is stable
across restarts. To simulate a changed/attacker certificate:

```bash
pkill -f fake_smartlink.py
rm -rf tools/.harness-certs     # forces a fresh self-signed cert
python3 tools/fake_smartlink.py &
```

Reconnect over WAN: the app should raise the mismatch dialog and send
**no** `wan validate` until you Accept. `fake_smartlink.py` logs every
line it receives, which is how to verify that.

---

## 5. Teardown

```bash
adb emu kill
pkill -f fake_radio_tcp.py; pkill -f fake_smartlink.py; pkill -f fake_radio.py
```

---

## 6. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `compileSdk … at least 36` build failure | Install `platforms;android-36` (see above). |
| `TLS initialization failed` on SmartLink login | OpenSSL not bundled — run `tools/setup-openssl-android.sh`, then re-run cmake configure. |
| App can't reach the host at `10.0.2.2` | That alias is **not routable** from the API-35 emulator's WiFi stack. Use `adb reverse` and point the app at `127.0.0.1`. |
| Discovery datagrams never arrive | Host port 4992 is probably owned by a running desktop AetherSDR; the harness deliberately uses host 14992 instead. Check `adb emu 'redir list'`. |
| Opus toggle missing from the toolbar | Built without libopus — run `tools/setup-opus-android.sh` and reconfigure. |
| `uiautomator dump` returns nothing | Intermittent on this image; retry, or use `adb exec-out screencap -p > shot.png`. |
| Scripted taps hit the wrong control | Layout shifts when status lines appear or the keyboard opens. Read coordinates from a fresh `uiautomator dump` instead of reusing them. |

---

## 7. Real-device notes

The arm64-v8a APK installs on a phone with USB debugging enabled:

```bash
adb install -r build/android-build/build/outputs/apk/debug/android-build-debug.apk
```

No `adb reverse`/`redir` wiring is needed there — put the phone on the
same WiFi as the radio and use the real discovery path. The
hardware-only checks still outstanding are tracked in
[`README.md`](README.md) and in the status issue.
