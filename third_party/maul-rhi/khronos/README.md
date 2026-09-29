# Khronos Vulkan headers

The C headers of the Vulkan API, kept exactly as published, for the
Vulkan driver (mrhi-0003; the library profile, mrhi-0001, names the
Vulkan loader as a platform dependency). They are the platform's API,
not code of this project.

- **Source:** https://github.com/KhronosGroup/Vulkan-Headers
- **Version:** tag `vulkan-sdk-1.4.363.0`, commit
  `6802bb4733b63ed5efd3adb308a6c885ef180ea1`
- **Licence:** `Apache-2.0 OR MIT`, as each file's SPDX line says; the
  texts are in `LICENSES/`.
- **Files:** `include/vulkan/vk_platform.h`,
  `include/vulkan/vulkan_core.h`, the platform headers of the surface
  sources (`vulkan_android.h`, `vulkan_metal.h`, `vulkan_wayland.h`,
  `vulkan_win32.h`, `vulkan_xcb.h`) and `include/vk_video/*.h`, under
  `vulkan/` and `vk_video/`; `LICENSES/` from the repository's root.

An update replaces the files from a newer tag and this list.

| File | SHA-256 |
| --- | --- |
| `LICENSES/Apache-2.0.txt` | `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30` |
| `LICENSES/MIT.txt` | `1ca3502222d967f3be5751c55f6b7ee735b5383909c3b501495f54b216dbf227` |
| `vk_video/vulkan_video_codec_av1std_decode.h` | `7e3a1ce177c12546d410f3179ce1b81f2da7a8eba4f525b29cd563ab4099c0e5` |
| `vk_video/vulkan_video_codec_av1std_encode.h` | `8d166b4543260a38347860443b1a59c7a5e86cdb0d0facaf4c704f667de030e3` |
| `vk_video/vulkan_video_codec_av1std.h` | `c75c1d324b97d247aef0008024bc7f3adb98c741ab7f66c882ec38fdacc7ee33` |
| `vk_video/vulkan_video_codec_h264std_decode.h` | `37b970c3d80536ad1ac074cfe19b58ee03c3075378e02179e1dc5e4351266821` |
| `vk_video/vulkan_video_codec_h264std_encode.h` | `227e092b53c4e7ca1a948ed021704511c1ed16040cd6188ff6703e5ae66db64d` |
| `vk_video/vulkan_video_codec_h264std.h` | `fded484cef9f90fbbd089e0647268f36e5f4292179fbc5428a2c9e8d7709bbe9` |
| `vk_video/vulkan_video_codec_h265std_decode.h` | `879a0dd370a1b1ad184638906c52bdfe7d80d639fbc5a5baa8b02d7c5b60b147` |
| `vk_video/vulkan_video_codec_h265std_encode.h` | `abb3e72af22e4e0a3dbe5dff7be1b275949388809fb3987830341e8f495ea1c7` |
| `vk_video/vulkan_video_codec_h265std.h` | `0b81f8986ada015ef2e127449eff9d9634899bb59a0a9277a4054e9ec4416c12` |
| `vk_video/vulkan_video_codecs_common.h` | `d2e7caa396c521d03d491a269572b1d31b925b5127d22fda14199951ebae89f8` |
| `vk_video/vulkan_video_codec_vp9std_decode.h` | `1ceb1a8d0e3370e508cf688a6e57dc314cd82186b60f3cef420ea4b1b483865d` |
| `vk_video/vulkan_video_codec_vp9std.h` | `0a47125865376a3fe7014b69ff6db9d04e30ebf8c4d15664f1d894649ad5c09d` |
| `vulkan/vk_platform.h` | `a2cd9085c66776845d2524c3b0e73ccf4827ba046aa6f676c4b8ddbed47d6552` |
| `vulkan/vulkan_android.h` | `9dcce545a790b5b1ce00e103ade95c8efa4c8a0bf2ff39865d76a7d2f987aef2` |
| `vulkan/vulkan_core.h` | `2f64fd6c7c3f342b0e44f039f1c359b7f0e6cfcb71a7dca1cc5156282faa3b51` |
| `vulkan/vulkan_metal.h` | `d5fe0caf881cc9c72ea2ba31ca86c684e97cdec6741d6e6298ecbd8f58dc8c5e` |
| `vulkan/vulkan_wayland.h` | `6c4146149d45bcbb5a22c7540feaa396ee9a49f077737c7a343c62de5199fbc7` |
| `vulkan/vulkan_win32.h` | `72f0b6de71287d3b04d12235e4f2fef8f343126f85be4963c516d31d5bb4c099` |
| `vulkan/vulkan_xcb.h` | `3d49f5eb52090e72e1cf7cde545088225ac524a2ff1d1f35737e7d944474c1b7` |
