#pragma once

// Reading the source tree from a test, the same way natively and in the web
// build, where the engine has no file system (RAWFRAME_FILE_SYSTEM) but the
// test runner lends Node's: for tests that compare what the engine embeds or
// compiles with the files in the repository.

#include <string>
#include <string_view>
#include <vector>

namespace rawframe::test {

/// A file's bytes as text; empty when it does not read.
[[nodiscard]] std::string readFile(const std::string& path);

/// Every regular file under `directory` whose name ends in `suffix`, by its
/// path relative to `directory` with `/` between parts, in path order.
[[nodiscard]] std::vector<std::string> filesUnder(const std::string& directory, std::string_view suffix);

} // namespace rawframe::test
