// The frames `render` owns (D285) on lavapipe: a frame nothing drew in is
// black; recorders declare in their order, the first to draw into the
// picture clearing it and the next keeping what it drew; and a recorder's
// refusal drops the frame, every recorder told, the next frame whole.
// Skips where no adapter answers, unless RAWFRAME_REQUIRE_GPU is set.

#include "rawframe/render/errors.h"
#include "rawframe/render/frame.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <memory>
#include <string>
#include <vector>

using namespace rawframe;

namespace {

bool required() {
    const char* value = std::getenv("RAWFRAME_REQUIRE_GPU");
    return value != nullptr && value[0] != '\0';
}

std::unique_ptr<render::Device> opened() {
    auto device = render::Device::request({.allowSoftware = true});
    if (!device.has_value()) {
        return nullptr;
    }
    for (int poll = 0; poll < 1000; ++poll) {
        const auto kOpen = (*device)->open();
        if (!kOpen.has_value()) {
            RAWFRAME_EXPECT(!required());
            std::puts("skip: no adapter");
            return nullptr;
        }
        if (*kOpen) {
            return std::move(*device);
        }
    }
    return nullptr;
}

constexpr std::uint32_t kSide = 8;

/// Fills the picture with `color` in a pass of its own, or refuses.
class Filling final : public render::FrameRecorder {
public:
    Filling(std::array<float, 4> color, std::vector<std::string>& log, std::string name)
        : color_(color), log_(log), name_(std::move(name)) {
    }

    bool refuse = false;
    bool cleared = false;

    result::Status declare(render::Frame& frame) override {
        log_.push_back(name_ + " declares");
        if (refuse) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::Unavailable,
                                                               render::kRenderDomain,
                                                               render::code(render::RenderError::Device),
                                                               "refused")
                                                      .error()};
        }
        cleared = frame.clearsPicture();
        mrhiPassDef def = mrhiDefaultPassDef();
        def.colorTargets[0].resource = mrhiResourceId{.index1 = static_cast<std::uint32_t>(frame.picture >> 32U),
                                                      .generation = static_cast<std::uint32_t>(frame.picture)};
        // Its own color, as a clear: the first pass clears, so a later one
        // keeping what is there must draw; a clear stands in for drawing.
        def.colorTargets[0].load = mrhi_loadClear;
        def.colorTargets[0].store = mrhi_storeKeep;
        def.colorTargets[0].clear =
            mrhiClearColor{.red = color_[0], .green = color_[1], .blue = color_[2], .alpha = color_[3]};
        def.colorTargetCount = 1;
        def.neverCull = true;
        if (mrhiAddPass(frame.device->native(), &def, &pass_) != mrhi_success) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::Unavailable,
                                                               render::kRenderDomain,
                                                               render::code(render::RenderError::Device),
                                                               "no pass")
                                                      .error()};
        }
        return {};
    }

    result::Status record(render::Frame& frame) override {
        log_.push_back(name_ + " records");
        if (mrhiBeginPass(frame.device->native(), pass_) != mrhi_success ||
            mrhiEndPass(frame.device->native(), pass_) != mrhi_success) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::Unavailable,
                                                               render::kRenderDomain,
                                                               render::code(render::RenderError::Device),
                                                               "not recorded")
                                                      .error()};
        }
        return {};
    }

    void ended(bool submitted) noexcept override {
        log_.push_back(name_ + (submitted ? " submitted" : " dropped"));
    }

private:
    std::array<float, 4> color_;
    std::vector<std::string>& log_;
    std::string name_;
    mrhiPassId pass_{};
};

std::array<int, 4> first(const std::vector<std::byte>& pixels) {
    return {std::to_integer<int>(pixels[0]),
            std::to_integer<int>(pixels[1]),
            std::to_integer<int>(pixels[2]),
            std::to_integer<int>(pixels[3])};
}

} // namespace

RAWFRAME_TEST(FramesAreMadeOfTheirRecordersInOrder) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(framer.has_value());
    if (!framer.has_value()) {
        return;
    }
    const auto kMade = [&](std::span<render::FrameRecorder* const> recorders) {
        RAWFRAME_EXPECT((*framer)->finish(5'000'000'000).has_value());
        auto made = (*framer)->make(recorders, {.width = kSide, .height = kSide, .readBack = true});
        RAWFRAME_EXPECT((*framer)->finish(5'000'000'000).has_value());
        return made;
    };
    // Nothing drew: black.
    RAWFRAME_EXPECT(kMade({}).value_or(false));
    const auto kBlack = (*framer)->pixels();
    RAWFRAME_EXPECT(kBlack.has_value() && first(*kBlack) == (std::array<int, 4>{0, 0, 0, 255}));

    // Red, then green: declared and recorded in order; the first clears.
    std::vector<std::string> log;
    Filling red{{1, 0, 0, 1}, log, "red"};
    Filling green{{0, 1, 0, 1}, log, "green"};
    const std::array<render::FrameRecorder*, 2> kBoth = {&red, &green};
    RAWFRAME_EXPECT(kMade(kBoth).value_or(false));
    RAWFRAME_EXPECT(red.cleared && !green.cleared);
    RAWFRAME_EXPECT(
        log ==
        (std::vector<std::string>{
            "red declares", "green declares", "red records", "green records", "red submitted", "green submitted"}));
    const auto kGreen = (*framer)->pixels();
    RAWFRAME_EXPECT(kGreen.has_value() && first(*kGreen) == (std::array<int, 4>{0, 255, 0, 255}));

    // The second refuses: the frame is dropped, those that declared told.
    log.clear();
    green.refuse = true;
    RAWFRAME_EXPECT(!kMade(kBoth).has_value());
    RAWFRAME_EXPECT(log ==
                    (std::vector<std::string>{"red declares", "green declares", "red dropped", "green dropped"}));
    // The next frame is whole.
    green.refuse = false;
    RAWFRAME_EXPECT(kMade(kBoth).value_or(false));
    RAWFRAME_EXPECT((*framer)->statistics().frames == 3);
}
