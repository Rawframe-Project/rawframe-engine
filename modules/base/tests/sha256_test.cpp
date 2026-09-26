// SHA-256 against FIPS 180-4 / NIST vectors, pieces fed any way, and empty
// pieces, which may carry no pointer.

#include "rawframe/base/sha256.h"
#include "rawframe/test/test.h"

#include <span>
#include <string>

using namespace rawframe::base;

namespace {

std::string hex(const Sha256Digest& digest) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (const std::byte kByte : digest) {
        text.push_back(kDigits[std::to_integer<unsigned>(kByte) >> 4U]);
        text.push_back(kDigits[std::to_integer<unsigned>(kByte) & 0xFU]);
    }
    return text;
}

} // namespace

RAWFRAME_TEST(Sha256MatchesNistVectors) {
    RAWFRAME_EXPECT(hex(sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    RAWFRAME_EXPECT(hex(sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    RAWFRAME_EXPECT(hex(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
                    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 million;
    const std::string kThousand(1000, 'a');
    for (int round = 0; round < 1000; ++round) {
        million.update(kThousand);
    }
    RAWFRAME_EXPECT(hex(million.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

RAWFRAME_TEST(Sha256IsTheSameInAnyPieces) {
    const std::string kText(1000, 'x');
    const Sha256Digest kWhole = sha256(kText);
    for (const std::size_t kPiece :
         {std::size_t{1}, std::size_t{7}, std::size_t{63}, std::size_t{64}, std::size_t{65}}) {
        Sha256 pieces;
        for (std::size_t at = 0; at < kText.size(); at += kPiece) {
            pieces.update(std::string_view{kText}.substr(at, kPiece));
        }
        RAWFRAME_EXPECT(pieces.finish() == kWhole);
    }
}

RAWFRAME_TEST(Sha256TakesEmptyPiecesWithNoPointer) {
    Sha256 pieces;
    pieces.update(std::span<const std::byte>{});
    pieces.update("ab");
    pieces.update(std::span<const std::byte>{});
    pieces.update("c");
    RAWFRAME_EXPECT(pieces.finish() == sha256("abc"));
}
