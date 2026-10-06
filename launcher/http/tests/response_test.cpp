// Responses read from servers no one vouches for (D414).

#include "rawframe/http/errors.h"
#include "rawframe/http/response.h"
#include "rawframe/test/test.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

using namespace rawframe;

namespace {

std::span<const std::byte> bytesOf(std::string_view text) {
    return std::as_bytes(std::span{text});
}

std::string bodyOf(const http::Response& response) {
    return std::string{reinterpret_cast<const char*>(response.body.data()), response.body.size()};
}

/// Reads `text` whole, one byte at a time when `trickle`.
bool readAll(http::ResponseReader& reader, std::string_view text, bool trickle) {
    std::size_t at = 0;
    while (at < text.size() && !reader.whole()) {
        const std::size_t kSize = trickle ? 1 : text.size() - at;
        const auto kTaken = reader.read(bytesOf(text.substr(at, kSize)));
        if (!kTaken.has_value()) {
            return false;
        }
        at += *kTaken;
    }
    return true;
}

bool refusedAs(std::string_view text, http::HttpError error, std::uint64_t ceiling = 1024) {
    http::ResponseReader reader{ceiling};
    const auto kTaken = reader.read(bytesOf(text));
    return !kTaken.has_value() && kTaken.error().code() == http::code(error);
}

} // namespace

RAWFRAME_TEST(ABodyIsReadByItsLengthByChunksOrToTheEnd) {
    for (const bool kTrickle : {false, true}) {
        http::ResponseReader sized{1024};
        const std::string_view kSized = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhelloNEXT";
        RAWFRAME_EXPECT(readAll(sized, kSized, kTrickle) && sized.whole() && bodyOf(sized.response()) == "hello" &&
                        sized.response().keepAlive);
        http::ResponseReader chunked{1024};
        const std::string_view kChunked = "HTTP/1.1 200 OK\nTransfer-Encoding: Chunked\n\n"
                                          "3;name=value\r\nhel\r\n2\r\nlo\r\n0\r\nTrailer: x\r\n\r\n";
        RAWFRAME_EXPECT(readAll(chunked, kChunked, kTrickle) && chunked.whole() &&
                        bodyOf(chunked.response()) == "hello");
        http::ResponseReader toEnd{1024};
        RAWFRAME_EXPECT(readAll(toEnd, "HTTP/1.0 200 OK\r\n\r\nhel", kTrickle) && !toEnd.whole() &&
                        toEnd.read(bytesOf("lo")).has_value() && toEnd.ended().has_value() &&
                        bodyOf(toEnd.response()) == "hello" && !toEnd.response().keepAlive);
    }
}

RAWFRAME_TEST(TheReaderTakesOnlyItsOwnBytes) {
    http::ResponseReader reader{1024};
    const std::string_view kTwo = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nokHTTP/1.1 200 OK\r\n";
    const auto kTaken = reader.read(bytesOf(kTwo));
    RAWFRAME_EXPECT(kTaken.has_value() && *kTaken == kTwo.find("okHTTP") + 2 && reader.whole());
}

RAWFRAME_TEST(AnInterimAnswerAndAHeadlessOneAreRead) {
    http::ResponseReader interim{1024};
    RAWFRAME_EXPECT(
        readAll(interim, "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", false) &&
        interim.whole() && interim.response().status == 404);
    http::ResponseReader moved{1024};
    RAWFRAME_EXPECT(readAll(moved, "HTTP/1.1 302 Found\r\nLocation: /there\r\nContent-Length: 0\r\n\r\n", false) &&
                    moved.response().location == "/there");
    http::ResponseReader closing{1024};
    RAWFRAME_EXPECT(readAll(closing, "HTTP/1.1 200 OK\r\nConnection: Close\r\nContent-Length: 0\r\n\r\n", false) &&
                    !closing.response().keepAlive);
    http::ResponseReader empty{1024};
    RAWFRAME_EXPECT(readAll(empty, "HTTP/1.1 204 No Content\r\n\r\n", false) && empty.whole());
}

RAWFRAME_TEST(AResponseOutOfFormIsRefused) {
    RAWFRAME_EXPECT(refusedAs("HTTP/2 200 OK\r\n\r\n", http::HttpError::Malformed));
    RAWFRAME_EXPECT(refusedAs("HTTP/1.1 2000 OK\r\n\r\n", http::HttpError::Malformed));
    RAWFRAME_EXPECT(refusedAs("HTTP/1.1 099 Odd\r\n\r\n", http::HttpError::Malformed));
    RAWFRAME_EXPECT(refusedAs("HTTP/1.1 101 Switching\r\n\r\n", http::HttpError::Malformed));
    RAWFRAME_EXPECT(refusedAs("HTTP/1.1 200 OK\r\nNo colon here\r\n\r\n", http::HttpError::Malformed));
    RAWFRAME_EXPECT(refusedAs("HTTP/1.1 200 OK\r\nBad Name: x\r\n\r\n", http::HttpError::Malformed));
    // Said twice, or two ways: the smuggling rule.
    RAWFRAME_EXPECT(
        refusedAs("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n", http::HttpError::Malformed));
    RAWFRAME_EXPECT(refusedAs("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
                              http::HttpError::Malformed));
    RAWFRAME_EXPECT(
        refusedAs("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n", http::HttpError::Malformed));
    RAWFRAME_EXPECT(refusedAs("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n", http::HttpError::Malformed));
    RAWFRAME_EXPECT(
        refusedAs("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n", http::HttpError::Malformed));
    RAWFRAME_EXPECT(
        refusedAs("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nab\r\n", http::HttpError::Malformed));
    RAWFRAME_EXPECT(refusedAs("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nffffffffffffffff\r\n",
                              http::HttpError::Malformed));
    // Headers past 64 KiB, and a chunk size line past 4 KiB.
    RAWFRAME_EXPECT(refusedAs("HTTP/1.1 200 OK\r\nX: " + std::string(70'000, 'a'), http::HttpError::Malformed));
    RAWFRAME_EXPECT(refusedAs("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;" + std::string(5000, 'a'),
                              http::HttpError::Malformed));
    // Once refused, always.
    http::ResponseReader reader{1024};
    RAWFRAME_EXPECT(!reader.read(bytesOf("nonsense\r\n")).has_value() &&
                    !reader.read(bytesOf("HTTP/1.1 200 OK\r\n\r\n")).has_value());
}

RAWFRAME_TEST(ABodyPastItsCeilingIsRefused) {
    RAWFRAME_EXPECT(refusedAs("HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n", http::HttpError::TooLarge, 10));
    RAWFRAME_EXPECT(refusedAs(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n6\r\nabcdef\r\n5\r\n", http::HttpError::TooLarge, 10));
    RAWFRAME_EXPECT(refusedAs("HTTP/1.0 200 OK\r\n\r\n" + std::string(11, 'a'), http::HttpError::TooLarge, 10));
    http::ResponseReader exact{10};
    RAWFRAME_EXPECT(readAll(exact, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789", false) && exact.whole());
}

RAWFRAME_TEST(AResponseCutShortIsInterrupted) {
    http::ResponseReader sized{1024};
    RAWFRAME_EXPECT(sized.read(bytesOf("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhel")).has_value());
    const auto kEnded = sized.ended();
    RAWFRAME_EXPECT(!kEnded.has_value() && kEnded.error().code() == http::code(http::HttpError::Interrupted));
    http::ResponseReader head{1024};
    RAWFRAME_EXPECT(head.read(bytesOf("HTTP/1.1 200 OK\r\n")).has_value() && !head.ended().has_value());
}
