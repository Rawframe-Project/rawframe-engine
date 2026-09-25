#include "rawframe/cook/audio.h"

#include "rawframe/audio_import/import.h"
#include "rawframe/cook/errors.h"
#include "rawframe/document/record.h"

#include <array>
#include <charconv>

namespace rawframe::cook {

namespace {

using document::Record;
using document::Value;

constexpr std::array<std::string_view, 2> kSettingsFields = {"tier", "bitrate"};

std::unexpected<result::Error> refuse(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kCookDomain, code(CookError::BadSidecar), why).error()};
}

/// `tier=wave`, or `tier=opus;bitrate=<bits a second, nought for the
/// encoder's choice>`.
result::Result<std::string> normalize(const Value* settings) {
    if (settings == nullptr) {
        return std::string{"tier=wave"};
    }
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(*settings, kSettingsFields, "$.settings"));
    RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kTier, kRecord.optionalText("tier"));
    if (kTier == "wave") {
        return document::notCanonical(kRecord.pathOf("tier"), "a field at its default is omitted");
    }
    if (kTier.has_value() && *kTier != "opus") {
        return refuse("a sound's tier is wave or opus");
    }
    RAWFRAME_TRY_ASSIGN(const std::int64_t kBitrate, kRecord.integer("bitrate", 0));
    if (!kTier.has_value()) {
        if (kBitrate != 0) {
            return refuse("only an opus tier takes a bitrate");
        }
        return std::string{"tier=wave"};
    }
    if (kBitrate != 0 && (kBitrate < 6'000 || kBitrate > 512'000)) {
        return refuse("an Opus bitrate is 6,000 to 512,000 bits a second");
    }
    return "tier=opus;bitrate=" + std::to_string(kBitrate);
}

result::Result<Artifact> cookSound(std::span<const std::byte> source, std::string_view settings) {
    RAWFRAME_TRY_ASSIGN(const audio::Clip kClip, audio_import::importSound(source));
    if (settings == "tier=wave") {
        return Artifact{.type = kSoundClipType,
                        .representation = *content::RepresentationId::parse("rawframe.audio.wave"),
                        .bytes = audio_import::cook(kClip)};
    }
    constexpr std::string_view kPrefix = "tier=opus;bitrate=";
    std::uint32_t bitrate = 0;
    const std::string_view kNumber = settings.substr(kPrefix.size());
    std::from_chars(kNumber.data(), kNumber.data() + kNumber.size(), bitrate);
    RAWFRAME_TRY_ASSIGN(std::vector<std::byte> bytes, audio_import::cookOpus(kClip, {.bitrate = bitrate}));
    return Artifact{.type = kSoundClipType,
                    .representation = *content::RepresentationId::parse("rawframe.audio.opus"),
                    .bytes = std::move(bytes)};
}

} // namespace

Importer audioImporter() noexcept {
    return Importer{.identity = "rawframe.audio", .normalize = &normalize, .cook = &cookSound};
}

} // namespace rawframe::cook
