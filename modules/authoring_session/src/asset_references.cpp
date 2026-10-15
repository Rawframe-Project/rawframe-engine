#include "asset_references.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/content/sidecar.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace rawframe::authoring_session {

namespace {

using document::Value;

std::unexpected<result::Error> refused(std::string_view why) {
    return std::unexpected{result::fail(result::ErrorClass::NotFound,
                                        authoring::kAuthoringDomain,
                                        authoring::code(authoring::AuthoringError::TargetNotFound),
                                        why)
                               .error()};
}

/// The file a read names: its path from the reading source's directory,
/// made the game's, a sidecar's read its source's; none for a directory
/// listed, which says only what a folder holds.
std::optional<std::string> fileRead(const std::filesystem::path& root, std::string_view reader, std::string_view read) {
    const std::filesystem::path kFolder = std::filesystem::path{std::string{reader}}.parent_path();
    std::string path = (kFolder / std::string{read}).lexically_normal().generic_string();
    if (path.ends_with(content::kSidecarSuffix)) {
        path.resize(path.size() - content::kSidecarSuffix.size());
    }
    std::error_code error;
    return std::filesystem::is_regular_file(root / path, error) ? std::optional<std::string>{path} : std::nullopt;
}

Value listOf(const std::set<std::string>& paths) {
    Value made = Value::array();
    for (const std::string& kPath : paths) {
        made.push(Value::string(kPath));
    }
    return made;
}

} // namespace

result::Result<Value> assetReferencesOf(const std::filesystem::path& game,
                                        std::string_view asset,
                                        const std::optional<std::filesystem::path>& cooked) {
    std::set<std::string, std::less<>> sources;
    for (const SourceListed& kSource : sourcesBeside(game)) {
        sources.insert(kSource.path);
    }
    if (!sources.contains(asset)) {
        return refused("no source with a sidecar is at that path under the game's directory");
    }
    if (!cooked.has_value()) {
        return refused("this session has not cooked the game");
    }
    const auto kText = readFile(*cooked / "cook.receipt");
    if (!kText.has_value()) {
        return refused("the last cook's receipt does not read");
    }
    const auto kReceipt = document::parse(*kText);
    const Value* kInputs = kReceipt.has_value() ? kReceipt->find("inputs") : nullptr;
    if (kInputs == nullptr || kInputs->kind() != Value::Kind::Array) {
        return refused("the last cook's receipt does not read");
    }
    std::set<std::string> uses;
    std::set<std::string> usedBy;
    for (const Value& kInput : kInputs->items()) {
        const Value* kSource = kInput.find("source");
        const Value* kReads = kInput.find("reads");
        if (kSource == nullptr || kSource->text() == nullptr || kReads == nullptr) {
            continue;
        }
        for (const Value& kRead : kReads->items()) {
            const Value* kPath = kRead.find("path");
            if (kPath == nullptr || kPath->text() == nullptr) {
                continue;
            }
            const std::optional<std::string> kNamed = fileRead(game.parent_path(), *kSource->text(), *kPath->text());
            if (!kNamed.has_value() || *kNamed == *kSource->text()) {
                continue;
            }
            if (*kSource->text() == asset) {
                uses.insert(*kNamed);
            }
            if (*kNamed == asset) {
                usedBy.insert(*kSource->text());
            }
        }
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.asset_references"));
    made.add("asset", Value::string(std::string{asset}));
    made.add("uses", listOf(uses));
    made.add("usedBy", listOf(usedBy));
    return made;
}

} // namespace rawframe::authoring_session
