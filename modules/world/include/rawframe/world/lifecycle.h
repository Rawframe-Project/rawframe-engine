#pragma once

// What came and went: the components entities gained and lost, and the
// entities destroyed, in the order the World changed them. A reader, by its
// identity, watches the kinds it wants; each change one reader watches is
// recorded, with a removed component's last value, and kept until every
// reader has read it. A reader reads every change once, from its cursor,
// and has read them when it marks them read; one that never does keeps
// them, up to the bounds, past which the oldest are dropped and counted.
// The World records as its structure changes, so a system's commands are
// recorded at the barrier that applies them, in their order. It is World
// state: a checkpoint carries it.

#include "rawframe/result/result.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/entity.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world {

/// The most changes kept, the most bytes of removed values kept, the most
/// readers, the most kinds one reader watches, and the longest identity: a
/// checkpoint carries it all in one chunk, which these keep under a MiB.
inline constexpr std::size_t kMaximumLifecycleChanges = 8192;
inline constexpr std::size_t kMaximumLifecycleValueBytes = std::size_t{256} << 10U;
inline constexpr std::size_t kMaximumLifecycleReaders = 64;
inline constexpr std::size_t kMaximumLifecycleWatches = 16;
inline constexpr std::size_t kMaximumLifecycleIdentityBytes = 256;

enum class LifecycleKind : std::uint8_t {
    /// An entity gained a component it did not have.
    Added = 0,
    /// An entity lost a component, by removal or by its destruction.
    Removed = 1,
    /// An entity was destroyed.
    Destroyed = 2,
};

/// One kind of change a reader watches: a component's additions or
/// removals, or, with no component, destructions.
struct LifecycleWatch {
    LifecycleKind kind = LifecycleKind::Added;
    std::optional<schema::ComponentRuntimeId> component;

    friend bool operator==(const LifecycleWatch&, const LifecycleWatch&) = default;
};

struct LifecycleChange {
    /// Its place among every change recorded, from nought.
    std::uint64_t sequence = 0;
    LifecycleKind kind = LifecycleKind::Added;
    /// None for a destruction.
    std::optional<schema::ComponentRuntimeId> component;
    EntityHandle entity;
    /// A removed component's last value, for plain data; empty otherwise.
    std::vector<std::byte> value;
};

struct LifecycleReader {
    std::string identity;
    /// The first change it has not read.
    std::uint64_t cursor = 0;
    std::vector<LifecycleWatch> watches;
};

class Lifecycle {
public:
    /// Adds `watch` to `reader`'s, making the reader if it is new: a new
    /// reader reads changes from now on. Refused past kMaximumLifecycleReaders
    /// or kMaximumLifecycleWatches.
    [[nodiscard]] result::Status watch(std::string_view reader, LifecycleWatch watch);
    [[nodiscard]] const LifecycleReader* reader(std::string_view identity) const noexcept;
    [[nodiscard]] std::span<const LifecycleReader> readers() const noexcept {
        return readers_;
    }
    /// Whether any reader watches `watch`, so that it is recorded.
    [[nodiscard]] bool watched(const LifecycleWatch& watch) const noexcept;

    /// The changes `reader` has not read, oldest first: every kept change
    /// from its cursor on, whatever it watches. Empty for no such reader.
    [[nodiscard]] std::span<const LifecycleChange> unread(std::string_view reader) const noexcept;
    /// `reader` has read everything recorded so far; changes every reader
    /// has read are let go.
    void markRead(std::string_view reader) noexcept;

    /// Every change kept, oldest first.
    [[nodiscard]] std::span<const LifecycleChange> changes() const noexcept {
        return std::span{changes_}.subspan(first_);
    }
    /// The sequence the next change takes.
    [[nodiscard]] std::uint64_t nextSequence() const noexcept {
        return nextSequence_;
    }
    /// Changes dropped, oldest first, past the bounds.
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_;
    }

    /// Records a change if a reader watches it; `value` is copied.
    void record(LifecycleKind kind,
                std::optional<schema::ComponentRuntimeId> component,
                EntityHandle entity,
                std::span<const std::byte> value);

    /// Replaces everything, as a checkpoint restore does: readers, kept
    /// changes rising in sequence below `nextSequence` (a checkpoint keeps
    /// only those of the components it carries, so they may skip), and the
    /// count dropped. Refused past the bounds, or for changes out of order.
    [[nodiscard]] result::Status restore(std::vector<LifecycleReader> readers,
                                         std::vector<LifecycleChange> changes,
                                         std::uint64_t nextSequence,
                                         std::uint64_t dropped);

private:
    /// Lets go of what every reader has read, and drops the oldest past the
    /// bounds.
    void trim() noexcept;

    std::vector<LifecycleReader> readers_;
    /// Every watch some reader holds, once.
    std::vector<LifecycleWatch> watched_;
    std::vector<LifecycleChange> changes_;
    /// The first kept change in changes_: the ones before it are let go and
    /// compacted away once they are half of it.
    std::size_t first_ = 0;
    std::size_t valueBytes_ = 0;
    std::uint64_t nextSequence_ = 0;
    std::uint64_t dropped_ = 0;
};

} // namespace rawframe::world
