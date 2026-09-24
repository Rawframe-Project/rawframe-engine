#pragma once

// A strict JSON reader for the tests: it accepts RFC 8259 text and nothing
// else, refuses raw control characters and invalid UTF-8 inside strings, and
// keeps object members in the order they were written so key order can be
// checked. Test code only; it favours plainness over speed.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::diagnostics::testing {

struct Json {
    enum class Type : std::uint8_t {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };
    Type type = Type::Null;
    bool boolean = false;
    std::string text; // a string's decoded value, or a number's spelling
    std::vector<Json> items;
    // An object's members as two parallel lists, in written order: a vector of
    // pairs would need Json complete here.
    std::vector<std::string> names;
    std::vector<Json> values;

    [[nodiscard]] const Json* find(std::string_view key) const {
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (names[index] == key) {
                return &values[index];
            }
        }
        return nullptr;
    }

    [[nodiscard]] const std::vector<std::string>& keys() const {
        return names;
    }
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {
    }

    /// The single value `text` holds, or nothing if it is not exactly one.
    [[nodiscard]] std::optional<Json> read() {
        Json value;
        skipSpace();
        if (!parseValue(value)) {
            return std::nullopt;
        }
        skipSpace();
        if (position_ != text_.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    void skipSpace() {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t' ||
                                            text_[position_] == '\n' || text_[position_] == '\r')) {
            ++position_;
        }
    }

    bool consume(std::string_view word) {
        if (text_.substr(position_, word.size()) != word) {
            return false;
        }
        position_ += word.size();
        return true;
    }

    bool parseValue(Json& value) {
        if (position_ >= text_.size() || ++depth_ > 32) {
            return false;
        }
        bool parsed = false;
        const char kNext = text_[position_];
        if (kNext == '{') {
            parsed = parseObject(value);
        } else if (kNext == '[') {
            parsed = parseArray(value);
        } else if (kNext == '"') {
            value.type = Json::Type::String;
            parsed = parseString(value.text);
        } else if (consume("null")) {
            value.type = Json::Type::Null;
            parsed = true;
        } else if (consume("true") || consume("false")) {
            value.type = Json::Type::Boolean;
            value.boolean = text_[position_ - 1] == 'e' && text_[position_ - 2] == 'u';
            parsed = true;
        } else {
            value.type = Json::Type::Number;
            parsed = parseNumber(value.text);
        }
        --depth_;
        return parsed;
    }

    bool parseObject(Json& value) {
        value.type = Json::Type::Object;
        ++position_;
        skipSpace();
        if (consume("}")) {
            return true;
        }
        for (;;) {
            skipSpace();
            std::string key;
            if (position_ >= text_.size() || text_[position_] != '"' || !parseString(key)) {
                return false;
            }
            skipSpace();
            if (!consume(":")) {
                return false;
            }
            skipSpace();
            Json member;
            if (!parseValue(member)) {
                return false;
            }
            value.names.push_back(std::move(key));
            value.values.push_back(std::move(member));
            skipSpace();
            if (consume("}")) {
                return true;
            }
            if (!consume(",")) {
                return false;
            }
        }
    }

    bool parseArray(Json& value) {
        value.type = Json::Type::Array;
        ++position_;
        skipSpace();
        if (consume("]")) {
            return true;
        }
        for (;;) {
            skipSpace();
            Json item;
            if (!parseValue(item)) {
                return false;
            }
            value.items.push_back(std::move(item));
            skipSpace();
            if (consume("]")) {
                return true;
            }
            if (!consume(",")) {
                return false;
            }
        }
    }

    static bool isDigit(char character) {
        return character >= '0' && character <= '9';
    }

    bool parseNumber(std::string& spelling) {
        const std::size_t kStart = position_;
        consume("-");
        if (consume("0")) {
        } else if (position_ < text_.size() && isDigit(text_[position_])) {
            while (position_ < text_.size() && isDigit(text_[position_])) {
                ++position_;
            }
        } else {
            return false;
        }
        if (consume(".")) {
            if (position_ >= text_.size() || !isDigit(text_[position_])) {
                return false;
            }
            while (position_ < text_.size() && isDigit(text_[position_])) {
                ++position_;
            }
        }
        if (consume("e") || consume("E")) {
            if (!consume("+")) {
                consume("-");
            }
            if (position_ >= text_.size() || !isDigit(text_[position_])) {
                return false;
            }
            while (position_ < text_.size() && isDigit(text_[position_])) {
                ++position_;
            }
        }
        spelling.assign(text_.substr(kStart, position_ - kStart));
        return true;
    }

    static void appendUtf8(std::string& out, std::uint32_t point) {
        if (point < 0x80U) {
            out.push_back(static_cast<char>(point));
        } else if (point < 0x800U) {
            out.push_back(static_cast<char>(0xC0U | (point >> 6U)));
            out.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
        } else if (point < 0x10000U) {
            out.push_back(static_cast<char>(0xE0U | (point >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((point >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xF0U | (point >> 18U)));
            out.push_back(static_cast<char>(0x80U | ((point >> 12U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | ((point >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
        }
    }

    bool parseHex4(std::uint32_t& point) {
        if (position_ + 4 > text_.size()) {
            return false;
        }
        point = 0;
        for (int index = 0; index < 4; ++index) {
            const char kDigit = text_[position_++];
            point <<= 4U;
            if (isDigit(kDigit)) {
                point |= static_cast<std::uint32_t>(kDigit - '0');
            } else if (kDigit >= 'a' && kDigit <= 'f') {
                point |= static_cast<std::uint32_t>(kDigit - 'a' + 10);
            } else if (kDigit >= 'A' && kDigit <= 'F') {
                point |= static_cast<std::uint32_t>(kDigit - 'A' + 10);
            } else {
                return false;
            }
        }
        return true;
    }

    /// Validates one UTF-8 sequence strictly (no overlongs, surrogates, or
    /// values past U+10FFFF) and copies it.
    bool copyUtf8(std::string& out) {
        const auto kByte = [&](std::size_t offset) {
            return static_cast<unsigned char>(text_[position_ + offset]);
        };
        const unsigned char kLead = kByte(0);
        std::size_t length = 0;
        std::uint32_t point = 0;
        if (kLead >= 0xC2U && kLead <= 0xDFU) {
            length = 2;
            point = kLead & 0x1FU;
        } else if (kLead >= 0xE0U && kLead <= 0xEFU) {
            length = 3;
            point = kLead & 0x0FU;
        } else if (kLead >= 0xF0U && kLead <= 0xF4U) {
            length = 4;
            point = kLead & 0x07U;
        } else {
            return false;
        }
        if (position_ + length > text_.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            if ((kByte(offset) & 0xC0U) != 0x80U) {
                return false;
            }
            point = (point << 6U) | (kByte(offset) & 0x3FU);
        }
        const bool kOverlong = (length == 3 && point < 0x800U) || (length == 4 && point < 0x10000U);
        if (kOverlong || (point >= 0xD800U && point <= 0xDFFFU) || point > 0x10FFFFU) {
            return false;
        }
        out.append(text_.substr(position_, length));
        position_ += length;
        return true;
    }

    bool parseString(std::string& out) {
        ++position_;
        while (position_ < text_.size()) {
            const auto kCharacter = static_cast<unsigned char>(text_[position_]);
            if (kCharacter == '"') {
                ++position_;
                return true;
            }
            if (kCharacter < 0x20U) {
                return false;
            }
            if (kCharacter >= 0x80U) {
                if (!copyUtf8(out)) {
                    return false;
                }
                continue;
            }
            ++position_;
            if (kCharacter != '\\') {
                out.push_back(static_cast<char>(kCharacter));
                continue;
            }
            if (position_ >= text_.size()) {
                return false;
            }
            const char kEscape = text_[position_++];
            switch (kEscape) {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '/':
                out.push_back('/');
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                std::uint32_t point = 0;
                if (!parseHex4(point) || (point >= 0xD800U && point <= 0xDFFFU)) {
                    return false;
                }
                appendUtf8(out, point);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    std::string_view text_;
    std::size_t position_ = 0;
    int depth_ = 0;
};

/// Splits NDJSON into lines, requiring every line to end in LF.
inline std::optional<std::vector<std::string>> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    while (!text.empty()) {
        const std::size_t kEnd = text.find('\n');
        if (kEnd == std::string_view::npos) {
            return std::nullopt;
        }
        lines.emplace_back(text.substr(0, kEnd));
        text.remove_prefix(kEnd + 1);
    }
    return lines;
}

} // namespace rawframe::diagnostics::testing
