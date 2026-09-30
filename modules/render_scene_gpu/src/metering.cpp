#include "metering.h"

#include "tables.h"

#include <algorithm>
#include <array>
#include <maul-rhi/encoder.h>
#include <utility>

namespace rawframe::render_scene_gpu {

namespace {

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

/// The histogram's bins zeroed, as the upload pass writes them.
constexpr std::array<std::uint32_t, kHistogramBins> kEmpty{};

} // namespace

Metering::Metering(mrhiDevice* native) noexcept : native_(native) {
}

Metering::~Metering() {
    if (native_ != nullptr && buffer_.index1 != 0) {
        // Maul RHI retires what a frame still uses once the frame is done.
        static_cast<void>(mrhiDestroyBuffer(native_, buffer_));
    }
}

result::Status Metering::make() {
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = sizeof(ExposureBlock);
    def.usage = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    if (const mrhiResult kMade = mrhiCreateBuffer(native_, &def, &buffer_); kMade != mrhi_success) {
        return failed("the exposure's buffer could not be made", kMade);
    }
    return {};
}

result::Status Metering::declare(const render_scene::SceneFrame& frame, std::vector<mrhiAccess>& writes) {
    metered_ = frame.metering.enabled;
    writes_ = !metered_ || !carried_;
    // A metered exposure starts within its bounds: a camera's not yet set
    // (nought) would draw the day past what the target holds.
    start_ = exposureOf(
        metered_ ? std::clamp(frame.exposure, frame.metering.settings.minimum, frame.metering.settings.maximum)
                 : frame.exposure);
    meter_ = meterOf(frame);
    if (const mrhiResult kImported = mrhiImportBuffer(native_, buffer_, &exposure_); kImported != mrhi_success) {
        return failed("the exposure could not join the frame", kImported);
    }
    if (writes_) {
        writes.push_back(wholeOf(exposure_, mrhi_accessCopyDestination));
    }
    if (!metered_) {
        return {};
    }
    for (const auto& [kBytes, kMade] :
         {std::pair{sizeof(kEmpty), &histogram_}, std::pair{sizeof(MeterBlock), &meterResource_}}) {
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = kBytes;
        if (mrhiDeclareBuffer(native_, &def, kMade) != mrhi_success) {
            return failed("the metering could not be declared", mrhi_errorCapacity);
        }
        writes.push_back(wholeOf(*kMade, mrhi_accessCopyDestination));
    }
    return {};
}

mrhiResourceId Metering::exposure() const noexcept {
    return exposure_;
}

result::Status Metering::addPasses(mrhiResourceId scene) {
    if (!metered_) {
        return {};
    }
    // The histogram counts the frame; the adaptation reads it and moves the
    // exposure the next frame draws with, which nothing in this frame reads
    // after it: never culled. Both bind the container's whole table, so
    // both name all it holds.
    const std::array<mrhiAccess, 4> kCounting = {wholeOf(scene, mrhi_accessSampled),
                                                 wholeOf(exposure_, mrhi_accessStorageReadWrite),
                                                 wholeOf(histogram_, mrhi_accessStorageReadWrite),
                                                 wholeOf(meterResource_, mrhi_accessUniform)};
    mrhiPassDef countingDef = mrhiDefaultPassDef();
    countingDef.accesses = kCounting.data();
    countingDef.accessCount = static_cast<std::uint32_t>(kCounting.size());
    if (const mrhiResult kAdded = mrhiAddPass(native_, &countingDef, &histogramPass_); kAdded != mrhi_success) {
        return failed("the metering's histogram could not be added", kAdded);
    }
    const std::array<mrhiAccess, 4> kAdapting = {wholeOf(scene, mrhi_accessSampled),
                                                 wholeOf(exposure_, mrhi_accessStorageReadWrite),
                                                 wholeOf(histogram_, mrhi_accessStorageReadWrite),
                                                 wholeOf(meterResource_, mrhi_accessUniform)};
    mrhiPassDef adaptingDef = mrhiDefaultPassDef();
    adaptingDef.accesses = kAdapting.data();
    adaptingDef.accessCount = static_cast<std::uint32_t>(kAdapting.size());
    adaptingDef.neverCull = true;
    if (const mrhiResult kAdded = mrhiAddPass(native_, &adaptingDef, &adaptPass_); kAdded != mrhi_success) {
        return failed("the metering's adaptation could not be added", kAdded);
    }
    return {};
}

result::Status Metering::write(mrhiPassId upload) {
    if (writes_ && mrhiWriteBuffer(native_, upload, exposure_, 0, &start_, sizeof(ExposureBlock)) != mrhi_success) {
        return failed("the exposure could not be written", mrhi_errorCapacity);
    }
    if (metered_ &&
        (mrhiWriteBuffer(native_, upload, histogram_, 0, kEmpty.data(), sizeof(kEmpty)) != mrhi_success ||
         mrhiWriteBuffer(native_, upload, meterResource_, 0, &meter_, sizeof(MeterBlock)) != mrhi_success)) {
        return failed("the metering could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status
Metering::record(const Pipelines& pipelines, mrhiResourceId scene, std::uint32_t width, std::uint32_t height) {
    if (!metered_) {
        return {};
    }
    const std::array<mrhiBinding, 4> kBindings = {textureAt(0, scene),
                                                  bufferAt(1, exposure_, sizeof(ExposureBlock)),
                                                  bufferAt(2, histogram_, sizeof(kEmpty)),
                                                  bufferAt(3, meterResource_, sizeof(MeterBlock))};
    // Every fourth texel each way: a workgroup of 16 by 16 covers 64 by 64.
    const std::uint32_t kAcross = (width + 63) / 64;
    const std::uint32_t kDown = (height + 63) / 64;
    if (mrhiBeginPass(native_, histogramPass_) != mrhi_success ||
        mrhiSetComputePipeline(native_, histogramPass_, pipelines.histogram.compute) != mrhi_success ||
        mrhiSetBindings(native_, histogramPass_, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
        mrhiDispatch(native_, histogramPass_, kAcross, kDown, 1) != mrhi_success ||
        mrhiEndPass(native_, histogramPass_) != mrhi_success) {
        return failed("the metering's histogram could not be recorded", mrhi_errorState);
    }
    if (mrhiBeginPass(native_, adaptPass_) != mrhi_success ||
        mrhiSetComputePipeline(native_, adaptPass_, pipelines.adapt.compute) != mrhi_success ||
        mrhiSetBindings(native_, adaptPass_, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
        mrhiDispatch(native_, adaptPass_, 1, 1, 1) != mrhi_success ||
        mrhiEndPass(native_, adaptPass_) != mrhi_success) {
        return failed("the metering's adaptation could not be recorded", mrhi_errorState);
    }
    return {};
}

void Metering::ended(bool submitted) noexcept {
    carried_ = submitted && metered_;
}

bool Metering::metered() const noexcept {
    return metered_;
}

} // namespace rawframe::render_scene_gpu
