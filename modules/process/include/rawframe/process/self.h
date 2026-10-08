#pragma once

// This process's own executable file, which a program reads (the cook reads
// its own bytes, D236) or finds what was shipped beside it by (a launcher's
// configuration, D395).

#include <filesystem>
#include <string_view>

namespace rawframe::process {

/// The path of this program's executable file, or an empty path where the
/// system will not say. On Linux it is `/proc/self/exe`, which opens the
/// file itself; `std::filesystem::canonical` of it is where the file lies.
[[nodiscard]] std::filesystem::path ownExecutable();

/// The program `name` beside this one, where an export puts a toolchain's
/// programs together (the cook beside the author tool and Studio, D502),
/// with the system's ending for one (`.exe` on Windows); an empty path
/// where there is none.
[[nodiscard]] std::filesystem::path besideSelf(std::string_view name);

} // namespace rawframe::process
