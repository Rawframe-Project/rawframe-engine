#pragma once

// What the provider hands MsQuic, made from what an operator hands us.

#include "rawframe/network_quic/certificate.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <vector>

namespace rawframe::network_quic {

/// The certificate and key as one unencrypted PKCS #12 blob, which MsQuic
/// loads from memory. Refuses a key that does not belong to the certificate.
[[nodiscard]] result::Result<std::vector<std::byte>> pkcs12Of(const Certificate& certificate);

} // namespace rawframe::network_quic
