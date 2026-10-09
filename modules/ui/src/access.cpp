#include "rawframe/ui/access.h"

#include "tree_state.h"

#include <cmath>
#include <cstdlib>
#include <maul-ui/access.h>
#include <maul-ui/access_tree.h>
#include <string>
#include <utility>

#if RAWFRAME_UI_ATSPI
#include <maul-ui/access_atspi.h>
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
    case Role::Generic:
        break;
    }
    return mui_roleGeneric;
}

} // namespace

result::Status Tree::setRole(Node node, Role role) {
    return checked(muiNode_SetAccessRole(state_->context, idOf(node), roleOf(role)), "a node's role was refused");
}

result::Status Tree::setName(Node node, std::string_view name) {
    return checked(muiNode_SetAccessText(state_->context, idOf(node), mui_accessLabel, name.data(), name.size()),
                   "a node's name was refused");
}

namespace {

/// A node's text as assistive technology reads it, its text block's: a
/// label's words, a field's typed text (D571).
bool readText(void* user, muiNodeId node, std::uint64_t /*key*/, const char** textOut, std::size_t* lengthOut) {
    const Tree::State* kState = static_cast<const Tree::State*>(user);
    const Text* kText = kState->textOf(node);
    return kText != nullptr && muiTextBlock_GetText(kState->text, kText->block, textOut, lengthOut) == mui_success;
}

} // namespace

bool accessBuilt(AccessPlatform platform) noexcept {
    return platform == AccessPlatform::Copy || (platform == AccessPlatform::Atspi && RAWFRAME_UI_ATSPI);
}

struct Access::State {
    Tree* tree = nullptr;
    muiNodeId root{};
    /// The copy the platform reads: the AT-SPI adapter's own, or one held
    /// here where there is none.
    muiAccessTree* copy = nullptr;
#if RAWFRAME_UI_ATSPI
    muiAtspiApp* app = nullptr;
    muiAtspiAdapter* adapter = nullptr;
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
        return copy;
    }

    ~State() {
#if RAWFRAME_UI_ATSPI
        muiDestroyAtspiAdapter(adapter);
        muiDestroyAtspiApp(app);
#endif
        muiDestroyAccessTree(copy);
        if (tree != nullptr) {
            static_cast<void>(muiAccess_Disable(context(), root));
        }
    }
};

#if RAWFRAME_UI_ATSPI
namespace {

/// What a client asked for, done by the tree: a focus moved, a value set,
/// a node scrolled; a press the game's own input decides is not done here.
bool act(void* user, const muiAccessRequest* request) {
    bool handled = false;
    return muiPerformAccessAction(static_cast<muiContext*>(user), request, &handled) == mui_success && handled;
}

} // namespace
#endif

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
        adapter.action = &act;
        adapter.user = state->context();
        RAWFRAME_TRY(checked(muiCreateAtspiAdapter(state->app, &adapter, &state->adapter),
                             "the window's accessibility could not be made"));
        if (settings.place.has_value()) {
            muiAtspiAdapter_SetPlace(state->adapter, (*settings.place)[0], (*settings.place)[1]);
        }
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
    return {};
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
