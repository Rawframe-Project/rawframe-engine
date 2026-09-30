#include "capture.h"

#include "pipelines.h"
#include "rawframe/texture/texture.h"

#include <cstring>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <vector>

namespace rawframe::render_scene_gpu {

LightCapturing::LightCapturing(render::Device& device) noexcept : device_(&device) {
}

void LightCapturing::ask() noexcept {
    asked_ = true;
}

result::Status
LightCapturing::declare(mrhiResourceId scene, std::uint32_t width, std::uint32_t height, float exposure) {
    pass_.reset();
    if (!asked_) {
        return {};
    }
    if (std::uint64_t{width} * height * 8 > render::kReadbackBytes || width == 0 || height == 0) {
        asked_ = false;
        return {};
    }
    const mrhiAccess kRead{
        .resource = scene,
        .kind = mrhi_accessCopySource,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = &kRead;
    def.accessCount = 1;
    def.neverCull = true;
    mrhiPassId made{};
    if (const mrhiResult kAdded = mrhiAddPass(device_->native(), &def, &made); kAdded != mrhi_success) {
        return failed("the light's capture could not be added", kAdded);
    }
    pass_ = made;
    frameWidth_ = width;
    frameHeight_ = height;
    frameExposure_ = exposure;
    return {};
}

result::Status LightCapturing::record(mrhiResourceId scene) {
    if (!pass_.has_value()) {
        return {};
    }
    mrhiDevice* native = device_->native();
    const mrhiTextureCopy kSource{.resource = scene};
    const mrhiExtent3d kExtent{.width = frameWidth_, .height = frameHeight_, .depthOrLayers = 1};
    mrhiRequestId request{};
    if (mrhiBeginPass(native, *pass_) != mrhi_success ||
        mrhiReadTexture(native, *pass_, &kSource, &kExtent, &request) != mrhi_success ||
        mrhiEndPass(native, *pass_) != mrhi_success) {
        return failed("the light could not be read back", mrhi_errorState);
    }
    reading_ = request;
    return {};
}

void LightCapturing::ended(bool submitted) noexcept {
    if (!pass_.has_value()) {
        return;
    }
    pass_.reset();
    if (!submitted) {
        reading_.reset();
        return;
    }
    asked_ = false;
    width_ = frameWidth_;
    height_ = frameHeight_;
    exposure_ = frameExposure_;
}

std::optional<LightCapture> LightCapturing::taken() {
    if (!reading_.has_value() || asked_) {
        return std::nullopt;
    }
    device_->pump();
    const auto kAnswer = device_->answer(render::requestKey(reading_->index1, reading_->generation));
    if (!kAnswer.has_value()) {
        return std::nullopt;
    }
    const mrhiRequestId kReading = *reading_;
    reading_.reset();
    if (!kAnswer->has_value()) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(std::size_t{width_} * height_ * 8);
    std::size_t read = 0;
    if (mrhiTakeReadback(device_->native(), kReading, bytes.data(), bytes.size(), &read) != mrhi_success ||
        read != bytes.size()) {
        return std::nullopt;
    }
    LightCapture made{.width = width_, .height = height_};
    made.light.reserve(std::size_t{width_} * height_ * 3);
    for (std::size_t pixel = 0; pixel < std::size_t{width_} * height_; ++pixel) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            std::uint16_t half = 0;
            std::memcpy(&half, bytes.data() + (pixel * 8) + (channel * 2), sizeof(half));
            made.light.push_back(texture::floatOf(half) / exposure_);
        }
    }
    return made;
}

} // namespace rawframe::render_scene_gpu
