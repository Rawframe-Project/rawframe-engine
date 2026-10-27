#pragma once

// The core parameters of a Surface: each one's name, shape,
// range, default, and place, in name order (as a node's inputs are) and in
// the contract's order (as cooked bytes are).

#include "rawframe/material/material.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rawframe::material {

/// A core parameter: its name, a color's three channels or one number,
/// its range, its default, and where a Surface holds it.
struct Parameter {
    std::string_view name;
    std::size_t channels;
    double lowest;
    double highest;
    double initial;
    float* (*place)(Surface&);
};

/// In name order, as a node's inputs are.
extern const std::array<Parameter, 10> kParameters;
/// In the contract's order, as cooked bytes are.
extern const std::array<Parameter, 10> kContractOrder;

/// The parameter named `name`; none for another name.
[[nodiscard]] const Parameter* parameterNamed(std::string_view name);

/// What a feature group's input is: a number from nought to one, an angle
/// in radians, a normal, a distance in meters above nought, a color whose
/// default is white, or a flag whose default is false.
enum class GroupKind : std::uint8_t {
    Unit,
    Angle,
    Normal,
    Distance,
    Color,
    Flag
};

/// A feature group's input: its name
/// and kind; a number nought by default, a normal connected or left out
/// for the mesh's own. The group is active where any of its inputs is
/// given, which only a program material carries; the blob holds none.
struct GroupInput {
    std::string_view name;
    GroupKind kind;
};

/// The coat's (`KHR_materials_clearcoat`'s), the anisotropy's
/// (`KHR_materials_anisotropy`'s), and the transmission's
/// (`KHR_materials_transmission`'s and `KHR_materials_volume`'s), in
/// name order.
inline constexpr std::array<GroupInput, 10> kGroupInputs = {{{"coat_normal", GroupKind::Normal},
                                                             {"coat_roughness", GroupKind::Unit},
                                                             {"coat_weight", GroupKind::Unit},
                                                             {"geometry_thickness", GroupKind::Distance},
                                                             {"geometry_thin_walled", GroupKind::Flag},
                                                             {"specular_anisotropy_rotation", GroupKind::Angle},
                                                             {"specular_roughness_anisotropy", GroupKind::Unit},
                                                             {"transmission_color", GroupKind::Color},
                                                             {"transmission_depth", GroupKind::Distance},
                                                             {"transmission_weight", GroupKind::Unit}}};

/// The group input named `name`; none for another name.
[[nodiscard]] const GroupInput* groupInputNamed(std::string_view name);

} // namespace rawframe::material
