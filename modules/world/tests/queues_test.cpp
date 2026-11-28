// Message queues: values kept in the order written for the ticks their
// queue says, each reader reading every message once, all or nothing on
// what a queue holds, and a restore taking only a consistent state.

#include "rawframe/test/test.h"
#include "rawframe/world/errors.h"
#include "rawframe/world/queues.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace rawframe;
using namespace rawframe::world;

namespace {

std::array<std::byte, 4> bytesOf(std::uint32_t value) {
    std::array<std::byte, 4> bytes{};
    std::memcpy(bytes.data(), &value, sizeof value);
    return bytes;
}

std::vector<std::uint32_t> valuesOf(std::span<const QueuedMessage> messages) {
    std::vector<std::uint32_t> values;
    for (const QueuedMessage& message : messages) {
        std::uint32_t value = 0;
        std::memcpy(&value, message.value.data(), sizeof value);
        values.push_back(value);
    }
    return values;
}

} // namespace

RAWFRAME_TEST(EachReaderReadsEveryMessageOnceWhileItLasts) {
    Queues queues;
    RAWFRAME_EXPECT(
        queues.declare({.name = "hits", .valueBytes = 4, .lasts = 2, .holds = 8, .readers = {"b.sound", "a.score"}})
            .has_value());
    MessageQueue& hits = *queues.find("hits");
    RAWFRAME_EXPECT(hits.write(TickIndex{5}, bytesOf(1)).has_value());
    RAWFRAME_EXPECT(hits.write(TickIndex{5}, bytesOf(2)).has_value());
    // The score reads both and marks them; the sound has read nothing yet.
    RAWFRAME_EXPECT(valuesOf(hits.unread("a.score")) == (std::vector<std::uint32_t>{1, 2}));
    hits.markRead("a.score");
    RAWFRAME_EXPECT(hits.unread("a.score").empty());
    RAWFRAME_EXPECT(valuesOf(hits.unread("b.sound")) == (std::vector<std::uint32_t>{1, 2}));
    // A system that is not a reader reads nothing.
    RAWFRAME_EXPECT(hits.unread("c.other").empty());
    // Written in tick 6, a third message outlives the first two by a tick.
    RAWFRAME_EXPECT(hits.write(TickIndex{6}, bytesOf(3)).has_value());
    queues.expire(TickIndex{6});
    RAWFRAME_EXPECT(valuesOf(hits.unread("a.score")) == (std::vector<std::uint32_t>{3}));
    RAWFRAME_EXPECT(valuesOf(hits.unread("b.sound")) == (std::vector<std::uint32_t>{1, 2, 3}));
    queues.expire(TickIndex{7});
    RAWFRAME_EXPECT(valuesOf(hits.messages()) == (std::vector<std::uint32_t>{3}));
    queues.expire(TickIndex{8});
    RAWFRAME_EXPECT(hits.messages().empty() && hits.nextSequence() == 3);
}

RAWFRAME_TEST(AQueueHoldsNoMoreThanItSaysAndWritesWholly) {
    Queues queues;
    RAWFRAME_EXPECT(
        queues.declare({.name = "few", .valueBytes = 4, .lasts = 1, .holds = 2, .readers = {}}).has_value());
    MessageQueue& few = *queues.find("few");
    std::array<std::byte, 12> three{};
    const auto kFull = few.write(TickIndex{0}, three);
    RAWFRAME_EXPECT(!kFull.has_value() && kFull.error().errorClass() == result::ErrorClass::ResourceExhausted);
    RAWFRAME_EXPECT(few.messages().empty() && few.nextSequence() == 0);
    RAWFRAME_EXPECT(!few.write(TickIndex{0}, std::span{three}.first(6)).has_value());
    RAWFRAME_EXPECT(few.write(TickIndex{0}, std::span{three}.first(8)).has_value() && few.messages().size() == 2);
}

RAWFRAME_TEST(WhatAQueueCannotBeIsRefused) {
    Queues queues;
    RAWFRAME_EXPECT(!queues.declare({.name = "", .valueBytes = 4}).has_value());
    RAWFRAME_EXPECT(!queues.declare({.name = "q", .valueBytes = 0}).has_value());
    RAWFRAME_EXPECT(!queues.declare({.name = "q", .valueBytes = 4097}).has_value());
    RAWFRAME_EXPECT(!queues.declare({.name = "q", .valueBytes = 4, .lasts = 0}).has_value());
    RAWFRAME_EXPECT(!queues.declare({.name = "q", .valueBytes = 4, .holds = 0}).has_value());
    RAWFRAME_EXPECT(!queues.declare({.name = "q", .valueBytes = 4, .readers = {"a", "a"}}).has_value());
    RAWFRAME_EXPECT(queues.declare({.name = "q", .valueBytes = 4}).has_value());
    const auto kTwice = queues.declare({.name = "q", .valueBytes = 4});
    RAWFRAME_EXPECT(!kTwice.has_value() && kTwice.error().errorClass() == result::ErrorClass::AlreadyExists);
}

RAWFRAME_TEST(ARestoreTakesOnlyAConsistentState) {
    Queues queues;
    RAWFRAME_EXPECT(
        queues.declare({.name = "q", .valueBytes = 4, .lasts = 3, .holds = 4, .readers = {"r"}}).has_value());
    MessageQueue& queue = *queues.find("q");
    const auto kMessage = [](std::uint64_t tick, std::uint64_t sequence) {
        const auto kBytes = bytesOf(static_cast<std::uint32_t>(sequence));
        return QueuedMessage{.tick = TickIndex{tick}, .sequence = sequence, .value = {kBytes.begin(), kBytes.end()}};
    };
    RAWFRAME_EXPECT(queue.restore({kMessage(1, 4), kMessage(2, 5)}, 6, {{"r", 5}}).has_value());
    RAWFRAME_EXPECT(valuesOf(queue.unread("r")) == (std::vector<std::uint32_t>{5}) && queue.nextSequence() == 6);
    // Out of order, at the next place, a stranger's cursor, or a cursor past
    // the next place: each refused, the state kept.
    RAWFRAME_EXPECT(!queue.restore({kMessage(2, 5), kMessage(1, 4)}, 6, {{"r", 0}}).has_value());
    RAWFRAME_EXPECT(!queue.restore({kMessage(1, 6)}, 6, {{"r", 0}}).has_value());
    RAWFRAME_EXPECT(!queue.restore({}, 6, {{"s", 0}}).has_value());
    RAWFRAME_EXPECT(!queue.restore({}, 6, {{"r", 7}}).has_value());
    RAWFRAME_EXPECT(queue.nextSequence() == 6 && queue.messages().size() == 2);
}
