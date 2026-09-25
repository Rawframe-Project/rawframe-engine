#include "rawframe/build/publisher_key.h"

#include "rawframe/base/secure_random.h"
#include "rawframe/base/sha256.h"
#include "rawframe/build/errors.h"
#include "rawframe/document/json.h"

#include <algorithm>
#include <memory>
#include <openssl/evp.h>

namespace rawframe::build {

namespace {

using document::Value;

std::unexpected<result::Error> refuse(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kBuildDomain, code(BuildError::BadKey), why).error()};
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
    for (const std::byte kByte : bytes) {
        text.push_back(kHex[static_cast<std::uint8_t>(kByte) >> 4U]);
        text.push_back(kHex[static_cast<std::uint8_t>(kByte) & 0xFU]);
    }
    return text;
}

bool bytesOf(std::string_view text, std::span<std::byte> out) noexcept {
    if (text.size() != out.size() * 2 || !std::ranges::all_of(text, [](char each) {
            return (each >= '0' && each <= '9') || (each >= 'a' && each <= 'f');
        })) {
        return false;
    }
    for (std::size_t index = 0; index < out.size(); ++index) {
        const auto kNibble = [](char digit) {
            return digit <= '9' ? digit - '0' : digit - 'a' + 10;
        };
        out[index] = static_cast<std::byte>((kNibble(text[index * 2]) << 4) | kNibble(text[(index * 2) + 1]));
    }
    return true;
}

bool publisherSegment(std::string_view text) noexcept {
    return !text.empty() && text.front() != '-' && text.back() != '-' && std::ranges::all_of(text, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '-';
    });
}

std::unique_ptr<EVP_PKEY, FreeKey> privateKeyOf(const PublisherKey& key) {
    return std::unique_ptr<EVP_PKEY, FreeKey>{EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(key.seed.data()), key.seed.size())};
}

} // namespace

result::Result<PublisherKey> generatePublisherKey(std::string_view publisher) {
    if (!publisherSegment(publisher)) {
        return refuse("a publisher is [a-z0-9]([a-z0-9-]*[a-z0-9])?");
    }
    PublisherKey key{.publisher = std::string{publisher}, .kid = {}, .seed = {}};
    std::array<std::byte, 8> kid{};
    if (!base::fillSecureRandom(key.seed) || !base::fillSecureRandom(kid)) {
        return refuse("the operating system's secure source is unavailable");
    }
    key.kid = hexOf(kid);
    return key;
}

std::string writePublisherKey(const PublisherKey& key) {
    Value record = Value::object();
    record.add("publisher", Value::string(key.publisher));
    record.add("kid", Value::string(key.kid));
    record.add("seed", Value::string(hexOf(key.seed)));
    return *document::writeCanonicalRecord(record);
}

result::Result<PublisherKey> readPublisherKey(std::string_view text) {
    auto parsed = document::parseCanonicalRecord(text, document::ReadLimits{.maximumBytes = 1024});
    if (!parsed.has_value()) {
        return refuse("a publisher key is a canonical record");
    }
    const Value* publisher = parsed->find("publisher");
    const Value* kid = parsed->find("kid");
    const Value* seed = parsed->find("seed");
    PublisherKey key;
    if (parsed->names().size() != 3 || publisher == nullptr || kid == nullptr || seed == nullptr ||
        publisher->kind() != Value::Kind::String || kid->kind() != Value::Kind::String ||
        seed->kind() != Value::Kind::String || !publisherSegment(*publisher->text()) ||
        !signature::validKeyId(*kid->text()) || !bytesOf(*seed->text(), key.seed)) {
        return refuse("a publisher key is exactly {publisher, kid, seed}");
    }
    key.publisher = *publisher->text();
    key.kid = *kid->text();
    return key;
}

result::Result<signature::PublicKey> publicKeyOf(const PublisherKey& key) {
    const auto kPrivate = privateKeyOf(key);
    signature::PublicKey made{};
    std::size_t length = made.size();
    if (kPrivate == nullptr ||
        EVP_PKEY_get_raw_public_key(kPrivate.get(), reinterpret_cast<unsigned char*>(made.data()), &length) != 1 ||
        length != made.size()) {
        return refuse("the key's public half cannot be made");
    }
    return made;
}

result::Result<signature::PublisherKeySet> keySetOf(const PublisherKey& key, std::int64_t now) {
    RAWFRAME_TRY_ASSIGN(const signature::PublicKey kPublic, publicKeyOf(key));
    Value made = Value::object();
    made.add("kid", Value::string(key.kid));
    made.add("public_key", Value::string(hexOf(kPublic)));
    const std::string kMade = *document::writeCanonicalRecord(made);
    const base::Sha256Digest kHead = base::sha256(kMade);
    return signature::PublisherKeySet{
        .publisher = key.publisher,
        .sequence = 1,
        .updatedAt = now,
        .head = "sha256:" + hexOf(kHead),
        .keys = {signature::PublisherKey{
            .kid = key.kid, .publicKey = kPublic, .state = signature::KeyState::Active, .since = now}}};
}

result::Result<signature::Envelope> sign(const PublisherKey& key, std::span<const std::byte> message) {
    const auto kPrivate = privateKeyOf(key);
    const std::unique_ptr<EVP_MD_CTX, FreeContext> kContext{EVP_MD_CTX_new()};
    signature::Envelope envelope{.kid = key.kid, .sig = {}};
    std::size_t length = envelope.sig.size();
    if (kPrivate == nullptr || kContext == nullptr ||
        EVP_DigestSignInit(kContext.get(), nullptr, nullptr, nullptr, kPrivate.get()) != 1 ||
        EVP_DigestSign(kContext.get(),
                       reinterpret_cast<unsigned char*>(envelope.sig.data()),
                       &length,
                       reinterpret_cast<const unsigned char*>(message.data()),
                       message.size()) != 1 ||
        length != envelope.sig.size()) {
        return refuse("the message cannot be signed");
    }
    return envelope;
}

} // namespace rawframe::build
