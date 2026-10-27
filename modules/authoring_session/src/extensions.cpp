#include "extensions.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/document/json.h"
#include "rawframe/kest/doors.h"
#include "rawframe/kest/machine.h"
#include "rawframe/kest_library/library.h"
#include "rawframe/schema/stable_id.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <memory>
#include <optional>
#include <random>

namespace rawframe::authoring_session {

namespace {

using document::Value;

/// The heap and fuel one extension's machine has: a command is small work
/// over a scene, never a game's tick.
constexpr kest::MachineLimits kLimits{.heapBytes = std::size_t{4} << 20U, .fuelPerCall = 10'000'000};

/// The most bytes of one extension's sources read.
constexpr std::size_t kMostSourceBytes = std::size_t{1} << 20U;

result::Error refused(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return result::fail(kind, authoring::kAuthoringDomain, code(error), why).error();
}

/// What one run of an extension is: in `extend()`, the commands it
/// names; in a command, the scene and the entities chosen to read, and
/// the operations it stages.
struct Run {
    bool extending = false;
    /// Filling a section: it shows rows, and stages nothing.
    bool showing = false;
    std::vector<ExtensionCommand> commands;
    std::vector<ExtensionSection> sections;
    std::vector<ExtensionCommand> modes;
    std::vector<ExtensionCommand> panels;
    std::vector<SectionRow> rows;
    const scene::Scene* scene = nullptr;
    std::span<const base::Bits128> chosen;
    std::optional<std::array<double, 3>> point;
    const authoring::ComponentCatalog* catalog = nullptr;
    std::vector<base::Bits128> made;
    Value operations = Value::array();
};

std::string entityText(base::Bits128 entity) {
    const auto kText = schema::formatStableIdText(entity);
    return std::string{kText.data(), kText.size()};
}

/// The catalog's component of the name the program gave, as an
/// operation names it; empty for none.
std::string componentOf(const Run& run, std::string_view name) {
    const authoring::ComponentSchema* kComponent = run.catalog->findNamed(name);
    return kComponent != nullptr ? entityText(kComponent->id.value) : std::string{};
}

/// Stages one operation, refusing the call in a section's run and past
/// the most one run stages.
bool stage(kest::DoorCall& call, Run& run, Value operation) {
    if (run.showing || run.extending) {
        call.fail("only a command stages changes");
        return false;
    }
    if (run.operations.items().size() >= kMostStaged) {
        call.fail("a run stages at most 4096 operations");
        return false;
    }
    run.operations.push(std::move(operation));
    return true;
}

/// The value a number field of `component` takes, in the form its kind
/// is written in an operation (`{"real": 1.5}`, `{"signed": 2}`); none
/// for a field it does not have or one that is not a number or a truth.
std::optional<Value> fieldValue(const authoring::ComponentSchema& component, std::string_view field, double value) {
    const auto kField = std::ranges::find(component.fields, field, &authoring::FieldSchema::name);
    if (kField == component.fields.end() || !std::isfinite(value)) {
        return std::nullopt;
    }
    Value made = Value::object();
    switch (kField->kind) {
    case authoring::FieldKind::Real:
        made.add("real", Value::real(value));
        return made;
    case authoring::FieldKind::Signed:
        made.add("signed", Value::integer(static_cast<std::int64_t>(std::llround(value))));
        return made;
    case authoring::FieldKind::Unsigned:
        if (value < 0) {
            return std::nullopt;
        }
        made.add("unsigned", Value::integer(static_cast<std::int64_t>(std::llround(value))));
        return made;
    case authoring::FieldKind::Truth:
        made.add("truth", Value::boolean(value != 0));
        return made;
    case authoring::FieldKind::Reference:
    case authoring::FieldKind::Case:
        break;
    }
    return std::nullopt;
}

/// Stages `entity`'s field `field` of the component the program names,
/// refusing the call for a component the game does not have or a field
/// it cannot take a number in.
void stageField(kest::DoorCall& call,
                Run& run,
                base::Bits128 entity,
                std::string_view component,
                std::string_view field,
                double value) {
    const authoring::ComponentSchema* kComponent = run.catalog->findNamed(component);
    const std::optional<Value> kValue =
        kComponent != nullptr ? fieldValue(*kComponent, field, value) : std::optional<Value>{};
    if (!kValue.has_value()) {
        call.fail("a component of the game and a number or truth field of it, given a finite number");
        return;
    }
    Value operation = Value::object();
    operation.add("operation", Value::string("scene.set_field"));
    operation.add("entity", Value::string(entityText(entity)));
    operation.add("component", Value::string(entityText(kComponent->id.value)));
    operation.add("field", Value::string(std::string{field}));
    operation.add("value", *kValue);
    static_cast<void>(stage(call, run, std::move(operation)));
}

void commandDoor(kest::DoorCall& call, void* context) noexcept {
    auto& run = *static_cast<Run*>(context);
    if (!run.extending) {
        call.fail("an extension names its commands only in extend()");
        return;
    }
    const std::string_view kName = call.text(0);
    const std::string_view kFunction = call.text(1);
    if (kName.empty() || kName.size() > 64 || kFunction.empty() || kFunction.size() > 64 ||
        run.commands.size() >= kMostCommands) {
        call.fail("a command has a name and a function of 1 to 64 characters, at most 32 of them");
        return;
    }
    run.commands.push_back(ExtensionCommand{.name = std::string{kName}, .function = std::string{kFunction}});
}

void sectionDoor(kest::DoorCall& call, void* context) noexcept {
    auto& run = *static_cast<Run*>(context);
    if (!run.extending) {
        call.fail("an extension names its sections only in extend()");
        return;
    }
    const std::string_view kComponent = call.text(0);
    const std::string_view kFunction = call.text(1);
    if (kComponent.empty() || kComponent.size() > 256 || kFunction.empty() || kFunction.size() > 64 ||
        run.sections.size() >= kMostCommands) {
        call.fail("a section names a component and a function of 1 to 64 characters, at most 32 of them");
        return;
    }
    run.sections.push_back(ExtensionSection{.component = std::string{kComponent}, .function = std::string{kFunction}});
}

void modeDoor(kest::DoorCall& call, void* context) noexcept {
    auto& run = *static_cast<Run*>(context);
    const std::string_view kName = call.text(0);
    const std::string_view kFunction = call.text(1);
    if (!run.extending || kName.empty() || kName.size() > 64 || kFunction.empty() || kFunction.size() > 64 ||
        run.modes.size() >= kMostCommands) {
        call.fail("in extend(), a mode has a name and a function of 1 to 64 characters, at most 32 of them");
        return;
    }
    run.modes.push_back(ExtensionCommand{.name = std::string{kName}, .function = std::string{kFunction}});
}

void panelDoor(kest::DoorCall& call, void* context) noexcept {
    auto& run = *static_cast<Run*>(context);
    const std::string_view kName = call.text(0);
    const std::string_view kFunction = call.text(1);
    if (!run.extending || kName.empty() || kName.size() > 64 || kFunction.empty() || kFunction.size() > 64 ||
        run.panels.size() >= kMostCommands) {
        call.fail("in extend(), a panel has a name and a function of 1 to 64 characters, at most 32 of them");
        return;
    }
    run.panels.push_back(ExtensionCommand{.name = std::string{kName}, .function = std::string{kFunction}});
}

void pointDoor(kest::DoorCall& call, void* context) noexcept {
    const auto& run = *static_cast<Run*>(context);
    const auto kAxis = static_cast<std::uint64_t>(call.integer(0));
    call.answerReal(run.point.has_value() && kAxis < 3 ? (*run.point)[kAxis] : 0);
}

void rowDoor(kest::DoorCall& call, void* context) noexcept {
    auto& run = *static_cast<Run*>(context);
    const std::string_view kLabel = call.text(0);
    if (!run.showing || kLabel.empty() || kLabel.size() > 64 || run.rows.size() >= kMostRows) {
        call.fail("a section shows at most 64 rows, each a label of 1 to 64 characters");
        return;
    }
    run.rows.push_back(SectionRow{.label = std::string{kLabel}, .value = call.real(1)});
}

void chosenDoor(kest::DoorCall& call, void* context) noexcept {
    call.answerInteger(static_cast<std::int64_t>(static_cast<Run*>(context)->chosen.size()));
}

/// The chosen entity at the call's first argument, or none.
const scene::SceneEntity* chosenEntity(kest::DoorCall& call, const Run& run) {
    const auto kIndex = static_cast<std::uint64_t>(call.integer(0));
    if (run.scene == nullptr || kIndex >= run.chosen.size()) {
        return nullptr;
    }
    const auto kFound = std::ranges::find(run.scene->entities, run.chosen[kIndex], &scene::SceneEntity::id);
    return kFound != run.scene->entities.end() ? &*kFound : nullptr;
}

void numberDoor(kest::DoorCall& call, void* context) noexcept {
    const auto& run = *static_cast<Run*>(context);
    const scene::SceneEntity* kEntity = chosenEntity(call, run);
    double answer = 0;
    if (kEntity != nullptr) {
        const auto kComponent = std::ranges::find(kEntity->components, call.text(1), &scene::SceneComponent::name);
        if (kComponent != kEntity->components.end()) {
            const auto kField = std::ranges::find(kComponent->fields, call.text(2), &scene::SceneField::name);
            if (kField != kComponent->fields.end() && kField->value.kind == scene::FieldValue::Kind::Number) {
                const std::string& kText = kField->value.number;
                static_cast<void>(std::from_chars(kText.data(), kText.data() + kText.size(), answer));
            }
        }
    }
    call.answerReal(answer);
}

void setNumberDoor(kest::DoorCall& call, void* context) noexcept {
    auto& run = *static_cast<Run*>(context);
    const scene::SceneEntity* kEntity = chosenEntity(call, run);
    if (kEntity == nullptr) {
        call.fail("set_number names a chosen entity");
        return;
    }
    stageField(call, run, kEntity->id, call.text(1), call.text(2), call.real(3));
}

void makeDoor(kest::DoorCall& call, void* context) noexcept {
    auto& run = *static_cast<Run*>(context);
    const std::string_view kName = call.text(0);
    if (kName.size() > 256) {
        call.fail("an entity's name is at most 256 characters");
        return;
    }
    std::random_device device;
    const auto kWord = [&device] {
        return (std::uint64_t{device()} << 32U) | std::uint64_t{device()};
    };
    const base::Bits128 kEntity{.high = kWord(), .low = kWord()};
    Value operation = Value::object();
    operation.add("operation", Value::string("scene.create_entity"));
    operation.add("entity", Value::string(entityText(kEntity)));
    operation.add("name", Value::string(std::string{kName}));
    if (stage(call, run, std::move(operation))) {
        run.made.push_back(kEntity);
        call.answerInteger(static_cast<std::int64_t>(run.made.size() - 1));
    }
}

/// The entity this run made at the call's first argument, or none.
std::optional<base::Bits128> madeEntity(kest::DoorCall& call, const Run& run) {
    const auto kIndex = static_cast<std::uint64_t>(call.integer(0));
    return kIndex < run.made.size() ? std::optional{run.made[kIndex]} : std::nullopt;
}

void addDoor(kest::DoorCall& call, void* context) noexcept {
    auto& run = *static_cast<Run*>(context);
    const std::optional<base::Bits128> kEntity = madeEntity(call, run);
    const std::string kComponent = componentOf(run, call.text(1));
    if (!kEntity.has_value() || kComponent.empty()) {
        call.fail("add names an entity this run made and a component of the game");
        return;
    }
    Value operation = Value::object();
    operation.add("operation", Value::string("scene.add_component"));
    operation.add("entity", Value::string(entityText(*kEntity)));
    operation.add("component", Value::string(kComponent));
    static_cast<void>(stage(call, run, std::move(operation)));
}

void setMadeDoor(kest::DoorCall& call, void* context) noexcept {
    auto& run = *static_cast<Run*>(context);
    const std::optional<base::Bits128> kEntity = madeEntity(call, run);
    if (!kEntity.has_value()) {
        call.fail("set_made names an entity this run made");
        return;
    }
    stageField(call, run, *kEntity, call.text(1), call.text(2), call.real(3));
}

constexpr std::array<kest::Parameter, 2> kTwoTexts = {kest::Slot::Text, kest::Slot::Text};
constexpr std::array<kest::Parameter, 1> kU32 = {kest::Slot::U32};
constexpr std::array<kest::Parameter, 1> kF64 = {kest::Slot::F64};
constexpr std::array<kest::Parameter, 3> kIndexAndTwoTexts = {kest::Slot::U32, kest::Slot::Text, kest::Slot::Text};
constexpr std::array<kest::Parameter, 4> kSetTakes = {
    kest::Slot::U32, kest::Slot::Text, kest::Slot::Text, kest::Slot::F64};
constexpr std::array<kest::Parameter, 1> kText = {kest::Slot::Text};
constexpr std::array<kest::Parameter, 2> kIndexAndText = {kest::Slot::U32, kest::Slot::Text};
constexpr std::array<kest::Parameter, 2> kTextAndF64 = {kest::Slot::Text, kest::Slot::F64};

/// `rawframe.studio`'s doors, each over `run`; every one checks what it
/// is given before any effect, and stages only into the run.
result::Result<kest::DoorTable> doorsOver(Run& run) {
    kest::DoorTable doors;
    RAWFRAME_TRY(kest::addStandardMath(doors));
    const std::array kDoors = {
        kest::Door{.name = "Studio.command", .function = &commandDoor, .takes = kTwoTexts},
        kest::Door{.name = "Studio.section", .function = &sectionDoor, .takes = kTwoTexts},
        kest::Door{.name = "Studio.mode", .function = &modeDoor, .takes = kTwoTexts},
        kest::Door{.name = "Studio.panel", .function = &panelDoor, .takes = kTwoTexts},
        kest::Door{.name = "Edit.point", .function = &pointDoor, .takes = kU32, .gives = kF64},
        kest::Door{.name = "Show.row", .function = &rowDoor, .takes = kTextAndF64},
        kest::Door{.name = "Edit.chosen", .function = &chosenDoor, .gives = kU32},
        kest::Door{.name = "Edit.number", .function = &numberDoor, .takes = kIndexAndTwoTexts, .gives = kF64},
        kest::Door{.name = "Edit.set_number", .function = &setNumberDoor, .takes = kSetTakes},
        kest::Door{.name = "Edit.make", .function = &makeDoor, .takes = kText, .gives = kU32},
        kest::Door{.name = "Edit.add", .function = &addDoor, .takes = kIndexAndText},
        kest::Door{.name = "Edit.set_made", .function = &setMadeDoor, .takes = kSetTakes},
    };
    for (kest::Door door : kDoors) {
        door.context = &run;
        door.safeForUntrusted = true;
        RAWFRAME_TRY(doors.add(door));
    }
    return doors;
}

/// The `.kest` files directly under the game's extensions directory, by
/// their paths there, in name order.
std::vector<kest::SourceFile> sourcesOf(const std::filesystem::path& gameDirectory) {
    std::vector<kest::SourceFile> files;
    std::error_code error;
    const std::filesystem::path kDirectory = gameDirectory / "extensions";
    for (const auto& kEntry : std::filesystem::directory_iterator(kDirectory, error)) {
        if (files.size() >= kMostExtensions) {
            break;
        }
        if (!kEntry.is_regular_file(error) || kEntry.path().extension() != ".kest" ||
            kEntry.file_size(error) > kMostSourceBytes) {
            continue;
        }
        if (std::optional<std::string> text = readFile(kEntry.path()); text.has_value()) {
            files.push_back(kest::SourceFile{.path = kEntry.path().filename().string(), .text = std::move(*text)});
        }
    }
    std::ranges::sort(files, {}, &kest::SourceFile::path);
    return files;
}

/// The extension at `path` (`extensions/<file>.kest`) compiled and started
/// on its own machine over `run`'s doors, with its capabilities.
result::Result<std::unique_ptr<kest::Machine>> started(const std::vector<kest::SourceFile>& files,
                                                       std::string_view path,
                                                       const kest::DoorTable& doors,
                                                       std::vector<std::string>* capabilities) {
    constexpr std::string_view kDirectory = "extensions/";
    if (!path.starts_with(kDirectory) ||
        std::ranges::find(files, path.substr(kDirectory.size()), &kest::SourceFile::path) == files.end()) {
        return std::unexpected{refused(authoring::AuthoringError::TargetNotFound,
                                       result::ErrorClass::NotFound,
                                       "no extension of the game is at that path")};
    }
    std::string report;
    auto program = kest_library::compile(path.substr(kDirectory.size()), files, {}, &report);
    if (!program.has_value()) {
        return std::unexpected{refused(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "the extension does not compile: " + kest::firstDiagnostic(report))};
    }
    if (capabilities != nullptr) {
        *capabilities = (*program)->capabilitiesRequested();
    }
    auto machine = kest::Machine::start(*program, doors, kest::Trust::Untrusted, kLimits);
    if (!machine.has_value()) {
        return std::unexpected{refused(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "the extension does not start: " + std::string{machine.error().description()})};
    }
    return std::move(*machine);
}

/// Calls `function`, which takes nothing and answers nothing.
result::Status called(kest::Machine& machine, std::string_view function) {
    auto entry = machine.entry(function);
    if (!entry.has_value() || !machine.checkArguments(*entry, {}).has_value() ||
        !machine.checkAnswer(*entry, std::nullopt).has_value()) {
        return std::unexpected{refused(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "the extension's " + std::string{function} + " takes and answers nothing")};
    }
    std::vector<kest::Value> frame(std::max<std::size_t>(1, entry->frameSlots));
    const execution::TaskOutcome<void> kCalled = machine.call(*entry, frame);
    if (!kCalled.hasValue()) {
        // A door's refusal is the call's error; the machine's own, its
        // report.
        std::string why = machine.takeReport();
        if (why.empty()) {
            why = kCalled.isError() ? std::string{kCalled.error().description()} : std::string{"it was stopped"};
        }
        return std::unexpected{refused(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "the extension's " + std::string{function} + " was refused: " + why)};
    }
    return {};
}

/// What the extension at `path` names in `extend()`, run on a machine of
/// its own.
result::Result<Run> named(const std::vector<kest::SourceFile>& files, std::string_view path) {
    Run naming{.extending = true};
    RAWFRAME_TRY_ASSIGN(const kest::DoorTable kDoors, doorsOver(naming));
    RAWFRAME_TRY_ASSIGN(std::unique_ptr<kest::Machine> machine, started(files, path, kDoors, nullptr));
    RAWFRAME_TRY(called(*machine, "extend"));
    return naming;
}

/// The rows `function` of the extension at `path` shows, run on its own
/// machine with `chosen` and staging nothing: a section's or a panel's.
result::Result<std::vector<SectionRow>> shown(const std::vector<kest::SourceFile>& files,
                                              std::string_view path,
                                              const std::string& function,
                                              const scene::Scene& scene,
                                              std::span<const base::Bits128> chosen,
                                              const authoring::ComponentCatalog& catalog) {
    Run run{.showing = true, .scene = &scene, .chosen = chosen, .catalog = &catalog};
    RAWFRAME_TRY_ASSIGN(const kest::DoorTable kDoors, doorsOver(run));
    RAWFRAME_TRY_ASSIGN(std::unique_ptr<kest::Machine> machine, started(files, path, kDoors, nullptr));
    RAWFRAME_TRY(called(*machine, function));
    return std::move(run.rows);
}

} // namespace

std::vector<GameExtension> readExtensions(const std::filesystem::path& gameDirectory) {
    const std::vector<kest::SourceFile> kFiles = sourcesOf(gameDirectory);
    std::vector<GameExtension> extensions;
    for (const kest::SourceFile& kFile : kFiles) {
        GameExtension extension{.path = "extensions/" + kFile.path};
        Run run{.extending = true};
        auto doors = doorsOver(run);
        auto machine = doors.has_value() ? started(kFiles, extension.path, *doors, &extension.capabilities)
                                         : std::unexpected<result::Error>{doors.error().clone()};
        const auto kExtended =
            machine.has_value() ? called(**machine, "extend") : std::unexpected<result::Error>{machine.error().clone()};
        if (kExtended.has_value()) {
            extension.commands = std::move(run.commands);
            extension.sections = std::move(run.sections);
            extension.modes = std::move(run.modes);
            extension.panels = std::move(run.panels);
        } else {
            extension.refused = std::string{kExtended.error().description()};
        }
        extensions.push_back(std::move(extension));
    }
    return extensions;
}

result::Result<authoring::Request> runExtension(const std::filesystem::path& gameDirectory,
                                                std::string_view path,
                                                std::string_view command,
                                                const scene::Scene& scene,
                                                std::span<const base::Bits128> chosen,
                                                std::optional<std::array<double, 3>> point,
                                                const authoring::ComponentCatalog& catalog) {
    const std::vector<kest::SourceFile> kFiles = sourcesOf(gameDirectory);
    // Its commands and modes as it names them now, then the one asked for
    // run on a machine of its own, so nothing one run kept reaches another.
    RAWFRAME_TRY_ASSIGN(const Run kNamed, named(kFiles, path));
    const auto kCommand = std::ranges::find(kNamed.commands, command, &ExtensionCommand::name);
    const auto kMode = std::ranges::find(kNamed.modes, command, &ExtensionCommand::name);
    if (kCommand == kNamed.commands.end() && kMode == kNamed.modes.end()) {
        return std::unexpected{refused(authoring::AuthoringError::TargetNotFound,
                                       result::ErrorClass::NotFound,
                                       "the extension offers no command or mode of that name")};
    }
    // A mode runs where the author clicked.
    if (kCommand == kNamed.commands.end() && !point.has_value()) {
        return std::unexpected{refused(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "a mode runs at a point")};
    }
    Run run{.scene = &scene, .chosen = chosen, .point = point, .catalog = &catalog};
    RAWFRAME_TRY_ASSIGN(const kest::DoorTable kDoors, doorsOver(run));
    RAWFRAME_TRY_ASSIGN(std::unique_ptr<kest::Machine> machine, started(kFiles, path, kDoors, nullptr));
    RAWFRAME_TRY(called(*machine, kCommand != kNamed.commands.end() ? kCommand->function : kMode->function));
    if (run.operations.items().empty()) {
        return std::unexpected{refused(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "the command staged nothing")};
    }
    Value request = Value::object();
    request.add("formatVersion", Value::integer(1));
    request.add("kind", Value::string("authoring.request"));
    request.add("batch", Value::string("atomic"));
    request.add("operations", std::move(run.operations));
    return authoring::readRequest(document::writeCompact(request));
}

result::Result<std::vector<SectionRow>> runSection(const std::filesystem::path& gameDirectory,
                                                   std::string_view path,
                                                   std::string_view component,
                                                   const scene::Scene& scene,
                                                   base::Bits128 entity,
                                                   const authoring::ComponentCatalog& catalog) {
    const std::vector<kest::SourceFile> kFiles = sourcesOf(gameDirectory);
    RAWFRAME_TRY_ASSIGN(const Run kNamed, named(kFiles, path));
    const auto kSection = std::ranges::find(kNamed.sections, component, &ExtensionSection::component);
    if (kSection == kNamed.sections.end()) {
        return std::unexpected{refused(authoring::AuthoringError::TargetNotFound,
                                       result::ErrorClass::NotFound,
                                       "the extension offers no section of that component")};
    }
    const std::array<base::Bits128, 1> kChosen = {entity};
    return shown(kFiles, path, kSection->function, scene, kChosen, catalog);
}

result::Result<std::vector<SectionRow>> runPanel(const std::filesystem::path& gameDirectory,
                                                 std::string_view path,
                                                 std::string_view panel,
                                                 const scene::Scene& scene,
                                                 std::span<const base::Bits128> chosen,
                                                 const authoring::ComponentCatalog& catalog) {
    const std::vector<kest::SourceFile> kFiles = sourcesOf(gameDirectory);
    RAWFRAME_TRY_ASSIGN(const Run kNamed, named(kFiles, path));
    const auto kPanel = std::ranges::find(kNamed.panels, panel, &ExtensionCommand::name);
    if (kPanel == kNamed.panels.end()) {
        return std::unexpected{refused(authoring::AuthoringError::TargetNotFound,
                                       result::ErrorClass::NotFound,
                                       "the extension offers no panel of that name")};
    }
    return shown(kFiles, path, kPanel->function, scene, chosen, catalog);
}

} // namespace rawframe::authoring_session
