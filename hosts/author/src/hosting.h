#pragma once

// `rawframe-author host` and `rawframe-author join` (ADR-0085, D881): a
// shared authoring session and a collaborator of one, over
// authoring_session's host and link.

#include "rawframe/authoring_session/host.h"

#include <filesystem>

namespace rawframe::author {

/// `rawframe-author host`: the game's session hosted as `settings` say
/// until this process is asked to stop. 0 when it stopped as asked, 1
/// when it could not host.
int hostGame(const std::filesystem::path& game,
             const std::filesystem::path& root,
             const std::filesystem::path& cook,
             const authoring_session::HostSettings& settings);

/// `rawframe-author join`: a link to a host, its welcome written, then
/// each line of standard input sent as a record and each reply written,
/// what the host said unasked written before it, until the input ends or
/// the host closes. 0 when every reply was an answer.
int join(const char* endpoint, const char* pinFile, const char* tokenFile);

} // namespace rawframe::author
