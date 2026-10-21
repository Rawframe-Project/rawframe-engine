#include "icon.h"

#include "rawframe/base/platform.h"
#include "rawframe/composition/errors.h"
#include "rawframe/texture/texture.h"

#include <cstddef>
#include <string>

#if RAWFRAME_FILE_SYSTEM
#include <fstream>
#include <iterator>
#endif

namespace rawframe::window_host {

namespace {

/// An icon's file at most.
constexpr std::size_t kMostIconBytes = std::size_t{4} << 20U;
constexpr std::uint32_t kLeastSide = 16;
constexpr std::uint32_t kMostSide = 256;
constexpr std::size_t kMostImages = 4;

std::unexpected<result::Error> refused(std::string_view why, std::string_view path) {
    return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                       composition::kCompositionDomain,
                                                       code(composition::CompositionError::BadConfiguration),
                                                       why)
                                              .error()
                                              .withContext("window.icon", path)};
}

result::Result<std::vector<std::byte>> bytesAt(std::string_view path, const composition::HeldFiles* held) {
    if (held != nullptr) {
        const std::vector<std::byte>* kHeld = held->find(path);
        if (kHeld == nullptr || kHeld->size() > kMostIconBytes) {
            return refused("the window's icon is not held, or is past 4 MiB", path);
        }
        return *kHeld;
    }
#if RAWFRAME_FILE_SYSTEM
    std::ifstream file{std::string{path}, std::ios::binary};
    std::vector<std::byte> bytes;
    if (file) {
        file.seekg(0, std::ios::end);
        const std::streamoff kSize = file.tellg();
        file.seekg(0, std::ios::beg);
        if (kSize >= 0 && static_cast<std::size_t>(kSize) <= kMostIconBytes) {
            bytes.resize(static_cast<std::size_t>(kSize));
            file.read(reinterpret_cast<char*>(bytes.data()), kSize);
            if (file.gcount() == kSize) {
                return bytes;
            }
        }
    }
#endif
    return refused("the window's icon cannot be read, or is past 4 MiB", path);
}

} // namespace

result::Result<std::vector<IconSide>> readIcon(std::string_view path, const composition::HeldFiles* held) {
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kBytes, bytesAt(path, held));
    const auto kTexture = texture::decode(kBytes, texture::TextureLimits{.maximumSide = 4096});
    if (!kTexture.has_value() || kTexture->faces != 1 || kTexture->depth != 1 ||
        (kTexture->format != texture::Format::Rgba8 && kTexture->format != texture::Format::Rgba8Srgb)) {
        return refused("the window's icon is not a texture of 8-bit RGBA texels", path);
    }
    std::vector<const texture::Level*> fitting;
    for (const texture::Level& level : kTexture->levels) {
        if (level.width == level.height && level.width >= kLeastSide && level.width <= kMostSide) {
            fitting.push_back(&level);
        }
    }
    if (fitting.empty()) {
        return refused("the window's icon has no square level of 16 to 256 texels a side", path);
    }
    // The largest, then the smallest: what a title bar and a task bar show.
    if (fitting.size() > kMostImages) {
        fitting.erase(fitting.begin() + 1, fitting.end() - static_cast<std::ptrdiff_t>(kMostImages - 1));
    }
    std::vector<IconSide> sides;
    for (const texture::Level* kLevel : fitting) {
        IconSide& made = sides.emplace_back();
        made.side = kLevel->width;
        made.pixels.resize(kLevel->bytes.size());
        for (std::size_t at = 0; at < kLevel->bytes.size(); ++at) {
            made.pixels[at] = static_cast<std::uint8_t>(kLevel->bytes[at]);
        }
    }
    return sides;
}

} // namespace rawframe::window_host
