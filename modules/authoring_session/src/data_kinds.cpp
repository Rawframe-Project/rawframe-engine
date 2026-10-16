#include "data_kinds.h"

#include "rawframe/animation/clip.h"
#include "rawframe/animation/graph.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/audio/layout.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/document/json.h"
#include "rawframe/input/actions.h"
#include "rawframe/localization/table.h"
#include "rawframe/world_kest/game.h"

#include <string>

namespace rawframe::authoring_session {

namespace {

/// What `read` makes of a document, or what its reader refused and
/// where, said to whoever asked as a document of the kind `called` that
/// does not read (`ValidationFailed`), as a style sheet's is (D636).
template <class Read> result::Status readsAs(std::string_view called, Read read) {
    auto made = read();
    if (made.has_value()) {
        return {};
    }
    std::string why = "the " + std::string{called} + " does not read: " + std::string{made.error().description()};
    for (const result::ContextField& kField : made.error().context()) {
        if (kField.key == "path") {
            why += " at " + std::string{kField.value};
        }
    }
    return std::unexpected<result::Error>{
        std::move(made).error().mappedTo(result::ErrorClass::InvalidArgument,
                                         authoring::kAuthoringDomain,
                                         authoring::code(authoring::AuthoringError::ValidationFailed),
                                         why)};
}

result::Status readsActions(std::string_view text) {
    return readsAs("action map", [text] {
        return input::readActionSet(text);
    });
}
result::Status readsMixer(std::string_view text) {
    return readsAs("mixer", [text] {
        return audio::readLayout(text);
    });
}
result::Status readsStrings(std::string_view text) {
    return readsAs("string table", [text] {
        return localization::readStrings(text);
    });
}
result::Status readsTranslations(std::string_view text) {
    return readsAs("translation table", [text] {
        return localization::readTranslations(text);
    });
}
/// An animation document is a graph, a clip, or a skeleton (D827), as its
/// kind says.
result::Status readsAnimation(std::string_view text) {
    const auto kParsed = document::parse(text);
    const document::Value* kKind = kParsed.has_value() ? kParsed->find("kind") : nullptr;
    const std::string_view kNamed =
        kKind != nullptr && kKind->kind() == document::Value::Kind::String ? *kKind->text() : std::string_view{};
    if (kNamed == "animation.clip") {
        return readsAs("animation clip", [text] {
            return animation::readClip(text);
        });
    }
    if (kNamed == "animation.skeleton") {
        return readsAs("skeleton", [text] {
            return animation::readSkeleton(text);
        });
    }
    return readsAs("animation graph", [text] {
        return animation::readGraph(text);
    });
}

constexpr std::array<DataKind, 5> kKinds = {{{".actions", "action map", &readsActions},
                                             {".mixer", "mixer", &readsMixer},
                                             {".strings", "string table", &readsStrings},
                                             {".translations", "translation table", &readsTranslations},
                                             {".rfanim", "animation document", &readsAnimation}}};

} // namespace

std::span<const DataKind> dataKinds() noexcept {
    return kKinds;
}

result::Status readsGameDescription(std::string_view text) {
    return readsAs("game description", [text] {
        return world_kest::parseGame(text);
    });
}

} // namespace rawframe::authoring_session
