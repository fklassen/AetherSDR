# Developing the Android companion spike

Everything needed to build, run, and exercise the spike from a clean
machine. This covers the toolchain and the emulator loop; for what the
spike *is* and which phases it proves, see [`README.md`](README.md).

Nothing here touches the main AetherSDR build. The spike is a
self-contained CMake project that does not link `aethercore`, and the
root `CMakeLists.txt` is untouched.

Written for macOS on **both Apple Silicon and Intel**. Linux hosts work
too — substitute the Linux host packages and Homebrew equivalents.

---

## 0. Host architecture — read this first

Apple Silicon and Intel differ in three places that matter: the
Homebrew prefix, which Android ABI you build, and which emulator system
image runs. Export these once and the rest of the guide is
copy-pasteable as written.

**Apple Silicon (M1/M2/M3/M4):**

```bash
export BREW_PREFIX=/opt/homebrew
export SPIKE_ABI=arm64-v8a          # emulator AND phone are both arm64
export SPIKE_QT_KIT=android_arm64_v8a
export SPIKE_BUILD_DIR=build
```

**Intel:**

```bash
export BREW_PREFIX=/usr/local
export SPIKE_ABI=x86_64             # emulator only; phone builds are arm64-v8a
export SPIKE_QT_KIT=android_x86_64
export SPIKE_BUILD_DIR=build-x86
```

Then, on either:

```bash
export ANDROID_SDK_ROOT=$BREW_PREFIX/share/android-commandlinetools
export PATH="$PATH:$ANDROID_SDK_ROOT/platform-tools:$ANDROID_SDK_ROOT/emulator"
```

| | Apple Silicon | Intel |
|---|---|---|
| Homebrew prefix | `/opt/homebrew` | `/usr/local` |
| Emulator system image | `…;google_apis;arm64-v8a` | `…;google_apis;x86_64` |
| Emulator APK ABI | `arm64-v8a` | `x86_64` |
| Phone APK ABI | `arm64-v8a` | `arm64-v8a` |
| Qt target kits needed | **one** (`android_arm64_v8a`) | **two** (arm64 for phone, x86_64 for emulator) |
| Suggested `-gpu` flag | `host` (Metal) | `swiftshader_indirect` |

**The Apple Silicon simplification:** the emulator runs arm64, which is
also what a phone runs, so one APK serves both and you only need one Qt
target kit. On Intel the emulator needs a separate x86_64 build — an
arm64 APK will not install on an x86_64 image, and vice versa.

**The `darwin-x86_64` path is correct on Apple Silicon too.** The NDK
keeps that directory name for compatibility, but the toolchain binaries
inside are universal (verify with
`file $ANDROID_SDK_ROOT/ndk/*/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang`
— it reports both `x86_64` and `arm64`). Do not substitute a
`darwin-arm64` path; it does not exist and cmake will fail.

---

## 1. Prerequisites

### Java + Android SDK

```bash
brew install --cask temurin@21 android-commandlinetools
```

Gradle needs a JDK; 21 is what this was built and tested against.
`sdkmanager` downloads host-architecture-appropriate packages
automatically (notably the `emulator` package), so the commands below
are identical on both architectures.

Accept licences and install packages — note `$SPIKE_ABI` in the system
image:

```bash
yes | sdkmanager --licenses
sdkmanager \
  "platform-tools" \
  "platforms;android-35" "build-tools;35.0.0" \
  "platforms;android-36" "build-tools;36.0.0" \
  "ndk;27.2.12479018" \
  "emulator" \
  "system-images;android-35;google_apis;$SPIKE_ABI"
```

Why both 35 and 36: the app targets SDK 35, but the Android Gradle
Plugin that Qt 6.11 drives **requires `compileSdk` 36** and fails the
build without it. Platform 36 is only there to satisfy the compile
step.

### Qt for Android

Qt does not ship Android kits with the Homebrew build, so use
[`aqtinstall`](https://github.com/miurahr/aqtinstall). A host (desktop)
kit is required alongside the target kit — the Android build needs the
host `moc`/`qmlcachegen`/`androiddeployqt` binaries.

```bash
pipx install aqtinstall

# Target kit for your emulator/phone ABI (-m qtmultimedia is not in the base kit)
aqt install-qt all_os android 6.11.1 $SPIKE_QT_KIT -m qtmultimedia -O ~/Qt

# On Intel, also install the phone kit (Apple Silicon already has it above):
#   aqt install-qt all_os android 6.11.1 android_arm64_v8a -m qtmultimedia -O ~/Qt

# Host kit used as QT_HOST_PATH — the macOS kit is universal, same command on both
aqt install-qt mac desktop 6.11.1 -O ~/Qt \
  --archives qtbase qtdeclarative qtshadertools qttools qtmultimedia qtwebsockets qtsvg
```

Result: `~/Qt/6.11.1/{android_arm64_v8a,macos}` on Apple Silicon, plus
`android_x86_64` on Intel.

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
  `android_openssl` prebuilts (all ABIs); CMake bundles the matching
  one via `QT_ANDROID_EXTRA_LIBS`.
- **Opus** — sha256-pinned opus 1.5.2, cross-compiled static per ABI.
  Without it the OPUS/PCM toggle is hidden and audio stays
  uncompressed. It builds `arm64-v8a` and `x86_64` by default; override
  with `ABIS="arm64-v8a" ./tools/setup-opus-android.sh` to build only
  what you need.

Both scripts read `ANDROID_SDK_ROOT`/`ANDROID_NDK_ROOT`, so export
those first. Configure prints what it found:

```
-- Android OpenSSL bundled (…/third_party/android_openssl/ssl_3/arm64-v8a)
-- Opus decode enabled (…/third_party/opus/arm64-v8a)
```

### Optional: Opus in the test harness

Only needed if you want the fake radio to send *real* Opus frames.
PCM mode needs none of this.

```bash
brew install opus                       # host libopus for ctypes
python3 -m venv /tmp/spike-venv
/tmp/spike-venv/bin/pip install opuslib
# run the harness with the library visible (note $BREW_PREFIX):
DYLD_LIBRARY_PATH=$BREW_PREFIX/opt/opus/lib /tmp/spike-venv/bin/python tools/fake_radio_tcp.py
```

---

## 2. Build

Using the variables from section 0, one command set works on both
architectures:

```bash
~/Qt/6.11.1/$SPIKE_QT_KIT/bin/qt-cmake -S . -B $SPIKE_BUILD_DIR -G Ninja \
  -DQT_HOST_PATH=$HOME/Qt/6.11.1/macos \
  -DANDROID_SDK_ROOT=$ANDROID_SDK_ROOT \
  -DANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/27.2.12479018
cmake --build $SPIKE_BUILD_DIR --target apk
```

APK lands at:

```
$SPIKE_BUILD_DIR/android-build/build/outputs/apk/debug/android-build-debug.apk
```

**Apple Silicon:** that one APK runs on both the emulator and a phone —
you are done.

**Intel:** the above builds the emulator (x86_64) APK. For a phone,
repeat with the arm64 kit into a separate directory:

```bash
~/Qt/6.11.1/android_arm64_v8a/bin/qt-cmake -S . -B build -G Ninja \
  -DQT_HOST_PATH=$HOME/Qt/6.11.1/macos \
  -DANDROID_SDK_ROOT=$ANDROID_SDK_ROOT \
  -DANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/27.2.12479018
cmake --build build --target apk
```

---

## 3. Emulator

### Create the AVD (once)

```bash
avdmanager create avd -n spike \
  -k "system-images;android-35;google_apis;$SPIKE_ABI" -d pixel_7
```

### Run it

```bash
# Apple Silicon — hardware GPU via Metal
emulator -avd spike -gpu host                                   # windowed, with audio
emulator -avd spike -no-window -no-audio -gpu host &            # headless

# Intel — software rendering is the reliable choice
emulator -avd spike -gpu swiftshader_indirect                   # windowed, with audio
emulator -avd spike -no-window -no-audio -gpu swiftshader_indirect &   # headless
```

Use the **windowed** form when you want to see and hear the app; audio
comes out of the host. Use headless for scripted checks. If `-gpu host`
misbehaves on Apple Silicon, `swiftshader_indirect` is the fallback
everywhere.

Wait for boot rather than guessing:

```bash
adb wait-for-device
until [ "$(adb shell getprop sys.boot_completed | tr -d '\r')" = "1" ]; do sleep 3; done
```

### Install and launch

```bash
adb install -r $SPIKE_BUILD_DIR/android-build/build/outputs/apk/debug/android-build-debug.apk
adb shell am start -n org.aethersdr.companion/org.qtproject.qt.android.bindings.QtActivity
```

---

## 4. Harness wiring

Architecture-independent. The emulator is NAT'd, so host and guest are
bridged explicitly: `adb reverse` sends guest→host (TCP); `adb emu
redir` sends host→guest (UDP).

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
| `INSTALL_FAILED_NO_MATCHING_ABIS` | APK ABI does not match the emulator image. Apple Silicon needs an `arm64-v8a` build and image; Intel needs `x86_64`. See section 0. |
| cmake cannot find the NDK toolchain on Apple Silicon | The prebuilt directory is named `darwin-x86_64` on every macOS host — the binaries are universal. Do not point at `darwin-arm64`. |
| `compileSdk … at least 36` build failure | Install `platforms;android-36` (see section 1). |
| `TLS initialization failed` on SmartLink login | OpenSSL not bundled — run `tools/setup-openssl-android.sh`, then re-run cmake configure. |
| Emulator very slow / black window | Wrong GPU mode for the host. Try `-gpu host` on Apple Silicon, `-gpu swiftshader_indirect` on Intel. |
| App can't reach the host at `10.0.2.2` | That alias is **not routable** from the API-35 emulator's WiFi stack. Use `adb reverse` and point the app at `127.0.0.1`. |
| Discovery datagrams never arrive | Host port 4992 is probably owned by a running desktop AetherSDR; the harness deliberately uses host 14992 instead. Check `adb emu 'redir list'`. |
| Opus toggle missing from the toolbar | Built without libopus for this ABI — run `tools/setup-opus-android.sh` and reconfigure. |
| `uiautomator dump` returns nothing | Intermittent on this image; retry, or use `adb exec-out screencap -p > shot.png`. |
| Scripted taps hit the wrong control | Layout shifts when status lines appear or the keyboard opens. Read coordinates from a fresh `uiautomator dump` instead of reusing them. |

---

## 7. Real-device notes

The `arm64-v8a` APK installs on a phone with USB debugging enabled:

```bash
adb install -r build/android-build/build/outputs/apk/debug/android-build-debug.apk
```

(On Apple Silicon that is `$SPIKE_BUILD_DIR`, the same APK you ran on
the emulator.)

No `adb reverse`/`redir` wiring is needed there — put the phone on the
same WiFi as the radio and use the real discovery path. The
hardware-only checks still outstanding are tracked in
[`README.md`](README.md) and in the status issue.
