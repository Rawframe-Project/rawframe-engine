#pragma once

// How an engine component is laid out, field by field, for a script that
// must declare the same struct: each module's table of its components is
// checked against the script's declarations.

#include "rawframe/schema/stable_id.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace rawframe::schema {

enum class FieldType : std::uint8_t {
    U8,
    U32,
    U64,
    Bool,
    F32,
    F64,
};

struct ComponentField {
    std::string_view name;
    std::size_t offset = 0;
    FieldType type = FieldType::U8;
};

/// One engine component as a script must declare it.
struct ComponentLayout {
    schema::ComponentTypeId id;
    std::string_view name;
    /// The type's name in the script.
    std::string_view scriptType;
    std::size_t size = 0;
    std::size_t alignment = 0;
    std::span<const ComponentField> fields;
};

} // namespace rawframe::schema
