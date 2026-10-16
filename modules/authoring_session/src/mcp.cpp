#include "rawframe/authoring_session/mcp.h"

#include "rawframe/authoring/operations.h"
#include "rawframe/authoring_session/session.h"
#include "rawframe/document/json.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace rawframe::authoring_session {

namespace {

using document::Value;

/// The protocol revisions the bridge speaks, newest first. A client asking
/// for another is answered with the newest, as MCP's version negotiation
/// says, and decides itself whether to go on.
constexpr std::array<std::string_view, 3> kProtocolVersions = {"2025-06-18", "2025-03-26", "2024-11-05"};

// JSON-RPC's error codes.
constexpr std::int64_t kParseError = -32700;
constexpr std::int64_t kInvalidRequest = -32600;
constexpr std::int64_t kMethodNotFound = -32601;
constexpr std::int64_t kInvalidParams = -32602;

/// What the bridge tells an agent once, at initialization: how the tools
/// fit together, which the tool list cannot say by itself.
constexpr std::string_view kInstructions =
    "Rawframe's authoring session over one game. A game is made without Studio: its documents are "
    "text files under the game's directory (scenes .scene, UI .ui, style sheets .styles, materials "
    ".rfmaterial), which you may read directly. Change them through these tools so each change is "
    "validated, written, and kept in the document's undo history. Call game_describe first: it lists "
    "the game's components and their fields. A new entity needs a new random UUID (version 4). Field "
    "values are typed objects with one member: {\"real\": 1.5}, {\"unsigned\": 2}, {\"signed\": -3}, "
    "{\"truth\": true}, {\"case\": \"Name\"} for an enum, or null to unset. Every changing tool takes "
    "dryRun to see the outcome without keeping it. document_apply runs several operations as one "
    "atomic change.";

/// What each input type of the operation surface is, for the agent.
struct InputType {
    std::string_view name;
    std::string_view json;
    bool nullable;
    std::string_view description;
};

constexpr std::array<InputType, 15> kInputTypes = {
    InputType{"entity", "string", false, "An entity's SourceEntityId (UUID); a new random UUID v4 for a new one."},
    InputType{"optional_entity", "string", true, "An entity's SourceEntityId (UUID), or null for none."},
    InputType{"component", "string", false, "A component's type id (UUID) or name, as game_describe lists."},
    InputType{"optional_component", "string", true, "A component's type id or name; left out or null for any."},
    InputType{"field", "string", false, "A field's name, as game_describe lists under its component."},
    InputType{"value",
              "object",
              true,
              "A typed field value, one member naming its kind: {\"real\": 1.5}, {\"unsigned\": 2}, "
              "{\"signed\": -3}, {\"truth\": true}, {\"case\": \"Name\"}; null to unset."},
    InputType{"document_value", "", true, "A JSON value written into the document as it is."},
    InputType{"place", "integer", false, "A position among siblings, from 0."},
    InputType{"optional_place", "integer", true, "A position among siblings, from 0; left out or null for the end."},
    InputType{"text", "string", false, "Text."},
    InputType{"optional_text", "string", true, "Text; left out or null for none."},
    InputType{"style", "string", false, "A style class's name."},
    InputType{"graph_node", "string", false, "A material graph node's id, 16 hexadecimal digits."},
    InputType{"identity", "string", false, "An identity (UUID) for the new instance; a new random UUID v4."},
    InputType{"resource", "string", false, "A resource id, 32 hexadecimal digits, as game_assets gives."}};

const InputType* inputTypeOf(std::string_view name) {
    // An iterator, not a pointer, on every standard library (MSVC's).
    const auto kFound = std::ranges::find(kInputTypes, name, &InputType::name);
    return kFound == kInputTypes.end() ? nullptr : &*kFound;
}

Value text(std::string_view said) {
    return Value::string(std::string{said});
}

Value property(std::string_view json, bool nullable, std::string_view description) {
    Value made = Value::object();
    if (!json.empty()) {
        if (nullable) {
            Value types = Value::array();
            types.push(text(json));
            types.push(text("null"));
            made.add("type", std::move(types));
        } else {
            made.add("type", text(json));
        }
        if (json == "integer") {
            made.add("minimum", Value::integer(0));
        }
    }
    made.add("description", text(description));
    return made;
}

Value documentProperty() {
    return property("string",
                    false,
                    "The document's path under the game's directory, such as level.scene, game.ui, "
                    "look.styles, or rock.rfmaterial.");
}

Value dryRunProperty() {
    return property("boolean", false, "Answer as the change would be kept, keeping nothing.");
}

/// A tool's input schema: its properties and the ones it needs.
Value schemaOf(Value properties, const std::vector<std::string>& required) {
    Value made = Value::object();
    made.add("type", text("object"));
    made.add("properties", std::move(properties));
    Value needed = Value::array();
    for (const std::string& kName : required) {
        needed.push(Value::string(kName));
    }
    made.add("required", std::move(needed));
    made.add("additionalProperties", Value::boolean(false));
    return made;
}

Value annotationsOf(bool readOnly, bool destructive) {
    Value made = Value::object();
    made.add("readOnlyHint", Value::boolean(readOnly));
    if (!readOnly) {
        made.add("destructiveHint", Value::boolean(destructive));
    }
    made.add("openWorldHint", Value::boolean(false));
    return made;
}

Value toolOf(std::string_view name, std::string_view description, Value schema, Value annotations) {
    Value made = Value::object();
    made.add("name", text(name));
    made.add("description", text(description));
    made.add("inputSchema", std::move(schema));
    made.add("annotations", std::move(annotations));
    return made;
}

/// An operation's tool name: its name with dots made underscores, which
/// every client accepts.
std::string toolNameOf(std::string_view operation) {
    std::string made{operation};
    std::ranges::replace(made, '.', '_');
    return made;
}

const std::string* textOf(const Value* value) {
    return value != nullptr ? value->text() : nullptr;
}

/// An operation the session describes, as a tool runs it.
struct OperationTool {
    std::string operation;
    bool readOnly = false;
    /// Its inputs' names and types, in the declaration's order.
    std::vector<std::pair<std::string, std::string>> inputs;
};

/// Whether an input of `type` may be left out, which is how the surface
/// says none (only an optional entity is said by null, and given always).
bool leftOutWhenNull(std::string_view type) {
    return type == "optional_place" || type == "optional_component" || type == "optional_text";
}

/// A JSON-RPC response carrying `result` to the request `id`.
std::string resultLine(const Value& id, Value result) {
    Value made = Value::object();
    made.add("jsonrpc", text("2.0"));
    made.add("id", id);
    made.add("result", std::move(result));
    return document::writeCompact(made) + "\n";
}

std::string errorLine(const Value& id, std::int64_t code, std::string_view message) {
    Value error = Value::object();
    error.add("code", Value::integer(code));
    error.add("message", text(message));
    Value made = Value::object();
    made.add("jsonrpc", text("2.0"));
    made.add("id", id);
    made.add("error", std::move(error));
    return document::writeCompact(made) + "\n";
}

std::string notificationLine(std::string_view method, Value params) {
    Value made = Value::object();
    made.add("jsonrpc", text("2.0"));
    made.add("method", text(method));
    made.add("params", std::move(params));
    return document::writeCompact(made) + "\n";
}

/// Whether a session's answer says something failed: the record, or any
/// slot of a request or a query in it.
bool failed(const Value& reply) {
    if (reply.find("error") != nullptr) {
        return true;
    }
    const Value* answer = reply.find("answer");
    if (answer == nullptr) {
        return false;
    }
    for (const std::string_view kSlots : {"results", "answers"}) {
        const Value* slots = answer->find(kSlots);
        if (slots == nullptr) {
            continue;
        }
        for (const Value& kSlot : slots->items()) {
            if (kSlot.find("error") != nullptr) {
                return true;
            }
        }
    }
    // A multi-document outcome holds an outcome per document.
    if (const Value* documents = answer->find("documents"); documents != nullptr) {
        for (const Value& kPart : documents->items()) {
            Value wrapped = Value::object();
            wrapped.add("answer", kPart);
            if (failed(wrapped)) {
                return true;
            }
        }
    }
    return false;
}

/// A tool call's result from the session's reply: its answer, error, or
/// cancellation as both text and structured content.
Value callResultOf(const Value& reply) {
    const Value* payload = reply.find("answer");
    if (payload == nullptr) {
        payload = reply.find("error");
    }
    if (payload == nullptr) {
        payload = reply.find("cancelled");
    }
    const Value kPayload = payload != nullptr ? *payload : reply;
    Value content = Value::array();
    Value said = Value::object();
    said.add("type", text("text"));
    said.add("text", Value::string(document::writeCompact(kPayload)));
    content.push(std::move(said));
    Value made = Value::object();
    made.add("content", std::move(content));
    if (kPayload.kind() == Value::Kind::Object) {
        made.add("structuredContent", kPayload);
    }
    made.add("isError", Value::boolean(failed(reply) || reply.find("cancelled") != nullptr));
    return made;
}

/// A call refused before reaching the session, as a failed tool result so
/// the agent reads why and can try again.
Value refusedResult(std::string_view why) {
    Value error = Value::object();
    error.add("code", text("invalid_arguments"));
    error.add("message", text(why));
    Value reply = Value::object();
    reply.add("error", std::move(error));
    return callResultOf(reply);
}

} // namespace

struct McpBridge::State {
    explicit State(Session& held) : session(held) {
    }

    Session& session;
    std::int64_t nextRecord = 1;
    bool ended = false;
    std::vector<Value> tools;
    std::map<std::string, OperationTool, std::less<>> operations;
    /// The game's components' type ids by name, from its catalog, so an
    /// agent may name a component as the catalog does.
    std::map<std::string, std::string, std::less<>> componentIds;

    /// `value` with a component named as the catalog names it given as
    /// its type id; anything else as it was, for the session to judge.
    Value componentOf(const Value& value) const {
        const std::string* name = value.text();
        if (name == nullptr) {
            return value;
        }
        const auto kFound = componentIds.find(*name);
        return kFound != componentIds.end() ? Value::string(kFound->second) : value;
    }

    /// One operation as the surface reads it: a component named by name
    /// given by id, and an optional input given as null left out.
    Value operationOf(const Value& given, const OperationTool* tool) const {
        Value made = Value::object();
        for (std::size_t index = 0; index < given.names().size(); ++index) {
            const std::string& kName = given.names()[index];
            const Value& kValue = given.items()[index];
            std::string_view type;
            if (tool != nullptr) {
                const auto kInput = std::ranges::find(tool->inputs, kName, &std::pair<std::string, std::string>::first);
                type = kInput != tool->inputs.end() ? std::string_view{kInput->second} : std::string_view{};
            }
            if (kValue.isNull() && (leftOutWhenNull(type) || (tool == nullptr && kName == "having"))) {
                continue;
            }
            made.add(kName, kName == "component" || kName == "having" ? componentOf(kValue) : kValue);
        }
        return made;
    }
    /// Calls whose operation answers later, by the session record's id: the
    /// request's id, and the progress token it gave, if any.
    struct Waiting {
        Value request;
        std::optional<Value> progressToken;
    };
    std::map<std::int64_t, Waiting> waiting;

    /// A record of `kind` with a new id, which `id` receives.
    Value record(std::string_view kind, std::int64_t& id) {
        id = nextRecord++;
        Value made = Value::object();
        made.add("kind", text(kind));
        made.add("id", Value::integer(id));
        return made;
    }

    /// The session's reply to `made`, read; none for a record answered
    /// later.
    std::optional<Value> ask(const Value& made) {
        const std::string kReply = session.answer(document::writeCompact(made), ended);
        if (kReply.empty()) {
            return std::nullopt;
        }
        auto parsed = document::parse(kReply);
        if (!parsed) {
            Value error = Value::object();
            error.add("message", text("the session's reply did not read"));
            Value reply = Value::object();
            reply.add("error", std::move(error));
            return reply;
        }
        return std::move(*parsed);
    }

    /// A line the session said: progress for a waiting call, or its reply.
    void route(std::string_view line, std::vector<std::string>& said);
    void addOperations(const Value& discovery);
    void addSessionTools();
    Value initialize(const Value* params) const;
    std::optional<Value> call(const Value& id, const Value* params);
    std::optional<Value> callOperation(const OperationTool& tool, const Value& arguments);
    std::optional<Value> callSession(const Value& id, std::string_view name, const Value& arguments, const Value* meta);
};

void McpBridge::State::addOperations(const Value& discovery) {
    const Value* listed = discovery.find("operations");
    if (listed == nullptr) {
        return;
    }
    for (const Value& kOperation : listed->items()) {
        const std::string* name = textOf(kOperation.find("name"));
        const std::string* history = textOf(kOperation.find("history"));
        const std::string* targets = textOf(kOperation.find("targets"));
        const Value* inputs = kOperation.find("inputs");
        if (name == nullptr || history == nullptr || inputs == nullptr) {
            continue;
        }
        OperationTool tool{.operation = *name, .readOnly = *history == "read_only"};
        Value properties = Value::object();
        properties.add("document", documentProperty());
        std::vector<std::string> required{"document"};
        for (const Value& kInput : inputs->items()) {
            const std::string* inputName = textOf(kInput.find("name"));
            const std::string* inputType = textOf(kInput.find("type"));
            if (inputName == nullptr || inputType == nullptr) {
                continue;
            }
            const InputType* kType = inputTypeOf(*inputType);
            properties.add(*inputName,
                           kType != nullptr ? property(kType->json, kType->nullable, kType->description)
                                            : property("", true, *inputType));
            if (kType == nullptr || !leftOutWhenNull(kType->name)) {
                required.push_back(*inputName);
            }
            tool.inputs.emplace_back(*inputName, *inputType);
        }
        if (!tool.readOnly) {
            properties.add("dryRun", dryRunProperty());
        }
        const bool kDestructive =
            name->find("destroy") != std::string::npos || name->find("remove") != std::string::npos;
        std::string description =
            "The authoring operation " + *name + " on " + (targets != nullptr ? *targets : std::string{"a document"}) +
            "; " +
            (tool.readOnly ? "it reads and changes nothing." : "it is one undoable change, written to the document.");
        tools.push_back(toolOf(toolNameOf(*name),
                               description,
                               schemaOf(std::move(properties), required),
                               annotationsOf(tool.readOnly, kDestructive)));
        operations.emplace(toolNameOf(*name), std::move(tool));
    }
}

void McpBridge::State::addSessionTools() {
    tools.push_back(toolOf("game_describe",
                           "The operation surface and the game's component catalog: every component's type id, "
                           "name, and fields with their kinds. Read it before changing a scene.",
                           schemaOf(Value::object(), {}),
                           annotationsOf(true, false)));
    tools.push_back(toolOf("game_assets",
                           "The assets the game declares: each one's kind, identity, and name.",
                           schemaOf(Value::object(), {}),
                           annotationsOf(true, false)));
    {
        Value properties = Value::object();
        properties.add("document",
                       property("string", false, "The new scene's path under the game's directory, ending in .scene."));
        tools.push_back(toolOf("document_create_scene",
                               "A new, empty scene and its sidecar, at a path that holds neither.",
                               schemaOf(std::move(properties), {"document"}),
                               annotationsOf(false, false)));
    }
    {
        Value properties = Value::object();
        properties.add("document", documentProperty());
        tools.push_back(toolOf("document_history",
                               "A document's undo history, oldest first, each entry summed up.",
                               schemaOf(std::move(properties), {"document"}),
                               annotationsOf(true, false)));
    }
    for (const std::string_view kStep : {"undo", "redo"}) {
        Value properties = Value::object();
        properties.add("document", documentProperty());
        properties.add(
            "expects",
            property("string", false, "The document's generation last seen (sha256:...); refused if it moved."));
        tools.push_back(toolOf(kStep == "undo" ? "document_undo" : "document_redo",
                               kStep == "undo" ? "Takes back the document's last change, written to its file."
                                               : "Makes again the change undo last took back, written to its file.",
                               schemaOf(std::move(properties), {"document"}),
                               annotationsOf(false, false)));
    }
    {
        Value operation = Value::object();
        operation.add("type", text("object"));
        operation.add("description",
                      text("One operation: {\"operation\": \"scene.set_field\", ...its inputs}, as game_describe "
                           "lists them."));
        Value items = Value::object();
        items.add("type", text("array"));
        items.add("items", std::move(operation));
        items.add("description", text("The operations, in order."));
        Value batch = property("string", false, "atomic (all kept or none, the default) or independent.");
        Value choices = Value::array();
        choices.push(text("atomic"));
        choices.push(text("independent"));
        batch.add("enum", std::move(choices));
        Value properties = Value::object();
        properties.add("document", documentProperty());
        properties.add("operations", std::move(items));
        properties.add("batch", std::move(batch));
        properties.add(
            "expects",
            property("string", false, "The document's generation last seen (sha256:...); refused if it moved."));
        properties.add("dryRun", dryRunProperty());
        tools.push_back(toolOf("document_apply",
                               "Several operations on one document as one change, one undo step.",
                               schemaOf(std::move(properties), {"document", "operations"}),
                               annotationsOf(false, true)));
    }
    {
        Value properties = Value::object();
        properties.add("output", property("string", false, "An absolute directory outside the game's to cook into."));
        properties.add("cache",
                       property("string", true, "An absolute directory whose earlier cook is reused, or null."));
        tools.push_back(toolOf("game_cook",
                               "Cooks the game's directory into content a server and a client load. Long-running: "
                               "the call answers when the cook ends, with progress if the caller asks for it.",
                               schemaOf(std::move(properties), {"output"}),
                               annotationsOf(false, false)));
    }
    {
        Value properties = Value::object();
        properties.add("source", property("string", false, "The absolute path of a file outside the game."));
        properties.add("as", property("string", false, "Its path under the game's directory, such as art/bark.png."));
        tools.push_back(toolOf("game_import",
                               "Copies a texture, mesh, sound, or font into the game's directory, gives it a "
                               "sidecar, and declares it in the game's description.",
                               schemaOf(std::move(properties), {"source", "as"}),
                               annotationsOf(false, false)));
    }
    // The played game paused, let run, and stepped (D806).
    {
        Value properties = Value::object();
        properties.add("scene", property("string", false, "The scene previewed while the game plays."));
        properties.add("paused",
                       property("boolean",
                                false,
                                "True to pause the game's World, false to let it run; leave out "
                                "to ask only."));
        tools.push_back(toolOf("game_run",
                               "Pause or resume the World of the game played for a previewed scene, on its server; "
                               "answers whether it is paused and its next tick.",
                               schemaOf(std::move(properties), {"scene"}),
                               annotationsOf(false, false)));
    }
    {
        Value properties = Value::object();
        properties.add("scene", property("string", false, "The scene previewed while the game plays."));
        properties.add("ticks", property("integer", false, "Ticks to run, 1 to 600."));
        tools.push_back(toolOf("game_step",
                               "Step the paused World of the played game some ticks, then hold it again.",
                               schemaOf(std::move(properties), {"scene", "ticks"}),
                               annotationsOf(false, false)));
    }
    // The played game's World as its server holds it (D808).
    {
        Value properties = Value::object();
        properties.add("scene", property("string", false, "The scene previewed while the game plays."));
        properties.add("after", property("string", false, "The last entity of the page before, as listed."));
        properties.add("component", property("string", false, "Only entities holding this component."));
        properties.add("limit", property("integer", false, "At most this many, 1 to 256; 64 by default."));
        tools.push_back(toolOf("game_entities",
                               "The played game's living entities on its server, in slot order, a page at a time, "
                               "each with its components' names.",
                               schemaOf(std::move(properties), {"scene"}),
                               annotationsOf(true, false)));
    }
    {
        Value properties = Value::object();
        properties.add("scene", property("string", false, "The scene previewed while the game plays."));
        properties.add("entity", property("string", false, "The entity as game_entities lists it."));
        tools.push_back(toolOf("game_entity",
                               "One living entity of the played game, its components field by field, as its "
                               "server holds them now.",
                               schemaOf(std::move(properties), {"scene", "entity"}),
                               annotationsOf(true, false)));
    }
    // The played game's server's traffic (D807).
    {
        Value properties = Value::object();
        properties.add("scene", property("string", false, "The scene previewed while the game plays."));
        tools.push_back(toolOf("game_traffic",
                               "The played game's server's replication totals so far and its connections now; ask "
                               "twice to measure rates.",
                               schemaOf(std::move(properties), {"scene"}),
                               annotationsOf(true, false)));
    }
    // What a source uses and what uses it, by the last cook (D803).
    {
        Value properties = Value::object();
        properties.add("asset", property("string", false, "The source's path under the game's directory."));
        tools.push_back(toolOf("asset_references",
                               "The sources a source read to cook, and those that read it, by the session's last "
                               "cook; cook first.",
                               schemaOf(std::move(properties), {"asset"}),
                               annotationsOf(true, false)));
    }
    // A texture's thumbnail from the session's last cook (D801).
    {
        Value properties = Value::object();
        properties.add("asset", property("string", false, "The texture's path under the game's directory."));
        properties.add("size", property("integer", false, "The most pixels on the longer side, 16 to 256."));
        tools.push_back(toolOf("asset_thumbnail",
                               "Where the session's last cook holds a texture, and which of its levels is the "
                               "first no larger than the size asked; cook first.",
                               schemaOf(std::move(properties), {"asset", "size"}),
                               annotationsOf(true, false)));
    }
    // A source's import settings, read and set (D800).
    {
        Value properties = Value::object();
        properties.add("asset", property("string", false, "The source's path under the game's directory."));
        tools.push_back(toolOf("asset_import_settings",
                               "A source's importer, its settings, and the settings the importer offers, each with "
                               "its type, fallback, and choices.",
                               schemaOf(std::move(properties), {"asset"}),
                               annotationsOf(true, false)));
    }
    {
        Value properties = Value::object();
        properties.add("asset", property("string", false, "The source's path under the game's directory."));
        properties.add("settings",
                       property("object", false, "The settings to change, each by its name; a fallback clears it."));
        tools.push_back(toolOf("asset_set_import_settings",
                               "Sets a source's import settings in its sidecar once its importer accepts them; the "
                               "next cook cooks it again.",
                               schemaOf(std::move(properties), {"asset", "settings"}),
                               annotationsOf(false, true)));
    }
}

Value McpBridge::State::initialize(const Value* params) const {
    std::string_view version = kProtocolVersions.front();
    if (const std::string* asked = params != nullptr ? textOf(params->find("protocolVersion")) : nullptr;
        asked != nullptr && std::ranges::contains(kProtocolVersions, std::string_view{*asked})) {
        version = *asked;
    }
    Value toolsCapability = Value::object();
    toolsCapability.add("listChanged", Value::boolean(false));
    Value capabilities = Value::object();
    capabilities.add("tools", std::move(toolsCapability));
    Value server = Value::object();
    server.add("name", text("rawframe-author"));
    server.add("version", Value::string(std::to_string(authoring::kSurfaceGeneration)));
    Value made = Value::object();
    made.add("protocolVersion", text(version));
    made.add("capabilities", std::move(capabilities));
    made.add("serverInfo", std::move(server));
    made.add("instructions", text(kInstructions));
    return made;
}

std::optional<Value> McpBridge::State::callOperation(const OperationTool& tool, const Value& arguments) {
    const std::string* scene = textOf(arguments.find("document"));
    if (scene == nullptr) {
        return refusedResult("document names the document to change or read");
    }
    Value operation = Value::object();
    operation.add("operation", Value::string(tool.operation));
    for (const std::string& kName : arguments.names()) {
        if (kName == "document" || kName == "dryRun") {
            continue;
        }
        if (!std::ranges::contains(tool.inputs, kName, &std::pair<std::string, std::string>::first)) {
            return refusedResult("the operation " + tool.operation + " has no input " + kName);
        }
        operation.add(kName, *arguments.find(kName));
    }
    operation = operationOf(operation, &tool);
    Value list = Value::array();
    list.push(std::move(operation));
    std::int64_t id = 0;
    Value made;
    if (tool.readOnly) {
        made = record("authoring.read", id);
        made.add("scene", Value::string(*scene));
        Value queries = Value::object();
        queries.add("formatVersion", Value::integer(1));
        queries.add("kind", text("authoring.query"));
        queries.add("queries", std::move(list));
        made.add("queries", std::move(queries));
    } else {
        made = record("authoring.apply", id);
        made.add("scene", Value::string(*scene));
        if (const Value* dry = arguments.find("dryRun"); dry != nullptr && dry->truth().value_or(false)) {
            made.add("dryRun", Value::boolean(true));
        }
        Value request = Value::object();
        request.add("formatVersion", Value::integer(1));
        request.add("kind", text("authoring.request"));
        request.add("batch", text("atomic"));
        request.add("operations", std::move(list));
        made.add("request", std::move(request));
    }
    const std::optional<Value> kReply = ask(made);
    return kReply.has_value() ? std::optional{callResultOf(*kReply)} : std::nullopt;
}

std::optional<Value>
McpBridge::State::callSession(const Value& id, std::string_view name, const Value& arguments, const Value* meta) {
    std::int64_t recordId = 0;
    Value made;
    const auto copy = [&arguments, &made](std::string_view from, std::string_view to) {
        if (const Value* given = arguments.find(from); given != nullptr) {
            made.add(std::string{to}, *given);
        }
    };
    if (name == "game_describe") {
        made = record("authoring.describe", recordId);
    } else if (name == "game_assets") {
        made = record("authoring.assets", recordId);
    } else if (name == "document_create_scene") {
        made = record("authoring.create_scene", recordId);
        copy("document", "scene");
    } else if (name == "document_history") {
        made = record("authoring.history", recordId);
        copy("document", "scene");
    } else if (name == "document_undo" || name == "document_redo") {
        made = record(name == "document_undo" ? "authoring.undo" : "authoring.redo", recordId);
        copy("document", "scene");
        copy("expects", "expects");
    } else if (name == "document_apply") {
        made = record("authoring.apply", recordId);
        copy("document", "scene");
        if (const Value* dry = arguments.find("dryRun"); dry != nullptr && dry->truth().value_or(false)) {
            made.add("dryRun", Value::boolean(true));
        }
        Value request = Value::object();
        request.add("formatVersion", Value::integer(1));
        request.add("kind", text("authoring.request"));
        const std::string* batch = textOf(arguments.find("batch"));
        request.add("batch", Value::string(batch != nullptr ? *batch : std::string{"atomic"}));
        if (const Value* expects = arguments.find("expects"); expects != nullptr) {
            request.add("expects", *expects);
        }
        Value listed = Value::array();
        if (const Value* given = arguments.find("operations"); given != nullptr) {
            for (const Value& kOperation : given->items()) {
                listed.push(kOperation.kind() == Value::Kind::Object ? operationOf(kOperation, nullptr) : kOperation);
            }
        }
        request.add("operations", std::move(listed));
        made.add("request", std::move(request));
    } else if (name == "game_cook") {
        made = record("authoring.cook", recordId);
        copy("output", "output");
        const Value* cache = arguments.find("cache");
        made.add("cache", cache != nullptr ? *cache : Value{});
    } else if (name == "game_import") {
        made = record("authoring.import", recordId);
        copy("source", "source");
        copy("as", "as");
    } else if (name == "game_run") {
        made = record("authoring.run", recordId);
        copy("scene", "scene");
        copy("paused", "paused");
    } else if (name == "game_entities") {
        made = record("authoring.live_entities", recordId);
        copy("scene", "scene");
        copy("after", "after");
        copy("component", "component");
        copy("limit", "limit");
    } else if (name == "game_entity") {
        made = record("authoring.live_entity", recordId);
        copy("scene", "scene");
        copy("entity", "entity");
    } else if (name == "game_traffic") {
        made = record("authoring.traffic", recordId);
        copy("scene", "scene");
    } else if (name == "game_step") {
        made = record("authoring.step", recordId);
        copy("scene", "scene");
        copy("ticks", "ticks");
    } else if (name == "asset_references") {
        made = record("authoring.asset_references", recordId);
        copy("asset", "asset");
    } else if (name == "asset_thumbnail") {
        made = record("authoring.thumbnail", recordId);
        copy("asset", "asset");
        copy("size", "size");
    } else if (name == "asset_import_settings" || name == "asset_set_import_settings") {
        made = record(name == "asset_import_settings" ? "authoring.import_settings" : "authoring.set_import_settings",
                      recordId);
        copy("asset", "asset");
        copy("settings", "settings");
    } else {
        return std::nullopt;
    }
    const std::optional<Value> kReply = ask(made);
    if (!kReply.has_value()) {
        Waiting kept{.request = id};
        if (const Value* token = meta != nullptr ? meta->find("progressToken") : nullptr; token != nullptr) {
            kept.progressToken = *token;
        }
        waiting.emplace(recordId, std::move(kept));
        return std::nullopt;
    }
    return callResultOf(*kReply);
}

std::optional<Value> McpBridge::State::call(const Value& id, const Value* params) {
    const std::string* name = params != nullptr ? textOf(params->find("name")) : nullptr;
    if (name == nullptr) {
        return std::nullopt;
    }
    const Value* given = params->find("arguments");
    const Value kArguments = given != nullptr && given->kind() == Value::Kind::Object ? *given : Value::object();
    if (const auto kFound = operations.find(*name); kFound != operations.end()) {
        return callOperation(kFound->second, kArguments);
    }
    return callSession(id, *name, kArguments, params->find("_meta"));
}

McpBridge::McpBridge(Session& session) : state_(std::make_unique<State>(session)) {
    std::int64_t id = 0;
    Value hello = state_->record("authoring.hello", id);
    hello.add("surfaceGeneration", Value::integer(authoring::kSurfaceGeneration));
    (void)state_->ask(hello);
    const std::optional<Value> kDescribed = state_->ask(state_->record("authoring.describe", id));
    if (kDescribed.has_value()) {
        if (const Value* discovery = kDescribed->find("answer"); discovery != nullptr) {
            state_->addOperations(*discovery);
            if (const Value* components = discovery->find("components"); components != nullptr) {
                for (const Value& kComponent : components->items()) {
                    const std::string* name = textOf(kComponent.find("name"));
                    const std::string* typeId = textOf(kComponent.find("id"));
                    if (name != nullptr && typeId != nullptr) {
                        state_->componentIds.emplace(*name, *typeId);
                    }
                }
            }
        }
    }
    state_->addSessionTools();
}

McpBridge::~McpBridge() = default;

std::string McpBridge::answer(std::string_view line) {
    auto parsed = document::parse(line);
    if (!parsed) {
        return errorLine(Value{}, kParseError, "the message is not JSON");
    }
    const Value& kMessage = *parsed;
    const Value* method = kMessage.find("method");
    const Value* id = kMessage.find("id");
    if (kMessage.kind() != Value::Kind::Object || method == nullptr || method->text() == nullptr) {
        // A response from the client, or something else: nothing the
        // bridge asked for, so nothing to answer.
        return id != nullptr && kMessage.kind() == Value::Kind::Object && kMessage.find("result") == nullptr &&
                       kMessage.find("error") == nullptr
                   ? errorLine(*id, kInvalidRequest, "a request names its method")
                   : std::string{};
    }
    const std::string& kMethod = *method->text();
    const Value* params = kMessage.find("params");
    if (id == nullptr) {
        // A notification. A cancelled call's running operation is asked to
        // stop; it still answers, cancelled.
        if (kMethod == "notifications/cancelled" && params != nullptr) {
            if (const Value* request = params->find("requestId"); request != nullptr) {
                for (const auto& [kRecord, kWaiting] : state_->waiting) {
                    if (document::writeCompact(kWaiting.request) == document::writeCompact(*request)) {
                        std::int64_t cancelId = 0;
                        Value cancel = state_->record("authoring.cancel", cancelId);
                        cancel.add("operation", Value::integer(kRecord));
                        (void)state_->ask(cancel);
                        break;
                    }
                }
            }
        }
        return {};
    }
    if (kMethod == "initialize") {
        return resultLine(*id, state_->initialize(params));
    }
    if (kMethod == "ping") {
        return resultLine(*id, Value::object());
    }
    if (kMethod == "tools/list") {
        Value list = Value::array();
        for (const Value& kTool : state_->tools) {
            list.push(kTool);
        }
        Value made = Value::object();
        made.add("tools", std::move(list));
        return resultLine(*id, std::move(made));
    }
    if (kMethod == "tools/call") {
        const std::string* name = params != nullptr ? textOf(params->find("name")) : nullptr;
        if (name == nullptr) {
            return errorLine(*id, kInvalidParams, "a call names its tool");
        }
        const bool kKnown =
            state_->operations.contains(*name) || std::ranges::any_of(state_->tools, [name](const Value& tool) {
                const std::string* toolName = textOf(tool.find("name"));
                return toolName != nullptr && *toolName == *name;
            });
        if (!kKnown) {
            return errorLine(*id, kInvalidParams, "no tool of that name");
        }
        const std::optional<Value> kResult = state_->call(*id, params);
        return kResult.has_value() ? resultLine(*id, *kResult) : std::string{};
    }
    return errorLine(*id, kMethodNotFound, "the bridge has no such method");
}

void McpBridge::State::route(std::string_view line, std::vector<std::string>& said) {
    auto parsed = document::parse(line);
    if (!parsed) {
        return;
    }
    const Value* recordId = parsed->find("id");
    const std::optional<std::int64_t> kRecord = recordId != nullptr ? recordId->integer() : std::nullopt;
    const auto kWaiting = kRecord.has_value() ? waiting.find(*kRecord) : waiting.end();
    if (kWaiting == waiting.end()) {
        return;
    }
    const std::string* kind = textOf(parsed->find("kind"));
    if (kind != nullptr && *kind == "authoring.progress") {
        if (kWaiting->second.progressToken.has_value()) {
            Value params = Value::object();
            params.add("progressToken", *kWaiting->second.progressToken);
            const Value* step = parsed->find("step");
            const Value* steps = parsed->find("steps");
            params.add("progress", step != nullptr ? *step : Value::integer(0));
            if (steps != nullptr) {
                params.add("total", *steps);
            }
            if (const std::string* source = textOf(parsed->find("source")); source != nullptr) {
                params.add("message", Value::string(*source));
            }
            said.push_back(notificationLine("notifications/progress", std::move(params)));
        }
        return;
    }
    said.push_back(resultLine(kWaiting->second.request, callResultOf(*parsed)));
    waiting.erase(kWaiting);
}

std::vector<std::string> McpBridge::poll() {
    std::vector<std::string> said;
    for (const std::string& kLine : state_->session.poll()) {
        state_->route(kLine, said);
    }
    return said;
}

std::vector<std::string> McpBridge::end() {
    std::vector<std::string> said = poll();
    std::int64_t id = 0;
    // The end's reply follows the last lines of an operation it stopped,
    // all in one answer.
    const std::string kReplies =
        state_->session.answer(document::writeCompact(state_->record("authoring.end", id)), state_->ended);
    std::string_view rest = kReplies;
    while (!rest.empty()) {
        const std::size_t kEnd = rest.find('\n');
        state_->route(rest.substr(0, kEnd), said);
        rest = kEnd == std::string_view::npos ? std::string_view{} : rest.substr(kEnd + 1);
    }
    return said;
}

} // namespace rawframe::authoring_session
