#pragma once

// A World's message queues: typed values one system writes and later
// systems read, in the order they were written, each kept for the ticks its
// queue says and then dropped. They are World state, carried by a
// checkpoint: the messages still held, where each reader has read to, and
// the place the next message takes. Nothing is dropped unread before its
// time, and nothing lives longer.

#include "rawframe/result/result.h"
#include "rawframe/world/time.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world {

/// The most queues a World holds, values one holds at once, and bytes one
/// value is.
inline constexpr std::size_t kMaximumQueues = 256;
inline constexpr std::uint32_t kMaximumQueuedMessages = 65'536;
inline constexpr std::size_t kMaximumQueuedValueBytes = 4096;
/// The most a full queue's messages come to, each counted as its value and
/// sixteen bytes more, so a checkpoint chunk always holds one.
inline constexpr std::size_t kMaximumQueueBytes = std::size_t{768} * 1024;
/// The most bytes a queue's name, or a reader's identity, is.
inline constexpr std::size_t kMaximumQueueNameBytes = 256;
/// The most ticks a message is kept, and readers a queue has.
inline constexpr std::uint32_t kMaximumQueueTicks = 65'536;
inline constexpr std::size_t kMaximumQueueReaders = 256;

/// What a queue is: its name, how many bytes each value is, how many ticks a
/// message is kept (one keeps it for the tick it was written in only), how
/// many it holds at once, and the systems that read it, by identity.
struct QueueDeclaration {
    std::string name;
    std::size_t valueBytes = 0;
    std::uint32_t lasts = 1;
    std::uint32_t holds = 1024;
    std::vector<std::string> readers;

    friend bool operator==(const QueueDeclaration&, const QueueDeclaration&) = default;
};

/// One message: the tick it was written in, its place in its queue's order,
/// and its value's bytes.
struct QueuedMessage {
    TickIndex tick;
    std::uint64_t sequence = 0;
    std::vector<std::byte> value;

    friend bool operator==(const QueuedMessage&, const QueuedMessage&) = default;
};

class MessageQueue {
public:
    explicit MessageQueue(QueueDeclaration declaration) noexcept : declaration_(std::move(declaration)) {
    }

    [[nodiscard]] const QueueDeclaration& declaration() const noexcept {
        return declaration_;
    }

    /// Appends `values`, each `valueBytes` long, written in `tick`, in order.
    /// All or nothing: `resource_exhausted` when the queue would hold more
    /// than it may, `invalid_argument` for a length not a whole number of
    /// values.
    [[nodiscard]] result::Status write(TickIndex tick, std::span<const std::byte> values);

    /// Every message held, oldest first.
    [[nodiscard]] std::span<const QueuedMessage> messages() const noexcept {
        return messages_;
    }
    /// The messages `reader` has not read yet, oldest first: none for a
    /// system that is not one of the queue's readers.
    [[nodiscard]] std::span<const QueuedMessage> unread(std::string_view reader) const noexcept;
    /// Marks every message held as read by `reader`.
    void markRead(std::string_view reader) noexcept;

    /// The place the next message takes.
    [[nodiscard]] std::uint64_t nextSequence() const noexcept {
        return nextSequence_;
    }
    /// The first place `reader` has not read.
    [[nodiscard]] std::uint64_t cursor(std::string_view reader) const noexcept;
    /// Every reader's cursor, by identity.
    [[nodiscard]] const std::map<std::string, std::uint64_t, std::less<>>& cursors() const noexcept {
        return cursors_;
    }

    /// Drops every message whose ticks are over by `now`.
    void expire(TickIndex now) noexcept;

    /// Sets the whole state, as a checkpoint restore does: refused
    /// (`invalid_argument`) for messages out of order, of another length,
    /// more than the queue holds, at or past `nextSequence`, or a cursor of
    /// a system that is not a reader or past `nextSequence`.
    [[nodiscard]] result::Status restore(std::vector<QueuedMessage> messages,
                                         std::uint64_t nextSequence,
                                         std::map<std::string, std::uint64_t, std::less<>> cursors);

private:
    QueueDeclaration declaration_;
    std::vector<QueuedMessage> messages_;
    std::uint64_t nextSequence_ = 0;
    std::map<std::string, std::uint64_t, std::less<>> cursors_;
};

/// Every queue of a World, by name.
class Queues {
public:
    /// Adds a queue. Refused (`invalid_argument`) for an empty name, a
    /// value of no bytes or more than kMaximumQueuedValueBytes, a `lasts`
    /// of nought or past kMaximumQueueTicks, a `holds` of nought or past
    /// kMaximumQueuedMessages, a full queue past kMaximumQueueBytes, more
    /// than kMaximumQueueReaders or a reader named twice; `already_exists` for a name held; `resource_exhausted`
    /// past kMaximumQueues.
    [[nodiscard]] result::Status declare(QueueDeclaration declaration);

    /// The queue `name`, or null.
    [[nodiscard]] MessageQueue* find(std::string_view name) noexcept;
    [[nodiscard]] const MessageQueue* find(std::string_view name) const noexcept;

    [[nodiscard]] const std::map<std::string, MessageQueue, std::less<>>& all() const noexcept {
        return queues_;
    }

    /// Drops every queue's messages whose ticks are over by `now`.
    void expire(TickIndex now) noexcept;

private:
    std::map<std::string, MessageQueue, std::less<>> queues_;
};

} // namespace rawframe::world
