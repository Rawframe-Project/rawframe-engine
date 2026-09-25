#include "qpack.h"

#include <array>
#include <string_view>

namespace rawframe::network_quic {

namespace {

struct StaticField {
    std::string_view name;
    std::string_view value;
};

// RFC 9204 Appendix A, checked entry by entry against an independent
// encoder when written (D172).
constexpr std::array<StaticField, 99> kStaticTable = {
    StaticField{":authority", ""},
    StaticField{":path", "/"},
    StaticField{"age", "0"},
    StaticField{"content-disposition", ""},
    StaticField{"content-length", "0"},
    StaticField{"cookie", ""},
    StaticField{"date", ""},
    StaticField{"etag", ""},
    StaticField{"if-modified-since", ""},
    StaticField{"if-none-match", ""},
    StaticField{"last-modified", ""},
    StaticField{"link", ""},
    StaticField{"location", ""},
    StaticField{"referer", ""},
    StaticField{"set-cookie", ""},
    StaticField{":method", "CONNECT"},
    StaticField{":method", "DELETE"},
    StaticField{":method", "GET"},
    StaticField{":method", "HEAD"},
    StaticField{":method", "OPTIONS"},
    StaticField{":method", "POST"},
    StaticField{":method", "PUT"},
    StaticField{":scheme", "http"},
    StaticField{":scheme", "https"},
    StaticField{":status", "103"},
    StaticField{":status", "200"},
    StaticField{":status", "304"},
    StaticField{":status", "404"},
    StaticField{":status", "503"},
    StaticField{"accept", "*/*"},
    StaticField{"accept", "application/dns-message"},
    StaticField{"accept-encoding", "gzip, deflate, br"},
    StaticField{"accept-ranges", "bytes"},
    StaticField{"access-control-allow-headers", "cache-control"},
    StaticField{"access-control-allow-headers", "content-type"},
    StaticField{"access-control-allow-origin", "*"},
    StaticField{"cache-control", "max-age=0"},
    StaticField{"cache-control", "max-age=2592000"},
    StaticField{"cache-control", "max-age=604800"},
    StaticField{"cache-control", "no-cache"},
    StaticField{"cache-control", "no-store"},
    StaticField{"cache-control", "public, max-age=31536000"},
    StaticField{"content-encoding", "br"},
    StaticField{"content-encoding", "gzip"},
    StaticField{"content-type", "application/dns-message"},
    StaticField{"content-type", "application/javascript"},
    StaticField{"content-type", "application/json"},
    StaticField{"content-type", "application/x-www-form-urlencoded"},
    StaticField{"content-type", "image/gif"},
    StaticField{"content-type", "image/jpeg"},
    StaticField{"content-type", "image/png"},
    StaticField{"content-type", "text/css"},
    StaticField{"content-type", "text/html; charset=utf-8"},
    StaticField{"content-type", "text/plain"},
    StaticField{"content-type", "text/plain;charset=utf-8"},
    StaticField{"range", "bytes=0-"},
    StaticField{"strict-transport-security", "max-age=31536000"},
    StaticField{"strict-transport-security", "max-age=31536000; includesubdomains"},
    StaticField{"strict-transport-security", "max-age=31536000; includesubdomains; preload"},
    StaticField{"vary", "accept-encoding"},
    StaticField{"vary", "origin"},
    StaticField{"x-content-type-options", "nosniff"},
    StaticField{"x-xss-protection", "1; mode=block"},
    StaticField{":status", "100"},
    StaticField{":status", "204"},
    StaticField{":status", "206"},
    StaticField{":status", "302"},
    StaticField{":status", "400"},
    StaticField{":status", "403"},
    StaticField{":status", "421"},
    StaticField{":status", "425"},
    StaticField{":status", "500"},
    StaticField{"accept-language", ""},
    StaticField{"access-control-allow-credentials", "FALSE"},
    StaticField{"access-control-allow-credentials", "TRUE"},
    StaticField{"access-control-allow-headers", "*"},
    StaticField{"access-control-allow-methods", "get"},
    StaticField{"access-control-allow-methods", "get, post, options"},
    StaticField{"access-control-allow-methods", "options"},
    StaticField{"access-control-expose-headers", "content-length"},
    StaticField{"access-control-request-headers", "content-type"},
    StaticField{"access-control-request-method", "get"},
    StaticField{"access-control-request-method", "post"},
    StaticField{"alt-svc", "clear"},
    StaticField{"authorization", ""},
    StaticField{"content-security-policy", "script-src 'none'; object-src 'none'; base-uri 'none'"},
    StaticField{"early-data", "1"},
    StaticField{"expect-ct", ""},
    StaticField{"forwarded", ""},
    StaticField{"if-range", ""},
    StaticField{"origin", ""},
    StaticField{"purpose", "prefetch"},
    StaticField{"server", ""},
    StaticField{"timing-allow-origin", "*"},
    StaticField{"upgrade-insecure-requests", "1"},
    StaticField{"user-agent", ""},
    StaticField{"x-forwarded-for", ""},
    StaticField{"x-frame-options", "deny"},
    StaticField{"x-frame-options", "sameorigin"}};

// RFC 7541 Appendix B as a canonical code, checked canonical and against the
// RFC's examples when written (D172).
// kCountByLength[n]: how many codes are n bits long.
constexpr std::array<std::uint16_t, 31> kCountByLength = {0, 0, 0, 0, 0, 10, 26, 32, 6,  0, 5,  3,  2,  6, 2, 3,
                                                          0, 0, 0, 3, 8, 13, 26, 29, 12, 4, 15, 19, 29, 0, 4};
// Every symbol, by code length then code: canonical order.
constexpr std::array<std::uint16_t, 257> kSymbolsInCodeOrder = {
    48,  49,  50,  97,  99,  101, 105, 111, 115, 116, 32,  37,  45,  46,  47,  51,  52,  53,  54,  55,  56,  57,
    61,  65,  95,  98,  100, 102, 103, 104, 108, 109, 110, 112, 114, 117, 58,  66,  67,  68,  69,  70,  71,  72,
    73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  89,  106, 107, 113, 118, 119, 120,
    121, 122, 38,  42,  44,  59,  88,  90,  33,  34,  40,  41,  63,  39,  43,  124, 35,  62,  0,   36,  64,  91,
    93,  126, 94,  125, 60,  96,  123, 92,  195, 208, 128, 130, 131, 162, 184, 194, 224, 226, 153, 161, 167, 172,
    176, 177, 179, 209, 216, 217, 227, 229, 230, 129, 132, 133, 134, 136, 146, 154, 156, 160, 163, 164, 169, 170,
    173, 178, 181, 185, 186, 187, 189, 190, 196, 198, 228, 232, 233, 1,   135, 137, 138, 139, 140, 141, 143, 147,
    149, 150, 151, 152, 155, 157, 158, 165, 166, 168, 174, 175, 180, 182, 183, 188, 191, 197, 231, 239, 9,   142,
    144, 145, 148, 159, 171, 206, 215, 225, 236, 237, 199, 207, 234, 235, 192, 193, 200, 201, 202, 205, 210, 213,
    218, 219, 238, 240, 242, 243, 255, 203, 204, 211, 212, 214, 221, 222, 223, 241, 244, 245, 246, 247, 248, 250,
    251, 252, 253, 254, 2,   3,   4,   5,   6,   7,   8,   11,  12,  14,  15,  16,  17,  18,  19,  20,  21,  23,
    24,  25,  26,  27,  28,  29,  30,  31,  127, 220, 249, 10,  13,  22,  256};

constexpr std::size_t kLongestCode = 30;
constexpr std::uint16_t kEndOfString = 256;

/// Reads bytes of a field section in order.
class Cursor {
public:
    explicit Cursor(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {
    }

    [[nodiscard]] bool done() const noexcept {
        return at_ == bytes_.size();
    }
    [[nodiscard]] std::optional<std::uint8_t> peek() const noexcept {
        if (done()) {
            return std::nullopt;
        }
        return std::to_integer<std::uint8_t>(bytes_[at_]);
    }

    /// An RFC 7541 integer with an `bits`-bit prefix in the next byte, whose
    /// upper bits are the caller's; nothing if it ends early or passes
    /// 2^32.
    std::optional<std::uint64_t> integer(unsigned bits) noexcept {
        const auto kFirst = peek();
        if (!kFirst) {
            return std::nullopt;
        }
        ++at_;
        const std::uint64_t kMask = (std::uint64_t{1} << bits) - 1;
        std::uint64_t value = *kFirst & kMask;
        if (value < kMask) {
            return value;
        }
        for (unsigned shift = 0; shift <= 28; shift += 7) {
            const auto kNext = peek();
            if (!kNext) {
                return std::nullopt;
            }
            ++at_;
            value += static_cast<std::uint64_t>(*kNext & 0x7FU) << shift;
            if ((*kNext & 0x80U) == 0) {
                return value <= 0xFFFF'FFFFU ? std::optional{value} : std::nullopt;
            }
        }
        return std::nullopt;
    }

    /// A string whose Huffman flag is `huffmanBit` of the next byte and
    /// whose length has a `bits`-bit prefix.
    std::optional<std::string> string(std::uint8_t huffmanBit, unsigned bits, std::size_t& budget) noexcept {
        const auto kFirst = peek();
        if (!kFirst) {
            return std::nullopt;
        }
        const bool kHuffman = (*kFirst & huffmanBit) != 0;
        const auto kLength = integer(bits);
        if (!kLength || *kLength > bytes_.size() - at_) {
            return std::nullopt;
        }
        const std::span<const std::byte> kBytes = bytes_.subspan(at_, static_cast<std::size_t>(*kLength));
        at_ += kBytes.size();
        std::optional<std::string> text;
        if (kHuffman) {
            text = decodeHuffman(kBytes, budget);
        } else if (kBytes.size() <= budget) {
            text = std::string{reinterpret_cast<const char*>(kBytes.data()), kBytes.size()};
        }
        if (text) {
            budget -= text->size();
        }
        return text;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t at_ = 0;
};

std::optional<StaticField> staticField(std::uint64_t index) noexcept {
    if (index >= kStaticTable.size()) {
        return std::nullopt;
    }
    return kStaticTable[static_cast<std::size_t>(index)];
}

void pushInteger(std::vector<std::byte>& into, std::uint8_t high, unsigned bits, std::uint64_t value) {
    const std::uint64_t kMask = (std::uint64_t{1} << bits) - 1;
    if (value < kMask) {
        into.push_back(static_cast<std::byte>(high | value));
        return;
    }
    into.push_back(static_cast<std::byte>(high | kMask));
    value -= kMask;
    while (value >= 0x80) {
        into.push_back(static_cast<std::byte>((value & 0x7FU) | 0x80U));
        value >>= 7U;
    }
    into.push_back(static_cast<std::byte>(value));
}

} // namespace

std::optional<std::string> decodeHuffman(std::span<const std::byte> coded, std::size_t maximumBytes) {
    std::string decoded;
    std::size_t bit = 0;
    const std::size_t kBits = coded.size() * 8;
    const auto kBitAt = [&coded](std::size_t at) {
        return (std::to_integer<unsigned>(coded[at / 8]) >> (7 - (at % 8))) & 1U;
    };
    while (bit < kBits) {
        // Canonical decoding, one bit at a time (as zlib's puff does): the
        // codes of each length follow the last of the length before.
        std::uint32_t code = 0;
        std::uint32_t first = 0;
        std::uint32_t index = 0;
        std::size_t length = 1;
        bool allOnes = true;
        std::optional<std::uint16_t> symbol;
        for (; length <= kLongestCode && bit < kBits; ++length) {
            const unsigned kBit = kBitAt(bit++);
            allOnes = allOnes && kBit == 1;
            code |= kBit;
            const std::uint32_t kCount = kCountByLength[length];
            if (code - first < kCount) {
                symbol = kSymbolsInCodeOrder[index + (code - first)];
                break;
            }
            index += kCount;
            first += kCount;
            first <<= 1U;
            code <<= 1U;
        }
        if (!symbol) {
            // What is left must be padding: fewer than eight bits, all ones
            // (the start of EOS).
            return allOnes && length - 1 < 8 ? std::optional{decoded} : std::nullopt;
        }
        if (*symbol == kEndOfString || decoded.size() == maximumBytes) {
            return std::nullopt;
        }
        decoded.push_back(static_cast<char>(*symbol));
    }
    return decoded;
}

std::optional<std::vector<FieldLine>> decodeFieldSection(std::span<const std::byte> section, std::size_t maximumBytes) {
    Cursor cursor{section};
    // The prefix: a Required Insert Count of zero (nothing dynamic), then a
    // Delta Base, which with nothing dynamic means nothing.
    const auto kInsertCount = cursor.integer(8);
    if (!kInsertCount || *kInsertCount != 0 || !cursor.integer(7)) {
        return std::nullopt;
    }
    std::vector<FieldLine> lines;
    std::size_t budget = maximumBytes;
    while (!cursor.done()) {
        const std::uint8_t kFirst = *cursor.peek();
        FieldLine line;
        if ((kFirst & 0x80U) != 0) {
            // Indexed field line: static only.
            if ((kFirst & 0x40U) == 0) {
                return std::nullopt;
            }
            const auto kIndex = cursor.integer(6);
            const auto kField = kIndex ? staticField(*kIndex) : std::nullopt;
            if (!kField || kField->name.size() + kField->value.size() > budget) {
                return std::nullopt;
            }
            budget -= kField->name.size() + kField->value.size();
            line = FieldLine{.name = std::string{kField->name}, .value = std::string{kField->value}};
        } else if ((kFirst & 0xC0U) == 0x40U) {
            // Literal with a name reference: static only.
            if ((kFirst & 0x10U) == 0) {
                return std::nullopt;
            }
            const auto kIndex = cursor.integer(4);
            const auto kField = kIndex ? staticField(*kIndex) : std::nullopt;
            if (!kField || kField->name.size() > budget) {
                return std::nullopt;
            }
            budget -= kField->name.size();
            auto value = cursor.string(0x80U, 7, budget);
            if (!value) {
                return std::nullopt;
            }
            line = FieldLine{.name = std::string{kField->name}, .value = std::move(*value)};
        } else if ((kFirst & 0xE0U) == 0x20U) {
            // Literal with a literal name.
            auto name = cursor.string(0x08U, 3, budget);
            auto value = name ? cursor.string(0x80U, 7, budget) : std::nullopt;
            if (!value) {
                return std::nullopt;
            }
            line = FieldLine{.name = std::move(*name), .value = std::move(*value)};
        } else {
            // Post-base references are to a dynamic table there is none of.
            return std::nullopt;
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

std::vector<std::byte> encodeStatus(std::uint16_t status) {
    std::vector<std::byte> section{std::byte{0}, std::byte{0}};
    constexpr std::uint64_t kStatus200 = 25;
    constexpr std::uint64_t kStatusName = 24;
    if (status == 200) {
        pushInteger(section, 0xC0U, 6, kStatus200);
        return section;
    }
    pushInteger(section, 0x50U, 4, kStatusName);
    const std::string kText = std::to_string(status);
    pushInteger(section, 0x00U, 7, kText.size());
    for (const char kCharacter : kText) {
        section.push_back(static_cast<std::byte>(kCharacter));
    }
    return section;
}

} // namespace rawframe::network_quic
