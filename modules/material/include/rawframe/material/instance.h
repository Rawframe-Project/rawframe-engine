#pragma once

// A material instance (ADR-0027's material instances, SPEC-0028's instance
// documents, D709): a base surface material and overrides of its interface
// inputs, never of its nodes. It cooks to its base's identity and the
// parameters its overrides make, so it draws with its base's program and
// differs only in its blob.
//
//   {"formatVersion": 1, "kind": "surface", "base": "stone.rfmaterial",
//    "overrides": {"tint": [0.5, 0.4, 0.3]}}

#include "rawframe/base/bits128.h"
#include "rawframe/document/json.h"
#include "rawframe/graph/graph.h"
#include "rawframe/material/material.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::material {

/// The cooked instance's representation, of the material type.
inline constexpr std::string_view kInstanceRepresentation = "rawframe.material.instance";

/// An instance document: its base's source, relative to the instance's
/// own, and its overrides by interface input.
struct InstanceDocument {
    std::string base;
    document::Value overrides;
};

/// Whether a surface material's source is an instance: an object naming a
/// base. What reads it is `readInstance`; anything else is a graph's.
[[nodiscard]] bool namesBase(std::string_view text);

/// Reads an instance in its canonical form: `formatVersion` 1, `kind`
/// `surface`, `base` a path of `/`-separated names, none `..`, and one to
/// sixteen `overrides`, in that order. Refuses (`Invalid`) anything else.
[[nodiscard]] result::Result<InstanceDocument> readInstance(std::string_view text);

/// What an instance's overrides make of its base: its parameters, and the
/// texture each of its base's program's texture slots samples where an
/// override of a texture input changes it (D710), nought where not.
struct InstanceValues {
    std::array<float, kParameterFloats> parameters{};
    std::array<std::uint64_t, 4> textures{};
};

/// An instance's overrides made into its base's values: each names one of
/// the base's interface inputs, is of its type (a texture input's the
/// game's texture by 16 lowercase hex digits), and differs from its
/// default, which an override equal to it would only repeat (SPEC-0028);
/// or names one of its static switches, which `switchedVariant` takes.
/// Refuses (`Invalid`) any other, and a base `validateSurface` refuses; a
/// texture override, a base `generateSlang` refuses.
[[nodiscard]] result::Result<InstanceValues> instanceValues(const graph::Document& base,
                                                            const document::Value& overrides);

/// The variant an instance's switch overrides make of its base (ADR-0027,
/// D719): the base with each overridden switch's default the override's,
/// which compiles to a program of its own; none where the instance
/// overrides no switch, and it shares its base's program. Each switch
/// override is a bool that differs from the switch's default. Refuses
/// (`Invalid`) any other, and a base `validateSurface` refuses.
[[nodiscard]] result::Result<std::optional<graph::Document>> switchedVariant(const graph::Document& base,
                                                                             const document::Value& overrides);

/// A cooked instance: its base's identity and its values.
struct MaterialInstance {
    base::Bits128 base;
    std::array<float, kParameterFloats> parameters{};
    std::array<std::uint64_t, 4> textures{};

    friend bool operator==(const MaterialInstance&, const MaterialInstance&) = default;
};

/// A cooked instance's bytes: `RFMI`, format 2 (D709, D710), its base's
/// identity (its high word, then its low, eight bytes each), its
/// parameters, then its four texture slots' identities (eight bytes each,
/// nought for the base's). Little-endian.
[[nodiscard]] std::vector<std::byte> encodeInstance(const MaterialInstance& made);
/// Refuses (`Invalid`) bytes `encodeInstance` would not write, a base of
/// nought, and a parameter not finite.
[[nodiscard]] result::Result<MaterialInstance> decodeInstance(std::span<const std::byte> bytes);

} // namespace rawframe::material
