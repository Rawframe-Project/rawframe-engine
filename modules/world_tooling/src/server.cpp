#include "rawframe/world_tooling/server.h"

#include "rawframe/document/json.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world_tooling/errors.h"
#include "rawframe/world_tooling/window_input.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::world_tooling {

namespace {

using document::Value;

/// Whether two texts are equal, in a time that depends on their lengths
/// only, so a token is not guessed a byte at a time.
bool sameSecret(std::string_view given, std::string_view expected) noexcept {
    unsigned difference = given.size() == expected.size() ? 0U : 1U;
    for (std::size_t at = 0; at < given.size(); ++at) {
        const char kExpected = expected.empty() ? '\0' : expected[at % expected.size()];
        difference |= static_cast<unsigned char>(given[at]) ^ static_cast<unsigned char>(kExpected);
    }
    return difference == 0;
}

std::string_view codeName(ToolingError error) noexcept {
    switch (error) {
    case ToolingError::Malformed:
        return "malformed";
    case ToolingError::Unauthenticated:
        return "unauthenticated";
    case ToolingError::NotGranted:
        return "not_granted";
    case ToolingError::Unsupported:
        return "unsupported";
    case ToolingError::LimitExceeded:
        return "limit_exceeded";
    case ToolingError::Configuration:
        return "configuration";
    case ToolingError::NotFound:
        return "not_found";
    case ToolingError::Stopped:
        return "stopped";
    }
    return "malformed";
}

/// One reply line, its line feed included.
std::string replyLine(const Value& id, std::string_view member, Value value) {
    Value made = Value::object();
    made.add("kind", Value::string("tooling.reply"));
    made.add("id", id);
    made.add(std::string{member}, std::move(value));
    return document::writeCompact(made) + "\n";
}

std::string errorLine(const Value& id, ToolingError error, std::string_view why) {
    Value record = Value::object();
    record.add("code", Value::string(std::string{codeName(error)}));
    record.add("message", Value::string(std::string{why}));
    return replyLine(id, "error", std::move(record));
}

std::string entityName(world::EntityHandle entity) {
    return std::to_string(entity.slot) + ":" + std::to_string(entity.generation);
}

/// `slot:generation`, both decimal.
/// A point of three finite numbers within a million metres, as `tooling.look`
/// and `tooling.pick` give one; none otherwise.
std::optional<std::array<double, 3>> pointOf(const Value* point) {
    if (point == nullptr || point->kind() != Value::Kind::Array || point->items().size() != 3) {
        return std::nullopt;
    }
    std::array<double, 3> at{};
    for (std::size_t each = 0; each < 3; ++each) {
        const std::optional<double> kNumber = point->items()[each].real();
        if (!kNumber.has_value() || !std::isfinite(*kNumber) || std::abs(*kNumber) > 1e6) {
            return std::nullopt;
        }
        at[each] = *kNumber;
    }
    return at;
}

/// What `tooling.look` asks: an eye looking at a target with a field of
/// view in degrees (1 to 179), or none to give the player's camera back;
/// nothing, with `refusal` said, for a record out of form (D432).
std::optional<std::optional<Look>> lookOf(const Value& record, std::string& refusal) {
    const Value* kView = record.find("view");
    if (kView != nullptr && kView->isNull()) {
        return std::optional<Look>{};
    }
    const bool kObject = kView != nullptr && kView->kind() == Value::Kind::Object && kView->names().size() == 3;
    const auto kEye = kObject ? pointOf(kView->find("eye")) : std::nullopt;
    const auto kTarget = kObject ? pointOf(kView->find("target")) : std::nullopt;
    const Value* kAngle = kObject ? kView->find("fieldOfView") : nullptr;
    const std::optional<double> kDegrees = kAngle != nullptr ? kAngle->real() : std::nullopt;
    if (!kEye.has_value() || !kTarget.has_value() || !kDegrees.has_value() || !(*kDegrees >= 1 && *kDegrees <= 179)) {
        refusal = "view is null, or an eye and a target of three numbers within a million metres and a fieldOfView "
                  "of 1 to 179 degrees";
        return std::nullopt;
    }
    return std::optional<Look>{Look{.eye = *kEye, .target = *kTarget, .fieldOfView = *kDegrees}};
}

std::optional<world::EntityHandle> entityNamed(std::string_view text) {
    const std::size_t kColon = text.find(':');
    if (kColon == std::string_view::npos || kColon == 0 || kColon + 1 == text.size()) {
        return std::nullopt;
    }
    const auto kNumber = [](std::string_view digits) -> std::optional<std::uint32_t> {
        std::uint64_t value = 0;
        for (const char kDigit : digits) {
            if (kDigit < '0' || kDigit > '9' || digits.size() > 10) {
                return std::nullopt;
            }
            value = (value * 10) + static_cast<std::uint64_t>(kDigit - '0');
        }
        return value <= UINT32_MAX ? std::optional{static_cast<std::uint32_t>(value)} : std::nullopt;
    };
    const auto kSlot = kNumber(text.substr(0, kColon));
    const auto kGeneration = kNumber(text.substr(kColon + 1));
    if (!kSlot.has_value() || !kGeneration.has_value()) {
        return std::nullopt;
    }
    return world::EntityHandle{.slot = *kSlot, .generation = *kGeneration};
}

/// An integer as JSON keeps it exactly: a number to 2^53, text past it.
Value integerValue(std::int64_t number) {
    constexpr std::int64_t kExact = std::int64_t{1} << 53U;
    return number > -kExact && number < kExact ? Value::integer(number) : Value::string(std::to_string(number));
}

Value unsignedValue(std::uint64_t number) {
    return number < (std::uint64_t{1} << 53U) ? Value::integer(static_cast<std::int64_t>(number))
                                              : Value::string(std::to_string(number));
}

template <typename T> T read(const std::byte* at) noexcept {
    T value{};
    std::memcpy(&value, at, sizeof(T));
    return value;
}

/// One field's value from a component's bytes.
Value fieldValue(const world_runtime::ComponentFieldEntry& field, const std::byte* bytes) {
    using K = world_runtime::ComponentFieldKind;
    const std::byte* at = bytes + field.offset;
    switch (field.kind) {
    case K::I8:
        return integerValue(read<std::int8_t>(at));
    case K::I16:
        return integerValue(read<std::int16_t>(at));
    case K::I32:
        return integerValue(read<std::int32_t>(at));
    case K::I64:
        return integerValue(read<std::int64_t>(at));
    case K::U8:
        return unsignedValue(read<std::uint8_t>(at));
    case K::U16:
        return unsignedValue(read<std::uint16_t>(at));
    case K::U32:
        return unsignedValue(read<std::uint32_t>(at));
    case K::U64:
        return unsignedValue(read<std::uint64_t>(at));
    case K::F32:
        return Value::real(read<float>(at));
    case K::F64:
        return Value::real(read<double>(at));
    case K::Bool:
        return Value::boolean(read<std::uint8_t>(at) != 0);
    case K::Entity: {
        const auto kEntity = read<world::EntityHandle>(at);
        return kEntity.isNull() ? Value{} : Value::string(entityName(kEntity));
    }
    case K::Case: {
        const auto kCase = read<std::uint32_t>(at);
        return kCase < field.cases.size() ? Value::string(field.cases[kCase]) : unsignedValue(kCase);
    }
    }
    return Value{};
}

struct Client {
    execution::MonotonicInstant since;
    std::optional<network::StreamId> stream;
    std::string pending;
    bool admitted = false;
    bool closing = false;
    /// When it began closing: the client closes once it has read the last
    /// reply, or the server does a second later. Closing at once can cut
    /// the reply off: a provider hands bytes on before its peer has them.
    std::optional<execution::MonotonicInstant> closingSince;
};

/// What `debug.break` takes at most: names or lines, and a name's or a
/// file's bytes.
constexpr std::size_t kMostBreakpoints = 64;
constexpr std::size_t kMostFunctionName = 256;

/// `debug.break`'s lines (D630): absent for none, else up to 64 of a
/// `file`, a path's bytes, and a `line` from one; none for anything else.
std::optional<std::vector<world_runtime::DebugLine>> linesOf(const Value* lines) {
    std::vector<world_runtime::DebugLine> read;
    if (lines == nullptr) {
        return read;
    }
    if (lines->kind() != Value::Kind::Array || lines->items().size() > kMostBreakpoints) {
        return std::nullopt;
    }
    for (const Value& each : lines->items()) {
        const Value* const kFile = each.kind() == Value::Kind::Object ? each.find("file") : nullptr;
        const Value* const kLine = each.kind() == Value::Kind::Object ? each.find("line") : nullptr;
        const std::optional<std::int64_t> kNumber = kLine != nullptr ? kLine->integer() : std::nullopt;
        if (kFile == nullptr || kFile->kind() != Value::Kind::String || kFile->text()->empty() ||
            kFile->text()->size() > kMostFunctionName || !kNumber.has_value() || *kNumber < 1 ||
            *kNumber > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        read.push_back(world_runtime::DebugLine{.file = *kFile->text(), .line = static_cast<std::uint32_t>(*kNumber)});
    }
    return read;
}

constexpr execution::MonotonicDuration kLastReplyWithin = execution::MonotonicDuration::fromSeconds(1);

} // namespace

struct ToolingServer::State {
    network::Provider* provider = nullptr;
    ToolingSettings settings;
    std::map<std::uint64_t, Client> clients;
    std::vector<network::Event> events;
    Statistics statistics;
    /// Serving from inside a stopped tick, and whether a client said to
    /// carry on (D460).
    bool stopped = false;
    bool carryOn = false;
    world::TickIndex lastTick;

    void send(network::ConnectionId connection, Client& client, std::string_view line) {
        if (!client.stream.has_value()) {
            return;
        }
        const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(line.data()), line.size()};
        if (!provider->send(connection, *client.stream, kBytes).has_value()) {
            client.closing = true;
        }
    }

    /// Answers with an error and closes the connection.
    void refuse(
        network::ConnectionId connection, Client& client, const Value& id, ToolingError error, std::string_view why) {
        send(connection, client, errorLine(id, error, why));
        client.closing = true;
        ++statistics.refused;
    }

    Value status(const world::World* world, world::TickIndex tick) const {
        Value made = Value::object();
        made.add("kind", Value::string("tooling.status"));
        made.add("tick", Value::integer(static_cast<std::int64_t>(tick.value)));
        made.add("world", Value::boolean(world != nullptr));
        made.add("entities", Value::integer(static_cast<std::int64_t>(world != nullptr ? world->entityCount() : 0)));
        Value components = Value::array();
        if (world != nullptr) {
            // Each component's holders, by name, in name order.
            std::map<std::string_view, std::size_t> held;
            for (const auto& archetype : world->archetypes()) {
                for (const schema::ComponentRuntimeId kComponent : archetype->components()) {
                    held[world->registry().descriptor(kComponent).name] += archetype->size();
                }
            }
            for (const auto& [name, count] : held) {
                Value each = Value::object();
                each.add("name", Value::string(std::string{name}));
                each.add("entities", Value::integer(static_cast<std::int64_t>(count)));
                components.push(std::move(each));
            }
        }
        made.add("components", std::move(components));
        return made;
    }

    /// The living entities in slot order, a page after `after`.
    Value entities(const world::World& world, const Value& record, std::string& refusal) const {
        std::optional<world::EntityHandle> after;
        if (const Value* kAfter = record.find("after"); kAfter != nullptr) {
            after = kAfter->kind() == Value::Kind::String ? entityNamed(*kAfter->text()) : std::nullopt;
            if (!after.has_value()) {
                refusal = "after names an entity as slot:generation";
                return {};
            }
        }
        std::int64_t limit = 64;
        if (const Value* kLimit = record.find("limit"); kLimit != nullptr) {
            limit = kLimit->integer().value_or(0);
            if (limit < 1 || limit > 256) {
                refusal = "limit is 1 to 256";
                return {};
            }
        }
        std::optional<schema::ComponentRuntimeId> holding;
        if (const Value* kComponent = record.find("component"); kComponent != nullptr) {
            const std::string* kName = kComponent->kind() == Value::Kind::String ? kComponent->text() : nullptr;
            for (std::size_t at = 0; kName != nullptr && at < world.registry().components().size(); ++at) {
                if (world.registry().components()[at].name == *kName) {
                    holding = schema::ComponentRuntimeId{static_cast<std::uint32_t>(at)};
                }
            }
            if (!holding.has_value()) {
                refusal = "component names one of the World's components";
                return {};
            }
        }
        struct Row {
            world::EntityHandle entity;
            const world::detail::Archetype* archetype = nullptr;
        };
        std::vector<Row> rows;
        for (const auto& archetype : world.archetypes()) {
            if (holding.has_value() && !archetype->has(*holding)) {
                continue;
            }
            for (const world::EntityHandle kEntity : archetype->entities()) {
                if (!after.has_value() || kEntity.slot > after->slot) {
                    rows.push_back(Row{.entity = kEntity, .archetype = archetype.get()});
                }
            }
        }
        std::ranges::sort(rows, {}, [](const Row& row) {
            return row.entity.slot;
        });
        const auto kShown = std::min(rows.size(), static_cast<std::size_t>(limit));
        Value listed = Value::array();
        for (std::size_t at = 0; at < kShown; ++at) {
            Value names = Value::array();
            for (const schema::ComponentRuntimeId kComponent : rows[at].archetype->components()) {
                names.push(Value::string(std::string{world.registry().descriptor(kComponent).name}));
            }
            Value each = Value::object();
            each.add("entity", Value::string(entityName(rows[at].entity)));
            each.add("components", std::move(names));
            listed.push(std::move(each));
        }
        Value made = Value::object();
        made.add("kind", Value::string("tooling.entities"));
        made.add("entities", std::move(listed));
        made.add("more", Value::boolean(rows.size() > kShown));
        return made;
    }

    /// One living entity's components, field by field where known.
    Value entity(const world::World& world, world::EntityHandle handle) const {
        const world::detail::Archetype* holder = nullptr;
        for (const auto& archetype : world.archetypes()) {
            if (std::ranges::find(archetype->entities(), handle) != archetype->entities().end()) {
                holder = archetype.get();
            }
        }
        Value components = Value::array();
        for (const schema::ComponentRuntimeId kComponent : holder->components()) {
            const schema::ComponentDescriptor& descriptor = world.registry().descriptor(kComponent);
            Value each = Value::object();
            each.add("name", Value::string(std::string{descriptor.name}));
            const world_runtime::ComponentFieldSet* known = nullptr;
            if (settings.fields != nullptr) {
                for (const world_runtime::ComponentFieldSet& set : settings.fields->fieldSets()) {
                    if (set.id == descriptor.id && set.size == descriptor.size) {
                        known = &set;
                    }
                }
            }
            const auto* kBytes = static_cast<const std::byte*>(world.getErased(handle, kComponent));
            if (known != nullptr && kBytes != nullptr) {
                Value fields = Value::object();
                for (const world_runtime::ComponentFieldEntry& field : known->fields) {
                    fields.add(field.name, fieldValue(field, kBytes));
                }
                each.add("fields", std::move(fields));
            }
            components.push(std::move(each));
        }
        Value made = Value::object();
        made.add("kind", Value::string("tooling.entity"));
        made.add("entity", Value::string(entityName(handle)));
        made.add("components", std::move(components));
        return made;
    }

    /// One whole record from a client.
    void answer(network::ConnectionId connection,
                Client& client,
                std::string_view line,
                const world::World* world,
                world::TickIndex tick) {
        ++statistics.records;
        auto parsed = document::parse(line, document::ReadLimits{.maximumBytes = settings.maximumRecord});
        const Value kNone;
        if (!parsed.has_value() || parsed->kind() != Value::Kind::Object) {
            refuse(connection, client, kNone, ToolingError::Malformed, "a record is one strict JSON object a line");
            return;
        }
        const Value* kId = parsed->find("id");
        const Value& id = kId != nullptr ? *kId : kNone;
        const Value* kKind = parsed->find("kind");
        const std::string* kind = kKind != nullptr && kKind->kind() == Value::Kind::String ? kKind->text() : nullptr;
        if (!client.admitted) {
            const Value* kVersion = parsed->find("protocolVersion");
            const Value* kToken = parsed->find("token");
            if (kind == nullptr || *kind != "tooling.hello" || kToken == nullptr ||
                kToken->kind() != Value::Kind::String) {
                refuse(connection, client, id, ToolingError::Unauthenticated, "a connection begins with hello");
                return;
            }
            if (kVersion == nullptr || kVersion->integer() != kToolingProtocolVersion) {
                refuse(connection,
                       client,
                       id,
                       ToolingError::Unsupported,
                       "this endpoint speaks tooling protocol version 1 only");
                return;
            }
            if (!sameSecret(*kToken->text(), settings.token)) {
                refuse(connection, client, id, ToolingError::Unauthenticated, "the token is not this endpoint's");
                return;
            }
            client.admitted = true;
            ++statistics.admitted;
            Value grants = Value::array();
            if (settings.grants.inspect) {
                grants.push(Value::string("inspect"));
            }
            if (settings.grants.view) {
                grants.push(Value::string("view"));
            }
            if (settings.grants.debug && settings.debugging != nullptr) {
                grants.push(Value::string("debug"));
            }
            Value made = Value::object();
            made.add("kind", Value::string("tooling.welcome"));
            made.add("protocolVersion", Value::integer(kToolingProtocolVersion));
            made.add("grants", std::move(grants));
            send(connection, client, replyLine(id, "answer", std::move(made)));
            return;
        }
        // Inside a stopped tick the World is half-stepped: only the
        // debugger is answered (D460).
        const bool kDebugVerb = kind != nullptr && kind->starts_with("debug.");
        if (stopped && !kDebugVerb) {
            send(connection,
                 client,
                 errorLine(id, ToolingError::Stopped, "the game is stopped at a breakpoint; only debug verbs answer"));
            return;
        }
        if (kDebugVerb) {
            debug(connection, client, id, *kind, *parsed);
            return;
        }
        if (kind != nullptr && *kind == "tooling.status") {
            if (!settings.grants.inspect) {
                send(connection, client, errorLine(id, ToolingError::NotGranted, "status needs the inspect grant"));
                return;
            }
            send(connection, client, replyLine(id, "answer", status(world, tick)));
            return;
        }
        if (kind != nullptr && (*kind == "tooling.entities" || *kind == "tooling.read_entity")) {
            if (!settings.grants.inspect) {
                send(connection, client, errorLine(id, ToolingError::NotGranted, "reading needs the inspect grant"));
                return;
            }
            if (world == nullptr) {
                send(connection, client, errorLine(id, ToolingError::NotFound, "no World is running"));
                return;
            }
            if (*kind == "tooling.entities") {
                std::string refusal;
                Value made = entities(*world, *parsed, refusal);
                send(connection,
                     client,
                     refusal.empty() ? replyLine(id, "answer", std::move(made))
                                     : errorLine(id, ToolingError::Malformed, refusal));
                return;
            }
            const Value* kEntity = parsed->find("entity");
            const auto kHandle = kEntity != nullptr && kEntity->kind() == Value::Kind::String
                                     ? entityNamed(*kEntity->text())
                                     : std::nullopt;
            if (!kHandle.has_value()) {
                send(connection,
                     client,
                     errorLine(id, ToolingError::Malformed, "entity names an entity as slot:generation"));
            } else if (!world->alive(*kHandle)) {
                send(connection, client, errorLine(id, ToolingError::NotFound, "no such entity is alive"));
            } else {
                send(connection, client, replyLine(id, "answer", entity(*world, *kHandle)));
            }
            return;
        }
        if (kind != nullptr && *kind == "tooling.look") {
            if (!settings.grants.view) {
                send(connection, client, errorLine(id, ToolingError::NotGranted, "looking needs the view grant"));
                return;
            }
            if (settings.previewer == nullptr) {
                send(connection, client, errorLine(id, ToolingError::NotFound, "this Runtime shows no preview"));
                return;
            }
            std::string refusal;
            const std::optional<std::optional<Look>> kLook = lookOf(*parsed, refusal);
            if (!kLook.has_value()) {
                send(connection, client, errorLine(id, ToolingError::Malformed, refusal));
                return;
            }
            // And the looks the window is split to show beside it (D797):
            // at most three, each a view as `view` is.
            std::vector<Look> others;
            if (const Value* kOthers = parsed->find("others"); kOthers != nullptr) {
                bool sound = kOthers->kind() == Value::Kind::Array && kOthers->items().size() <= 3;
                for (std::size_t at = 0; sound && at < kOthers->items().size(); ++at) {
                    Value one = Value::object();
                    one.add("view", kOthers->items()[at]);
                    std::string ignored;
                    const std::optional<std::optional<Look>> kOther = lookOf(one, ignored);
                    sound = kOther.has_value() && kOther->has_value();
                    if (sound) {
                        others.push_back(**kOther);
                    }
                }
                if (!sound) {
                    send(connection, client, errorLine(id, ToolingError::Malformed, "others are at most three views"));
                    return;
                }
            }
            if (!settings.previewer->look(*kLook)) {
                send(connection, client, errorLine(id, ToolingError::Malformed, "a view's eye is at its target"));
                return;
            }
            const bool kSplit = settings.previewer->split(others);
            Value made = Value::object();
            made.add("kind", Value::string("tooling.looking"));
            made.add("previewing", Value::boolean(kLook->has_value()));
            made.add("split", Value::integer(kSplit ? static_cast<std::int64_t>(others.size()) : 0));
            send(connection, client, replyLine(id, "answer", std::move(made)));
            return;
        }
        if (kind != nullptr && *kind == "tooling.mark") {
            // A point the preview marks, or none (D464): the view's.
            if (!settings.grants.view) {
                send(connection, client, errorLine(id, ToolingError::NotGranted, "marking needs the view grant"));
                return;
            }
            const Value* kAt = parsed->find("at");
            const std::optional<std::array<double, 3>> kPoint = pointOf(kAt);
            if (kAt == nullptr || (!kAt->isNull() && !kPoint.has_value())) {
                send(connection,
                     client,
                     errorLine(id, ToolingError::Malformed, "at is a point of three finite numbers, or null"));
                return;
            }
            Value made = Value::object();
            made.add("kind", Value::string("tooling.marked"));
            // The part drawn lit, by name; none unless named (D468).
            const Value* kLit = parsed->find("lit");
            constexpr std::array<std::string_view, 5> kParts = {"", "x", "y", "z", "ring"};
            std::uint8_t lit = 0;
            if (kLit != nullptr && !kLit->isNull()) {
                const auto kNamed = kLit->text() != nullptr ? std::ranges::find(kParts, *kLit->text()) : kParts.end();
                if (kNamed == kParts.end() || kNamed == kParts.begin()) {
                    send(connection, client, errorLine(id, ToolingError::Malformed, "lit is x, y, z, ring, or null"));
                    return;
                }
                lit = static_cast<std::uint8_t>(kNamed - kParts.begin());
            }
            made.add("marked", Value::boolean(settings.previewer != nullptr && settings.previewer->mark(kPoint, lit)));
            send(connection, client, replyLine(id, "answer", std::move(made)));
            return;
        }
        if (kind != nullptr && *kind == "tooling.hide") {
            // The layers the preview leaves out (D791): the view's.
            if (!settings.grants.view) {
                send(connection, client, errorLine(id, ToolingError::NotGranted, "hiding needs the view grant"));
                return;
            }
            const Value* kLayers = parsed->find("layers");
            std::vector<std::uint32_t> layers;
            bool sound = kLayers != nullptr && kLayers->kind() == Value::Kind::Array && kLayers->items().size() <= 64;
            for (std::size_t at = 0; sound && at < kLayers->items().size(); ++at) {
                const std::optional<std::int64_t> kLayer = kLayers->items()[at].integer();
                sound = kLayer.has_value() && *kLayer > 0 && *kLayer <= std::numeric_limits<std::uint32_t>::max();
                if (sound) {
                    layers.push_back(static_cast<std::uint32_t>(*kLayer));
                }
            }
            if (!sound) {
                send(connection,
                     client,
                     errorLine(id, ToolingError::Malformed, "layers are at most 64 layer numbers, each above nought"));
                return;
            }
            Value made = Value::object();
            made.add("kind", Value::string("tooling.hidden"));
            made.add("hidden", Value::boolean(settings.previewer != nullptr && settings.previewer->hide(layers)));
            send(connection, client, replyLine(id, "answer", std::move(made)));
            return;
        }
        if (kind != nullptr && *kind == "tooling.overlays") {
            // What the preview draws over its scene (D798): the view's.
            // The names are the renderer's; this only bounds them.
            if (!settings.grants.view) {
                send(connection, client, errorLine(id, ToolingError::NotGranted, "overlays need the view grant"));
                return;
            }
            const Value* kOverlays = parsed->find("overlays");
            std::vector<std::string> names;
            bool sound =
                kOverlays != nullptr && kOverlays->kind() == Value::Kind::Array && kOverlays->items().size() <= 8;
            for (std::size_t at = 0; sound && at < kOverlays->items().size(); ++at) {
                const Value& kItem = kOverlays->items()[at];
                const std::string* kName = kItem.kind() == Value::Kind::String ? kItem.text() : nullptr;
                sound = kName != nullptr && !kName->empty() && kName->size() <= 32;
                if (sound) {
                    names.push_back(*kName);
                }
            }
            if (!sound) {
                send(connection, client, errorLine(id, ToolingError::Malformed, "overlays are at most 8 names"));
                return;
            }
            Value made = Value::object();
            made.add("kind", Value::string("tooling.overlays"));
            made.add("shown", Value::boolean(settings.previewer != nullptr && settings.previewer->overlay(names)));
            send(connection, client, replyLine(id, "answer", std::move(made)));
            return;
        }
        if (kind != nullptr && *kind == "tooling.view_mode") {
            // How the preview is drawn (D794): the view's. The names are
            // the renderer's; this only bounds the text.
            if (!settings.grants.view) {
                send(connection, client, errorLine(id, ToolingError::NotGranted, "a view mode needs the view grant"));
                return;
            }
            const Value* kMode = parsed->find("mode");
            if (kMode == nullptr || kMode->text() == nullptr || kMode->text()->empty() || kMode->text()->size() > 32) {
                send(connection, client, errorLine(id, ToolingError::Malformed, "a view mode is a name"));
                return;
            }
            Value made = Value::object();
            made.add("kind", Value::string("tooling.view_mode"));
            made.add("shown",
                     Value::boolean(settings.previewer != nullptr && settings.previewer->viewMode(*kMode->text())));
            send(connection, client, replyLine(id, "answer", std::move(made)));
            return;
        }
        if (kind != nullptr && *kind == "tooling.input") {
            // What an author did in an editor's viewport showing the
            // preview (D790): the view's, as its look is.
            if (!settings.grants.view) {
                send(connection, client, errorLine(id, ToolingError::NotGranted, "input needs the view grant"));
                return;
            }
            const Value* kEvents = parsed->find("events");
            const auto kRead = kEvents != nullptr ? windowInputsOf(*kEvents) : std::nullopt;
            if (!kRead.has_value()) {
                send(connection,
                     client,
                     errorLine(id, ToolingError::Malformed, "events are at most 64 input events that read"));
                return;
            }
            Value made = Value::object();
            made.add("kind", Value::string("tooling.inputted"));
            made.add("taken", Value::boolean(settings.previewer != nullptr && settings.previewer->input(*kRead)));
            send(connection, client, replyLine(id, "answer", std::move(made)));
            return;
        }
        if (kind != nullptr && *kind == "tooling.clicked") {
            // Where the author pressed in the preview (D456): the view's.
            if (!settings.grants.view) {
                send(connection, client, errorLine(id, ToolingError::NotGranted, "clicks need the view grant"));
                return;
            }
            const std::optional<Clicked> kClicked =
                settings.previewer != nullptr ? settings.previewer->clicked() : std::nullopt;
            Value made = Value::object();
            made.add("kind", Value::string("tooling.clicked"));
            made.add("count", Value::integer(kClicked.has_value() ? static_cast<std::int64_t>(kClicked->count) : 0));
            const auto kPoint = [](const std::array<double, 3>& at) {
                Value point = Value::array();
                for (const double kAt : at) {
                    point.push(Value::real(kAt));
                }
                return point;
            };
            // The wheel and the right button's drags, totals (D469).
            if (kClicked.has_value()) {
                made.add("wheel", Value::real(kClicked->wheel));
                Value orbit = Value::array();
                orbit.push(Value::real(kClicked->orbit[0]));
                orbit.push(Value::real(kClicked->orbit[1]));
                made.add("orbit", std::move(orbit));
                // With Shift, freelook's (D472).
                Value look = Value::array();
                look.push(Value::real(kClicked->look[0]));
                look.push(Value::real(kClicked->look[1]));
                made.add("look", std::move(look));
                made.add("fly", Value::real(kClicked->fly));
            }
            // Where the pointer is, press or none (D468).
            if (kClicked.has_value() && kClicked->pointing) {
                made.add("pointOrigin", kPoint(kClicked->pointOrigin));
                made.add("pointToward", kPoint(kClicked->pointToward));
            }
            if (kClicked.has_value() && kClicked->count != 0) {
                made.add("origin", kPoint(kClicked->origin));
                made.add("toward", kPoint(kClicked->toward));
                made.add("modifiers", Value::integer(kClicked->modifiers));
                if (kClicked->released != 0) {
                    made.add("released", Value::integer(static_cast<std::int64_t>(kClicked->released)));
                    made.add("releaseOrigin", kPoint(kClicked->releaseOrigin));
                    made.add("releaseToward", kPoint(kClicked->releaseToward));
                }
            }
            send(connection, client, replyLine(id, "answer", std::move(made)));
            return;
        }
        if (kind != nullptr && *kind == "tooling.pick") {
            // What a ray from an author's view meets, and where it was
            // authored (D456): reading, so the inspect grant's.
            if (!settings.grants.inspect) {
                send(connection, client, errorLine(id, ToolingError::NotGranted, "picking needs the inspect grant"));
                return;
            }
            if (settings.picking == nullptr) {
                send(connection, client, errorLine(id, ToolingError::NotFound, "this Runtime has no bodies to pick"));
                return;
            }
            const auto kOrigin = pointOf(parsed->find("origin"));
            const auto kToward = pointOf(parsed->find("toward"));
            if (!kOrigin.has_value() || !kToward.has_value() || parsed->names().size() != 4) {
                send(connection,
                     client,
                     errorLine(id,
                               ToolingError::Malformed,
                               "a pick is an origin and a toward of three numbers within a million metres"));
                return;
            }
            const std::optional<world_runtime::Picked> kPicked = settings.picking->pick(*kOrigin, *kToward);
            Value made = Value::object();
            made.add("kind", Value::string("tooling.picked"));
            made.add("entity", kPicked.has_value() ? Value::string(entityName(kPicked->entity)) : Value{});
            if (kPicked.has_value()) {
                Value point = Value::array();
                for (const double kAt : kPicked->point) {
                    point.push(Value::real(kAt));
                }
                made.add("point", std::move(point));
                made.add("scene", kPicked->scene.empty() ? Value{} : Value::string(kPicked->scene));
                made.add("source",
                         kPicked->scene.empty()
                             ? Value{}
                             : Value::string(std::string{schema::formatStableIdText(kPicked->source).data(), 36}));
            }
            send(connection, client, replyLine(id, "answer", std::move(made)));
            return;
        }
        if (kind != nullptr && *kind == "tooling.end") {
            Value made = Value::object();
            made.add("kind", Value::string("tooling.ended"));
            send(connection, client, replyLine(id, "answer", std::move(made)));
            client.closing = true;
            return;
        }
        send(connection, client, errorLine(id, ToolingError::Unsupported, "this endpoint has no such verb"));
    }

    /// `debug.break` (`functions`, names, and `lines`, each a `file` and a
    /// `line`, D630), `debug.status`, `debug.continue`, and `debug.step`
    /// (`how`: over, into, or out, D632), under the debug grant (D460).
    void debug(
        network::ConnectionId connection, Client& client, const Value& id, std::string_view kind, const Value& record) {
        if (!settings.grants.debug || settings.debugging == nullptr) {
            send(connection, client, errorLine(id, ToolingError::NotGranted, "debugging needs the debug grant"));
            return;
        }
        world_runtime::Debugging& debugging = *settings.debugging;
        Value made = Value::object();
        if (kind == "debug.break") {
            const Value* kFunctions = record.find("functions");
            std::vector<std::string> names;
            bool named = kFunctions != nullptr && kFunctions->kind() == Value::Kind::Array &&
                         kFunctions->items().size() <= kMostBreakpoints;
            for (const Value& each : named ? kFunctions->items() : std::span<const Value>{}) {
                named = named && each.kind() == Value::Kind::String && !each.text()->empty() &&
                        each.text()->size() <= kMostFunctionName;
                if (named) {
                    names.push_back(*each.text());
                }
            }
            if (!named) {
                send(connection,
                     client,
                     errorLine(id, ToolingError::Malformed, "functions is a list of up to 64 function names"));
                return;
            }
            const std::optional<std::vector<world_runtime::DebugLine>> kLines = linesOf(record.find("lines"));
            if (!kLines.has_value()) {
                send(connection,
                     client,
                     errorLine(
                         id, ToolingError::Malformed, "lines is a list of up to 64 of a file and a line from one"));
                return;
            }
            const world_runtime::DebugBroken kBroken = debugging.breakAt(std::move(names), *kLines);
            Value placed = Value::array();
            for (const bool kPlaced : kBroken.lines) {
                placed.push(Value::boolean(kPlaced));
            }
            made.add("kind", Value::string("debug.broken"));
            made.add("found", Value::integer(static_cast<std::int64_t>(kBroken.functions)));
            made.add("lines", std::move(placed));
        } else if (kind == "debug.status") {
            made.add("kind", Value::string("debug.status"));
            made.add("stopped", Value::boolean(debugging.stopped()));
            made.add("stops", Value::integer(static_cast<std::int64_t>(debugging.stops())));
            Value frames = Value::array();
            for (const world_runtime::DebugFrame& each : debugging.stack()) {
                Value locals = Value::array();
                for (const auto& [kName, kValue] : each.locals) {
                    Value local = Value::object();
                    local.add("name", Value::string(kName));
                    local.add("value", Value::string(kValue));
                    locals.push(std::move(local));
                }
                Value frame = Value::object();
                frame.add("function", Value::string(each.function));
                if (each.at.has_value()) {
                    frame.add("file", Value::string(each.at->file));
                    frame.add("line", Value::integer(each.at->line));
                }
                frame.add("locals", std::move(locals));
                frames.push(std::move(frame));
            }
            made.add("frames", std::move(frames));
        } else if (kind == "debug.continue") {
            if (!stopped) {
                send(connection, client, errorLine(id, ToolingError::Stopped, "the game is not stopped"));
                return;
            }
            carryOn = true;
            made.add("kind", Value::string("debug.continued"));
        } else if (kind == "debug.step") {
            const Value* const kHow = record.find("how");
            const std::string_view kSaid =
                kHow != nullptr && kHow->text() != nullptr ? std::string_view{*kHow->text()} : std::string_view{};
            const std::optional<world_runtime::DebugStep> kStep =
                kSaid == "over"   ? std::optional{world_runtime::DebugStep::Over}
                : kSaid == "into" ? std::optional{world_runtime::DebugStep::Into}
                : kSaid == "out"  ? std::optional{world_runtime::DebugStep::Out}
                                  : std::nullopt;
            if (!kStep.has_value()) {
                send(connection, client, errorLine(id, ToolingError::Malformed, "how is over, into, or out"));
                return;
            }
            if (!stopped || !debugging.step(*kStep)) {
                send(connection, client, errorLine(id, ToolingError::Stopped, "the game is not stopped"));
                return;
            }
            carryOn = true;
            made.add("kind", Value::string("debug.stepped"));
        } else {
            send(connection, client, errorLine(id, ToolingError::Unsupported, "this endpoint has no such verb"));
            return;
        }
        send(connection, client, replyLine(id, "answer", std::move(made)));
    }

    void bytes(network::ConnectionId connection,
               Client& client,
               const network::Event& event,
               const world::World* world,
               world::TickIndex tick) {
        // One stream a client: the first it opens carries every record.
        if (!client.stream.has_value()) {
            if (!event.stream.openedByConnector() || event.stream.unidirectional()) {
                client.closing = true;
                ++statistics.refused;
                return;
            }
            client.stream = event.stream;
        } else if (!(*client.stream == event.stream)) {
            refuse(connection, client, Value{}, ToolingError::Malformed, "a client speaks on one stream");
            return;
        }
        client.pending.append(reinterpret_cast<const char*>(event.bytes.data()), event.bytes.size());
        std::size_t start = 0;
        for (std::size_t end = client.pending.find('\n'); end != std::string::npos && !client.closing;
             end = client.pending.find('\n', start)) {
            answer(connection, client, std::string_view{client.pending}.substr(start, end - start), world, tick);
            start = end + 1;
        }
        client.pending.erase(0, start);
        if (!client.closing && client.pending.size() > settings.maximumRecord) {
            refuse(
                connection, client, Value{}, ToolingError::LimitExceeded, "a record is at most the endpoint's limit");
        }
    }
};

network::ProviderProfile toolingProfile(std::size_t clients) noexcept {
    return network::ProviderProfile{.application = network::kToolingApplication,
                                    .maximumConnections = clients,
                                    .maximumStreamsPerConnection = 1,
                                    .maximumStreamSend = std::size_t{4} << 20U,
                                    .maximumDatagram = 1,
                                    .maximumQueuedEvents = 256,
                                    .maximumQueuedBytes = std::size_t{4} << 20U};
}

result::Result<std::unique_ptr<ToolingServer>> ToolingServer::create(network::Provider& provider,
                                                                     ToolingSettings settings) {
    if (settings.token.size() < 32) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kToolingDomain,
                            code(ToolingError::Configuration),
                            "a tooling token is at least 32 bytes");
    }
    auto state = std::make_unique<State>();
    state->provider = &provider;
    state->settings = std::move(settings);
    return std::unique_ptr<ToolingServer>{new ToolingServer{std::move(state)}};
}

ToolingServer::ToolingServer(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

ToolingServer::~ToolingServer() = default;

bool ToolingServer::serveStopped(execution::MonotonicInstant now) {
    State& state = *state_;
    state.stopped = true;
    state.carryOn = false;
    serve(nullptr, state.lastTick, now);
    state.stopped = false;
    const bool kAnyAdmitted = std::ranges::any_of(state.clients, [](const auto& kEach) {
        return kEach.second.admitted && !kEach.second.closing;
    });
    return state.carryOn || !kAnyAdmitted;
}

void ToolingServer::serve(const world::World* world, world::TickIndex tick, execution::MonotonicInstant now) {
    State& state = *state_;
    if (!state.stopped) {
        state.lastTick = tick;
    }
    state.events.clear();
    state.provider->poll(state.events, 1024);
    for (const network::Event& event : state.events) {
        switch (event.kind) {
        case network::EventKind::Accepted:
            state.clients[event.connection.value] = Client{.since = now};
            ++state.statistics.accepted;
            break;
        case network::EventKind::StreamBytes: {
            const auto kFound = state.clients.find(event.connection.value);
            if (kFound != state.clients.end() && !kFound->second.closing) {
                state.bytes(event.connection, kFound->second, event, world, tick);
            }
            break;
        }
        case network::EventKind::Closed:
            state.clients.erase(event.connection.value);
            break;
        case network::EventKind::Connected:
        case network::EventKind::Datagram:
            break;
        }
    }
    for (auto at = state.clients.begin(); at != state.clients.end();) {
        Client& client = at->second;
        if (!client.admitted && !client.closing && now - client.since > state.settings.helloWithin) {
            state.refuse(network::ConnectionId{at->first},
                         client,
                         Value{},
                         ToolingError::Unauthenticated,
                         "no hello came in time");
        }
        if (client.closing && !client.closingSince.has_value()) {
            client.closingSince = now;
        }
        if (client.closing && (!client.stream.has_value() || now - *client.closingSince > kLastReplyWithin)) {
            state.provider->close(network::ConnectionId{at->first});
            at = state.clients.erase(at);
        } else {
            ++at;
        }
    }
}

ToolingServer::Statistics ToolingServer::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::world_tooling
