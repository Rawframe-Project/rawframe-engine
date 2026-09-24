#include "rawframe/result/error.h"

#include "rawframe/base/assert.h"
#include "rawframe/result/result.h"

#include <algorithm>
#include <array>
#include <new>
#include <optional>
#include <utility>

namespace rawframe::result {

namespace {

/// A bounded copy of some text: the bytes that fit, cut at a code point
/// boundary, and whether anything was cut.
template <std::size_t Capacity> struct BoundedText {
    std::array<char, Capacity> bytes{};
    std::size_t length = 0;

    /// Copies `text`, keeping at most Capacity bytes and never splitting a UTF-8
    /// code point. Returns true if the text was cut.
    bool assign(std::string_view text) noexcept {
        std::size_t keep = text.size();
        bool cut = false;
        if (keep > Capacity) {
            keep = Capacity;
            cut = true;
            // The first excluded byte continues a code point that started inside
            // the kept range: back off to that code point's first byte.
            while (keep > 0 && (static_cast<unsigned char>(text[keep]) & 0xC0U) == 0x80U) {
                --keep;
            }
        }
        std::copy_n(text.data(), keep, bytes.data());
        length = keep;
        return cut;
    }

    [[nodiscard]] std::string_view view() const noexcept {
        return std::string_view{bytes.data(), length};
    }
};

} // namespace

/// Everything but the class, in one allocation. Frames and context fields hold
/// views into this object's own text storage, so it is never copied bytewise:
/// copyFrom rebuilds the views.
struct ErrorDetail {
    ErrorDomain domain;
    ErrorCode code;
    std::source_location origin;

    BoundedText<kMaximumDescriptionBytes> description;
    bool descriptionTruncated = false;

    std::array<BoundedText<kMaximumFrameDescriptionBytes>, kMaximumFrames> frameText{};
    std::array<Frame, kMaximumFrames> frames{};
    std::size_t frameCount = 0;
    std::size_t framesDropped = 0;

    std::array<BoundedText<kMaximumContextKeyBytes>, kMaximumContextFields> contextKeys{};
    std::array<BoundedText<kMaximumContextValueBytes>, kMaximumContextFields> contextValues{};
    std::array<ContextField, kMaximumContextFields> context{};
    std::size_t contextCount = 0;
    std::size_t contextDropped = 0;
    bool contextTruncated = false;

    std::optional<Error> cause;
    bool causeDropped = false;

    ErrorDetail() = default;
    ErrorDetail(const ErrorDetail&) = delete;
    ErrorDetail& operator=(const ErrorDetail&) = delete;

    void addFrame(std::string_view text, std::source_location location) noexcept {
        if (frameCount == kMaximumFrames) {
            ++framesDropped;
            return;
        }
        static_cast<void>(frameText[frameCount].assign(text));
        frames[frameCount] = Frame{frameText[frameCount].view(), location};
        ++frameCount;
    }

    void addContext(std::string_view key, std::string_view value) noexcept {
        if (contextCount == kMaximumContextFields) {
            ++contextDropped;
            return;
        }
        contextTruncated = contextKeys[contextCount].assign(key) || contextTruncated;
        contextTruncated = contextValues[contextCount].assign(value) || contextTruncated;
        context[contextCount] = ContextField{contextKeys[contextCount].view(), contextValues[contextCount].view()};
        ++contextCount;
    }

    /// Copies everything except the cause, rebuilding views into this object.
    void copyFrom(const ErrorDetail& other) noexcept {
        domain = other.domain;
        code = other.code;
        origin = other.origin;
        static_cast<void>(description.assign(other.description.view()));
        descriptionTruncated = other.descriptionTruncated;
        for (std::size_t index = 0; index < other.frameCount; ++index) {
            addFrame(other.frames[index].description, other.frames[index].location);
        }
        framesDropped = other.framesDropped;
        for (std::size_t index = 0; index < other.contextCount; ++index) {
            addContext(other.context[index].key, other.context[index].value);
        }
        contextDropped = other.contextDropped;
        contextTruncated = other.contextTruncated;
        causeDropped = other.causeDropped;
    }
};

namespace {

/// The one allocation per Error. Heap exhaustion here is process-fatal: an
/// error that must allocate to report a failed allocation is a recursion.
std::unique_ptr<ErrorDetail> allocateDetail() {
    ErrorDetail* detail = new (std::nothrow) ErrorDetail{};
    RAWFRAME_CHECK(detail != nullptr, "out of memory while constructing an Error");
    return std::unique_ptr<ErrorDetail>{detail};
}

/// How many causes hang below an error.
std::size_t causeDepth(const Error& error) noexcept {
    std::size_t depth = 0;
    for (const Error* cause = error.cause(); cause != nullptr; cause = cause->cause()) {
        ++depth;
    }
    return depth;
}

} // namespace

Error::Error(ErrorClass errorClass, std::unique_ptr<ErrorDetail> detail) noexcept
    : class_(errorClass), detail_(std::move(detail)) {
}

Error::Error(Error&&) noexcept = default;
Error& Error::operator=(Error&&) noexcept = default;
Error::~Error() = default;

Error Error::clone() const {
    RAWFRAME_ASSERT(detail_ != nullptr, "clone of a moved-from Error");
    auto copy = allocateDetail();
    copy->copyFrom(*detail_);
    if (detail_->cause.has_value()) {
        copy->cause.emplace(detail_->cause->clone());
    }
    return Error{class_, std::move(copy)};
}

ErrorDomain Error::domain() const noexcept {
    return detail_->domain;
}

ErrorCode Error::code() const noexcept {
    return detail_->code;
}

std::string_view Error::description() const noexcept {
    return detail_->description.view();
}

bool Error::descriptionTruncated() const noexcept {
    return detail_->descriptionTruncated;
}

std::source_location Error::origin() const noexcept {
    return detail_->origin;
}

std::span<const Frame> Error::frames() const noexcept {
    return std::span<const Frame>{detail_->frames.data(), detail_->frameCount};
}

std::size_t Error::framesDropped() const noexcept {
    return detail_->framesDropped;
}

std::span<const ContextField> Error::context() const noexcept {
    return std::span<const ContextField>{detail_->context.data(), detail_->contextCount};
}

std::size_t Error::contextFieldsDropped() const noexcept {
    return detail_->contextDropped;
}

bool Error::contextTruncated() const noexcept {
    return detail_->contextTruncated;
}

const Error* Error::cause() const noexcept {
    return detail_->cause.has_value() ? &*detail_->cause : nullptr;
}

bool Error::causeDropped() const noexcept {
    return detail_->causeDropped;
}

Error Error::withFrame(std::string_view description, std::source_location location) && {
    auto next = allocateDetail();
    next->copyFrom(*detail_);
    next->addFrame(description, location);
    // The original is consumed, so its cause chain moves rather than copies.
    next->cause = std::move(detail_->cause);
    return Error{class_, std::move(next)};
}

Error Error::withContext(std::string_view key, std::string_view value) && {
    auto next = allocateDetail();
    next->copyFrom(*detail_);
    next->addContext(key, value);
    next->cause = std::move(detail_->cause);
    return Error{class_, std::move(next)};
}

Error Error::mappedTo(ErrorClass errorClass,
                      ErrorDomain domain,
                      ErrorCode code,
                      std::string_view description,
                      std::source_location location) && {
    auto next = allocateDetail();
    next->domain = domain;
    next->code = code;
    next->origin = location;
    next->descriptionTruncated = next->description.assign(description);
    // The original becomes the cause only while the chain stays within its
    // bound; otherwise it is dropped and the drop is recorded.
    if (causeDepth(*this) + 1 <= kMaximumCauseDepth) {
        next->cause.emplace(std::move(*this));
    } else {
        next->causeDropped = true;
    }
    return Error{errorClass, std::move(next)};
}

std::unexpected<Error> fail(ErrorClass errorClass,
                            ErrorDomain domain,
                            ErrorCode code,
                            std::string_view description,
                            std::source_location origin) {
    auto detail = allocateDetail();
    detail->domain = domain;
    detail->code = code;
    detail->origin = origin;
    detail->descriptionTruncated = detail->description.assign(description);
    return std::unexpected<Error>{Error{errorClass, std::move(detail)}};
}

} // namespace rawframe::result
