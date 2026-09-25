// The generation-1 wire: RFC 9000 varint vectors, canonical-only readers,
// every truncation boundary, frames, prefaces, datagrams, and a property run
// over random bytes where every accepted input re-encodes to itself.

#include "rawframe/network/errors.h"
#include "rawframe/network/wire.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <vector>

using namespace rawframe;
using namespace rawframe::network;

namespace {

std::vector<std::byte> bytesOf(std::initializer_list<unsigned> values) {
    std::vector<std::byte> bytes;
    for (const unsigned kValue : values) {
        bytes.push_back(static_cast<std::byte>(kValue));
    }
    return bytes;
}

template <typename T> bool failedWith(const result::Result<T>& outcome, NetworkError error) {
    return !outcome.has_value() && outcome.error().code() == code(error);
}

} // namespace

RAWFRAME_TEST(VarintsMatchRfc9000) {
    struct Vector {
        std::vector<std::byte> bytes;
        std::uint64_t value;
    };
    const std::array<Vector, 4> kVectors = {{
        {bytesOf({0xc2, 0x19, 0x7c, 0x5e, 0xff, 0x14, 0xe8, 0x8c}), 151288809941952652ULL},
        {bytesOf({0x9d, 0x7f, 0x3e, 0x7d}), 494878333},
        {bytesOf({0x7b, 0xbd}), 15293},
        {bytesOf({0x25}), 37},
    }};
    for (const Vector& vector : kVectors) {
        Reader reader{vector.bytes};
        const auto kRead = reader.varint();
        RAWFRAME_EXPECT(kRead.has_value() && *kRead == vector.value && reader.remaining() == 0);
        std::array<std::byte, 8> out{};
        Writer writer{out};
        RAWFRAME_EXPECT(writer.varint(vector.value).has_value());
        RAWFRAME_EXPECT(std::ranges::equal(writer.written(), vector.bytes));
    }
    // RFC 9000 spells 37 in two bytes too; generation 1 takes one spelling.
    const auto kLong = bytesOf({0x40, 0x25});
    Reader reader{kLong};
    RAWFRAME_EXPECT(failedWith(reader.varint(), NetworkError::NonCanonical));
}

RAWFRAME_TEST(VarintBoundariesRoundTrip) {
    for (const std::uint64_t kValue : {std::uint64_t{0},
                                       std::uint64_t{63},
                                       std::uint64_t{64},
                                       std::uint64_t{16383},
                                       std::uint64_t{16384},
                                       (std::uint64_t{1} << 30U) - 1U,
                                       std::uint64_t{1} << 30U,
                                       kMaximumVarint}) {
        std::array<std::byte, 8> out{};
        Writer writer{out};
        RAWFRAME_EXPECT(writer.varint(kValue).has_value() && writer.written().size() == varintSize(kValue));
        Reader reader{writer.written()};
        const auto kRead = reader.varint();
        RAWFRAME_EXPECT(kRead.has_value() && *kRead == kValue);
        // Every shorter prefix is truncated, never misread.
        for (std::size_t length = 0; length < writer.written().size(); ++length) {
            Reader cut{writer.written().first(length)};
            RAWFRAME_EXPECT(failedWith(cut.varint(), NetworkError::Truncated));
        }
    }
    std::array<std::byte, 8> out{};
    Writer writer{out};
    RAWFRAME_EXPECT(!writer.varint(kMaximumVarint + 1).has_value());
    // A failed writer writes nothing more.
    RAWFRAME_EXPECT(!writer.varint(1).has_value() && writer.written().empty());
    std::array<std::byte, 1> tiny{};
    Writer small{tiny};
    const auto kFull = small.varint(64);
    RAWFRAME_EXPECT(!kFull.has_value() && kFull.error().code() == code(NetworkError::BufferFull));
}

RAWFRAME_TEST(FramesAreTakenWholeOrWaitedFor) {
    const auto kPayload = bytesOf({1, 2, 3, 4, 5});
    std::array<std::byte, 64> out{};
    Writer writer{out};
    RAWFRAME_EXPECT(writeFrame(writer, 20, kPayload).has_value());
    RAWFRAME_EXPECT(writeFrame(writer, 21, {}).has_value());
    const auto kStream = writer.written();

    Reader reader{kStream};
    const auto kFirst = readFrame(reader, 16);
    RAWFRAME_EXPECT(kFirst.has_value() && kFirst->type == 20 && std::ranges::equal(kFirst->payload, kPayload));
    RAWFRAME_EXPECT(criticalFrame(kFirst->type));
    const auto kSecond = readFrame(reader, 16);
    RAWFRAME_EXPECT(kSecond.has_value() && kSecond->type == 21 && kSecond->payload.empty());
    RAWFRAME_EXPECT(!criticalFrame(kSecond->type) && reader.remaining() == 0);

    // A stream that has not delivered the whole frame yet leaves it unread.
    for (std::size_t length = 0; length < 7; ++length) {
        Reader partial{kStream.first(length)};
        RAWFRAME_EXPECT(failedWith(readFrame(partial, 16), NetworkError::Truncated) && partial.consumed() == 0);
    }
    // Too long is known from the length alone, before any payload arrives.
    Reader limited{kStream.first(2)};
    RAWFRAME_EXPECT(failedWith(readFrame(limited, 4), NetworkError::Oversized));
}

RAWFRAME_TEST(PrefacesCarryTheirKindsFields) {
    const std::array<StreamPreface, 3> kPrefaces = {{
        {.kind = StreamKind::Control},
        {.kind = StreamKind::EventLane, .eventLaneId = 3, .eventLaneEpoch = 70000},
        {.kind = StreamKind::Baseline, .baselineSequence = 9, .replicationEpoch = kMaximumVarint},
    }};
    for (const StreamPreface& preface : kPrefaces) {
        std::array<std::byte, 32> out{};
        Writer writer{out};
        RAWFRAME_EXPECT(writePreface(writer, preface).has_value());
        Reader reader{writer.written()};
        const auto kRead = readPreface(reader);
        RAWFRAME_EXPECT(kRead.has_value() && kRead->kind == preface.kind && kRead->eventLaneId == preface.eventLaneId &&
                        kRead->eventLaneEpoch == preface.eventLaneEpoch &&
                        kRead->baselineSequence == preface.baselineSequence &&
                        kRead->replicationEpoch == preface.replicationEpoch && reader.remaining() == 0);
    }
    const auto kControl = bytesOf({0x00, 0x01});
    Reader control{kControl};
    RAWFRAME_EXPECT(readPreface(control).has_value());
    const auto kUnknown = bytesOf({0x03, 0x01});
    Reader unknown{kUnknown};
    RAWFRAME_EXPECT(failedWith(readPreface(unknown), NetworkError::UnknownCritical));
    const auto kOld = bytesOf({0x00, 0x02});
    Reader old{kOld};
    RAWFRAME_EXPECT(failedWith(readPreface(old), NetworkError::WrongVersion));
}

RAWFRAME_TEST(DatagramsAreExactlyOneRecord) {
    const auto kPayload = bytesOf({9, 8, 7});
    const DatagramRecord kRecord{
        .lane = DatagramLane::State, .laneEpoch = 5, .sequence = 1000, .payloadType = 2, .payload = kPayload};
    std::array<std::byte, 32> out{};
    Writer writer{out};
    RAWFRAME_EXPECT(writeDatagram(writer, kRecord).has_value());
    const auto kDatagram = writer.written();
    // lane 1, epoch 5, sequence 1000 in two bytes, type 2, length 3, payload.
    RAWFRAME_EXPECT(std::ranges::equal(kDatagram, bytesOf({0x01, 0x05, 0x43, 0xe8, 0x02, 0x03, 9, 8, 7})));
    const auto kRead = readDatagram(kDatagram, 16);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->lane == DatagramLane::State && kRead->sequence == 1000 &&
                    std::ranges::equal(kRead->payload, kPayload));

    std::vector<std::byte> longer{kDatagram.begin(), kDatagram.end()};
    longer.push_back(std::byte{0});
    RAWFRAME_EXPECT(failedWith(readDatagram(longer, 16), NetworkError::TrailingBytes));
    RAWFRAME_EXPECT(failedWith(readDatagram(kDatagram.first(kDatagram.size() - 1), 16), NetworkError::Truncated));
    RAWFRAME_EXPECT(failedWith(readDatagram(kDatagram, 2), NetworkError::Oversized));
    const auto kUnknownLane = bytesOf({0x02, 0x00, 0x00, 0x00, 0x00});
    RAWFRAME_EXPECT(failedWith(readDatagram(kUnknownLane, 16), NetworkError::UnknownCritical));
}

RAWFRAME_TEST(AcceptedBytesReencodeToThemselves) {
    // Canonical-only reading means one spelling per value: whatever random
    // input is accepted must be exactly what writing the result produces.
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    const auto kNext = [&state] {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    };
    std::size_t accepted = 0;
    for (int round = 0; round < 200'000; ++round) {
        std::array<std::byte, 12> input{};
        const std::size_t kLength = kNext() % (input.size() + 1);
        for (std::size_t index = 0; index < kLength; ++index) {
            // Small first bytes keep lanes and lengths plausible often enough.
            input[index] = static_cast<std::byte>(index % 2 == 0 ? kNext() % 4 : kNext() % 256);
        }
        const std::span<const std::byte> kInput{input.data(), kLength};
        const auto kRecord = readDatagram(kInput, 8);
        if (!kRecord.has_value()) {
            continue;
        }
        ++accepted;
        std::array<std::byte, 32> out{};
        Writer writer{out};
        RAWFRAME_EXPECT(writeDatagram(writer, *kRecord).has_value());
        RAWFRAME_EXPECT(std::ranges::equal(writer.written(), kInput));
    }
    RAWFRAME_EXPECT(accepted > 100);
}
