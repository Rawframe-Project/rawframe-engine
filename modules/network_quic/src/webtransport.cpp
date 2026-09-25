#include "webtransport.h"

#include "qpack.h"

#include <algorithm>
#include <string_view>

namespace rawframe::network_quic {

namespace {

// Stream types (RFC 9114, RFC 9204, WebTransport over HTTP/3).
constexpr std::uint64_t kControlStream = 0x00;
constexpr std::uint64_t kWebTransportUnidirectional = 0x54;
// Frame types.
constexpr std::uint64_t kHeadersFrame = 0x01;
constexpr std::uint64_t kSettingsFrame = 0x04;
constexpr std::uint64_t kWebTransportBidirectional = 0x41;
// Settings: extended CONNECT (RFC 9220), HTTP datagrams (RFC 9297), and
// WebTransport, as the draft browsers speak spells it and as the newer one
// counts sessions.
constexpr std::uint64_t kEnableConnectProtocol = 0x08;
constexpr std::uint64_t kH3Datagram = 0x33;
constexpr std::uint64_t kEnableWebTransport = 0x2b603742;
constexpr std::uint64_t kWebTransportMaximumSessions = 0xc671706a;

/// A QUIC variable-length integer, in any of its spellings: HTTP/3 peers
/// need not write the shortest.
struct Varint {
    std::uint64_t value = 0;
    std::size_t size = 0;
};

std::optional<Varint> readVarint(std::span<const std::byte> bytes) noexcept {
    if (bytes.empty()) {
        return std::nullopt;
    }
    const auto kFirst = std::to_integer<std::uint8_t>(bytes[0]);
    const std::size_t kSize = std::size_t{1} << (kFirst >> 6U);
    if (bytes.size() < kSize) {
        return std::nullopt;
    }
    std::uint64_t value = kFirst & 0x3FU;
    for (std::size_t index = 1; index < kSize; ++index) {
        value = (value << 8U) | std::to_integer<std::uint8_t>(bytes[index]);
    }
    return Varint{.value = value, .size = kSize};
}

void pushVarint(std::vector<std::byte>& into, std::uint64_t value) {
    std::size_t size = 1;
    std::uint8_t tag = 0;
    if (value >= (std::uint64_t{1} << 30U)) {
        size = 8;
        tag = 3;
    } else if (value >= (std::uint64_t{1} << 14U)) {
        size = 4;
        tag = 2;
    } else if (value >= (std::uint64_t{1} << 6U)) {
        size = 2;
        tag = 1;
    }
    for (std::size_t index = 0; index < size; ++index) {
        auto byte = static_cast<std::uint8_t>(value >> (8 * (size - 1 - index)));
        if (index == 0) {
            byte = static_cast<std::uint8_t>(byte | (tag << 6U));
        }
        into.push_back(static_cast<std::byte>(byte));
    }
}

/// One whole frame at the start of `bytes`: its type, its payload, and how
/// much it took. Nothing yet if it is not all there.
struct Frame {
    std::uint64_t type = 0;
    std::span<const std::byte> payload;
    std::size_t size = 0;
};

enum class FrameRead : std::uint8_t {
    Whole,
    Partial,
    TooLarge
};

FrameRead readFrame(std::span<const std::byte> bytes, std::size_t largest, Frame& frame) noexcept {
    const auto kType = readVarint(bytes);
    const auto kLength = kType ? readVarint(bytes.subspan(kType->size)) : std::nullopt;
    if (!kLength) {
        return FrameRead::Partial;
    }
    if (kLength->value > largest) {
        return FrameRead::TooLarge;
    }
    const std::size_t kHeader = kType->size + kLength->size;
    if (bytes.size() - kHeader < kLength->value) {
        return FrameRead::Partial;
    }
    frame = Frame{.type = kType->value,
                  .payload = bytes.subspan(kHeader, static_cast<std::size_t>(kLength->value)),
                  .size = kHeader + static_cast<std::size_t>(kLength->value)};
    return FrameRead::Whole;
}

std::vector<std::byte> frameOf(std::uint64_t type, std::span<const std::byte> payload) {
    std::vector<std::byte> frame;
    pushVarint(frame, type);
    pushVarint(frame, payload.size());
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

/// Whether a request is an extended CONNECT for WebTransport over https,
/// with an authority and a path.
bool webTransportRequest(const std::vector<FieldLine>& lines) {
    const auto kValue = [&lines](std::string_view name) -> std::optional<std::string_view> {
        const auto kFound = std::ranges::find(lines, name, &FieldLine::name);
        return kFound == lines.end() ? std::nullopt : std::optional<std::string_view>{kFound->value};
    };
    return kValue(":method") == "CONNECT" && kValue(":protocol") == "webtransport" && kValue(":scheme") == "https" &&
           kValue(":authority").has_value() && !kValue(":authority")->empty() && kValue(":path").has_value() &&
           !kValue(":path")->empty();
}

bool clientBidirectional(std::uint64_t stream) noexcept {
    return (stream & 3U) == 0;
}

bool clientUnidirectional(std::uint64_t stream) noexcept {
    return (stream & 3U) == 2;
}

} // namespace

std::vector<std::byte> WebTransportServer::controlStream() {
    std::vector<std::byte> settings;
    for (const auto& [kKey, kValue] : {std::pair{kEnableConnectProtocol, std::uint64_t{1}},
                                       std::pair{kH3Datagram, std::uint64_t{1}},
                                       std::pair{kEnableWebTransport, std::uint64_t{1}},
                                       std::pair{kWebTransportMaximumSessions, std::uint64_t{1}}}) {
        pushVarint(settings, kKey);
        pushVarint(settings, kValue);
    }
    std::vector<std::byte> stream;
    pushVarint(stream, kControlStream);
    const std::vector<std::byte> kFrame = frameOf(kSettingsFrame, settings);
    stream.insert(stream.end(), kFrame.begin(), kFrame.end());
    return stream;
}

WebTransportServer::Arrived
WebTransportServer::receive(std::uint64_t stream, std::span<const std::byte> bytes, bool finished) {
    Arrived arrived;
    // Streams this side opened are the session's: the peer's answers on them
    // are the owner's bytes as they come.
    if (!clientBidirectional(stream) && !clientUnidirectional(stream)) {
        arrived.bytes.assign(bytes.begin(), bytes.end());
        return arrived;
    }
    StreamState& state = streams_[stream];
    if (state.phase == Phase::Owner) {
        arrived.bytes.assign(bytes.begin(), bytes.end());
    } else if (state.phase != Phase::Ignored) {
        state.pending.insert(state.pending.end(), bytes.begin(), bytes.end());
        if (state.phase == Phase::Start) {
            start(stream, state, arrived);
        }
        if (state.phase == Phase::Control) {
            control(state, arrived);
        }
        if (state.phase == Phase::Session) {
            // Capsules, which this side does not act on: the session ends
            // when its stream does.
            state.pending.clear();
        }
        if (state.phase == Phase::Owner && !state.pending.empty()) {
            arrived.bytes = std::move(state.pending);
            state.pending.clear();
        }
        if (state.phase == Phase::Ignored) {
            state.pending.clear();
        } else if (state.pending.size() > kLargestRequest) {
            arrived.failure = kH3ExcessiveLoad;
        }
    }
    if (finished) {
        if (session_ == stream && !closed_) {
            closed_ = true;
            arrived.closed = true;
        }
        if (state.phase == Phase::Control) {
            // The control stream must not end (RFC 9114 6.2.1).
            arrived.failure = kH3StreamCreationError;
        }
        streams_.erase(stream);
    }
    return arrived;
}

void WebTransportServer::start(std::uint64_t stream, StreamState& state, Arrived& arrived) {
    const auto kType = readVarint(state.pending);
    if (!kType) {
        return;
    }
    if (clientUnidirectional(stream)) {
        if (kType->value == kControlStream) {
            if (controlSeen_) {
                arrived.failure = kH3StreamCreationError;
                return;
            }
            controlSeen_ = true;
            state.pending.erase(state.pending.begin(),
                                state.pending.begin() + static_cast<std::ptrdiff_t>(kType->size));
            state.phase = Phase::Control;
            return;
        }
        if (kType->value != kWebTransportUnidirectional) {
            // QPACK's streams, and types nobody here knows: read and dropped.
            state.phase = Phase::Ignored;
            return;
        }
    } else if (kType->value != kWebTransportBidirectional) {
        request(stream, state, arrived);
        return;
    }
    // A stream of a session: the session it names must be this one.
    const auto kSession = readVarint(std::span{state.pending}.subspan(kType->size));
    if (!kSession) {
        return;
    }
    if (!open() || kSession->value != *session_) {
        state.phase = Phase::Ignored;
        return;
    }
    state.pending.erase(state.pending.begin(),
                        state.pending.begin() + static_cast<std::ptrdiff_t>(kType->size + kSession->size));
    state.phase = Phase::Owner;
}

void WebTransportServer::control(StreamState& state, Arrived& arrived) {
    Frame frame;
    while (true) {
        const FrameRead kRead = readFrame(state.pending, kLargestRequest, frame);
        if (kRead == FrameRead::TooLarge) {
            arrived.failure = kH3ExcessiveLoad;
            return;
        }
        if (kRead == FrameRead::Partial) {
            return;
        }
        if (!state.settingsSeen && frame.type != kSettingsFrame) {
            arrived.failure = kH3MissingSettings;
            return;
        }
        if (state.settingsSeen && frame.type == kSettingsFrame) {
            arrived.failure = kH3FrameUnexpected;
            return;
        }
        if (frame.type == kSettingsFrame) {
            // Every setting must at least parse; none changes what this side
            // sends, since it announces no dynamic table and needs nothing
            // of the peer's.
            std::span<const std::byte> rest = frame.payload;
            while (!rest.empty()) {
                const auto kKey = readVarint(rest);
                const auto kValue = kKey ? readVarint(rest.subspan(kKey->size)) : std::nullopt;
                if (!kValue) {
                    arrived.failure = kH3FrameError;
                    return;
                }
                rest = rest.subspan(kKey->size + kValue->size);
            }
            state.settingsSeen = true;
        }
        state.pending.erase(state.pending.begin(), state.pending.begin() + static_cast<std::ptrdiff_t>(frame.size));
    }
}

void WebTransportServer::request(std::uint64_t stream, StreamState& state, Arrived& arrived) {
    Frame frame;
    const FrameRead kRead = readFrame(state.pending, kLargestRequest, frame);
    if (kRead == FrameRead::TooLarge) {
        arrived.failure = kH3ExcessiveLoad;
        return;
    }
    if (kRead == FrameRead::Partial) {
        return;
    }
    if (frame.type != kHeadersFrame) {
        arrived.failure = kH3FrameUnexpected;
        return;
    }
    const auto kLines = decodeFieldSection(frame.payload, kLargestRequest);
    std::uint16_t status = 200;
    if (!kLines || !webTransportRequest(*kLines)) {
        status = 400;
    } else if (session_.has_value()) {
        // One session a connection.
        status = 429;
    }
    const std::vector<std::byte> kSection = encodeStatus(status);
    arrived.reply = frameOf(kHeadersFrame, kSection);
    state.pending.erase(state.pending.begin(), state.pending.begin() + static_cast<std::ptrdiff_t>(frame.size));
    if (status != 200) {
        state.phase = Phase::Ignored;
        return;
    }
    session_ = stream;
    arrived.opened = true;
    state.phase = Phase::Session;
}

std::optional<std::span<const std::byte>> WebTransportServer::datagram(std::span<const std::byte> datagram) const {
    const auto kQuarter = readVarint(datagram);
    if (!kQuarter || !open() || kQuarter->value * 4 != *session_) {
        return std::nullopt;
    }
    return datagram.subspan(kQuarter->size);
}

std::vector<std::byte> WebTransportServer::streamPreface(bool unidirectional) const {
    std::vector<std::byte> preface;
    pushVarint(preface, unidirectional ? kWebTransportUnidirectional : kWebTransportBidirectional);
    pushVarint(preface, session_.value_or(0));
    return preface;
}

std::vector<std::byte> WebTransportServer::datagramPrefix() const {
    std::vector<std::byte> prefix;
    pushVarint(prefix, session_.value_or(0) / 4);
    return prefix;
}

} // namespace rawframe::network_quic
