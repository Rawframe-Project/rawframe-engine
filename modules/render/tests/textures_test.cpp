// Textures held on the device: a new revision of a texture that the
// frame's budget cannot take yet leaves the one held drawn, of its shape,
// until a later frame uploads it. A machine with no adapter skips,
// unless RAWFRAME_REQUIRE_GPU is set.

#include "rawframe/render/device.h"
#include "rawframe/render/errors.h"
#include "rawframe/render/textures.h"
#include "rawframe/test/test.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace rawframe;

namespace {

std::unique_ptr<render::Device> opened() {
    auto device = render::Device::request({.allowSoftware = true});
    RAWFRAME_EXPECT(device.has_value());
    if (!device.has_value()) {
        return nullptr;
    }
    for (int poll = 0; poll < 1000; ++poll) {
        const auto kOpen = (*device)->open();
        if (!kOpen.has_value()) {
            const char* kRequired = std::getenv("RAWFRAME_REQUIRE_GPU");
            const bool kNoAdapter = kOpen.error().domain() == render::kRenderDomain &&
                                    kOpen.error().code() == render::code(render::RenderError::NoAdapter);
            RAWFRAME_EXPECT(kNoAdapter && (kRequired == nullptr || kRequired[0] == '\0'));
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

std::shared_ptr<const texture::Texture> picture(std::uint32_t side, std::byte value) {
    texture::Texture made{.format = texture::Format::Rgba8Srgb};
    made.levels.push_back(texture::Level{
        .width = side, .height = side, .bytes = std::vector<std::byte>(std::size_t{side} * side * 4, value)});
    return std::make_shared<const texture::Texture>(std::move(made));
}

} // namespace

RAWFRAME_TEST(ANewRevisionPastTheBudgetLeavesTheOneHeldDrawn) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto textures = render::DeviceTextures::create(*kDevice);
    RAWFRAME_EXPECT(textures.has_value());
    if (!textures.has_value()) {
        return;
    }
    render::DeviceTextures& held = **textures;
    constexpr std::uint64_t kPicture = 7;
    // A picture streamed a frame: the first uploaded.
    held.begin(std::uint64_t{1} << 20U);
    RAWFRAME_EXPECT(held.choose(kPicture, picture(64, std::byte{1})));
    held.ended(true);
    // The next one comes in a frame whose uploads went elsewhere: the one
    // held is drawn, not nothing, and the new one waits.
    const auto kNext = picture(64, std::byte{2});
    held.begin(1000);
    RAWFRAME_EXPECT(held.choose(kPicture, kNext));
    RAWFRAME_EXPECT(held.statistics().uploadsDeferred == 1 && held.statistics().texturesReplaced == 0);
    held.ended(true);
    // With room again it replaces it.
    held.begin(std::uint64_t{1} << 20U);
    RAWFRAME_EXPECT(held.choose(kPicture, kNext));
    RAWFRAME_EXPECT(held.statistics().texturesReplaced == 1);
    held.ended(true);
    // One of another size cannot be drawn from the one held: it waits.
    held.begin(1000);
    RAWFRAME_EXPECT(!held.choose(kPicture, picture(32, std::byte{3})));
}
