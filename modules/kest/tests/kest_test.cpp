// Kest programs compiled from handed files, bound to typed doors, and run on
// machines whose every call has a fuel budget and a heap ceiling.

#include "rawframe/kest/errors.h"
#include "rawframe/kest/machine.h"
#include "rawframe/test/test.h"

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

constexpr std::array<Slot, 1> kI32 = {Slot::I32};
constexpr std::array<Slot, 1> kI64 = {Slot::I64};
constexpr std::array<Slot, 2> kTextAndI32 = {Slot::Text, Slot::I32};

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
    constexpr std::array<Slot, 1> kText = {Slot::Text};
    RAWFRAME_EXPECT(refusedWith(wrongShape.add(Door{.name = "Text.out", .function = &tick, .gives = kText}),
                                KestError::DoorShapeMismatch));
}

RAWFRAME_TEST(UntrustedCodeWaitsForItsProfile) {
    const auto kProgram = compile(kDoors);
    Doorway doorway;
    RAWFRAME_EXPECT(refusedWith(Machine::start(kProgram, table(doorway, false), Trust::Untrusted, kLimits),
                                KestError::DoorNotForUntrusted));
    RAWFRAME_EXPECT(
        refusedWith(Machine::start(compile(kAdd), {}, Trust::Untrusted, kLimits), KestError::UntrustedNotYetSupported));
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
