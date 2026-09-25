#pragma once

// The browser's transport (ADR-0084 decision 3, D170): WebTransport over
// HTTP/3, which is QUIC, reached through the page. The engine cannot open a
// WebTransport session from WebAssembly; the page's JavaScript can. So
// every provider operation here is a call out to the page (the
// `rawframe_web_transport` imports), and events come back when the provider
// polls. The engine keeps every bound the profile sets on what it sends and
// on the connections it knows; the page keeps the queue bounds it is handed
// at `open`, and numbers streams as QUIC does.
//
// The page's side, one function per import, each answering 0 for done or a
// NetworkError code:
//
//   open(maximumQueuedEvents, maximumQueuedBytes) -> provider (0 refused)
//   listen(provider, name, length) -> code
//   connect(provider, name, length) -> connection (0 refused)
//   open_stream(provider, connection, unidirectional) -> stream + 1 (0 refused)
//   send(provider, connection, stream, bytes, length) -> code
//   send_datagram(provider, connection, bytes, length) -> code
//   close(provider, connection, reason): both ends hear Closed with `reason`
//   poll(provider, into, capacity) -> event size, 0 for none, or -size needed
//   release(provider)
//
// An event is written as: kind (u8), reason (u8), two zero bytes, the
// connection (u32), the stream (u64), the byte count (u32), four zero
// bytes, then the bytes: kinds and reasons are network::EventKind and
// network::CloseReason's values.

#include "rawframe/network/provider.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <memory>

namespace rawframe::network_web {

/// The fixed part of an event the page writes, before its bytes.
inline constexpr std::size_t kEventHeaderBytes = 24;

/// A provider over the page's transport. Refuses (`invalid_argument`) a
/// profile with any bound at zero, and (`unavailable`) a page that gave no
/// provider.
[[nodiscard]] result::Result<std::unique_ptr<network::Provider>> webProvider(const network::ProviderProfile& profile);

} // namespace rawframe::network_web
