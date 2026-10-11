#include "rawframe/ui/access.h"

#include "tree_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <maul-ui/access.h>
#include <maul-ui/access_tree.h>
#include <maul-ui/focus.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if RAWFRAME_UI_ATSPI
#include <maul-ui/access_atspi.h>
#endif
#if RAWFRAME_UI_ANDROID
#include <maul-ui/access_android.h>
#endif
#if RAWFRAME_UI_UIA
#include <maul-ui/access_uia.h>
#endif
#if RAWFRAME_UI_APPKIT
#include <maul-ui/access_ns.h>
#endif
#if RAWFRAME_UI_UIKIT
#include <maul-ui/access_uikit.h>
#endif
#if RAWFRAME_UI_ARIA
#include <maul-ui/access_aria.h>
#endif

namespace rawframe::ui {

namespace {

muiRole roleOf(Role role) noexcept {
    switch (role) {
    case Role::Label:
        return mui_roleLabel;
    case Role::Image:
        return mui_roleImage;
    case Role::Button:
        return mui_roleButton;
    case Role::TextInput:
        return mui_roleTextInput;
    case Role::MultilineTextInput:
        return mui_roleMultilineTextInput;
    case Role::ScrollView:
        return mui_roleScrollView;
    case Role::Dialog:
        return mui_roleDialog;
    case Role::Group:
        return mui_roleGroup;
    case Role::Slider:
        return mui_roleSlider;
    case Role::CheckBox:
        return mui_roleCheckBox;
    case Role::Switch:
        return mui_roleSwitch;
    case Role::RadioButton:
        return mui_roleRadioButton;
    case Role::Generic:
        break;
    }
    return mui_roleGeneric;
}

} // namespace

result::Status Tree::setRole(Node node, Role role) {
    RAWFRAME_TRY(
        checked(muiNode_SetAccessRole(state_->context, idOf(node), roleOf(role)), "a node's role was refused"));
    // What can be checked says whether it is, from its states (D622).
    const bool kCheckable = role == Role::CheckBox || role == Role::Switch || role == Role::RadioButton;
    return checked(muiNode_SetAccessFlags(state_->context, idOf(node), kCheckable ? mui_accessCheckable : 0),
                   "a node's flags were refused");
}

result::Status Tree::setName(Node node, std::string_view name) {
    return checked(muiNode_SetAccessText(state_->context, idOf(node), mui_accessLabel, name.data(), name.size()),
                   "a node's name was refused");
}

namespace {

/// A node's text as assistive technology reads it, its text block's: a
/// label's words, a field's typed text (D571), with the block's lines and
/// words as Maul UI reads them. A field reads as being edited, its
/// selection as its editor told it (else the caret at its end) and a
/// placeholder as no text, so that a screen reader may type into it (D613).
bool readText(void* user, muiNodeId node, std::uint64_t, const muiRect* shown, muiAccessContent* contentOut) {
    auto* state = static_cast<Tree::State*>(user);
    const Text* kText = state->textOf(node);
    if (kText == nullptr) {
        return false;
    }
    if (kText->editable && kText->placeholder) {
        *contentOut = muiAccessContent{};
        contentOut->text = "";
        contentOut->marks.selected = true;
        return true;
    }
    if (!muiAccessTextOf(&state->host, node, muiTextBlock_GetKey(kText->block), shown, contentOut)) {
        return false;
    }
    if (kText->editable) {
        const auto kLength = static_cast<std::uint32_t>(contentOut->length);
        const std::array<std::uint32_t, 2> kSelection = kText->selection.value_or(std::array{kLength, kLength});
        contentOut->marks.anchor = std::min(kSelection[0], kLength);
        contentOut->marks.focus = std::min(kSelection[1], kLength);
        contentOut->marks.selected = true;
    }
    return true;
}

} // namespace

result::Status Tree::focus(std::optional<Node> node, bool navigated) {
    return checked(muiFocus_Set(state_->context,
                                0,
                                node.has_value() ? idOf(*node) : muiNodeId{},
                                navigated ? mui_focusByNavigation : mui_focusByPointer),
                   "a node could not take focus");
}

bool accessBuilt(AccessPlatform platform) noexcept {
    return platform == AccessPlatform::Copy || (platform == AccessPlatform::Atspi && RAWFRAME_UI_ATSPI) ||
           (platform == AccessPlatform::Android && RAWFRAME_UI_ANDROID) ||
           (platform == AccessPlatform::Uia && RAWFRAME_UI_UIA) ||
           (platform == AccessPlatform::AppKit && RAWFRAME_UI_APPKIT) ||
           (platform == AccessPlatform::UiKit && RAWFRAME_UI_UIKIT) ||
           (platform == AccessPlatform::Aria && RAWFRAME_UI_ARIA);
}

struct Access::State {
    Tree* tree = nullptr;
    muiNodeId root{};
    /// The copy the platform reads: the AT-SPI adapter's own, or one held
    /// here where there is none.
    muiAccessTree* copy = nullptr;
    /// What screen readers asked since last taken (D572).
    std::vector<AccessRequest> requests;
#if RAWFRAME_UI_ATSPI
    muiAtspiApp* app = nullptr;
    muiAtspiAdapter* adapter = nullptr;
#endif
#if RAWFRAME_UI_ANDROID
    muiAndroidAdapter* android = nullptr;
#endif
#if RAWFRAME_UI_UIA
    muiUiaAdapter* uia = nullptr;
#endif
#if RAWFRAME_UI_APPKIT
    muiNsAdapter* appKit = nullptr;
#endif
#if RAWFRAME_UI_UIKIT
    muiUikitAdapter* uiKit = nullptr;
#endif
#if RAWFRAME_UI_ARIA
    muiAriaAdapter* aria = nullptr;
#endif

    [[nodiscard]] muiContext* context() const noexcept {
        return tree->state_->context;
    }

    [[nodiscard]] const muiAccessTree* read() const noexcept {
#if RAWFRAME_UI_ATSPI
        if (adapter != nullptr) {
            return muiAtspiAdapter_GetTree(adapter);
        }
#endif
#if RAWFRAME_UI_ANDROID
        if (android != nullptr) {
            return muiAndroidAdapter_GetTree(android);
        }
#endif
#if RAWFRAME_UI_UIA
        if (uia != nullptr) {
            return muiUiaAdapter_GetTree(uia);
        }
#endif
#if RAWFRAME_UI_APPKIT
        if (appKit != nullptr) {
            return muiNsAdapter_GetTree(appKit);
        }
#endif
#if RAWFRAME_UI_UIKIT
        if (uiKit != nullptr) {
            return muiUikitAdapter_GetTree(uiKit);
        }
#endif
#if RAWFRAME_UI_ARIA
        if (aria != nullptr) {
            return muiAriaAdapter_GetTree(aria);
        }
#endif
        return copy;
    }

    ~State() {
#if RAWFRAME_UI_ATSPI
        muiDestroyAtspiAdapter(adapter);
        muiDestroyAtspiApp(app);
#endif
#if RAWFRAME_UI_ANDROID
        muiDestroyAndroidAdapter(android);
#endif
#if RAWFRAME_UI_UIA
        muiDestroyUiaAdapter(uia);
#endif
#if RAWFRAME_UI_APPKIT
        muiDestroyNsAdapter(appKit);
#endif
#if RAWFRAME_UI_UIKIT
        muiDestroyUikitAdapter(uiKit);
#endif
#if RAWFRAME_UI_ARIA
        muiDestroyAriaAdapter(aria);
#endif
        muiDestroyAccessTree(copy);
        if (tree != nullptr) {
            static_cast<void>(muiAccess_Disable(context(), root));
        }
    }
};

namespace {

/// What a client asked for (D572): a press, a focus, or a field's edit
/// (D613) kept for the UI's owner, whose input decides what they do; a
/// scroll, and a range stepped or set (D621), done by the tree, as a drag
/// of the range is.
bool act(Access::State& state, const muiAccessRequest& request) {
    const muiNodeId kId = muiNodeIdOfAccess(request.target);
    const Node kNode{.index1 = kId.index1, .generation = kId.generation};
    const auto kKind = [&]() -> std::optional<AccessRequest::Kind> {
        switch (request.action) {
        case mui_actionClick:
            return AccessRequest::Kind::Press;
        case mui_actionFocus:
            return AccessRequest::Kind::Focus;
        case mui_actionSetSelection:
            return AccessRequest::Kind::Select;
        case mui_actionReplaceText:
            return AccessRequest::Kind::Replace;
        default:
            return std::nullopt;
        }
    }();
    if (kKind.has_value()) {
        if (!state.tree->contains(kNode)) {
            return false;
        }
        state.requests.push_back({.kind = *kKind,
                                  .node = kNode,
                                  .anchor = request.anchor,
                                  .focus = request.focus,
                                  .text = *kKind == AccessRequest::Kind::Replace && request.text != nullptr
                                              ? std::string{request.text, request.length}
                                              : std::string{}});
        return true;
    }
    const bool kScroll = request.action >= mui_actionScrollIntoView && request.action <= mui_actionSetScrollOffset;
    const bool kRange = request.action >= mui_actionIncrement && request.action <= mui_actionSetValue;
    bool handled = false;
    return (kScroll || kRange) && muiPerformAccessAction(state.context(), &request, &handled) == mui_success && handled;
}

#if RAWFRAME_UI_ATSPI || RAWFRAME_UI_ANDROID || RAWFRAME_UI_UIA || RAWFRAME_UI_APPKIT || RAWFRAME_UI_UIKIT ||          \
    RAWFRAME_UI_ARIA
bool actFor(void* user, const muiAccessRequest* request) {
    return act(*static_cast<Access::State*>(user), *request);
}
#endif

} // namespace

Access::Access(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Access::~Access() = default;

result::Result<std::unique_ptr<Access>> Access::create(Tree& tree, Node root, const AccessSettings& settings) {
    if (!accessBuilt(settings.platform)) {
        return refuse(UiError::Unavailable, "this build has no adapter for the accessibility platform");
    }
    if (!(settings.scale > 0) || !std::isfinite(settings.scale) || settings.nodes == 0) {
        return refuse(UiError::Invalid, "an accessibility needs a scale above nought and room for a node");
    }
    auto state = std::make_unique<State>();
    RAWFRAME_TRY(checked(muiSetAccessTextFunction(tree.state_->context, &readText, tree.state_.get()),
                         "a tree's texts could not be read"));
    RAWFRAME_TRY(checked(muiAccess_Enable(tree.state_->context, idOf(root)), "a root's accessibility was refused"));
    state->tree = &tree;
    state->root = idOf(root);
#if RAWFRAME_UI_ATSPI
    if (settings.platform == AccessPlatform::Atspi) {
        // Without a session bus there is no screen reader to read the UI,
        // and asking libdbus for one could launch a bus of its own.
        if (std::getenv("AT_SPI_BUS_ADDRESS") == nullptr && std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr) {
            return refuse(UiError::Unavailable, "no accessibility or session bus is named");
        }
        muiAtspiAppDef app = muiDefaultAtspiAppDef();
        app.name = settings.application.c_str();
        app.windows = 1;
        if (muiCreateAtspiApp(&app, &state->app) != mui_success) {
            return refuse(UiError::Unavailable, "the accessibility bus could not be reached");
        }
        muiAtspiAdapterDef adapter = muiDefaultAtspiAdapterDef();
        adapter.nodes = settings.nodes;
        adapter.scale = settings.scale;
        adapter.action = &actFor;
        adapter.user = state.get();
        RAWFRAME_TRY(checked(muiCreateAtspiAdapter(state->app, &adapter, &state->adapter),
                             "the window's accessibility could not be made"));
        if (settings.place.has_value()) {
            muiAtspiAdapter_SetPlace(state->adapter, (*settings.place)[0], (*settings.place)[1]);
        }
        return std::unique_ptr<Access>{new Access{std::move(state)}};
    }
#endif
#if RAWFRAME_UI_ANDROID
    if (settings.platform == AccessPlatform::Android) {
        if (settings.env == nullptr || settings.host == nullptr) {
            return refuse(UiError::Invalid, "Android's accessibility needs the main thread's JNIEnv and the view");
        }
        muiAndroidAdapterDef adapter = muiDefaultAndroidAdapterDef();
        adapter.nodes = settings.nodes;
        adapter.env = settings.env;
        adapter.view = settings.host;
        adapter.scale = settings.scale;
        adapter.action = &actFor;
        adapter.user = state.get();
        RAWFRAME_TRY(checked(muiCreateAndroidAdapter(&adapter, &state->android),
                             "the window's accessibility could not be made"));
        return std::unique_ptr<Access>{new Access{std::move(state)}};
    }
#endif
#if RAWFRAME_UI_UIA
    if (settings.platform == AccessPlatform::Uia) {
        if (settings.host == nullptr) {
            return refuse(UiError::Invalid, "UI Automation needs the window");
        }
        muiUiaAdapterDef adapter = muiDefaultUiaAdapterDef();
        adapter.nodes = settings.nodes;
        adapter.window = settings.host;
        adapter.scale = settings.scale;
        adapter.action = &actFor;
        adapter.user = state.get();
        RAWFRAME_TRY(
            checked(muiCreateUiaAdapter(&adapter, &state->uia), "the window's accessibility could not be made"));
        return std::unique_ptr<Access>{new Access{std::move(state)}};
    }
#endif
#if RAWFRAME_UI_APPKIT
    if (settings.platform == AccessPlatform::AppKit) {
        if (settings.host == nullptr) {
            return refuse(UiError::Invalid, "AppKit's accessibility needs the view");
        }
        muiNsAdapterDef adapter = muiDefaultNsAdapterDef();
        adapter.nodes = settings.nodes;
        adapter.view = settings.host;
        adapter.scale = settings.scale;
        adapter.action = &actFor;
        adapter.user = state.get();
        RAWFRAME_TRY(
            checked(muiCreateNsAdapter(&adapter, &state->appKit), "the window's accessibility could not be made"));
        return std::unique_ptr<Access>{new Access{std::move(state)}};
    }
#endif
#if RAWFRAME_UI_UIKIT
    if (settings.platform == AccessPlatform::UiKit) {
        if (settings.host == nullptr) {
            return refuse(UiError::Invalid, "UIKit's accessibility needs the view");
        }
        muiUikitAdapterDef adapter = muiDefaultUikitAdapterDef();
        adapter.nodes = settings.nodes;
        adapter.view = settings.host;
        adapter.scale = settings.scale;
        adapter.action = &actFor;
        adapter.user = state.get();
        RAWFRAME_TRY(
            checked(muiCreateUikitAdapter(&adapter, &state->uiKit), "the window's accessibility could not be made"));
        return std::unique_ptr<Access>{new Access{std::move(state)}};
    }
#endif
#if RAWFRAME_UI_ARIA
    if (settings.platform == AccessPlatform::Aria) {
        if (settings.selector.empty()) {
            return refuse(UiError::Invalid, "the page's accessibility needs the element over the canvas");
        }
        // Deferred, Maul UI's default: a page cannot tell whether a screen
        // reader runs, so its user presses the hidden button first.
        muiAriaAdapterDef adapter = muiDefaultAriaAdapterDef();
        adapter.nodes = settings.nodes;
        adapter.host = settings.selector.c_str();
        adapter.scale = settings.scale;
        adapter.action = &actFor;
        adapter.user = state.get();
        RAWFRAME_TRY(
            checked(muiCreateAriaAdapter(&adapter, &state->aria), "the page's accessibility could not be made"));
        return std::unique_ptr<Access>{new Access{std::move(state)}};
    }
#endif
    muiAccessTreeDef copy = muiDefaultAccessTreeDef();
    copy.nodes = settings.nodes;
    RAWFRAME_TRY(checked(muiCreateAccessTree(&copy, &state->copy), "an accessibility tree could not be made"));
    return std::unique_ptr<Access>{new Access{std::move(state)}};
}

result::Status Access::update() {
    muiAccessUpdate update{};
    const muiResult kBuilt = muiBuildAccessUpdate(state_->context(), state_->root, &update);
    if (kBuilt != mui_empty) {
        RAWFRAME_TRY(checked(kBuilt, "the tree's accessibility could not be read"));
    }
#if RAWFRAME_UI_ATSPI
    if (state_->adapter != nullptr) {
        if (kBuilt != mui_empty) {
            RAWFRAME_TRY(checked(muiAtspiAdapter_Apply(state_->adapter, &update), "the platform refused the tree"));
        }
        // Clients' calls are answered, and what waits is written, each
        // frame whether or not the tree changed.
        muiAtspiApp_Pump(state_->app);
        return {};
    }
#endif
#if RAWFRAME_UI_ANDROID
    if (state_->android != nullptr) {
        return kBuilt == mui_empty
                   ? result::Status{}
                   : checked(muiAndroidAdapter_Apply(state_->android, &update), "the platform refused the tree");
    }
#endif
#if RAWFRAME_UI_UIA
    if (state_->uia != nullptr) {
        return kBuilt == mui_empty
                   ? result::Status{}
                   : checked(muiUiaAdapter_Apply(state_->uia, &update), "the platform refused the tree");
    }
#endif
#if RAWFRAME_UI_APPKIT
    if (state_->appKit != nullptr) {
        return kBuilt == mui_empty
                   ? result::Status{}
                   : checked(muiNsAdapter_Apply(state_->appKit, &update), "the platform refused the tree");
    }
#endif
#if RAWFRAME_UI_UIKIT
    if (state_->uiKit != nullptr) {
        return kBuilt == mui_empty
                   ? result::Status{}
                   : checked(muiUikitAdapter_Apply(state_->uiKit, &update), "the platform refused the tree");
    }
#endif
#if RAWFRAME_UI_ARIA
    if (state_->aria != nullptr) {
        return kBuilt == mui_empty
                   ? result::Status{}
                   : checked(muiAriaAdapter_Apply(state_->aria, &update), "the platform refused the tree");
    }
#endif
    return kBuilt == mui_empty ? result::Status{}
                               : checked(muiAccessTree_Apply(state_->copy, &update, nullptr),
                                         "the accessibility tree refused an update");
}

result::Status Access::setScale(float scale) {
    if (!(scale > 0) || !std::isfinite(scale)) {
        return refuse(UiError::Invalid, "a scale is above nought");
    }
#if RAWFRAME_UI_ATSPI
    if (state_->adapter != nullptr) {
        return checked(muiAtspiAdapter_SetScale(state_->adapter, scale), "the platform refused a scale");
    }
#endif
#if RAWFRAME_UI_ANDROID
    if (state_->android != nullptr) {
        return checked(muiAndroidAdapter_SetScale(state_->android, scale), "the platform refused a scale");
    }
#endif
#if RAWFRAME_UI_UIA
    if (state_->uia != nullptr) {
        return checked(muiUiaAdapter_SetScale(state_->uia, scale), "the platform refused a scale");
    }
#endif
#if RAWFRAME_UI_APPKIT
    if (state_->appKit != nullptr) {
        return checked(muiNsAdapter_SetScale(state_->appKit, scale), "the platform refused a scale");
    }
#endif
#if RAWFRAME_UI_UIKIT
    if (state_->uiKit != nullptr) {
        return checked(muiUikitAdapter_SetScale(state_->uiKit, scale), "the platform refused a scale");
    }
#endif
#if RAWFRAME_UI_ARIA
    if (state_->aria != nullptr) {
        return checked(muiAriaAdapter_SetScale(state_->aria, scale), "the platform refused a scale");
    }
#endif
    return {};
}

void* Access::root() const noexcept {
#if RAWFRAME_UI_ANDROID
    if (state_->android != nullptr) {
        return muiAndroidAdapter_GetRoot(state_->android);
    }
#endif
#if RAWFRAME_UI_UIA
    if (state_->uia != nullptr) {
        return muiUiaAdapter_GetRoot(state_->uia);
    }
#endif
#if RAWFRAME_UI_APPKIT
    if (state_->appKit != nullptr) {
        return muiNsAdapter_GetRoot(state_->appKit);
    }
#endif
#if RAWFRAME_UI_UIKIT
    if (state_->uiKit != nullptr) {
        return muiUikitAdapter_GetRoot(state_->uiKit);
    }
#endif
    return nullptr;
}

void Access::setPlace(std::array<std::int32_t, 2> place) noexcept {
#if RAWFRAME_UI_ATSPI
    if (state_->adapter != nullptr) {
        muiAtspiAdapter_SetPlace(state_->adapter, place[0], place[1]);
    }
#else
    static_cast<void>(place);
#endif
}

result::Status AccessSeat::seat(Tree& tree, Node root) {
    access_.reset();
    tree_ = &tree;
    root_ = root;
    return make();
}

void AccessSeat::leave() noexcept {
    access_.reset();
    tree_ = nullptr;
}

result::Status AccessSeat::open(const AccessSettings& settings) {
    access_.reset();
    settings_ = settings;
    return make();
}

result::Status AccessSeat::make() {
    if (tree_ == nullptr || !settings_.has_value()) {
        return {};
    }
    RAWFRAME_TRY_ASSIGN(access_, Access::create(*tree_, root_, *settings_));
    return {};
}

result::Status AccessSeat::update() {
    return access_ != nullptr ? access_->update() : result::Status{};
}

result::Status AccessSeat::setScale(float scale) {
    if (settings_.has_value()) {
        settings_->scale = scale;
    }
    return access_ != nullptr ? access_->setScale(scale) : result::Status{};
}

void AccessSeat::setPlace(std::array<std::int32_t, 2> place) noexcept {
    if (settings_.has_value()) {
        settings_->place = place;
    }
    if (access_ != nullptr) {
        access_->setPlace(place);
    }
}

std::optional<Node> Access::focused() const {
    const muiAccessTree* kRead = state_->read();
    if (kRead == nullptr) {
        return std::nullopt;
    }
    const std::uint64_t kFocus = muiAccessTree_GetFocus(kRead);
    if (kFocus == 0 || kFocus == muiAccessTree_GetRoot(kRead)) {
        return std::nullopt;
    }
    const muiNodeId kId = muiNodeIdOfAccess(kFocus);
    return Node{.index1 = kId.index1, .generation = kId.generation};
}

std::vector<AccessRequest> Access::takeRequests() {
    return std::exchange(state_->requests, {});
}

bool Access::ask(const AccessRequest& request) {
    constexpr std::array<muiAccessAction, 4> kActions = {
        mui_actionClick, mui_actionFocus, mui_actionSetSelection, mui_actionReplaceText};
    const muiAccessRequest kRequest{
        .action = kActions[static_cast<std::size_t>(request.kind)],
        .target = muiAccessIdOf(idOf(request.node)),
        .anchor = request.anchor,
        .focus = request.focus,
        .text = request.text.data(),
        .length = static_cast<std::uint32_t>(request.text.size()),
    };
    return act(*state_, kRequest);
}

std::vector<AccessRequest> AccessSeat::takeRequests() {
    return access_ != nullptr ? access_->takeRequests() : std::vector<AccessRequest>{};
}

std::string Access::written() const {
    const muiAccessTree* kRead = state_->read();
    std::size_t length = 0;
    if (kRead == nullptr || muiAccessTree_Write(kRead, nullptr, 0, &length) == mui_errorInvalid) {
        return {};
    }
    std::string text(length + 1, '\0');
    if (muiAccessTree_Write(kRead, text.data(), text.size(), &length) != mui_success) {
        return {};
    }
    text.resize(length);
    return text;
}

} // namespace rawframe::ui
