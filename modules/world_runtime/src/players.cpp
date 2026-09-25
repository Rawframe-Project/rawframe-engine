#include "rawframe/world_runtime/players.h"

#include "rawframe/base/sha256.h"

namespace rawframe::world_runtime {

std::optional<PlayerIdentity> playerIdentity(std::span<const std::byte> session) noexcept {
    if (session.empty()) {
        return std::nullopt;
    }
    base::Sha256 hash;
    hash.update(std::string_view{"rawframe.player.identity.v1"});
    hash.update(session);
    const base::Sha256Digest kDigest = hash.finish();
    base::Bits128 value;
    for (std::size_t index = 0; index < 8; ++index) {
        value.high = (value.high << 8U) | std::to_integer<std::uint64_t>(kDigest[index]);
        value.low = (value.low << 8U) | std::to_integer<std::uint64_t>(kDigest[8 + index]);
    }
    value.low |= value.high == 0 && value.low == 0 ? 1U : 0U;
    return PlayerIdentity{value};
}

} // namespace rawframe::world_runtime
