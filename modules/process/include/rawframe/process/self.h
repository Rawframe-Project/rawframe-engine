#pragma once

// This process's own executable file, which a program reads (the cook reads
// its own bytes, D236) or finds what was shipped beside it by (a launcher's
// configuration, D395).

#include <filesystem>

namespace rawframe::process {

/// The path of this program's executable file, or an empty path where the
/// system will not say. On Linux it is `/proc/self/exe`, which opens the
/// file itself; `std::filesystem::canonical` of it is where the file lies.
[[nodiscard]] std::filesystem::path ownExecutable();

} // namespace rawframe::process
