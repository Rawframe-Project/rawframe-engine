# The Android emulator (mrhi-0017), for the tests:
#
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/android-emulator.cmake
#
# The NDK's toolchain from ANDROID_NDK_HOME (r28 or newer), x86_64 unless
# ANDROID_ABI says otherwise, Android 11 (API 30) unless ANDROID_PLATFORM
# does. Tests run on the running emulator (or the device ANDROID_SERIAL
# names) through tools/run_android.sh.
if(NOT DEFINED ENV{ANDROID_NDK_HOME})
    message(FATAL_ERROR "ANDROID_NDK_HOME must name the NDK")
endif()
if(NOT ANDROID_ABI)
    set(ANDROID_ABI x86_64)
endif()
if(NOT ANDROID_PLATFORM)
    set(ANDROID_PLATFORM 30)
endif()
include($ENV{ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake)
set(CMAKE_CROSSCOMPILING_EMULATOR ${CMAKE_CURRENT_LIST_DIR}/../tools/run_android.sh)
