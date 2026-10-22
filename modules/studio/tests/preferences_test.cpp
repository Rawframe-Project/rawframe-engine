// Studio's preferences (D779): the docks' sizes kept in a document of
// Studio's own, read back as written, held inside what the window shows,
// and a document that is not one left for the defaults.

#include "../src/preferences.h"
#include "rawframe/test/test.h"

#include <string>
#include <vector>

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

RAWFRAME_TEST(AnInterfaceScaleIsKeptAmongTheChoices) {
    // As chosen (D966), and one not offered the screen's.
    const Preferences kLarger{.interfaceScale = 175};
    const auto kRead = preferencesOf(writtenPreferences(kLarger));
    RAWFRAME_EXPECT(kRead.has_value() && kRead->interfaceScale == 175);
    const auto kOdd = preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,"interfaceScale":333})");
    RAWFRAME_EXPECT(kOdd.has_value() && kOdd->interfaceScale == 0);
    RAWFRAME_EXPECT(!preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,"interfaceScale":"big"})")
                         .has_value());
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

RAWFRAME_TEST(TheGamesOpenedAreKeptLatestFirst) {
    Preferences kept;
    for (int each = 0; each < 10; ++each) {
        kept = opened(kept, "game" + std::to_string(each) + ".game");
    }
    kept = opened(kept, "game5.game");
    RAWFRAME_EXPECT(kept.recent.size() == kRecentGames);
    RAWFRAME_EXPECT(kept.recent.front() == "game5.game" && kept.recent[1] == "game9.game");
    RAWFRAME_EXPECT(kept.recent.back() == "game2.game");
    const auto kRead = preferencesOf(writtenPreferences(kept));
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == kept);
    // A name that is not a string is not one.
    RAWFRAME_EXPECT(
        !preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,"recent":[3]})").has_value());
}

RAWFRAME_TEST(AChordIsWhatTheWindowNamesAPress) {
    for (const std::string_view kGood : {"Ctrl+D", "Ctrl+Shift+Z", "Ctrl+Shift+Alt+9", "F5", "Shift+F12", "Delete"}) {
        RAWFRAME_EXPECT(isChord(kGood));
    }
    // A letter without Ctrl is typing; Ctrl+A selects a field's text; F11
    // is the window's; the modifiers come in one order.
    for (const std::string_view kBad : {"D", "Shift+D", "Ctrl+A", "F11", "F13", "F0", "Shift+Ctrl+D", "Ctrl+d", ""}) {
        RAWFRAME_EXPECT(!isChord(kBad));
    }
    const auto kRead = chordsOf(" Ctrl+Y,Ctrl+Shift+Z , Ctrl+Y");
    RAWFRAME_EXPECT(kRead.has_value() && chordsText(*kRead) == "Ctrl+Y, Ctrl+Shift+Z");
    RAWFRAME_EXPECT(chordsOf("").has_value() && chordsOf("")->empty());
    RAWFRAME_EXPECT(!chordsOf("Ctrl+D, hello").has_value());
}

RAWFRAME_TEST(KeysAndTheEditorAreKeptAsWritten) {
    Preferences kept;
    kept.keys["Duplicate"] = {"Ctrl+Shift+D"};
    kept.keys["Play"] = {};
    kept.editor = "kate --line {line} {file}";
    const auto kRead = preferencesOf(writtenPreferences(kept));
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == kept);
    RAWFRAME_EXPECT(!preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,"keys":{"Play":["P"]}})")
                         .has_value());
}

RAWFRAME_TEST(SavedLayoutsAreKeptAsWritten) {
    Preferences kept;
    kept.layouts.push_back(
        Layout{.name = "Saved 1", .hierarchyWidth = 280, .inspectorWidth = 440, .dockHeight = 300, .dock = "Network"});
    const auto kRead = preferencesOf(writtenPreferences(kept));
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == kept);
    // A layout without its dock, or not a layout, makes no preferences.
    RAWFRAME_EXPECT(!preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,"layouts":[)"
                                   R"({"name":"A","hierarchyWidth":1,"inspectorWidth":1,"dockHeight":0}]})")
                         .has_value());
    RAWFRAME_EXPECT(
        !preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,"layouts":[3]})").has_value());
}

RAWFRAME_TEST(PanelsApartAreKeptWithTheirWindows) {
    // D994: by their tabs' names, each window's size and, where told, place.
    const Preferences kKept{
        .apartPanels = {ApartPanel{.name = "Assets", .width = 640, .height = 900, .place = {{-1200, 40}}},
                        ApartPanel{.name = "Console", .width = 720, .height = 480}}};
    const auto kRead = preferencesOf(writtenPreferences(kKept));
    RAWFRAME_EXPECT(kRead.has_value() && kRead->apartPanels == kKept.apartPanels);
    // A size held to what a window can be; a place both numbers or none.
    const auto kHeld = preferencesOf(
        R"({"kind":"rawframe.studio.preferences","formatVersion":1,"apartPanels":[{"name":"Mixer","width":20,"height":90000}]})");
    RAWFRAME_EXPECT(kHeld.has_value() && kHeld->apartPanels.size() == 1 && kHeld->apartPanels[0].width == 160 &&
                    kHeld->apartPanels[0].height == 4096 && !kHeld->apartPanels[0].place.has_value());
    RAWFRAME_EXPECT(
        !preferencesOf(
             R"({"kind":"rawframe.studio.preferences","formatVersion":1,"apartPanels":[{"name":"Mixer","left":3}]})")
             .has_value());
    RAWFRAME_EXPECT(
        !preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,"apartPanels":[{"width":3}]})")
             .has_value());
}

RAWFRAME_TEST(PanelsDockedOnTheRightAreKeptAsWritten) {
    // D980: by their tabs' names, once each.
    const Preferences kKept{.rightPanels = {"Console", "Profiler"}};
    const auto kRead = preferencesOf(writtenPreferences(kKept));
    RAWFRAME_EXPECT(kRead.has_value() && kRead->rightPanels == kKept.rightPanels);
    const auto kTwice =
        preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,"rightPanels":["Mixer","Mixer"]})");
    RAWFRAME_EXPECT(kTwice.has_value() && kTwice->rightPanels == std::vector<std::string>{"Mixer"});
    RAWFRAME_EXPECT(
        !preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,"rightPanels":[3]})").has_value());
    RAWFRAME_EXPECT(
        !preferencesOf(R"({"kind":"rawframe.studio.preferences","formatVersion":1,"rightPanels":[""]})").has_value());
}
