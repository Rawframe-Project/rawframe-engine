#pragma once

// Authored documents (SPEC-0028's authored-document canonicalization
// profile): JSON read strictly, and written the one way every Rawframe
// authored document is written, so the same meaning is always the same bytes.

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::document {

/// One JSON value. An object keeps its members in the order they were read
/// or made: a typed record's order is its schema's, an open map's is sorted
/// by its reader. A number keeps its text, so an integer too wide for any
/// C++ type still round-trips; `integer` and `real` read it.
class Value {
public:
    enum class Kind : std::uint8_t {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Value() = default;

    [[nodiscard]] static Value boolean(bool truth);
    [[nodiscard]] static Value integer(std::int64_t number);
    /// A finite double; not a number or an infinity is written as null.
    [[nodiscard]] static Value real(double number);
    [[nodiscard]] static Value string(std::string text);
    [[nodiscard]] static Value array(std::vector<Value> items = {});
    [[nodiscard]] static Value object();
    /// A number from its JSON text, which the caller has checked.
    [[nodiscard]] static Value numberText(std::string text);

    [[nodiscard]] Kind kind() const noexcept {
        return kind_;
    }
    [[nodiscard]] bool isNull() const noexcept {
        return kind_ == Kind::Null;
    }

    [[nodiscard]] std::optional<bool> truth() const noexcept;
    /// A number written as an integer (no fraction or exponent) that fits.
    [[nodiscard]] std::optional<std::int64_t> integer() const noexcept;
    /// Any number, as the nearest double.
    [[nodiscard]] std::optional<double> real() const noexcept;
    /// A string's text, or a number's.
    [[nodiscard]] const std::string* text() const noexcept;

    /// An array's items or an object's member values, in order.
    [[nodiscard]] std::span<const Value> items() const noexcept {
        return items_;
    }
    /// An object's member names, beside `items`.
    [[nodiscard]] std::span<const std::string> names() const noexcept {
        return names_;
    }
    /// An object's member of that name, if it has one.
    [[nodiscard]] const Value* find(std::string_view name) const noexcept;

    /// Appends to an array.
    void push(Value item);
    /// Appends a member to an object; the caller keeps names unique.
    void add(std::string name, Value value);

private:
    Kind kind_ = Kind::Null;
    bool truth_ = false;
    std::string text_;
    std::vector<Value> items_;
    std::vector<std::string> names_;
};

/// How much a reader takes. Past either is `TooLarge`, never a truncation.
struct ReadLimits {
    std::size_t maximumBytes = std::size_t{16} * 1024 * 1024;
    std::size_t maximumDepth = 64;
};

/// Strict JSON (RFC 8259) under the profile: UTF-8 without a byte order
/// mark, no duplicate keys in an object, no number outside a double's range.
/// Any whitespace JSON allows is read.
[[nodiscard]] result::Result<Value> parse(std::string_view text, const ReadLimits& limits = {});

/// The profile's bytes: two spaces of indentation, one member or item a line,
/// one space after a colon, empty objects and arrays inline, strings escaped
/// only where JSON requires (`\"`, `\\`, `\b`, `\f`, `\n`, `\r`, `\t`, and
/// lowercase `\u00xx` for other control characters), numbers written as the
/// shortest text that reads back to the same double (integers as they are),
/// and one line feed at the end.
[[nodiscard]] std::string write(const Value& value);

/// `parse`, refusing (`NotCanonical`, at the first line that differs) text
/// that `write` would not have produced byte for byte.
[[nodiscard]] result::Result<Value> parseCanonical(std::string_view text, const ReadLimits& limits = {});

/// A canonical record's bytes (SPEC-0017, for records whose identity is a
/// digest of them): RFC 8785 JCS of one object, members sorted by name at
/// every depth, no whitespace and no final line feed, strings escaped as
/// `write` escapes them. Stricter than JCS: member names are printable
/// ASCII, and numbers are integers within +/-(2^53 - 1). Refuses
/// (`Invalid`) anything else, and two members of one name.
[[nodiscard]] result::Result<std::string> writeCanonicalRecord(const Value& value);

/// `parse`, refusing (`NotCanonical`) text that `writeCanonicalRecord` would
/// not have produced byte for byte, and what it refuses to write.
[[nodiscard]] result::Result<Value> parseCanonicalRecord(std::string_view text, const ReadLimits& limits = {});

} // namespace rawframe::document
