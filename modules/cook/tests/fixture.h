#pragma once

// What the cook's tests share: sidecars written, files read back, and a
// project of two sounds cooked into a scratch directory.

#include "rawframe/cook/audio.h"
#include "rawframe/cook/cook.h"
#include "rawframe/cook/errors.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::cook_fixture {

namespace fs = std::filesystem;

inline const std::string kToneId = "000000000000000000000000000000a1";
inline const std::string kMp3Id = "000000000000000000000000000000a2";

inline std::string readText(const fs::path& path) {
    std::ifstream file{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

inline void writeText(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

inline std::string
sidecar(std::string_view id, std::string_view settings = "", std::string_view importer = "rawframe.audio") {
    std::string text = "{\n  \"schema\": 1,\n  \"resourceId\": \"" + std::string{id} + "\",\n  \"importer\": \"" +
                       std::string{importer} + "\"";
    if (!settings.empty()) {
        text += ",\n  \"settings\": {\n    " + std::string{settings} + "\n  }";
    }
    return text + "\n}\n";
}

/// A project of two sources: a Vorbis tone cooked to Opus, and an MP3 tone
/// in the short-form tier.
struct Project {
    fs::path base = test::scratchDirectory("cook");
    fs::path sources = base / "sources";
    fs::path output = base / "output";
    fs::path cache = base / "cache";

    Project() {
        fs::remove_all(base);
        fs::create_directories(sources / "sounds");
        fs::copy_file(fs::path{RAWFRAME_COOK_DATA} / "tone.ogg", sources / "sounds" / "tone.ogg");
        fs::copy_file(fs::path{RAWFRAME_COOK_DATA} / "tone.mp3", sources / "sounds" / "tone.mp3");
        writeText(sources / "sounds" / "tone.ogg.rfmeta", sidecar(kToneId, "\"tier\": \"opus\""));
        writeText(sources / "sounds" / "tone.mp3.rfmeta", sidecar(kMp3Id));
    }
    ~Project() {
        fs::remove_all(base);
    }
    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    rawframe::cook::CookReport cook(std::uint8_t tool = 1,
                                    std::optional<fs::path> into = std::nullopt,
                                    std::function<bool(const rawframe::cook::CookStep&)> step = {}) const {
        static const std::array<rawframe::cook::Importer, 1> kImporters = {rawframe::cook::audioImporter()};
        base::Sha256Digest toolchain{};
        toolchain[0] = std::byte{tool};
        auto report = rawframe::cook::cookSources(rawframe::cook::CookRequest{.sources = sources,
                                                                              .output = into.value_or(output),
                                                                              .cache = cache,
                                                                              .importers = kImporters,
                                                                              .toolchain = toolchain,
                                                                              .target = "any",
                                                                              .step = std::move(step)});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : rawframe::cook::CookReport{};
    }
};

inline bool failedWith(const rawframe::cook::CookReport& report, rawframe::cook::CookError error) {
    return std::ranges::any_of(report.failures, [error](const result::Error& each) {
        return each.domain() == rawframe::cook::kCookDomain && each.code() == code(error);
    });
}

} // namespace rawframe::cook_fixture
