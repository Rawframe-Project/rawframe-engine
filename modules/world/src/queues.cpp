#include "rawframe/world/queues.h"

#include "rawframe/world/errors.h"

#include <algorithm>
#include <utility>

namespace rawframe::world {

namespace {

result::Status refuse(result::ErrorClass kind, std::string_view why) {
    return result::fail(kind, kWorldDomain, code(WorldError::InvalidQueue), why);
}

} // namespace

result::Status MessageQueue::write(TickIndex tick, std::span<const std::byte> values) {
    const std::size_t kBytes = declaration_.valueBytes;
    if (values.size() % kBytes != 0) {
        return refuse(result::ErrorClass::InvalidArgument, "the bytes are not a whole number of the queue's values");
    }
    const std::size_t kCount = values.size() / kBytes;
    if (messages_.size() + kCount > declaration_.holds) {
        return refuse(result::ErrorClass::ResourceExhausted, "the queue holds as many messages as it may");
    }
    for (std::size_t at = 0; at < kCount; ++at) {
        const auto kValue = values.subspan(at * kBytes, kBytes);
        messages_.push_back(QueuedMessage{
            .tick = tick, .sequence = nextSequence_++, .value = std::vector<std::byte>{kValue.begin(), kValue.end()}});
    }
    return {};
}

std::span<const QueuedMessage> MessageQueue::unread(std::string_view reader) const noexcept {
    const auto kCursor = cursors_.find(reader);
    if (kCursor == cursors_.end()) {
        return {};
    }
    const auto kFirst = std::ranges::lower_bound(messages_, kCursor->second, {}, &QueuedMessage::sequence);
    return {kFirst, messages_.end()};
}

void MessageQueue::markRead(std::string_view reader) noexcept {
    if (const auto kCursor = cursors_.find(reader); kCursor != cursors_.end()) {
        kCursor->second = nextSequence_;
    }
}

std::uint64_t MessageQueue::cursor(std::string_view reader) const noexcept {
    const auto kCursor = cursors_.find(reader);
    return kCursor == cursors_.end() ? nextSequence_ : kCursor->second;
}

void MessageQueue::expire(TickIndex now) noexcept {
    // A message of tick T lasting L ticks is read in T to T + L - 1.
    const auto kKept = std::ranges::find_if(messages_, [&](const QueuedMessage& message) {
        return now.value - message.tick.value < declaration_.lasts;
    });
    messages_.erase(messages_.begin(), kKept);
}

result::Status MessageQueue::restore(std::vector<QueuedMessage> messages,
                                     std::uint64_t nextSequence,
                                     std::map<std::string, std::uint64_t, std::less<>> cursors) {
    if (messages.size() > declaration_.holds) {
        return refuse(result::ErrorClass::InvalidArgument, "a queue holds more messages than it may");
    }
    for (std::size_t at = 0; at < messages.size(); ++at) {
        const QueuedMessage& message = messages[at];
        if (message.value.size() != declaration_.valueBytes || message.sequence >= nextSequence ||
            (at > 0 && (messages[at - 1].sequence >= message.sequence || messages[at - 1].tick > message.tick))) {
            return refuse(result::ErrorClass::InvalidArgument,
                          "a queue's messages are out of order, of another length, or past its next place");
        }
    }
    if (cursors.size() != declaration_.readers.size()) {
        return refuse(result::ErrorClass::InvalidArgument, "a queue's cursors are not its readers'");
    }
    for (const auto& [kReader, kCursor] : cursors) {
        if (std::ranges::find(declaration_.readers, kReader) == declaration_.readers.end() || kCursor > nextSequence) {
            return refuse(result::ErrorClass::InvalidArgument, "a queue's cursors are not its readers'");
        }
    }
    messages_ = std::move(messages);
    nextSequence_ = nextSequence;
    cursors_ = std::move(cursors);
    return {};
}

result::Status Queues::declare(QueueDeclaration declaration) {
    const bool kLongReader = std::ranges::any_of(declaration.readers, [](const std::string& reader) {
        return reader.empty() || reader.size() > kMaximumQueueNameBytes;
    });
    if (declaration.name.empty() || declaration.name.size() > kMaximumQueueNameBytes || kLongReader ||
        declaration.valueBytes == 0 || declaration.valueBytes > kMaximumQueuedValueBytes || declaration.lasts == 0 ||
        declaration.lasts > kMaximumQueueTicks || declaration.holds == 0 ||
        declaration.holds > kMaximumQueuedMessages || declaration.readers.size() > kMaximumQueueReaders ||
        declaration.holds * (declaration.valueBytes + 16) > kMaximumQueueBytes) {
        return refuse(result::ErrorClass::InvalidArgument,
                      "a queue has a name, a value of 1 to 4096 bytes, lasts 1 to 65,536 ticks, holds 1 to 65,536 "
                      "messages, at most 768 KiB full, and at most 256 readers");
    }
    std::vector<std::string> readers = declaration.readers;
    std::ranges::sort(readers);
    if (std::ranges::adjacent_find(readers) != readers.end()) {
        return refuse(result::ErrorClass::InvalidArgument, "a queue names a reader twice");
    }
    if (queues_.contains(declaration.name)) {
        return refuse(result::ErrorClass::AlreadyExists, "a queue of this name is held");
    }
    if (queues_.size() >= kMaximumQueues) {
        return refuse(result::ErrorClass::ResourceExhausted, "the World holds as many queues as it may");
    }
    std::map<std::string, std::uint64_t, std::less<>> cursors;
    for (std::string& reader : readers) {
        cursors.emplace(std::move(reader), 0);
    }
    std::string name = declaration.name;
    auto [placed, kAdded] = queues_.emplace(std::move(name), MessageQueue{std::move(declaration)});
    static_cast<void>(kAdded);
    return placed->second.restore({}, 0, std::move(cursors));
}

MessageQueue* Queues::find(std::string_view name) noexcept {
    const auto kFound = queues_.find(name);
    return kFound == queues_.end() ? nullptr : &kFound->second;
}

const MessageQueue* Queues::find(std::string_view name) const noexcept {
    const auto kFound = queues_.find(name);
    return kFound == queues_.end() ? nullptr : &kFound->second;
}

void Queues::expire(TickIndex now) noexcept {
    for (auto& [kName, queue] : queues_) {
        queue.expire(now);
    }
}

} // namespace rawframe::world
