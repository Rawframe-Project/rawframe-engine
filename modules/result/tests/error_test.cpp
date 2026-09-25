#include "rawframe/result/error.h"
#include "rawframe/result/result.h"
#include "rawframe/test/test.h"

#include <array>
#include <string>
#include <string_view>
#include <type_traits>

using namespace rawframe::result;

namespace {

constexpr ErrorDomain kTestDomain{rawframe::base::parseBits128Hex("5a1e0c7d9f3b4e21a8c6d0f2b4e6a8c0").value};
constexpr ErrorDomain kOtherDomain{rawframe::base::parseBits128Hex("0f1e2d3c4b5a69788796a5b4c3d2e1f0").value};

Error make(std::string_view description = "it failed") {
    return fail(ErrorClass::NotFound, kTestDomain, ErrorCode{7}, description).error();
}

bool isValidUtf8(std::string_view text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto kLead = static_cast<unsigned char>(text[index]);
        std::size_t length = 1;
        if (kLead >= 0xF0) {
            length = 4;
        } else if (kLead >= 0xE0) {
            length = 3;
        } else if (kLead >= 0xC0) {
            length = 2;
        } else if (kLead >= 0x80) {
            return false;
        }
        if (index + length > text.size()) {
            return false;
        }
        index += length;
    }
    return true;
}

} // namespace

static_assert(sizeof(Error) == 2 * sizeof(void*), "an Error is a class and one pointer");
static_assert(alignof(Error) == alignof(void*));
static_assert(std::is_nothrow_move_constructible_v<Error> && std::is_nothrow_move_assignable_v<Error>);
static_assert(!std::is_copy_constructible_v<Error> && !std::is_copy_assignable_v<Error>);

RAWFRAME_TEST(ClassNamesAreTheSpec0004Names) {
    constexpr std::array<std::pair<ErrorClass, std::string_view>, 13> kTable = {{
        {ErrorClass::InvalidArgument, "invalid_argument"},
        {ErrorClass::FailedPrecondition, "failed_precondition"},
        {ErrorClass::OutOfRange, "out_of_range"},
        {ErrorClass::NotFound, "not_found"},
        {ErrorClass::AlreadyExists, "already_exists"},
        {ErrorClass::Conflict, "conflict"},
        {ErrorClass::Unauthenticated, "unauthenticated"},
        {ErrorClass::PermissionDenied, "permission_denied"},
        {ErrorClass::Unsupported, "unsupported"},
        {ErrorClass::ResourceExhausted, "resource_exhausted"},
        {ErrorClass::Unavailable, "unavailable"},
        {ErrorClass::DataLoss, "data_loss"},
        {ErrorClass::Internal, "internal"},
    }};
    for (std::size_t index = 0; index < kTable.size(); ++index) {
        RAWFRAME_EXPECT(static_cast<std::size_t>(kTable[index].first) == index);
        RAWFRAME_EXPECT(describe(kTable[index].first) == kTable[index].second);
    }
}

RAWFRAME_TEST(AnErrorReportsWhatItWasGiven) {
    const auto kLine = std::source_location::current().line() + 1;
    const Error kError = fail(ErrorClass::Conflict, kTestDomain, ErrorCode{42}, "two owners").error();
    RAWFRAME_EXPECT(kError.errorClass() == ErrorClass::Conflict);
    RAWFRAME_EXPECT(kError.domain() == kTestDomain);
    RAWFRAME_EXPECT(kError.code() == ErrorCode{42});
    RAWFRAME_EXPECT(kError.description() == "two owners");
    RAWFRAME_EXPECT(!kError.descriptionTruncated());
    RAWFRAME_EXPECT(kError.origin().line() == kLine);
    RAWFRAME_EXPECT(kError.frames().empty() && kError.context().empty() && kError.cause() == nullptr);
}

RAWFRAME_TEST(ALongDescriptionIsCutAtACodePointBoundary) {
    // 255 ASCII bytes, then a three-byte character straddling the 256-byte bound.
    std::string text(kMaximumDescriptionBytes - 1, 'a');
    text += "\xE2\x82\xAC"; // U+20AC
    const Error kError = make(text);
    RAWFRAME_EXPECT(kError.descriptionTruncated());
    RAWFRAME_EXPECT(kError.description().size() == kMaximumDescriptionBytes - 1);
    RAWFRAME_EXPECT(isValidUtf8(kError.description()));
}

RAWFRAME_TEST(FramesBeyondTheBoundAreDroppedAndCounted) {
    Error error = make();
    for (std::size_t index = 0; index < kMaximumFrames + 3; ++index) {
        error = std::move(error).withFrame(std::string(1, static_cast<char>('a' + index)));
    }
    RAWFRAME_EXPECT(error.frames().size() == kMaximumFrames);
    RAWFRAME_EXPECT(error.framesDropped() == 3);
    RAWFRAME_EXPECT(error.frames()[0].description == "a");
    RAWFRAME_EXPECT(error.frames()[kMaximumFrames - 1].description == "h");
}

RAWFRAME_TEST(ContextIsBoundedInCountAndLength) {
    Error error = make();
    error = std::move(error).withContext(std::string(kMaximumContextKeyBytes + 5, 'k'), "v");
    RAWFRAME_EXPECT(error.contextTruncated());
    RAWFRAME_EXPECT(error.context()[0].key.size() == kMaximumContextKeyBytes);
    for (std::size_t index = 1; index < kMaximumContextFields + 2; ++index) {
        error = std::move(error).withContext("key", std::string(kMaximumContextValueBytes + 1, 'x'));
    }
    RAWFRAME_EXPECT(error.context().size() == kMaximumContextFields);
    RAWFRAME_EXPECT(error.contextFieldsDropped() == 2);
    RAWFRAME_EXPECT(error.context()[1].value.size() == kMaximumContextValueBytes);
}

RAWFRAME_TEST(MappingReplacesIdentityAndKeepsTheCause) {
    Error original = std::move(make("disk said no")).withContext("slot", "3");
    Error mapped = std::move(original).mappedTo(ErrorClass::Unavailable, kOtherDomain, ErrorCode{1}, "save failed");
    RAWFRAME_EXPECT(mapped.errorClass() == ErrorClass::Unavailable);
    RAWFRAME_EXPECT(mapped.domain() == kOtherDomain);
    RAWFRAME_EXPECT(mapped.description() == "save failed");
    RAWFRAME_EXPECT(mapped.context().empty());
    RAWFRAME_EXPECT(mapped.cause() != nullptr);
    RAWFRAME_EXPECT(mapped.cause()->description() == "disk said no");
    RAWFRAME_EXPECT(mapped.cause()->context()[0].value == "3");
    RAWFRAME_EXPECT(mapped.cause()->cause() == nullptr);
}

RAWFRAME_TEST(TheCauseChainStopsAtItsDepthBound) {
    Error error = make("root");
    for (std::size_t depth = 0; depth < kMaximumCauseDepth + 2; ++depth) {
        error = std::move(error).mappedTo(ErrorClass::Internal, kTestDomain, ErrorCode{0}, "layer");
    }
    std::size_t depth = 0;
    const Error* last = &error;
    while (last->cause() != nullptr) {
        last = last->cause();
        ++depth;
    }
    RAWFRAME_EXPECT(depth <= kMaximumCauseDepth);
    RAWFRAME_EXPECT(error.causeDropped() || last->causeDropped());
}

RAWFRAME_TEST(AnErrorOwnsItsStorage) {
    Error error = make();
    {
        std::string key = "transient-key";
        std::string value = "transient-value";
        std::string frame = "transient-frame";
        error = std::move(error).withContext(key, value).withFrame(frame);
        key.assign(key.size(), '#');
        value.assign(value.size(), '#');
        frame.assign(frame.size(), '#');
    }
    RAWFRAME_EXPECT(error.context()[0].key == "transient-key");
    RAWFRAME_EXPECT(error.context()[0].value == "transient-value");
    RAWFRAME_EXPECT(error.frames()[0].description == "transient-frame");
}

RAWFRAME_TEST(CloneIsDeepAndIndependent) {
    Error original = std::move(make("inner")).mappedTo(ErrorClass::DataLoss, kTestDomain, ErrorCode{9}, "outer");
    const Error kCopy = original.clone();
    original = make("replaced");
    RAWFRAME_EXPECT(kCopy.description() == "outer");
    RAWFRAME_EXPECT(kCopy.cause() != nullptr && kCopy.cause()->description() == "inner");
}
