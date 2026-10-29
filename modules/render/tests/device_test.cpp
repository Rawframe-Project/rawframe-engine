// The one device: opened on the first adapter that answers, a
// software rasterizer allowed, and shown to work by clearing a target and
// reading it back. A machine with no adapter skips, unless
// RAWFRAME_REQUIRE_GPU is set, as the check sets it where lavapipe is, and
// makes lavapipe the only driver the Vulkan loader sees. A runtime that
// makes the Vulkan objects, as an OpenXR runtime does, has the device opened
// on its own adapter.

#include "../src/present.h"
#include "rawframe/render/device.h"
#include "rawframe/render/errors.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <maul-rhi/device.h>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/surface.h>
#include <memory>
#include <optional>
#include <string_view>
#include <vulkan/vulkan_core.h>

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

/// A runtime that makes the Vulkan objects on the first device the
/// loader lists, as an OpenXR runtime does on the one it presents from, or
/// that names a device the loader never listed.
class Runtime final : public render::VulkanMaker {
public:
    explicit Runtime(bool listed) : listed_(listed) {
    }

    result::Result<void*> instance(const void* createInfo, void* getInstanceProcAddr) override {
        entry_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(getInstanceProcAddr);
        const auto kCreate = reinterpret_cast<PFN_vkCreateInstance>(entry_(VK_NULL_HANDLE, "vkCreateInstance"));
        VkInstance made = VK_NULL_HANDLE;
        if (kCreate == nullptr ||
            kCreate(static_cast<const VkInstanceCreateInfo*>(createInfo), nullptr, &made) != VK_SUCCESS) {
            return refused("the runtime's instance");
        }
        instance_ = made;
        return static_cast<void*>(made);
    }

    result::Result<void*> physicalDevice(void* instance) override {
        if (!listed_) {
            return static_cast<void*>(&unlisted_);
        }
        const auto kList = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
            entry_(static_cast<VkInstance>(instance), "vkEnumeratePhysicalDevices"));
        std::uint32_t count = 1;
        VkPhysicalDevice first = VK_NULL_HANDLE;
        if (kList == nullptr || kList(static_cast<VkInstance>(instance), &count, &first) < VK_SUCCESS || count == 0) {
            return refused("the runtime's device");
        }
        chosen = first;
        return static_cast<void*>(first);
    }

    result::Result<void*> device(void* physicalDevice, const void* createInfo, void*) override {
        const auto kCreate = reinterpret_cast<PFN_vkCreateDevice>(entry_(instance_, "vkCreateDevice"));
        VkDevice made = VK_NULL_HANDLE;
        if (kCreate == nullptr || kCreate(static_cast<VkPhysicalDevice>(physicalDevice),
                                          static_cast<const VkDeviceCreateInfo*>(createInfo),
                                          nullptr,
                                          &made) != VK_SUCCESS) {
            return refused("the runtime's device");
        }
        return static_cast<void*>(made);
    }

    VkPhysicalDevice chosen = VK_NULL_HANDLE;

private:
    static std::unexpected<result::Error> refused(std::string_view what) {
        return result::fail(
            result::ErrorClass::Unavailable, render::kRenderDomain, render::code(render::RenderError::NoAdapter), what);
    }

    bool listed_;
    int unlisted_ = 0;
    PFN_vkGetInstanceProcAddr entry_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
};

/// Opens `device` to the end: true when ready, false when it failed with
/// `NoAdapter`.
std::optional<bool> openToTheEnd(render::Device& device) {
    for (int poll = 0; poll < 1000; ++poll) {
        const auto kOpen = device.open();
        if (!kOpen.has_value()) {
            if (kOpen.error().domain() == render::kRenderDomain &&
                kOpen.error().code() == render::code(render::RenderError::NoAdapter)) {
                return false;
            }
            return std::nullopt;
        }
        if (*kOpen) {
            return true;
        }
    }
    return std::nullopt;
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
    RAWFRAME_EXPECT(mrhiDevice_BeginFrame(device, &kFrame) == mrhi_success);
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
    clearDef.colorTargets[0].clear = mrhiColor{.red = 0.25F, .green = 0.5F, .blue = 1.0F, .alpha = 1.0F};
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
    RAWFRAME_EXPECT(mrhiDevice_CompileFrame(device) == mrhi_success);
    RAWFRAME_EXPECT(mrhiPass_Begin(device, clearing) == mrhi_success && mrhiPass_End(device, clearing) == mrhi_success);
    mrhiTextureCopy source{};
    source.resource = target;
    const mrhiExtent3d kExtent{.width = kSide, .height = kSide, .depthOrLayers = 1};
    mrhiRequestId pixels{};
    RAWFRAME_EXPECT(mrhiPass_Begin(device, reading) == mrhi_success &&
                    mrhiPass_ReadTexture(device, reading, &source, &kExtent, &pixels) == mrhi_success &&
                    mrhiPass_End(device, reading) == mrhi_success);
    mrhiRequestId token{};
    RAWFRAME_EXPECT(mrhiDevice_SubmitFrame(device, &token) == mrhi_success);
    RAWFRAME_EXPECT(mrhiRequest_WaitFrame(device, token, 10'000'000'000ULL) == mrhi_success);
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
    RAWFRAME_EXPECT(mrhiRequest_TakeReadback(device, pixels, image.data(), image.size(), &taken) == mrhi_success &&
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

RAWFRAME_TEST(APolicyPresentsByTheFirstModeOfItsChainTheSurfaceOffers) {
    using render::PresentPolicy;
    // A set holds a mode as the bit its value shifts 1 by: mailbox is mode
    // 1 and bit 2. Read as a mode, a bit names the wrong one.
    constexpr mrhiPresentModes kFifo = mrhi_presentFifoBit;
    constexpr mrhiPresentModes kMailbox = mrhi_presentFifoBit | mrhi_presentMailboxBit;
    constexpr mrhiPresentModes kImmediate = mrhi_presentFifoBit | mrhi_presentImmediateBit;
    constexpr mrhiPresentModes kAll = kMailbox | kImmediate;

    RAWFRAME_EXPECT(render::presentModeFor(PresentPolicy::Immediate, kMailbox) == mrhi_presentMailbox);
    RAWFRAME_EXPECT(render::presentModeFor(PresentPolicy::LowLatencyVsync, kMailbox) == mrhi_presentMailbox);
    RAWFRAME_EXPECT(render::presentModeFor(PresentPolicy::Immediate, kImmediate) == mrhi_presentImmediate);
    RAWFRAME_EXPECT(render::presentModeFor(PresentPolicy::LowLatencyVsync, kImmediate) == mrhi_presentFifo);
    RAWFRAME_EXPECT(render::presentModeFor(PresentPolicy::Immediate, kAll) == mrhi_presentImmediate);
    RAWFRAME_EXPECT(render::presentModeFor(PresentPolicy::LowLatencyVsync, kAll) == mrhi_presentMailbox);
    for (const PresentPolicy kPolicy : {PresentPolicy::Vsync, PresentPolicy::AdaptiveVsync}) {
        RAWFRAME_EXPECT(render::presentModeFor(kPolicy, kAll) == mrhi_presentFifo);
    }
    for (const PresentPolicy kPolicy : {PresentPolicy::LowLatencyVsync, PresentPolicy::Immediate}) {
        RAWFRAME_EXPECT(render::presentModeFor(kPolicy, kFifo) == mrhi_presentFifo);
    }
    // And the policy a configured mode presents by.
    RAWFRAME_EXPECT(render::policyOf(mrhi_presentFifo) == PresentPolicy::Vsync);
    RAWFRAME_EXPECT(render::policyOf(mrhi_presentMailbox) == PresentPolicy::LowLatencyVsync);
    RAWFRAME_EXPECT(render::policyOf(mrhi_presentImmediate) == PresentPolicy::Immediate);
}

RAWFRAME_TEST(ARuntimesDeviceOpensOnItsOwnAdapterAmongThoseListed) {
    // The device layer lists the adapters Maul RHI found and opens the one
    // whose Vulkan device the runtime presents from; a device the runtime
    // names that is not among them finds no adapter. Where there is no
    // Vulkan loader or no adapter answers, it skips unless one is required.
    Runtime runtime(true);
    auto device = render::Device::request({.allowSoftware = true, .vulkan = &runtime});
    const std::optional<bool> kOpened = device.has_value() ? openToTheEnd(**device) : std::optional<bool>{false};
    if (kOpened != true) {
        RAWFRAME_EXPECT(!required());
        std::puts("skip: no Vulkan adapter");
        return;
    }
    const std::optional<render::VulkanObjects> kObjects = (*device)->vulkan();
    RAWFRAME_EXPECT(kObjects.has_value() && kObjects->physicalDevice == static_cast<void*>(runtime.chosen));

    Runtime elsewhere(false);
    auto unlisted = render::Device::request({.allowSoftware = true, .vulkan = &elsewhere});
    RAWFRAME_EXPECT(unlisted.has_value());
    if (unlisted.has_value()) {
        RAWFRAME_EXPECT(openToTheEnd(**unlisted) == false);
    }
}
