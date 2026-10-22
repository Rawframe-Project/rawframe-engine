#include "field_hints.h"

#include "description_words.h"
#include "rawframe/world_kest/layouts.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace rawframe::world_kest {

namespace {

/// The engine's hints for its own types' fields, where a game declares a
/// component of one: a color is a color wherever it is, and a body's motion
/// and shape are named as rawframe.physics2d and rawframe.physics3d name
/// them, and a particle emitter's, a trail's, and a beam's fields are in
/// the units model.kest gives them.
struct EngineHint {
    std::string_view type;
    std::string_view field;
    std::string_view unit;
    bool color = false;
    std::span<const std::pair<std::int64_t, std::string_view>> labels;
    std::optional<double> least;
    std::optional<double> most;
    std::string_view asset;
};

constexpr std::array<std::pair<std::int64_t, std::string_view>, 3> kMotions{
    {{0, "static"}, {1, "kinematic"}, {2, "dynamic"}}};
constexpr std::array<std::pair<std::int64_t, std::string_view>, 4> kShapes2d{
    {{0, "circle"}, {1, "box"}, {2, "capsule"}, {3, "tiles"}}};
constexpr std::array<std::pair<std::int64_t, std::string_view>, 5> kShapes3d{
    {{0, "sphere"}, {1, "box"}, {2, "capsule"}, {3, "cylinder"}, {4, "mesh"}}};

const std::array<EngineHint, 65> kEngineHints{{
    {.type = "rawframe.canvas.Sprite", .field = "color", .color = true},
    {.type = "rawframe.canvas.Sprite", .field = "texture", .asset = "texture"},
    {.type = "rawframe.canvas.Sprite", .field = "material", .asset = "material"},
    {.type = "rawframe.canvas.Tilemap", .field = "tilemap", .asset = "tilemap"},
    {.type = "rawframe.physics2d.Tiles2D", .field = "tilemap", .asset = "tilemap"},
    {.type = "rawframe.sequence.Player", .field = "sequence", .asset = "sequence"},
    {.type = "rawframe.model.Model", .field = "mesh", .asset = "mesh"},
    {.type = "rawframe.model.Model", .field = "material", .asset = "material"},
    {.type = "rawframe.sound.Emitter", .field = "sound", .asset = "sound"},
    {.type = "rawframe.model.Model", .field = "color", .color = true},
    {.type = "rawframe.model.Sun", .field = "color", .color = true},
    {.type = "rawframe.model.Sun", .field = "illuminance", .unit = "lux", .least = 0.0},
    {.type = "rawframe.model.Sun", .field = "angle", .unit = "rad", .least = 0.0, .most = 0.35},
    {.type = "rawframe.model.Sky", .field = "color", .color = true},
    {.type = "rawframe.model.Sky", .field = "ground", .color = true},
    {.type = "rawframe.model.Decal", .field = "color", .color = true},
    {.type = "rawframe.model.PointLight", .field = "color", .color = true},
    {.type = "rawframe.model.SpotLight", .field = "color", .color = true},
    {.type = "rawframe.physics2d.Body2D", .field = "motion", .labels = kMotions},
    {.type = "rawframe.physics2d.Body2D", .field = "shape", .labels = kShapes2d},
    {.type = "rawframe.physics2d.Body2D", .field = "width", .unit = "m", .least = 0.0},
    {.type = "rawframe.physics2d.Body2D", .field = "height", .unit = "m", .least = 0.0},
    {.type = "rawframe.physics2d.Body2D", .field = "friction", .least = 0.0, .most = 1.0},
    {.type = "rawframe.physics2d.Body2D", .field = "restitution", .least = 0.0, .most = 1.0},
    {.type = "rawframe.physics3d.Body3D", .field = "motion", .labels = kMotions},
    {.type = "rawframe.physics3d.Body3D", .field = "shape", .labels = kShapes3d},
    {.type = "rawframe.physics3d.Body3D", .field = "friction", .least = 0.0, .most = 1.0},
    // The game's mesh a body of shape mesh is made of (D987).
    {.type = "rawframe.physics3d.Mesh3D", .field = "mesh", .asset = "mesh"},
    // An emitter's, a trail's, and a beam's (D831): what each field is
    // measured in, as model.kest says it, and its colors and material.
    {.type = "rawframe.model.ParticleEmitter", .field = "material", .asset = "material"},
    {.type = "rawframe.model.ParticleEmitter", .field = "colorStart", .color = true},
    {.type = "rawframe.model.ParticleEmitter", .field = "colorEnd", .color = true},
    {.type = "rawframe.model.Trail", .field = "material", .asset = "material"},
    {.type = "rawframe.model.Trail", .field = "colorStart", .color = true},
    {.type = "rawframe.model.Trail", .field = "colorEnd", .color = true},
    {.type = "rawframe.model.Beam", .field = "material", .asset = "material"},
    {.type = "rawframe.model.Beam", .field = "colorStart", .color = true},
    {.type = "rawframe.model.Beam", .field = "colorEnd", .color = true},
    {.type = "rawframe.model.ParticleEmitter", .field = "rate", .unit = "1/s", .least = 0.0},
    {.type = "rawframe.model.ParticleEmitter", .field = "lifetime", .unit = "s", .least = 0.0},
    {.type = "rawframe.model.ParticleEmitter", .field = "speed", .unit = "m/s", .least = 0.0},
    {.type = "rawframe.model.ParticleEmitter", .field = "spread", .unit = "rad", .least = 0.0, .most = 3.14159},
    {.type = "rawframe.model.ParticleEmitter", .field = "radius", .unit = "m", .least = 0.0},
    {.type = "rawframe.model.ParticleEmitter", .field = "sizeStart", .unit = "m", .least = 0.0},
    {.type = "rawframe.model.ParticleEmitter", .field = "sizeEnd", .unit = "m", .least = 0.0},
    {.type = "rawframe.model.ParticleEmitter", .field = "accelerationX", .unit = "m/s2"},
    {.type = "rawframe.model.ParticleEmitter", .field = "accelerationY", .unit = "m/s2"},
    {.type = "rawframe.model.ParticleEmitter", .field = "accelerationZ", .unit = "m/s2"},
    {.type = "rawframe.model.ParticleEmitter", .field = "drag", .unit = "1/s", .least = 0.0},
    {.type = "rawframe.model.ParticleEmitter", .field = "variation", .least = 0.0, .most = 1.0},
    {.type = "rawframe.model.ParticleEmitter", .field = "inherit", .least = 0.0, .most = 1.0},
    {.type = "rawframe.model.ParticleEmitter", .field = "streak", .unit = "s", .least = 0.0},
    {.type = "rawframe.model.Trail", .field = "lifetime", .unit = "s", .least = 0.0},
    {.type = "rawframe.model.Trail", .field = "spacing", .unit = "m", .least = 0.0},
    {.type = "rawframe.model.Trail", .field = "widthStart", .unit = "m", .least = 0.0},
    {.type = "rawframe.model.Trail", .field = "widthEnd", .unit = "m", .least = 0.0},
    {.type = "rawframe.model.Beam", .field = "toX", .unit = "m"},
    {.type = "rawframe.model.Beam", .field = "toY", .unit = "m"},
    {.type = "rawframe.model.Beam", .field = "toZ", .unit = "m"},
    {.type = "rawframe.model.Beam", .field = "bendX", .unit = "m"},
    {.type = "rawframe.model.Beam", .field = "bendY", .unit = "m"},
    {.type = "rawframe.model.Beam", .field = "bendZ", .unit = "m"},
    {.type = "rawframe.model.Beam", .field = "widthStart", .unit = "m", .least = 0.0},
    {.type = "rawframe.model.Beam", .field = "widthEnd", .unit = "m", .least = 0.0},
    {.type = "rawframe.model.Beam", .field = "textureLength", .unit = "m", .least = 0.0},
    {.type = "rawframe.model.Beam", .field = "textureSpeed", .unit = "1/s"},
}};

std::optional<std::int64_t> integerOf(std::string_view word) {
    std::int64_t value = 0;
    const auto kRead = std::from_chars(word.data(), word.data() + word.size(), value);
    if (word.empty() || kRead.ec != std::errc{} || kRead.ptr != word.data() + word.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<std::string_view> readFieldLine(std::span<const std::string_view> words, GameFieldHint& hint) {
    constexpr std::string_view kShape =
        "a field line is `field <component> <field>` and any of `range <least> <most>`, "
        "`unit <word>`, `color`, `asset <kind>`, and `labels <number> <name>...`";
    if (words.size() < 3) {
        return kShape;
    }
    hint.component = std::string{words[0]};
    hint.field = std::string{words[1]};
    bool ranged = false;
    bool united = false;
    bool labelled = false;
    bool kinded = false;
    std::size_t at = 2;
    while (at < words.size()) {
        const std::string_view kWhat = words[at++];
        if (kWhat == "range" && !ranged && at + 2 <= words.size()) {
            constexpr double kMost = std::numeric_limits<double>::max();
            hint.least = parseReal(words[at], -kMost, kMost);
            hint.most = parseReal(words[at + 1], -kMost, kMost);
            if (!hint.least.has_value() || !hint.most.has_value() || *hint.least > *hint.most) {
                return "a field's range is two numbers, the least first";
            }
            at += 2;
            ranged = true;
        } else if (kWhat == "unit" && !united && at < words.size()) {
            hint.unit = std::string{words[at++]};
            united = true;
        } else if (kWhat == "asset" && !kinded && at < words.size()) {
            // The kinds of asset a game's lines declare (assetsOf).
            constexpr std::array<std::string_view, 9> kKinds{
                "texture", "mesh", "tilemap", "sequence", "material", "sound", "font", "prefab", "label"};
            if (std::ranges::find(kKinds, words[at]) == kKinds.end()) {
                return "a field's asset is a texture, mesh, tilemap, sequence, material, sound, font, prefab, or "
                       "label";
            }
            hint.asset = std::string{words[at++]};
            kinded = true;
        } else if (kWhat == "color" && !hint.color) {
            hint.color = true;
        } else if (kWhat == "labels" && !labelled) {
            // Pairs to the line's end or the next hint's word.
            while (at + 1 < words.size() && integerOf(words[at]).has_value()) {
                const std::int64_t kValue = *integerOf(words[at]);
                if (std::ranges::any_of(hint.labels, [&](const auto& label) {
                        return label.first == kValue || label.second == words[at + 1];
                    })) {
                    return "a field's labels name each value once, each by a name of its own";
                }
                hint.labels.emplace_back(kValue, std::string{words[at + 1]});
                at += 2;
            }
            if (hint.labels.empty()) {
                return "a field's labels are a number and its name, each";
            }
            labelled = true;
        } else {
            return kShape;
        }
    }
    return std::nullopt;
}

std::vector<GameFieldHint> fieldHintsOf(const GameDescription& game, const GameComponent& component) {
    std::vector<GameFieldHint> hints;
    for (const EngineHint& kEngine : kEngineHints) {
        if (!ofEngineType(component, kEngine.type)) {
            continue;
        }
        GameFieldHint hint{.component = component.name,
                           .field = std::string{kEngine.field},
                           .least = kEngine.least,
                           .most = kEngine.most,
                           .unit = std::string{kEngine.unit},
                           .color = kEngine.color,
                           .labels = {},
                           .asset = std::string{kEngine.asset}};
        for (const auto& [kValue, kName] : kEngine.labels) {
            hint.labels.emplace_back(kValue, std::string{kName});
        }
        hints.push_back(std::move(hint));
    }
    // A game's own line says what it says of the field in place of the
    // engine's.
    for (const GameFieldHint& kGiven : game.fieldHints) {
        if (kGiven.component != component.name) {
            continue;
        }
        std::erase_if(hints, [&](const GameFieldHint& hint) {
            return hint.field == kGiven.field;
        });
        hints.push_back(kGiven);
    }
    return hints;
}

} // namespace rawframe::world_kest
