#include "rawframe/kest/doors.h"

#include "rawframe/kest/errors.h"
#include "state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace rawframe::kest {

static_assert(sizeof(Value) == sizeof(KestValue) && alignof(Value) == alignof(KestValue),
              "Value crosses as a KestValue and must be laid out as one");

namespace {

KestRuntime* runtimeOf(void* machine) noexcept {
    return static_cast<KestRuntime*>(machine);
}

void squareRoot(DoorCall& call, void*) noexcept {
    call.answerReal(std::sqrt(call.real(0)));
}

void floorOf(DoorCall& call, void*) noexcept {
    call.answerReal(std::floor(call.real(0)));
}

void ceilingOf(DoorCall& call, void*) noexcept {
    call.answerReal(std::ceil(call.real(0)));
}

constexpr std::array<Slot, 1> kOneReal = {Slot::F64};

} // namespace

const Value& DoorCall::at(std::size_t argument) const noexcept {
    // Text is two slots, the bytes and their length; everything else is one.
    std::size_t slot = 0;
    for (std::size_t index = 0; index < argument; ++index) {
        slot += takes_[index] == Slot::Text ? 2 : 1;
    }
    return frame_[slot];
}

std::int64_t DoorCall::integer(std::size_t argument) const noexcept {
    return at(argument).integer;
}

double DoorCall::real(std::size_t argument) const noexcept {
    return at(argument).real;
}

bool DoorCall::boolean(std::size_t argument) const noexcept {
    return at(argument).integer != 0;
}

std::string_view DoorCall::text(std::size_t argument) const noexcept {
    std::uint32_t length = 0;
    const char* const kBytes = kest_text_bytes(reinterpret_cast<const KestValue*>(&at(argument)), &length);
    return kBytes == nullptr ? std::string_view{} : std::string_view{kBytes, length};
}

void DoorCall::answerInteger(std::int64_t value) noexcept {
    frame_[0].integer = value;
}

void DoorCall::answerReal(double value) noexcept {
    frame_[0].real = value;
}

void DoorCall::answerBoolean(bool value) noexcept {
    // A bool crosses as a whole slot, nought or one.
    frame_[0].integer = value ? 1 : 0;
}

void DoorCall::fail(std::string_view why) noexcept {
    char words[256];
    const std::size_t kLength = std::min(why.size(), sizeof words - 1);
    std::memcpy(words, why.data(), kLength);
    words[kLength] = '\0';
    kest_native_failed(runtimeOf(machine_), words);
}

void DoorCall::spendFuel(std::uint64_t work) noexcept {
    kest_fuel_spend(runtimeOf(machine_), work);
}

result::Status DoorTable::add(const Door& door) {
    if (door.name.empty() || door.function == nullptr || door.gives.size() > 1 ||
        (door.gives.size() == 1 && door.gives[0] == Slot::Text)) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kKestDomain,
                            code(KestError::DoorShapeMismatch),
                            "a door needs a name, a function, and at most one answer that is not text");
    }
    if (find(door.name) != nullptr) {
        return result::fail(result::ErrorClass::AlreadyExists,
                            kKestDomain,
                            code(KestError::DuplicateDoor),
                            "a door of this name is already in the table");
    }
    doors_.push_back(door);
    return {};
}

const Door* DoorTable::find(std::string_view name) const noexcept {
    for (const Door& door : doors_) {
        if (door.name == name) {
            return &door;
        }
    }
    return nullptr;
}

result::Status addStandardMath(DoorTable& table) {
    for (const auto& [name, function] : {std::pair{std::string_view{"Math.sqrt"}, &squareRoot},
                                         std::pair{std::string_view{"Math.floor"}, &floorOf},
                                         std::pair{std::string_view{"Math.ceil"}, &ceilingOf}}) {
        RAWFRAME_TRY(table.add(
            Door{.name = name, .function = function, .takes = kOneReal, .gives = kOneReal, .safeForUntrusted = true}));
    }
    return {};
}

} // namespace rawframe::kest
