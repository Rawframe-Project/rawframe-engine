#include "rawframe/animation/resources.h"

#include "rawframe/document/json.h"

namespace rawframe::animation {

std::optional<DocumentKind> documentKind(std::string_view text) {
    const auto kParsed = document::parse(text);
    const document::Value* kind = kParsed.has_value() ? kParsed->find("kind") : nullptr;
    if (kind == nullptr || kind->kind() != document::Value::Kind::String) {
        return std::nullopt;
    }
    if (*kind->text() == "animation.skeleton") {
        return DocumentKind::Skeleton;
    }
    if (*kind->text() == "animation.clip") {
        return DocumentKind::Clip;
    }
    if (*kind->text() == "animation.graph") {
        return DocumentKind::Graph;
    }
    if (*kind->text() == "animation.mask") {
        return DocumentKind::Mask;
    }
    return std::nullopt;
}

} // namespace rawframe::animation
