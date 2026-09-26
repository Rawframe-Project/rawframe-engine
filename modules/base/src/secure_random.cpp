#include "rawframe/base/secure_random.h"

#if defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#elif defined(__wasi__)
#include <algorithm>
#include <unistd.h>
#elif defined(_WIN32)
#include <algorithm>
#include <limits>
// clang-format off: windows.h first, bcrypt.h needs its types.
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#endif

namespace rawframe::base {

bool fillSecureRandom(std::span<std::byte> into) noexcept {
#if defined(__linux__)
    std::size_t filled = 0;
    while (filled < into.size()) {
        // getrandom may return fewer bytes than asked for, or be interrupted.
        const ssize_t kGot = ::getrandom(into.data() + filled, into.size() - filled, 0);
        if (kGot < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        filled += static_cast<std::size_t>(kGot);
    }
    return true;
#elif defined(__APPLE__)
    ::arc4random_buf(into.data(), into.size());
    return true;
#elif defined(__wasi__)
    // The host's random_get, at most 256 bytes a call.
    for (std::size_t filled = 0; filled < into.size(); filled += 256) {
        if (::getentropy(into.data() + filled, std::min<std::size_t>(256, into.size() - filled)) != 0) {
            return false;
        }
    }
    return true;
#elif defined(_WIN32)
    // The system's preferred generator, at most a ULONG's worth a call.
    constexpr std::size_t kMost = std::numeric_limits<ULONG>::max();
    for (std::size_t filled = 0; filled < into.size(); filled += kMost) {
        const auto kPart = static_cast<ULONG>(std::min(kMost, into.size() - filled));
        if (!BCRYPT_SUCCESS(::BCryptGenRandom(
                nullptr, reinterpret_cast<PUCHAR>(into.data() + filled), kPart, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
            return false;
        }
    }
    return true;
#else
    static_cast<void>(into);
    return false;
#endif
}

} // namespace rawframe::base
