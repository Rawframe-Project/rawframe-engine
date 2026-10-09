#pragma once

// A UI tree as assistive technology reads it (ADR-0034, SPEC-0030's push
// tree, D571): from a root's subtree Maul UI builds updates of the nodes
// that changed, each whole, the focus with every one, and they are handed
// to the platform's adapter, AT-SPI's on Linux, or to a copy of the tree
// where no adapter is built. Nothing is built for a root until an access is
// made for it, and nothing after it is destroyed. Main thread only, as the
// tree's other calls are.

#include "rawframe/composition/participant.h"
#include "rawframe/result/result.h"
#include "rawframe/ui/tree.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::ui {

enum class AccessPlatform : std::uint8_t {
    /// A copy of the tree and no platform: tests, and platforms whose
    /// adapter this build leaves out.
    Copy,
    /// AT-SPI's accessibility bus (Linux): the address AT_SPI_BUS_ADDRESS
    /// names, else the one the session bus gives.
    Atspi,
    /// Android's accessibility framework (D576): a node provider for the
    /// view `AccessSettings` names, which the window gives as its root.
    Android
};

struct AccessSettings {
    AccessPlatform platform = AccessPlatform::Copy;
    /// The application's name, as AT-SPI lists it.
    std::string application = "Rawframe";
    /// Device pixels a UI pixel.
    float scale = 1;
    /// Where the window's client area is on the screen, in device pixels,
    /// where the platform says (X11); none where it does not (Wayland).
    std::optional<std::array<std::int32_t, 2>> place;
    /// The most nodes the platform's copy holds.
    std::uint32_t nodes = 4096;
    /// Android's: the main thread's `JNIEnv*`, and the `android.view.View`
    /// the tree lies in, its origin the tree's (a `jobject`, which the
    /// access holds a reference of its own to).
    void* env = nullptr;
    void* view = nullptr;
};

/// What a screen reader asked of a node (D572), for the UI's owner to do as
/// its own input would: a press, as a pointer's or navigation's, and focus,
/// as navigation's. Scrolls the tree does itself.
struct AccessRequest {
    enum class Kind : std::uint8_t {
        Press,
        Focus
    };
    Kind kind = Kind::Press;
    Node node;
    friend constexpr bool operator==(const AccessRequest&, const AccessRequest&) noexcept = default;
};

/// Whether this build has `platform`'s adapter.
[[nodiscard]] bool accessBuilt(AccessPlatform platform) noexcept;

class Access {
public:
    /// `root`'s subtree of `tree`, read from now on; `tree` outlives it.
    /// Refused (`Unavailable`) where the platform cannot be reached, as a
    /// machine with no accessibility bus.
    [[nodiscard]] static result::Result<std::unique_ptr<Access>>
    create(Tree& tree, Node root, const AccessSettings& settings);

    Access(const Access&) = delete;
    Access& operator=(const Access&) = delete;
    /// No longer read: the root stops building updates.
    ~Access();

    /// What changed in the subtree since the last update handed on, and
    /// the platform's messages answered; once a frame, after the tree's
    /// changes for it.
    [[nodiscard]] result::Status update();
    /// The window's scale and place changed.
    [[nodiscard]] result::Status setScale(float scale);
    void setPlace(std::array<std::int32_t, 2> place) noexcept;

    /// The tree as the platform was last given it, one node a line, as
    /// Maul UI writes it: what a screen reader reads.
    [[nodiscard]] std::string written() const;

    /// The node the platform was last told holds focus, none for the root
    /// (D573).
    [[nodiscard]] std::optional<Node> focused() const;

    /// The root the window hands the platform's clients, where the
    /// platform takes one: Android's node provider, a global reference the
    /// access keeps while it lives (D576); none elsewhere.
    [[nodiscard]] void* root() const noexcept;

    /// What screen readers asked since last taken, oldest first.
    [[nodiscard]] std::vector<AccessRequest> takeRequests();
    /// A request as a platform gives it: one of a node the tree holds is
    /// kept for its owner, a scroll is done, anything else is refused.
    /// Whether it was taken. Platforms call it while they are updated;
    /// tests, to play a screen reader.
    bool ask(AccessRequest::Kind kind, Node node);

    struct State;

private:
    explicit Access(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

/// Where a window's UI is read for assistive technology (D571): the program
/// that runs the window lends it to its Host as `rawframe.ui.access`, the
/// participant whose tree is the window's UI seats its tree there, and the
/// program opens it when the platform is to be told and updates it each
/// frame, after its Host's iterations, on the same thread. The access it
/// makes goes with the tree when the tree leaves, so it never outlives it.
class AccessSeat {
public:
    AccessSeat() = default;
    AccessSeat(const AccessSeat&) = delete;
    AccessSeat& operator=(const AccessSeat&) = delete;

    /// The UI's side: `root`'s subtree of `tree` is the window's, read once
    /// the seat is open; the access made then, if it was, is answered.
    /// `tree` stays until `leave`.
    [[nodiscard]] result::Status seat(Tree& tree, Node root);
    /// The UI's side, before its tree goes: no longer read.
    void leave() noexcept;

    /// The program's side: the window is read with `settings` from the time
    /// a tree is seated, or now when one is; what the seated tree's access
    /// answered, as `seat` does.
    [[nodiscard]] result::Status open(const AccessSettings& settings);
    /// The program's side, once a frame: what changed, handed on.
    [[nodiscard]] result::Status update();
    [[nodiscard]] result::Status setScale(float scale);
    void setPlace(std::array<std::int32_t, 2> place) noexcept;

    /// The access being read, none before one is made.
    [[nodiscard]] const Access* access() const noexcept {
        return access_.get();
    }
    [[nodiscard]] Access* access() noexcept {
        return access_.get();
    }
    /// The UI's side: what screen readers asked since last taken.
    [[nodiscard]] std::vector<AccessRequest> takeRequests();

private:
    [[nodiscard]] result::Status make();

    Tree* tree_ = nullptr;
    Node root_;
    std::optional<AccessSettings> settings_;
    std::unique_ptr<Access> access_;
};

inline constexpr composition::Capability<AccessSeat> kAccessSeat{"rawframe.ui.access"};

} // namespace rawframe::ui
