#include "rawframe/terrain/terrain.h"

#include "rawframe/mesh/errors.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace rawframe::terrain {

namespace {

std::unexpected<result::Error> refused(std::size_t line, std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument,
                        mesh::kMeshDomain,
                        mesh::code(mesh::MeshError::BadSource),
                        "terrain line " + std::to_string(line) + ": " + std::string{why});
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

std::optional<double> numberOf(std::string_view word) {
    double value = 0;
    const auto [kEnd, kError] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (kError != std::errc{} || kEnd != word.data() + word.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint32_t> countOf(std::string_view word) {
    std::uint32_t value = 0;
    const auto [kEnd, kError] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (kError != std::errc{} || kEnd != word.data() + word.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint8_t> digitOf(char each) {
    if (each >= '0' && each <= '9') {
        return static_cast<std::uint8_t>(each - '0');
    }
    if (each >= 'a' && each <= 'f') {
        return static_cast<std::uint8_t>(each - 'a' + 10);
    }
    return std::nullopt;
}

/// A height in meters as written: whole where it is whole, else to the
/// millimeter with no trailing noughts.
std::string metersOf(std::int32_t millimeters) {
    char made[32];
    std::snprintf(made, sizeof made, "%.3f", static_cast<double>(millimeters) / 1000);
    std::string text = made;
    while (text.back() == '0') {
        text.pop_back();
    }
    if (text.back() == '.') {
        text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

std::string numberText(double value) {
    char made[32];
    std::snprintf(made, sizeof made, "%.9g", value);
    return made;
}

/// How much of a brush's effect reaches `distance` from its middle: one
/// there, falling smoothly to nought at its edge.
double falloff(double distance, double radius) {
    if (distance >= radius) {
        return 0;
    }
    const double kNear = 1 - ((distance / radius) * (distance / radius));
    return kNear * kNear;
}

enum class Part : std::uint8_t {
    Header,
    Settings,
    Heights,
    AfterHeights,
    Layers
};

} // namespace

result::Result<Terrain> parseTerrain(std::string_view text, const TerrainLimits& limits) {
    Terrain made;
    Part part = Part::Header;
    std::size_t line = 0;
    std::size_t row = 0;
    bool celled = false;
    while (!text.empty()) {
        const std::size_t kEnd = text.find('\n');
        const std::string_view kLine = text.substr(0, kEnd);
        text = kEnd == std::string_view::npos ? std::string_view{} : text.substr(kEnd + 1);
        ++line;
        const std::vector<std::string_view> kWords = wordsOf(kLine);
        if (kWords.empty()) {
            continue;
        }
        switch (part) {
        case Part::Header:
            if (kWords.size() != 2 || kWords[0] != "terrain" || kWords[1] != "1") {
                return refused(line, "a terrain begins terrain 1");
            }
            part = Part::Settings;
            break;
        case Part::Settings:
            if (kWords[0] == "grid" && kWords.size() == 3 && made.columns == 0) {
                const auto kColumns = countOf(kWords[1]);
                const auto kRows = countOf(kWords[2]);
                if (!kColumns.has_value() || !kRows.has_value() || *kColumns < 2 || *kRows < 2 ||
                    *kColumns > limits.mostSamples || *kRows > limits.mostSamples) {
                    return refused(line, "a grid is 2 to " + std::to_string(limits.mostSamples) + " samples each way");
                }
                made.columns = *kColumns;
                made.rows = *kRows;
            } else if (kWords[0] == "cell" && kWords.size() == 2 && !celled) {
                const auto kCell = numberOf(kWords[1]);
                if (!kCell.has_value() || !(*kCell > 0) || *kCell > limits.mostCell) {
                    return refused(line, "a cell is above nought and at most " + numberText(limits.mostCell) + " m");
                }
                made.cell = *kCell;
                celled = true;
            } else if (kWords[0] == "layer" && kWords.size() == 3) {
                if (made.layers.size() == limits.mostLayers) {
                    return refused(line, "a terrain names at most " + std::to_string(limits.mostLayers) + " layers");
                }
                if (std::ranges::contains(made.layers, kWords[1], &Layer::name)) {
                    return refused(line, "two layers are named " + std::string{kWords[1]});
                }
                std::uint64_t material = 0;
                const auto [kDigitsEnd, kDigitsError] =
                    std::from_chars(kWords[2].data(), kWords[2].data() + kWords[2].size(), material, 16);
                if (kWords[2].size() != 16 || kDigitsError != std::errc{} ||
                    kDigitsEnd != kWords[2].data() + kWords[2].size()) {
                    return refused(line, "a layer names its material by sixteen hexadecimal digits");
                }
                made.layers.push_back(Layer{.name = std::string{kWords[1]}, .material = material});
            } else if (kWords[0] == "heights" && kWords.size() == 1 && made.columns != 0) {
                part = Part::Heights;
                made.heights.reserve(static_cast<std::size_t>(made.columns) * made.rows);
            } else {
                return refused(line, "a terrain's settings are grid, cell, and layer lines, then its heights");
            }
            break;
        case Part::Heights:
            if (kWords.size() != made.columns) {
                return refused(line, "a row of heights holds " + std::to_string(made.columns) + " numbers");
            }
            for (const std::string_view kWord : kWords) {
                const auto kMeters = numberOf(kWord);
                const double kMillimeters = kMeters.has_value() ? std::round(*kMeters * 1000) : 0;
                if (!kMeters.has_value() || std::abs(kMillimeters) > limits.mostMillimeters) {
                    return refused(line, "a height is a number of meters within the limits");
                }
                made.heights.push_back(static_cast<std::int32_t>(kMillimeters));
            }
            if (++row == made.rows) {
                part = Part::AfterHeights;
                row = 0;
            }
            break;
        case Part::AfterHeights:
            if (kWords.size() != 1 || kWords[0] != "layers" || made.layers.empty()) {
                return refused(line, "after its heights a terrain of layers holds a layers line");
            }
            made.painted.reserve(static_cast<std::size_t>(made.columns - 1) * (made.rows - 1));
            part = Part::Layers;
            break;
        case Part::Layers:
            if (row + 1 == made.rows || kWords.size() != 1 || kWords[0].size() != made.columns - 1) {
                return refused(line,
                               "a row of layers is " + std::to_string(made.columns - 1) +
                                   " hexadecimal digits, one a row of cells");
            }
            for (const char kEach : kWords[0]) {
                const auto kLayer = digitOf(kEach);
                if (!kLayer.has_value() || *kLayer >= made.layers.size()) {
                    return refused(line, "a cell's layer is one the terrain names");
                }
                made.painted.push_back(*kLayer);
            }
            ++row;
            break;
        }
    }
    if (part == Part::Header) {
        return refused(line, "a terrain begins terrain 1");
    }
    if (part == Part::Settings || part == Part::Heights) {
        return refused(line, "a terrain holds a grid and every row of its heights");
    }
    if (part == Part::Layers && row + 1 != made.rows) {
        return refused(line, "a terrain painted holds a row of layers for every row of cells");
    }
    // Unpainted, every cell is its first layer's, or none's.
    if (made.painted.empty()) {
        made.painted.assign(static_cast<std::size_t>(made.columns - 1) * (made.rows - 1), 0);
    }
    return made;
}

std::string writeTerrain(const Terrain& terrain) {
    std::string made = "terrain 1\ngrid " + std::to_string(terrain.columns) + " " + std::to_string(terrain.rows) +
                       "\ncell " + numberText(terrain.cell) + "\n";
    for (const Layer& kLayer : terrain.layers) {
        char material[17];
        std::snprintf(material, sizeof material, "%016llx", static_cast<unsigned long long>(kLayer.material));
        made += "layer " + kLayer.name + " " + material + "\n";
    }
    made += "heights\n";
    for (std::uint32_t row = 0; row < terrain.rows; ++row) {
        for (std::uint32_t column = 0; column < terrain.columns; ++column) {
            made += (column == 0 ? "" : " ") + metersOf(terrain.heights[(row * terrain.columns) + column]);
        }
        made += "\n";
    }
    if (!terrain.layers.empty()) {
        made += "layers\n";
        constexpr std::string_view kDigits = "0123456789abcdef";
        for (std::uint32_t row = 0; row + 1 < terrain.rows; ++row) {
            for (std::uint32_t column = 0; column + 1 < terrain.columns; ++column) {
                made += kDigits[terrain.painted[(row * (terrain.columns - 1)) + column] & 0xFU];
            }
            made += "\n";
        }
    }
    return made;
}

result::Result<mesh::Mesh> meshOf(const Terrain& terrain) {
    mesh::Mesh made;
    const auto kHeight = [&terrain](std::int64_t column, std::int64_t row) {
        column = std::clamp<std::int64_t>(column, 0, terrain.columns - 1);
        row = std::clamp<std::int64_t>(row, 0, terrain.rows - 1);
        return static_cast<double>(terrain.heights[(static_cast<std::size_t>(row) * terrain.columns) +
                                                   static_cast<std::size_t>(column)]) /
               1000;
    };
    for (std::uint32_t row = 0; row < terrain.rows; ++row) {
        for (std::uint32_t column = 0; column < terrain.columns; ++column) {
            made.positions.push_back({static_cast<float>(column * terrain.cell),
                                      static_cast<float>(kHeight(column, row)),
                                      static_cast<float>(row * terrain.cell)});
            // The slope each way from the samples beside it, an edge's own
            // standing for the one past it.
            const double kAcross = (kHeight(column + 1, row) - kHeight(column - 1, row)) /
                                   (terrain.cell * (column == 0 || column + 1 == terrain.columns ? 1 : 2));
            const double kAlong = (kHeight(column, row + 1) - kHeight(column, row - 1)) /
                                  (terrain.cell * (row == 0 || row + 1 == terrain.rows ? 1 : 2));
            const double kLength = std::sqrt((kAcross * kAcross) + 1 + (kAlong * kAlong));
            made.normals.push_back({static_cast<float>(-kAcross / kLength),
                                    static_cast<float>(1 / kLength),
                                    static_cast<float>(-kAlong / kLength)});
            made.uvs.push_back({static_cast<float>(column) / static_cast<float>(terrain.columns - 1),
                                static_cast<float>(row) / static_cast<float>(terrain.rows - 1)});
        }
    }
    // Each layer's cells together, facing +Y: counter-clockwise from above.
    const std::size_t kLayers = std::max<std::size_t>(terrain.layers.size(), 1);
    for (std::size_t layer = 0; layer < kLayers; ++layer) {
        const auto kFirst = static_cast<std::uint32_t>(made.indices.size());
        for (std::uint32_t row = 0; row + 1 < terrain.rows; ++row) {
            for (std::uint32_t column = 0; column + 1 < terrain.columns; ++column) {
                if (terrain.painted[(row * (terrain.columns - 1)) + column] != layer) {
                    continue;
                }
                const std::uint32_t kA = (row * terrain.columns) + column;
                const std::uint32_t kB = kA + 1;
                const std::uint32_t kC = kA + terrain.columns;
                const std::uint32_t kD = kC + 1;
                made.indices.insert(made.indices.end(), {kA, kC, kB, kB, kC, kD});
            }
        }
        const auto kCount = static_cast<std::uint32_t>(made.indices.size()) - kFirst;
        if (kCount != 0) {
            made.parts.push_back(
                mesh::Part{.firstIndex = kFirst,
                           .indexCount = kCount,
                           .material = layer < terrain.layers.size() ? terrain.layers[layer].material : 0});
        }
    }
    RAWFRAME_TRY(mesh::validate(made));
    return made;
}

std::size_t raise(Terrain& terrain, const Brush& brush, double meters, const TerrainLimits& limits) {
    std::size_t changed = 0;
    for (std::uint32_t row = 0; row < terrain.rows; ++row) {
        for (std::uint32_t column = 0; column < terrain.columns; ++column) {
            const double kReach =
                falloff(std::hypot((column * terrain.cell) - brush.x, (row * terrain.cell) - brush.z), brush.radius);
            std::int32_t& height = terrain.heights[(row * terrain.columns) + column];
            const auto kAfter = static_cast<std::int32_t>(std::clamp<double>(
                height + std::round(meters * 1000 * kReach), -limits.mostMillimeters, limits.mostMillimeters));
            changed += kAfter != height ? 1 : 0;
            height = kAfter;
        }
    }
    return changed;
}

std::size_t smooth(Terrain& terrain, const Brush& brush, double strength) {
    const std::vector<std::int32_t> kBefore = terrain.heights;
    const auto kAt = [&](std::int64_t column, std::int64_t row) {
        return static_cast<double>(
            kBefore[(static_cast<std::size_t>(row) * terrain.columns) + static_cast<std::size_t>(column)]);
    };
    std::size_t changed = 0;
    strength = std::clamp(strength, 0.0, 1.0);
    for (std::uint32_t row = 0; row < terrain.rows; ++row) {
        for (std::uint32_t column = 0; column < terrain.columns; ++column) {
            const double kReach =
                falloff(std::hypot((column * terrain.cell) - brush.x, (row * terrain.cell) - brush.z), brush.radius);
            if (kReach == 0) {
                continue;
            }
            double sum = 0;
            double count = 0;
            for (const auto& [kDx, kDz] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}}) {
                const std::int64_t kColumn = static_cast<std::int64_t>(column) + kDx;
                const std::int64_t kRow = static_cast<std::int64_t>(row) + kDz;
                if (kColumn >= 0 && kRow >= 0 && kColumn < terrain.columns && kRow < terrain.rows) {
                    sum += kAt(kColumn, kRow);
                    ++count;
                }
            }
            std::int32_t& height = terrain.heights[(row * terrain.columns) + column];
            const auto kAfter =
                static_cast<std::int32_t>(std::round(height + (((sum / count) - height) * strength * kReach)));
            changed += kAfter != height ? 1 : 0;
            height = kAfter;
        }
    }
    return changed;
}

std::size_t paint(Terrain& terrain, const Brush& brush, std::uint8_t layer) {
    if (layer >= terrain.layers.size()) {
        return 0;
    }
    std::size_t changed = 0;
    for (std::uint32_t row = 0; row + 1 < terrain.rows; ++row) {
        for (std::uint32_t column = 0; column + 1 < terrain.columns; ++column) {
            if (std::hypot(((column + 0.5) * terrain.cell) - brush.x, ((row + 0.5) * terrain.cell) - brush.z) >
                brush.radius) {
                continue;
            }
            std::uint8_t& painted = terrain.painted[(row * (terrain.columns - 1)) + column];
            changed += painted != layer ? 1 : 0;
            painted = layer;
        }
    }
    return changed;
}

namespace {

/// The cell `x` `z` lies in and where in it, each from nought to one; none
/// off the grid. The far edges belong to the last cells.
struct InCell {
    std::uint32_t column = 0;
    std::uint32_t row = 0;
    double across = 0;
    double along = 0;
};

std::optional<InCell> cellOf(const Terrain& terrain, double x, double z) noexcept {
    const double kColumns = static_cast<double>(terrain.columns - 1);
    const double kRows = static_cast<double>(terrain.rows - 1);
    const double kU = x / terrain.cell;
    const double kV = z / terrain.cell;
    if (!(kU >= 0 && kU <= kColumns && kV >= 0 && kV <= kRows) || terrain.columns < 2 || terrain.rows < 2) {
        return std::nullopt;
    }
    const auto kColumn = static_cast<std::uint32_t>(std::min(std::floor(kU), kColumns - 1));
    const auto kRow = static_cast<std::uint32_t>(std::min(std::floor(kV), kRows - 1));
    return InCell{.column = kColumn, .row = kRow, .across = kU - kColumn, .along = kV - kRow};
}

} // namespace

std::optional<double> heightAt(const Terrain& terrain, double x, double z) noexcept {
    const std::optional<InCell> kIn = cellOf(terrain, x, z);
    if (!kIn.has_value()) {
        return std::nullopt;
    }
    const auto kHeight = [&terrain](std::uint32_t column, std::uint32_t row) {
        return terrain.heights[(static_cast<std::size_t>(row) * terrain.columns) + column] / 1000.0;
    };
    // The mesh's triangles: A C B below the diagonal from B to C, and B C D
    // above it.
    const double kA = kHeight(kIn->column, kIn->row);
    const double kB = kHeight(kIn->column + 1, kIn->row);
    const double kC = kHeight(kIn->column, kIn->row + 1);
    const double kD = kHeight(kIn->column + 1, kIn->row + 1);
    if (kIn->across + kIn->along <= 1) {
        return kA + (kIn->across * (kB - kA)) + (kIn->along * (kC - kA));
    }
    return kD + ((1 - kIn->across) * (kC - kD)) + ((1 - kIn->along) * (kB - kD));
}

std::optional<std::uint8_t> layerAt(const Terrain& terrain, double x, double z) noexcept {
    const std::optional<InCell> kIn = cellOf(terrain, x, z);
    if (!kIn.has_value()) {
        return std::nullopt;
    }
    return terrain.painted[(static_cast<std::size_t>(kIn->row) * (terrain.columns - 1)) + kIn->column];
}

} // namespace rawframe::terrain
