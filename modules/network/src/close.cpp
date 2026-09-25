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

} // namespace rawframe::network
