#!/usr/bin/env bash
# Build static libopus for the Android companion spike (DFNR-style
# run-before-cmake setup: downloads + cross-compiles, nothing vendored).
# Output: third_party/opus/<abi>/{lib/libopus.a,include/opus/*.h}
# The spike's CMakeLists auto-detects it and defines HAVE_OPUS.
set -euo pipefail

OPUS_VERSION=1.5.2
OPUS_SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
NDK_ROOT=${ANDROID_NDK_ROOT:-${ANDROID_SDK_ROOT:?set ANDROID_SDK_ROOT or ANDROID_NDK_ROOT}/ndk/27.2.12479018}
ABIS=${ABIS:-"arm64-v8a x86_64"}

cd "$(dirname "$0")/.."
mkdir -p third_party/build
cd third_party/build

tarball=opus-${OPUS_VERSION}.tar.gz
if [ ! -f "$tarball" ]; then
    curl -LO "https://downloads.xiph.org/releases/opus/${tarball}"
fi
echo "${OPUS_SHA256}  ${tarball}" | shasum -a 256 -c -
rm -rf "opus-${OPUS_VERSION}"
tar xzf "$tarball"

for abi in $ABIS; do
    echo "=== building libopus ${OPUS_VERSION} for ${abi} ==="
    cmake -S "opus-${OPUS_VERSION}" -B "opus-${abi}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${NDK_ROOT}/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="${abi}" \
        -DANDROID_PLATFORM=android-28 \
        -DCMAKE_BUILD_TYPE=Release \
        -DOPUS_BUILD_SHARED_LIBRARY=OFF \
        -DOPUS_BUILD_TESTING=OFF \
        -DOPUS_BUILD_PROGRAMS=OFF \
        -DCMAKE_INSTALL_PREFIX="$(pwd)/../opus/${abi}"
    cmake --build "opus-${abi}" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
    cmake --install "opus-${abi}"
done

echo "done — libopus installed under third_party/opus/{$(echo $ABIS | tr ' ' ',')}"
