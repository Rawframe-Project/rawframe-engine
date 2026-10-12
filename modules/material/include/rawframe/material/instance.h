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
/// eight `overrides`, in that order. Refuses (`Invalid`) anything else.
[[nodiscard]] result::Result<InstanceDocument> readInstance(std::string_view text);

/// The parameters an instance's overrides make of its base's: each names
/// one of the base's interface inputs, is of its type, and differs from its
/// default, which an override equal to it would only repeat (SPEC-0028).
/// Refuses (`Invalid`) any other, and a base `validateSurface` refuses.
[[nodiscard]] result::Result<std::array<float, kParameterFloats>> instanceParameters(const graph::Document& base,
                                                                                     const document::Value& overrides);

/// A cooked instance: its base's identity and its parameters.
struct MaterialInstance {
    base::Bits128 base;
    std::array<float, kParameterFloats> parameters{};

    friend bool operator==(const MaterialInstance&, const MaterialInstance&) = default;
};

/// A cooked instance's bytes: `RFMI`, format 1, its base's identity (its
/// high word, then its low, eight bytes each), then its parameters.
/// Little-endian.
[[nodiscard]] std::vector<std::byte> encodeInstance(const MaterialInstance& made);
/// Refuses (`Invalid`) bytes `encodeInstance` would not write, a base of
/// nought, and a parameter not finite.
[[nodiscard]] result::Result<MaterialInstance> decodeInstance(std::span<const std::byte> bytes);

} // namespace rawframe::material
