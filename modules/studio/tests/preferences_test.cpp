// Studio's preferences (D779): the docks' sizes kept in a document of
// Studio's own, read back as written, held inside what the window shows,
// and a document that is not one left for the defaults.

#include "../src/preferences.h"
#include "rawframe/test/test.h"

using namespace rawframe::studio;

RAWFRAME_TEST(PreferencesReadBackAsWritten) {
    const Preferences kKept{.hierarchyWidth = 420, .inspectorWidth = 300, .dockHeight = 250};
    const auto kRead = preferencesOf(writtenPreferences(kKept));
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == kKept);
    // The defaults, a dock at its share, read back too.
    RAWFRAME_EXPECT(preferencesOf(writtenPreferences(Preferences{})) == Preferences{});
}

RAWFRAME_TEST(PreferencesAreHeldInsideTheWindow) {
    const auto kRead = preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,)"
                                     R"("hierarchyWidth":5,"inspectorWidth":90000,"dockHeight":3})");
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kRead->hierarchyWidth == 200 && kRead->inspectorWidth == 800 && kRead->dockHeight == 120);
}

RAWFRAME_TEST(ADocumentThatIsNotPreferencesIsNone) {
    RAWFRAME_EXPECT(!preferencesOf("").has_value());
    RAWFRAME_EXPECT(!preferencesOf("[]").has_value());
    RAWFRAME_EXPECT(!preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":2})").has_value());
    RAWFRAME_EXPECT(!preferencesOf(R"({"kind":"ui.kept","formatVersion":1})").has_value());
    RAWFRAME_EXPECT(
        !preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,"hierarchyWidth":"wide"})")
             .has_value());
    // What it does not give stays as Studio starts.
    const auto kSome = preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,"dockHeight":300})");
    RAWFRAME_EXPECT(kSome.has_value() && kSome->hierarchyWidth == 360 && kSome->dockHeight == 300);
}
