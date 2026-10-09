# The iOS simulator's toolchain, as Maul Window's: arm64, iOS 15, tests
# run through simctl spawn on the device MUI_IOS_DEVICE names, or the
# booted one (record mui-0008).
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_OSX_SYSROOT iphonesimulator)
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64)
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
endif()
if(DEFINED ENV{MUI_IOS_DEVICE})
    set(CMAKE_CROSSCOMPILING_EMULATOR xcrun;simctl;spawn;$ENV{MUI_IOS_DEVICE})
else()
    set(CMAKE_CROSSCOMPILING_EMULATOR xcrun;simctl;spawn;booted)
endif()
