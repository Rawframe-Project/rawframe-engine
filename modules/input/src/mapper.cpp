#include "rawframe/input/mapper.h"

#include "rawframe/input/errors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace rawframe::input {

namespace {

constexpr std::size_t kUnrouted = std::numeric_limits<std::size_t>::max();

/// A control's state on one device.
struct Held {
    float x = 0;
    float y = 0;
    /// Held when a node claiming it was activated: it reads as rest until
    /// it comes to rest itself, so the node sees no press it did not see
    /// begin.
    bool swallowed = false;

    [[nodiscard]] bool resting() const noexcept {
        return x == 0 && y == 0;
    }
};

struct Device {
    DeviceId id;
    DeviceClass deviceClass = DeviceClass::Keyboard;
    std::uint8_t player = 0;
};

/// An active context: a routing node.
struct Node {
    std::size_t context = 0;
    bool enabled = true;
    /// Larger is more recently activated or brought to front.
    std::uint64_t serial = 0;
};

struct Live {
    ActionState state;
    /// On when routing last changed: it keeps reading its controls as they
    /// are until it turns off, so the node that saw the press sees the
    /// release.
    bool pinned = false;
    /// Whether the context it was last routed through is gated by text
    /// editing.
    bool gated = true;
};

struct Player {
    std::vector<Node> nodes;
    /// Each action's routing rank (its first node in dispatch order), or
    /// unrouted.
    std::vector<std::size_t> rankOf;
    /// Each claimed control's highest claimant's rank.
    std::map<Control, std::size_t> claims;
    std::vector<Live> live;
    std::vector<ActionState> committed;
    std::vector<Edge> pendingTick;
    std::vector<Edge> committedEdges;
    std::vector<Edge> frame;
    std::uint32_t tickOrdinal = 0;
    std::uint32_t frameOrdinal = 0;
    std::deque<ControlEvent> queue;
    bool resetPending = false;
};

float deadzoned(float magnitude, const Binding& binding) noexcept {
    if (magnitude <= binding.deadzoneLower) {
        return 0;
    }
    return std::min((magnitude - binding.deadzoneLower) / (binding.deadzoneUpper - binding.deadzoneLower), 1.0F);
}

} // namespace

struct Mapper::State {
    ActionSet set;
    MapperLimits limits;
    MapperStatistics statistics;
    std::vector<Device> devices;
    std::map<std::pair<std::uint32_t, Control>, Held> held;
    std::vector<Player> players;
    std::uint64_t serial = 0;
    std::uint64_t committedTick = 0;
    bool textEditing = false;

    [[nodiscard]] Device* deviceOf(DeviceId id) noexcept {
        const auto kFound = std::ranges::find(devices, id, &Device::id);
        return kFound == devices.end() ? nullptr : &*kFound;
    }

    [[nodiscard]] const Held* heldOf(DeviceId device, Control control) const noexcept {
        const auto kFound = held.find({device.value, control});
        return kFound == held.end() ? nullptr : &kFound->second;
    }

    /// What `action` of `player` reads of `control` on `device`, after
    /// routing and the text gate.
    [[nodiscard]] Held visible(const Player& player, std::size_t action, DeviceId device, Control control) const {
        const Held* found = heldOf(device, control);
        if (found == nullptr) {
            return {};
        }
        const Live& live = player.live[action];
        if (textEditing && control.device == DeviceClass::Keyboard && live.gated) {
            return {};
        }
        if (live.pinned) {
            return *found;
        }
        const std::size_t kRank = player.rankOf[action];
        if (kRank == kUnrouted || found->swallowed) {
            return {};
        }
        const auto kClaim = player.claims.find(control);
        if (kClaim != player.claims.end() && kClaim->second < kRank) {
            return {};
        }
        return *found;
    }

    /// Modifier bits held on a keyboard, besides the one `except` is.
    [[nodiscard]] std::uint8_t modifiersHeld(DeviceId device, Control except) const {
        std::uint8_t bits = 0;
        for (const auto& [kKey, kHeld] : held) {
            if (kKey.first != device.value || kHeld.resting() || kKey.second == except) {
                continue;
            }
            if (const auto kModifier = modifierOf(kKey.second)) {
                bits |= static_cast<std::uint8_t>(*kModifier);
            }
        }
        return bits;
    }

    /// One binding's value on one device, before the action's type is
    /// applied: one or two axes.
    [[nodiscard]] std::array<float, 2>
    bindingValue(const Player& player, std::size_t action, const Binding& binding, DeviceId device) const {
        const auto kRead = [&](std::size_t part) {
            return visible(player, action, device, binding.controls[part]);
        };
        const auto kOn = [&](std::size_t part) {
            return kRead(part).resting() ? 0.0F : 1.0F;
        };
        std::array<float, 2> value{};
        switch (binding.composite) {
        case Composite::Pair:
            value = {kOn(1) - kOn(0), 0};
            break;
        case Composite::Quad: {
            value = {kOn(3) - kOn(2), kOn(0) - kOn(1)};
            const float kLength = std::hypot(value[0], value[1]);
            if (binding.mode == CompositeMode::DigitalNormalized && kLength > 1) {
                value = {value[0] / kLength, value[1] / kLength};
            }
            break;
        }
        case Composite::None: {
            const Control kControl = binding.controls[0];
            const Held kHeld = kRead(0);
            if (binding.device == DeviceClass::Keyboard && binding.modifiers != 0 && !kHeld.resting()) {
                const std::uint8_t kModifiers = modifiersHeld(device, kControl);
                const bool kMatch = binding.exactModifiers ? kModifiers == binding.modifiers
                                                           : (kModifiers & binding.modifiers) == binding.modifiers;
                if (!kMatch) {
                    return {};
                }
            } else if (binding.exactModifiers && !kHeld.resting() && modifiersHeld(device, kControl) != 0) {
                return {};
            }
            switch (shapeOf(kControl)) {
            case ControlShape::Digital:
                value = {kHeld.resting() ? 0.0F : 1.0F, 0};
                break;
            case ControlShape::Axis1: {
                const float kMagnitude = relative(kControl) ? std::abs(kHeld.x) : deadzoned(std::abs(kHeld.x), binding);
                value = {std::copysign(kMagnitude, kHeld.x), 0};
                break;
            }
            case ControlShape::Axis2: {
                const float kLength = std::hypot(kHeld.x, kHeld.y);
                if (relative(kControl) || kLength == 0) {
                    value = {kHeld.x, kHeld.y};
                } else {
                    const float kScale = deadzoned(kLength, binding) / kLength;
                    value = {kHeld.x * kScale, kHeld.y * kScale};
                }
                break;
            }
            }
            break;
        }
        }
        if (binding.invertX) {
            value[0] = -value[0];
        }
        if (binding.invertY) {
            value[1] = -value[1];
        }
        return {value[0] * binding.scale, value[1] * binding.scale};
    }

    void pushEdge(Player& player, std::size_t action, bool pressed) {
        player.pendingTick.push_back(Edge{.action = action, .pressed = pressed, .ordinal = player.tickOrdinal++});
        player.frame.push_back(Edge{.action = action, .pressed = pressed, .ordinal = player.frameOrdinal++});
    }

    /// Every action of the player from its controls as they are now; edges
    /// where one turns on or off.
    void evaluate(std::uint8_t slot) {
        Player& player = players[slot];
        for (std::size_t index = 0; index < set.actions.size(); ++index) {
            const Action& action = set.actions[index];
            std::array<float, 2> best{};
            float magnitude = 0;
            for (const Binding& binding : action.bindings) {
                for (const Device& device : devices) {
                    if (device.player != slot || device.deviceClass != binding.device) {
                        continue;
                    }
                    const std::array<float, 2> kValue = bindingValue(player, index, binding, device.id);
                    const float kMagnitude = std::hypot(kValue[0], kValue[1]);
                    // The strongest binding speaks; the first of equals.
                    if (kMagnitude > magnitude) {
                        magnitude = kMagnitude;
                        best = kValue;
                    }
                }
            }
            Live& live = player.live[index];
            const bool kOn = live.state.on ? magnitude >= action.releaseThreshold && magnitude > 0
                                           : magnitude >= action.pressThreshold;
            if (kOn != live.state.on) {
                pushEdge(player, index, kOn);
            }
            live.state.on = kOn;
            if (!kOn) {
                live.pinned = false;
            }
            switch (action.type) {
            case ValueType::Bool:
                live.state.x = kOn ? 1.0F : 0.0F;
                live.state.y = 0;
                break;
            case ValueType::Axis1D:
                live.state.x = best[0];
                live.state.y = 0;
                break;
            case ValueType::Axis2D:
                live.state.x = best[0];
                live.state.y = best[1];
                break;
            }
        }
    }

    /// The player's routing after a change: ranks, claims, and what the
    /// node just activated (if any) must not see pressed.
    void routingChanged(std::uint8_t slot, std::optional<std::size_t> activated) {
        Player& player = players[slot];
        for (Live& live : player.live) {
            live.pinned = live.pinned || live.state.on;
        }
        std::vector<const Node*> order;
        for (const Node& node : player.nodes) {
            if (node.enabled) {
                order.push_back(&node);
            }
        }
        std::ranges::sort(order, [this](const Node* left, const Node* right) {
            const std::int64_t kLeft = set.contexts[left->context].priority;
            const std::int64_t kRight = set.contexts[right->context].priority;
            return kLeft != kRight ? kLeft > kRight : left->serial > right->serial;
        });
        std::ranges::fill(player.rankOf, kUnrouted);
        player.claims.clear();
        std::optional<std::size_t> activatedRank;
        for (std::size_t rank = 0; rank < order.size(); ++rank) {
            const Context& context = set.contexts[order[rank]->context];
            if (activated == order[rank]->context) {
                activatedRank = rank;
            }
            for (const std::size_t kAction : context.actions) {
                if (player.rankOf[kAction] != kUnrouted) {
                    continue;
                }
                player.rankOf[kAction] = rank;
                if (!player.live[kAction].pinned) {
                    player.live[kAction].gated = context.textEditGated;
                }
                if (!set.actions[kAction].consume) {
                    continue;
                }
                for (const Binding& binding : set.actions[kAction].bindings) {
                    for (const Control kControl : binding.controls) {
                        if (kControl.valid()) {
                            player.claims.try_emplace(kControl, rank);
                        }
                    }
                }
            }
        }
        if (activatedRank) {
            for (const auto& [kControl, kRank] : player.claims) {
                if (kRank != *activatedRank) {
                    continue;
                }
                for (const Device& device : devices) {
                    if (device.player != slot) {
                        continue;
                    }
                    const auto kHeld = held.find({device.id.value, kControl});
                    if (kHeld != held.end() && !kHeld->second.resting()) {
                        kHeld->second.swallowed = true;
                    }
                }
            }
        }
        evaluate(slot);
    }

    /// Every control of the player's devices at rest.
    void releasePlayer(std::uint8_t slot) {
        for (auto& [kKey, value] : held) {
            const Device* device = deviceOf(DeviceId{kKey.first});
            if (device != nullptr && device->player == slot) {
                value = Held{};
            }
        }
        evaluate(slot);
    }

    void apply(std::uint8_t slot, const ControlEvent& event) {
        Held& state = held[{event.device.value, event.control}];
        switch (shapeOf(event.control)) {
        case ControlShape::Digital:
            state.x = event.x != 0 ? 1.0F : 0.0F;
            state.y = 0;
            break;
        case ControlShape::Axis1:
            state.x = relative(event.control) ? state.x + event.x : std::clamp(event.x, -1.0F, 1.0F);
            state.y = 0;
            break;
        case ControlShape::Axis2:
            if (relative(event.control)) {
                state.x += event.x;
                state.y += event.y;
            } else {
                state.x = std::clamp(event.x, -1.0F, 1.0F);
                state.y = std::clamp(event.y, -1.0F, 1.0F);
            }
            break;
        }
        // Not a number is no reading at all.
        if (!std::isfinite(state.x) || !std::isfinite(state.y)) {
            state.x = 0;
            state.y = 0;
        }
        if (state.resting()) {
            state.swallowed = false;
        }
        evaluate(slot);
    }

    void update() {
        for (std::size_t slot = 0; slot < players.size(); ++slot) {
            Player& player = players[slot];
            const auto kSlot = static_cast<std::uint8_t>(slot);
            if (player.resetPending) {
                player.resetPending = false;
                releasePlayer(kSlot);
            }
            while (!player.queue.empty()) {
                const ControlEvent kEvent = player.queue.front();
                player.queue.pop_front();
                // A device unpaired since the event was queued is ignored.
                const Device* device = deviceOf(kEvent.device);
                if (device != nullptr && device->player == kSlot && device->deviceClass == kEvent.control.device) {
                    apply(kSlot, kEvent);
                }
            }
        }
    }
};

Mapper::Mapper(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Mapper::~Mapper() = default;

result::Result<std::unique_ptr<Mapper>> Mapper::create(ActionSet set, const MapperLimits& limits) {
    if (limits.players == 0 || limits.players > 255) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kInputDomain,
                            code(InputError::TooMany),
                            "a mapper has one to 255 players");
    }
    auto state = std::make_unique<State>();
    state->set = std::move(set);
    state->limits = limits;
    state->players.resize(limits.players);
    for (Player& player : state->players) {
        player.rankOf.assign(state->set.actions.size(), kUnrouted);
        player.live.assign(state->set.actions.size(), Live{});
        player.committed.assign(state->set.actions.size(), ActionState{});
    }
    return std::unique_ptr<Mapper>{new Mapper{std::move(state)}};
}

const ActionSet& Mapper::actions() const noexcept {
    return state_->set;
}

result::Status Mapper::pair(DeviceId device, DeviceClass deviceClass, PlayerSlot player) {
    State& state = *state_;
    if (player.value >= state.players.size()) {
        return result::fail(
            result::ErrorClass::NotFound, kInputDomain, code(InputError::Unknown), "no such player slot");
    }
    if (state.deviceOf(device) != nullptr) {
        return result::fail(
            result::ErrorClass::AlreadyExists, kInputDomain, code(InputError::AlreadyPaired), "the device is paired");
    }
    if (state.devices.size() >= state.limits.maximumDevices) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kInputDomain,
                            code(InputError::TooMany),
                            "more devices than the limit allows");
    }
    state.devices.push_back(Device{.id = device, .deviceClass = deviceClass, .player = player.value});
    std::ranges::sort(state.devices, {}, &Device::id);
    return {};
}

void Mapper::unpair(DeviceId device) {
    State& state = *state_;
    const Device* found = state.deviceOf(device);
    if (found == nullptr) {
        return;
    }
    const std::uint8_t kSlot = found->player;
    std::erase_if(state.held, [device](const auto& entry) {
        return entry.first.first == device.value;
    });
    std::erase_if(state.devices, [device](const Device& each) {
        return each.id == device;
    });
    state.evaluate(kSlot);
}

result::Status Mapper::activate(PlayerSlot player, std::size_t context) {
    State& state = *state_;
    if (player.value >= state.players.size() || context >= state.set.contexts.size()) {
        return result::fail(
            result::ErrorClass::NotFound, kInputDomain, code(InputError::Unknown), "no such player or context");
    }
    Player& target = state.players[player.value];
    if (std::ranges::contains(target.nodes, context, &Node::context)) {
        return {};
    }
    if (target.nodes.size() >= state.limits.maximumActiveNodes) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kInputDomain,
                            code(InputError::TooMany),
                            "more active contexts than the limit allows");
    }
    target.nodes.push_back(Node{.context = context, .enabled = true, .serial = ++state.serial});
    state.routingChanged(player.value, context);
    return {};
}

void Mapper::deactivate(PlayerSlot player, std::size_t context) {
    State& state = *state_;
    if (player.value >= state.players.size()) {
        return;
    }
    if (std::erase_if(state.players[player.value].nodes, [context](const Node& node) {
            return node.context == context;
        }) != 0) {
        state.routingChanged(player.value, std::nullopt);
    }
}

void Mapper::setEnabled(PlayerSlot player, std::size_t context, bool enabled) {
    State& state = *state_;
    if (player.value >= state.players.size()) {
        return;
    }
    for (Node& node : state.players[player.value].nodes) {
        if (node.context == context && node.enabled != enabled) {
            node.enabled = enabled;
            state.routingChanged(player.value, enabled ? std::optional{context} : std::nullopt);
        }
    }
}

void Mapper::bringToFront(PlayerSlot player, std::size_t context) {
    State& state = *state_;
    if (player.value >= state.players.size()) {
        return;
    }
    for (Node& node : state.players[player.value].nodes) {
        if (node.context == context) {
            node.serial = ++state.serial;
            state.routingChanged(player.value, std::nullopt);
        }
    }
}

void Mapper::setTextEditing(bool editing) {
    State& state = *state_;
    if (state.textEditing == editing) {
        return;
    }
    state.textEditing = editing;
    for (std::size_t slot = 0; slot < state.players.size(); ++slot) {
        state.evaluate(static_cast<std::uint8_t>(slot));
    }
}

void Mapper::releaseAll() {
    State& state = *state_;
    for (std::size_t slot = 0; slot < state.players.size(); ++slot) {
        state.players[slot].queue.clear();
        state.releasePlayer(static_cast<std::uint8_t>(slot));
    }
}

void Mapper::submit(const ControlEvent& event) {
    State& state = *state_;
    const Device* device = state.deviceOf(event.device);
    if (device == nullptr || device->deviceClass != event.control.device || nameOf(event.control).empty()) {
        ++state.statistics.unpairedEvents;
        return;
    }
    Player& player = state.players[device->player];
    player.queue.push_back(event);
    if (player.queue.size() > state.limits.maximumQueuedEventsPerPlayer) {
        player.queue.pop_front();
        player.resetPending = true;
        ++state.statistics.droppedEvents;
    }
}

void Mapper::update() {
    state_->update();
}

void Mapper::commit(std::uint64_t tick) {
    State& state = *state_;
    state.update();
    for (std::size_t slot = 0; slot < state.players.size(); ++slot) {
        Player& player = state.players[slot];
        for (std::size_t index = 0; index < player.live.size(); ++index) {
            player.committed[index] = player.live[index].state;
        }
        player.committedEdges = std::move(player.pendingTick);
        player.pendingTick.clear();
        player.tickOrdinal = 0;
    }
    state.committedTick = tick;
    // Relative motion belonged to this tick; the next starts from rest.
    std::vector<bool> moved(state.players.size(), false);
    for (auto& [kKey, value] : state.held) {
        if (relative(kKey.second) && !value.resting()) {
            value = Held{};
            if (const Device* device = state.deviceOf(DeviceId{kKey.first})) {
                moved[device->player] = true;
            }
        }
    }
    for (std::size_t slot = 0; slot < moved.size(); ++slot) {
        if (moved[slot]) {
            state.evaluate(static_cast<std::uint8_t>(slot));
        }
    }
}

void Mapper::endFrame() {
    for (Player& player : state_->players) {
        player.frame.clear();
        player.frameOrdinal = 0;
    }
}

std::uint64_t Mapper::committedTick() const noexcept {
    return state_->committedTick;
}

namespace {

constexpr ActionState kRest{};

} // namespace

const ActionState& Mapper::committed(PlayerSlot player, std::size_t action) const noexcept {
    const State& state = *state_;
    return player.value < state.players.size() && action < state.set.actions.size()
               ? state.players[player.value].committed[action]
               : kRest;
}

std::span<const Edge> Mapper::tickEdges(PlayerSlot player) const noexcept {
    return player.value < state_->players.size() ? std::span<const Edge>{state_->players[player.value].committedEdges}
                                                 : std::span<const Edge>{};
}

bool Mapper::pressedThisTick(PlayerSlot player, std::size_t action) const noexcept {
    return std::ranges::any_of(tickEdges(player), [action](const Edge& edge) {
        return edge.action == action && edge.pressed;
    });
}

bool Mapper::releasedThisTick(PlayerSlot player, std::size_t action) const noexcept {
    return std::ranges::any_of(tickEdges(player), [action](const Edge& edge) {
        return edge.action == action && !edge.pressed;
    });
}

const ActionState& Mapper::current(PlayerSlot player, std::size_t action) const noexcept {
    const State& state = *state_;
    return player.value < state.players.size() && action < state.set.actions.size()
               ? state.players[player.value].live[action].state
               : kRest;
}

std::span<const Edge> Mapper::frameEdges(PlayerSlot player) const noexcept {
    return player.value < state_->players.size() ? std::span<const Edge>{state_->players[player.value].frame}
                                                 : std::span<const Edge>{};
}

const MapperStatistics& Mapper::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::input
