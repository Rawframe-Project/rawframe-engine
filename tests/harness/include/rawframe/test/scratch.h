#pragma once

// A directory of this test process's own under the system's temporary
// directory, for tests that write files: named by the test and the process,
// so tests running at once never share one. Where there are files only.

#include <filesystem>
#include <string_view>

namespace rawframe::test {

/// `<temporary>/rawframe-<name>-<process id>`, not created.
[[nodiscard]] std::filesystem::path scratchDirectory(std::string_view name);

} // namespace rawframe::test
