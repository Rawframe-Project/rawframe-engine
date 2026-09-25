#include "animation_plan.h"

#include "rawframe/animation/clip.h"
#include "rawframe/animation/graph.h"
#include "rawframe/animation/instance.h"
#include "rawframe/animation/mask.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/world_kest/errors.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::world_kest {

namespace {

std::unexpected<result::Error> refuse(std::string_view why, std::string_view graph) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kWorldKestDomain, code(WorldKestError::BadGameLine), why)
            .error()
            .withContext("graph", graph)};
}

/// A failure below, told of the graph it was for.
std::unexpected<result::Error> within(result::Error error, std::string_view graph) {
    return std::unexpected<result::Error>{std::move(error).withContext("graph", graph)};
}

/// The field's type as the World reads it, when it may carry a parameter of
/// `type`: a number of either width for a `float` or a `vec2` lane, a u32
/// for an `int`, and a bool for a `bool`.
std::optional<schema::FieldType> carried(kest::FieldKind kind, animation::ParameterType type) {
    switch (type) {
    case animation::ParameterType::Float:
    case animation::ParameterType::Vec2:
        if (kind == kest::FieldKind::F32) {
            return schema::FieldType::F32;
        }
        if (kind == kest::FieldKind::F64) {
            return schema::FieldType::F64;
        }
        return std::nullopt;
    case animation::ParameterType::Int:
        return kind == kest::FieldKind::U32 ? std::optional{schema::FieldType::U32} : std::nullopt;
    case animation::ParameterType::Bool:
        return kind == kest::FieldKind::Bool ? std::optional{schema::FieldType::Bool} : std::nullopt;
    }
    return std::nullopt;
}

/// The field's type as the World writes it, when a track of `channel` may
/// play it: a number of either width for `float`, and a u8, u32, or u64
/// for `discrete`.
std::optional<schema::FieldType> played(kest::FieldKind kind, animation::Channel channel) {
    if (channel == animation::Channel::Float) {
        return carried(kind, animation::ParameterType::Float);
    }
    switch (kind) {
    case kest::FieldKind::U8:
        return schema::FieldType::U8;
    case kest::FieldKind::U32:
        return schema::FieldType::U32;
    case kest::FieldKind::U64:
        return schema::FieldType::U64;
    default:
        return std::nullopt;
    }
}

result::Result<world_animation::AnimatorSettings>
animatorSettings(const GameFiles& files, std::span<const kest::TypeLayout> layouts, const GameAnimator& animator) {
    const GameDescription& game = files.description();
    RAWFRAME_TRY_ASSIGN(const std::string_view kGraphText, files.animatorGraph(animator.path));
    auto graph = animation::readGraph(kGraphText);
    if (!graph.has_value()) {
        return within(std::move(graph).error(), animator.path);
    }
    std::vector<animation::NamedClip> clips;
    std::optional<base::Bits128> skeletonId;
    for (const base::Bits128 kClip : animation::clipsOf(*graph)) {
        RAWFRAME_TRY_ASSIGN(const std::string_view kClipText, files.animationDocument(kClip));
        auto clip = animation::readClip(kClipText);
        if (!clip.has_value()) {
            return within(std::move(clip).error(), animator.path);
        }
        if (clip->skeleton.has_value()) {
            if (skeletonId.has_value() && *skeletonId != *clip->skeleton) {
                return refuse("an animator's clips animate one skeleton", animator.path);
            }
            skeletonId = clip->skeleton;
        }
        clips.push_back(animation::NamedClip{.id = kClip, .clip = std::make_shared<const animation::Clip>(*clip)});
    }
    // A graph of property clips alone (D139) poses no bone.
    result::Result<animation::Skeleton> skeleton = animation::Skeleton{};
    if (skeletonId.has_value()) {
        RAWFRAME_TRY_ASSIGN(const std::string_view kSkeletonText, files.animationDocument(*skeletonId));
        skeleton = animation::readSkeleton(kSkeletonText);
        if (!skeleton.has_value()) {
            return within(std::move(skeleton).error(), animator.path);
        }
    }
    std::vector<animation::NamedMask> masks;
    for (const base::Bits128 kMask : animation::masksOf(*graph)) {
        RAWFRAME_TRY_ASSIGN(const std::string_view kMaskText, files.animationDocument(kMask));
        auto mask = animation::readMask(kMaskText);
        if (!mask.has_value()) {
            return within(std::move(mask).error(), animator.path);
        }
        masks.push_back(animation::NamedMask{.id = kMask, .mask = std::make_shared<const animation::Mask>(*mask)});
    }
    auto compiled =
        animation::CompiledGraph::compile(*graph, *skeleton, skeletonId.value_or(base::Bits128{}), clips, masks);
    if (!compiled.has_value()) {
        return within(std::move(compiled).error(), animator.path);
    }

    world_animation::AnimatorSettings settings{.id = animator.id, .graph = std::move(*compiled)};
    // Each field the clips animate: a game component's, by its identity,
    // and a field of the kind its track plays, by its name.
    for (const animation::CompiledGraph::Property& property : settings.graph->properties()) {
        const auto kComponent =
            std::ranges::find(game.components, schema::ComponentTypeId{property.binding.component}, &GameComponent::id);
        const kest::TypeLayout* layout = kComponent != game.components.end()
                                             ? &layouts[static_cast<std::size_t>(kComponent - game.components.begin())]
                                             : nullptr;
        const auto kField = layout != nullptr
                                ? std::ranges::find(layout->fields, property.binding.field, &kest::Field::name)
                                : std::vector<kest::Field>::const_iterator{};
        const std::optional<schema::FieldType> kType =
            layout != nullptr && kField != layout->fields.end() ? played(kField->kind, property.channel) : std::nullopt;
        if (!kType.has_value()) {
            return std::unexpected<result::Error>{
                refuse("a field an animator's clips animate is a game component's, of a kind its track plays",
                       animator.path)
                    .error()
                    .withContext("field", property.binding.field)};
        }
        settings.properties.push_back(
            world_animation::PropertyField{.component = kComponent->id, .offset = kField->offset, .type = *kType});
    }
    if (animator.subset.has_value()) {
        RAWFRAME_TRY_ASSIGN(const std::string_view kSubsetText, files.animationDocument(*animator.subset));
        auto mask = animation::readMask(kSubsetText);
        if (!mask.has_value()) {
            return within(std::move(mask).error(), animator.path);
        }
        auto subset = animation::boneSubset(*mask, *skeleton, *skeletonId);
        if (!subset.has_value()) {
            return within(std::move(subset).error(), animator.path);
        }
        settings.subset = std::move(*subset);
    }
    if (animator.parameters.empty()) {
        return settings;
    }
    const auto kComponent = std::ranges::find(game.components, animator.parameters, &GameComponent::name);
    const kest::TypeLayout& layout = layouts[static_cast<std::size_t>(kComponent - game.components.begin())];
    settings.parameters = kComponent->id;
    for (std::uint32_t index = 0; index < settings.graph->parameterCount(); ++index) {
        const animation::ParameterIndex kParameter{index};
        const animation::Parameter& declared = settings.graph->declaration(kParameter);
        const bool kPair = declared.type == animation::ParameterType::Vec2;
        for (std::uint8_t lane = 0; lane < (kPair ? 2U : 1U); ++lane) {
            const std::string kName = kPair ? declared.name + (lane == 0 ? ".x" : ".y") : declared.name;
            const auto kField = std::ranges::find(layout.fields, kName, &kest::Field::name);
            const std::optional<schema::FieldType> kType =
                kField != layout.fields.end() ? carried(kField->kind, declared.type) : std::nullopt;
            if (!kType.has_value()) {
                return std::unexpected<result::Error>{
                    refuse("an animator's parameters component has a field of each parameter's type, by its name",
                           animator.path)
                        .error()
                        .withContext("field", kName)};
            }
            settings.fields.push_back(world_animation::ParameterField{
                .parameter = kParameter, .lane = lane, .offset = kField->offset, .type = *kType});
        }
    }
    return settings;
}

} // namespace

result::Result<std::optional<world_animation::AnimationSettings>>
animationSettings(const GameFiles& files, std::span<const kest::TypeLayout> layouts) {
    if (files.description().animators.empty()) {
        return std::optional<world_animation::AnimationSettings>{};
    }
    world_animation::AnimationSettings settings;
    for (const GameAnimator& animator : files.description().animators) {
        RAWFRAME_TRY_ASSIGN(world_animation::AnimatorSettings made, animatorSettings(files, layouts, animator));
        settings.animators.push_back(std::move(made));
    }
    return std::optional{std::move(settings)};
}

} // namespace rawframe::world_kest
