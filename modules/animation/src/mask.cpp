#include "rawframe/animation/mask.h"

#include "rawframe/animation/errors.h"
#include "rawframe/document/json.h"
#include "text.h"

#include <algorithm>
#include <cmath>

namespace rawframe::animation {

namespace {

using document::Value;

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kAnimationDomain, code(AnimationError::MaskInvalid), why);
}

std::unexpected<result::Error> overLimit(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kAnimationDomain, code(AnimationError::OverLimit), why);
}

} // namespace

result::Status validate(const Mask& mask, const MaskLimits& limits) {
    if (mask.chains.size() > limits.maximumChains) {
        return overLimit("a mask has more chains than its limit");
    }
    if (mask.skeleton == base::Bits128{} || mask.chains.empty()) {
        return invalid("a mask names its skeleton and has a chain");
    }
    for (std::size_t at = 0; at < mask.chains.size(); ++at) {
        const MaskChain& chain = mask.chains[at];
        if (chain.root == base::Bits128{} || (at > 0 && !(mask.chains[at - 1].root < chain.root)) ||
            !std::isfinite(chain.weight) || !(chain.weight > 0.0) || chain.weight > 1.0) {
            return invalid("a mask's chains name their roots, once each, in order, weighing over nought to one");
        }
    }
    return {};
}

result::Result<std::string> writeMask(const Mask& mask, const MaskLimits& limits) {
    RAWFRAME_TRY(validate(mask, limits));
    Value chains = Value::array();
    for (const MaskChain& chain : mask.chains) {
        Value made = Value::object();
        made.add("root", Value::string(hexOf(chain.root)));
        made.add("descendants", Value::boolean(chain.descendants));
        made.add("weight", Value::real(chain.weight));
        chains.push(std::move(made));
    }
    Value made = Value::object();
    made.add("formatVersion", Value::integer(1));
    made.add("kind", Value::string("animation.mask"));
    made.add("skeleton", Value::string(hexOf(mask.skeleton)));
    made.add("chains", std::move(chains));
    return document::write(made);
}

result::Result<Mask> readMask(std::string_view text, const MaskLimits& limits) {
    auto parsed = document::parse(text);
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{std::move(parsed).error()};
    }
    const Value* kind = parsed->find("kind");
    const auto kSkeleton = bits128Of(parsed->find("skeleton"));
    if (!hasMembers(*parsed, {"formatVersion", "kind", "skeleton", "chains"}) || kind->text() == nullptr ||
        *kind->text() != "animation.mask" || parsed->find("formatVersion")->integer() != 1 || !kSkeleton.has_value() ||
        parsed->find("chains")->kind() != Value::Kind::Array) {
        return invalid("a mask is format version 1, kind animation.mask, its skeleton, and chains");
    }
    const Value& chains = *parsed->find("chains");
    if (chains.items().size() > limits.maximumChains) {
        return overLimit("a mask has more chains than its limit");
    }
    Mask mask{.skeleton = *kSkeleton, .chains = {}};
    for (const Value& each : chains.items()) {
        const auto kRoot = bits128Of(each.find("root"));
        const Value* descendants = each.find("descendants");
        const auto kWeight = numberOf(each.find("weight"));
        if (!hasMembers(each, {"root", "descendants", "weight"}) || !kRoot.has_value() ||
            !descendants->truth().has_value() || !kWeight.has_value()) {
            return invalid("a mask's chain is a root, whether its descendants are in it, and a weight");
        }
        mask.chains.push_back(MaskChain{.root = *kRoot, .descendants = *descendants->truth(), .weight = *kWeight});
    }
    RAWFRAME_TRY_ASSIGN(const std::string kWritten, writeMask(mask, limits));
    if (kWritten != text) {
        return invalid("a mask is not in its canonical form");
    }
    return mask;
}

result::Result<std::vector<double>> boneWeights(const Mask& mask, const Skeleton& skeleton, base::Bits128 skeletonId) {
    if (mask.skeleton != skeletonId) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kAnimationDomain,
                            code(AnimationError::BindingInvalid),
                            "a mask is of the skeleton it is used with");
    }
    // Each bone's chain, if a chain's root: the chain it roots.
    std::vector<const MaskChain*> rooted(skeleton.bones.size(), nullptr);
    for (const MaskChain& chain : mask.chains) {
        const auto kBone = skeleton.find(chain.root);
        if (!kBone.has_value()) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kAnimationDomain,
                                code(AnimationError::BindingInvalid),
                                "a mask's chain roots at a bone its skeleton has");
        }
        rooted[kBone->value] = &chain;
    }
    // Parents come first, so each bone inherits from a parent already done.
    std::vector<double> weights(skeleton.bones.size(), 0.0);
    std::vector<bool> reaches(skeleton.bones.size(), false);
    for (std::size_t bone = 0; bone < skeleton.bones.size(); ++bone) {
        if (rooted[bone] != nullptr) {
            weights[bone] = rooted[bone]->weight;
            reaches[bone] = rooted[bone]->descendants;
        } else if (const auto kParent = skeleton.bones[bone].parent; kParent.has_value() && reaches[kParent->value]) {
            weights[bone] = weights[kParent->value];
            reaches[bone] = true;
        }
    }
    return weights;
}

result::Result<std::vector<std::uint8_t>>
boneSubset(const Mask& mask, const Skeleton& skeleton, base::Bits128 skeletonId) {
    RAWFRAME_TRY_ASSIGN(const std::vector<double> kWeights, boneWeights(mask, skeleton, skeletonId));
    std::vector<std::uint8_t> subset(skeleton.bones.size(), 0);
    // Children come after parents, so walking back marks each bone's
    // parent once the bone itself is known.
    for (std::size_t bone = skeleton.bones.size(); bone-- > 0;) {
        if (kWeights[bone] > 0.0) {
            subset[bone] = 1;
        }
        if (subset[bone] != 0 && skeleton.bones[bone].parent.has_value()) {
            subset[skeleton.bones[bone].parent->value] = 1;
        }
    }
    return subset;
}

} // namespace rawframe::animation
