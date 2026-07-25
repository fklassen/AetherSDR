#!/usr/bin/env bash
# Fetch KDAB's prebuilt OpenSSL 3.x libraries for Android (the standard
# way to give Qt's QSslSocket a TLS backend on Android — Qt does not
# bundle OpenSSL). Installs into gitignored third_party/android_openssl;
# the spike CMake wires them up via QT_ANDROID_EXTRA_LIBS.
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p third_party
cd third_party

if [ ! -d android_openssl ]; then
    git clone --depth 1 https://github.com/KDAB/android_openssl.git
else
    echo "android_openssl already present"
fi

ls android_openssl/ssl_3/arm64-v8a/libssl_3.so \
   android_openssl/ssl_3/x86_64/libcrypto_3.so > /dev/null
echo "done — OpenSSL prebuilts ready under third_party/android_openssl"
