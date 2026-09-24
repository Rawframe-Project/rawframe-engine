#include "rawframe/world_replication/records.h"

#include "rawframe/world_replication/errors.h"

#include <limits>

namespace rawframe::world_replication {

namespace {

std::unexpected<result::Error> malformed(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kReplicationDomain, code(ReplicationError::Malformed), why);
}

result::Result<NetEntityId> readEntity(network::Reader& reader) {
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kValue, reader.varintAtMost(std::numeric_limits<std::uint32_t>::max()));
    if (kValue == 0) {
        return malformed("net entity zero names nothing");
    }
    return NetEntityId{static_cast<std::uint32_t>(kValue)};
}

} // namespace

result::Status encodeMapping(network::Writer& writer, const MappingRecord& record) {
    RAWFRAME_TRY(writer.varint(record.replicationEpoch));
    RAWFRAME_TRY(writer.varint(record.entity.value));
    return writer.varint(record.owned ? 1 : 0);
}

result::Result<MappingRecord> decodeMapping(std::span<const std::byte> payload) {
    network::Reader reader{payload};
    MappingRecord record;
    RAWFRAME_TRY_ASSIGN(record.replicationEpoch, reader.varint());
    RAWFRAME_TRY_ASSIGN(record.entity, readEntity(reader));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kOwned, reader.varintAtMost(1));
    record.owned = kOwned == 1;
    if (record.replicationEpoch == 0 || reader.remaining() != 0) {
        return malformed("a mapping record has a zero epoch or bytes past its end");
    }
    return record;
}

result::Status encodeStateHeader(network::Writer& writer, const StateHeader& header) {
    RAWFRAME_TRY(writer.varint(header.serverTick));
    RAWFRAME_TRY(writer.varint(header.consumedInputTick));
    return writer.varint(header.recordCount);
}

result::Result<StateHeader> decodeStateHeader(network::Reader& reader) {
    StateHeader header;
    RAWFRAME_TRY_ASSIGN(header.serverTick, reader.varint());
    RAWFRAME_TRY_ASSIGN(header.consumedInputTick, reader.varint());
    // Every record is at least two bytes, which bounds the count by what is
    // left before anything is read for it.
    RAWFRAME_TRY_ASSIGN(header.recordCount, reader.varintAtMost(reader.remaining() / 2));
    return header;
}

result::Status encodeStateRecordHead(network::Writer& writer, const StateRecordHead& head) {
    RAWFRAME_TRY(writer.varint(head.entity.value));
    return writer.varint(head.component);
}

result::Result<StateRecordHead> decodeStateRecordHead(network::Reader& reader, std::size_t componentCount) {
    StateRecordHead head;
    RAWFRAME_TRY_ASSIGN(head.entity, readEntity(reader));
    RAWFRAME_TRY_ASSIGN(head.component, reader.varint());
    if (head.component >= componentCount) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kReplicationDomain,
                            code(ReplicationError::UnknownComponent),
                            "a state record names a component outside the replication table");
    }
    return head;
}

result::Status encodeInputWindow(network::Writer& writer, const InputWindow& window) {
    if (window.commands.empty() || window.commands.size() > kMaximumInputWindow ||
        window.newestInputTick + 1 < window.commands.size()) {
        return malformed("an input window carries one to kMaximumInputWindow commands, all at real ticks");
    }
    RAWFRAME_TRY(writer.varint(window.newestInputTick));
    RAWFRAME_TRY(writer.varint(window.commands.size()));
    RAWFRAME_TRY(writer.varint(window.ackedStateSequence));
    RAWFRAME_TRY(writer.varint(window.ackedServerTick));
    for (const std::span<const std::byte> kCommand : window.commands) {
        if (kCommand.size() > kMaximumInputCommand) {
            return malformed("an input command is over its bound");
        }
        RAWFRAME_TRY(writer.varint(kCommand.size()));
        RAWFRAME_TRY(writer.bytes(kCommand));
    }
    return {};
}

result::Result<InputWindow> decodeInputWindow(std::span<const std::byte> payload) {
    network::Reader reader{payload};
    InputWindow window;
    RAWFRAME_TRY_ASSIGN(window.newestInputTick, reader.varint());
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kCount, reader.varintAtMost(kMaximumInputWindow));
    RAWFRAME_TRY_ASSIGN(window.ackedStateSequence, reader.varint());
    RAWFRAME_TRY_ASSIGN(window.ackedServerTick, reader.varint());
    if (kCount == 0 || window.newestInputTick + 1 < kCount) {
        return malformed("an input window carries at least one command, all at real ticks");
    }
    for (std::uint64_t index = 0; index < kCount; ++index) {
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kLength, reader.varintAtMost(kMaximumInputCommand));
        RAWFRAME_TRY_ASSIGN(const std::span<const std::byte> kCommand, reader.bytes(kLength));
        window.commands.push_back(kCommand);
    }
    if (reader.remaining() != 0) {
        return malformed("an input window has bytes past its last command");
    }
    return window;
}

} // namespace rawframe::world_replication
