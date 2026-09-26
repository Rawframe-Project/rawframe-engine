// The server's book of a connection's checksums (D204): the ticks it keeps,
// the verdicts it gives, and a flood held to the rate limit before any work.

#include "../src/checksums.h"
#include "rawframe/test/test.h"

using namespace rawframe::world_replication;

RAWFRAME_TEST(ABookKeepsTheLatestTicksAndJudgesAgainstThem) {
    ChecksumBook book;
    for (std::uint64_t tick = 1; tick <= 100; ++tick) {
        book.keep(tick, tick * 7);
    }
    RAWFRAME_EXPECT(book.check(100, 700) == ChecksumBook::Verdict::Verified);
    RAWFRAME_EXPECT(book.check(37, 259) == ChecksumBook::Verdict::Verified);
    // Older than the ring, or never published: nothing to say.
    RAWFRAME_EXPECT(book.check(36, 252) == ChecksumBook::Verdict::Unverifiable);
    RAWFRAME_EXPECT(book.check(101, 707) == ChecksumBook::Verdict::Unverifiable);
    RAWFRAME_EXPECT(book.divergences == 0);
    RAWFRAME_EXPECT(book.check(100, 701) == ChecksumBook::Verdict::Diverged && book.divergences == 1);
}

RAWFRAME_TEST(AFloodIsHeldToTheRateLimit) {
    ChecksumBook book;
    int taken = 0;
    // A thousand records within one second of 60 ticks: eight looked at.
    for (int record = 0; record < 1000; ++record) {
        taken += book.admit(125, 60, 8) ? 1 : 0;
    }
    RAWFRAME_EXPECT(taken == 8);
    // The next second starts again.
    RAWFRAME_EXPECT(book.admit(180, 60, 8));
}
