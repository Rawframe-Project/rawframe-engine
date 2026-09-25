#include "rawframe/world_kest/cooked_game.h"

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

/// Paths present, strictly increasing, and not too many.
template <typename Named> bool ordered(const std::vector<Named>& named) {
    if (named.size() > kMaximumCookedGameNames) {
        return false;
    }
    for (std::size_t at = 0; at < named.size(); ++at) {
        if (named[at].path.empty() || (at > 0 && !(named[at - 1].path < named[at].path))) {
            return false;
        }
    }
    return true;
}

bool wellFormed(const CookedGame& game) {
    return ordered(game.files) && ordered(game.meshes) && ordered(game.programs) && ordered(game.scenes) &&
           std::ranges::all_of(game.programs,
                               [](const CookedGameProgram& program) {
                                   return !program.entry.empty() && program.sources != base::Bits128{};
                               }) &&
           std::ranges::all_of(game.scenes,
                               [](const CookedGameScene& scene) {
                                   return scene.scene != base::Bits128{};
                               }) &&
           std::ranges::all_of(game.meshes, [](const CookedGameMesh& mesh) {
               return mesh.mesh != base::Bits128{};
           });
}

/// A resource identity as 32 lowercase hex digits, not nought.
std::optional<base::Bits128> identityOf(const std::string* text) {
    const base::Bits128Parse kParsed = text != nullptr ? base::parseBits128Hex(*text) : base::Bits128Parse{};
    if (!kParsed.parsed || hexOf(kParsed.value) != *text) {
        return std::nullopt;
    }
    return kParsed.value;
}

/// The text of member `name` of an object with exactly `members` members.
const std::string* textOf(const Value& object, std::size_t members, std::string_view name) {
    if (object.kind() != Value::Kind::Object || object.names().size() != members) {
        return nullptr;
    }
    const Value* member = object.find(name);
    return member == nullptr ? nullptr : member->text();
}

} // namespace

const CookedGameFile* CookedGame::file(std::string_view path) const noexcept {
    const auto kFound = std::ranges::find(files, path, &CookedGameFile::path);
    return kFound == files.end() ? nullptr : &*kFound;
}

const CookedGameProgram* CookedGame::program(std::string_view path) const noexcept {
    const auto kFound = std::ranges::find(programs, path, &CookedGameProgram::path);
    return kFound == programs.end() ? nullptr : &*kFound;
}

const CookedGameScene* CookedGame::scene(std::string_view path) const noexcept {
    const auto kFound = std::ranges::find(scenes, path, &CookedGameScene::path);
    return kFound == scenes.end() ? nullptr : &*kFound;
}

const CookedGameMesh* CookedGame::mesh(std::string_view path) const noexcept {
    const auto kFound = std::ranges::find(meshes, path, &CookedGameMesh::path);
    return kFound == meshes.end() ? nullptr : &*kFound;
}

result::Result<std::string> writeCookedGame(const CookedGame& game) {
    CookedGame sorted = game;
    std::ranges::sort(sorted.files, {}, &CookedGameFile::path);
    std::ranges::sort(sorted.programs, {}, &CookedGameProgram::path);
    std::ranges::sort(sorted.scenes, {}, &CookedGameScene::path);
    std::ranges::sort(sorted.meshes, {}, &CookedGameMesh::path);
    if (!wellFormed(sorted)) {
        return std::unexpected<result::Error>{
            invalid("a cooked game names each path once, each program's sources and entry, and not too many")};
    }
    Value files = Value::array();
    for (CookedGameFile& file : sorted.files) {
        Value each = Value::object();
        each.add("path", Value::string(std::move(file.path)));
        each.add("text", Value::string(std::move(file.text)));
        files.push(std::move(each));
    }
    Value programs = Value::array();
    for (CookedGameProgram& program : sorted.programs) {
        Value each = Value::object();
        each.add("entry", Value::string(std::move(program.entry)));
        each.add("path", Value::string(std::move(program.path)));
        each.add("sources", Value::string(hexOf(program.sources)));
        programs.push(std::move(each));
    }
    Value scenes = Value::array();
    for (CookedGameScene& scene : sorted.scenes) {
        Value each = Value::object();
        each.add("path", Value::string(std::move(scene.path)));
        each.add("scene", Value::string(hexOf(scene.scene)));
        scenes.push(std::move(each));
    }
    Value meshes = Value::array();
    for (CookedGameMesh& mesh : sorted.meshes) {
        Value each = Value::object();
        each.add("mesh", Value::string(hexOf(mesh.mesh)));
        each.add("path", Value::string(std::move(mesh.path)));
        meshes.push(std::move(each));
    }
    Value record = Value::object();
    record.add("files", std::move(files));
    record.add("formatVersion", Value::integer(3));
    record.add("kind", Value::string("game.description"));
    record.add("meshes", std::move(meshes));
    record.add("programs", std::move(programs));
    record.add("scenes", std::move(scenes));
    record.add("text", Value::string(std::move(sorted.text)));
    auto written = document::writeCanonicalRecord(record);
    if (!written.has_value()) {
        return std::unexpected<result::Error>{invalid("a cooked game's text is not what a record can hold")};
    }
    return written;
}

result::Result<CookedGame> readCookedGame(std::string_view bytes) {
    auto parsed = document::parseCanonicalRecord(bytes);
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{invalid("a cooked game is not a canonical record")};
    }
    const Value* kind = parsed->find("kind");
    const Value* version = parsed->find("formatVersion");
    const Value* files = parsed->find("files");
    const Value* programs = parsed->find("programs");
    const Value* scenes = parsed->find("scenes");
    const Value* meshes = parsed->find("meshes");
    const std::string* text = textOf(*parsed, 7, "text");
    if (kind == nullptr || kind->text() == nullptr || *kind->text() != "game.description" || version == nullptr ||
        version->integer() != 3 || files == nullptr || files->kind() != Value::Kind::Array || programs == nullptr ||
        programs->kind() != Value::Kind::Array || scenes == nullptr || scenes->kind() != Value::Kind::Array ||
        meshes == nullptr || meshes->kind() != Value::Kind::Array || text == nullptr) {
        return std::unexpected<result::Error>{invalid("a cooked game is game.description, format 3, and its parts")};
    }
    CookedGame game{.text = *text};
    for (const Value& each : files->items()) {
        const std::string* path = textOf(each, 2, "path");
        const std::string* fileText = textOf(each, 2, "text");
        if (path == nullptr || fileText == nullptr) {
            return std::unexpected<result::Error>{invalid("a cooked game's file is a path and a text")};
        }
        game.files.push_back(CookedGameFile{.path = *path, .text = *fileText});
    }
    for (const Value& each : programs->items()) {
        const std::string* path = textOf(each, 3, "path");
        const std::string* entry = textOf(each, 3, "entry");
        const std::optional<base::Bits128> kSources = identityOf(textOf(each, 3, "sources"));
        if (path == nullptr || entry == nullptr || !kSources.has_value()) {
            return std::unexpected<result::Error>{invalid("a cooked game's program is a path, sources, and entry")};
        }
        game.programs.push_back(CookedGameProgram{.path = *path, .sources = *kSources, .entry = *entry});
    }
    for (const Value& each : scenes->items()) {
        const std::string* path = textOf(each, 2, "path");
        const std::optional<base::Bits128> kScene = identityOf(textOf(each, 2, "scene"));
        if (path == nullptr || !kScene.has_value()) {
            return std::unexpected<result::Error>{invalid("a cooked game's scene is a path and a scene")};
        }
        game.scenes.push_back(CookedGameScene{.path = *path, .scene = *kScene});
    }
    for (const Value& each : meshes->items()) {
        const std::string* path = textOf(each, 2, "path");
        const std::optional<base::Bits128> kMesh = identityOf(textOf(each, 2, "mesh"));
        if (path == nullptr || !kMesh.has_value()) {
            return std::unexpected<result::Error>{invalid("a cooked game's mesh is a path and a mesh")};
        }
        game.meshes.push_back(CookedGameMesh{.path = *path, .mesh = *kMesh});
    }
    if (!wellFormed(game)) {
        return std::unexpected<result::Error>{invalid("a cooked game names each path once, in order, fully")};
    }
    return game;
}

} // namespace rawframe::world_kest
