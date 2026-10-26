# Platforms

Linux is where the engine is developed and fully checked. The other targets are built and played by the extended nightly run (`.github/workflows/extended.yml`) and the scripts below.

| Target | Window and input | Drawing | Where it is checked |
|---|---|---|---|
| Linux x86-64 | Wayland and X11 | Vulkan | every tier, on the self-hosted runner |
| Linux arm64 | Wayland and X11 | Vulkan | extended nightly |
| Windows | Win32 | Direct3D 12 | extended nightly |
| macOS | AppKit | Metal | extended nightly |
| Web | a canvas, without Emscripten | WebGPU | the `full` tier: the web build under Node, a page against a dedicated server, real browsers |
| Android | NativeActivity | Vulkan | extended nightly, in an emulator |
| iOS | UIKit | Metal | `ios.yml`, on request, in a simulator |
| OpenXR headsets | the runtime's | Vulkan, made by the runtime | every tier, against Monado's headless service |

The dedicated server builds wherever the engine does and never links rendering, window, UI or audio code.

## Windows

`windows-development` builds with `clang-cl` and MSVC's libraries, from a shell with MSVC's developer environment. Configure needs Perl for OpenSSL (`RAWFRAME_PERL`), Git's bash (`RAWFRAME_BASH`) and Python. Only a Windows build sees bugs of the MSVC ABI, so a change to platform code is checked by dispatching `extended` with `only: windows` before it merges.

## Android

Set `RAWFRAME_ANDROID_NDK` to NDK r29, then configure and build `android-development`.

```sh
tools/android_apk.sh <out.apk> out/android-development/hosts/android_client/librawframe_client.so <package> <label>
ADB="adb -s emulator-5554" tools/android_tests.sh out/android-development [<pattern>]
ADB="adb -s emulator-5554" tools/android_play_exported.sh out/android-development games/tycoon
```

`android_apk.sh` makes the client's package with the SDK's own tools and no Gradle; with a keystore and alias it signs a release package. `android_tests.sh` pushes the test programs and the source files they read to the device and runs them there. `android_play_exported.sh` exports a game on the device and plays the package made; `android_read_aloud.sh` prints the UI as accessibility services read it.

## iOS

The engine is compiled for the iOS simulator against LLVM 20's libc++, which `tools/build_libcxx_ios.sh <prefix>` builds from its released source (Xcode's libc++ lacks parts of C++23's library before iOS 26).

```sh
cmake --preset ios-simulator && cmake --build out/ios-simulator
tools/ios_tests.sh out/ios-simulator [<pattern>]
tools/ios_play.sh out/ios-simulator
tools/ios_play_exported.sh out/ios-simulator [<game>]
```

`RAWFRAME_IOS_DEVICE` names the simulator, the booted one when unset.

## OpenXR

`tools/xr_run.sh <command>` runs a command against a headless Monado service of its own (a simulated headset and two simple controllers) and sets `RAWFRAME_REQUIRE_XR`, so a test that finds no runtime fails instead of skipping. A game names its runtime in `xr.runtime`; the loader's own `XR_RUNTIME_JSON` is never trusted.

## Studio packages

`tools/package_studio.sh <build tree> <output> [<game>...]` packages Studio for an author without a build: Studio, the cook, the server, the client, the authoring tool, the thumbnail tool, the debug adapter with its VS Code extension, everything Export runs, the shader toolchain where the machine has one, the templates and the named sample games.
