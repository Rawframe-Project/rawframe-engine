// Kest programs compiled from handed files, bound to typed doors, and run on
// machines whose every call has a fuel budget and a heap ceiling.

#include "rawframe/kest/errors.h"
#include "rawframe/kest/machine.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace rawframe;
using kest::Door;
using kest::DoorTable;
using kest::KestError;
using kest::Machine;
using kest::MachineLimits;
using kest::Program;
using kest::Slot;
using kest::SourceFile;
using kest::Trust;
using kest::Value;

namespace {

constexpr MachineLimits kLimits{.heapBytes = 1U << 20U, .fuelPerCall = 100'000};

std::shared_ptr<const Program> compile(std::string_view text) {
    const std::array<SourceFile, 1> kFiles = {SourceFile{.path = "test.kest", .text = std::string{text}}};
    auto program = Program::compile(kFiles, {});
    RAWFRAME_EXPECT(program.has_value());
    return program.has_value() ? *program : nullptr;
}

std::unique_ptr<Machine> start(std::string_view text, const DoorTable& doors = {}) {
    auto machine = Machine::start(compile(text), doors, Trust::Trusted, kLimits);
    RAWFRAME_EXPECT(machine.has_value());
    return machine.has_value() ? std::move(*machine) : nullptr;
}

template <typename T> bool refusedWith(const result::Result<T>& outcome, KestError error) {
    return !outcome.has_value() && outcome.error().code() == kest::code(error);
}

bool failedWith(const execution::TaskOutcome<void>& outcome, KestError error) {
    return outcome.isError() && outcome.error().code() == kest::code(error);
}

constexpr std::string_view kAdd = "module t\n"
                                  "\n"
                                  "fn add(a: i32, b: i32) -> i32 {\n"
                                  "    return a + b\n"
                                  "}\n"
                                  "\n"
                                  "fn sum(n: i32) -> i32 {\n"
                                  "    let total = 0\n"
                                  "    let i = 0\n"
                                  "    while i < n {\n"
                                  "        total = total + i\n"
                                  "        i = i + 1\n"
                                  "    }\n"
                                  "    return total\n"
                                  "}\n"
                                  "\n"
                                  "fn spin() -> i32 {\n"
                                  "    let turns = 0\n"
                                  "    while turns >= 0 {\n"
                                  "        turns = turns + 1\n"
                                  "        if turns > 1000000000 {\n"
                                  "            turns = 0\n"
                                  "        }\n"
                                  "    }\n"
                                  "    return turns\n"
                                  "}\n";

constexpr std::string_view kDoors = "module t\n"
                                    "\n"
                                    "extern fn Engine.scale(value: i32) -> i32 no.alloc\n"
                                    "extern fn Engine.named(name: text, extra: i32) -> i32 no.alloc\n"
                                    "extern fn Clock.tick()\n"
                                    "\n"
                                    "fn run(value: i32) -> i32 {\n"
                                    "    Clock.tick()\n"
                                    "    return Engine.scale(value) + Engine.named(\"four\", 1)\n"
                                    "}\n";

struct Doorway {
    std::int64_t scale = 10;
    std::int64_t ticks = 0;
    bool refuse = false;
};

void scale(kest::DoorCall& call, void* context) noexcept {
    Doorway& doorway = *static_cast<Doorway*>(context);
    if (doorway.refuse) {
        call.fail("the engine will not scale today");
        return;
    }
    call.spendFuel(64 * 1000);
    call.answerInteger(call.integer(0) * doorway.scale);
}

void named(kest::DoorCall& call, void*) noexcept {
    call.answerInteger(static_cast<std::int64_t>(call.text(0).size()) + call.integer(1));
}

void tick(kest::DoorCall&, void* context) noexcept {
    ++static_cast<Doorway*>(context)->ticks;
}

constexpr std::array<kest::Parameter, 1> kI32 = {Slot::I32};
constexpr std::array<kest::Parameter, 1> kI64 = {Slot::I64};
constexpr std::array<kest::Parameter, 2> kTextAndI32 = {Slot::Text, Slot::I32};

DoorTable table(Doorway& doorway, bool safe = true) {
    DoorTable doors;
    RAWFRAME_EXPECT(doors
                        .add(Door{.name = "Engine.scale",
                                  .function = &scale,
                                  .context = &doorway,
                                  .takes = kI32,
                                  .gives = kI32,
                                  .safeForUntrusted = safe})
                        .has_value());
    RAWFRAME_EXPECT(
        doors.add(Door{.name = "Engine.named", .function = &named, .takes = kTextAndI32, .gives = kI32}).has_value());
    RAWFRAME_EXPECT(doors.add(Door{.name = "Clock.tick", .function = &tick, .context = &doorway}).has_value());
    return doors;
}

} // namespace

RAWFRAME_TEST(AProgramCompilesAndAnswers) {
    auto machine = start(kAdd);
    auto add = machine->entry("add");
    RAWFRAME_EXPECT(add.has_value() && add->frameSlots == 2);
    std::array<Value, 2> frame{};
    frame[0].integer = 2;
    frame[1].integer = 40;
    RAWFRAME_EXPECT(machine->call(*add, frame).hasValue());
    RAWFRAME_EXPECT(frame[0].integer == 42);
}

RAWFRAME_TEST(WhatDoesNotCompileIsReported) {
    const std::array<SourceFile, 1> kFiles = {SourceFile{.path = "broken.kest", .text = "module t\nfn f( -> {\n"}};
    std::string report;
    const auto kProgram = Program::compile(kFiles, {}, &report);
    RAWFRAME_EXPECT(refusedWith(kProgram, KestError::DoesNotCompile));
    RAWFRAME_EXPECT(report.find("broken.kest") != std::string::npos);
    // The first diagnostic, on one line, rides on the error itself.
    RAWFRAME_EXPECT(kest::firstDiagnostic(report) == "broken.kest:2:7: expected identifier, found `->` [K0201]");
    const auto kContext = kProgram.has_value() ? std::span<const result::ContextField>{} : kProgram.error().context();
    RAWFRAME_EXPECT(!kContext.empty() && kContext.front().key == "diagnostic" &&
                    kContext.front().value == "broken.kest:2:7: expected identifier, found `->` [K0201]");
    // Not in that shape, its first line; nothing, nothing.
    RAWFRAME_EXPECT(kest::firstDiagnostic("the room ran out\nmore") == "the room ran out" &&
                    kest::firstDiagnostic("").empty());
    RAWFRAME_EXPECT(refusedWith(Program::compile({}, {}), KestError::DoesNotCompile));
}

RAWFRAME_TEST(TheStandardLibraryIsHandedLikeAnyFile) {
    const auto kRead = [](const std::string& path) {
        std::ifstream in{path};
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    };
    const std::string kLibrary = RAWFRAME_KEST_LIBRARY;
    const std::array<SourceFile, 3> kFiles = {
        SourceFile{.path = "t.kest",
                   .text = "module t\n\nimport std.math\n\nfn larger(a: i32, b: i32) -> i32 {\n"
                           "    return math.max(a, b)\n}\n"},
        SourceFile{.path = kLibrary + "std/math.kest", .text = kRead(kLibrary + "std/math.kest")},
        SourceFile{.path = kLibrary + "std/fdlibm.kest", .text = kRead(kLibrary + "std/fdlibm.kest")},
    };
    std::string report;
    auto program = Program::compile(kFiles, {.library = kLibrary}, &report);
    RAWFRAME_EXPECT(program.has_value());
    if (!program.has_value()) {
        return;
    }
    // std.math asks for its three doors whether or not the program reaches them.
    RAWFRAME_EXPECT(refusedWith(Machine::start(*program, {}, Trust::Trusted, kLimits), KestError::UnknownDoor));
    DoorTable doors;
    RAWFRAME_EXPECT(kest::addStandardMath(doors).has_value());
    auto machine = Machine::start(*program, doors, Trust::Trusted, kLimits);
    RAWFRAME_EXPECT(machine.has_value());
    if (!machine.has_value()) {
        return;
    }
    auto larger = (*machine)->entry("larger");
    std::array<Value, 2> frame{};
    frame[0].integer = 3;
    frame[1].integer = 9;
    RAWFRAME_EXPECT(larger.has_value() && (*machine)->call(*larger, frame).hasValue() && frame[0].integer == 9);
}

RAWFRAME_TEST(TheMathDoorsAreExact) {
    DoorTable doors;
    RAWFRAME_EXPECT(kest::addStandardMath(doors).has_value());
    RAWFRAME_EXPECT(!kest::addStandardMath(doors).has_value());
    auto machine = start("module t\n\nextern fn Math.sqrt(value: f64) -> f64 no.alloc deterministic\n"
                         "extern fn Math.floor(value: f64) -> f64 no.alloc deterministic\n\n"
                         "fn root(value: f64) -> f64 {\n    return Math.floor(Math.sqrt(value) * 10.0)\n}\n",
                         doors);
    auto root = machine->entry("root");
    std::array<Value, 1> frame{};
    frame[0].real = 2.0;
    RAWFRAME_EXPECT(machine->call(*root, frame).hasValue() && frame[0].real == 14.0);
}

RAWFRAME_TEST(DoorsCrossBothWaysAndChargeFuel) {
    const auto kProgram = compile(kDoors);
    RAWFRAME_EXPECT(
        (kProgram->doorsRequested() == std::vector<std::string>{"Clock.tick", "Engine.scale", "Engine.named"}));
    RAWFRAME_EXPECT((kProgram->capabilitiesRequested() == std::vector<std::string>{"Clock", "Engine"}));
    Doorway doorway;
    auto machine = Machine::start(kProgram, table(doorway), Trust::Trusted, kLimits);
    RAWFRAME_EXPECT(machine.has_value());
    auto run = (*machine)->entry("run");
    std::array<Value, 1> frame{};
    frame[0].integer = 4;
    RAWFRAME_EXPECT((*machine)->call(*run, frame).hasValue());
    // 4 * 10 + len("four") + 1.
    RAWFRAME_EXPECT(frame[0].integer == 45);
    RAWFRAME_EXPECT(doorway.ticks == 1);
    // The door charged a unit for crossing and a thousand for its work.
    RAWFRAME_EXPECT((*machine)->fuelLeft() < kLimits.fuelPerCall - 1001);
}

RAWFRAME_TEST(ADoorThatFailsRefusesTheCallWithItsWords) {
    Doorway doorway;
    auto machine = start(kDoors, table(doorway));
    auto run = machine->entry("run");
    doorway.refuse = true;
    std::array<Value, 1> frame{};
    const auto kOutcome = machine->call(*run, frame);
    RAWFRAME_EXPECT(failedWith(kOutcome, KestError::ScriptFailed));
    RAWFRAME_EXPECT(kOutcome.isError() &&
                    kOutcome.error().description().find("will not scale today") != std::string_view::npos);
    // A refusal is not a broken machine.
    doorway.refuse = false;
    frame[0].integer = 1;
    RAWFRAME_EXPECT(machine->call(*run, frame).hasValue() && frame[0].integer == 15);
}

RAWFRAME_TEST(DoorsAreCheckedBeforeAnythingRuns) {
    const auto kProgram = compile(kDoors);
    Doorway doorway;
    DoorTable missing;
    RAWFRAME_EXPECT(refusedWith(Machine::start(kProgram, missing, Trust::Trusted, kLimits), KestError::UnknownDoor));

    DoorTable wrongShape = table(doorway);
    DoorTable wide;
    RAWFRAME_EXPECT(
        wide.add(Door{.name = "Engine.scale", .function = &scale, .context = &doorway, .takes = kI64, .gives = kI32})
            .has_value());
    RAWFRAME_EXPECT(
        wide.add(Door{.name = "Engine.named", .function = &named, .takes = kTextAndI32, .gives = kI32}).has_value());
    RAWFRAME_EXPECT(wide.add(Door{.name = "Clock.tick", .function = &tick, .context = &doorway}).has_value());
    RAWFRAME_EXPECT(refusedWith(Machine::start(kProgram, wide, Trust::Trusted, kLimits), KestError::DoorShapeMismatch));

    RAWFRAME_EXPECT(
        refusedWith(wrongShape.add(Door{.name = "Clock.tick", .function = &tick}), KestError::DuplicateDoor));
    constexpr std::array<kest::Parameter, 1> kText = {Slot::Text};
    RAWFRAME_EXPECT(refusedWith(wrongShape.add(Door{.name = "Text.out", .function = &tick, .gives = kText}),
                                KestError::DoorShapeMismatch));
}

RAWFRAME_TEST(UntrustedCodeRunsWithOnlyItsDoors) {
    Doorway doorway;
    // Every door it asks for must be marked safe.
    RAWFRAME_EXPECT(refusedWith(Machine::start(compile(kDoors), table(doorway, true), Trust::Untrusted, kLimits),
                                KestError::DoorNotForUntrusted));
    // One that asks only for safe doors runs, under its ceilings.
    constexpr std::string_view kScaled = "module t\n"
                                         "\n"
                                         "extern fn Engine.scale(value: i32) -> i32 no.alloc\n"
                                         "\n"
                                         "fn run(value: i32) -> i32 {\n"
                                         "    return Engine.scale(value)\n"
                                         "}\n";
    auto machine = Machine::start(compile(kScaled), table(doorway, true), Trust::Untrusted, kLimits);
    RAWFRAME_EXPECT(machine.has_value());
    if (!machine.has_value()) {
        return;
    }
    auto run = (*machine)->entry("run");
    std::array<Value, 1> frame{Value{.integer = 4}};
    RAWFRAME_EXPECT(run.has_value() && (*machine)->call(*run, frame).hasValue() && frame[0].integer == 40);
    auto spinning = Machine::start(compile(kAdd), {}, Trust::Untrusted, kLimits);
    RAWFRAME_EXPECT(spinning.has_value());
    if (spinning.has_value()) {
        auto spin = (*spinning)->entry("spin");
        std::array<Value, 1> empty{};
        RAWFRAME_EXPECT(spin.has_value() && failedWith((*spinning)->call(*spin, empty), KestError::FuelExhausted));
    }
}

RAWFRAME_TEST(EveryMachineHasFiniteLimits) {
    const auto kProgram = compile(kAdd);
    RAWFRAME_EXPECT(
        refusedWith(Machine::start(kProgram, {}, Trust::Trusted, {.fuelPerCall = 10}), KestError::MissingLimit));
    RAWFRAME_EXPECT(
        refusedWith(Machine::start(kProgram, {}, Trust::Trusted, {.heapBytes = 4096}), KestError::MissingLimit));
    RAWFRAME_EXPECT(refusedWith(Machine::start(nullptr, {}, Trust::Trusted, kLimits), KestError::MissingLimit));
}

RAWFRAME_TEST(FuelRunsOutAndIsRefilledForTheNextCall) {
    auto machine = start(kAdd);
    auto spin = machine->entry("spin");
    std::array<Value, 1> frame{};
    const auto kOutcome = machine->call(*spin, frame);
    RAWFRAME_EXPECT(failedWith(kOutcome, KestError::FuelExhausted));
    RAWFRAME_EXPECT(kOutcome.isError() && kOutcome.error().errorClass() == result::ErrorClass::ResourceExhausted);
    auto add = machine->entry("add");
    std::array<Value, 2> sum{};
    sum[0].integer = 1;
    sum[1].integer = 2;
    RAWFRAME_EXPECT(machine->call(*add, sum).hasValue() && sum[0].integer == 3);
}

RAWFRAME_TEST(EntriesAndFramesAreChecked) {
    auto machine = start(kAdd);
    RAWFRAME_EXPECT(refusedWith(machine->entry("absent"), KestError::UnknownEntry));
    auto add = machine->entry("add");
    std::array<Value, 1> narrow{};
    RAWFRAME_EXPECT(failedWith(machine->call(*add, narrow), KestError::FrameTooSmall));
    RAWFRAME_EXPECT(failedWith(machine->call(kest::Entry{}, narrow), KestError::FrameTooSmall));
}

RAWFRAME_TEST(CancellationIsNotAnError) {
    auto machine = start(kAdd);
    auto add = machine->entry("add");
    machine->cancel(execution::CancelReason::OwnerStopping);
    std::array<Value, 2> frame{};
    const auto kOutcome = machine->call(*add, frame);
    RAWFRAME_EXPECT(kOutcome.isCancelled() && kOutcome.cancelReason() == execution::CancelReason::OwnerStopping);
}

RAWFRAME_TEST(ARunningMachineStopsWhenCancelledFromAnotherThread) {
    const auto kProgram = compile(kAdd);
    auto machine = Machine::start(kProgram, {}, Trust::Trusted, {.heapBytes = 1U << 20U, .fuelPerCall = ~0ULL >> 1U});
    RAWFRAME_EXPECT(machine.has_value());
    auto spin = (*machine)->entry("spin");
    std::atomic<bool> running{false};
    std::thread canceller{[&] {
        while (!running.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        (*machine)->cancel(execution::CancelReason::DeadlineReached);
    }};
    std::array<Value, 1> frame{};
    running.store(true, std::memory_order_release);
    const auto kOutcome = (*machine)->call(*spin, frame);
    canceller.join();
    RAWFRAME_EXPECT(kOutcome.isCancelled() && kOutcome.cancelReason() == execution::CancelReason::DeadlineReached);
}

namespace {

struct Mover {
    float x = 0;
    float y = 0;
};

constexpr std::string_view kMovers = "module t\n"
                                     "\n"
                                     "struct Mover {\n"
                                     "    x: f32\n"
                                     "    y: f32\n"
                                     "}\n"
                                     "\n"
                                     "fn push(count: i32, movers: [Mover], speeds: [f32]) -> i32 {\n"
                                     "    let i = 0\n"
                                     "    while i < count {\n"
                                     "        movers[i].x = movers[i].x + speeds[i]\n"
                                     "        i = i + 1\n"
                                     "    }\n"
                                     "    return count\n"
                                     "}\n";

} // namespace

RAWFRAME_TEST(EngineMemoryIsLentWithoutCopying) {
    const auto kProgram = compile(kMovers);
    const auto kLayout = kProgram->layout("Mover");
    RAWFRAME_EXPECT(kLayout.has_value() && kLayout->size == sizeof(Mover) && kLayout->alignment == alignof(Mover));
    RAWFRAME_EXPECT(kLayout.has_value() && kLayout->mark != 0);
    RAWFRAME_EXPECT(refusedWith(kProgram->layout("Absent"), KestError::UnknownType));
    RAWFRAME_EXPECT(kLayout.has_value() && kLayout->fields.size() == 2);
    if (kLayout.has_value() && kLayout->fields.size() == 2) {
        RAWFRAME_EXPECT(kLayout->fields[0].name == "x" && kLayout->fields[0].offset == 0);
        RAWFRAME_EXPECT(kLayout->fields[1].name == "y" && kLayout->fields[1].offset == 4);
        RAWFRAME_EXPECT(kLayout->fields[1].kind == kest::FieldKind::F32);
    }

    auto machine = Machine::start(kProgram, {}, Trust::Trusted, kLimits);
    RAWFRAME_EXPECT(machine.has_value());
    std::array<Mover, 3> movers = {Mover{1, 0}, Mover{2, 0}, Mover{3, 0}};
    std::array<float, 3> speeds = {10, 20, 30};
    auto lentMovers = (*machine)->lend(movers.data(), 3, "Mover", sizeof(Mover));
    auto lentSpeeds = (*machine)->lend(speeds.data(), 3, "f32", sizeof(float));
    RAWFRAME_EXPECT(lentMovers.has_value() && lentSpeeds.has_value());
    auto push = (*machine)->entry("push");
    std::array<Value, 3> frame{};
    frame[0].integer = 3;
    frame[1] = *lentMovers;
    frame[2] = *lentSpeeds;
    RAWFRAME_EXPECT((*machine)->call(*push, frame).hasValue() && frame[0].integer == 3);
    RAWFRAME_EXPECT(movers[0].x == 11 && movers[1].x == 22 && movers[2].x == 33);

    // Kest holds each lent array to the type its parameter names.
    frame[0].integer = 3;
    frame[1] = *lentSpeeds;
    frame[2] = *lentSpeeds;
    RAWFRAME_EXPECT(failedWith((*machine)->call(*push, frame), KestError::ScriptFailed));

    // And a handle past its lend is refused, not read.
    (*machine)->endLend(*lentMovers);
    (*machine)->endLend(*lentSpeeds);
    frame[0].integer = 3;
    frame[1] = *lentMovers;
    frame[2] = *lentSpeeds;
    RAWFRAME_EXPECT((*machine)->call(*push, frame).isError());
    RAWFRAME_EXPECT(movers[0].x == 11);

    RAWFRAME_EXPECT(refusedWith((*machine)->lend(movers.data(), 3, "Mover", 4), KestError::LendRefused));
    RAWFRAME_EXPECT(refusedWith((*machine)->lend(movers.data(), 3, "Absent", 8), KestError::LendRefused));
}

RAWFRAME_TEST(ContinuedCallsShareOneBudget) {
    auto machine = start(kAdd);
    auto sum = machine->entry("sum");
    std::array<Value, 1> frame{};
    frame[0].integer = 100;
    RAWFRAME_EXPECT(machine->call(*sum, frame).hasValue() && frame[0].integer == 4950);
    const std::uint64_t kOneCall = kLimits.fuelPerCall - machine->fuelLeft();
    RAWFRAME_EXPECT(kOneCall >= 100);
    frame[0].integer = 100;
    RAWFRAME_EXPECT(machine->call(*sum, frame, kest::Fuel::Continue).hasValue());
    RAWFRAME_EXPECT(machine->fuelLeft() == kLimits.fuelPerCall - (2 * kOneCall));
    frame[0].integer = 100;
    RAWFRAME_EXPECT(machine->call(*sum, frame).hasValue());
    RAWFRAME_EXPECT(machine->fuelLeft() == kLimits.fuelPerCall - kOneCall);
}

RAWFRAME_TEST(ProgramsCompileFromDisk) {
    const std::string kLibrary = RAWFRAME_KEST_LIBRARY;
    auto program = Program::compileFile(kLibrary + "std/math.kest", {.library = kLibrary});
    RAWFRAME_EXPECT(program.has_value());
    if (program.has_value()) {
        const auto kRequested = (*program)->doorsRequested();
        RAWFRAME_EXPECT(std::find(kRequested.begin(), kRequested.end(), "Math.sqrt") != kRequested.end());
    }
    std::string report;
    RAWFRAME_EXPECT(refusedWith(Program::compileFile(kLibrary + "absent.kest", {.library = kLibrary}, &report),
                                KestError::DoesNotCompile));
    RAWFRAME_EXPECT(report.find("absent.kest") != std::string::npos);
}

namespace {

struct Pair {
    std::int32_t a = 0;
    float b = 0;
    bool on = false;
};

constexpr std::string_view kPairs = "module t\n"
                                    "\n"
                                    "struct Pair {\n"
                                    "    a: i32\n"
                                    "    b: f32\n"
                                    "    on: bool\n"
                                    "}\n"
                                    "\n"
                                    "extern fn Engine.twice(pair: Pair, extra: i64) -> Pair no.alloc\n"
                                    "\n"
                                    "fn run(x: i32) -> f32 {\n"
                                    "    let doubled = Engine.twice(Pair(x, 2.5, false), 7)\n"
                                    "    if doubled.on {\n"
                                    "        return doubled.b + f32(doubled.a)\n"
                                    "    }\n"
                                    "    return 0.0\n"
                                    "}\n";

void twice(kest::DoorCall& call, void*) noexcept {
    Pair pair;
    if (!call.value(0, std::as_writable_bytes(std::span{&pair, 1}))) {
        call.fail("not a pair");
        return;
    }
    const std::int64_t kExtra = call.integer(1);
    pair.a = static_cast<std::int32_t>((pair.a * 2) + kExtra);
    pair.b *= 2;
    pair.on = !pair.on;
    if (!call.answerValue(std::as_bytes(std::span{&pair, 1}))) {
        call.fail("could not answer");
    }
}

constexpr std::array<kest::Parameter, 2> kPairAndI64 = {kest::Parameter{Slot::Value, "Pair"}, Slot::I64};
constexpr std::array<kest::Parameter, 1> kPair = {kest::Parameter{Slot::Value, "Pair"}};

} // namespace

RAWFRAME_TEST(StructsCrossDoorsByValue) {
    const auto kProgram = compile(kPairs);
    const auto kLayout = kProgram->layout("Pair");
    RAWFRAME_EXPECT(kLayout.has_value() && kLayout->size == sizeof(Pair));
    DoorTable doors;
    RAWFRAME_EXPECT(
        doors.add(Door{.name = "Engine.twice", .function = &twice, .takes = kPairAndI64, .gives = kPair}).has_value());
    auto machine = Machine::start(kProgram, doors, Trust::Trusted, kLimits);
    RAWFRAME_EXPECT(machine.has_value());
    if (!machine.has_value()) {
        return;
    }
    auto run = (*machine)->entry("run");
    std::array<Value, 1> frame{};
    frame[0].integer = 5;
    RAWFRAME_EXPECT((*machine)->call(*run, frame).hasValue());
    // (5 * 2 + 7) + 2.5 * 2.
    RAWFRAME_EXPECT(frame[0].real == 22.0);
    // Negative numbers keep their sign across the crossing both ways.
    frame[0].integer = -5;
    RAWFRAME_EXPECT((*machine)->call(*run, frame).hasValue());
    RAWFRAME_EXPECT(frame[0].real == 2.0);
}

RAWFRAME_TEST(ValueDoorsNameTheirType) {
    const auto kProgram = compile(kPairs);
    constexpr std::array<kest::Parameter, 2> kWrongType = {kest::Parameter{Slot::Value, "Mover"}, Slot::I64};
    DoorTable wrong;
    RAWFRAME_EXPECT(
        wrong.add(Door{.name = "Engine.twice", .function = &twice, .takes = kWrongType, .gives = kPair}).has_value());
    RAWFRAME_EXPECT(
        refusedWith(Machine::start(kProgram, wrong, Trust::Trusted, kLimits), KestError::DoorShapeMismatch));
    constexpr std::array<kest::Parameter, 1> kUntyped = {Slot::Value};
    DoorTable untyped;
    RAWFRAME_EXPECT(refusedWith(untyped.add(Door{.name = "Engine.twice", .function = &twice, .takes = kUntyped}),
                                KestError::DoorShapeMismatch));
}
