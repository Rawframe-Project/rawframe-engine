#pragma once

// A server's TLS identity and the fingerprint a client pins it by. Rawframe
// does not ask a certificate authority who a game server is: an operator
// hands clients the SHA-256 of the server's certificate, and a client accepts
// that certificate and no other (D27).

#include "rawframe/base/sha256.h"
#include "rawframe/result/result.h"

#include <string>
#include <string_view>

namespace rawframe::network_quic {

/// A certificate and its private key, as PEM text.
struct Certificate {
    std::string certificatePem;
    std::string privateKeyPem;
};

/// SHA-256 of a certificate's DER bytes.
using Fingerprint = base::Sha256Digest;

/// A new self-signed P-256 certificate for `commonName`, valid for
/// `validDays` from now. For a server with no issued certificate, and tests.
[[nodiscard]] result::Result<Certificate> makeSelfSignedCertificate(std::string_view commonName, unsigned validDays);

/// The fingerprint of the certificate in `certificate.certificatePem`.
[[nodiscard]] result::Result<Fingerprint> fingerprintOf(const Certificate& certificate);

/// 64 lower-case hexadecimal digits, and back. Parsing takes either case.
[[nodiscard]] std::string formatFingerprint(const Fingerprint& fingerprint);
[[nodiscard]] result::Result<Fingerprint> parseFingerprint(std::string_view text);

} // namespace rawframe::network_quic
