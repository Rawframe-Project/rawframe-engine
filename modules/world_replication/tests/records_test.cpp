// Replication payloads and component codecs: exact round trips, network byte
// order, every cut refused, and bounds kept.

#include "rawframe/test/mutations.h"
#include "rawframe/test/test.h"
#include "rawframe/world_replication/checksum.h"
#include "rawframe/world_replication/codec.h"
#include "rawframe/world_replication/errors.h"
#include "rawframe/world_replication/records.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <span>
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

RAWFRAME_TEST(MappingDeclarationsRoundTrip) {
    const MappingDeclaration kDeclaration{
        .replicationEpoch = 99, .run = {.first = NetEntityId{70000}, .count = 4096}, .ownedFirst = true};
    std::array<std::byte, 32> out{};
    network::Writer writer{out};
    RAWFRAME_EXPECT(encodeMappingDeclaration(writer, kDeclaration).has_value());
    const auto kBack = decodeMappingDeclaration(writer.written());
    RAWFRAME_EXPECT(kBack.has_value() && kBack->replicationEpoch == 99 && kBack->run == kDeclaration.run &&
                    kBack->ownedFirst);
    for (std::size_t length = 0; length < writer.written().size(); ++length) {
        RAWFRAME_EXPECT(!decodeMappingDeclaration(writer.written().first(length)).has_value());
    }
    // No ID zero, no empty run, and no run past the last ID.
    const auto kRefused = [](std::uint32_t first, std::uint64_t count) {
        std::array<std::byte, 32> bytes{};
        network::Writer refused{bytes};
        RAWFRAME_EXPECT(refused.varint(1).has_value() && refused.varint(first).has_value() &&
                        refused.varint(count).has_value() && refused.varint(0).has_value());
        return failedWith(decodeMappingDeclaration(refused.written()), ReplicationError::Malformed);
    };
    RAWFRAME_EXPECT(kRefused(0, 1));
    RAWFRAME_EXPECT(kRefused(5, 0));
    RAWFRAME_EXPECT(kRefused(std::numeric_limits<std::uint32_t>::max(), 2));
    RAWFRAME_EXPECT(!kRefused(std::numeric_limits<std::uint32_t>::max(), 1));
}

RAWFRAME_TEST(MappingRetirementsAreAscendingRunsWithGaps) {
    const MappingRetirement kRetirement{.replicationEpoch = 7,
                                        .runs = {{.first = NetEntityId{3}, .count = 2},
                                                 {.first = NetEntityId{9}, .count = 1},
                                                 {.first = NetEntityId{70000}, .count = 300}}};
    std::array<std::byte, 64> out{};
    network::Writer writer{out};
    RAWFRAME_EXPECT(encodeMappingRetirement(writer, kRetirement).has_value());
    // The epoch, then each run as the gap from where the one before ended
    // and its count: 3 2, 4 1, 69990 300.
    RAWFRAME_EXPECT(writer.written().size() == 1 + 1 + 1 + 1 + 1 + 4 + 2);
    const auto kBack = decodeMappingRetirement(writer.written());
    RAWFRAME_EXPECT(kBack.has_value() && kBack->replicationEpoch == 7 && kBack->runs == kRetirement.runs);
    // Cut inside a run, it is refused.
    RAWFRAME_EXPECT(!decodeMappingRetirement(writer.written().first(writer.written().size() - 1)).has_value());
    const auto kRuns = [](std::initializer_list<std::uint64_t> values) {
        std::array<std::byte, 64> bytes{};
        network::Writer runs{bytes};
        static_cast<void>(runs.varint(7));
        for (const std::uint64_t kValue : values) {
            static_cast<void>(runs.varint(kValue));
        }
        return decodeMappingRetirement(runs.written());
    };
    RAWFRAME_EXPECT(kRuns({3, 2}).has_value());
    // One set of IDs has one spelling: nothing retired, an empty run, a
    // run touching the one before (one run, written as two), ID zero.
    RAWFRAME_EXPECT(failedWith(kRuns({}), ReplicationError::Malformed));
    RAWFRAME_EXPECT(failedWith(kRuns({3, 0}), ReplicationError::Malformed));
    RAWFRAME_EXPECT(failedWith(kRuns({3, 2, 0, 1}), ReplicationError::Malformed));
    RAWFRAME_EXPECT(failedWith(kRuns({0, 1}), ReplicationError::Malformed));
    // Nothing past the last ID, however the gaps add up.
    RAWFRAME_EXPECT(kRuns({std::numeric_limits<std::uint32_t>::max(), 1}).has_value());
    RAWFRAME_EXPECT(failedWith(kRuns({std::numeric_limits<std::uint32_t>::max(), 2}), ReplicationError::Malformed));
    RAWFRAME_EXPECT(
        failedWith(kRuns({std::numeric_limits<std::uint32_t>::max() - 1, 1, 1, 1}), ReplicationError::Malformed));
}

RAWFRAME_TEST(StatePayloadsCarryRecordsForTheTable) {
    std::array<std::byte, 64> out{};
    network::Writer writer{out};
    RAWFRAME_EXPECT(
        encodeStateHeader(writer, {.serverTick = 1000, .consumedInputTick = 998, .playerWhole = true, .recordCount = 2})
            .has_value());
    RAWFRAME_EXPECT(encodeStateRecordHead(writer, {.entity = NetEntityId{1}, .component = 0}).has_value());
    RAWFRAME_EXPECT(encodeStateRecordHead(writer, {.entity = NetEntityId{2}, .component = 1}).has_value());
    network::Reader reader{writer.written()};
    const auto kHeader = decodeStateHeader(reader);
    RAWFRAME_EXPECT(kHeader.has_value() && kHeader->serverTick == 1000 && kHeader->playerWhole &&
                    kHeader->recordCount == 2);
    RAWFRAME_EXPECT(decodeStateRecordHead(reader, 2).has_value());
    RAWFRAME_EXPECT(failedWith(decodeStateRecordHead(reader, 1), ReplicationError::UnknownComponent));
    // A count that the rest could not hold is refused before any record.
    std::array<std::byte, 16> lying{};
    network::Writer liar{lying};
    RAWFRAME_EXPECT(encodeStateHeader(liar, {.serverTick = 1, .consumedInputTick = 0, .recordCount = 50}).has_value());
    network::Reader lyingReader{liar.written()};
    RAWFRAME_EXPECT(!decodeStateHeader(lyingReader).has_value());
    // The held flag is a truth, nothing else.
    const std::array<std::byte, 4> kTwo = {std::byte{1}, std::byte{0}, std::byte{2}, std::byte{0}};
    network::Reader twoReader{kTwo};
    RAWFRAME_EXPECT(!decodeStateHeader(twoReader).has_value());
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
        RAWFRAME_EXPECT(
            encodePace(writer, Pace{.measuredLead = kLead, .targetLead = 2, .greatestLead = 6}).has_value());
        const auto kBack = decodePace(writer.written());
        RAWFRAME_EXPECT(kBack.has_value() && kBack->measuredLead == kLead && kBack->targetLead == 2 &&
                        kBack->greatestLead == 6);
    }
    // Minus one is one byte: zigzag keeps small leads small either way.
    std::array<std::byte, 16> out{};
    network::Writer writer{out};
    RAWFRAME_EXPECT(encodePace(writer, Pace{.measuredLead = -1, .targetLead = 2}).has_value());
    RAWFRAME_EXPECT(writer.written().size() == 3 && writer.written()[0] == std::byte{1});
    // A greatest lead past a minute of ticks is refused.
    std::array<std::byte, 16> far{};
    network::Writer farWriter{far};
    RAWFRAME_EXPECT(encodePace(farWriter, Pace{.targetLead = 2, .greatestLead = kMaximumPaceLead + 1}).has_value());
    RAWFRAME_EXPECT(!decodePace(farWriter.written()).has_value());
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

RAWFRAME_TEST(AClaimedMomentIsKeptNearWhatItsConnectionUsuallyClaims) {
    std::optional<double> lag;
    // The first claim is taken as it is and sets the lag: ten ticks.
    const KeptPerception kFirst = keepPerception({.baseTick = 990, .fraction = 0}, 1000, 6, lag);
    RAWFRAME_EXPECT(!kFirst.clamped && kFirst.moment.baseTick == 990 && lag == 10.0);
    // Honest claims a little either side keep their exact moment.
    const KeptPerception kNear = keepPerception({.baseTick = 1003, .fraction = 32768}, 1010, 6, lag);
    RAWFRAME_EXPECT(!kNear.clamped && kNear.moment.baseTick == 1003 && kNear.moment.fraction == 32768);
    lag = 10.0;
    // Twenty ticks further back than usual: moved to six past the lag, and
    // the lag moves a sixty-fourth of that.
    const KeptPerception kBack = keepPerception({.baseTick = 1970, .fraction = 1234}, 2000, 6, lag);
    RAWFRAME_EXPECT(kBack.clamped && kBack.moment.baseTick == 1984 && kBack.moment.fraction == 0);
    RAWFRAME_EXPECT(lag == 10.09375);
    // From the future: moved to six short of it.
    lag = 10.0;
    const KeptPerception kAhead = keepPerception({.baseTick = 2005, .fraction = 0}, 2000, 6, lag);
    RAWFRAME_EXPECT(kAhead.clamped && kAhead.moment.baseTick == 1996);
    // A liar claiming thirty ticks back every time drags the lag no faster
    // than the skew allows: after a quarter of a second, a tenth of the way.
    lag = 10.0;
    for (std::uint64_t tick = 3000; tick < 3016; ++tick) {
        static_cast<void>(keepPerception({.baseTick = tick - 30, .fraction = 0}, tick, 6, lag));
    }
    RAWFRAME_EXPECT(*lag <= 11.5);
    // A moment of tick nought saw nothing, and changes nothing.
    const double kBefore = *lag;
    const KeptPerception kNothing = keepPerception({}, 4000, 6, lag);
    RAWFRAME_EXPECT(!kNothing.clamped && kNothing.moment.baseTick == 0 && lag == kBefore);
}

namespace {

/// Runs `rounds` mutations of `seed` through `decode`, and for each one it
/// accepts, checks `encode` writes the very same bytes back; the count
/// accepted.
template <typename Decode, typename Encode>
int reencodes(std::span<const std::byte> seed, Decode decode, Encode encode, int rounds = 20'000) {
    // Any byte may be inserted.
    std::string everyByte;
    for (int value = 0; value < 256; ++value) {
        everyByte.push_back(static_cast<char>(value));
    }
    test::Mutations mutations;
    int accepted = 0;
    for (int round = 0; round < rounds; ++round) {
        const std::string kText =
            mutations.mutate(std::string_view{reinterpret_cast<const char*>(seed.data()), seed.size()}, everyByte);
        const auto kBytes = std::as_bytes(std::span{kText.data(), kText.size()});
        const auto kRead = decode(std::span<const std::byte>{kBytes});
        if (!kRead.has_value()) {
            continue;
        }
        ++accepted;
        std::array<std::byte, 4096> out{};
        network::Writer writer{out};
        const bool kWritten = encode(writer, *kRead).has_value();
        RAWFRAME_EXPECT(kWritten && std::ranges::equal(writer.written(), kBytes));
    }
    return accepted;
}

} // namespace

RAWFRAME_TEST(HostilePeerPayloadsReadOnlyAsTheyWrite) {
    // What a peer sends is hostile input: each payload a
    // server reads from a client, and each a client reads from a server,
    // mutated, and whatever a decoder accepts writes back to the very same
    // bytes, so no payload has two readings and no bound is passed.
    std::array<std::byte, 256> seed{};

    // Client to server: an input window, a state acknowledgement, a
    // perception context, a mapping acknowledgement, a checksum.
    const std::array<std::byte, 3> kFirst = {std::byte{1}, std::byte{2}, std::byte{3}};
    const std::array<std::byte, 1> kSecond = {std::byte{9}};
    network::Writer window{seed};
    RAWFRAME_EXPECT(encodeInputWindow(window,
                                      InputWindow{.newestInputTick = 300,
                                                  .ackedStateSequence = 77,
                                                  .ackedServerTick = 290,
                                                  .commands = {kFirst, kSecond, kFirst}})
                        .has_value());
    const int kWindows = reencodes(
        window.written(),
        [](std::span<const std::byte> bytes) {
            auto read = decodeInputWindow(bytes);
            if (read.has_value()) {
                RAWFRAME_EXPECT(read->commands.size() <= kMaximumInputWindow &&
                                std::ranges::all_of(read->commands, [](std::span<const std::byte> command) {
                                    return command.size() <= kMaximumInputCommand;
                                }));
            }
            return read;
        },
        [](network::Writer& writer, const InputWindow& read) {
            return encodeInputWindow(writer, read);
        });
    std::array<std::byte, 16> ackSeed{};
    network::Writer ack{ackSeed};
    RAWFRAME_EXPECT(encodeStateAck(ack, StateAck{.latest = 300, .earlier = 0x8000'0000'0000'0005ULL}).has_value());
    const int kAcks = reencodes(ack.written(), &decodeStateAck, &encodeStateAck);
    std::array<std::byte, 16> perceptionSeed{};
    network::Writer perception{perceptionSeed};
    RAWFRAME_EXPECT(encodePerception(perception, PerceptionContext{.baseTick = 4000, .fraction = 777}).has_value());
    const int kPerceptions = reencodes(perception.written(), &decodePerception, &encodePerception);
    std::array<std::byte, 32> mappingSeed{};
    network::Writer mapping{mappingSeed};
    RAWFRAME_EXPECT(encodeMappingDeclaration(mapping,
                                             MappingDeclaration{.replicationEpoch = 3,
                                                                .run = {.first = NetEntityId{77}, .count = 40},
                                                                .ownedFirst = true})
                        .has_value());
    std::array<std::byte, 32> retirementSeed{};
    network::Writer retiring{retirementSeed};
    RAWFRAME_EXPECT(encodeMappingRetirement(retiring,
                                            MappingRetirement{.replicationEpoch = 3,
                                                              .runs = {{.first = NetEntityId{5}, .count = 3},
                                                                       {.first = NetEntityId{90}, .count = 1}}})
                        .has_value());
    const int kMappings = reencodes(mapping.written(), &decodeMappingDeclaration, &encodeMappingDeclaration) +
                          reencodes(retiring.written(), &decodeMappingRetirement, &encodeMappingRetirement);
    std::array<std::byte, 32> checksumSeed{};
    network::Writer checksum{checksumSeed};
    RAWFRAME_EXPECT(
        encodeChecksum(checksum, ChecksumRecord{.tick = 4000, .scope = 0xdc31f5d4fc776b6cULL, .checksum = 7})
            .has_value());
    const int kChecksums = reencodes(checksum.written(), &decodeChecksum, &encodeChecksum);

    // Server to client: a pace signal, and a component value by its codec.
    std::array<std::byte, 16> paceSeed{};
    network::Writer pace{paceSeed};
    RAWFRAME_EXPECT(encodePace(pace, Pace{.measuredLead = -3, .targetLead = 2}).has_value());
    const int kPaces = reencodes(pace.written(), &decodePace, &encodePace);

    std::printf("  accepted: %d windows, %d acks, %d perceptions, %d mappings, %d checksums, %d paces of 20000 each\n",
                kWindows,
                kAcks,
                kPerceptions,
                kMappings,
                kChecksums,
                kPaces);
    RAWFRAME_EXPECT(kWindows > 0 && kAcks > 0 && kPerceptions > 0 && kMappings > 0 && kChecksums > 0 && kPaces > 0);
}

RAWFRAME_TEST(TheChecksumIsPinned) {
    // Golden vectors, computed apart from this code: a change to the
    // algorithm is a new identity, never a quiet change to these.
    constexpr std::array<schema::ComponentTypeId, 2> kScope = {
        schema::ComponentTypeId::fromText("6b05cdb4-0683-437a-aaac-7ad990b2ddb4"),
        schema::ComponentTypeId::fromText("3d8b6f21-7c4e-4a95-b0d3-e6f1a2c95b78")};
    RAWFRAME_EXPECT(scopeFingerprint(kScope) == 0xdc31f5d4fc776b6cULL);
    RAWFRAME_EXPECT(scopeFingerprint({}) == 0xd3a3834dfe70caf2ULL);
    const std::array<std::byte, 3> kValue = {std::byte{1}, std::byte{2}, std::byte{3}};
    const std::array<std::span<const std::byte>, 2> kBoth = {kValue, std::span<const std::byte>{}};
    RAWFRAME_EXPECT(predictedChecksum(kBoth) == 0xecf4cf2e9344edf0ULL);
    RAWFRAME_EXPECT(predictedChecksum(std::span{kBoth}.first(1)) == 0x3393db065a7b1300ULL);
    // The record: a varint tick, then the scope and the checksum in network
    // order.
    std::array<std::byte, 32> buffer{};
    network::Writer writer{buffer};
    RAWFRAME_EXPECT(
        encodeChecksum(writer, ChecksumRecord{.tick = 300, .scope = 0x0102030405060708ULL, .checksum = 9}).has_value());
    const std::array<std::byte, 18> kExpected = {std::byte{0x41},
                                                 std::byte{0x2c},
                                                 std::byte{1},
                                                 std::byte{2},
                                                 std::byte{3},
                                                 std::byte{4},
                                                 std::byte{5},
                                                 std::byte{6},
                                                 std::byte{7},
                                                 std::byte{8},
                                                 std::byte{0},
                                                 std::byte{0},
                                                 std::byte{0},
                                                 std::byte{0},
                                                 std::byte{0},
                                                 std::byte{0},
                                                 std::byte{0},
                                                 std::byte{9}};
    RAWFRAME_EXPECT(std::ranges::equal(writer.written(), kExpected));
    const auto kRead = decodeChecksum(kExpected);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->tick == 300 && kRead->scope == 0x0102030405060708ULL &&
                    kRead->checksum == 9);
    RAWFRAME_EXPECT(!decodeChecksum(std::span{kExpected}.first(17)).has_value());
}
