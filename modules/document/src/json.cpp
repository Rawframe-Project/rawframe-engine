#include "rawframe/document/json.h"

#include "rawframe/document/errors.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace rawframe::document {

namespace {

/// Whether a number's text is an integer: no fraction and no exponent.
bool integral(std::string_view text) noexcept {
    return text.find_first_of(".eE") == std::string_view::npos;
}

std::string shortest(double number) {
    std::array<char, 32> buffer{};
    const auto [kEnd, kError] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    return kError == std::errc{} ? std::string{buffer.data(), kEnd} : std::string{"0"};
}

/// Where `offset` is in `text`, as one-based line and column.
std::pair<std::size_t, std::size_t> placeOf(std::string_view text, std::size_t offset) noexcept {
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t index = 0; index < std::min(offset, text.size()); ++index) {
        if (text[index] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return {line, column};
}

std::unexpected<result::Error>
failAt(std::string_view text, std::size_t offset, DocumentError error, std::string_view why) {
    const auto [kLine, kColumn] = placeOf(text, offset);
    return std::unexpected<result::Error>{result::fail(error == DocumentError::TooLarge
                                                           ? result::ErrorClass::ResourceExhausted
                                                           : result::ErrorClass::InvalidArgument,
                                                       kDocumentDomain,
                                                       code(error),
                                                       why)
                                              .error()
                                              .withContext("line", std::to_string(kLine))
                                              .withContext("column", std::to_string(kColumn))};
}

/// Recursive descent over the text, one value at a time.
class Reader {
public:
    Reader(std::string_view text, const ReadLimits& limits) noexcept : text_(text), limits_(limits) {
    }

    result::Result<Value> document() {
        if (text_.size() > limits_.maximumBytes) {
            return failAt(text_, limits_.maximumBytes, DocumentError::TooLarge, "the document is longer than allowed");
        }
        if (text_.starts_with("\xEF\xBB\xBF")) {
            return failAt(text_, 0, DocumentError::Malformed, "a document has no byte order mark");
        }
        skipSpace();
        RAWFRAME_TRY_ASSIGN(Value value, readValue(0));
        skipSpace();
        if (at_ != text_.size()) {
            return failAt(text_, at_, DocumentError::Malformed, "text follows the document's value");
        }
        return value;
    }

private:
    void skipSpace() noexcept {
        while (at_ < text_.size() &&
               (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) {
            ++at_;
        }
    }

    bool literal(std::string_view word) noexcept {
        if (text_.substr(at_).starts_with(word)) {
            at_ += word.size();
            return true;
        }
        return false;
    }

    result::Result<Value> readValue(std::size_t depth) {
        if (depth >= limits_.maximumDepth) {
            return failAt(text_, at_, DocumentError::TooLarge, "the document nests deeper than allowed");
        }
        if (at_ >= text_.size()) {
            return failAt(text_, at_, DocumentError::Malformed, "a value was expected");
        }
        switch (text_[at_]) {
        case '{':
            return readObject(depth);
        case '[':
            return readArray(depth);
        case '"': {
            RAWFRAME_TRY_ASSIGN(std::string text, readString());
            return Value::string(std::move(text));
        }
        case 't':
            if (literal("true")) {
                return Value::boolean(true);
            }
            break;
        case 'f':
            if (literal("false")) {
                return Value::boolean(false);
            }
            break;
        case 'n':
            if (literal("null")) {
                return Value{};
            }
            break;
        default:
            if (text_[at_] == '-' || (text_[at_] >= '0' && text_[at_] <= '9')) {
                return readNumber();
            }
            break;
        }
        return failAt(text_, at_, DocumentError::Malformed, "a value was expected");
    }

    result::Result<Value> readObject(std::size_t depth) {
        const std::size_t kStart = at_;
        ++at_;
        Value object = Value::object();
        skipSpace();
        if (at_ < text_.size() && text_[at_] == '}') {
            ++at_;
            return object;
        }
        while (true) {
            skipSpace();
            if (at_ >= text_.size() || text_[at_] != '"') {
                return failAt(text_, at_, DocumentError::Malformed, "a member name was expected");
            }
            RAWFRAME_TRY_ASSIGN(std::string name, readString());
            skipSpace();
            if (at_ >= text_.size() || text_[at_] != ':') {
                return failAt(text_, at_, DocumentError::Malformed, "a colon was expected after a member name");
            }
            ++at_;
            skipSpace();
            RAWFRAME_TRY_ASSIGN(Value value, readValue(depth + 1));
            object.add(std::move(name), std::move(value));
            skipSpace();
            if (at_ < text_.size() && text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (at_ < text_.size() && text_[at_] == '}') {
                ++at_;
                break;
            }
            return failAt(text_, at_, DocumentError::Malformed, "a comma or the object's end was expected");
        }
        std::vector<std::string_view> sorted(object.names().begin(), object.names().end());
        std::ranges::sort(sorted);
        if (std::ranges::adjacent_find(sorted) != sorted.end()) {
            return failAt(text_, kStart, DocumentError::Malformed, "an object names a member twice");
        }
        return object;
    }

    result::Result<Value> readArray(std::size_t depth) {
        ++at_;
        Value array = Value::array();
        skipSpace();
        if (at_ < text_.size() && text_[at_] == ']') {
            ++at_;
            return array;
        }
        while (true) {
            skipSpace();
            RAWFRAME_TRY_ASSIGN(Value item, readValue(depth + 1));
            array.push(std::move(item));
            skipSpace();
            if (at_ < text_.size() && text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (at_ < text_.size() && text_[at_] == ']') {
                ++at_;
                return array;
            }
            return failAt(text_, at_, DocumentError::Malformed, "a comma or the array's end was expected");
        }
    }

    result::Result<Value> readNumber() {
        const std::size_t kStart = at_;
        const auto kDigits = [this] {
            const std::size_t kFrom = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
                ++at_;
            }
            return at_ - kFrom;
        };
        if (text_[at_] == '-') {
            ++at_;
        }
        const std::size_t kWhole = at_;
        if (kDigits() == 0 || (text_[kWhole] == '0' && at_ - kWhole > 1)) {
            return failAt(text_, kStart, DocumentError::Malformed, "a number's whole part is malformed");
        }
        if (at_ < text_.size() && text_[at_] == '.') {
            ++at_;
            if (kDigits() == 0) {
                return failAt(text_, kStart, DocumentError::Malformed, "a number's fraction has no digits");
            }
        }
        if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
            ++at_;
            if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) {
                ++at_;
            }
            if (kDigits() == 0) {
                return failAt(text_, kStart, DocumentError::Malformed, "a number's exponent has no digits");
            }
        }
        const std::string_view kText = text_.substr(kStart, at_ - kStart);
        double parsed = 0;
        const auto [kEnd, kError] = std::from_chars(kText.data(), kText.data() + kText.size(), parsed);
        if (kError != std::errc{} || kEnd != kText.data() + kText.size() || !std::isfinite(parsed)) {
            return failAt(text_, kStart, DocumentError::Malformed, "a number is outside a double's range");
        }
        return Value::numberText(std::string{kText});
    }

    /// Four hexadecimal digits after `\u`.
    std::optional<std::uint32_t> hex4() noexcept {
        if (at_ + 4 > text_.size()) {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            const char kDigit = text_[at_ + index];
            value <<= 4U;
            if (kDigit >= '0' && kDigit <= '9') {
                value |= static_cast<std::uint32_t>(kDigit - '0');
            } else if (kDigit >= 'a' && kDigit <= 'f') {
                value |= static_cast<std::uint32_t>(kDigit - 'a' + 10);
            } else if (kDigit >= 'A' && kDigit <= 'F') {
                value |= static_cast<std::uint32_t>(kDigit - 'A' + 10);
            } else {
                return std::nullopt;
            }
        }
        at_ += 4;
        return value;
    }

    static void appendUtf8(std::string& into, std::uint32_t point) {
        if (point < 0x80) {
            into.push_back(static_cast<char>(point));
        } else if (point < 0x800) {
            into.push_back(static_cast<char>(0xC0U | (point >> 6U)));
            into.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
        } else if (point < 0x10000) {
            into.push_back(static_cast<char>(0xE0U | (point >> 12U)));
            into.push_back(static_cast<char>(0x80U | ((point >> 6U) & 0x3FU)));
            into.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
        } else {
            into.push_back(static_cast<char>(0xF0U | (point >> 18U)));
            into.push_back(static_cast<char>(0x80U | ((point >> 12U) & 0x3FU)));
            into.push_back(static_cast<char>(0x80U | ((point >> 6U) & 0x3FU)));
            into.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
        }
    }

    /// The length of the well-formed UTF-8 sequence at `at_`, or nought.
    [[nodiscard]] std::size_t utf8Length() const noexcept {
        const auto kByte = [this](std::size_t offset) {
            return at_ + offset < text_.size() ? static_cast<std::uint8_t>(text_[at_ + offset]) : std::uint8_t{0};
        };
        const auto kFollows = [&kByte](std::size_t offset) {
            return (kByte(offset) & 0xC0U) == 0x80U;
        };
        const std::uint8_t kLead = kByte(0);
        if (kLead >= 0xC2 && kLead <= 0xDF) {
            return kFollows(1) ? 2 : 0;
        }
        if (kLead >= 0xE0 && kLead <= 0xEF) {
            // No overlong forms, and no surrogates.
            const std::uint8_t kSecond = kByte(1);
            const bool kRange = kLead == 0xE0 ? kSecond >= 0xA0 : kLead == 0xED ? kSecond <= 0x9F : true;
            return kFollows(1) && kFollows(2) && kRange ? 3 : 0;
        }
        if (kLead >= 0xF0 && kLead <= 0xF4) {
            const std::uint8_t kSecond = kByte(1);
            const bool kRange = kLead == 0xF0 ? kSecond >= 0x90 : kLead == 0xF4 ? kSecond <= 0x8F : true;
            return kFollows(1) && kFollows(2) && kFollows(3) && kRange ? 4 : 0;
        }
        return 0;
    }

    result::Result<std::string> readString() {
        ++at_;
        std::string text;
        while (true) {
            if (at_ >= text_.size()) {
                return failAt(text_, at_, DocumentError::Malformed, "a string has no end");
            }
            const auto kByte = static_cast<std::uint8_t>(text_[at_]);
            if (kByte == '"') {
                ++at_;
                return text;
            }
            if (kByte < 0x20) {
                return failAt(text_, at_, DocumentError::Malformed, "a control character in a string is escaped");
            }
            if (kByte >= 0x80) {
                const std::size_t kLength = utf8Length();
                if (kLength == 0) {
                    return failAt(text_, at_, DocumentError::Malformed, "a string is not UTF-8");
                }
                text.append(text_.substr(at_, kLength));
                at_ += kLength;
                continue;
            }
            if (kByte != '\\') {
                text.push_back(static_cast<char>(kByte));
                ++at_;
                continue;
            }
            const std::size_t kEscape = at_;
            ++at_;
            if (at_ >= text_.size()) {
                return failAt(text_, kEscape, DocumentError::Malformed, "a string has no end");
            }
            const char kKind = text_[at_++];
            switch (kKind) {
            case '"':
            case '\\':
            case '/':
                text.push_back(kKind);
                break;
            case 'b':
                text.push_back('\b');
                break;
            case 'f':
                text.push_back('\f');
                break;
            case 'n':
                text.push_back('\n');
                break;
            case 'r':
                text.push_back('\r');
                break;
            case 't':
                text.push_back('\t');
                break;
            case 'u': {
                const auto kUnit = hex4();
                if (!kUnit) {
                    return failAt(text_, kEscape, DocumentError::Malformed, "a \\u escape needs four hex digits");
                }
                std::uint32_t point = *kUnit;
                if (point >= 0xDC00 && point <= 0xDFFF) {
                    return failAt(text_, kEscape, DocumentError::Malformed, "a low surrogate stands alone");
                }
                if (point >= 0xD800 && point <= 0xDBFF) {
                    if (!literal("\\u")) {
                        return failAt(text_, kEscape, DocumentError::Malformed, "a high surrogate stands alone");
                    }
                    const auto kLow = hex4();
                    if (!kLow || *kLow < 0xDC00 || *kLow > 0xDFFF) {
                        return failAt(text_, kEscape, DocumentError::Malformed, "a high surrogate stands alone");
                    }
                    point = 0x10000 + ((point - 0xD800) << 10U) + (*kLow - 0xDC00);
                }
                appendUtf8(text, point);
                break;
            }
            default:
                return failAt(text_, kEscape, DocumentError::Malformed, "a string has an unknown escape");
            }
        }
    }

    std::string_view text_;
    ReadLimits limits_;
    std::size_t at_ = 0;
};

void writeString(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char kChar : text) {
        const auto kByte = static_cast<std::uint8_t>(kChar);
        switch (kChar) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (kByte < 0x20) {
                constexpr std::string_view kHex = "0123456789abcdef";
                out += "\\u00";
                out.push_back(kHex[kByte >> 4U]);
                out.push_back(kHex[kByte & 0xFU]);
            } else {
                out.push_back(kChar);
            }
        }
    }
    out.push_back('"');
}

void writeValue(std::string& out, const Value& value, std::size_t indent) {
    const auto kNewLine = [&out](std::size_t depth) {
        out.push_back('\n');
        out.append(depth * 2, ' ');
    };
    switch (value.kind()) {
    case Value::Kind::Null:
        out += "null";
        return;
    case Value::Kind::Bool:
        out += *value.truth() ? "true" : "false";
        return;
    case Value::Kind::Number: {
        const std::string& kText = *value.text();
        out += integral(kText) ? kText : shortest(*value.real());
        return;
    }
    case Value::Kind::String:
        writeString(out, *value.text());
        return;
    case Value::Kind::Array:
    case Value::Kind::Object: {
        const bool kObject = value.kind() == Value::Kind::Object;
        if (value.items().empty()) {
            out += kObject ? "{}" : "[]";
            return;
        }
        out.push_back(kObject ? '{' : '[');
        for (std::size_t index = 0; index < value.items().size(); ++index) {
            kNewLine(indent + 1);
            if (kObject) {
                writeString(out, value.names()[index]);
                out += ": ";
            }
            writeValue(out, value.items()[index], indent + 1);
            if (index + 1 < value.items().size()) {
                out.push_back(',');
            }
        }
        kNewLine(indent);
        out.push_back(kObject ? '}' : ']');
        return;
    }
    }
}

} // namespace

Value Value::boolean(bool truth) {
    Value made;
    made.kind_ = Kind::Bool;
    made.truth_ = truth;
    return made;
}

Value Value::integer(std::int64_t number) {
    std::array<char, 24> buffer{};
    const auto [kEnd, kError] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    static_cast<void>(kError);
    return numberText(std::string{buffer.data(), kEnd});
}

Value Value::real(double number) {
    return std::isfinite(number) ? numberText(shortest(number)) : Value{};
}

Value Value::string(std::string text) {
    Value made;
    made.kind_ = Kind::String;
    made.text_ = std::move(text);
    return made;
}

Value Value::array(std::vector<Value> items) {
    Value made;
    made.kind_ = Kind::Array;
    made.items_ = std::move(items);
    return made;
}

Value Value::object() {
    Value made;
    made.kind_ = Kind::Object;
    return made;
}

Value Value::numberText(std::string text) {
    Value made;
    made.kind_ = Kind::Number;
    made.text_ = std::move(text);
    return made;
}

std::optional<bool> Value::truth() const noexcept {
    return kind_ == Kind::Bool ? std::optional{truth_} : std::nullopt;
}

std::optional<std::int64_t> Value::integer() const noexcept {
    if (kind_ != Kind::Number || !integral(text_)) {
        return std::nullopt;
    }
    std::int64_t number = 0;
    const auto [kEnd, kError] = std::from_chars(text_.data(), text_.data() + text_.size(), number);
    return kError == std::errc{} && kEnd == text_.data() + text_.size() ? std::optional{number} : std::nullopt;
}

std::optional<double> Value::real() const noexcept {
    if (kind_ != Kind::Number) {
        return std::nullopt;
    }
    double number = 0;
    const auto [kEnd, kError] = std::from_chars(text_.data(), text_.data() + text_.size(), number);
    return kError == std::errc{} ? std::optional{number} : std::nullopt;
}

const std::string* Value::text() const noexcept {
    return kind_ == Kind::String || kind_ == Kind::Number ? &text_ : nullptr;
}

const Value* Value::find(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < names_.size(); ++index) {
        if (names_[index] == name) {
            return &items_[index];
        }
    }
    return nullptr;
}

void Value::push(Value item) {
    items_.push_back(std::move(item));
}

void Value::add(std::string name, Value value) {
    names_.push_back(std::move(name));
    items_.push_back(std::move(value));
}

result::Result<Value> parse(std::string_view text, const ReadLimits& limits) {
    return Reader{text, limits}.document();
}

std::string write(const Value& value) {
    std::string out;
    writeValue(out, value, 0);
    out.push_back('\n');
    return out;
}

result::Result<Value> parseCanonical(std::string_view text, const ReadLimits& limits) {
    RAWFRAME_TRY_ASSIGN(Value value, parse(text, limits));
    const std::string kCanonical = write(value);
    if (kCanonical != text) {
        const auto kDiffer = std::ranges::mismatch(kCanonical, text);
        const auto kOffset = static_cast<std::size_t>(kDiffer.in2 - text.begin());
        return failAt(text, kOffset, DocumentError::NotCanonical, "the document is not written canonically");
    }
    return value;
}

namespace {

constexpr std::int64_t kSafeInteger = 9'007'199'254'740'991;

result::Status writeRecordValue(std::string& out, const Value& value) {
    const auto kInvalid = [](std::string_view why) -> result::Status {
        return std::unexpected<result::Error>{
            result::fail(result::ErrorClass::InvalidArgument, kDocumentDomain, code(DocumentError::Invalid), why)
                .error()};
    };
    switch (value.kind()) {
    case Value::Kind::Null:
        out += "null";
        return {};
    case Value::Kind::Bool:
        out += *value.truth() ? "true" : "false";
        return {};
    case Value::Kind::Number: {
        const std::optional<std::int64_t> kInteger = value.integer();
        if (!kInteger.has_value() || *kInteger < -kSafeInteger || *kInteger > kSafeInteger) {
            return kInvalid("a canonical record's numbers are integers within +/-(2^53 - 1)");
        }
        out += std::to_string(*kInteger);
        return {};
    }
    case Value::Kind::String:
        writeString(out, *value.text());
        return {};
    case Value::Kind::Array: {
        out.push_back('[');
        for (std::size_t index = 0; index < value.items().size(); ++index) {
            if (index != 0) {
                out.push_back(',');
            }
            RAWFRAME_TRY(writeRecordValue(out, value.items()[index]));
        }
        out.push_back(']');
        return {};
    }
    case Value::Kind::Object: {
        // Printable ASCII names sort by their bytes as JCS sorts them by
        // UTF-16 code units.
        std::vector<std::size_t> order(value.names().size());
        for (std::size_t index = 0; index < order.size(); ++index) {
            const std::string& name = value.names()[index];
            if (std::ranges::any_of(name, [](char each) {
                    return each < 0x20 || each > 0x7e;
                })) {
                return kInvalid("a canonical record's member names are printable ASCII");
            }
            order[index] = index;
        }
        std::ranges::sort(order, [&value](std::size_t left, std::size_t right) {
            return value.names()[left] < value.names()[right];
        });
        out.push_back('{');
        for (std::size_t index = 0; index < order.size(); ++index) {
            if (index != 0) {
                if (value.names()[order[index]] == value.names()[order[index - 1]]) {
                    return kInvalid("a canonical record's member names are unique");
                }
                out.push_back(',');
            }
            writeString(out, value.names()[order[index]]);
            out.push_back(':');
            RAWFRAME_TRY(writeRecordValue(out, value.items()[order[index]]));
        }
        out.push_back('}');
        return {};
    }
    }
    return {};
}

} // namespace

result::Result<std::string> writeCanonicalRecord(const Value& value) {
    if (value.kind() != Value::Kind::Object) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           kDocumentDomain,
                                                           code(DocumentError::Invalid),
                                                           "a canonical record is one object")
                                                  .error()};
    }
    std::string out;
    RAWFRAME_TRY(writeRecordValue(out, value));
    return out;
}

result::Result<Value> parseCanonicalRecord(std::string_view text, const ReadLimits& limits) {
    RAWFRAME_TRY_ASSIGN(Value value, parse(text, limits));
    RAWFRAME_TRY_ASSIGN(const std::string kCanonical, writeCanonicalRecord(value));
    if (kCanonical != text) {
        const auto kDiffer = std::ranges::mismatch(kCanonical, text);
        const auto kOffset = static_cast<std::size_t>(kDiffer.in2 - text.begin());
        return failAt(text, kOffset, DocumentError::NotCanonical, "the record is not written canonically");
    }
    return value;
}

} // namespace rawframe::document
