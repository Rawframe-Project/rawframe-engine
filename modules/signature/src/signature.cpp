#include "rawframe/signature/signature.h"

#include "rawframe/document/json.h"
#include "rawframe/signature/errors.h"

#include <algorithm>
#include <memory>
#include <openssl/evp.h>
#include <optional>
#include <set>

namespace rawframe::signature {

namespace {

using document::Value;

constexpr std::size_t kMaximumKeySet = std::size_t{16} * 1024;
constexpr std::size_t kMaximumKeys = 64;
constexpr std::size_t kMaximumActive = 16;

std::unexpected<result::Error> refuse(SignatureError error, std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kSignatureDomain, code(error), why).error()};
}

struct FreeKey {
    void operator()(EVP_PKEY* key) const noexcept {
        EVP_PKEY_free(key);
    }
};

struct FreeContext {
    void operator()(EVP_MD_CTX* context) const noexcept {
        EVP_MD_CTX_free(context);
    }
};

std::string hexOf(std::span<const std::byte> bytes) {
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::byte kByte : bytes) {
        text.push_back(kHex[static_cast<std::uint8_t>(kByte) >> 4U]);
        text.push_back(kHex[static_cast<std::uint8_t>(kByte) & 0xFU]);
    }
    return text;
}

/// Exactly `out.size()` bytes from twice as many lowercase hexadecimal
/// characters.
bool bytesOf(std::string_view text, std::span<std::byte> out) noexcept {
    if (text.size() != out.size() * 2) {
        return false;
    }
    const auto kNibble = [](char digit) -> int {
        if (digit >= '0' && digit <= '9') {
            return digit - '0';
        }
        if (digit >= 'a' && digit <= 'f') {
            return digit - 'a' + 10;
        }
        return -1;
    };
    for (std::size_t index = 0; index < out.size(); ++index) {
        const int kHigh = kNibble(text[index * 2]);
        const int kLow = kNibble(text[(index * 2) + 1]);
        if (kHigh < 0 || kLow < 0) {
            return false;
        }
        out[index] = static_cast<std::byte>((kHigh << 4) | kLow);
    }
    return true;
}

const std::string* textOf(const Value& record, std::string_view name) {
    const Value* value = record.find(name);
    return value != nullptr && value->kind() == Value::Kind::String ? value->text() : nullptr;
}

std::optional<std::int64_t> integerOf(const Value& record, std::string_view name) {
    const Value* value = record.find(name);
    return value != nullptr ? value->integer() : std::nullopt;
}

bool publisherSegment(std::string_view text) noexcept {
    return !text.empty() && text.front() != '-' && text.back() != '-' && std::ranges::all_of(text, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '-';
    });
}

std::string_view stateText(KeyState state) noexcept {
    switch (state) {
    case KeyState::Active:
        return "active";
    case KeyState::Retired:
        return "retired";
    case KeyState::Revoked:
        return "revoked";
    }
    return "revoked";
}

} // namespace

bool verify(const PublicKey& key, std::span<const std::byte> message, const SignatureBytes& signature) noexcept {
    const std::unique_ptr<EVP_PKEY, FreeKey> kKey{EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(key.data()), key.size())};
    const std::unique_ptr<EVP_MD_CTX, FreeContext> kContext{EVP_MD_CTX_new()};
    if (kKey == nullptr || kContext == nullptr ||
        EVP_DigestVerifyInit(kContext.get(), nullptr, nullptr, nullptr, kKey.get()) != 1) {
        return false;
    }
    return EVP_DigestVerify(kContext.get(),
                            reinterpret_cast<const unsigned char*>(signature.data()),
                            signature.size(),
                            reinterpret_cast<const unsigned char*>(message.data()),
                            message.size()) == 1;
}

bool validKeyId(std::string_view text) noexcept {
    return text.size() == 16 && std::ranges::all_of(text, [](char each) {
               return (each >= '0' && each <= '9') || (each >= 'a' && each <= 'f');
           });
}

result::Result<Envelope> readEnvelope(std::string_view text) {
    auto parsed = document::parseCanonicalRecord(text, document::ReadLimits{.maximumBytes = 1024});
    if (!parsed.has_value()) {
        return refuse(SignatureError::Malformed, "a signature envelope is a canonical record");
    }
    const std::string* kid = textOf(*parsed, "kid");
    const std::string* sig = textOf(*parsed, "sig");
    Envelope envelope;
    if (parsed->names().size() != 2 || kid == nullptr || sig == nullptr || !validKeyId(*kid) ||
        !bytesOf(*sig, envelope.sig)) {
        return refuse(SignatureError::Malformed, "a signature envelope is exactly {kid, sig}");
    }
    envelope.kid = *kid;
    return envelope;
}

std::string writeEnvelope(const Envelope& envelope) {
    Value record = Value::object();
    record.add("kid", Value::string(envelope.kid));
    record.add("sig", Value::string(hexOf(envelope.sig)));
    return *document::writeCanonicalRecord(record);
}

result::Result<PublisherKeySet> readPublisherKeySet(std::string_view text) {
    if (text.size() > kMaximumKeySet) {
        return refuse(SignatureError::Malformed, "a publisher key set is at most 16 KiB");
    }
    auto parsed = document::parseCanonicalRecord(text, document::ReadLimits{.maximumBytes = kMaximumKeySet});
    if (!parsed.has_value()) {
        return refuse(SignatureError::Malformed, "a publisher key set is a canonical record");
    }
    const Value& record = *parsed;
    const std::string* publisher = textOf(record, "publisher");
    const std::string* head = textOf(record, "head");
    const auto kSchema = integerOf(record, "schema");
    const auto kSequence = integerOf(record, "sequence");
    const auto kUpdated = integerOf(record, "updated_at");
    const Value* keys = record.find("keys");
    if (record.names().size() != 6 || kSchema != 1 || publisher == nullptr || !publisherSegment(*publisher) ||
        !kSequence.has_value() || *kSequence < 1 || !kUpdated.has_value() || head == nullptr || head->size() != 71 ||
        !head->starts_with("sha256:") || keys == nullptr || keys->kind() != Value::Kind::Array ||
        keys->items().empty() || keys->items().size() > kMaximumKeys) {
        return refuse(SignatureError::Malformed,
                      "a publisher key set is {schema: 1, publisher, sequence, updated_at, head, keys}");
    }
    PublisherKeySet made{
        .publisher = *publisher, .sequence = *kSequence, .updatedAt = *kUpdated, .head = *head, .keys = {}};
    std::array<std::byte, 32> headBytes{};
    if (!bytesOf(std::string_view{*head}.substr(7), headBytes)) {
        return refuse(SignatureError::Malformed, "a key set's head is a digest");
    }
    std::set<std::string, std::less<>> seen;
    std::size_t active = 0;
    for (const Value& each : keys->items()) {
        const std::string* kid = textOf(each, "kid");
        const std::string* publicKey = textOf(each, "public_key");
        const std::string* state = textOf(each, "state");
        const auto kSince = integerOf(each, "since");
        PublisherKey key;
        if (each.kind() != Value::Kind::Object || each.names().size() != 4 || kid == nullptr || !validKeyId(*kid) ||
            publicKey == nullptr || !bytesOf(*publicKey, key.publicKey) || state == nullptr || !kSince.has_value() ||
            !seen.insert(*kid).second) {
            return refuse(SignatureError::Malformed, "a key is {kid, public_key, state, since}, its kid unique");
        }
        if (*state == "active") {
            key.state = KeyState::Active;
            ++active;
        } else if (*state == "retired") {
            key.state = KeyState::Retired;
        } else if (*state == "revoked") {
            key.state = KeyState::Revoked;
        } else {
            return refuse(SignatureError::Malformed, "a key's state is active, retired, or revoked");
        }
        key.kid = *kid;
        key.since = *kSince;
        made.keys.push_back(std::move(key));
    }
    if (active > kMaximumActive) {
        return refuse(SignatureError::Malformed, "at most 16 keys are active at once");
    }
    return made;
}

result::Result<std::string> writePublisherKeySet(const PublisherKeySet& keys) {
    Value list = Value::array();
    for (const PublisherKey& key : keys.keys) {
        Value each = Value::object();
        each.add("kid", Value::string(key.kid));
        each.add("public_key", Value::string(hexOf(key.publicKey)));
        each.add("state", Value::string(std::string{stateText(key.state)}));
        each.add("since", Value::integer(key.since));
        list.push(std::move(each));
    }
    Value record = Value::object();
    record.add("schema", Value::integer(1));
    record.add("publisher", Value::string(keys.publisher));
    record.add("sequence", Value::integer(keys.sequence));
    record.add("updated_at", Value::integer(keys.updatedAt));
    record.add("head", Value::string(keys.head));
    record.add("keys", std::move(list));
    RAWFRAME_TRY_ASSIGN(std::string text, document::writeCanonicalRecord(record));
    // What is written must read back: the grammar is one.
    RAWFRAME_TRY(readPublisherKeySet(text));
    return text;
}

result::Status
verifyPublished(const PublisherKeySet& keys, std::span<const std::byte> message, const Envelope& envelope) {
    const auto kKey = std::ranges::find(keys.keys, envelope.kid, &PublisherKey::kid);
    if (kKey == keys.keys.end()) {
        return refuse(SignatureError::UnknownKey, "the key set does not list the signing key");
    }
    if (kKey->state == KeyState::Revoked) {
        return refuse(SignatureError::KeyRevoked, "publisher_key_revoked: the signing key is revoked");
    }
    if (!verify(kKey->publicKey, message, envelope.sig)) {
        return refuse(SignatureError::BadSignature, "the signature does not verify");
    }
    return {};
}

} // namespace rawframe::signature
