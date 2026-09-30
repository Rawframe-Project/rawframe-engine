#include "rawframe/render/textures.h"

#include "rawframe/render/errors.h"

#include <map>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/resources.h>
#include <string>
#include <string_view>
#include <utility>

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

mrhiFormat formatOf(texture::Format format) noexcept {
    switch (format) {
    case texture::Format::Rgba8:
        return mrhi_formatRgba8Unorm;
    case texture::Format::Rgba8Srgb:
        return mrhi_formatRgba8UnormSrgb;
    case texture::Format::Bc7:
        return mrhi_formatBc7RgbaUnorm;
    case texture::Format::Bc7Srgb:
        return mrhi_formatBc7RgbaUnormSrgb;
    }
    return mrhi_formatRgba8Unorm;
}

bool blocks(texture::Format format) noexcept {
    return format == texture::Format::Bc7 || format == texture::Format::Bc7Srgb;
}

std::uint64_t bytesOf(const texture::Texture& image) noexcept {
    std::uint64_t bytes = 0;
    for (const texture::Level& level : image.levels) {
        bytes += level.bytes.size();
    }
    return bytes;
}

mrhiResourceId resourceOf(std::uint64_t key) noexcept {
    return mrhiResourceId{.index1 = static_cast<std::uint32_t>(key >> 32U),
                          .generation = static_cast<std::uint32_t>(key)};
}

/// A texture held on the device: the decoded image it was made from, so a
/// reload's new one is seen, and whether its levels are there yet.
struct Held {
    std::shared_ptr<const texture::Texture> source;
    mrhiTextureId texture{};
    bool uploaded = false;
};

} // namespace

struct DeviceTextures::State {
    Device* device = nullptr;
    mrhiDevice* native = nullptr;
    TextureLimits limits;
    TextureStatistics statistics;
    std::map<std::uint64_t, Held> held;
    /// What the frame being declared chose, and the budget it has left.
    std::map<std::uint64_t, Held*> chosen;
    std::map<std::uint64_t, mrhiResourceId> imported;
    std::vector<std::uint64_t> uploads;
    std::uint64_t budget = 0;

    ~State() {
        if (native == nullptr) {
            return;
        }
        // Maul RHI retires what a frame still uses once the frame is done.
        for (auto& [id, texture] : held) {
            static_cast<void>(mrhiDestroyTexture(native, texture.texture));
        }
    }
};

DeviceTextures::DeviceTextures(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

DeviceTextures::~DeviceTextures() = default;

result::Result<std::unique_ptr<DeviceTextures>> DeviceTextures::create(Device& device, TextureLimits limits) {
    if (device.native() == nullptr) {
        return refuse(result::ErrorClass::FailedPrecondition, RenderError::State, "textures need a ready device");
    }
    auto state = std::make_unique<State>();
    state->device = &device;
    state->native = device.native();
    state->limits = limits;
    return std::unique_ptr<DeviceTextures>{new DeviceTextures{std::move(state)}};
}

void DeviceTextures::begin(std::uint64_t budget) noexcept {
    state_->chosen.clear();
    state_->imported.clear();
    state_->uploads.clear();
    state_->budget = budget;
}

bool DeviceTextures::choose(std::uint64_t id, const std::shared_ptr<const texture::Texture>& image) {
    State& state = *state_;
    if (state.chosen.contains(id)) {
        return true;
    }
    if (image == nullptr || image->levels.empty() ||
        (blocks(image->format) && !state.device->adapter()->blockCompression)) {
        return false;
    }
    auto found = state.held.find(id);
    if (found != state.held.end() && found->second.source != image) {
        // A reload's new revision: the old one is retired once the frames
        // that draw it are done.
        static_cast<void>(mrhiDestroyTexture(state.native, found->second.texture));
        state.held.erase(found);
        ++state.statistics.texturesReplaced;
        found = state.held.end();
    }
    if (found == state.held.end()) {
        if (state.held.size() >= state.limits.maximumTextures) {
            return false;
        }
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format = formatOf(image->format);
        def.width = image->levels[0].width;
        def.height = image->levels[0].height;
        def.mipLevels = static_cast<std::uint32_t>(image->levels.size());
        def.usage = mrhi_textureSampled | mrhi_textureCopyDestination;
        mrhiTextureId made{};
        if (mrhiCreateTexture(state.native, &def, &made) != mrhi_success) {
            return false;
        }
        found = state.held.emplace(id, Held{.source = image, .texture = made}).first;
    }
    Held& texture = found->second;
    if (!texture.uploaded) {
        const std::uint64_t kBytes = bytesOf(*texture.source);
        if (kBytes > state.budget) {
            ++state.statistics.uploadsDeferred;
            return false;
        }
        state.budget -= kBytes;
        state.uploads.push_back(id);
    }
    state.chosen.emplace(id, &texture);
    return true;
}

result::Status DeviceTextures::import() {
    for (const auto& [id, texture] : state_->chosen) {
        mrhiResourceId resource{};
        if (const mrhiResult kImported = mrhiImportTexture(state_->native, texture->texture, &resource);
            kImported != mrhi_success) {
            return failed("a texture could not join the frame", kImported);
        }
        state_->imported.emplace(id, resource);
    }
    return {};
}

std::vector<std::uint64_t> DeviceTextures::chosen() const {
    std::vector<std::uint64_t> resources;
    resources.reserve(state_->imported.size());
    for (const auto& [id, resource] : state_->imported) {
        resources.push_back(requestKey(resource.index1, resource.generation));
    }
    return resources;
}

std::vector<std::uint64_t> DeviceTextures::uploading() const {
    std::vector<std::uint64_t> resources;
    resources.reserve(state_->uploads.size());
    for (const std::uint64_t kId : state_->uploads) {
        resources.push_back(resource(kId));
    }
    return resources;
}

std::uint64_t DeviceTextures::resource(std::uint64_t id) const noexcept {
    const auto kFound = state_->imported.find(id);
    return kFound == state_->imported.end() ? 0 : requestKey(kFound->second.index1, kFound->second.generation);
}

bool DeviceTextures::compressed(std::uint64_t id) const noexcept {
    const auto kFound = state_->chosen.find(id);
    return kFound != state_->chosen.end() && blocks(kFound->second->source->format);
}

result::Status DeviceTextures::write(std::uint64_t pass) {
    const mrhiPassId kPass{.index1 = static_cast<std::uint32_t>(pass >> 32U),
                           .generation = static_cast<std::uint32_t>(pass)};
    for (const std::uint64_t kId : state_->uploads) {
        const Held& texture = *state_->chosen.at(kId);
        const bool kBlocks = blocks(texture.source->format);
        for (std::uint32_t mip = 0; mip < texture.source->levels.size(); ++mip) {
            const texture::Level& level = texture.source->levels[mip];
            const std::uint32_t kRowBytes = kBlocks ? ((level.width + 3) / 4) * 16 : level.width * 4;
            const std::uint32_t kRows = kBlocks ? (level.height + 3) / 4 : level.height;
            const mrhiTextureCopy kPlace{.resource = resourceOf(resource(kId)), .mip = mip};
            const mrhiTexelLayout kLayout{.offset = 0, .bytesPerRow = kRowBytes, .rowsPerImage = kRows};
            // A compressed level's copy covers whole blocks.
            const mrhiExtent3d kExtent{.width = kBlocks ? ((level.width + 3) / 4) * 4 : level.width,
                                       .height = kBlocks ? kRows * 4 : level.height,
                                       .depthOrLayers = 1};
            if (const mrhiResult kWritten = mrhiWriteTexture(
                    state_->native, kPass, &kPlace, level.bytes.data(), level.bytes.size(), &kLayout, &kExtent);
                kWritten != mrhi_success) {
                return failed("a texture's level could not be written", kWritten);
            }
        }
    }
    return {};
}

void DeviceTextures::ended(bool submitted) noexcept {
    State& state = *state_;
    if (submitted) {
        for (const std::uint64_t kId : state.uploads) {
            Held& texture = *state.chosen.at(kId);
            texture.uploaded = true;
            ++state.statistics.texturesUploaded;
            state.statistics.uploadBytes += bytesOf(*texture.source);
        }
    }
    state.uploads.clear();
}

const TextureStatistics& DeviceTextures::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::render
