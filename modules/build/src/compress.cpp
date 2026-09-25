#include "compress.h"

#define ZSTD_STATIC_LINKING_ONLY
#include <memory>
#include <zstd.h>

namespace rawframe::build {

namespace {

struct FreeContext {
    void operator()(ZSTD_CCtx* context) const noexcept {
        ZSTD_freeCCtx(context);
    }
};

// Packer generation 2's parameters, pinned with the Zstandard revision: a
// change to either is a new generation (SPEC-0021). Level 12 trades a
// little size for packing at tens of megabytes a second; a 4 MiB window
// holds any chunk whole; the content size is always written, and neither a
// checksum (the digests are the checks) nor a dictionary identity.
constexpr int kLevel = 12;
constexpr int kWindowLog = 22;

} // namespace

std::optional<std::vector<std::byte>> compressChunk(std::span<const std::byte> chunk) {
    const std::unique_ptr<ZSTD_CCtx, FreeContext> kContext{ZSTD_createCCtx()};
    if (kContext == nullptr) {
        return std::nullopt;
    }
    ZSTD_CCtx* const kRaw = kContext.get();
    const bool kSet = ZSTD_isError(ZSTD_CCtx_setParameter(kRaw, ZSTD_c_compressionLevel, kLevel)) == 0U &&
                      ZSTD_isError(ZSTD_CCtx_setParameter(kRaw, ZSTD_c_windowLog, kWindowLog)) == 0U &&
                      ZSTD_isError(ZSTD_CCtx_setParameter(kRaw, ZSTD_c_contentSizeFlag, 1)) == 0U &&
                      ZSTD_isError(ZSTD_CCtx_setParameter(kRaw, ZSTD_c_checksumFlag, 0)) == 0U &&
                      ZSTD_isError(ZSTD_CCtx_setParameter(kRaw, ZSTD_c_dictIDFlag, 0)) == 0U;
    if (!kSet) {
        return std::nullopt;
    }
    std::vector<std::byte> blob(ZSTD_compressBound(chunk.size()));
    const std::size_t kMade = ZSTD_compress2(kRaw, blob.data(), blob.size(), chunk.data(), chunk.size());
    if (ZSTD_isError(kMade) != 0U || kMade >= chunk.size()) {
        return std::nullopt;
    }
    blob.resize(kMade);
    return blob;
}

} // namespace rawframe::build
