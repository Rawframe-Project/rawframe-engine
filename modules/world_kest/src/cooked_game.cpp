#include "rawframe/world_kest/cooked_game.h"

#include "rawframe/document/json.h"
#include "rawframe/world_kest/errors.h"

#include <algorithm>
#include <array>

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
    return ordered(game.files) && ordered(game.programs) &&
           std::ranges::all_of(game.programs, [](const CookedGameProgram& program) {
               return !program.entry.empty() && program.sources != base::Bits128{};
           });
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

result::Result<std::string> writeCookedGame(const CookedGame& game) {
    CookedGame sorted = game;
    std::ranges::sort(sorted.files, {}, &CookedGameFile::path);
    std::ranges::sort(sorted.programs, {}, &CookedGameProgram::path);
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
    Value record = Value::object();
    record.add("files", std::move(files));
    record.add("formatVersion", Value::integer(1));
    record.add("kind", Value::string("game.description"));
    record.add("programs", std::move(programs));
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
    const std::string* text = textOf(*parsed, 5, "text");
    if (kind == nullptr || kind->text() == nullptr || *kind->text() != "game.description" || version == nullptr ||
        version->integer() != 1 || files == nullptr || files->kind() != Value::Kind::Array || programs == nullptr ||
        programs->kind() != Value::Kind::Array || text == nullptr) {
        return std::unexpected<result::Error>{invalid("a cooked game is game.description, format 1, and its parts")};
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
        const std::string* sources = textOf(each, 3, "sources");
        const base::Bits128Parse kSources = sources != nullptr ? base::parseBits128Hex(*sources) : base::Bits128Parse{};
        if (path == nullptr || entry == nullptr || !kSources.parsed || hexOf(kSources.value) != *sources) {
            return std::unexpected<result::Error>{invalid("a cooked game's program is a path, sources, and entry")};
        }
        game.programs.push_back(CookedGameProgram{.path = *path, .sources = kSources.value, .entry = *entry});
    }
    if (!wellFormed(game)) {
        return std::unexpected<result::Error>{invalid("a cooked game names each path once, in order, fully")};
    }
    return game;
}

} // namespace rawframe::world_kest
