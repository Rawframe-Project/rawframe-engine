# The iOS simulator on 64-bit ARM (D583, D584): LLVM 20's clang, as macOS
# builds with (D236), against LLVM 20's libc++ built for the simulator by
# tools/build_libcxx_ios.sh into RAWFRAME_IOS_LIBCXX, linked statically, at
# iOS 16.3, where a double's std::to_chars begins. RAWFRAME_LLVM names the
# LLVM prefix, Homebrew's llvm@20 unless set. Run on macOS with Xcode.
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_OSX_SYSROOT iphonesimulator)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET 16.3)
if(DEFINED ENV{RAWFRAME_LLVM})
    set(rawframe_llvm "$ENV{RAWFRAME_LLVM}")
else()
    set(rawframe_llvm /opt/homebrew/opt/llvm@20)
endif()
set(CMAKE_C_COMPILER "${rawframe_llvm}/bin/clang")
set(CMAKE_CXX_COMPILER "${rawframe_llvm}/bin/clang++")
set(CMAKE_C_COMPILER_TARGET arm64-apple-ios16.3-simulator)
set(CMAKE_CXX_COMPILER_TARGET arm64-apple-ios16.3-simulator)
# Objective-C, for what Apple's frameworks are reached through.
set(CMAKE_OBJC_COMPILER "${rawframe_llvm}/bin/clang")
set(CMAKE_OBJC_COMPILER_TARGET arm64-apple-ios16.3-simulator)
if(NOT EXISTS "$ENV{RAWFRAME_IOS_LIBCXX}/complete")
    message(FATAL_ERROR "RAWFRAME_IOS_LIBCXX must name a libc++ tools/build_libcxx_ios.sh built")
endif()
set(rawframe_libcxx "$ENV{RAWFRAME_IOS_LIBCXX}")
set(CMAKE_CXX_FLAGS_INIT "-nostdinc++ -isystem ${rawframe_libcxx}/include/c++/v1")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-nostdlib++")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-nostdlib++")
# Homebrew's LLVM carries no compiler runtime for the simulator, which
# `@available` checks call into (`__isPlatformVersionAtLeast`): Xcode's is
# linked, as Xcode's own clang links it.
execute_process(COMMAND xcrun --sdk iphonesimulator clang -print-resource-dir
                OUTPUT_VARIABLE rawframe_xcode_resources OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(rawframe_ios_runtime "${rawframe_xcode_resources}/lib/darwin/libclang_rt.iossim.a")
set(CMAKE_CXX_STANDARD_LIBRARIES "${rawframe_libcxx}/lib/libc++.a ${rawframe_ios_runtime}")
set(CMAKE_C_STANDARD_LIBRARIES "${rawframe_libcxx}/lib/libc++.a ${rawframe_ios_runtime}")
set(CMAKE_OBJC_STANDARD_LIBRARIES "${rawframe_ios_runtime}")
# Programs the build runs are this machine's, never the simulator's.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
