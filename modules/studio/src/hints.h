#pragma once

// A field's typed control: what the game and the engine tell its
// authors of it, as `authoring.describe` gives it, made into what
// the Inspector shows and back into what an author types.

#include "rawframe/document/json.h"
#include "records.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rawframe::studio {

/// `field`'s hints and cases, as a described field gives them, into `into`.
void readHints(const document::Value& field, Catalog::Field& into);

/// A value's text as the Inspector shows it: a labelled number by its
/// label, a color as `#RRGGBBAA`, else as it is.
[[nodiscard]] std::string shownAs(const Catalog::Field& type, std::string_view text);

/// What an author typed as the field's value text: a label's number, a
/// color's `#RRGGBBAA` or `#RRGGBB` (opaque) as its number, else as typed.
[[nodiscard]] std::string typedAs(const Catalog::Field& type, std::string_view typed);

/// Typed text as a document holds it: nothing for none, JSON as it reads,
/// anything else as text, so a color is typed bare (a style sheet's part,
/// a UI node's property, a material's parameter, a data document's value).
[[nodiscard]] document::Value documentedValue(const std::string& text);

/// What is said beside the field: its unit and its range, as `m, 4 to 100`,
/// or nothing.
[[nodiscard]] std::string hintText(const Catalog::Field& type);

/// Whether the number `text` is outside the field's range, which an author
/// is told and not stopped by.
[[nodiscard]] bool pastRange(const Catalog::Field& type, std::string_view text);

/// The values the field is chosen from, as their names: a case field's
/// cases, a labelled number's labels, or true and false; none for a field
/// typed into.
[[nodiscard]] std::vector<std::string> choicesOf(const Catalog::Field& type);

/// A field's name as words, its first capital ("fixedRotation" reads
/// "Fixed rotation"), and a component's by its own last part, a pose the
/// entity's transform.
[[nodiscard]] std::string prettyField(std::string_view name);
[[nodiscard]] std::string prettyComponent(std::string_view name);

/// A turn as the Inspector shows it: degrees about X, Y, and Z,
/// applied Z first, then X, then Y (yaw over pitch over roll), from a unit
/// quaternion (x, y, z, w), and back.
[[nodiscard]] std::array<double, 3> degreesOf(const std::array<double, 4>& quaternion) noexcept;
[[nodiscard]] std::array<double, 4> quaternionOf(const std::array<double, 3>& degrees) noexcept;

/// The game's assets an asset field is chosen from: none, then each
/// of its kind by name; nothing for a field that holds no asset.
[[nodiscard]] std::vector<std::string> assetChoices(const Catalog::Field& type, std::span<const Asset> assets);

/// A color's number as its four bytes, red first, for a swatch; none for
/// text that is not one.
[[nodiscard]] std::optional<std::uint32_t> colorOf(std::string_view text);

} // namespace rawframe::studio
