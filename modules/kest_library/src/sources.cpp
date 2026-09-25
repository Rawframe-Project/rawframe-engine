#include "rawframe/document/json.h"
#include "rawframe/kest_library/errors.h"
#include "rawframe/kest_library/library.h"

#include <algorithm>

namespace rawframe::kest_library {

namespace {

using document::Value;

result::Error invalid(std::string_view why) {
    return result::fail(result::ErrorClass::DataLoss, kKestLibraryDomain, code(KestLibraryError::SourcesInvalid), why)
        .error();
}

/// Paths plain, strictly increasing, and not too many.
bool wellFormed(std::span<const kest::SourceFile> files) {
    if (files.size() > kMaximumGameFiles) {
        return false;
    }
    for (std::size_t at = 0; at < files.size(); ++at) {
        if (!plainGamePath(files[at].path) || (at > 0 && !(files[at - 1].path < files[at].path))) {
            return false;
        }
    }
    return true;
}

} // namespace

result::Result<std::string> writeGameSources(std::span<const kest::SourceFile> files) {
    std::vector<kest::SourceFile> ordered{files.begin(), files.end()};
    std::ranges::sort(ordered, {}, &kest::SourceFile::path);
    if (!wellFormed(ordered)) {
        return std::unexpected<result::Error>{invalid("a game's files are plain paths, each once, and not too many")};
    }
    Value listed = Value::array();
    for (kest::SourceFile& file : ordered) {
        Value each = Value::object();
        each.add("path", Value::string(std::move(file.path)));
        each.add("text", Value::string(std::move(file.text)));
        listed.push(std::move(each));
    }
    Value record = Value::object();
    record.add("files", std::move(listed));
    record.add("formatVersion", Value::integer(1));
    record.add("kind", Value::string("kest.sources"));
    auto written = document::writeCanonicalRecord(record);
    if (!written.has_value()) {
        return std::unexpected<result::Error>{invalid("a game's file is not text a record can hold")};
    }
    return written;
}

result::Result<std::vector<kest::SourceFile>> readGameSources(std::string_view bytes) {
    auto parsed = document::parseCanonicalRecord(bytes);
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{invalid("a game's sources are not a canonical record")};
    }
    const Value* kind = parsed->find("kind");
    const Value* version = parsed->find("formatVersion");
    const Value* listed = parsed->find("files");
    if (parsed->names().size() != 3 || kind == nullptr || kind->text() == nullptr || *kind->text() != "kest.sources" ||
        version == nullptr || version->integer() != 1 || listed == nullptr || listed->kind() != Value::Kind::Array) {
        return std::unexpected<result::Error>{invalid("a game's sources are kest.sources, format 1, and files")};
    }
    std::vector<kest::SourceFile> files;
    for (const Value& each : listed->items()) {
        const Value* path = each.find("path");
        const Value* text = each.find("text");
        if (each.kind() != Value::Kind::Object || each.names().size() != 2 || path == nullptr ||
            path->text() == nullptr || text == nullptr || text->text() == nullptr) {
            return std::unexpected<result::Error>{invalid("a game's file is a path and a text")};
        }
        files.push_back(kest::SourceFile{.path = *path->text(), .text = *text->text()});
    }
    if (!wellFormed(files)) {
        return std::unexpected<result::Error>{invalid("a game's files are plain paths, in order, each once")};
    }
    return files;
}

} // namespace rawframe::kest_library
