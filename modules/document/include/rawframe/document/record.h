#pragma once

// Typed records in authored documents: an object whose members its kind
// declares, in the declared order, none unknown, and none written at its
// declared default (SPEC-0028: a default-valued field is omitted, so the
// same meaning is the same bytes). Refusals name the field by its path in
// the document (`actions[2].bindings[0].deadzoneLower`).

#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rawframe::document {

/// A typed-document refusal (`DocumentError::Invalid`) at `path`.
[[nodiscard]] std::unexpected<result::Error> invalid(std::string_view path, std::string_view why);

/// The profile refusal (`DocumentError::NotCanonical`) at `path`.
[[nodiscard]] std::unexpected<result::Error> notCanonical(std::string_view path, std::string_view why);

class Record {
public:
    /// `value` read as a record with `fields`, which lists every member the
    /// kind declares in the order it writes them. Refuses anything but an
    /// object, a member not listed (`Invalid`), and members out of order
    /// (`NotCanonical`). `fields` and `value` outlive the record.
    [[nodiscard]] static result::Result<Record>
    of(const Value& value, std::span<const std::string_view> fields, std::string path);

    [[nodiscard]] const std::string& path() const noexcept {
        return path_;
    }
    /// The path of one of its fields.
    [[nodiscard]] std::string pathOf(std::string_view field) const;

    /// The member, which must be there and of that kind.
    [[nodiscard]] result::Result<const Value*> required(std::string_view field, Value::Kind kind) const;
    /// The member if it is there, which must then be of that kind.
    [[nodiscard]] result::Result<const Value*> optional(std::string_view field, Value::Kind kind) const;

    [[nodiscard]] result::Result<std::string_view> text(std::string_view field) const;
    [[nodiscard]] result::Result<std::optional<std::string_view>> optionalText(std::string_view field) const;
    [[nodiscard]] result::Result<std::int64_t> integer(std::string_view field) const;
    /// Absent is `fallback`; present at `fallback` is not canonical.
    [[nodiscard]] result::Result<std::int64_t> integer(std::string_view field, std::int64_t fallback) const;
    [[nodiscard]] result::Result<double> real(std::string_view field, double fallback) const;
    [[nodiscard]] result::Result<bool> truth(std::string_view field, bool fallback) const;

private:
    Record(const Value& value, std::string path) noexcept : value_(&value), path_(std::move(path)) {
    }

    const Value* value_;
    std::string path_;
};

} // namespace rawframe::document
