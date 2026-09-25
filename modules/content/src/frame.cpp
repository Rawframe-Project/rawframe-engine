#include "frame.h"

#include "rawframe/content/errors.h"

#define ZSTD_STATIC_LINKING_ONLY
#include <memory>
#include <zstd.h>

namespace rawframe::content {

namespace {

constexpr unsigned long long kMaximumWindow = std::uint64_t{8} * 1024 * 1024;
constexpr int kMaximumWindowLog = 23;

std::unexpected<result::Error> refuse(ContentError error, std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::DataLoss, kContentDomain, code(error), why).error()};
}

struct FreeContext {
    void operator()(ZSTD_DCtx* context) const noexcept {
        ZSTD_freeDCtx(context);
    }
};

} // namespace

result::Result<std::vector<std::byte>> decompressFrame(std::span<const std::byte> blob, std::uint64_t size) {
    ZSTD_FrameHeader header{};
    if (ZSTD_getFrameHeader(&header, blob.data(), blob.size()) != 0 || header.frameType != ZSTD_frame ||
        header.dictID != 0 || header.frameContentSize == ZSTD_CONTENTSIZE_UNKNOWN || header.frameContentSize != size ||
        header.windowSize > kMaximumWindow || ZSTD_findFrameCompressedSize(blob.data(), blob.size()) != blob.size()) {
        return refuse(ContentError::ManifestInvalid,
                      "a blob is not one Zstandard frame of its declared size, window, and no dictionary");
    }
    const std::unique_ptr<ZSTD_DCtx, FreeContext> kContext{ZSTD_createDCtx()};
    if (kContext == nullptr ||
        ZSTD_isError(ZSTD_DCtx_setParameter(kContext.get(), ZSTD_d_windowLogMax, kMaximumWindowLog)) != 0U) {
        return refuse(ContentError::ReadFailed, "a decompression context cannot be made");
    }
    std::vector<std::byte> content(static_cast<std::size_t>(size));
    const std::size_t kMade =
        ZSTD_decompressDCtx(kContext.get(), content.data(), content.size(), blob.data(), blob.size());
    if (ZSTD_isError(kMade) != 0U || kMade != content.size()) {
        return refuse(ContentError::ReadFailed, "a blob does not decompress to its declared size");
    }
    return content;
}

} // namespace rawframe::content
