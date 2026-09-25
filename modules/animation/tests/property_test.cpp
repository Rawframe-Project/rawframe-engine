// Property tracks (SPEC-0035, D139): a component's field animated by a
// clip, as a float weighed among the clips that animate it, or as a
// discrete value such as a flipbook's frame from the clip counting most.

#include "rawframe/animation/errors.h"
#include "rawframe/animation/instance.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::animation;

namespace {

bool refusedWith(const auto& outcome, AnimationError error) {
    return !outcome.has_value() && outcome.error().domain() == kAnimationDomain &&
           outcome.error().code() == code(error);
}

constexpr base::Bits128 kSkeletonId{7, 7};
constexpr base::Bits128 kRoot{1, 1};
constexpr base::Bits128 kLampType{0x1c0ffee000004000ULL, 0x80000000000000a2ULL};
constexpr base::Bits128 kDimId{2, 1};
constexpr base::Bits128 kBrightId{2, 2};
constexpr base::Bits128 kStillId{2, 3};
constexpr std::uint64_t kLit = 0x6a00000000000001ULL;

PropertyBinding field(std::string name) {
    return PropertyBinding{.component = kLampType, .field = std::move(name)};
}

/// A lamp at `glow` showing flipbook frames `first` then `second` at half a
/// second; no bone.
Clip lamp(double glow, double first, double second) {
    return Clip{
        .duration = 1.0,
        .loop = Loop::Loop,
        .tracks = {Track{.channel = Channel::Float, .keys = {Key{.value = {glow, 0, 0, 0}}}, .property = field("glow")},
                   Track{.channel = Channel::Discrete,
                         .keys = {Key{.value = {first, 0, 0, 0}, .interpolation = Interpolation::Step},
                                  Key{.time = 0.5, .value = {second, 0, 0, 0}, .interpolation = Interpolation::Step}},
                         .property = field("frame")}}};
}

/// Dim and bright blended by `lit` (bright's weight, dim's being one), and
/// a still clip of the skeleton that animates no field.
Graph lamps() {
    return Graph{.parameters = {Parameter{.name = "lit", .id = kLit, .initial = {3.0, 0.0}}},
                 .nodes = {GraphNode{.id = 1, .node = ClipNode{.clip = kDimId}},
                           GraphNode{.id = 2, .node = ClipNode{.clip = kBrightId}},
                           GraphNode{.id = 3, .node = ClipNode{.clip = kStillId}},
                           GraphNode{.id = 4,
                                     .node = BlendNode{.inputs = {BlendInput{.name = "bright",
                                                                             .from = {.node = 2},
                                                                             .weight = ParameterRef{kLit}},
                                                                  BlendInput{.name = "dim", .from = {.node = 1}},
                                                                  BlendInput{.name = "still", .from = {.node = 3}}}}},
                           GraphNode{.id = 5, .node = OutputNode{.pose = {.node = 4}}}},
                 .presentation = {}};
}

Skeleton rig() {
    return Skeleton{.bones = {Bone{.target = kRoot, .name = "root", .parent = std::nullopt, .bind = {}}}};
}

result::Result<std::shared_ptr<const CompiledGraph>> compiled(const Clip& dim, const Clip& bright) {
    const std::vector<NamedClip> kClips{
        {kDimId, std::make_shared<const Clip>(dim)},
        {kBrightId, std::make_shared<const Clip>(bright)},
        {kStillId,
         std::make_shared<const Clip>(
             Clip{.skeleton = kSkeletonId,
                  .duration = 1.0,
                  .loop = Loop::Loop,
                  .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {}}}}}})}};
    return CompiledGraph::compile(lamps(), rig(), kSkeletonId, kClips);
}

} // namespace

RAWFRAME_TEST(APropertyTrackHasOneText) {
    const Clip kLamp = lamp(1.0, 3.0, 4.0);
    const auto kText = writeClip(kLamp);
    RAWFRAME_EXPECT(kText.has_value() && readClip(*kText) == kLamp && !kText->contains("skeleton"));
    RAWFRAME_EXPECT(kText.has_value() &&
                    kText->contains("\"component\": \"1c0ffee00000400080000000000000a2\",\n      \"field\": "
                                    "\"glow\",\n      \"channel\": \"float\""));
    // A discrete key eased, halved, or past a u32; a field twice; a field
    // named out of form; a drift; a property in an additive clip; and a
    // bone track on a property's channel.
    std::vector<Clip> wrong(8, kLamp);
    wrong[0].tracks[1].keys[0].interpolation = Interpolation::Linear;
    wrong[1].tracks[1].keys[0].value[0] = 2.5;
    wrong[2].tracks[1].keys[0].value[0] = 4294967296.0;
    wrong[3].tracks[1].property = field("glow");
    wrong[4].tracks[0].property = field("Glow");
    wrong[5].tracks[0].drift = std::array<double, 4>{1, 0, 0, 0};
    wrong[6].additive = AdditiveBasis::Bind;
    wrong[7].skeleton = kSkeletonId;
    wrong[7].tracks[0].property.reset();
    wrong[7].tracks[0].bone = kRoot;
    for (const Clip& kClip : wrong) {
        RAWFRAME_EXPECT(refusedWith(writeClip(kClip), AnimationError::ClipInvalid));
    }
}

RAWFRAME_TEST(PropertiesAreWeighedAmongTheClipsThatAnimateThem) {
    const auto kGraph = compiled(lamp(0.0, 1.0, 2.0), lamp(8.0, 5.0, 6.0));
    RAWFRAME_EXPECT(kGraph.has_value() && (*kGraph)->properties().size() == 2);
    if (!kGraph.has_value()) {
        return;
    }
    // In binding order: frame, then glow.
    RAWFRAME_EXPECT((*kGraph)->properties()[0].binding.field == "frame" &&
                    (*kGraph)->properties()[1].channel == Channel::Float);
    GraphInstance instance{*kGraph};
    std::vector<GraphEvent> events;
    RAWFRAME_EXPECT(instance.advance(0.0, events));
    PoseEvaluator evaluator;
    std::vector<std::optional<double>> values;
    evaluator.evaluateProperties(instance, values);
    // Bright three fifths, dim one, still one: the glow weighed between the
    // two lamps alone, three quarters bright; the frame the brighter's.
    RAWFRAME_EXPECT(values.size() == 2 && values[1].has_value() && std::abs(*values[1] - 6.0) < 1e-12 &&
                    values[0] == 5.0);
    // Half a second on, the flipbook's next frame.
    RAWFRAME_EXPECT(instance.advance(0.5, events));
    evaluator.evaluateProperties(instance, values);
    RAWFRAME_EXPECT(values[0] == 6.0);
    // With the lamps weighed nothing, no clip counting anything animates
    // either field.
    GraphInstance dark{*kGraph};
    RAWFRAME_EXPECT(dark.set(ParameterIndex{0}, {0.0, 0.0}).has_value());
    RAWFRAME_EXPECT(dark.advance(0.0, events));
    evaluator.evaluateProperties(dark, values);
    RAWFRAME_EXPECT(values[0] == 1.0 && values[1] == 0.0);
}

RAWFRAME_TEST(AFieldIsFloatOrDiscreteNotBoth) {
    Clip other = lamp(8.0, 5.0, 6.0);
    other.tracks[0].channel = Channel::Discrete;
    other.tracks[0].keys[0].interpolation = Interpolation::Step;
    RAWFRAME_EXPECT(refusedWith(compiled(lamp(0.0, 1.0, 2.0), other), AnimationError::GraphInvalid));
}
