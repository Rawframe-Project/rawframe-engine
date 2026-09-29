// The one device (D277): opened on the first adapter that answers, a
// software rasterizer allowed, and shown to work by clearing a target and
// reading it back. A machine with no adapter skips, unless
// RAWFRAME_REQUIRE_GPU is set, as the check sets it where lavapipe is, and
// makes lavapipe the only driver the Vulkan loader sees.

#include "rawframe/render/device.h"
#include "rawframe/render/errors.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <maul-rhi/device.h>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <memory>

using namespace rawframe;

namespace {

bool required() {
    const char* value = std::getenv("RAWFRAME_REQUIRE_GPU");
    return value != nullptr && value[0] != '\0';
}

/// A device opened, or none where no adapter answers and none is required.
std::unique_ptr<render::Device> opened() {
    auto device = render::Device::request({.allowSoftware = true});
    RAWFRAME_EXPECT(device.has_value());
    if (!device.has_value()) {
        return nullptr;
    }
    // Natively the answers come at once; a bound keeps a broken layer from
    // spinning the test.
    for (int poll = 0; poll < 1000; ++poll) {
        const auto kOpen = (*device)->open();
        if (!kOpen.has_value()) {
            const bool kNoAdapter = kOpen.error().domain() == render::kRenderDomain &&
                                    kOpen.error().code() == render::code(render::RenderError::NoAdapter);
            RAWFRAME_EXPECT(kNoAdapter && !required());
            if (kNoAdapter) {
                std::puts("skip: no adapter");
            }
            return nullptr;
        }
        if (*kOpen) {
            return std::move(*device);
        }
    }
    RAWFRAME_EXPECT(false);
    return nullptr;
}

} // namespace

RAWFRAME_TEST(ADeviceClearsATargetAndReadsItBack) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    RAWFRAME_EXPECT(kDevice->adapter().has_value() && !kDevice->adapter()->name.empty());
    // Asked again, a ready device stays ready.
    const auto kAgain = kDevice->open();
    RAWFRAME_EXPECT(kAgain.has_value() && *kAgain);
    mrhiDevice* device = kDevice->native();
    RAWFRAME_EXPECT(device != nullptr);

    constexpr std::uint32_t kSide = 8;
    const mrhiFrameDef kFrame = mrhiDefaultFrameDef();
    RAWFRAME_EXPECT(mrhiBeginFrame(device, &kFrame) == mrhi_success);
    mrhiTextureDef targetDef = mrhiDefaultTextureDef();
    targetDef.format = mrhi_formatRgba8Unorm;
    targetDef.width = kSide;
    targetDef.height = kSide;
    mrhiResourceId target{};
    RAWFRAME_EXPECT(mrhiDeclareTexture(device, &targetDef, &target) == mrhi_success);
    mrhiPassDef clearDef = mrhiDefaultPassDef();
    clearDef.colorTargets[0].resource = target;
    clearDef.colorTargets[0].load = mrhi_loadClear;
    clearDef.colorTargets[0].store = mrhi_storeKeep;
    clearDef.colorTargets[0].clear = mrhiClearColor{.red = 0.25F, .green = 0.5F, .blue = 1.0F, .alpha = 1.0F};
    clearDef.colorTargetCount = 1;
    mrhiPassId clearing{};
    RAWFRAME_EXPECT(mrhiAddPass(device, &clearDef, &clearing) == mrhi_success);
    const mrhiAccess kRead{.resource = target,
                           .kind = mrhi_accessCopySource,
                           .range = {.baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &kRead;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId reading{};
    RAWFRAME_EXPECT(mrhiAddPass(device, &readDef, &reading) == mrhi_success);
    RAWFRAME_EXPECT(mrhiCompileFrame(device) == mrhi_success);
    RAWFRAME_EXPECT(mrhiBeginPass(device, clearing) == mrhi_success && mrhiEndPass(device, clearing) == mrhi_success);
    mrhiTextureCopy source{};
    source.resource = target;
    const mrhiExtent3d kExtent{.width = kSide, .height = kSide, .depthOrLayers = 1};
    mrhiRequestId pixels{};
    RAWFRAME_EXPECT(mrhiBeginPass(device, reading) == mrhi_success &&
                    mrhiReadTexture(device, reading, &source, &kExtent, &pixels) == mrhi_success &&
                    mrhiEndPass(device, reading) == mrhi_success);
    mrhiRequestId token{};
    RAWFRAME_EXPECT(mrhiSubmitFrame(device, &token) == mrhi_success);
    RAWFRAME_EXPECT(mrhiWaitFrame(device, token, 10'000'000'000ULL) == mrhi_success);
    // The readback is answered through the device's queue, taken by the
    // one that asked; the frame's own answer waits for its asker.
    kDevice->pump();
    const auto kAnswer = kDevice->answer(render::requestKey(pixels.index1, pixels.generation));
    RAWFRAME_EXPECT(kAnswer.has_value() && kAnswer->has_value());
    RAWFRAME_EXPECT(!kDevice->answer(render::requestKey(pixels.index1, pixels.generation)).has_value());
    const auto kDone = kDevice->answer(render::requestKey(token.index1, token.generation));
    RAWFRAME_EXPECT(kDone.has_value() && kDone->has_value() && !kDevice->lost());
    std::array<std::uint8_t, std::size_t{kSide} * kSide * 4> image{};
    std::size_t taken = 0;
    RAWFRAME_EXPECT(mrhiTakeReadback(device, pixels, image.data(), image.size(), &taken) == mrhi_success &&
                    taken == image.size());
    // A quarter, a half, and all of blue, opaque: 64, 128, 255, 255, give
    // or take the rounding of a quarter.
    bool cleared = true;
    for (std::size_t at = 0; at < image.size(); at += 4) {
        cleared = cleared && (image[at] == 63 || image[at] == 64) && (image[at + 1] == 127 || image[at + 1] == 128) &&
                  image[at + 2] == 255 && image[at + 3] == 255;
    }
    RAWFRAME_EXPECT(cleared);
}

RAWFRAME_TEST(NoHardwareAdapterIsNotASoftwareOne) {
    // Asked without software, a machine whose only adapter is lavapipe
    // finds none; one with a GPU opens it. Neither is a crash or a hang.
    auto device = render::Device::request({.allowSoftware = false});
    RAWFRAME_EXPECT(device.has_value());
    if (!device.has_value()) {
        return;
    }
    for (int poll = 0; poll < 1000; ++poll) {
        const auto kOpen = (*device)->open();
        if (!kOpen.has_value() || *kOpen) {
            RAWFRAME_EXPECT(!kOpen.has_value() || !(*device)->adapter()->software);
            return;
        }
    }
    RAWFRAME_EXPECT(false);
}
