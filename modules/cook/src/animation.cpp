#include "rawframe/cook/animation.h"

#include "rawframe/animation/clip.h"
#include "rawframe/animation/graph.h"
#include "rawframe/animation/mask.h"
#include "rawframe/animation/resources.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/cook/errors.h"

namespace rawframe::cook {

namespace {

std::unexpected<result::Error> refuse(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kCookDomain, code(CookError::BadSidecar), why);
}

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return refuse("an animation document takes no settings");
    }
    return std::string{};
}

result::Result<Artifact> cookAnimation(std::span<const std::byte> source, std::string_view, Reads&) {
    const std::string_view kText{reinterpret_cast<const char*>(source.data()), source.size()};
    const std::optional<animation::DocumentKind> kKind = animation::documentKind(kText);
    if (!kKind.has_value()) {
        return refuse("an animation document is a skeleton, a clip, a graph, or a mask");
    }
    base::Bits128 type;
    std::string_view representation;
    switch (*kKind) {
    case animation::DocumentKind::Skeleton:
        RAWFRAME_TRY(animation::readSkeleton(kText));
        type = animation::kSkeletonType;
        representation = animation::kSkeletonRepresentation;
        break;
    case animation::DocumentKind::Clip:
        RAWFRAME_TRY(animation::readClip(kText));
        type = animation::kClipType;
        representation = animation::kClipRepresentation;
        break;
    case animation::DocumentKind::Graph:
        RAWFRAME_TRY(animation::readGraph(kText));
        type = animation::kGraphType;
        representation = animation::kGraphRepresentation;
        break;
    case animation::DocumentKind::Mask:
        RAWFRAME_TRY(animation::readMask(kText));
        type = animation::kMaskType;
        representation = animation::kMaskRepresentation;
        break;
    }
    return Artifact{.type = content::ResourceTypeId{type},
                    .representation = *content::RepresentationId::parse(representation),
                    .bytes = {source.begin(), source.end()}};
}

} // namespace

Importer animationImporter() noexcept {
    return Importer{.identity = "rawframe.animation", .normalize = &normalize, .cook = &cookAnimation};
}

} // namespace rawframe::cook
