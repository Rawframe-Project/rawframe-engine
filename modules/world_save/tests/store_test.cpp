// The directory store: what is kept comes back, absence and bad slots are
// typed, a save over its limit is refused, and keeping again replaces
// whole.

#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"
#include "rawframe/world_save/errors.h"
#include "rawframe/world_save/store.h"

#include <filesystem>
#include <string>

using namespace rawframe;
using namespace rawframe::world_save;

namespace {

bool refusedWith(const auto& outcome, SaveError error) {
    return !outcome.has_value() && outcome.error().domain() == kSaveDomain && outcome.error().code() == code(error);
}

} // namespace

RAWFRAME_TEST(ADirectoryKeepsSavesBySlot) {
    const std::filesystem::path kDirectory = test::scratchDirectory("saves") / "slots";
    DirectorySaveStore store{kDirectory};
    RAWFRAME_EXPECT(refusedWith(store.load("world", 1024), SaveError::Absent));
    const std::vector<std::byte> kFirst(100, std::byte{1});
    const std::vector<std::byte> kSecond(40, std::byte{2});
    RAWFRAME_EXPECT(store.keep("world", kFirst).has_value());
    RAWFRAME_EXPECT(store.load("world", 1024) == kFirst);
    // Kept again: the new save whole, and nothing left beside it.
    RAWFRAME_EXPECT(store.keep("world", kSecond).has_value());
    RAWFRAME_EXPECT(store.load("world", 1024) == kSecond);
    RAWFRAME_EXPECT(!std::filesystem::exists(kDirectory / "world.rfsave.partial"));
    RAWFRAME_EXPECT(refusedWith(store.load("world", 39), SaveError::LimitExceeded));
    // Slots are plain names, so none reaches outside the directory.
    for (const std::string_view kSlot : {"", "../world", "World", "a/b", "a.b"}) {
        RAWFRAME_EXPECT(refusedWith(store.keep(kSlot, kFirst), SaveError::InvalidSlot));
        RAWFRAME_EXPECT(refusedWith(store.load(kSlot, 1024), SaveError::InvalidSlot));
    }
    std::filesystem::remove_all(kDirectory.parent_path());
}
