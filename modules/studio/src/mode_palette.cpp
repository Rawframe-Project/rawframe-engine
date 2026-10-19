#include "shell.h"
#include "surfaces.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace rawframe::studio {

namespace {

/// Studio's content modes, each the kind of document it opens and its
/// name as the surface table gives it (D910).
constexpr std::array<std::pair<std::string_view, Listed>, 4> kContentModes = {{{"Blockout", Listed::Blockout},
                                                                               {"Terrain", Listed::Terrain},
                                                                               {"Tiles", Listed::Tilemap},
                                                                               {"Scatter", Listed::Scatter}}};

/// The mode Transform is: the scene's own editing, picks choosing and drags
/// moving (D457).
constexpr std::string_view kTransform = placed("Transform", SurfaceKind::ViewportMode, "Mode palette");

/// How many modes the extensions may add, as the Extensions menu offers
/// them (D877).
constexpr std::size_t kModesLeft =
    8 - static_cast<std::size_t>(std::ranges::count(kSurfaces, SurfaceKind::ViewportMode, &Surface::kind));

} // namespace

void ShellParticipant::showModes() {
    std::vector<ui::Node> buttons;
    for (const auto& [kNode, kName] : held_.modeButtons) {
        buttons.push_back(kNode);
    }
    clear(buttons);
    held_.modeButtons.clear();
    if (held_.modePalette == ui::Node{} || !gameOpen()) {
        return;
    }
    // The mode in use: an extension's while it is on, a content mode while
    // its document is chosen, else Transform.
    std::string active{kTransform};
    for (const auto& [kName, kKind] : kContentModes) {
        if (chosen(kKind)) {
            active = kName;
        }
    }
    if (held_.extensionMode.has_value()) {
        active = held_.extensionMode->second;
    }
    std::vector<std::string> modes;
    for (const Surface& kSurface : kSurfaces) {
        if (kSurface.kind == SurfaceKind::ViewportMode) {
            modes.emplace_back(kSurface.name);
        }
    }
    std::size_t offered = 0;
    for (const ListedExtension& kExtension : extensions_) {
        for (const std::string& kMode : kExtension.modes) {
            if (offered++ < kModesLeft) {
                modes.push_back(kMode);
            }
        }
    }
    for (const std::string& kMode : modes) {
        // A content mode whose kind of document the game has none of is
        // quiet: choosing it says so.
        const auto kContent = std::ranges::find(kContentModes, kMode, &std::pair<std::string_view, Listed>::first);
        const bool kHas = kContent == kContentModes.end() || std::ranges::contains(listed_, kContent->second);
        // Each by its icon (D915), an extension's its puzzle piece.
        const Icon kIcon = kMode == "Transform"  ? Icon::Move
                           : kMode == "Blockout" ? Icon::Blockout
                           : kMode == "Terrain"  ? Icon::Terrain
                           : kMode == "Tiles"    ? Icon::Tiles
                           : kMode == "Scatter"  ? Icon::Scatter
                                                 : Icon::Puzzle;
        auto made = iconButton(held_.modePalette, kIcon, kMode, kMode == active);
        if (!made.has_value()) {
            return;
        }
        if (!kHas) {
            setIconButton(*made, false, false);
        }
        held_.modeButtons.emplace_back(*made, kMode);
    }
}

void ShellParticipant::chooseMode(const std::string& mode) {
    const auto kContent = std::ranges::find(kContentModes, mode, &std::pair<std::string_view, Listed>::first);
    if (mode == kTransform) {
        // Back to the scene a content mode was chosen from, and out of an
        // extension's mode.
        held_.extensionMode.reset();
        const auto kBack = std::ranges::find(scenes_, held_.modeReturn);
        if (documentChosen() && kBack != scenes_.end()) {
            showScene(static_cast<std::size_t>(kBack - scenes_.begin()));
        }
        say("Transform mode");
    } else if (kContent != kContentModes.end()) {
        // The game's first document of the kind, opened in the center.
        const auto kListed = std::ranges::find(listed_, kContent->second);
        if (kListed == listed_.end()) {
            std::string kind{mode};
            std::ranges::transform(kind, kind.begin(), [](char each) {
                return static_cast<char>(std::tolower(each));
            });
            say("the game has no " + kind + " document");
            return;
        }
        held_.extensionMode.reset();
        if (chosen(Listed::Scene)) {
            held_.modeReturn = scene_;
        }
        showScene(static_cast<std::size_t>(kListed - listed_.begin()));
        say(mode + " mode");
    } else {
        switchExtensionMode(mode);
    }
    ++held_.modesChosen;
    emitter_.log(diagnostics::Severity::Info,
                 kModeChosen,
                 "a viewport mode chosen in the mode palette",
                 {diagnostics::field("mode", std::string_view{mode}),
                  diagnostics::field("document", std::string_view{scene_}),
                  diagnostics::field("chosen", held_.modesChosen)});
    showModes();
}

} // namespace rawframe::studio
