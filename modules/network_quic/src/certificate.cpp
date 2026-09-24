#include "rawframe/network_quic/certificate.h"

#include "rawframe/network_quic/errors.h"
#include "tls.h"

#include <array>
#include <climits>
#include <memory>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

namespace rawframe::network_quic {

namespace {

template <auto kFree> struct Free {
    template <typename T> void operator()(T* pointer) const noexcept {
        kFree(pointer);
    }
};

using Key = std::unique_ptr<EVP_PKEY, Free<&EVP_PKEY_free>>;
using X509Certificate = std::unique_ptr<X509, Free<&X509_free>>;
using Bio = std::unique_ptr<BIO, Free<&BIO_free>>;
using Number = std::unique_ptr<BIGNUM, Free<&BN_free>>;
using Pkcs12 = std::unique_ptr<PKCS12, Free<&PKCS12_free>>;

constexpr long kSecondsPerDay = 86'400;
constexpr std::size_t kMaximumCommonName = 64;

std::unexpected<result::Error> bad(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kQuicDomain, code(QuicError::BadCertificate), why);
}

std::string drain(BIO* bio) {
    char* data = nullptr;
    const long kLength = BIO_get_mem_data(bio, &data);
    return kLength > 0 ? std::string(data, static_cast<std::size_t>(kLength)) : std::string{};
}

Bio readable(const std::string& pem) {
    if (pem.size() > static_cast<std::size_t>(INT_MAX)) {
        return nullptr;
    }
    return Bio{BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()))};
}

X509Certificate readCertificate(const Certificate& certificate) {
    const Bio kBio = readable(certificate.certificatePem);
    return kBio == nullptr ? nullptr : X509Certificate{PEM_read_bio_X509(kBio.get(), nullptr, nullptr, nullptr)};
}

Key readKey(const Certificate& certificate) {
    const Bio kBio = readable(certificate.privateKeyPem);
    return kBio == nullptr ? nullptr : Key{PEM_read_bio_PrivateKey(kBio.get(), nullptr, nullptr, nullptr)};
}

/// A positive random 127-bit serial: RFC 5280 wants it unique and at most
/// 20 bytes, and a leading zero bit keeps it positive.
bool setSerial(X509* certificate) {
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        return false;
    }
    bytes[0] &= 0x7FU;
    const Number kSerial{BN_bin2bn(bytes.data(), static_cast<int>(bytes.size()), nullptr)};
    return kSerial != nullptr && BN_to_ASN1_INTEGER(kSerial.get(), X509_get_serialNumber(certificate)) != nullptr;
}

} // namespace

result::Result<Certificate> makeSelfSignedCertificate(std::string_view commonName, unsigned validDays) {
    if (commonName.empty() || commonName.size() > kMaximumCommonName || validDays == 0 || validDays > 3650) {
        return bad("a certificate needs a name of 1 to 64 bytes and 1 to 3650 days");
    }
    const Key kKey{EVP_EC_gen("P-256")};
    const X509Certificate kCertificate{X509_new()};
    if (kKey == nullptr || kCertificate == nullptr) {
        return bad("OpenSSL could not make a key or a certificate");
    }
    X509* const kRaw = kCertificate.get();
    X509_NAME* const kName = X509_get_subject_name(kRaw);
    // An hour of slack before now, for a peer whose clock is behind.
    const bool kMade =
        X509_set_version(kRaw, X509_VERSION_3) == 1 && setSerial(kRaw) &&
        X509_gmtime_adj(X509_getm_notBefore(kRaw), -3600) != nullptr &&
        X509_gmtime_adj(X509_getm_notAfter(kRaw), static_cast<long>(validDays) * kSecondsPerDay) != nullptr &&
        X509_set_pubkey(kRaw, kKey.get()) == 1 &&
        X509_NAME_add_entry_by_txt(kName,
                                   "CN",
                                   MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>(commonName.data()),
                                   static_cast<int>(commonName.size()),
                                   -1,
                                   0) == 1 &&
        X509_set_issuer_name(kRaw, kName) == 1 && X509_sign(kRaw, kKey.get(), EVP_sha256()) > 0;
    const Bio kCertificateText{BIO_new(BIO_s_mem())};
    const Bio kKeyText{BIO_new(BIO_s_mem())};
    if (!kMade || kCertificateText == nullptr || kKeyText == nullptr ||
        PEM_write_bio_X509(kCertificateText.get(), kRaw) != 1 ||
        PEM_write_bio_PrivateKey(kKeyText.get(), kKey.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        return bad("OpenSSL could not sign or write the certificate");
    }
    return Certificate{.certificatePem = drain(kCertificateText.get()), .privateKeyPem = drain(kKeyText.get())};
}

result::Result<Fingerprint> fingerprintOf(const Certificate& certificate) {
    const X509Certificate kCertificate = readCertificate(certificate);
    if (kCertificate == nullptr) {
        return bad("the certificate is not PEM that OpenSSL can read");
    }
    unsigned char* der = nullptr;
    const int kLength = i2d_X509(kCertificate.get(), &der);
    if (kLength <= 0) {
        return bad("the certificate could not be written as DER");
    }
    const Fingerprint kFingerprint = base::sha256(std::as_bytes(std::span{der, static_cast<std::size_t>(kLength)}));
    OPENSSL_free(der);
    return kFingerprint;
}

std::string formatFingerprint(const Fingerprint& fingerprint) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    text.reserve(fingerprint.size() * 2);
    for (const std::byte kByte : fingerprint) {
        text.push_back(kDigits[std::to_integer<unsigned>(kByte) >> 4U]);
        text.push_back(kDigits[std::to_integer<unsigned>(kByte) & 0xFU]);
    }
    return text;
}

result::Result<Fingerprint> parseFingerprint(std::string_view text) {
    const auto kDigit = [](char digit) -> int {
        if (digit >= '0' && digit <= '9') {
            return digit - '0';
        }
        if (digit >= 'a' && digit <= 'f') {
            return digit - 'a' + 10;
        }
        if (digit >= 'A' && digit <= 'F') {
            return digit - 'A' + 10;
        }
        return -1;
    };
    Fingerprint fingerprint{};
    if (text.size() != fingerprint.size() * 2) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kQuicDomain,
                            code(QuicError::BadFingerprint),
                            "a fingerprint is 64 hexadecimal digits");
    }
    for (std::size_t index = 0; index < fingerprint.size(); ++index) {
        const int kHigh = kDigit(text[2 * index]);
        const int kLow = kDigit(text[(2 * index) + 1]);
        if (kHigh < 0 || kLow < 0) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kQuicDomain,
                                code(QuicError::BadFingerprint),
                                "a fingerprint is 64 hexadecimal digits");
        }
        fingerprint[index] = static_cast<std::byte>((kHigh << 4) | kLow);
    }
    return fingerprint;
}

result::Result<std::vector<std::byte>> pkcs12Of(const Certificate& certificate) {
    const X509Certificate kCertificate = readCertificate(certificate);
    const Key kKey = readKey(certificate);
    if (kCertificate == nullptr || kKey == nullptr) {
        return bad("the certificate or its key is not PEM that OpenSSL can read");
    }
    if (X509_check_private_key(kCertificate.get(), kKey.get()) != 1) {
        return bad("the private key does not belong to the certificate");
    }
    // No encryption (-1) and no MAC (-1): the blob never leaves this process.
    const Pkcs12 kBundle{PKCS12_create(nullptr, nullptr, kKey.get(), kCertificate.get(), nullptr, -1, -1, 0, -1, 0)};
    if (kBundle == nullptr) {
        return bad("OpenSSL could not bundle the certificate and key");
    }
    unsigned char* der = nullptr;
    const int kLength = i2d_PKCS12(kBundle.get(), &der);
    if (kLength <= 0) {
        return bad("OpenSSL could not write the bundle");
    }
    const auto kBytes = std::as_bytes(std::span{der, static_cast<std::size_t>(kLength)});
    std::vector<std::byte> blob{kBytes.begin(), kBytes.end()};
    OPENSSL_free(der);
    return blob;
}

} // namespace rawframe::network_quic
