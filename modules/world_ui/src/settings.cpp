#include "settings.h"

#include "rawframe/world_kest/layouts.h"
#include "rawframe/world_ui/errors.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::world_ui {

namespace {

std::unexpected<result::Error> refuse(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kWorldUiDomain, code(WorldUiError::BadNodes), why).error()};
}

} // namespace

/// What the game's UI document `text` says: which node is inside
/// which, which typed words each shows, and what each presentation
/// component starts as.
result::Result<UiDocument>
documentOf(const world_kest::GameDescription& game, const kest::Program& program, std::string_view text) {
    UiDocument made;
    std::vector<schema::ComponentTypeId> nodes;
    std::vector<std::string_view> names;
    for (const world_kest::GameComponent& component : game.components) {
        if (world_kest::ofEngineType(component, "rawframe.ui.Node")) {
            nodes.push_back(component.id);
            names.push_back(component.name);
        }
    }
    made.parents.resize(nodes.size());
    auto document = ui::readDocument(text);
    if (!document.has_value()) {
        return std::unexpected<result::Error>{std::move(document).error()};
    }
    const auto kPresented = [&game](std::string_view component) {
        return std::ranges::any_of(game.presentation, [component](const world_kest::GamePresentation& each) {
            return std::ranges::contains(each.components, component);
        });
    };
    for (const ui::DocumentNode& kNode : document->nodes) {
        const auto kNamed = std::ranges::find(names, kNode.component);
        if (kNamed == names.end()) {
            return std::unexpected<result::Error>{
                refuse("a UI document names a component that is not a rawframe.ui.Node")
                    .error()
                    .withContext("component", kNode.component)};
        }
        const auto kIndex = static_cast<std::size_t>(kNamed - names.begin());
        if (kNode.parent.has_value()) {
            // A parent comes before what is inside it, so is named.
            made.parents[kIndex] = static_cast<std::size_t>(
                std::ranges::find(names, document->nodes[*kNode.parent].component) - names.begin());
        }
        if (!kNode.shows.empty()) {
            // What nodes show of typed words: rawframe.ui.Typed's
            // length, then its bytes, each laid out a byte.
            const auto kTyped = program.layout("rawframe.ui.Typed");
            if (!kTyped.has_value() || kTyped->size != sizeof(Typed) || kTyped->fields.size() < 2 ||
                kTyped->fields[0].name != "length" || kTyped->fields[0].offset != offsetof(Typed, length) ||
                kTyped->fields[1].offset != offsetof(Typed, bytes)) {
                return refuse("the game's rawframe.ui.Typed is not as the engine reads it");
            }
            const auto kWords = std::ranges::find(game.components, kNode.shows, &world_kest::GameComponent::name);
            if (kWords == game.components.end() || !world_kest::ofEngineType(*kWords, "rawframe.ui.Typed")) {
                return std::unexpected<result::Error>{
                    refuse("a UI node shows the words of a rawframe.ui.Typed component")
                        .error()
                        .withContext("component", kNode.shows)};
            }
            made.shows.resize(nodes.size());
            made.shows[kIndex] = kWords->id;
        }
        constexpr ui::NodeFields kNought{};
        if (std::memcmp(&kNode.fields, &kNought, sizeof kNought) != 0) {
            // Only presentation attaches a component, so only one of
            // its components starts as a document says.
            if (!kPresented(kNode.component)) {
                return std::unexpected<result::Error>{
                    refuse("a UI document starts only the game's presentation components")
                        .error()
                        .withContext("component", kNode.component)};
            }
            made.starts.push_back(Start{.component = nodes[kIndex], .fields = kNode.fields});
        }
    }
    return made;
}

/// The game's node components and their parents, and in `starts` what its
/// UI document starts them as; none for a game without any.
result::Result<std::optional<UiSettings>>
settingsOf(const world_kest::GameFiles& files, const kest::Program& program, std::vector<Start>& starts) {
    const world_kest::GameDescription& kGame = files.description();
    UiSettings settings;
    for (const world_kest::GameComponent& component : kGame.components) {
        if (world_kest::ofEngineType(component, "rawframe.ui.Node")) {
            settings.nodes.push_back(component.id);
        }
    }
    if (settings.nodes.empty()) {
        return std::optional<UiSettings>{};
    }
    // By its full name: a game's own type called `Node` is not this one.
    if (!world_kest::laidOutAs(program,
                               "rawframe.ui.Node",
                               sizeof(ui::NodeFields),
                               {{"widthScale", offsetof(ui::NodeFields, widthScale)},
                                {"widthOffset", offsetof(ui::NodeFields, widthOffset)},
                                {"heightScale", offsetof(ui::NodeFields, heightScale)},
                                {"heightOffset", offsetof(ui::NodeFields, heightOffset)},
                                {"direction", offsetof(ui::NodeFields, direction)},
                                {"justify", offsetof(ui::NodeFields, justify)},
                                {"alignItems", offsetof(ui::NodeFields, alignItems)},
                                {"alignSelf", offsetof(ui::NodeFields, alignSelf)},
                                {"gap", offsetof(ui::NodeFields, gap)},
                                {"grow", offsetof(ui::NodeFields, grow)},
                                {"shrink", offsetof(ui::NodeFields, shrink)},
                                {"padding", offsetof(ui::NodeFields, padding)},
                                {"margin", offsetof(ui::NodeFields, margin)},
                                {"border", offsetof(ui::NodeFields, border)},
                                {"absolute", offsetof(ui::NodeFields, absolute)},
                                {"xScale", offsetof(ui::NodeFields, xScale)},
                                {"xOffset", offsetof(ui::NodeFields, xOffset)},
                                {"yScale", offsetof(ui::NodeFields, yScale)},
                                {"yOffset", offsetof(ui::NodeFields, yOffset)},
                                {"anchorX", offsetof(ui::NodeFields, anchorX)},
                                {"anchorY", offsetof(ui::NodeFields, anchorY)},
                                {"fill", offsetof(ui::NodeFields, fill)},
                                {"borderColor", offsetof(ui::NodeFields, borderColor)},
                                {"radius", offsetof(ui::NodeFields, radius)},
                                {"clip", offsetof(ui::NodeFields, clip)},
                                {"order", offsetof(ui::NodeFields, order)},
                                {"image", offsetof(ui::NodeFields, image)},
                                {"imageTint", offsetof(ui::NodeFields, imageTint)},
                                {"imageSlice", offsetof(ui::NodeFields, imageSlice)},
                                {"shadowColor", offsetof(ui::NodeFields, shadowColor)},
                                {"shadowX", offsetof(ui::NodeFields, shadowX)},
                                {"shadowY", offsetof(ui::NodeFields, shadowY)},
                                {"shadowBlur", offsetof(ui::NodeFields, shadowBlur)},
                                {"gradientKind", offsetof(ui::NodeFields, gradientKind)},
                                {"gradientAngle", offsetof(ui::NodeFields, gradientAngle)},
                                {"gradientFrom", offsetof(ui::NodeFields, gradientFrom)},
                                {"gradientTo", offsetof(ui::NodeFields, gradientTo)},
                                {"text", offsetof(ui::NodeFields, text)},
                                {"textValue", offsetof(ui::NodeFields, textValue)},
                                {"textSecond", offsetof(ui::NodeFields, textSecond)},
                                {"font", offsetof(ui::NodeFields, font)},
                                {"textSize", offsetof(ui::NodeFields, textSize)},
                                {"textColor", offsetof(ui::NodeFields, textColor)},
                                {"textAlign", offsetof(ui::NodeFields, textAlign)},
                                {"textWrap", offsetof(ui::NodeFields, textWrap)},
                                {"press", offsetof(ui::NodeFields, press)},
                                {"hit", offsetof(ui::NodeFields, hit)},
                                {"layer", offsetof(ui::NodeFields, layer)},
                                {"edit", offsetof(ui::NodeFields, edit)},
                                {"editLimit", offsetof(ui::NodeFields, editLimit)},
                                {"style", offsetof(ui::NodeFields, style)},
                                {"scroll", offsetof(ui::NodeFields, scroll)},
                                {"range", offsetof(ui::NodeFields, range)},
                                {"rangeMinimum", offsetof(ui::NodeFields, rangeMinimum)},
                                {"rangeMaximum", offsetof(ui::NodeFields, rangeMaximum)},
                                {"rangeStep", offsetof(ui::NodeFields, rangeStep)},
                                {"rangeValue", offsetof(ui::NodeFields, rangeValue)},
                                {"thumb", offsetof(ui::NodeFields, thumb)},
                                {"check", offsetof(ui::NodeFields, check)},
                                {"checked", offsetof(ui::NodeFields, checked)},
                                {"popup", offsetof(ui::NodeFields, popup)},
                                {"disabled", offsetof(ui::NodeFields, disabled)},
                                {"kept", offsetof(ui::NodeFields, kept)}})) {
        return refuse("the game's rawframe.ui.Node is not as the engine reads it");
    }
    settings.parents.resize(settings.nodes.size());
    if (!kGame.ui.empty()) {
        RAWFRAME_TRY_ASSIGN(const std::string_view kText, files.document(kGame.ui));
        auto document = documentOf(kGame, program, kText);
        if (!document.has_value()) {
            return std::unexpected<result::Error>{std::move(document).error().withContext("name", kGame.ui)};
        }
        settings.parents = std::move(document->parents);
        settings.shows = std::move(document->shows);
        starts = std::move(document->starts);
    }
    for (const world_kest::GameFont& kFont : kGame.fonts) {
        settings.fonts.push_back(kFont.id);
    }
    // The game's style classes.
    if (!kGame.styles.empty()) {
        RAWFRAME_TRY_ASSIGN(const std::string_view kText, files.document(kGame.styles));
        auto styles = ui::readStyles(kText);
        if (!styles.has_value()) {
            return std::unexpected<result::Error>{std::move(styles).error().withContext("name", kGame.styles)};
        }
        settings.styles = *std::move(styles);
    }
    return std::optional<UiSettings>{std::move(settings)};
}

} // namespace rawframe::world_ui
