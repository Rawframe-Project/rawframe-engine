#pragma once

// SPEC-0026's core parameters of a Surface (D301): each one's name, shape,
// range, default, and place, in name order (as a node's inputs are) and in
// the contract's order (as cooked bytes are).

#include "rawframe/material/material.h"

#include <array>
#include <cstddef>
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
/// In the contract's order (SPEC-0026's table), as cooked bytes are.
extern const std::array<Parameter, 10> kContractOrder;

/// The parameter named `name`; none for another name.
[[nodiscard]] const Parameter* parameterNamed(std::string_view name);

} // namespace rawframe::material
