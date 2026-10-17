#include "rawframe/scatter/scatter.h"

#include "rawframe/scatter/errors.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <optional>
#include <utility>

namespace rawframe::scatter {

namespace {

std::unexpected<result::Error> refused(std::size_t line, std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument,
                        kScatterDomain,
                        code(ScatterError::BadSource),
                        "scatter line " + std::to_string(line) + ": " + std::string{why});
}

/// The line's words, split on spaces and tabs, with a comment left out.
std::vector<std::string_view> wordsOf(std::string_view line) {
    if (const std::size_t kComment = line.find('#'); kComment != std::string_view::npos) {
        line = line.substr(0, kComment);
    }
    std::vector<std::string_view> words;
    std::size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) {
            ++at;
        }
        const std::size_t kStart = at;
        while (at < line.size() && line[at] != ' ' && line[at] != '\t' && line[at] != '\r') {
            ++at;
        }
        if (at > kStart) {
            words.push_back(line.substr(kStart, at - kStart));
        }
    }
    return words;
}

template <class Number> std::optional<Number> numberOf(std::string_view word) {
    Number value{};
    const auto [kEnd, kError] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (kError != std::errc{} || kEnd != word.data() + word.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> metersOf(std::string_view word) {
    const std::optional<double> kValue = numberOf<double>(word);
    return kValue.has_value() && std::isfinite(*kValue) ? kValue : std::nullopt;
}

std::string metersText(double value) {
    char made[32];
    std::snprintf(made, sizeof made, "%.9g", value);
    return made;
}

/// SplitMix64: the draws a scatter places its copies by, from its seed
/// alone.
struct Draws {
    std::uint64_t state = 0;

    std::uint64_t next() noexcept {
        std::uint64_t mixed = (state += 0x9E3779B97F4A7C15ULL);
        mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
        return mixed ^ (mixed >> 31U);
    }
    /// From nought up to one.
    double unit() noexcept {
        return static_cast<double>(next() >> 11U) * 0x1.0p-53;
    }
};

} // namespace

result::Result<Scatter> parseScatter(std::string_view text, const ScatterLimits& limits) {
    Scatter made;
    bool headed = false;
    bool prefabbed = false;
    bool areaed = false;
    bool counted = false;
    std::size_t line = 0;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t kEnd = std::min(text.find('\n', at), text.size());
        const std::vector<std::string_view> kWords = wordsOf(text.substr(at, kEnd - at));
        at = kEnd + 1;
        ++line;
        if (kWords.empty()) {
            continue;
        }
        if (!headed) {
            if (kWords.size() != 2 || kWords[0] != "scatter" || kWords[1] != "1") {
                return refused(line, "a scatter begins `scatter 1`");
            }
            headed = true;
        } else if (kWords[0] == "prefab" && kWords.size() == 2 && !prefabbed) {
            made.prefab = std::string{kWords[1]};
            prefabbed = true;
        } else if (kWords[0] == "seed" && kWords.size() == 2) {
            const auto kSeed = numberOf<std::uint64_t>(kWords[1]);
            if (!kSeed.has_value()) {
                return refused(line, "a seed is a whole number from nought");
            }
            made.seed = *kSeed;
        } else if (kWords[0] == "area" && kWords.size() == 5 && !areaed) {
            const auto kX = metersOf(kWords[1]);
            const auto kZ = metersOf(kWords[2]);
            const auto kWidth = metersOf(kWords[3]);
            const auto kDepth = metersOf(kWords[4]);
            if (!kX.has_value() || !kZ.has_value() || !kWidth.has_value() || !kDepth.has_value() ||
                !(*kWidth > 0 && *kWidth <= limits.mostSide && *kDepth > 0 && *kDepth <= limits.mostSide)) {
                return refused(line,
                               "an area is its least X and Z, then a width and a depth above nought and at most " +
                                   metersText(limits.mostSide) + " meters");
            }
            made.x = *kX;
            made.z = *kZ;
            made.width = *kWidth;
            made.depth = *kDepth;
            areaed = true;
        } else if (kWords[0] == "spacing" && kWords.size() == 2) {
            const auto kSpacing = metersOf(kWords[1]);
            if (!kSpacing.has_value() || !(*kSpacing > 0)) {
                return refused(line, "a spacing is meters above nought");
            }
            made.spacing = *kSpacing;
        } else if (kWords[0] == "count" && kWords.size() == 2 && !counted) {
            const auto kCount = numberOf<std::uint32_t>(kWords[1]);
            if (!kCount.has_value() || *kCount == 0 || *kCount > limits.mostCount) {
                return refused(line, "a count is 1 to " + std::to_string(limits.mostCount));
            }
            made.count = *kCount;
            counted = true;
        } else if (kWords[0] == "turn" && kWords.size() == 1) {
            made.turn = true;
        } else if (kWords[0] == "on" && kWords.size() == 6 && kWords[2] == "at" && made.terrain.empty()) {
            const auto kX = metersOf(kWords[3]);
            const auto kY = metersOf(kWords[4]);
            const auto kZ = metersOf(kWords[5]);
            if (!kX.has_value() || !kY.has_value() || !kZ.has_value()) {
                return refused(line, "a terrain's origin is three meters");
            }
            made.terrain = std::string{kWords[1]};
            made.at = {*kX, *kY, *kZ};
        } else if (kWords[0] == "layer" && kWords.size() == 2 && made.layer.empty()) {
            made.layer = std::string{kWords[1]};
        } else {
            return refused(line,
                           "a scatter's lines are prefab, seed, area, spacing, count, turn, `on <terrain> at <x> "
                           "<y> <z>`, and layer, each once");
        }
    }
    if (!headed || !prefabbed || !areaed || !counted) {
        return refused(line, "a scatter names its prefab, its area, and its count");
    }
    if (!made.layer.empty() && made.terrain.empty()) {
        return refused(line, "a layer is a terrain's: a scatter with a layer is on a terrain");
    }
    return made;
}

std::string writeScatter(const Scatter& scatter) {
    std::string made = "scatter 1\nprefab " + scatter.prefab + "\nseed " + std::to_string(scatter.seed) + "\narea " +
                       metersText(scatter.x) + " " + metersText(scatter.z) + " " + metersText(scatter.width) + " " +
                       metersText(scatter.depth) + "\nspacing " + metersText(scatter.spacing) + "\ncount " +
                       std::to_string(scatter.count) + "\n";
    if (scatter.turn) {
        made += "turn\n";
    }
    if (!scatter.terrain.empty()) {
        made += "on " + scatter.terrain + " at " + metersText(scatter.at[0]) + " " + metersText(scatter.at[1]) + " " +
                metersText(scatter.at[2]) + "\n";
    }
    if (!scatter.layer.empty()) {
        made += "layer " + scatter.layer + "\n";
    }
    return made;
}

result::Result<std::vector<Placement>>
placements(const Scatter& scatter, const terrain::Terrain* ground, const ScatterLimits& limits) {
    std::optional<std::uint8_t> layer;
    if (ground != nullptr && !scatter.layer.empty()) {
        const auto kLayer = std::ranges::find(ground->layers, scatter.layer, &terrain::Layer::name);
        if (kLayer == ground->layers.end()) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kScatterDomain,
                                code(ScatterError::UnknownLayer),
                                "the scatter's terrain names no layer " + scatter.layer);
        }
        layer = static_cast<std::uint8_t>(kLayer - ground->layers.begin());
    }
    // Copies kept, by the square of the spacing's side each lies in, so a
    // draw is tried against the nine squares around it.
    std::vector<Placement> made;
    std::map<std::pair<std::int64_t, std::int64_t>, std::vector<std::size_t>> squares;
    const auto kSquareOf = [&scatter](double x, double z) {
        return std::pair{static_cast<std::int64_t>(std::floor((x - scatter.x) / scatter.spacing)),
                         static_cast<std::int64_t>(std::floor((z - scatter.z) / scatter.spacing))};
    };
    Draws draws{.state = scatter.seed};
    const std::uint64_t kTries = static_cast<std::uint64_t>(scatter.count) * limits.triesEach;
    for (std::uint64_t tried = 0; tried < kTries && made.size() < scatter.count; ++tried) {
        // Three draws a try, kept or not, so each try's place is its own.
        Placement placed{.x = scatter.x + (draws.unit() * scatter.width),
                         .y = 0,
                         .z = scatter.z + (draws.unit() * scatter.depth),
                         .yaw = draws.unit() * 2 * std::numbers::pi};
        placed.yaw = scatter.turn ? placed.yaw : 0;
        if (ground != nullptr) {
            const double kX = placed.x - scatter.at[0];
            const double kZ = placed.z - scatter.at[2];
            const std::optional<double> kHeight = terrain::heightAt(*ground, kX, kZ);
            if (!kHeight.has_value() || (layer.has_value() && terrain::layerAt(*ground, kX, kZ) != layer)) {
                continue;
            }
            placed.y = scatter.at[1] + *kHeight;
        }
        const auto [kColumn, kRow] = kSquareOf(placed.x, placed.z);
        bool near = false;
        for (std::int64_t across = kColumn - 1; across <= kColumn + 1 && !near; ++across) {
            for (std::int64_t along = kRow - 1; along <= kRow + 1 && !near; ++along) {
                const auto kSquare = squares.find({across, along});
                if (kSquare == squares.end()) {
                    continue;
                }
                near = std::ranges::any_of(kSquare->second, [&](std::size_t index) {
                    const double kDx = made[index].x - placed.x;
                    const double kDz = made[index].z - placed.z;
                    return (kDx * kDx) + (kDz * kDz) < scatter.spacing * scatter.spacing;
                });
            }
        }
        if (near) {
            continue;
        }
        squares[{kColumn, kRow}].push_back(made.size());
        made.push_back(placed);
    }
    return made;
}

std::string writePlaced(const Placed& placed) {
    std::string made = "placed 1\nprefab " + placed.prefab + "\n";
    for (const Placement& kEach : placed.placements) {
        char line[160];
        std::snprintf(line, sizeof line, "%.17g %.17g %.17g %.17g\n", kEach.x, kEach.y, kEach.z, kEach.yaw);
        made += line;
    }
    return made;
}

result::Result<Placed> parsePlaced(std::string_view text, const ScatterLimits& limits) {
    Placed made;
    std::size_t line = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t kEnd = std::min(text.find('\n', at), text.size());
        const std::vector<std::string_view> kWords = wordsOf(text.substr(at, kEnd - at));
        at = kEnd + 1;
        ++line;
        if (line == 1) {
            if (kWords.size() != 2 || kWords[0] != "placed" || kWords[1] != "1") {
                return refused(line, "a placed scatter begins `placed 1`");
            }
        } else if (line == 2) {
            if (kWords.size() != 2 || kWords[0] != "prefab") {
                return refused(line, "a placed scatter names its prefab second");
            }
            made.prefab = std::string{kWords[1]};
        } else {
            if (kWords.size() != 4 || made.placements.size() == limits.mostCount) {
                return refused(line,
                               "a placement is its x, y, z, and turn, at most " + std::to_string(limits.mostCount));
            }
            Placement& placement = made.placements.emplace_back();
            for (std::size_t each = 0; each < 4; ++each) {
                const auto kValue = metersOf(kWords[each]);
                if (!kValue.has_value()) {
                    return refused(line, "a placement's numbers are finite");
                }
                (each == 0 ? placement.x : each == 1 ? placement.y : each == 2 ? placement.z : placement.yaw) = *kValue;
            }
        }
    }
    if (line < 2) {
        return refused(line, "a placed scatter names its prefab");
    }
    return made;
}

} // namespace rawframe::scatter
