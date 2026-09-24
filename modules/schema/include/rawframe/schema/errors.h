#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::schema {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kSchemaDomain{base::parseBits128Hex("8f382065edb27136be159d3c8142cde6").value};

/// Codes within kSchemaDomain.
enum class SchemaError : std::uint32_t {
    UnknownComponent = 1,
    InvalidComponentId = 2,
    InvalidComponentName = 3,
    DuplicateComponentId = 4,
    DuplicateComponentName = 5,
    MissingOperations = 6,
};

[[nodiscard]] constexpr result::ErrorCode code(SchemaError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::schema
