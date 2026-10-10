#include "rawframe/render/frame.h"

#include "rawframe/render/display.h"
#include "rawframe/render/errors.h"

#include <cmath>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/resources.h>
#include <string>
#include <string_view>

namespace rawframe::render {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, RenderError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kRenderDomain, code(error), why).error()};
}

std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kRenderDomain, code(RenderError::Device), why)
            .error()
            .withContext("outcome", std::string{mrhiResultName(outcome)})};
}

/// A picture's sides at most.
constexpr std::uint32_t kMaximumSide = 8192;

} // namespace

struct Framer::State {
    Device* device = nullptr;
    mrhiDevice* native = nullptr;
    FramerStatistics statistics;
    /// Made the first time a picture is shown.
    std::unique_ptr<Display> display;
    /// The last frame submitted, and its readback, if it read one.
    std::optional<std::uint64_t> frame;
    bool frameDone = false;
    std::optional<mrhiRequestId> readback;
    std::size_t readbackBytes = 0;
    std::optional<std::vector<std::byte>> pixels;

    /// Takes the last frame's answer, and its pixels once they are ready.
    void takeDone() {
        if (!frame.has_value() || frameDone) {
            return;
        }
        device->pump();
        if (const auto kAnswer = device->answer(*frame)) {
            frameDone = true;
        }
        if (frameDone && readback.has_value()) {
            if (const auto kAnswer = device->answer(requestKey(readback->index1, readback->generation));
                kAnswer.has_value() && kAnswer->has_value()) {
                std::vector<std::byte> bytes(readbackBytes);
                std::size_t taken = 0;
                if (mrhiTakeReadback(native, *readback, bytes.data(), bytes.size(), &taken) == mrhi_success &&
                    taken == bytes.size()) {
                    pixels = std::move(bytes);
                }
                readback.reset();
            }
        }
    }

    /// Drops the open frame: every recorder that declared learns it.
    std::unexpected<result::Error> drop(std::span<FrameRecorder* const> recorders, result::Error error) {
        static_cast<void>(mrhiDropFrame(native));
        for (FrameRecorder* recorder : recorders) {
            recorder->ended(false);
        }
        return std::unexpected<result::Error>{std::move(error)};
    }

    result::Result<bool> make(std::span<FrameRecorder* const> recorders, const FrameTarget& target) {
        if (target.width == 0 || target.height == 0 || target.width > kMaximumSide || target.height > kMaximumSide) {
            return refuse(
                result::ErrorClass::OutOfRange, RenderError::OverLimit, "a picture's sides are from 1 to 8192");
        }
        device->pump();
        if (device->lost()) {
            return refuse(result::ErrorClass::Unavailable, RenderError::State, "the device was lost");
        }
        takeDone();
        if (frame.has_value() && !frameDone) {
            ++statistics.framesBusy;
            return false;
        }
        if ((target.surface.has_value() || !target.images.empty()) && display == nullptr) {
            RAWFRAME_TRY_ASSIGN(display, Display::create(*device));
        }
        const mrhiFrameDef kFrameDef = mrhiDefaultFrameDef();
        if (const mrhiResult kBegun = mrhiBeginFrame(native, &kFrameDef); kBegun != mrhi_success) {
            return failed("a frame could not begin", kBegun);
        }
        mrhiTextureDef pictureDef = mrhiDefaultTextureDef();
        pictureDef.format = mrhi_formatRgba8UnormSrgb;
        // Shown, the picture's bytes are read through its linear twin (D280).
        pictureDef.viewFormats[0] = target.surface.has_value() ? mrhi_formatRgba8Unorm : mrhi_formatNone;
        pictureDef.width = target.width;
        pictureDef.height = target.height;
        mrhiResourceId picture{};
        if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &pictureDef, &picture); kDeclared != mrhi_success) {
            static_cast<void>(mrhiDropFrame(native));
            return failed("the picture could not be declared", kDeclared);
        }
        Frame open{.device = device,
                   .width = target.width,
                   .height = target.height,
                   .picture = requestKey(picture.index1, picture.generation)};
        for (std::size_t at = 0; at < recorders.size(); ++at) {
            if (auto declared = recorders[at]->declare(open); !declared.has_value()) {
                return drop(recorders.first(at + 1), std::move(declared).error());
            }
        }
        // A frame nothing drew in is black.
        if (!open.drawn) {
            mrhiPassDef def = mrhiDefaultPassDef();
            def.colorTargets[0].resource = picture;
            def.colorTargets[0].load = mrhi_loadClear;
            def.colorTargets[0].store = mrhi_storeKeep;
            def.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
            def.colorTargetCount = 1;
            def.neverCull = true;
            mrhiPassId cleared{};
            if (const mrhiResult kAdded = mrhiAddPass(native, &def, &cleared); kAdded != mrhi_success) {
                return drop(recorders, failed("the clearing pass could not be added", kAdded).error());
            }
            clearing = cleared;
        } else {
            clearing.reset();
        }
        std::optional<std::uint64_t> displaying;
        if (target.surface.has_value()) {
            auto added = display->add(*target.surface, open.picture);
            if (!added.has_value()) {
                return drop(recorders, std::move(added).error());
            }
            displaying = *added;
        }
        std::vector<std::uint64_t> placing;
        for (const std::uint64_t kImage : target.images) {
            const std::optional<window::PixelSize> kSize = device->adoptedSize(kImage);
            mrhiResourceId into{};
            if (!kSize.has_value() ||
                mrhiImportTexture(native,
                                  mrhiTextureId{.index1 = static_cast<std::uint32_t>(kImage >> 32U),
                                                .generation = static_cast<std::uint32_t>(kImage)},
                                  &into) != mrhi_success) {
                return drop(recorders,
                            refuse(result::ErrorClass::FailedPrecondition,
                                   RenderError::State,
                                   "an image the picture is placed into is not adopted")
                                .error());
            }
            auto placed = display->place(
                open.picture, requestKey(into.index1, into.generation), {0, 0, kSize->width, kSize->height});
            if (!placed.has_value()) {
                return drop(recorders, std::move(placed).error());
            }
            if (placed->has_value()) {
                placing.push_back(**placed);
            }
        }
        const mrhiAccess kRead{
            .resource = picture,
            .kind = mrhi_accessCopySource,
            .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
        std::optional<mrhiPassId> reading;
        if (target.readBack) {
            mrhiPassDef def = mrhiDefaultPassDef();
            def.passClass = mrhi_passTransfer;
            def.accesses = &kRead;
            def.accessCount = 1;
            def.neverCull = true;
            mrhiPassId made{};
            if (const mrhiResult kAdded = mrhiAddPass(native, &def, &made); kAdded != mrhi_success) {
                return drop(recorders, failed("the reading pass could not be added", kAdded).error());
            }
            reading = made;
        }
        if (const mrhiResult kCompiled = mrhiCompileFrame(native); kCompiled != mrhi_success) {
            return drop(recorders, failed("the frame could not be compiled", kCompiled).error());
        }
        if (clearing.has_value() &&
            (mrhiBeginPass(native, *clearing) != mrhi_success || mrhiEndPass(native, *clearing) != mrhi_success)) {
            return drop(recorders, failed("the picture could not be cleared", mrhi_errorState).error());
        }
        for (FrameRecorder* recorder : recorders) {
            if (auto recorded = recorder->record(open); !recorded.has_value()) {
                return drop(recorders, std::move(recorded).error());
            }
        }
        if (displaying.has_value()) {
            if (auto recorded = display->record(*displaying); !recorded.has_value()) {
                return drop(recorders, std::move(recorded).error());
            }
        }
        for (const std::uint64_t kPass : placing) {
            if (auto recorded = display->record(kPass); !recorded.has_value()) {
                return drop(recorders, std::move(recorded).error());
            }
        }
        readback.reset();
        pixels.reset();
        if (reading.has_value()) {
            const mrhiTextureCopy kSource{.resource = picture};
            const mrhiExtent3d kExtent{.width = target.width, .height = target.height, .depthOrLayers = 1};
            mrhiRequestId request{};
            if (mrhiBeginPass(native, *reading) != mrhi_success ||
                mrhiReadTexture(native, *reading, &kSource, &kExtent, &request) != mrhi_success ||
                mrhiEndPass(native, *reading) != mrhi_success) {
                return drop(recorders, failed("the picture could not be read back", mrhi_errorState).error());
            }
            readback = request;
            readbackBytes = std::size_t{target.width} * target.height * 4;
        }
        mrhiRequestId token{};
        if (const mrhiResult kSubmitted = mrhiSubmitFrame(native, &token); kSubmitted != mrhi_success) {
            for (FrameRecorder* recorder : recorders) {
                recorder->ended(false);
            }
            return failed("the frame could not be submitted", kSubmitted);
        }
        for (FrameRecorder* recorder : recorders) {
            recorder->ended(true);
        }
        frame = requestKey(token.index1, token.generation);
        frameDone = false;
        ++statistics.frames;
        if (target.surface.has_value()) {
            ++(displaying.has_value() ? statistics.framesShown : statistics.framesNotShown);
        }
        statistics.imagesPlaced += placing.size();
        statistics.imagesNotPlaced += target.images.size() - placing.size();
        return true;
    }

    std::optional<mrhiPassId> clearing;
};

Framer::Framer(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Framer::~Framer() = default;

result::Result<std::unique_ptr<Framer>> Framer::create(Device& device) {
    if (device.native() == nullptr) {
        return refuse(result::ErrorClass::FailedPrecondition, RenderError::State, "frames need a ready device");
    }
    auto state = std::make_unique<State>();
    state->device = &device;
    state->native = device.native();
    return std::unique_ptr<Framer>{new Framer{std::move(state)}};
}

result::Result<bool> Framer::make(std::span<FrameRecorder* const> recorders, const FrameTarget& target) {
    return state_->make(recorders, target);
}

result::Result<bool> Framer::done() {
    state_->takeDone();
    return !state_->frame.has_value() || state_->frameDone;
}

result::Status Framer::finish(std::uint64_t nanoseconds) {
    if (!state_->frame.has_value() || state_->frameDone) {
        return {};
    }
    const mrhiRequestId kToken{.index1 = static_cast<std::uint32_t>(*state_->frame >> 32U),
                               .generation = static_cast<std::uint32_t>(*state_->frame)};
    if (const mrhiResult kWaited = mrhiWaitFrame(state_->native, kToken, nanoseconds); kWaited != mrhi_success) {
        return failed("the frame did not finish", kWaited);
    }
    state_->takeDone();
    return {};
}

std::optional<std::vector<std::byte>> Framer::pixels() {
    state_->takeDone();
    return std::exchange(state_->pixels, std::nullopt);
}

const FramerStatistics& Framer::statistics() const noexcept {
    return state_->statistics;
}

float linearOf(std::uint8_t encoded) noexcept {
    const float kValue = static_cast<float>(encoded) / 255.0F;
    return kValue <= 0.04045F ? kValue / 12.92F : std::pow((kValue + 0.055F) / 1.055F, 2.4F);
}

} // namespace rawframe::render
