#include "rawframe/world_kest/cooked_mod.h"

#include "rawframe/document/json.h"
#include "rawframe/world_kest/errors.h"

#include <algorithm>
#include <array>
#include <optional>

namespace rawframe::world_kest {

namespace {

using document::Value;

result::Error invalid(std::string_view why) {
    return result::fail(result::ErrorClass::DataLoss, kWorldKestDomain, code(WorldKestError::CookedGameInvalid), why)
        .error();
}

std::string hexOf(base::Bits128 value) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(value, digits);
    return std::string{digits.data(), digits.size()};
}

bool wellFormed(const CookedMod& mod) {
    if (mod.scenes.size() > kMaximumCookedGameNames || mod.programs.size() > 1) {
        return false;
    }
    if (!mod.programs.empty() &&
        (mod.programs[0].path.empty() || mod.programs[0].entry.empty() || mod.programs[0].sources == base::Bits128{})) {
        return false;
    }
    for (std::size_t at = 0; at < mod.scenes.size(); ++at) {
        if (mod.scenes[at].path.empty() || mod.scenes[at].scene == base::Bits128{} ||
            (at > 0 && !(mod.scenes[at - 1].path < mod.scenes[at].path))) {
            return false;
        }
    }
    return true;
}

const std::string* textOf(const Value& object, std::size_t members, std::string_view name) {
    if (object.kind() != Value::Kind::Object || object.names().size() != members) {
        return nullptr;
    }
    const Value* member = object.find(name);
    return member == nullptr ? nullptr : member->text();
}

} // namespace

const CookedGameScene* CookedMod::scene(std::string_view path) const noexcept {
    const auto kFound = std::ranges::find(scenes, path, &CookedGameScene::path);
    return kFound == scenes.end() ? nullptr : &*kFound;
}

result::Result<std::string> writeCookedMod(const CookedMod& mod) {
    CookedMod sorted = mod;
    std::ranges::sort(sorted.scenes, {}, &CookedGameScene::path);
    if (!wellFormed(sorted)) {
        return std::unexpected<result::Error>{invalid("a cooked mod names each scene once, by its resource")};
    }
    Value scenes = Value::array();
    for (CookedGameScene& scene : sorted.scenes) {
        Value each = Value::object();
        each.add("path", Value::string(std::move(scene.path)));
        each.add("scene", Value::string(hexOf(scene.scene)));
        scenes.push(std::move(each));
    }
    Value programs = Value::array();
    for (CookedGameProgram& program : sorted.programs) {
        Value each = Value::object();
        each.add("entry", Value::string(std::move(program.entry)));
        each.add("path", Value::string(std::move(program.path)));
        each.add("sources", Value::string(hexOf(program.sources)));
        programs.push(std::move(each));
    }
    Value record = Value::object();
    record.add("formatVersion", Value::integer(2));
    record.add("kind", Value::string("mod.description"));
    record.add("programs", std::move(programs));
    record.add("scenes", std::move(scenes));
    record.add("text", Value::string(std::move(sorted.text)));
    auto written = document::writeCanonicalRecord(record);
    if (!written.has_value()) {
        return std::unexpected<result::Error>{invalid("a cooked mod's text is not what a record can hold")};
    }
    return written;
}

result::Result<CookedMod> readCookedMod(std::string_view bytes) {
    auto parsed = document::parseCanonicalRecord(bytes);
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{invalid("a cooked mod is not a canonical record")};
    }
    const Value* kind = parsed->find("kind");
    const Value* version = parsed->find("formatVersion");
    const Value* scenes = parsed->find("scenes");
    const Value* programs = parsed->find("programs");
    const std::string* text = textOf(*parsed, 5, "text");
    if (kind == nullptr || kind->text() == nullptr || *kind->text() != "mod.description" || version == nullptr ||
        version->integer() != 2 || scenes == nullptr || scenes->kind() != Value::Kind::Array || programs == nullptr ||
        programs->kind() != Value::Kind::Array || text == nullptr) {
        return std::unexpected<result::Error>{invalid("a cooked mod is mod.description, format 2, and its parts")};
    }
    CookedMod mod{.text = *text};
    for (const Value& each : scenes->items()) {
        const std::string* path = textOf(each, 2, "path");
        const std::string* scene = textOf(each, 2, "scene");
        const base::Bits128Parse kScene = scene != nullptr ? base::parseBits128Hex(*scene) : base::Bits128Parse{};
        if (path == nullptr || !kScene.parsed || hexOf(kScene.value) != *scene) {
            return std::unexpected<result::Error>{invalid("a cooked mod's scene is a path and a scene")};
        }
        mod.scenes.push_back(CookedGameScene{.path = *path, .scene = kScene.value});
    }
    for (const Value& each : programs->items()) {
        const std::string* path = textOf(each, 3, "path");
        const std::string* entry = textOf(each, 3, "entry");
        const std::string* sources = textOf(each, 3, "sources");
        const base::Bits128Parse kSources = sources != nullptr ? base::parseBits128Hex(*sources) : base::Bits128Parse{};
        if (path == nullptr || entry == nullptr || !kSources.parsed || hexOf(kSources.value) != *sources) {
            return std::unexpected<result::Error>{invalid("a cooked mod's program is a path, sources, and entry")};
        }
        mod.programs.push_back(CookedGameProgram{.path = *path, .sources = kSources.value, .entry = *entry});
    }
    if (!wellFormed(mod)) {
        return std::unexpected<result::Error>{invalid("a cooked mod names each scene once, in order, fully")};
    }
    return mod;
}

} // namespace rawframe::world_kest
