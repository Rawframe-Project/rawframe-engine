// Publisher signatures: RFC 8032's first vector verifies and nothing near it
// does, envelopes and key sets are their canonical records and nothing
// else, and a verdict is SPEC-0023's: active and retired keys verify,
// revoked ones are refused whatever the signature, and unknown ones are
// refused as unknown.

#include "rawframe/signature/errors.h"
#include "rawframe/signature/signature.h"
#include "rawframe/test/mutations.h"
#include "rawframe/test/test.h"

#include <cstdio>
#include <openssl/evp.h>
#include <string>

using namespace rawframe;
using namespace rawframe::signature;

namespace {

template <std::size_t Size> std::array<std::byte, Size> fromHex(std::string_view text) {
    std::array<std::byte, Size> bytes{};
    for (std::size_t index = 0; index < Size; ++index) {
        bytes[index] = static_cast<std::byte>(std::stoi(std::string{text.substr(index * 2, 2)}, nullptr, 16));
    }
    return bytes;
}

std::span<const std::byte> bytesOf(std::string_view text) {
    return std::as_bytes(std::span{text.data(), text.size()});
}

/// An Ed25519 key from its 32-byte seed, as packaging tooling makes one.
struct Signer {
    std::array<std::byte, 32> seed;
    EVP_PKEY* key;

    explicit Signer(std::uint8_t fill) : seed{} {
        seed.fill(std::byte{fill});
        key = EVP_PKEY_new_raw_private_key(
            EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(seed.data()), seed.size());
    }
    ~Signer() {
        EVP_PKEY_free(key);
    }
    Signer(const Signer&) = delete;
    Signer& operator=(const Signer&) = delete;

    [[nodiscard]] PublicKey publicKey() const {
        PublicKey made{};
        std::size_t length = made.size();
        EVP_PKEY_get_raw_public_key(key, reinterpret_cast<unsigned char*>(made.data()), &length);
        return made;
    }
    [[nodiscard]] SignatureBytes sign(std::span<const std::byte> message) const {
        SignatureBytes made{};
        std::size_t length = made.size();
        EVP_MD_CTX* context = EVP_MD_CTX_new();
        EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key);
        EVP_DigestSign(context,
                       reinterpret_cast<unsigned char*>(made.data()),
                       &length,
                       reinterpret_cast<const unsigned char*>(message.data()),
                       message.size());
        EVP_MD_CTX_free(context);
        return made;
    }
};

PublisherKeySet keySetOf(std::vector<PublisherKey> keys) {
    return PublisherKeySet{.publisher = "rawframe",
                           .sequence = 1,
                           .updatedAt = 1'790'000'000,
                           .head = "sha256:" + std::string(64, '0'),
                           .keys = std::move(keys)};
}

bool refusedAs(const result::Status& status, SignatureError error) {
    return !status.has_value() && status.error().domain() == kSignatureDomain && status.error().code() == code(error);
}

} // namespace

RAWFRAME_TEST(Rfc8032VectorOneVerifies) {
    const PublicKey kKey = fromHex<32>("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    const SignatureBytes kSignature = fromHex<64>(
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25"
        "bf5f0595bbe24655141438e7a100b");
    RAWFRAME_EXPECT(verify(kKey, {}, kSignature));
    // One bit off anywhere, or one byte of message, and it does not.
    SignatureBytes flipped = kSignature;
    flipped[10] ^= std::byte{1};
    RAWFRAME_EXPECT(!verify(kKey, {}, flipped));
    PublicKey other = kKey;
    other[0] ^= std::byte{1};
    RAWFRAME_EXPECT(!verify(other, {}, kSignature));
    RAWFRAME_EXPECT(!verify(kKey, bytesOf("x"), kSignature));
}

RAWFRAME_TEST(EnvelopesAndKeySetsAreTheirRecords) {
    const Signer kSigner{7};
    const Envelope kEnvelope{.kid = "00112233445566ff", .sig = kSigner.sign(bytesOf("manifest"))};
    const std::string kText = writeEnvelope(kEnvelope);
    const auto kRead = readEnvelope(kText);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->kid == kEnvelope.kid && kRead->sig == kEnvelope.sig);
    for (const std::string_view kBad :
         {std::string_view{"{\"kid\":\"00112233445566FF\",\"sig\":\"00\"}"},
          std::string_view{"{\"kid\":\"0011\",\"sig\":\"00\"}"},
          std::string_view{"{\"alg\":\"ed25519\",\"kid\":\"00112233445566ff\",\"sig\":\"00\"}"}}) {
        RAWFRAME_EXPECT(!readEnvelope(kBad).has_value());
    }
    // An envelope with whitespace is not the canonical record.
    std::string spaced = kText;
    spaced.insert(1, " ");
    RAWFRAME_EXPECT(!readEnvelope(spaced).has_value());

    const PublisherKeySet kKeys = keySetOf({PublisherKey{
        .kid = "00112233445566ff", .publicKey = kSigner.publicKey(), .state = KeyState::Active, .since = 1}});
    const auto kKeysText = writePublisherKeySet(kKeys);
    RAWFRAME_EXPECT(kKeysText.has_value());
    const auto kKeysRead = readPublisherKeySet(kKeysText.value_or(""));
    RAWFRAME_EXPECT(kKeysRead.has_value() && kKeysRead->keys.size() == 1 &&
                    kKeysRead->keys[0].publicKey == kSigner.publicKey());
    // A key identity twice, a state outside the three, no keys, and a
    // publisher outside its grammar are refused.
    PublisherKeySet twice = kKeys;
    twice.keys.push_back(twice.keys[0]);
    RAWFRAME_EXPECT(!writePublisherKeySet(twice).has_value());
    RAWFRAME_EXPECT(!writePublisherKeySet(keySetOf({})).has_value());
    PublisherKeySet named = kKeys;
    named.publisher = "Rawframe";
    RAWFRAME_EXPECT(!writePublisherKeySet(named).has_value());
    std::string suspended = *kKeysText;
    suspended.replace(suspended.find("\"active\""), 8, "\"paused\"");
    RAWFRAME_EXPECT(!readPublisherKeySet(suspended).has_value());
}

RAWFRAME_TEST(AVerdictFollowsTheKeysState) {
    const Signer kActive{1};
    const Signer kRetired{2};
    const Signer kRevoked{3};
    const PublisherKeySet kKeys = keySetOf(
        {PublisherKey{.kid = "000000000000000a", .publicKey = kActive.publicKey(), .state = KeyState::Active},
         PublisherKey{.kid = "000000000000000b", .publicKey = kRetired.publicKey(), .state = KeyState::Retired},
         PublisherKey{.kid = "000000000000000c", .publicKey = kRevoked.publicKey(), .state = KeyState::Revoked}});
    const std::span<const std::byte> kManifest = bytesOf("{\"schema\":1}");
    RAWFRAME_EXPECT(verifyPublished(kKeys, kManifest, {.kid = "000000000000000a", .sig = kActive.sign(kManifest)}));
    RAWFRAME_EXPECT(verifyPublished(kKeys, kManifest, {.kid = "000000000000000b", .sig = kRetired.sign(kManifest)}));
    // Revoked: refused though the signature is good.
    RAWFRAME_EXPECT(
        refusedAs(verifyPublished(kKeys, kManifest, {.kid = "000000000000000c", .sig = kRevoked.sign(kManifest)}),
                  SignatureError::KeyRevoked));
    RAWFRAME_EXPECT(
        refusedAs(verifyPublished(kKeys, kManifest, {.kid = "000000000000000d", .sig = kActive.sign(kManifest)}),
                  SignatureError::UnknownKey));
    // Another key's signature under this kid, and another message.
    RAWFRAME_EXPECT(
        refusedAs(verifyPublished(kKeys, kManifest, {.kid = "000000000000000a", .sig = kRetired.sign(kManifest)}),
                  SignatureError::BadSignature));
    RAWFRAME_EXPECT(refusedAs(
        verifyPublished(kKeys, bytesOf("{\"schema\":2}"), {.kid = "000000000000000a", .sig = kActive.sign(kManifest)}),
        SignatureError::BadSignature));
}

RAWFRAME_TEST(HostileEnvelopesAndKeySetsReadOnlyAsTheyWrite) {
    // A Build's signature and a publisher's key set come over the network
    // (D190): seeded mutations of each, and whatever the reader accepts
    // writes back to the very same bytes.
    const Signer kSigner{7};
    const std::string kEnvelope =
        writeEnvelope(Envelope{.kid = "00112233445566ff", .sig = kSigner.sign(bytesOf("manifest"))});
    const auto kKeys = writePublisherKeySet(keySetOf(
        {PublisherKey{
             .kid = "00112233445566ff", .publicKey = kSigner.publicKey(), .state = KeyState::Active, .since = 1},
         PublisherKey{
             .kid = "00112233445566fe", .publicKey = Signer{8}.publicKey(), .state = KeyState::Retired, .since = 2}}));
    RAWFRAME_EXPECT(kKeys.has_value());
    if (!kKeys.has_value()) {
        return;
    }
    constexpr std::string_view kInserted = "{}[]\",:0123456789abcdef";
    test::Mutations mutations;
    int envelopes = 0;
    int keySets = 0;
    for (int round = 0; round < 20'000; ++round) {
        const std::string kText = mutations.mutate(kEnvelope, kInserted);
        if (const auto kRead = readEnvelope(kText)) {
            ++envelopes;
            RAWFRAME_EXPECT(writeEnvelope(*kRead) == kText);
        }
        const std::string kKeysText = mutations.mutate(*kKeys, kInserted);
        if (const auto kRead = readPublisherKeySet(kKeysText)) {
            ++keySets;
            RAWFRAME_EXPECT(writePublisherKeySet(*kRead).value_or("") == kKeysText);
        }
    }
    std::printf("  accepted: %d envelopes, %d key sets of 20000 each\n", envelopes, keySets);
    RAWFRAME_EXPECT(envelopes > 0 && keySets > 0);
}
