#include "rawframe/render/frame.h"

#include "rawframe/render/display.h"
#include "rawframe/render/errors.h"
#include "rawframe/render/timing.h"

#include <cmath>
#include <maul-rhi/device.h>
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
    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() {
        if (timestamps.has_value()) {
            static_cast<void>(mrhiDestroyQuerySet(native, *timestamps));
        }
    }

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
    /// The passes' timestamps, made the first time a frame is timed, the
    /// nanoseconds a tick of them takes, the last frame's readback of
    /// them and the passes it timed, and the last times read.
    std::optional<mrhiQuerySetId> timestamps;
    double period = 1;
    std::optional<mrhiRequestId> timesRead;
    std::vector<std::string> timesNamed;
    std::vector<PassTime> passTimes;

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
        if (frameDone && timesRead.has_value()) {
            if (const auto kAnswer = device->answer(requestKey(timesRead->index1, timesRead->generation));
                kAnswer.has_value() && kAnswer->has_value()) {
                std::vector<std::uint64_t> ticks(2 * timesNamed.size());
                std::size_t taken = 0;
                if (mrhiTakeReadback(native, *timesRead, ticks.data(), ticks.size() * 8, &taken) == mrhi_success &&
                    taken == ticks.size() * 8) {
                    passTimes.clear();
                    for (std::size_t at = 0; at < timesNamed.size(); ++at) {
                        const std::uint64_t kTicks =
                            ticks[2 * at + 1] >= ticks[2 * at] ? ticks[2 * at + 1] - ticks[2 * at] : 0;
                        passTimes.push_back(
                            PassTime{.pass = timesNamed[at],
                                     .nanoseconds = static_cast<std::uint64_t>(static_cast<double>(kTicks) * period)});
                    }
                }
                timesRead.reset();
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
        if ((target.surface.has_value() || !target.images.empty() || !target.asides.empty()) && display == nullptr) {
            RAWFRAME_TRY_ASSIGN(display, Display::create(*device));
        }
        const mrhiFrameDef kFrameDef = mrhiDefaultFrameDef();
        if (const mrhiResult kBegun = mrhiBeginFrame(native, &kFrameDef); kBegun != mrhi_success) {
            return failed("a frame could not begin", kBegun);
        }
        mrhiTextureDef pictureDef = mrhiDefaultTextureDef();
        pictureDef.format = mrhi_formatRgba8UnormSrgb;
        // Shown, the picture's bytes are read through its linear twin.
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
        open.covers.assign(target.images.size(), std::nullopt);
        // Timed where asked and the device writes timestamps.
        if (target.timed && device->adapter().has_value() && device->adapter()->timestamps) {
            if (!timestamps.has_value()) {
                mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
                def.type = mrhi_queryTimestamp;
                def.count = static_cast<std::uint32_t>(2 * kMostTimedPasses);
                mrhiQuerySetId made{};
                if (mrhiCreateQuerySet(native, &def, &made) == mrhi_success &&
                    mrhiGetDeviceTimestampPeriod(native, &period) == mrhi_success) {
                    timestamps = made;
                }
            }
            if (timestamps.has_value()) {
                open.timestamps = requestKey(timestamps->index1, timestamps->generation);
            }
        }
        // The panel: a texture of its image's size, covering it
        // whole.
        std::optional<mrhiResourceId> panel;
        if (target.panel.has_value()) {
            const std::optional<window::PixelSize> kSize =
                *target.panel < target.images.size() ? device->adoptedSize(target.images[*target.panel]) : std::nullopt;
            if (!kSize.has_value()) {
                static_cast<void>(mrhiDropFrame(native));
                return refuse(result::ErrorClass::FailedPrecondition,
                              RenderError::State,
                              "the panel is not an adopted image of the target");
            }
            mrhiTextureDef panelDef = pictureDef;
            panelDef.viewFormats[0] = mrhi_formatNone;
            panelDef.width = kSize->width;
            panelDef.height = kSize->height;
            mrhiResourceId declared{};
            if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &panelDef, &declared);
                kDeclared != mrhi_success) {
                static_cast<void>(mrhiDropFrame(native));
                return failed("the panel could not be declared", kDeclared);
            }
            panel = declared;
            open.panel = requestKey(declared.index1, declared.generation);
            open.panelWidth = kSize->width;
            open.panelHeight = kSize->height;
            open.covers[*target.panel] =
                Frame::Cover{.picture = open.panel,
                             .viewport = {0, 0, static_cast<float>(kSize->width), static_cast<float>(kSize->height)}};
        }
        // Each aside: a texture of its size, shown on its surface.
        std::vector<mrhiResourceId> asides;
        for (const FrameTarget::Aside& kAside : target.asides) {
            if (kAside.width == 0 || kAside.height == 0 || kAside.width > kMaximumSide ||
                kAside.height > kMaximumSide) {
                static_cast<void>(mrhiDropFrame(native));
                return refuse(
                    result::ErrorClass::OutOfRange, RenderError::OverLimit, "an aside's sides are from 1 to 8192");
            }
            mrhiTextureDef asideDef = pictureDef;
            asideDef.viewFormats[0] = mrhi_formatRgba8Unorm;
            asideDef.width = kAside.width;
            asideDef.height = kAside.height;
            mrhiResourceId declared{};
            if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &asideDef, &declared);
                kDeclared != mrhi_success) {
                static_cast<void>(mrhiDropFrame(native));
                return failed("an aside could not be declared", kDeclared);
            }
            asides.push_back(declared);
            open.asides.push_back(Frame::Aside{.window = kAside.window,
                                               .picture = requestKey(declared.index1, declared.generation),
                                               .width = kAside.width,
                                               .height = kAside.height});
        }
        for (std::size_t at = 0; at < recorders.size(); ++at) {
            if (auto declared = recorders[at]->declare(open); !declared.has_value()) {
                return drop(recorders.first(at + 1), std::move(declared).error());
            }
        }
        // An aside nothing drew in is black, as the picture is.
        asidesClearing.clear();
        for (std::size_t at = 0; at < asides.size(); ++at) {
            if (open.asides[at].drawn) {
                continue;
            }
            mrhiPassDef def = mrhiDefaultPassDef();
            def.colorTargets[0].resource = asides[at];
            def.colorTargets[0].load = mrhi_loadClear;
            def.colorTargets[0].store = mrhi_storeKeep;
            def.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
            def.colorTargetCount = 1;
            def.neverCull = true;
            mrhiPassId cleared{};
            if (const mrhiResult kAdded = mrhiAddPass(native, &def, &cleared); kAdded != mrhi_success) {
                return drop(recorders, failed("an aside's clearing pass could not be added", kAdded).error());
            }
            asidesClearing.push_back(cleared);
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
        // A panel nothing drew on is clear.
        panelClearing.reset();
        if (panel.has_value() && !open.panelDrawn) {
            mrhiPassDef def = mrhiDefaultPassDef();
            def.colorTargets[0].resource = *panel;
            def.colorTargets[0].load = mrhi_loadClear;
            def.colorTargets[0].store = mrhi_storeKeep;
            def.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 0};
            def.colorTargetCount = 1;
            def.neverCull = true;
            mrhiPassId cleared{};
            if (const mrhiResult kAdded = mrhiAddPass(native, &def, &cleared); kAdded != mrhi_success) {
                return drop(recorders, failed("the panel's clearing pass could not be added", kAdded).error());
            }
            panelClearing = cleared;
        }
        std::optional<std::uint64_t> displaying;
        if (target.surface.has_value()) {
            auto added = display->add(*target.surface, open.picture);
            if (!added.has_value()) {
                return drop(recorders, std::move(added).error());
            }
            displaying = *added;
        }
        // Each aside shown on its window's surface.
        std::vector<std::uint64_t> showingAsides;
        for (std::size_t at = 0; at < target.asides.size(); ++at) {
            auto added = display->add(target.asides[at].surface, open.asides[at].picture);
            if (!added.has_value()) {
                return drop(recorders, std::move(added).error());
            }
            if (added->has_value()) {
                showingAsides.push_back(**added);
            }
        }
        std::vector<std::uint64_t> placing;
        for (std::size_t at = 0; at < target.images.size(); ++at) {
            const std::uint64_t kImage = target.images[at];
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
            // Covered by what a recorder drew for it, else the picture
            // placed whole.
            const std::uint64_t kInto = requestKey(into.index1, into.generation);
            const std::optional<Frame::Cover>& kCover = open.covers[at];
            auto placed = kCover.has_value()
                              ? display->cover(kCover->picture, kInto, kCover->viewport, {kSize->width, kSize->height})
                              : display->place(open.picture, kInto, {0, 0, kSize->width, kSize->height});
            if (!placed.has_value()) {
                return drop(recorders, std::move(placed).error());
            }
            if (placed->has_value()) {
                placing.push_back(**placed);
            }
        }
        // The timed passes' times resolved into a buffer, then read back.
        std::optional<mrhiResourceId> times;
        std::optional<mrhiPassId> resolving;
        std::optional<mrhiPassId> timesReading;
        if (!open.timed.empty()) {
            mrhiBufferDef def = mrhiDefaultBufferDef();
            def.size = ((open.timed.size() * 16 + 255) / 256) * 256;
            mrhiResourceId declared{};
            if (const mrhiResult kDeclared = mrhiDeclareBuffer(native, &def, &declared); kDeclared != mrhi_success) {
                return drop(recorders, failed("the passes' times could not be declared", kDeclared).error());
            }
            times = declared;
            for (const auto& [kAccess, kInto] :
                 {std::pair{mrhi_accessQueryResolve, &resolving}, std::pair{mrhi_accessCopySource, &timesReading}}) {
                const mrhiAccess kUse{.resource = declared, .kind = static_cast<mrhiAccessKind>(kAccess), .range = {}};
                mrhiPassDef pass = mrhiDefaultPassDef();
                pass.accesses = &kUse;
                pass.accessCount = 1;
                pass.neverCull = true;
                mrhiPassId made{};
                if (const mrhiResult kAdded = mrhiAddPass(native, &pass, &made); kAdded != mrhi_success) {
                    return drop(recorders, failed("the passes' times could not be read", kAdded).error());
                }
                *kInto = made;
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
        if (mrhiResult outcome = mrhi_success;
            clearing.has_value() && ((outcome = mrhiBeginPass(native, *clearing)) != mrhi_success ||
                                     (outcome = mrhiEndPass(native, *clearing)) != mrhi_success)) {
            return drop(recorders, failed("the picture could not be cleared", outcome).error());
        }
        if (mrhiResult outcome = mrhi_success;
            panelClearing.has_value() && ((outcome = mrhiBeginPass(native, *panelClearing)) != mrhi_success ||
                                          (outcome = mrhiEndPass(native, *panelClearing)) != mrhi_success)) {
            return drop(recorders, failed("the panel could not be cleared", outcome).error());
        }
        for (const mrhiPassId kCleared : asidesClearing) {
            if (mrhiResult outcome = mrhi_success; (outcome = mrhiBeginPass(native, kCleared)) != mrhi_success ||
                                                   (outcome = mrhiEndPass(native, kCleared)) != mrhi_success) {
                return drop(recorders, failed("an aside could not be cleared", outcome).error());
            }
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
        for (const std::uint64_t kPass : showingAsides) {
            if (auto recorded = display->record(kPass); !recorded.has_value()) {
                return drop(recorders, std::move(recorded).error());
            }
        }
        timesRead.reset();
        if (times.has_value()) {
            const auto kCount = static_cast<std::uint32_t>(2 * open.timed.size());
            mrhiRequestId request{};
            if (mrhiResult outcome = mrhi_success;
                (outcome = mrhiBeginPass(native, *resolving)) != mrhi_success ||
                (outcome = mrhiResolveQueries(native, *resolving, *timestamps, 0, kCount, *times, 0)) != mrhi_success ||
                (outcome = mrhiEndPass(native, *resolving)) != mrhi_success ||
                (outcome = mrhiBeginPass(native, *timesReading)) != mrhi_success ||
                (outcome = mrhiReadBuffer(native, *timesReading, *times, 0, std::uint64_t{kCount} * 8, &request)) !=
                    mrhi_success ||
                (outcome = mrhiEndPass(native, *timesReading)) != mrhi_success) {
                return drop(recorders, failed("the passes' times could not be read back", outcome).error());
            }
            timesRead = request;
            timesNamed = open.timed;
        }
        readback.reset();
        pixels.reset();
        if (reading.has_value()) {
            const mrhiTextureCopy kSource{.resource = picture};
            const mrhiExtent3d kExtent{.width = target.width, .height = target.height, .depthOrLayers = 1};
            mrhiRequestId request{};
            if (mrhiResult outcome = mrhi_success;
                (outcome = mrhiBeginPass(native, *reading)) != mrhi_success ||
                (outcome = mrhiReadTexture(native, *reading, &kSource, &kExtent, &request)) != mrhi_success ||
                (outcome = mrhiEndPass(native, *reading)) != mrhi_success) {
                return drop(recorders, failed("the picture could not be read back", outcome).error());
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
    std::optional<mrhiPassId> panelClearing;
    std::vector<mrhiPassId> asidesClearing;
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

std::span<const PassTime> Framer::passTimes() const noexcept {
    return state_->passTimes;
}

std::optional<std::uint32_t> Frame::time(std::string_view pass) {
    if (timestamps == 0 || timed.size() >= kMostTimedPasses) {
        return std::nullopt;
    }
    timed.emplace_back(pass);
    return static_cast<std::uint32_t>(2 * (timed.size() - 1));
}

void timePass(Frame& frame, mrhiPassDef& def, std::string_view pass) {
    if (const std::optional<std::uint32_t> kAt = frame.time(pass); kAt.has_value()) {
        def.timestampQuerySet = mrhiQuerySetId{.index1 = static_cast<std::uint32_t>(frame.timestamps >> 32U),
                                               .generation = static_cast<std::uint32_t>(frame.timestamps)};
        def.timestampBegin = *kAt;
        def.timestampEnd = *kAt + 1;
    }
}

const FramerStatistics& Framer::statistics() const noexcept {
    return state_->statistics;
}

float linearOf(std::uint8_t encoded) noexcept {
    const float kValue = static_cast<float>(encoded) / 255.0F;
    return kValue <= 0.04045F ? kValue / 12.92F : std::pow((kValue + 0.055F) / 1.055F, 2.4F);
}

} // namespace rawframe::render
