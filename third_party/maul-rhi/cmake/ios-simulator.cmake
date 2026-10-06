# The iOS simulator, for the tests of the Metal driver built for iOS:
#
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/ios-simulator.cmake
#
# arm64 unless CMAKE_OSX_ARCHITECTURES says otherwise, iOS 15 unless
# CMAKE_OSX_DEPLOYMENT_TARGET does. Tests run on a booted simulator (or
# MWIN_IOS_DEVICE, as Maul Window names it) through simctl spawn.
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_OSX_SYSROOT iphonesimulator)
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64)
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
endif()
if(DEFINED ENV{MWIN_IOS_DEVICE})
    set(CMAKE_CROSSCOMPILING_EMULATOR xcrun;simctl;spawn;$ENV{MWIN_IOS_DEVICE})
else()
    set(CMAKE_CROSSCOMPILING_EMULATOR xcrun;simctl;spawn;booted)
endif()
