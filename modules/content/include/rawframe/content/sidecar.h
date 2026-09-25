#pragma once

// A source's sidecar (ADR-0024): `<source file>.rfmeta` beside it, naming
// the resource the source becomes and the importer that makes it. The cook
// reads every sidecar; whatever resolves an authored name to a resource
// (a game's scene line, a scene's instance in development) reads the
// name's sidecar the same way.

#include "rawframe/content/identity.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <optional>
#include <string>
#include <string_view>

namespace rawframe::content {

/// The suffix of a source's sidecar.
inline constexpr std::string_view kSidecarSuffix = ".rfmeta";

/// A sidecar as written: the resource its source becomes, the importer that
/// makes it, and the settings it gives, if any.
struct Sidecar {
    ResourceId id;
    std::string importer;
    std::optional<document::Value> settings;
};

/// Reads a sidecar: canonical JSON `{schema: 1, resourceId, importer,
/// settings?}` with a resource identity other than nought. Refused
/// (`sidecar_invalid`, or the document's own error) otherwise.
[[nodiscard]] result::Result<Sidecar> readSidecar(std::string_view text);

} // namespace rawframe::content
