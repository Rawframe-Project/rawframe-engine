// Replication payloads and component codecs: exact round trips, network byte
// order, every cut refused, and bounds kept.

#include "rawframe/test/test.h"
#include "rawframe/world_replication/codec.h"
#include "rawframe/world_replication/errors.h"
#include "rawframe/world_replication/records.h"

#include <array>
#include <cstring>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_replication;

namespace {

struct Sample {
    std::int16_t small = 0;
    float x = 0;
    bool on = false;
    std::uint64_t big = 0;
};

ComponentCodec sampleCodec() {
    return ComponentCodec{.component = schema::ComponentTypeId::fromText("1d8b6e3a-0c47-4f92-a5d1-73e9b2c6f408"),
                          .size = sizeof(Sample),
                          .fields = {{offsetof(Sample, small), WireKind::I16},
                                     {offsetof(Sample, x), WireKind::F32},
                                     {offsetof(Sample, on), WireKind::Bool},
                                     {offsetof(Sample, big), WireKind::U64}}};
}

template <typename T> bool failedWith(const result::Result<T>& outcome, ReplicationError error) {
    return !outcome.has_value() && outcome.error().code() == code(error);
}

} // namespace

RAWFRAME_TEST(ValuesCrossFieldByFieldInNetworkOrder) {
    const ComponentCodec kCodec = sampleCodec();
    RAWFRAME_EXPECT(kCodec.valid() && kCodec.wireSize() == 2 + 4 + 1 + 8);
    const Sample kValue{.small = -2, .x = 1.5F, .on = true, .big = 0x0102030405060708ULL};
    std::array<std::byte, 32> out{};
    network::Writer writer{out};
    RAWFRAME_EXPECT(kCodec.encode(reinterpret_cast<const std::byte*>(&kValue), writer).has_value());
    const std::array<unsigned, 15> kExpected = {
        0xff, 0xfe, 0x3f, 0xc0, 0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    RAWFRAME_EXPECT(writer.written().size() == kExpected.size());
    for (std::size_t index = 0; index < kExpected.size() && index < writer.written().size(); ++index) {
        RAWFRAME_EXPECT(std::to_integer<unsigned>(writer.written()[index]) == kExpected[index]);
    }
    Sample back{};
    network::Reader reader{writer.written()};
    RAWFRAME_EXPECT(kCodec.decode(reader, reinterpret_cast<std::byte*>(&back)).has_value());
    RAWFRAME_EXPECT(back.small == -2 && back.x == 1.5F && back.on && back.big == kValue.big);

    // A truth that is neither nought nor one is refused.
    std::array<std::byte, 15> bad{};
    std::memcpy(bad.data(), writer.written().data(), bad.size());
    bad[6] = std::byte{2};
    network::Reader badReader{bad};
    RAWFRAME_EXPECT(!kCodec.decode(badReader, reinterpret_cast<std::byte*>(&back)).has_value());
    // A field outside the value makes the codec invalid.
    ComponentCodec outside = kCodec;
    outside.fields.push_back({sizeof(Sample) - 2, WireKind::U32});
    RAWFRAME_EXPECT(!outside.valid());
}

RAWFRAME_TEST(MappingRecordsRoundTrip) {
    const MappingRecord kRecord{.replicationEpoch = 99, .entity = NetEntityId{70000}, .owned = true};
    std::array<std::byte, 16> out{};
    network::Writer writer{out};
    RAWFRAME_EXPECT(encodeMapping(writer, kRecord).has_value());
    const auto kBack = decodeMapping(writer.written());
    RAWFRAME_EXPECT(kBack.has_value() && kBack->replicationEpoch == 99 && kBack->entity == NetEntityId{70000} &&
                    kBack->owned);
    for (std::size_t length = 0; length < writer.written().size(); ++length) {
        RAWFRAME_EXPECT(!decodeMapping(writer.written().first(length)).has_value());
    }
    const std::array<std::byte, 3> kZeroEntity = {std::byte{1}, std::byte{0}, std::byte{0}};
    RAWFRAME_EXPECT(failedWith(decodeMapping(kZeroEntity), ReplicationError::Malformed));
}

RAWFRAME_TEST(StatePayloadsCarryRecordsForTheTable) {
    std::array<std::byte, 64> out{};
    network::Writer writer{out};
    RAWFRAME_EXPECT(
        encodeStateHeader(writer, {.serverTick = 1000, .consumedInputTick = 998, .recordCount = 2}).has_value());
    RAWFRAME_EXPECT(encodeStateRecordHead(writer, {.entity = NetEntityId{1}, .component = 0}).has_value());
    RAWFRAME_EXPECT(encodeStateRecordHead(writer, {.entity = NetEntityId{2}, .component = 1}).has_value());
    network::Reader reader{writer.written()};
    const auto kHeader = decodeStateHeader(reader);
    RAWFRAME_EXPECT(kHeader.has_value() && kHeader->serverTick == 1000 && kHeader->recordCount == 2);
    RAWFRAME_EXPECT(decodeStateRecordHead(reader, 2).has_value());
    RAWFRAME_EXPECT(failedWith(decodeStateRecordHead(reader, 1), ReplicationError::UnknownComponent));
    // A count that the rest could not hold is refused before any record.
    std::array<std::byte, 16> lying{};
    network::Writer liar{lying};
    RAWFRAME_EXPECT(encodeStateHeader(liar, {.serverTick = 1, .consumedInputTick = 0, .recordCount = 50}).has_value());
    network::Reader lyingReader{liar.written()};
    RAWFRAME_EXPECT(!decodeStateHeader(lyingReader).has_value());
}

RAWFRAME_TEST(InputWindowsAreBounded) {
    const std::array<std::byte, 2> kFirst = {std::byte{1}, std::byte{2}};
    const std::array<std::byte, 1> kSecond = {std::byte{3}};
    const InputWindow kWindow{
        .newestInputTick = 41, .ackedStateSequence = 7, .ackedServerTick = 39, .commands = {kFirst, kSecond}};
    std::array<std::byte, 64> out{};
    network::Writer writer{out};
    RAWFRAME_EXPECT(encodeInputWindow(writer, kWindow).has_value());
    const auto kBack = decodeInputWindow(writer.written());
    RAWFRAME_EXPECT(kBack.has_value() && kBack->newestInputTick == 41 && kBack->commands.size() == 2 &&
                    kBack->commands[1].size() == 1 && kBack->ackedServerTick == 39);
    for (std::size_t length = 0; length < writer.written().size(); ++length) {
        RAWFRAME_EXPECT(!decodeInputWindow(writer.written().first(length)).has_value());
    }
    // Empty, too many, or reaching before tick zero is refused both ways.
    network::Writer again{out};
    RAWFRAME_EXPECT(!encodeInputWindow(again, InputWindow{.newestInputTick = 5}).has_value());
    InputWindow tooMany{.newestInputTick = 100};
    tooMany.commands.assign(kMaximumInputWindow + 1, kSecond);
    RAWFRAME_EXPECT(!encodeInputWindow(again, tooMany).has_value());
    const std::array<std::byte, 7> kBeforeZero = {
        std::byte{0}, std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    RAWFRAME_EXPECT(failedWith(decodeInputWindow(kBeforeZero), ReplicationError::Malformed));
}

RAWFRAME_TEST(PaceSignalsCarrySignedLeads) {
    for (const std::int64_t kLead : {std::int64_t{0}, std::int64_t{-1}, std::int64_t{5}, std::int64_t{-300}}) {
        std::array<std::byte, 16> out{};
        network::Writer writer{out};
        RAWFRAME_EXPECT(encodePace(writer, Pace{.measuredLead = kLead, .targetLead = 2}).has_value());
        const auto kBack = decodePace(writer.written());
        RAWFRAME_EXPECT(kBack.has_value() && kBack->measuredLead == kLead && kBack->targetLead == 2);
    }
    // Minus one is one byte: zigzag keeps small leads small either way.
    std::array<std::byte, 16> out{};
    network::Writer writer{out};
    RAWFRAME_EXPECT(encodePace(writer, Pace{.measuredLead = -1, .targetLead = 2}).has_value());
    RAWFRAME_EXPECT(writer.written().size() == 2 && writer.written()[0] == std::byte{1});
}

RAWFRAME_TEST(StateAcknowledgementsCarryABitmap) {
    std::array<std::byte, 16> out{};
    network::Writer writer{out};
    const StateAck kAck{.latest = 300, .earlier = 0x8000'0000'0000'0005ULL};
    RAWFRAME_EXPECT(encodeStateAck(writer, kAck).has_value());
    // A two-byte varint, then the bitmap most significant byte first.
    RAWFRAME_EXPECT(writer.written().size() == 10 && writer.written()[2] == std::byte{0x80} &&
                    writer.written()[9] == std::byte{0x05});
    const auto kBack = decodeStateAck(writer.written());
    RAWFRAME_EXPECT(kBack.has_value() && kBack->latest == 300 && kBack->earlier == kAck.earlier);
    RAWFRAME_EXPECT(!decodeStateAck(writer.written().first(9)).has_value());
    std::array<std::byte, 11> longer{};
    std::copy(writer.written().begin(), writer.written().end(), longer.begin());
    RAWFRAME_EXPECT(failedWith(decodeStateAck(longer), ReplicationError::Malformed));
    std::array<std::byte, 16> zero{};
    network::Writer zeroWriter{zero};
    RAWFRAME_EXPECT(!encodeStateAck(zeroWriter, StateAck{}).has_value());
}
