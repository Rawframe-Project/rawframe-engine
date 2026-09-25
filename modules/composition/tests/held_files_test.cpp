// Files a host holds (D167): found by their paths, listed under a
// directory relative to it, and refused when a path is not one.

#include "rawframe/composition/held_files.h"
#include "rawframe/test/test.h"

#include <string>
#include <vector>

using namespace rawframe;
using composition::HeldFiles;

namespace {

HeldFiles::File file(std::string path, std::string_view text) {
    const auto kBytes = std::as_bytes(std::span{text.data(), text.size()});
    return {std::move(path), {kBytes.begin(), kBytes.end()}};
}

} // namespace

RAWFRAME_TEST(HeldFilesAreFoundAndListedByPath) {
    const auto kHeld = HeldFiles::of({file("game/b.kest", "b"),
                                      file("game/a.game", "a"),
                                      file("game/prefabs/p.scene", "p"),
                                      file("gamer/x", "x"),
                                      file("top", "t")});
    RAWFRAME_EXPECT(kHeld.has_value());
    if (!kHeld.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kHeld->find("game/a.game") != nullptr && kHeld->find("game/a.game")->size() == 1);
    RAWFRAME_EXPECT(kHeld->find("game") == nullptr && kHeld->find("a.game") == nullptr);
    // A directory's files only, not a sibling that shares its name's start.
    const auto kGame = kHeld->under("game");
    RAWFRAME_EXPECT(kGame.size() == 3 && kGame[0].first == "a.game" && kGame[2].first == "prefabs/p.scene");
    RAWFRAME_EXPECT(kHeld->under("").size() == 5 && kHeld->under("none").empty());
}

RAWFRAME_TEST(HeldFilesRefuseWhatIsNotAPath) {
    for (const std::string& path : {std::string{},
                                    std::string{"/abs"},
                                    std::string{"a//b"},
                                    std::string{"a/./b"},
                                    std::string{"../up"},
                                    std::string{"dir/"}}) {
        RAWFRAME_EXPECT(!HeldFiles::of({file(path, "x")}).has_value());
    }
    RAWFRAME_EXPECT(!HeldFiles::of({file("same", "1"), file("same", "2")}).has_value());
    RAWFRAME_EXPECT(HeldFiles::of({}).has_value());
}
