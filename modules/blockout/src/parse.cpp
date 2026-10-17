#include "rawframe/blockout/blockout.h"
#include "rawframe/mesh/errors.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace rawframe::blockout {

namespace {

std::unexpected<result::Error> refused(std::size_t line, std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument,
                        mesh::kMeshDomain,
                        mesh::code(mesh::MeshError::BadSource),
                        "blockout line " + std::to_string(line) + ": " + std::string{why});
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

/// Reads the words after `at` into `solid` as its shape takes them.
result::Status
readRest(std::span<const std::string_view> words, std::size_t line, Solid& solid, const BlockoutLimits& limits) {
    std::size_t at = 0;
    const auto kNumbers = [&](std::size_t count, double* into) -> bool {
        for (std::size_t each = 0; each < count; ++each) {
            if (at >= words.size()) {
                return false;
            }
            const std::optional<double> kValue = numberOf(words[at++]);
            if (!kValue.has_value() || std::abs(*kValue) > limits.extent) {
                return false;
            }
            into[each] = *kValue;
        }
        return true;
    };
    if (at >= words.size() || words[at++] != "at" || !kNumbers(3, solid.at.data())) {
        return refused(line, "a solid's name is followed by at and three numbers");
    }
    while (at < words.size()) {
        const std::string_view kWord = words[at++];
        bool read = false;
        if (kWord == "size" && (solid.shape == Shape::Box || solid.shape == Shape::Ramp)) {
            read = kNumbers(3, solid.size.data());
        } else if (kWord == "radius" && solid.shape == Shape::Cylinder) {
            double radius = 0;
            read = kNumbers(1, &radius);
            solid.size[0] = solid.size[2] = radius * 2;
        } else if (kWord == "height" && (solid.shape == Shape::Cylinder || solid.shape == Shape::Extrude)) {
            read = kNumbers(1, &solid.size[1]);
        } else if (kWord == "turn" && solid.shape != Shape::Cylinder) {
            read = kNumbers(1, &solid.turn);
        } else if (kWord == "sides" && solid.shape == Shape::Cylinder) {
            double sides = 0;
            read = kNumbers(1, &sides) && sides >= 3 && sides <= limits.mostSides && sides == std::floor(sides);
            solid.sides = static_cast<std::uint32_t>(sides);
        } else if (kWord == "points" && solid.shape == Shape::Extrude) {
            read = (words.size() - at) % 2 == 0 && (words.size() - at) / 2 >= 3 &&
                   (words.size() - at) / 2 <= limits.points;
            while (read && at < words.size()) {
                std::array<double, 2> point{};
                read = kNumbers(2, point.data());
                solid.points.push_back(point);
            }
        }
        if (!read) {
            return refused(line, "'" + std::string{kWord} + "' is not read so for a solid of this shape");
        }
    }
    if (solid.shape == Shape::Extrude) {
        if (solid.points.empty()) {
            return refused(line, "an extrusion names its points last");
        }
        // Convex and counter-clockwise seen from above (+Y): with Z toward
        // the viewer, each turn's cross product is negative along +Y.
        const std::size_t kCount = solid.points.size();
        for (std::size_t each = 0; each < kCount; ++each) {
            const auto& kA = solid.points[each];
            const auto& kB = solid.points[(each + 1) % kCount];
            const auto& kC = solid.points[(each + 2) % kCount];
            const double kCross = ((kB[0] - kA[0]) * (kC[1] - kB[1])) - ((kB[1] - kA[1]) * (kC[0] - kB[0]));
            if (kCross >= -1e-9) {
                return refused(line, "an extrusion's points are convex and counter-clockwise seen from above");
            }
        }
    }
    if (!(solid.size[0] > 0 || solid.shape == Shape::Extrude) || !(solid.size[1] > 0) ||
        !(solid.size[2] > 0 || solid.shape == Shape::Extrude)) {
        return refused(line, "a solid's size is above nought each way");
    }
    return {};
}

std::string numberText(double value) {
    char made[32];
    std::snprintf(made, sizeof made, "%.9g", value);
    return made;
}

} // namespace

result::Result<Blockout> parseBlockout(std::string_view text, const BlockoutLimits& limits) {
    Blockout made;
    std::size_t line = 0;
    bool headed = false;
    while (!text.empty()) {
        const std::size_t kEnd = text.find('\n');
        const std::string_view kLine = text.substr(0, kEnd);
        text = kEnd == std::string_view::npos ? std::string_view{} : text.substr(kEnd + 1);
        ++line;
        std::vector<std::string_view> words = wordsOf(kLine);
        if (words.empty()) {
            continue;
        }
        if (!headed) {
            if (words.size() != 2 || words[0] != "blockout" || words[1] != "1") {
                return refused(line, "a blockout begins blockout 1");
            }
            headed = true;
            continue;
        }
        Solid solid;
        std::size_t at = 0;
        if (words[at] == "cut") {
            solid.cut = true;
            ++at;
        }
        if (at + 1 >= words.size()) {
            return refused(line, "a solid is a shape and a name");
        }
        const std::string_view kShape = words[at++];
        if (kShape == "box") {
            solid.shape = Shape::Box;
        } else if (kShape == "ramp") {
            solid.shape = Shape::Ramp;
        } else if (kShape == "cylinder") {
            solid.shape = Shape::Cylinder;
        } else if (kShape == "extrude") {
            solid.shape = Shape::Extrude;
        } else {
            return refused(line, "a solid is a box, a ramp, a cylinder, or an extrusion");
        }
        solid.name = std::string{words[at++]};
        if (std::ranges::contains(made.solids, solid.name, &Solid::name)) {
            return refused(line, "two solids are named " + solid.name);
        }
        RAWFRAME_TRY(readRest(std::span{words}.subspan(at), line, solid, limits));
        if (made.solids.size() == limits.solids) {
            return refused(line, "a blockout holds at most " + std::to_string(limits.solids) + " solids");
        }
        made.solids.push_back(std::move(solid));
    }
    if (!headed) {
        return refused(line, "a blockout begins blockout 1");
    }
    return made;
}

std::string writeBlockout(const Blockout& blockout) {
    std::string made = "blockout 1\n";
    for (const Solid& kSolid : blockout.solids) {
        if (kSolid.cut) {
            made += "cut ";
        }
        made += kSolid.shape == Shape::Box        ? "box "
                : kSolid.shape == Shape::Ramp     ? "ramp "
                : kSolid.shape == Shape::Cylinder ? "cylinder "
                                                  : "extrude ";
        made += kSolid.name + " at " + numberText(kSolid.at[0]) + " " + numberText(kSolid.at[1]) + " " +
                numberText(kSolid.at[2]);
        if (kSolid.shape == Shape::Box || kSolid.shape == Shape::Ramp) {
            made += " size " + numberText(kSolid.size[0]) + " " + numberText(kSolid.size[1]) + " " +
                    numberText(kSolid.size[2]);
        } else if (kSolid.shape == Shape::Cylinder) {
            made += " radius " + numberText(kSolid.size[0] / 2) + " height " + numberText(kSolid.size[1]) + " sides " +
                    std::to_string(kSolid.sides);
        } else {
            made += " height " + numberText(kSolid.size[1]);
        }
        if (kSolid.turn != 0 && kSolid.shape != Shape::Cylinder) {
            made += " turn " + numberText(kSolid.turn);
        }
        if (kSolid.shape == Shape::Extrude) {
            made += " points";
            for (const auto& kPoint : kSolid.points) {
                made += " " + numberText(kPoint[0]) + " " + numberText(kPoint[1]);
            }
        }
        made += "\n";
    }
    return made;
}

} // namespace rawframe::blockout
