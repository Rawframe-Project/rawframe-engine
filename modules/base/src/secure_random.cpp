#include "rawframe/base/secure_random.h"

#if defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#elif defined(__APPLE__)
#include <stdlib.h>
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
#else
    static_cast<void>(into);
    return false;
#endif
}

} // namespace rawframe::base
