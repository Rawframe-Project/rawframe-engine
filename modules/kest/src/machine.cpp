#include "rawframe/kest/machine.h"

#include "rawframe/kest/errors.h"
#include "state.h"

#include <atomic>
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
        shape.slots.push_back(door.takes[which].slot);
        shape.offsets.push_back(offset);
        shape.layouts.push_back(door.takes[which].slot == Slot::Value ? kLayout : nullptr);
        offset += kLayout->slots;
    }
    const KestLayout* const kGives = kest_extern_gives(build, at);
    if (door.gives.empty()) {
        return kGives == nullptr;
    }
    if (!sameParameter(build, kGives, door.gives[0])) {
        return false;
    }
    shape.gives = door.gives[0].slot == Slot::Value ? kGives : nullptr;
    return true;
}

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, KestError error, std::string_view description) {
    return result::fail(errorClass, kKestDomain, code(error), description);
}

} // namespace

struct Machine::State {
    std::shared_ptr<const Program> program;
    std::vector<Binding> bindings;
    KestRuntime* runtime = nullptr;
    std::uint64_t fuelPerCall = 0;
    std::atomic<bool> cancelRequested{false};
    std::atomic<execution::CancelReason> cancelReason{execution::CancelReason::Requested};

    ~State() {
        // Never inside a call: the machine is thread-affine and its owner is
        // not in one while destroying it.
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
            return refuse(result::ErrorClass::NotFound,
                          KestError::UnknownDoor,
                          "the program asks for a door this table does not hold");
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
    if (trust == Trust::Untrusted) {
        return refuse(result::ErrorClass::Unsupported,
                      KestError::UntrustedNotYetSupported,
                      "untrusted code waits for Kest's untrusted profile");
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
        if (!kest_host_bind(kHost, binding.name.c_str(), &crossing, &binding)) {
            kest_host_free(kHost);
            return refuse(result::ErrorClass::ResourceExhausted, KestError::DidNotStart, "a door could not be bound");
        }
    }
    // A machine keeps its own copy of what was bound, so the host goes now.
    state->runtime = kest_start(kBuild, kHost, &kestLimits);
    kest_host_free(kHost);
    if (state->runtime == nullptr) {
        ReportFile said;
        kest_build_report(kBuild, said.file(), KEST_FORM_TEXT);
        const std::string kText = said.text();
        return refuse(result::ErrorClass::FailedPrecondition,
                      KestError::DidNotStart,
                      kText.empty() ? std::string_view{"the machine did not start"} : std::string_view{kText});
    }
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
    if (kest_call(state_->runtime, entry.index, kFrame, static_cast<std::uint32_t>(frame.size()))) {
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
