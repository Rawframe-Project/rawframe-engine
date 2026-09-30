#include "materials.h"

#include "rawframe/mesh/errors.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace rawframe::mesh_import {

namespace {

using mesh::MeshError;

/// The luminance of glTF's emissive one (D314): glTF gives emission no
/// unit, and Rawframe's is nits.
constexpr float kEmissionNits = 1000.0F;

std::unexpected<result::Error> unsupported(std::string_view why) {
    return result::fail(
        result::ErrorClass::Unsupported, mesh::kMeshDomain, mesh::code(MeshError::UnsupportedMaterial), why);
}

std::unexpected<result::Error> badSource(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, mesh::kMeshDomain, mesh::code(MeshError::BadSource), why);
}

/// A name a subasset key can end with: some characters, none a control.
bool printable(std::string_view name) noexcept {
    return !name.empty() && std::ranges::none_of(name, [](char letter) {
        return static_cast<unsigned char>(letter) < 0x20U || letter == 0x7F;
    });
}

bool unit(float value) noexcept {
    return value >= 0 && value <= 1;
}

/// RFC 4648's base64, padded; none for anything else.
std::optional<std::vector<std::byte>> base64(std::string_view text) {
    const auto kValue = [](char letter) -> int {
        if (letter >= 'A' && letter <= 'Z') {
            return letter - 'A';
        }
        if (letter >= 'a' && letter <= 'z') {
            return letter - 'a' + 26;
        }
        if (letter >= '0' && letter <= '9') {
            return letter - '0' + 52;
        }
        return letter == '+' ? 62 : (letter == '/' ? 63 : -1);
    };
    if (text.size() % 4 != 0) {
        return std::nullopt;
    }
    std::vector<std::byte> out;
    out.reserve((text.size() / 4) * 3);
    for (std::size_t at = 0; at < text.size(); at += 4) {
        const bool kLast = at + 4 == text.size();
        const std::size_t kPadding = kLast ? static_cast<std::size_t>(std::ranges::count(text.substr(at + 2), '=')) : 0;
        std::uint32_t group = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const int kDigit = i >= 4 - kPadding ? 0 : kValue(text[at + i]);
            if (kDigit < 0) {
                return std::nullopt;
            }
            group = (group << 6U) | static_cast<std::uint32_t>(kDigit);
        }
        for (std::size_t i = 0; i < 3 - kPadding; ++i) {
            out.push_back(static_cast<std::byte>(group >> (16U - (8U * i))));
        }
    }
    return out;
}

/// An image's bytes as the glTF holds them: in a buffer view, a data URI,
/// or a file beside it.
result::Result<std::vector<std::byte>> bytesOf(const cgltf_image& image, const ReadFile& read) {
    if (image.buffer_view != nullptr) {
        const std::uint8_t* data = cgltf_buffer_view_data(image.buffer_view);
        if (data == nullptr) {
            return badSource("an image whose buffer view holds nothing");
        }
        const auto* kFirst = reinterpret_cast<const std::byte*>(data);
        return std::vector<std::byte>{kFirst, kFirst + image.buffer_view->size};
    }
    if (image.uri == nullptr) {
        return badSource("an image with neither a URI nor a buffer view");
    }
    const std::string_view kUri = image.uri;
    if (kUri.starts_with("data:")) {
        const std::size_t kComma = kUri.find(',');
        auto decoded = kComma != std::string_view::npos && kUri.substr(0, kComma).ends_with(";base64")
                           ? base64(kUri.substr(kComma + 1))
                           : std::nullopt;
        if (!decoded.has_value()) {
            return badSource("an image's data URI that is not base64");
        }
        return std::move(*decoded);
    }
    std::string path{kUri};
    path.resize(cgltf_decode_uri(path.data()));
    const auto kBytes = read(path);
    if (!kBytes.has_value()) {
        return badSource("the glTF's image " + path + " cannot be read");
    }
    return std::vector<std::byte>{kBytes->begin(), kBytes->end()};
}

} // namespace

result::Result<std::uint64_t> MaterialMaker::imageOf(const cgltf_image& image, bool color) {
    if (const auto kFound = images_.find(&image); kFound != images_.end()) {
        if (kFound->second.second != color) {
            return unsupported("an image sampled as colors by one texture and as data by another");
        }
        return kFound->second.first;
    }
    const bool kNamed = image.name != nullptr && image.name[0] != '\0';
    const bool kFile = image.uri != nullptr && !std::string_view{image.uri}.starts_with("data:");
    if (!kNamed && !kFile) {
        return unsupported("an image with no name or URI: its subasset has no stable key");
    }
    const std::string kName = kNamed ? image.name : image.uri;
    if (!printable(kName)) {
        return unsupported("an image whose name holds a control character");
    }
    std::string key = "texture/" + kName;
    if (!keys_.insert(key).second) {
        return unsupported("two images named " + kName);
    }
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kId, (*identify_)(key));
    RAWFRAME_TRY_ASSIGN(std::vector<std::byte> bytes, bytesOf(image, *read_));
    textures_.push_back(ImportedTexture{.key = std::move(key), .image = std::move(bytes), .color = color});
    images_.emplace(&image, std::pair{kId, color});
    return kId;
}

result::Result<material::SampledTexture> MaterialMaker::sampled(const cgltf_texture_view& view, bool color) {
    const cgltf_texture& kTexture = *view.texture;
    if (kTexture.image == nullptr) {
        return unsupported("a texture with no image this importer reads");
    }
    const cgltf_int kCoordinates =
        view.has_transform && view.transform.has_texcoord ? view.transform.texcoord : view.texcoord;
    if (kCoordinates != 0) {
        return unsupported("a texture sampled at coordinates other than the first");
    }
    material::SampledTexture made;
    if (view.has_transform) {
        if (view.transform.rotation != 0) {
            return unsupported("a texture turned by KHR_texture_transform");
        }
        made.scale = {view.transform.scale[0], view.transform.scale[1]};
        made.offset = {view.transform.offset[0], view.transform.offset[1]};
    }
    if (const cgltf_sampler* sampler = kTexture.sampler; sampler != nullptr) {
        if (sampler->wrap_s != sampler->wrap_t || sampler->wrap_s == cgltf_wrap_mode_mirrored_repeat) {
            return unsupported("a texture mirrored, or repeated one way and clamped the other");
        }
        made.address =
            sampler->wrap_s == cgltf_wrap_mode_clamp_to_edge ? material::Address::Clamp : material::Address::Repeat;
        made.filter =
            sampler->mag_filter == cgltf_filter_type_nearest ? material::Filter::Nearest : material::Filter::Linear;
    }
    RAWFRAME_TRY_ASSIGN(made.id, imageOf(*kTexture.image, color));
    return made;
}

result::Result<std::uint64_t> MaterialMaker::identityOf(const cgltf_material* material) {
    if (material == nullptr) {
        return 0;
    }
    if (const auto kFound = made_.find(material); kFound != made_.end()) {
        return kFound->second;
    }
    if (material->name == nullptr || !printable(material->name)) {
        return unsupported("a material with no name, or one holding a control character: its subasset has no "
                           "stable key");
    }
    const std::string kName = material->name;
    std::string key = "material/" + kName;
    if (!keys_.insert(key).second) {
        return unsupported("two materials named " + kName);
    }
    if (material->has_pbr_specular_glossiness && !material->has_pbr_metallic_roughness) {
        return unsupported("the material " + kName + " is specular-glossiness only");
    }
    const cgltf_pbr_metallic_roughness& kPbr = material->pbr_metallic_roughness;
    const std::array<float, 4> kBase{
        kPbr.base_color_factor[0], kPbr.base_color_factor[1], kPbr.base_color_factor[2], kPbr.base_color_factor[3]};
    const std::array<float, 3> kEmissive{
        material->emissive_factor[0], material->emissive_factor[1], material->emissive_factor[2]};
    if (!std::ranges::all_of(kBase, unit) || !std::ranges::all_of(kEmissive, unit) || !unit(kPbr.metallic_factor) ||
        !unit(kPbr.roughness_factor) || !unit(material->alpha_cutoff)) {
        return unsupported("the material " + kName + " has a factor out of glTF's range");
    }

    material::Material made;
    made.shading = material->unlit ? material::Shading::Unlit : material::Shading::Lit;
    made.doubleSided = material->double_sided != 0;
    made.surface.baseColor = {kBase[0], kBase[1], kBase[2]};
    switch (material->alpha_mode) {
    case cgltf_alpha_mode_mask:
        made.blend = material::Blend::Masked;
        made.alphaCutoff = material->alpha_cutoff;
        break;
    case cgltf_alpha_mode_blend:
        made.blend = material::Blend::Translucent;
        break;
    default:
        break;
    }
    // An opaque material's alpha is ignored, as glTF has it.
    const bool kSeeThrough = made.blend != material::Blend::Opaque;
    made.surface.geometryOpacity = kSeeThrough ? kBase[3] : 1;
    if (kPbr.base_color_texture.texture != nullptr) {
        RAWFRAME_TRY_ASSIGN(made.textures.base, sampled(kPbr.base_color_texture, true));
        made.textures.baseColor = true;
        made.textures.opacity = kSeeThrough;
    }

    made.surface.baseMetalness = kPbr.metallic_factor;
    made.surface.specularRoughness = kPbr.roughness_factor;
    if (kPbr.metallic_roughness_texture.texture != nullptr) {
        RAWFRAME_TRY_ASSIGN(made.textures.packed, sampled(kPbr.metallic_roughness_texture, false));
        made.textures.metalness = material::Channel::Blue;
        made.textures.roughness = material::Channel::Green;
    }
    if (material->occlusion_texture.texture != nullptr) {
        if (material->occlusion_texture.scale != 1) {
            return unsupported("the material " + kName + " has an occlusion strength other than one");
        }
        RAWFRAME_TRY_ASSIGN(const material::SampledTexture kOcclusion, sampled(material->occlusion_texture, false));
        if (made.textures.packed.id != 0 && made.textures.packed != kOcclusion) {
            return unsupported("the material " + kName +
                               " has an occlusion texture apart from its metallic-roughness texture; generation 1 "
                               "samples one packed texture");
        }
        made.textures.packed = kOcclusion;
        made.textures.occlusion = material::Channel::Red;
    }

    const float kStrength = material->has_emissive_strength ? material->emissive_strength.emissive_strength : 1;
    const float kPeak = std::ranges::max(kEmissive);
    if (kPeak > 0 && kStrength > 0) {
        made.surface.emissionColor = {kEmissive[0] / kPeak, kEmissive[1] / kPeak, kEmissive[2] / kPeak};
        made.surface.emissionLuminance = kPeak * kStrength * kEmissionNits;
        if (material->emissive_texture.texture != nullptr) {
            RAWFRAME_TRY_ASSIGN(made.textures.emission, sampled(material->emissive_texture, true));
        }
    }
    if (material->normal_texture.texture != nullptr) {
        RAWFRAME_TRY_ASSIGN(made.textures.normal, sampled(material->normal_texture, false));
        made.textures.normalScale = material->normal_texture.scale;
    }

    // What the cook writes must read back as itself: a material generation 1
    // compiles, or none.
    const auto kRead = material::decode(material::encode(made));
    if (!kRead.has_value() || *kRead != made) {
        return unsupported("the material " + kName + " is not one generation 1 compiles" +
                           (kRead.has_value() ? std::string{} : ": " + std::string{kRead.error().description()}));
    }
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kId, (*identify_)(key));
    materials_.push_back(ImportedMaterial{.key = std::move(key), .made = made});
    made_.emplace(material, kId);
    return kId;
}

std::vector<ImportedMaterial> MaterialMaker::takeMaterials() {
    std::ranges::sort(materials_, {}, &ImportedMaterial::key);
    return std::move(materials_);
}

std::vector<ImportedTexture> MaterialMaker::takeTextures() {
    std::ranges::sort(textures_, {}, &ImportedTexture::key);
    return std::move(textures_);
}

} // namespace rawframe::mesh_import
