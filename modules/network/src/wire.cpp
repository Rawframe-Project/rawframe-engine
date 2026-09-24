#include "rawframe/network/wire.h"

#include "rawframe/network/errors.h"

#include <cstring>

namespace rawframe::network {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, NetworkError error, std::string_view why) {
    return result::fail(errorClass, kNetworkDomain, code(error), why);
}

std::unexpected<result::Error> truncated() {
    return refuse(result::ErrorClass::InvalidArgument, NetworkError::Truncated, "the input ends inside a value");
}

} // namespace

result::Result<std::uint64_t> Reader::varint() {
    if (at_ == input_.size()) {
        return truncated();
    }
    const auto kFirst = std::to_integer<std::uint8_t>(input_[at_]);
    // The top two bits say the length: 1, 2, 4, or 8 bytes.
    const std::size_t kLength = std::size_t{1} << (kFirst >> 6U);
    if (input_.size() - at_ < kLength) {
        return truncated();
    }
    std::uint64_t value = kFirst & 0x3FU;
    for (std::size_t index = 1; index < kLength; ++index) {
        value = (value << 8U) | std::to_integer<std::uint8_t>(input_[at_ + index]);
    }
    if (varintSize(value) != kLength) {
        return refuse(
            result::ErrorClass::InvalidArgument, NetworkError::NonCanonical, "a varint is longer than its value needs");
    }
    at_ += kLength;
    return value;
}

result::Result<std::uint64_t> Reader::varintAtMost(std::uint64_t maximum) {
    const std::size_t kStart = at_;
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kValue, varint());
    if (kValue > maximum) {
        at_ = kStart;
        return refuse(result::ErrorClass::InvalidArgument, NetworkError::Oversized, "a value exceeds its limit");
    }
    return kValue;
}

result::Result<std::span<const std::byte>> Reader::bytes(std::uint64_t count) {
    if (count > remaining()) {
        return truncated();
    }
    const auto kTaken = input_.subspan(at_, static_cast<std::size_t>(count));
    at_ += static_cast<std::size_t>(count);
    return kTaken;
}

result::Status Writer::room(std::size_t count) {
    if (failed_ || output_.size() - at_ < count) {
        failed_ = true;
        return refuse(result::ErrorClass::ResourceExhausted, NetworkError::BufferFull, "the output has no room left");
    }
    return {};
}

result::Status Writer::varint(std::uint64_t value) {
    if (value > kMaximumVarint) {
        failed_ = true;
        return refuse(result::ErrorClass::InvalidArgument, NetworkError::Oversized, "a value exceeds 2^62 - 1");
    }
    const std::size_t kLength = varintSize(value);
    RAWFRAME_TRY(room(kLength));
    // The length's two bits sit above the value's most significant byte.
    constexpr std::uint8_t kLengthBits[] = {0x00, 0x40, 0x00, 0x80, 0x00, 0x00, 0x00, 0xC0};
    for (std::size_t index = 0; index < kLength; ++index) {
        const auto kByte = static_cast<std::uint8_t>(value >> (8U * (kLength - 1U - index)));
        output_[at_ + index] = static_cast<std::byte>(index == 0 ? kByte | kLengthBits[kLength - 1] : kByte);
    }
    at_ += kLength;
    return {};
}

result::Status Writer::bytes(std::span<const std::byte> data) {
    RAWFRAME_TRY(room(data.size()));
    if (!data.empty()) {
        std::memcpy(output_.data() + at_, data.data(), data.size());
    }
    at_ += data.size();
    return {};
}

result::Result<StreamPreface> readPreface(Reader& reader) {
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kKind, reader.varint());
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kVersion, reader.varint());
    if (kKind > static_cast<std::uint64_t>(StreamKind::Baseline)) {
        return refuse(result::ErrorClass::InvalidArgument, NetworkError::UnknownCritical, "an unknown stream kind");
    }
    if (kVersion != kStreamFormatVersion) {
        return refuse(
            result::ErrorClass::InvalidArgument, NetworkError::WrongVersion, "a stream of another format version");
    }
    StreamPreface preface{.kind = static_cast<StreamKind>(kKind)};
    if (preface.kind == StreamKind::EventLane) {
        RAWFRAME_TRY_ASSIGN(preface.eventLaneId, reader.varint());
        RAWFRAME_TRY_ASSIGN(preface.eventLaneEpoch, reader.varint());
    } else if (preface.kind == StreamKind::Baseline) {
        RAWFRAME_TRY_ASSIGN(preface.baselineSequence, reader.varint());
        RAWFRAME_TRY_ASSIGN(preface.replicationEpoch, reader.varint());
    }
    return preface;
}

result::Status writePreface(Writer& writer, const StreamPreface& preface) {
    RAWFRAME_TRY(writer.varint(static_cast<std::uint64_t>(preface.kind)));
    RAWFRAME_TRY(writer.varint(kStreamFormatVersion));
    if (preface.kind == StreamKind::EventLane) {
        RAWFRAME_TRY(writer.varint(preface.eventLaneId));
        RAWFRAME_TRY(writer.varint(preface.eventLaneEpoch));
    } else if (preface.kind == StreamKind::Baseline) {
        RAWFRAME_TRY(writer.varint(preface.baselineSequence));
        RAWFRAME_TRY(writer.varint(preface.replicationEpoch));
    }
    return {};
}

result::Result<Frame> readFrame(Reader& reader, std::size_t maximumPayload) {
    // A frame is taken whole or not at all, so a stream can wait for more.
    Reader attempt = reader;
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kType, attempt.varint());
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kLength, attempt.varintAtMost(maximumPayload));
    RAWFRAME_TRY_ASSIGN(const std::span<const std::byte> kPayload, attempt.bytes(kLength));
    reader = attempt;
    return Frame{.type = kType, .payload = kPayload};
}

result::Status writeFrame(Writer& writer, std::uint64_t type, std::span<const std::byte> payload) {
    RAWFRAME_TRY(writer.varint(type));
    RAWFRAME_TRY(writer.varint(payload.size()));
    return writer.bytes(payload);
}

result::Result<DatagramRecord> readDatagram(std::span<const std::byte> datagram, std::size_t maximumPayload) {
    Reader reader{datagram};
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kLane, reader.varint());
    if (kLane > static_cast<std::uint64_t>(DatagramLane::State)) {
        return refuse(result::ErrorClass::InvalidArgument, NetworkError::UnknownCritical, "an unknown datagram lane");
    }
    DatagramRecord record{.lane = static_cast<DatagramLane>(kLane)};
    RAWFRAME_TRY_ASSIGN(record.laneEpoch, reader.varint());
    RAWFRAME_TRY_ASSIGN(record.sequence, reader.varint());
    RAWFRAME_TRY_ASSIGN(record.payloadType, reader.varint());
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kLength, reader.varintAtMost(maximumPayload));
    RAWFRAME_TRY_ASSIGN(record.payload, reader.bytes(kLength));
    if (reader.remaining() != 0) {
        return refuse(
            result::ErrorClass::InvalidArgument, NetworkError::TrailingBytes, "a datagram continues past its record");
    }
    return record;
}

result::Status writeDatagram(Writer& writer, const DatagramRecord& record) {
    RAWFRAME_TRY(writer.varint(static_cast<std::uint64_t>(record.lane)));
    RAWFRAME_TRY(writer.varint(record.laneEpoch));
    RAWFRAME_TRY(writer.varint(record.sequence));
    RAWFRAME_TRY(writer.varint(record.payloadType));
    RAWFRAME_TRY(writer.varint(record.payload.size()));
    return writer.bytes(record.payload);
}

} // namespace rawframe::network
