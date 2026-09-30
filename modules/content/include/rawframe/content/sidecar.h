#pragma once

// A source's sidecar (ADR-0024): `<source file>.rfmeta` beside it, naming
// the resource the source becomes and the importer that makes it. The cook
// reads every sidecar; whatever resolves an authored name to a resource
// (a game's scene line, a scene's instance in development) reads the
// name's sidecar the same way.

#include "rawframe/content/identity.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::content {

/// The suffix of a source's sidecar.
inline constexpr std::string_view kSidecarSuffix = ".rfmeta";

/// A sidecar as written: the resource its source becomes, the importer that
/// makes it, the settings it gives, if any, and its subasset map.
struct Sidecar {
    ResourceId id;
    std::string importer;
    std::optional<document::Value> settings;
    /// The resource each subasset the importer finds in the source becomes
    /// (ADR-0024, D314), by the key the importer gives it: its family, a
    /// slash, and a name the source gives it, as `material/Brass`. A key
    /// the source no longer has keeps its identity, never given to another.
    std::map<std::string, ResourceId, std::less<>> subassets;
};

/// Whether `key` is a subasset key: a family of lowercase letters, a
/// slash, and a name of printable characters.
[[nodiscard]] bool isSubassetKey(std::string_view key) noexcept;

/// Reads a sidecar: canonical JSON `{schema: 1, resourceId, importer,
/// settings?, subassets?}` with a resource identity other than nought, and
/// subassets keyed by subasset keys in order, each a distinct identity
/// other than nought and the source's own. Refused (`sidecar_invalid`, or
/// the document's own error) otherwise.
[[nodiscard]] result::Result<Sidecar> readSidecar(std::string_view text);

/// The canonical text of `sidecar`, which `readSidecar` reads back as it:
/// what tooling writes when it gives a source or a subasset an identity.
[[nodiscard]] std::string writeSidecar(const Sidecar& sidecar);

} // namespace rawframe::content
