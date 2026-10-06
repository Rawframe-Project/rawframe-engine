#pragma once

// A publisher's mirror over HTTP (D414): laid out as a library, as
// rawframe.install's mirror on disk is, at a base URL any web server or CDN
// can serve. What it serves is trusted for nothing: every manifest is
// verified against the pinned key set and every blob against its digest
// (SPEC-0020, SPEC-0021).

#include "rawframe/http/client.h"
#include "rawframe/install/origin.h"
#include "rawframe/result/result.h"

#include <memory>
#include <string_view>

namespace rawframe::install_tool {

/// Whether `text` names an origin over HTTP rather than a directory.
[[nodiscard]] bool overHttp(std::string_view text) noexcept;

/// The mirror at `base`, an http or https URL. Refuses a URL out of form
/// and authorities that cannot be read.
[[nodiscard]] result::Result<std::unique_ptr<install::Origin>> mirrorOver(std::string_view base,
                                                                          http::ClientSettings settings);

} // namespace rawframe::install_tool
