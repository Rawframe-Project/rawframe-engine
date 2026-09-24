#include "rawframe/execution/time.h"

#include <chrono>

namespace rawframe::execution {

MonotonicInstant SteadyClock::now() const noexcept {
    const auto kSinceEpoch = std::chrono::steady_clock::now().time_since_epoch();
    return MonotonicInstant{std::chrono::duration_cast<std::chrono::nanoseconds>(kSinceEpoch).count()};
}

} // namespace rawframe::execution
