#include "rawframe/kest/machine.h"

#include "rawframe/kest/errors.h"
#include "state.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::kest {

namespace {

/// What one bound door needs at the crossing. Owned by the machine, so its
/// address is what Kest hands back as the context.
struct Binding {
    DoorFunction function = nullptr;
    void* context = nullptr;
    DoorCall::Shape shape;
    std::string name;
};

void crossing(KestValue* frame, KestRuntime* runtime, void* context) {
    const Binding& binding = *static_cast<const Binding*>(context);
    DoorCall call{reinterpret_cast<Value*>(frame), runtime, binding.shape};
    binding.function(call, binding.context);
}

std::uint8_t kindOf(Slot slot) noexcept {
    switch (slot) {
    case Slot::I32:
        return KEST_L_I32;
    case Slot::I64:
        return KEST_L_I64;
    case Slot::U32:
        return KEST_L_U32;
    case Slot::U64:
        return KEST_L_U64;
    case Slot::F32:
        return KEST_L_F32;
    case Slot::F64:
        return KEST_L_F64;
    case Slot::Bool:
        return KEST_L_BOOL;
    case Slot::Text:
        return KEST_L_TEXT;
    case Slot::Value:
        return KEST_L_NOTHING;
    }
    return KEST_L_NOTHING;
}

/// Whether a layout is one piece of `kind`.
bool isOne(const KestLayout* layout, std::uint8_t kind) noexcept {
    return layout != nullptr && layout->count == 1 && layout->pieces[0].kind == kind;
}

/// Whether what the program declared at one place of an extern is what the
/// door says it is. A value must be the very type the door names: same
/// program type, same shape mark, and made only of what crosses by value.
bool sameParameter(KestBuild* build, const KestLayout* layout, const Parameter& parameter) {
    if (layout == nullptr) {
        return false;
    }
    if (parameter.slot != Slot::Value) {
        return layout->count == 1 && layout->pieces[0].kind == kindOf(parameter.slot);
    }
    const std::string kType{parameter.type};
    const KestLayout* named = nullptr;
    return kest_build_layout(build, kType.c_str(), &named) == 1 && named != nullptr &&
           kest_layout_mark(named) == kest_layout_mark(layout) && crossesByValue(layout);
}

/// Checks the door against the extern at `at` and says where each argument
/// sits: a C function bound to a name is otherwise read with whatever frame
/// the program built.
bool shapeOf(KestBuild* build, std::uint32_t at, const Door& door, DoorCall::Shape& shape) {
    if (kest_extern_takes(build, at) != door.takes.size()) {
        return false;
    }
    std::uint32_t offset = 0;
    for (std::uint32_t which = 0; which < door.takes.size(); ++which) {
        const KestLayout* const kLayout = kest_extern_layout(build, at, which);
        if (!sameParameter(build, kLayout, door.takes[which])) {
            return false;
        }
        shape.offsets.push_back(offset);
        shape.takes.push_back(door.takes[which].slot == Slot::Value ? crossingOf(kLayout) : Crossing{});
        offset += kLayout->slots;
    }
    const KestLayout* const kGives = kest_extern_gives(build, at);
    if (door.gives.empty()) {
        return kGives == nullptr;
    }
    if (!sameParameter(build, kGives, door.gives[0])) {
        return false;
    }
    shape.gives = door.gives[0].slot == Slot::Value ? crossingOf(kGives) : Crossing{};
    return true;
}

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, KestError error, std::string_view description) {
    return result::fail(errorClass, kKestDomain, code(error), description);
}

/// Whether two paths name one file (D630): the same, or either the other's
/// end after a separator, a backslash read as `/`.
bool sameFile(std::string_view built, std::string_view asked) {
    std::string named{built};
    std::string given{asked};
    std::ranges::replace(named, '\\', '/');
    std::ranges::replace(given, '\\', '/');
    const auto kEndsWith = [](std::string_view longer, std::string_view shorter) {
        return longer.size() > shorter.size() && longer.ends_with(shorter) &&
               longer[longer.size() - shorter.size() - 1] == '/';
    };
    return !given.empty() && (named == given || kEndsWith(named, given) || kEndsWith(given, named));
}

/// The most bytes of a text local said (D631).
constexpr std::uint32_t kMostTextSaid = 200;

/// What a stopped frame's slot holds, as text: numbers and truths by value,
/// text by its bytes where Kest proves it one piece of text, and nothing
/// read as an address (D460, D631). Asked with the breakpoints out.
std::string saidOf(const KestRuntime* runtime,
                   std::uint32_t frame,
                   std::uint16_t slot,
                   std::uint8_t kind,
                   std::uint16_t slots,
                   KestValue value) {
    if (!kest_frame_written(runtime, frame, slot)) {
        return "(not written yet)";
    }
    if (kest_frame_at_address(runtime, frame, slot)) {
        return "(an element)";
    }
    if (kind == KEST_L_TEXT) {
        std::uint32_t length = 0;
        const char* const kBytes = kest_frame_text(runtime, frame, slot, &length);
        if (kBytes == nullptr) {
            return "(text, not one piece here)";
        }
        return "\"" + std::string{kBytes, std::min(length, kMostTextSaid)} + (length > kMostTextSaid ? "...\"" : "\"");
    }
    std::string text;
    if (kind == KEST_L_F32 || kind == KEST_L_F64) {
        std::array<char, 32> number{};
        std::snprintf(number.data(), number.size(), "%.17g", value.real);
        text = number.data();
    } else if (kind == KEST_L_BOOL) {
        text = value.integer != 0 ? "true" : "false";
    } else if (kind >= KEST_L_U8 && kind <= KEST_L_U64) {
        text = std::to_string(static_cast<std::uint64_t>(value.integer));
    } else if (kind <= KEST_L_I64 || kind == KEST_L_TAG || (kind >= KEST_L_FLAGS8 && kind <= KEST_L_FLAGS64)) {
        text = std::to_string(value.integer);
    } else {
        text = "(a value)";
    }
    // A wider value's other slots are its pieces, not read.
    if (slots > 1) {
        text += " (of " + std::to_string(slots) + " slots)";
    }
    return text;
}
} // namespace

struct Machine::State {
    std::shared_ptr<const Program> program;
    std::vector<Binding> bindings;
    KestRuntime* runtime = nullptr;
    std::uint64_t fuelPerCall = 0;
    std::atomic<bool> cancelRequested{false};
    std::atomic<execution::CancelReason> cancelReason{execution::CancelReason::Requested};

    std::function<void()> whenStopped;
    std::uint64_t stops = 0;
    /// Whether this machine wrote the program's breakpoints (D460).
    bool breaking = false;
    /// The machine's own breakpoints, written to carry on from a stop and
    /// taken out at the next: at the instruction after it, which steps past
    /// a breakpoint (D630), and at the first instruction of every line a
    /// step may stop at (D632).
    std::vector<Program::State::Written> temporary;
    /// A step asked for from the handler (D632): how, and where the machine
    /// stood, its body, line, and depth.
    struct Stepping {
        Step how = Step::Over;
        std::int32_t in = -1;
        std::uint32_t line = 0;
        std::uint32_t deep = 0;
    };
    std::optional<Stepping> stepping;

    [[nodiscard]] Program::State& built() const noexcept {
        return program->state();
    }

    /// The breakpoint the machine stands on, taken out so it can move.
    void takeOutWhereItStands() {
        const std::int64_t kAt = kest_stopped(runtime);
        const std::int32_t kIn = kest_stopped_in(runtime);
        for (Program::State::Written& one : built().written) {
            if (one.entry == kIn && static_cast<std::int64_t>(one.at) == kAt && !one.out) {
                built().put(runtime, one, false);
            }
        }
    }

    /// The machine's own breakpoints taken out, and every breakpoint asked
    /// for that was taken out to carry on written again.
    void putBack() {
        for (const Program::State::Written& one : temporary) {
            built().write(runtime, one, false);
        }
        temporary.clear();
        for (Program::State::Written& one : built().written) {
            if (one.out) {
                built().put(runtime, one, true);
            }
        }
    }

    /// Whether a breakpoint the program was asked for is at `at` in `entry`.
    [[nodiscard]] bool asked(std::int32_t entry, std::int64_t at) const {
        return std::ranges::any_of(built().written, [entry, at](const Program::State::Written& one) {
            return one.entry == entry && static_cast<std::int64_t>(one.at) == at;
        });
    }

    /// A breakpoint of the machine's own at `at` in `entry`, where none is.
    void writeTemporary(std::int32_t entry, std::int64_t at) {
        std::uint32_t count = 0;
        const std::uint8_t* const kCode = kest_code_of(runtime, entry, &count);
        if (kCode == nullptr || at < 0 || at >= static_cast<std::int64_t>(count) || asked(entry, at) ||
            std::ranges::any_of(
                temporary,
                [entry, at](const Program::State::Written& one) {
                    return one.entry == entry && static_cast<std::int64_t>(one.at) == at;
                })) {
            return;
        }
        temporary.push_back(Program::State::Written{
            .entry = entry, .at = static_cast<std::uint32_t>(at), .was = kCode[at], .out = false});
        built().write(runtime, temporary.back(), true);
    }

    /// Where the machine stands: its body, line, and depth (D632).
    [[nodiscard]] Stepping standing(Step how) const {
        const std::int32_t kIn = kest_stopped_in(runtime);
        showCompiled(true);
        const std::optional<SourceLine> kLine = lineOf(kIn, kest_stopped(runtime));
        showCompiled(false);
        return Stepping{
            .how = how, .in = kIn, .line = kLine.has_value() ? kLine->line : 0, .deep = kest_frames_deep(runtime)};
    }

    /// A stop: the machine's own breakpoints taken out and those asked for
    /// written again; whether anyone is told of it. A stop at a breakpoint
    /// asked for is told, and ends a step; one at the machine's own is told
    /// only where it ends the step asked for (D632), and is otherwise a step
    /// past a breakpoint or a move of a step not yet done (D630).
    bool arrive() {
        const std::int64_t kAt = kest_stopped(runtime);
        const std::int32_t kIn = kest_stopped_in(runtime);
        putBack();
        if (asked(kIn, kAt)) {
            stepping.reset();
            return true;
        }
        if (!stepping.has_value()) {
            return false;
        }
        const Stepping kNow = standing(stepping->how);
        bool done = false;
        if (stepping->how == Step::Out) {
            done = kNow.deep < stepping->deep;
        } else if (stepping->how == Step::Into || kNow.deep <= stepping->deep) {
            done = kNow.in != stepping->in || kNow.line != stepping->line || kNow.deep != stepping->deep;
        }
        if (done) {
            stepping.reset();
        }
        return done;
    }

    /// The first instruction of every line of `entry`, its body as compiled.
    [[nodiscard]] std::vector<std::uint32_t> lineStarts(std::int32_t entry) const {
        std::vector<std::uint32_t> starts;
        std::uint32_t count = 0;
        if (kest_code_of(runtime, entry, &count) == nullptr) {
            return starts;
        }
        std::optional<std::uint32_t> last;
        for (std::int64_t at = 0; at >= 0 && at < static_cast<std::int64_t>(count);) {
            const std::optional<SourceLine> kLine = lineOf(entry, at);
            const std::uint32_t kNumber = kLine.has_value() ? kLine->line : 0;
            if (!last.has_value() || *last != kNumber) {
                starts.push_back(static_cast<std::uint32_t>(at));
                last = kNumber;
            }
            const std::int64_t kNext = kest_next_at(runtime, entry, static_cast<std::uint32_t>(at));
            at = kNext > at ? kNext : -1;
        }
        return starts;
    }

    /// Carrying on from a stop: a breakpoint asked for where it stands taken
    /// out, and one of the machine's own written at the instruction after
    /// it, which writes it again as the machine stops there (D630); while a
    /// step is asked for, at the instruction after it whatever stands there,
    /// and at the first instruction of every line of each body the step may
    /// stop in: every body for one into, else those of the frames (D632).
    void carryOn() {
        const std::int64_t kAt = kest_stopped(runtime);
        const std::int32_t kIn = kest_stopped_in(runtime);
        std::vector<std::pair<std::int32_t, std::uint32_t>> starts;
        if (stepping.has_value()) {
            std::vector<std::int32_t> bodies;
            if (stepping->how == Step::Into) {
                for (std::int32_t index = 0; kest_entry_wrote(runtime, index) != nullptr; ++index) {
                    bodies.push_back(index);
                }
            } else {
                for (std::uint32_t deep = 0; deep < kest_frames_deep(runtime); ++deep) {
                    bodies.push_back(kest_frame_in(runtime, deep));
                }
            }
            showCompiled(true);
            for (const std::int32_t kBody : bodies) {
                for (const std::uint32_t kStart : lineStarts(kBody)) {
                    starts.emplace_back(kBody, kStart);
                }
            }
            showCompiled(false);
        }
        if (!asked(kIn, kAt) && !stepping.has_value()) {
            return;
        }
        takeOutWhereItStands();
        writeTemporary(kIn, kest_next_at(runtime, kIn, static_cast<std::uint32_t>(kAt)));
        for (const auto& [kBody, kStart] : starts) {
            if (kBody != kIn || static_cast<std::int64_t>(kStart) != kAt) {
                writeTemporary(kBody, kStart);
            }
        }
    }

    /// Every byte a breakpoint is written over put back (`compiled`), or
    /// written again: Kest walks a body from its start, as it was compiled
    /// (D630).
    void showCompiled(bool compiled) const {
        for (const Program::State::Written& one : built().written) {
            if (!one.out) {
                built().write(runtime, one, !compiled);
            }
        }
        for (const Program::State::Written& one : temporary) {
            built().write(runtime, one, !compiled);
        }
    }

    /// The line of `entry`'s instruction at `at`, its body as compiled;
    /// none where Kest cannot say.
    [[nodiscard]] std::optional<SourceLine> lineOf(std::int32_t entry, std::int64_t at) const {
        if (at < 0) {
            return std::nullopt;
        }
        const std::int64_t kFrom = kest_came_from(runtime, entry, static_cast<std::uint32_t>(at));
        const char* const kFile = kest_entry_source(runtime, entry);
        std::uint32_t line = 0;
        std::uint32_t column = 0;
        if (kFrom < 0 || kFile == nullptr ||
            !kest_locate(runtime, entry, static_cast<std::uint32_t>(kFrom), &line, &column)) {
            return std::nullopt;
        }
        return SourceLine{.file = kFile, .line = line};
    }

    /// The program's breakpoints taken out, so it is the program again.
    void clear() {
        for (const Program::State::Written& one : temporary) {
            built().write(runtime, one, false);
        }
        temporary.clear();
        for (Program::State::Written& one : built().written) {
            if (!one.out) {
                built().put(runtime, one, false);
            }
        }
        built().written.clear();
        breaking = false;
    }

    ~State() {
        // Never inside a call: the machine is thread-affine and its owner is
        // not in one while destroying it. The breakpoints it wrote go with
        // it; the program outlives it.
        if (breaking && program != nullptr) {
            clear();
        }
        if (runtime != nullptr && program != nullptr) {
            built().machines.fetch_sub(1, std::memory_order_acq_rel);
        }
        static_cast<void>(kest_runtime_free(runtime));
    }
};

Machine::Machine(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Machine::~Machine() = default;

result::Result<std::unique_ptr<Machine>> Machine::start(std::shared_ptr<const Program> program,
                                                        const DoorTable& doors,
                                                        Trust trust,
                                                        const MachineLimits& limits) {
    if (program == nullptr || limits.heapBytes == 0 || limits.fuelPerCall == 0) {
        return refuse(result::ErrorClass::InvalidArgument,
                      KestError::MissingLimit,
                      "a machine needs a program, a heap ceiling, and fuel for each call");
    }
    KestBuild* const kBuild = program->state().build;

    auto state = std::make_unique<State>();
    std::uint32_t requested = 0;
    while (kest_build_extern(kBuild, requested) != nullptr) {
        ++requested;
    }
    // Reserved once so no binding moves: Kest keeps each one's address.
    state->bindings.reserve(requested);
    for (std::uint32_t at = 0; at < requested; ++at) {
        const std::string_view kName = kest_build_extern(kBuild, at);
        const Door* const kDoor = doors.find(kName);
        if (kDoor == nullptr) {
            return std::unexpected<result::Error>{refuse(result::ErrorClass::NotFound,
                                                         KestError::UnknownDoor,
                                                         "the program asks for a door this table does not hold")
                                                      .error()
                                                      .withContext("door", kName)};
        }
        DoorCall::Shape shape;
        if (!shapeOf(kBuild, at, *kDoor, shape)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          KestError::DoorShapeMismatch,
                          "a door's slots differ from what the program declared for it");
        }
        if (trust == Trust::Untrusted && !kDoor->safeForUntrusted) {
            return refuse(result::ErrorClass::PermissionDenied,
                          KestError::DoorNotForUntrusted,
                          "untrusted code asks for a door not marked safe for it");
        }
        state->bindings.push_back(Binding{.function = kDoor->function,
                                          .context = kDoor->context,
                                          .shape = std::move(shape),
                                          .name = std::string{kName}});
    }
    KestLimits kestLimits{.stack_slots = limits.stackSlots,
                          .call_depth = limits.callDepth,
                          .heap_bytes = limits.heapBytes,
                          .fuel = limits.fuelPerCall};
    if (kestLimits.stack_slots == 0 || kestLimits.call_depth == 0) {
        KestLimits least{};
        if (!kest_needs(kBuild, &least, nullptr)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          KestError::MissingLimit,
                          "Kest cannot prove how deep this program goes; give stack slots and call depth");
        }
        kestLimits.stack_slots = kestLimits.stack_slots != 0 ? kestLimits.stack_slots : least.stack_slots;
        kestLimits.call_depth = kestLimits.call_depth != 0 ? kestLimits.call_depth : least.call_depth;
    }

    KestHost* const kHost = kest_host_new();
    if (kHost == nullptr) {
        return refuse(result::ErrorClass::ResourceExhausted, KestError::DidNotStart, "no memory for a door table");
    }
    for (Binding& binding : state->bindings) {
        // Every door untrusted code asks for is marked safe (checked above);
        // Kest refuses the start for any it was not told of.
        if (!kest_host_bind(kHost, binding.name.c_str(), &crossing, &binding) ||
            (trust == Trust::Untrusted && !kest_host_open(kHost, binding.name.c_str()))) {
            kest_host_free(kHost);
            return refuse(result::ErrorClass::ResourceExhausted, KestError::DidNotStart, "a door could not be bound");
        }
    }
    // A machine keeps its own copy of what was bound, so the host goes now.
    // Untrusted code runs only what Kest's verifier proved (ADR-0084).
    state->runtime = trust == Trust::Untrusted ? kest_start_untrusted(kBuild, kHost, &kestLimits)
                                               : kest_start(kBuild, kHost, &kestLimits);
    kest_host_free(kHost);
    if (state->runtime == nullptr) {
        ReportFile said;
        kest_build_report(kBuild, said.file(), KEST_FORM_TEXT);
        const std::string kText = said.text();
        return refuse(result::ErrorClass::FailedPrecondition,
                      KestError::DidNotStart,
                      kText.empty() ? std::string_view{"the machine did not start"} : std::string_view{kText});
    }
    program->state().machines.fetch_add(1, std::memory_order_acq_rel);
    state->program = std::move(program);
    state->fuelPerCall = limits.fuelPerCall;
    return std::make_unique<Machine>(std::move(state));
}

result::Result<Entry> Machine::entry(std::string_view name) {
    const std::string kName{name};
    const std::int32_t kIndex = kest_entry(state_->runtime, kName.c_str());
    if (kIndex < 0) {
        static_cast<void>(takeReport());
        return refuse(result::ErrorClass::NotFound,
                      KestError::UnknownEntry,
                      "the program defines no one function of this name to call");
    }
    return Entry{.index = kIndex, .frameSlots = kest_frame_slots(state_->runtime, kIndex)};
}

result::Status Machine::checkArguments(Entry entry, std::span<const Argument> takes) {
    // A number is its one kind; a lent array is a handle, one word.
    bool same = entry.index >= 0 && kest_frame_takes(state_->runtime, entry.index) == takes.size();
    for (std::uint32_t which = 0; same && which < takes.size(); ++which) {
        same = takes[which].slot != Slot::Value &&
               isOne(kest_frame_layout(state_->runtime, entry.index, which),
                     takes[which].lent ? static_cast<std::uint8_t>(KEST_L_WORD) : kindOf(takes[which].slot));
    }
    static_cast<void>(takeReport());
    if (!same) {
        return refuse(result::ErrorClass::InvalidArgument,
                      KestError::EntryShapeMismatch,
                      "a function takes other than what the engine calls it with");
    }
    return {};
}

result::Status Machine::checkAnswer(Entry entry, std::optional<Slot> gives) {
    const KestLayout* const kGives = entry.index >= 0 ? kest_frame_gives(state_->runtime, entry.index) : nullptr;
    const bool kSame = entry.index >= 0 &&
                       (gives.has_value() ? *gives != Slot::Value && isOne(kGives, kindOf(*gives)) : kGives == nullptr);
    static_cast<void>(takeReport());
    if (!kSame) {
        return refuse(result::ErrorClass::InvalidArgument,
                      KestError::EntryShapeMismatch,
                      "a function answers other than what the engine reads from it");
    }
    return {};
}

execution::TaskOutcome<void> Machine::call(Entry entry, std::span<Value> frame, Fuel fuel) {
    using Outcome = execution::TaskOutcome<void>;
    if (entry.index < 0 || frame.size() < entry.frameSlots) {
        return Outcome::failed(refuse(result::ErrorClass::InvalidArgument,
                                      KestError::FrameTooSmall,
                                      "the frame is narrower than the function needs")
                                   .error());
    }
    // Refilling fuel also takes back Kest's own cancellation, so the request
    // is read after it: a cancel landing between the two is either seen here
    // or reaches the machine after the refill.
    if (fuel == Fuel::Refill) {
        kest_fuel_set(state_->runtime, state_->fuelPerCall);
    }
    if (state_->cancelRequested.load(std::memory_order_acquire)) {
        return Outcome::cancelled(state_->cancelReason.load(std::memory_order_relaxed));
    }
    KestValue* const kFrame = frame.empty() ? nullptr : reinterpret_cast<KestValue*>(frame.data());
    bool went = kest_call(state_->runtime, entry.index, kFrame, static_cast<std::uint32_t>(frame.size()));
    // Stopped at a breakpoint (D460): the handler sees it, and the call
    // carries on from it, stepping past it (D630) or a line as the handler
    // asked (D632); a stop that is only a move of either carries on unseen.
    while (!went && kest_stopped(state_->runtime) >= 0) {
        if (state_->arrive()) {
            ++state_->stops;
            if (state_->whenStopped) {
                state_->whenStopped();
            }
        }
        state_->carryOn();
        went = kest_resume(state_->runtime, kFrame, static_cast<std::uint32_t>(frame.size()));
    }
    // A step the call ended before is over.
    state_->stepping.reset();
    state_->putBack();
    if (went) {
        return Outcome::success();
    }
    if (state_->cancelRequested.load(std::memory_order_acquire) && kest_cancelled(state_->runtime)) {
        static_cast<void>(takeReport());
        return Outcome::cancelled(state_->cancelReason.load(std::memory_order_relaxed));
    }
    const std::string kReport = takeReport();
    // Nothing left is running out: a refusal leaves fuel behind.
    if (kest_fuel_left(state_->runtime) == 0) {
        return Outcome::failed(
            refuse(result::ErrorClass::ResourceExhausted, KestError::FuelExhausted, kReport).error());
    }
    if (kest_heap_refused_by(state_->runtime) == KEST_REFUSED_CEILING) {
        return Outcome::failed(
            refuse(result::ErrorClass::ResourceExhausted, KestError::HeapExhausted, kReport).error());
    }
    return Outcome::failed(refuse(result::ErrorClass::FailedPrecondition, KestError::ScriptFailed, kReport).error());
}

result::Result<Value>
Machine::lend(void* data, std::uint32_t length, std::string_view element, std::size_t elementSize) {
    const std::string kElement{element};
    const KestValue kLent = kest_borrow(state_->runtime, data, length, kElement.c_str(), elementSize);
    if (kLent.object == nullptr) {
        const std::string kReport = takeReport();
        return refuse(result::ErrorClass::InvalidArgument, KestError::LendRefused, kReport);
    }
    Value lent{};
    lent.object = kLent.object;
    return lent;
}

void Machine::endLend(Value lent) noexcept {
    KestValue value{};
    value.object = lent.object;
    // A value that is not a live lend of this machine has nothing to end; the
    // report says so to whoever asks next.
    static_cast<void>(kest_lend_ends(state_->runtime, value));
}

void Machine::cancel(execution::CancelReason reason) noexcept {
    // The reason is written before the request is, so whoever sees the
    // request sees a reason; two cancellers racing leave either one.
    if (!state_->cancelRequested.load(std::memory_order_acquire)) {
        state_->cancelReason.store(reason, std::memory_order_relaxed);
        state_->cancelRequested.store(true, std::memory_order_release);
    }
    kest_cancel(state_->runtime);
}

Breaks Machine::breakAt(std::span<const std::string> functions, std::span<const SourceLine> lines) {
    State& state = *state_;
    state.clear();
    Breaks breaks;
    breaks.lines.assign(lines.size(), false);
    // Only into a program no other machine stands on: another, perhaps on
    // another thread, would run into them (Kest's D1077).
    if (state.built().machines.load(std::memory_order_acquire) != 1) {
        return breaks;
    }
    // Found with every breakpoint out, as Kest walks the bodies as they were
    // compiled, and written after.
    std::vector<Program::State::Written> wanted;
    const auto kWant = [&wanted](std::int32_t entry, std::uint32_t at, std::uint8_t was) {
        if (std::ranges::none_of(wanted, [entry, at](const Program::State::Written& one) {
                return one.entry == entry && one.at == at;
            })) {
            wanted.push_back(Program::State::Written{.entry = entry, .at = at, .was = was, .out = false});
        }
    };
    for (std::int32_t index = 0;; ++index) {
        const char* const kWrote = kest_entry_wrote(state.runtime, index);
        if (kWrote == nullptr) {
            break;
        }
        std::uint32_t count = 0;
        const std::uint8_t* const kCode = kest_code_of(state.runtime, index, &count);
        if (kCode == nullptr || count == 0) {
            continue;
        }
        // A function by the name it was written with, which every copy of a
        // generic shares, with its module's name before it (`game.tick`) or
        // without it.
        const std::string_view kQualified{kWrote};
        if (std::ranges::any_of(functions, [kQualified](const std::string& name) {
                return kQualified == name || (kQualified.size() > name.size() && kQualified.ends_with(name) &&
                                              kQualified[kQualified.size() - name.size() - 1] == '.');
            })) {
            kWant(index, 0, kCode[0]);
            ++breaks.functions;
        }
        // A line, at the first instruction from it in each body of its file
        // that has one (D630).
        const char* const kSource = kest_entry_source(state.runtime, index);
        if (lines.empty() || kSource == nullptr) {
            continue;
        }
        std::vector<std::size_t> mine;
        for (std::size_t at = 0; at < lines.size(); ++at) {
            if (sameFile(kSource, lines[at].file)) {
                mine.push_back(at);
            }
        }
        std::vector<bool> placed(lines.size(), false);
        for (std::int64_t at = 0; !mine.empty() && at >= 0 && at < static_cast<std::int64_t>(count);) {
            const std::optional<SourceLine> kLine = state.lineOf(index, at);
            for (const std::size_t kAsked : mine) {
                if (kLine.has_value() && kLine->line == lines[kAsked].line && !placed[kAsked]) {
                    kWant(index, static_cast<std::uint32_t>(at), kCode[at]);
                    placed[kAsked] = true;
                    breaks.lines[kAsked] = true;
                }
            }
            const std::int64_t kNext = kest_next_at(state.runtime, index, static_cast<std::uint32_t>(at));
            at = kNext > at ? kNext : -1;
        }
    }
    for (Program::State::Written& one : wanted) {
        state.built().put(state.runtime, one, true);
        state.built().written.push_back(one);
        state.breaking = true;
    }
    // Asked while stopped, in the middle of a call: one where the machine
    // stands is out until it carries on, or carrying on would stop on it
    // again at once.
    if (kest_stopped(state.runtime) >= 0) {
        state.takeOutWhereItStands();
    }
    static_cast<void>(takeReport());
    return breaks;
}

void Machine::whenStopped(std::function<void()> handler) {
    state_->whenStopped = std::move(handler);
}

void Machine::step(Step how) {
    if (kest_stopped(state_->runtime) >= 0) {
        state_->stepping = state_->standing(how);
    }
}

std::vector<StoppedFrame> Machine::stack() const {
    std::vector<StoppedFrame> frames;
    const KestRuntime* const kRuntime = state_->runtime;
    if (kest_stopped(kRuntime) < 0) {
        return frames;
    }
    const std::uint32_t kDeep = kest_frames_deep(kRuntime);
    for (std::uint32_t at = kDeep; at > 0; --at) {
        const std::uint32_t kFrame = at - 1;
        StoppedFrame& made = frames.emplace_back();
        const char* const kWrote = kest_entry_wrote(state_->runtime, kest_frame_in(kRuntime, kFrame));
        made.function = kWrote != nullptr ? kWrote : "?";
        // Where it stands (D630), its body read as compiled, as the proof
        // below is too.
        state_->showCompiled(true);
        made.at = state_->lineOf(kest_frame_in(kRuntime, kFrame), kest_frame_ip(kRuntime, kFrame));
        // Every slot the body named (D631), each said only as Kest's proof
        // of the body allows: one not written on every path to where the
        // frame stands holds what the stack held, an address among it, and
        // is said as not written; text is said where it is proved one piece
        // of text; nothing is followed as an address. The proof is of the
        // body as compiled, so it is asked with the breakpoints out.
        const std::uint16_t kWide = kest_frame_wide(kRuntime, kFrame);
        for (std::uint16_t slot = 0; slot < kWide; ++slot) {
            std::uint16_t slots = 1;
            std::uint8_t kind = KEST_L_WORD;
            const char* const kName = kest_frame_name(kRuntime, kFrame, slot, &slots, &kind);
            KestValue value{};
            if (kName == nullptr || !kest_frame_slot(kRuntime, kFrame, slot, &value)) {
                continue;
            }
            made.locals.emplace_back(kName, saidOf(kRuntime, kFrame, slot, kind, slots, value));
        }
        state_->showCompiled(false);
    }
    return frames;
}

std::uint64_t Machine::stops() const noexcept {
    return state_->stops;
}

std::uint64_t Machine::fuelLeft() const noexcept {
    return kest_fuel_left(state_->runtime);
}

std::size_t Machine::heapUsed() const noexcept {
    return kest_heap_used(state_->runtime);
}

std::string Machine::takeReport() {
    ReportFile said;
    kest_report(state_->runtime, said.file(), KEST_FORM_TEXT);
    return said.text();
}

} // namespace rawframe::kest
