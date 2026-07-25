cmake_minimum_required(VERSION 3.25)

# Android companion spike — throwaway Phase-0/1 prototype (see README.md).
# Self-contained on purpose: does NOT link aethercore and is not added to
# the root build. Configure with the Qt-for-Android kit's qt-cmake.
project(AetherCompanionSpike VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Network Multimedia)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(aethercompanion
    src/main.cpp
    src/CertPinStore.cpp
    src/CertPinStore.h
    src/ConnectionModel.cpp
    src/ConnectionModel.h
    src/DiscoveryModel.cpp
    src/DiscoveryModel.h
    src/SpectrumStrip.cpp
    src/SpectrumStrip.h
    src/VitaStream.cpp
    src/VitaStream.h
    src/SliceListModel.cpp
    src/SliceListModel.h
    src/SmartLinkModel.cpp
    src/SmartLinkModel.h
)

qt_add_qml_module(aethercompanion
    URI AetherCompanion
    VERSION 1.0
    QML_FILES
        qml/Main.qml
    SOURCES
        src/SpectrumStrip.cpp
        src/SpectrumStrip.h
        src/WaterfallStrip.cpp
        src/WaterfallStrip.h
)

# The generated QML type registration includes <SpectrumStrip.h>.
target_include_directories(aethercompanion PRIVATE src)

# OpenSSL for QSslSocket (run tools/setup-openssl-android.sh before
# cmake) — Qt on Android has no TLS backend without bundled libs.
set(_ssl_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_openssl/ssl_3/${ANDROID_ABI}")
if(EXISTS "${_ssl_root}/libssl_3.so")
    set_property(TARGET aethercompanion APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
        "${_ssl_root}/libssl_3.so"
        "${_ssl_root}/libcrypto_3.so")
    message(STATUS "Android OpenSSL bundled (${_ssl_root})")
else()
    message(STATUS "Android OpenSSL missing — TLS (SmartLink) will fail; run tools/setup-openssl-android.sh")
endif()

# Optional libopus (run tools/setup-opus-android.sh before cmake).
# Auto-detects per-ABI like the main tree's DFNR setup pattern.
set(_opus_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opus/${ANDROID_ABI}")
if(EXISTS "${_opus_root}/lib/libopus.a")
    target_include_directories(aethercompanion PRIVATE "${_opus_root}/include")
    target_link_libraries(aethercompanion PRIVATE "${_opus_root}/lib/libopus.a")
    target_compile_definitions(aethercompanion PRIVATE HAVE_OPUS)
    message(STATUS "Opus decode enabled (${_opus_root})")
else()
    message(STATUS "Opus decode disabled — run tools/setup-opus-android.sh")
endif()

set_target_properties(aethercompanion PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    QT_ANDROID_MIN_SDK_VERSION 28
    QT_ANDROID_TARGET_SDK_VERSION 35
)

target_link_libraries(aethercompanion PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Network
    Qt6::Multimedia
)
