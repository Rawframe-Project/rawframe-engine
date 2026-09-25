// glTF animation import: a skin's joints become a skeleton, parents first,
// with the placement above its root folded in; each animation a clip whose
// keys play in the skeleton's frame, looped or held; channels that are not
// on joints skipped; and rigs a skeleton cannot hold refused.

#include "rawframe/animation/errors.h"
#include "rawframe/animation/sample.h"
#include "rawframe/animation_import/import.h"
#include "rawframe/test/test.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::animation_import;
using animation::AnimationError;

namespace {

bool refusedWith(const auto& outcome, AnimationError error) {
    return !outcome.has_value() && outcome.error().domain() == animation::kAnimationDomain &&
           outcome.error().code() == animation::code(error);
}

bool near(double a, double b) {
    return std::abs(a - b) < 1e-6;
}

void putFloats(std::vector<std::byte>& out, std::initializer_list<float> values) {
    for (const float kValue : values) {
        const auto kBits = std::bit_cast<std::uint32_t>(kValue);
        for (std::uint32_t shift = 0; shift < 32; shift += 8) {
            out.push_back(static_cast<std::byte>((kBits >> shift) & 0xFFU));
        }
    }
}

std::string base64(std::span<const std::byte> bytes) {
    constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        std::uint32_t group = std::to_integer<std::uint32_t>(bytes[i]) << 16U;
        if (i + 1 < bytes.size()) {
            group |= std::to_integer<std::uint32_t>(bytes[i + 1]) << 8U;
        }
        if (i + 2 < bytes.size()) {
            group |= std::to_integer<std::uint32_t>(bytes[i + 2]);
        }
        out.push_back(kAlphabet[(group >> 18U) & 63U]);
        out.push_back(kAlphabet[(group >> 12U) & 63U]);
        out.push_back(i + 1 < bytes.size() ? kAlphabet[(group >> 6U) & 63U] : '=');
        out.push_back(i + 2 < bytes.size() ? kAlphabet[group & 63U] : '=');
    }
    return out;
}

/// An arm: an armature node, turned a quarter about y and scaled by two, over
/// a two-joint skin, the shoulder and the elbow a meter out along x from it.
/// One animation raises the shoulder a meter and moves it a meter along x
/// over a second (the key at the end repeating the first, as a loop does,
/// or `stride` meters on along x for a walk), turns the elbow a quarter
/// about z, and moves the armature itself, which is no joint. Buffer: times
/// (0, 0.5, 1), shoulder translations, elbow rotations (x, y, z, w).
std::string arm(std::string_view skins = "", std::string_view required = "", float stride = 0.0F) {
    std::vector<std::byte> buffer;
    putFloats(buffer, {0.0F, 0.5F, 1.0F});
    putFloats(buffer, {0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, stride, 0.0F, 0.0F});
    const float kHalf = std::sqrt(0.5F);
    putFloats(buffer, {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, kHalf, kHalf, 0.0F, 0.0F, 0.0F, 1.0F});
    const std::string kSkins = skins.empty() ? std::string{R"("skins": [{"joints": [2, 1]}],)"} : std::string{skins};
    return std::string{R"({"asset": {"version": "2.0"},)"} + std::string{required} +
           R"("scene": 0, "scenes": [{"nodes": [0]}],
  "nodes": [
    {"name": "Armature", "rotation": [0, 0.7071067811865476, 0, 0.7071067811865476], "scale": [2, 2, 2], "children": [1]},
    {"name": "shoulder", "children": [2]},
    {"name": "elbow", "translation": [1, 0, 0]}
  ],
  )" + kSkins +
           R"(
  "buffers": [{"byteLength": 96, "uri": "data:application/octet-stream;base64,)" +
           base64(buffer) + R"("}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 12},
    {"buffer": 0, "byteOffset": 12, "byteLength": 36},
    {"buffer": 0, "byteOffset": 48, "byteLength": 48}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0], "max": [1]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4"}
  ],
  "animations": [{"name": "raise", "samplers": [
      {"input": 0, "output": 1},
      {"input": 0, "output": 2, "interpolation": "STEP"}
    ], "channels": [
      {"sampler": 0, "target": {"node": 1, "path": "translation"}},
      {"sampler": 1, "target": {"node": 2, "path": "rotation"}},
      {"sampler": 0, "target": {"node": 0, "path": "translation"}}
    ]}]
})";
}

result::Result<ImportedAnimation>
imported(const std::string& text, bool loop = false, std::optional<animation::AdditiveBasis> additive = std::nullopt) {
    const ReadFile kNothing = [](std::string_view) -> result::Result<std::span<const std::byte>> {
        return std::unexpected<result::Error>{
            result::fail(result::ErrorClass::NotFound, animation::kAnimationDomain, {}, "no file").error()};
    };
    return importGltf(std::as_bytes(std::span{text.data(), text.size()}),
                      kNothing,
                      ImportSettings{.skeleton = base::Bits128{0, 0x5c}, .loop = loop, .additive = additive});
}

} // namespace

RAWFRAME_TEST(ASkinBecomesASkeletonWithItsPlacementFolded) {
    const auto kImported = imported(arm());
    RAWFRAME_EXPECT(kImported.has_value());
    if (!kImported.has_value()) {
        return;
    }
    const animation::Skeleton& skeleton = kImported->skeleton;
    RAWFRAME_EXPECT(skeleton.bones.size() == 2 && skeleton.bones[0].name == "shoulder" &&
                    skeleton.bones[1].name == "elbow" && !skeleton.bones[0].parent.has_value() &&
                    skeleton.bones[1].parent == animation::BoneIndex{0});
    // Targets come from name paths.
    const std::array<std::string_view, 2> kElbowPath = {"shoulder", "elbow"};
    RAWFRAME_EXPECT(skeleton.bones[1].target == animation::targetIdOf(kElbowPath));
    // The armature's quarter turn and doubling are the shoulder's now; the
    // elbow stays as authored, relative to it.
    const animation::Transform& root = skeleton.bones[0].bind;
    RAWFRAME_EXPECT(near(root.rotation[1], std::sqrt(0.5)) && near(root.rotation[3], std::sqrt(0.5)) &&
                    near(root.scale[0], 2.0) && near(root.translation[0], 0.0));
    RAWFRAME_EXPECT(near(skeleton.bones[1].bind.translation[0], 1.0));
    RAWFRAME_EXPECT(animation::writeSkeleton(skeleton).has_value());
}

RAWFRAME_TEST(AnAnimationBecomesAClipInTheSkeletonsFrame) {
    const auto kImported = imported(arm());
    RAWFRAME_EXPECT(kImported.has_value() && kImported->clips.size() == 1);
    if (!kImported.has_value() || kImported->clips.size() != 1) {
        return;
    }
    const animation::Clip& clip = kImported->clips[0].clip;
    RAWFRAME_EXPECT(kImported->clips[0].name == "raise" && clip.skeleton == base::Bits128(0, 0x5c) &&
                    clip.duration == 1.0 && clip.loop == animation::Loop::Clamp && clip.tracks.size() == 2);
    // The armature's own channel is skipped.
    RAWFRAME_EXPECT(kImported->channelsSkipped == 1 && kImported->rotationsFlattened == 0);
    // The shoulder's rise and reach, a meter along the armature's y and x,
    // are two meters up and two back along -z in the skeleton's frame: the
    // armature's doubling, and its quarter turn about y.
    const animation::Track& rise = clip.tracks[0];
    RAWFRAME_EXPECT(rise.channel == animation::Channel::Translation && rise.keys.size() == 3 &&
                    near(rise.keys[1].value[0], 0.0) && near(rise.keys[1].value[1], 2.0) &&
                    near(rise.keys[1].value[2], -2.0));
    const animation::Track& turn = clip.tracks[1];
    RAWFRAME_EXPECT(turn.channel == animation::Channel::Rotation &&
                    turn.keys[1].interpolation == animation::Interpolation::Step &&
                    near(turn.keys[1].value[2], std::sqrt(0.5)));
    RAWFRAME_EXPECT(animation::writeClip(clip).has_value());
    // Looped, the key at the end is the start's and goes.
    const auto kLooped = imported(arm(), true);
    RAWFRAME_EXPECT(kLooped.has_value() && kLooped->clips[0].clip.loop == animation::Loop::Loop &&
                    kLooped->clips[0].clip.tracks[0].keys.size() == 2 &&
                    !kLooped->clips[0].clip.tracks[0].drift.has_value());
    // A root that ends a meter on drifts that meter a period, in the
    // skeleton's frame: two back along -z.
    const auto kWalked = imported(arm("", "", 1.0F), true);
    RAWFRAME_EXPECT(kWalked.has_value());
    if (kWalked.has_value()) {
        const animation::Track& walked = kWalked->clips[0].clip.tracks[0];
        RAWFRAME_EXPECT(walked.keys.size() == 2 && walked.drift.has_value() && near((*walked.drift)[0], 0.0) &&
                        near((*walked.drift)[1], 0.0) && near((*walked.drift)[2], -2.0));
        RAWFRAME_EXPECT(!kWalked->clips[0].clip.tracks[1].drift.has_value());
        RAWFRAME_EXPECT(animation::writeClip(kWalked->clips[0].clip).has_value());
    }
}

RAWFRAME_TEST(AnAdditiveImportHoldsDifferences) {
    // From the first frame: the shoulder starts at no difference and rises
    // by what it rose, two up and two along -z, however the armature placed
    // it; the elbow's first turn is none, so its turns stay.
    const auto kFirst = imported(arm(), false, animation::AdditiveBasis::FirstFrame);
    RAWFRAME_EXPECT(kFirst.has_value());
    if (kFirst.has_value()) {
        const animation::Clip& clip = kFirst->clips[0].clip;
        RAWFRAME_EXPECT(clip.additive == animation::AdditiveBasis::FirstFrame);
        RAWFRAME_EXPECT(clip.tracks[0].keys[0].value == (std::array<double, 4>{}));
        RAWFRAME_EXPECT(near(clip.tracks[0].keys[1].value[1], 2.0) && near(clip.tracks[0].keys[1].value[2], -2.0));
        RAWFRAME_EXPECT(near(clip.tracks[1].keys[1].value[2], std::sqrt(0.5)));
        RAWFRAME_EXPECT(animation::writeClip(clip).has_value());
    }
    // From the bind, looped, a walk keeps no drift: differences carry no
    // root motion.
    const auto kBind = imported(arm("", "", 1.0F), true, animation::AdditiveBasis::Bind);
    RAWFRAME_EXPECT(kBind.has_value() && kBind->clips[0].clip.additive == animation::AdditiveBasis::Bind &&
                    !kBind->clips[0].clip.tracks[0].drift.has_value());
}

RAWFRAME_TEST(RigsASkeletonCannotHoldAreRefused) {
    // No skin; two roots; a required extension unknown; not glTF at all.
    RAWFRAME_EXPECT(refusedWith(imported(arm(R"("skins": [],)")), AnimationError::BadSource));
    RAWFRAME_EXPECT(refusedWith(imported(arm(R"("skins": [{"joints": [2, 0]}],)")), AnimationError::BadSource));
    RAWFRAME_EXPECT(refusedWith(
        imported(arm(
            "", R"("extensionsRequired": ["KHR_animation_pointer"], "extensionsUsed": ["KHR_animation_pointer"],)")),
        AnimationError::UnsupportedExtension));
    RAWFRAME_EXPECT(refusedWith(imported("{\"asset\": 1"), AnimationError::BadSource));
}
