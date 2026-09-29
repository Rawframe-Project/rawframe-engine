#include "rawframe/network/close.h"

#include "rawframe/network/errors.h"

namespace rawframe::network {

result::Status encodeGracefulClose(Writer& writer, CloseNotice notice) {
    return writer.varint(static_cast<std::uint64_t>(notice));
}

result::Result<CloseNotice> decodeGracefulClose(std::span<const std::byte> payload) {
    Reader reader{payload};
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kNotice, reader.varint());
    if (kNotice == 0 || kNotice > kLastCloseNotice) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kNetworkDomain,
                            code(NetworkError::Malformed),
                            "a graceful close gives no notice this generation knows");
    }
    if (reader.remaining() != 0) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kNetworkDomain,
                            code(NetworkError::TrailingBytes),
                            "a graceful close continues past its last field");
    }
    return static_cast<CloseNotice>(kNotice);
}

result::Status encodeTermination(Writer& writer, const Termination& termination) {
    if (termination.note.size() > kMaximumTerminationNote) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kNetworkDomain,
                            code(NetworkError::TooLarge),
                            "a termination's note is at most 256 bytes");
    }
    RAWFRAME_TRY(writer.varint(static_cast<std::uint64_t>(termination.reason)));
    RAWFRAME_TRY(writer.varint(termination.note.size()));
    return writer.bytes(std::as_bytes(std::span{termination.note}));
}

result::Result<Termination> decodeTermination(std::span<const std::byte> payload) {
    Reader reader{payload};
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kReason, reader.varint());
    if (kReason == 0 || kReason > kLastTerminationReason) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kNetworkDomain,
                            code(NetworkError::Malformed),
                            "a termination gives no reason this generation knows");
    }
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kLength, reader.varintAtMost(kMaximumTerminationNote));
    RAWFRAME_TRY_ASSIGN(const std::span<const std::byte> kNote, reader.bytes(kLength));
    if (reader.remaining() != 0) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kNetworkDomain,
                            code(NetworkError::TrailingBytes),
                            "a termination continues past its last field");
    }
    return Termination{.reason = static_cast<TerminationReason>(kReason),
                       .note = std::string{reinterpret_cast<const char*>(kNote.data()), kNote.size()}};
}

} // namespace rawframe::network
