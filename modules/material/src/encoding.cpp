#include "common.h"
#include "parameters.h"
#include "rawframe/material/material.h"

#include <algorithm>
#include <bit>
#include <span>
#include <utility>
#include <vector>

namespace rawframe::material {

namespace {

/// A material's bytes after the format: its states, Surface, textures,
/// what they feed, and the normal's scale.
constexpr std::size_t kBodyBytes = 8 + (4 * 16) + (4 * 28) + 8 + 4;

void putWord(std::vector<std::byte>& bytes, std::uint32_t word) {
    for (std::size_t at = 0; at < 4; ++at) {
        bytes.push_back(static_cast<std::byte>((word >> (at * 8)) & 0xFFU));
    }
}

void putBody(std::vector<std::byte>& bytes, const Material& made) {
    const auto kPut = [&bytes](std::uint32_t word) {
        putWord(bytes, word);
    };
    bytes.push_back(static_cast<std::byte>(made.shading));
    bytes.push_back(static_cast<std::byte>(made.blend));
    bytes.push_back(static_cast<std::byte>(made.doubleSided ? 1 : 0));
    bytes.push_back(std::byte{0});
    kPut(std::bit_cast<std::uint32_t>(made.alphaCutoff));
    Surface surface = made.surface;
    for (const Parameter& parameter : kContractOrder) {
        const float* kValue = parameter.place(surface);
        for (std::size_t channel = 0; channel < parameter.channels; ++channel) {
            kPut(std::bit_cast<std::uint32_t>(kValue[channel]));
        }
    }
    for (const SampledTexture* kTexture :
         {&made.textures.base, &made.textures.packed, &made.textures.emission, &made.textures.normal}) {
        kPut(static_cast<std::uint32_t>(kTexture->id));
        kPut(static_cast<std::uint32_t>(kTexture->id >> 32U));
        bytes.push_back(static_cast<std::byte>(kTexture->filter));
        bytes.push_back(static_cast<std::byte>(kTexture->address));
        bytes.push_back(std::byte{0});
        bytes.push_back(std::byte{0});
        for (const float kValue : {kTexture->scale[0], kTexture->scale[1], kTexture->offset[0], kTexture->offset[1]}) {
            kPut(std::bit_cast<std::uint32_t>(kValue));
        }
    }
    for (const std::uint8_t kFeeds : {static_cast<std::uint8_t>(made.textures.baseColor ? 1 : 0),
                                      static_cast<std::uint8_t>(made.textures.opacity ? 1 : 0),
                                      static_cast<std::uint8_t>(made.textures.metalness),
                                      static_cast<std::uint8_t>(made.textures.roughness),
                                      static_cast<std::uint8_t>(made.textures.occlusion),
                                      std::uint8_t{0},
                                      std::uint8_t{0},
                                      std::uint8_t{0}}) {
        bytes.push_back(static_cast<std::byte>(kFeeds));
    }
    kPut(std::bit_cast<std::uint32_t>(made.textures.normalScale));
}

/// A material's bytes as `putBody` writes them, `kBodyBytes` long, as
/// `decode` refuses them.
result::Result<Material> bodyOf(std::span<const std::byte> bytes) {
    // Offsets below are the whole record's, whose body follows 8 bytes.
    const auto kWord = [&bytes](std::size_t at) {
        std::uint32_t word = 0;
        for (std::size_t each = 0; each < 4; ++each) {
            word |= std::to_integer<std::uint32_t>(bytes[at - 8 + each]) << (each * 8);
        }
        return word;
    };
    const auto kByte = [&bytes](std::size_t at) {
        return bytes[at - 8];
    };
    const auto kShading = std::to_integer<std::uint8_t>(kByte(8));
    const auto kBlend = std::to_integer<std::uint8_t>(kByte(9));
    const auto kSided = std::to_integer<std::uint8_t>(kByte(10));
    if (kShading > 1 || kBlend > 2 || kSided > 1 || kByte(11) != std::byte{0}) {
        return invalid("a cooked material's states are in their sets");
    }
    Material made{.shading = static_cast<Shading>(kShading),
                  .blend = static_cast<Blend>(kBlend),
                  .alphaCutoff = std::bit_cast<float>(kWord(12)),
                  .doubleSided = kSided == 1};
    std::size_t at = 16;
    for (const Parameter& parameter : kContractOrder) {
        float* place = parameter.place(made.surface);
        for (std::size_t channel = 0; channel < parameter.channels; ++channel, at += 4) {
            place[channel] = std::bit_cast<float>(kWord(at));
        }
    }
    for (SampledTexture* texture :
         {&made.textures.base, &made.textures.packed, &made.textures.emission, &made.textures.normal}) {
        const auto kFilter = std::to_integer<std::uint8_t>(kByte(at + 8));
        const auto kAddress = std::to_integer<std::uint8_t>(kByte(at + 9));
        if (kFilter > 1 || kAddress > 1 || kByte(at + 10) != std::byte{0} || kByte(at + 11) != std::byte{0}) {
            return invalid("a cooked material's texture's sampler state is in its sets");
        }
        *texture =
            SampledTexture{.id = kWord(at) | (std::uint64_t{kWord(at + 4)} << 32U),
                           .filter = static_cast<Filter>(kFilter),
                           .address = static_cast<Address>(kAddress),
                           .scale = {std::bit_cast<float>(kWord(at + 12)), std::bit_cast<float>(kWord(at + 16))},
                           .offset = {std::bit_cast<float>(kWord(at + 20)), std::bit_cast<float>(kWord(at + 24))}};
        at += 28;
    }
    std::array<std::uint8_t, 8> feeds{};
    for (std::size_t each = 0; each < feeds.size(); ++each) {
        feeds.at(each) = std::to_integer<std::uint8_t>(kByte(at + each));
    }
    if (feeds[0] > 1 || feeds[1] > 1 || feeds[2] > 4 || feeds[3] > 4 || feeds[4] > 4 || feeds[5] != 0 ||
        feeds[6] != 0 || feeds[7] != 0) {
        return invalid("a cooked material's textures feed what they may");
    }
    made.textures.baseColor = feeds[0] == 1;
    made.textures.opacity = feeds[1] == 1;
    made.textures.metalness = static_cast<Channel>(feeds[2]);
    made.textures.roughness = static_cast<Channel>(feeds[3]);
    made.textures.occlusion = static_cast<Channel>(feeds[4]);
    made.textures.normalScale = std::bit_cast<float>(kWord(at + 8));
    // Its ranges are the document's: written as one and checked.
    if (!(made.alphaCutoff >= 0 && made.alphaCutoff <= 1)) {
        return invalid("a cooked material's alpha cutoff is from nought to one");
    }
    Material checked = made;
    checked.alphaCutoff = made.blend == Blend::Masked ? made.alphaCutoff : 0.5F;
    // A material a document compiles to, and nothing else: its ranges,
    // its factors, and a texture's map as the document's.
    const auto kCompiled = compile(documentOf(checked, 1));
    if (!kCompiled.has_value() || *kCompiled != checked) {
        return invalid("a cooked material is one a surface document compiles to");
    }
    return made;
}

} // namespace

std::vector<std::byte> encode(const Qualities& made) {
    std::vector<std::byte> bytes;
    for (const char kLetter : std::string_view{"RFMT"}) {
        bytes.push_back(static_cast<std::byte>(kLetter));
    }
    putWord(bytes, 6);
    const Material& kHigh = made.at(static_cast<std::size_t>(Quality::High));
    putBody(bytes, kHigh);
    const bool kLow = made.at(static_cast<std::size_t>(Quality::Low)) != kHigh;
    const bool kMedium = made.at(static_cast<std::size_t>(Quality::Medium)) != kHigh;
    putWord(bytes, (kLow ? 1U : 0U) | (kMedium ? 2U : 0U));
    for (const auto& [kDiffers, kQuality] : {std::pair{kLow, Quality::Low}, std::pair{kMedium, Quality::Medium}}) {
        if (kDiffers) {
            putBody(bytes, made.at(static_cast<std::size_t>(kQuality)));
        }
    }
    return bytes;
}

std::vector<std::byte> encode(const Material& made) {
    return encode(Qualities{made, made, made});
}

result::Result<Qualities> decode(std::span<const std::byte> bytes) {
    constexpr std::size_t kHead = 8;
    const auto kWordAt = [&bytes](std::size_t at) {
        std::uint32_t word = 0;
        for (std::size_t each = 0; each < 4; ++each) {
            word |= std::to_integer<std::uint32_t>(bytes[at + each]) << (each * 8);
        }
        return word;
    };
    if (bytes.size() < kHead + kBodyBytes + 4 ||
        std::string_view{reinterpret_cast<const char*>(bytes.data()), 4} != "RFMT" || kWordAt(4) != 6) {
        return invalid("a cooked material is RFMT, format 6, and its material at the high quality");
    }
    const std::uint32_t kDiffer = kWordAt(kHead + kBodyBytes);
    const std::size_t kMore = ((kDiffer & 1U) != 0 ? 1 : 0) + ((kDiffer & 2U) != 0 ? 1 : 0);
    if (kDiffer > 3 || bytes.size() != kHead + kBodyBytes + 4 + (kMore * kBodyBytes)) {
        return invalid("a cooked material holds the qualities that differ, and nothing else");
    }
    RAWFRAME_TRY_ASSIGN(const Material kHigh, bodyOf(bytes.subspan(kHead, kBodyBytes)));
    Qualities made{kHigh, kHigh, kHigh};
    std::size_t at = kHead + kBodyBytes + 4;
    for (const auto& [kBit, kQuality] : {std::pair{1U, Quality::Low}, std::pair{2U, Quality::Medium}}) {
        if ((kDiffer & kBit) == 0) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(made.at(static_cast<std::size_t>(kQuality)), bodyOf(bytes.subspan(at, kBodyBytes)));
        if (made.at(static_cast<std::size_t>(kQuality)) == kHigh) {
            return invalid("a cooked material holds a quality apart only where it differs");
        }
        at += kBodyBytes;
    }
    return made;
}

std::array<float, kBlobFloats> blobOf(const Material& made) noexcept {
    const Surface& kSurface = made.surface;
    std::array<float, kBlobFloats> blob{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        blob[channel] = kSurface.baseColor[channel];
        blob[4 + channel] = kSurface.specularColor[channel] * kSurface.specularWeight;
        blob[8 + channel] = kSurface.emissionColor[channel] * kSurface.emissionLuminance;
    }
    blob[3] = kSurface.baseMetalness;
    blob[7] = kSurface.specularRoughness;
    blob[11] = kSurface.specularIor;
    blob[12] = kSurface.geometryOpacity;
    blob[13] = kSurface.ambientOcclusion;
    blob[14] = made.blend == Blend::Masked ? made.alphaCutoff : 0;
    const Textures& kTextures = made.textures;
    blob[15] = static_cast<float>((made.shading == Shading::Unlit ? 1U : 0U) | (kTextures.baseColor ? 2U : 0U) |
                                  (kTextures.opacity ? 4U : 0U) | (kTextures.emission.id != 0 ? 8U : 0U) |
                                  (kTextures.normal.id != 0 ? 16U : 0U));
    std::size_t at = 16;
    for (const SampledTexture* kTexture :
         {&kTextures.base, &kTextures.packed, &kTextures.emission, &kTextures.normal}) {
        blob[at++] = kTexture->scale[0];
        blob[at++] = kTexture->scale[1];
        blob[at++] = kTexture->offset[0];
        blob[at++] = kTexture->offset[1];
    }
    blob[32] = static_cast<float>(kTextures.metalness);
    blob[33] = static_cast<float>(kTextures.roughness);
    blob[34] = static_cast<float>(kTextures.occlusion);
    blob[35] = kTextures.normalScale;
    return blob;
}

} // namespace rawframe::material
