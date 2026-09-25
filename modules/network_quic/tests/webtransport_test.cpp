// A browser's HTTP/3 as the server reads it (D172): Huffman strings against
// RFC 7541's examples, field sections an independent QPACK encoder wrote,
// the session's opening, streams, datagrams, and ending, the refusals, and
// hostile bytes on every kind of stream.

#include "qpack.h"
#include "rawframe/test/test.h"
#include "webtransport.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace rawframe;
using network_quic::WebTransportServer;

namespace {

std::vector<std::byte> hex(std::string_view text) {
    std::vector<std::byte> bytes;
    for (std::size_t at = 0; at + 1 < text.size(); at += 2) {
        bytes.push_back(static_cast<std::byte>(std::stoi(std::string{text.substr(at, 2)}, nullptr, 16)));
    }
    return bytes;
}

std::vector<std::byte> text(std::string_view characters) {
    const auto kBytes = std::as_bytes(std::span{characters.data(), characters.size()});
    return {kBytes.begin(), kBytes.end()};
}

std::vector<std::byte> join(std::initializer_list<std::vector<std::byte>> parts) {
    std::vector<std::byte> joined;
    for (const auto& part : parts) {
        joined.insert(joined.end(), part.begin(), part.end());
    }
    return joined;
}

/// SplitMix64: the fuzzing's bytes, the same every run.
struct Random {
    std::uint64_t state;
    std::uint64_t next() noexcept {
        std::uint64_t value = (state += 0x9e3779b97f4a7c15U);
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9U;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebU;
        return value ^ (value >> 31U);
    }
};

/// A frame: its type as a one-byte varint, its length as the shortest.
std::vector<std::byte> frame(std::uint8_t type, const std::vector<std::byte>& payload) {
    std::vector<std::byte> length{static_cast<std::byte>(payload.size())};
    if (payload.size() >= 64) {
        length = {static_cast<std::byte>(0x40U | (payload.size() >> 8U)),
                  static_cast<std::byte>(payload.size() & 0xFFU)};
    }
    return join({{static_cast<std::byte>(type)}, length, payload});
}

// Written by pylsqpack (an independent QPACK encoder) with no dynamic table:
// a browser's WebTransport CONNECT, with Huffman-coded literals and a
// literal name, and a plain GET.
constexpr std::string_view kConnect =
    "0000cf2f00b95d8749c87a3f89f058d360ea4567b13fd75090ae81fa5cbe474d7415721e9b8d34cb3f518762c1f896c1d25f5f4b929d29ad"
    "171862ba07e972f91d35d055c87a7f2f0e4148b782c69b07522b3d895a74a6b65692c1ca900b0131";
constexpr std::string_view kGet = "0000d1d7500161c1";

/// The client's control stream: its type and SETTINGS enabling WebTransport.
std::vector<std::byte> clientControl() {
    return join({hex("00"),
                 frame(0x04,
                       hex("0801"
                           "3301"
                           "80"
                           "2b603742"
                           "01"))});
}

/// The session opened on stream 0, the control stream having said SETTINGS.
WebTransportServer opened() {
    WebTransportServer server;
    RAWFRAME_EXPECT(!server.receive(2, clientControl(), false).failure);
    const auto kArrived = server.receive(0, frame(0x01, hex(kConnect)), false);
    RAWFRAME_EXPECT(kArrived.opened && !kArrived.failure && server.open());
    return server;
}

} // namespace

RAWFRAME_TEST(HuffmanStringsDecodeAsRfc7541Says) {
    const auto kDecode = [](std::string_view coded) {
        return network_quic::decodeHuffman(hex(coded), 64);
    };
    RAWFRAME_EXPECT(kDecode("f1e3c2e5f23a6ba0ab90f4ff") == "www.example.com");
    RAWFRAME_EXPECT(kDecode("a8eb10649cbf") == "no-cache");
    RAWFRAME_EXPECT(kDecode("25a849e95ba97d7f") == "custom-key");
    RAWFRAME_EXPECT(kDecode("25a849e95bb8e8b4bf") == "custom-value");
    RAWFRAME_EXPECT(kDecode("") == "");
    // Padding that is not all ones, a whole byte of it, and EOS itself.
    RAWFRAME_EXPECT(!kDecode("a8eb10649cbe"));
    RAWFRAME_EXPECT(!kDecode("a8eb10649cbfff"));
    RAWFRAME_EXPECT(!kDecode("fffffffc"));
    RAWFRAME_EXPECT(!network_quic::decodeHuffman(hex("f1e3c2e5f23a6ba0ab90f4ff"), 14));
}

RAWFRAME_TEST(FieldSectionsDecodeWhatAnotherEncoderWrote) {
    const auto kLines = network_quic::decodeFieldSection(hex(kConnect), 4096);
    RAWFRAME_EXPECT(kLines.has_value() && kLines->size() == 7);
    if (kLines.has_value() && kLines->size() == 7) {
        const std::vector<std::pair<std::string_view, std::string_view>> kExpected = {
            {":method", "CONNECT"},
            {":protocol", "webtransport"},
            {":scheme", "https"},
            {":authority", "play.example.com:4433"},
            {":path", "/rawframe"},
            {"origin", "https://play.example.com"},
            {"sec-webtransport-http3-draft02", "1"}};
        for (std::size_t index = 0; index < kExpected.size(); ++index) {
            RAWFRAME_EXPECT((*kLines)[index].name == kExpected[index].first &&
                            (*kLines)[index].value == kExpected[index].second);
        }
    }
    const auto kGetLines = network_quic::decodeFieldSection(hex(kGet), 4096);
    RAWFRAME_EXPECT(kGetLines.has_value() && kGetLines->size() == 4 && (*kGetLines)[0].value == "GET");
    // A dynamic table, a Required Insert Count, an index past the static
    // table, a string past the end, and more than the budget: refused.
    RAWFRAME_EXPECT(!network_quic::decodeFieldSection(hex("000080"), 4096));
    RAWFRAME_EXPECT(!network_quic::decodeFieldSection(hex("0100d1"), 4096));
    RAWFRAME_EXPECT(!network_quic::decodeFieldSection(hex("0000ff25"), 4096));
    RAWFRAME_EXPECT(!network_quic::decodeFieldSection(hex("00002f0561"), 4096));
    RAWFRAME_EXPECT(!network_quic::decodeFieldSection(hex(kConnect), 40));
    // Its answers: 200 indexed, anything else a literal the decoder reads.
    RAWFRAME_EXPECT(network_quic::encodeStatus(200) == hex("0000d9"));
    const auto kRefusal = network_quic::decodeFieldSection(network_quic::encodeStatus(429), 64);
    RAWFRAME_EXPECT(kRefusal.has_value() && kRefusal->size() == 1 && (*kRefusal)[0].name == ":status" &&
                    (*kRefusal)[0].value == "429");
}

RAWFRAME_TEST(ASessionOpensAndCarriesTheOwnersBytes) {
    WebTransportServer server;
    // QPACK's encoder stream is read and dropped; the control stream says
    // SETTINGS; this side's own control stream says what it speaks.
    RAWFRAME_EXPECT(!server.receive(6, hex("02"), false).failure);
    RAWFRAME_EXPECT(!server.receive(2, clientControl(), false).failure);
    RAWFRAME_EXPECT(WebTransportServer::controlStream().front() == std::byte{0x00});
    // The CONNECT, a byte at a time: answered 200 once whole.
    const std::vector<std::byte> kRequest = frame(0x01, hex(kConnect));
    WebTransportServer::Arrived arrived;
    for (const std::byte kByte : kRequest) {
        arrived = server.receive(0, std::span{&kByte, 1}, false);
    }
    RAWFRAME_EXPECT(arrived.opened && arrived.reply == frame(0x01, hex("0000d9")) && server.open());
    // A two-way stream of the session, its preface (0x41 is a two-byte
    // varint) split: only what follows is the owner's.
    RAWFRAME_EXPECT(server.receive(4, hex("40"), false).bytes.empty());
    RAWFRAME_EXPECT(server.receive(4, join({hex("4100"), text("hel")}), false).bytes == text("hel"));
    RAWFRAME_EXPECT(server.receive(4, text("lo"), false).bytes == text("lo"));
    // A one-way stream, a stream this side opened, and datagrams.
    RAWFRAME_EXPECT(server.receive(10, join({hex("405400"), text("x")}), false).bytes == text("x"));
    RAWFRAME_EXPECT(server.receive(1, text("back"), false).bytes == text("back"));
    const std::vector<std::byte> kSent = join({hex("00"), text("d")});
    const auto kDatagram = server.datagram(kSent);
    RAWFRAME_EXPECT(kDatagram.has_value() && std::ranges::equal(*kDatagram, text("d")));
    RAWFRAME_EXPECT(!server.datagram(join({hex("01"), text("d")})));
    RAWFRAME_EXPECT(server.streamPreface(false) == hex("404100") && server.streamPreface(true) == hex("405400") &&
                    server.datagramPrefix() == hex("00"));
    // A stream naming another session is dropped.
    RAWFRAME_EXPECT(server.receive(8, join({hex("404104"), text("no")}), false).bytes.empty());
    // The session's stream ends: so does the session.
    const auto kEnded = server.receive(0, {}, true);
    RAWFRAME_EXPECT(kEnded.closed && !server.open() && !server.datagram(join({hex("00"), text("d")})));
}

RAWFRAME_TEST(WhatIsNotASessionIsRefused) {
    // A second session, and a request that is not WebTransport.
    WebTransportServer server = opened();
    const auto kSecond = server.receive(4, frame(0x01, hex(kConnect)), false);
    RAWFRAME_EXPECT(!kSecond.opened && !kSecond.failure && kSecond.reply.size() > 3);
    WebTransportServer plain;
    const auto kGetArrived = plain.receive(0, frame(0x01, hex(kGet)), false);
    RAWFRAME_EXPECT(!kGetArrived.opened && !plain.open());
    const auto kStatus = network_quic::decodeFieldSection(std::span{kGetArrived.reply}.subspan(2), 64);
    RAWFRAME_EXPECT(kStatus.has_value() && (*kStatus)[0].value == "400");
    // A request stream starting with DATA, a control stream without SETTINGS
    // first, a second control stream, a control stream ending, and a frame
    // larger than any request.
    RAWFRAME_EXPECT(WebTransportServer{}.receive(0, frame(0x00, text("x")), false).failure ==
                    network_quic::kH3FrameUnexpected);
    RAWFRAME_EXPECT(WebTransportServer{}.receive(2, join({hex("00"), frame(0x07, hex("00"))}), false).failure ==
                    network_quic::kH3MissingSettings);
    WebTransportServer twice;
    RAWFRAME_EXPECT(!twice.receive(2, clientControl(), false).failure);
    RAWFRAME_EXPECT(twice.receive(6, hex("00"), false).failure == network_quic::kH3StreamCreationError);
    RAWFRAME_EXPECT(twice.receive(2, {}, true).failure == network_quic::kH3StreamCreationError);
    RAWFRAME_EXPECT(WebTransportServer{}.receive(0, hex("01bfffffff"), false).failure ==
                    network_quic::kH3ExcessiveLoad);
}

RAWFRAME_TEST(HostileBytesOnEveryStreamAreSurvived) {
    // Random bytes in random pieces on the kinds of stream a peer can open,
    // after a session and without one: every delivery answers, nothing it
    // holds grows past a request, and no crash (the sanitizers watch).
    Random random{0x5eed'0172};
    for (int round = 0; round < 200; ++round) {
        WebTransportServer server = round % 2 == 0 ? opened() : WebTransportServer{};
        for (int delivery = 0; delivery < 50; ++delivery) {
            const std::uint64_t kStream = (random.next() % 16) * 4 + (random.next() % 2) * 2;
            std::vector<std::byte> bytes(random.next() % 64);
            for (std::byte& byte : bytes) {
                byte = static_cast<std::byte>(random.next());
            }
            const auto kArrived = server.receive(kStream, bytes, random.next() % 8 == 0);
            RAWFRAME_EXPECT(kArrived.bytes.size() <= bytes.size() + WebTransportServer::kLargestRequest);
            static_cast<void>(server.datagram(bytes));
        }
    }
    // A field section of random bytes decodes or is refused, never more.
    for (int round = 0; round < 2000; ++round) {
        std::vector<std::byte> section(random.next() % 48);
        for (std::byte& byte : section) {
            byte = static_cast<std::byte>(random.next());
        }
        const auto kLines = network_quic::decodeFieldSection(section, 256);
        if (kLines.has_value()) {
            std::size_t total = 0;
            for (const auto& line : *kLines) {
                total += line.name.size() + line.value.size();
            }
            RAWFRAME_EXPECT(total <= 256);
        }
    }
}
