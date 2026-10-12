#include "references.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/base/hex64.h"
#include "rawframe/ui/document.h"
#include "rawframe/ui/styles.h"

#include <optional>
#include <string>
#include <variant>

namespace rawframe::authoring_session {

namespace {

std::optional<ui::StyleSheet> sheetOf(const std::filesystem::path& path) {
    const auto kText = path.empty() ? std::nullopt : readFile(path);
    auto read = kText.has_value() ? ui::readStyles(*kText) : result::Result<ui::StyleSheet>{};
    return kText.has_value() && read.has_value() ? std::optional{std::move(*read)} : std::nullopt;
}

std::optional<ui::UiDocument> documentOf(const std::filesystem::path& path) {
    const auto kText = path.empty() ? std::nullopt : readFile(path);
    auto read = kText.has_value() ? ui::readDocument(*kText) : result::Result<ui::UiDocument>{};
    return kText.has_value() && read.has_value() ? std::optional{std::move(*read)} : std::nullopt;
}

std::string hexOf(std::uint64_t id) {
    std::string text;
    for (int at = 60; at >= 0; at -= 4) {
        text.push_back("0123456789abcdef"[(id >> static_cast<unsigned>(at)) & 0xFU]);
    }
    return text;
}

} // namespace

std::set<std::uint64_t> unresolvedStyles(const GameDocuments& game) {
    const std::optional<ui::StyleSheet> kSheet = sheetOf(game.styles);
    const std::optional<ui::UiDocument> kDocument = documentOf(game.ui);
    std::set<std::uint64_t> made;
    if (!kSheet.has_value() || !kDocument.has_value()) {
        return made;
    }
    for (const ui::DocumentNode& kNode : kDocument->nodes) {
        if (kNode.fields.style != 0 && !kSheet->styleWithId(kNode.fields.style).has_value()) {
            made.insert(kNode.fields.style);
        }
    }
    return made;
}

result::Status
referencesKept(const GameDocuments& game, const std::filesystem::path& path, const authoring::Request& request) {
    const bool kUi = !game.ui.empty() && path == game.ui;
    const bool kStyles = !game.styles.empty() && path == game.styles;
    if (!kUi && !kStyles) {
        return {};
    }
    const std::optional<ui::StyleSheet> kSheet = sheetOf(game.styles);
    const std::optional<ui::UiDocument> kDocument = documentOf(game.ui);
    for (const authoring::Operation& kOperation : request.operations) {
        if (const auto* set = std::get_if<authoring::SetUiField>(&kOperation); kUi && set != nullptr) {
            const std::string* named = set->field == "style" && set->value.has_value() ? set->value->text() : nullptr;
            const std::optional<std::uint64_t> kStyle = named != nullptr ? base::parseHex64(*named) : std::nullopt;
            if (kStyle.has_value() && kSheet.has_value() && !kSheet->styleWithId(*kStyle).has_value()) {
                return std::unexpected{result::fail(result::ErrorClass::InvalidArgument,
                                                    authoring::kAuthoringDomain,
                                                    code(authoring::AuthoringError::ValidationFailed),
                                                    "a node's style is a class the game's style sheet has")
                                           .error()
                                           .withContext("style", *named)};
            }
        }
        if (const auto* create = std::get_if<authoring::CreateUiNode>(&kOperation);
            kUi && create != nullptr && !game.nodes.contains(create->node)) {
            return std::unexpected{result::fail(result::ErrorClass::InvalidArgument,
                                                authoring::kAuthoringDomain,
                                                code(authoring::AuthoringError::ValidationFailed),
                                                "a node is of a component the game declares a rawframe.ui.Node")
                                       .error()
                                       .withContext("node", create->node)};
        }
        if (const auto* remove = std::get_if<authoring::RemoveStyleClass>(&kOperation);
            kStyles && remove != nullptr && kDocument.has_value()) {
            for (const ui::DocumentNode& kNode : kDocument->nodes) {
                if (kNode.fields.style == remove->style) {
                    return std::unexpected{result::fail(result::ErrorClass::Conflict,
                                                        authoring::kAuthoringDomain,
                                                        code(authoring::AuthoringError::Conflict),
                                                        "a class a node of the game's UI document names stays")
                                               .error()
                                               .withContext("style", hexOf(remove->style))
                                               .withContext("node", kNode.component)};
                }
            }
        }
    }
    return {};
}

} // namespace rawframe::authoring_session
