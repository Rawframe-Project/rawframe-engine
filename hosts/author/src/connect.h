#pragma once

// `rawframe-author connect` (D408): a client of a running Runtime's tooling
// endpoint, over authoring_session's link.

namespace rawframe::author {

/// `rawframe-author connect`: opens a link, writes the welcome, then sends
/// each line of standard input as a record and writes each reply, a line
/// for a line, until the input ends (when it says `tooling.end`) or the
/// endpoint closes. 0 when every reply was an answer.
int connect(const char* endpoint, const char* pinFile, const char* tokenFile);

} // namespace rawframe::author
