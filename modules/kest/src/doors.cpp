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

constexpr std::array<Parameter, 1> kOneReal = {Slot::F64};

} // namespace

namespace {

/// How many bytes a piece of this kind is where memory is shared, or zero for
/// a kind that does not cross by value.
std::size_t widthOf(std::uint8_t kind) noexcept {
    switch (kind) {
    case KEST_L_I8:
    case KEST_L_U8:
    case KEST_L_BOOL:
    case KEST_L_HELD:
    case KEST_L_FLAGS8:
        return 1;
    case KEST_L_I16:
    case KEST_L_U16:
    case KEST_L_FLAGS16:
        return 2;
    case KEST_L_I32:
    case KEST_L_U32:
    case KEST_L_F32:
    case KEST_L_TAG:
    case KEST_L_FLAGS32:
        return 4;
    case KEST_L_I64:
    case KEST_L_U64:
    case KEST_L_F64:
    case KEST_L_FLAGS64:
        return 8;
    default:
        return 0;
    }
}

/// How a piece of this kind becomes a slot and back; `widthOf` says the
/// kind crosses at all.
Crossing::Copy copyOf(std::uint8_t kind) noexcept {
    switch (kind) {
    case KEST_L_F32:
        return Crossing::Copy::Real32;
    case KEST_L_F64:
        return Crossing::Copy::Real64;
    case KEST_L_I8:
        return Crossing::Copy::Signed8;
    case KEST_L_I16:
        return Crossing::Copy::Signed16;
    case KEST_L_I32:
    case KEST_L_TAG:
        return Crossing::Copy::Signed32;
    case KEST_L_U8:
    case KEST_L_BOOL:
    case KEST_L_HELD:
    case KEST_L_FLAGS8:
        return Crossing::Copy::Unsigned8;
    case KEST_L_U16:
    case KEST_L_FLAGS16:
        return Crossing::Copy::Unsigned16;
    case KEST_L_U32:
    case KEST_L_FLAGS32:
        return Crossing::Copy::Unsigned32;
    default:
        return Crossing::Copy::Whole64;
    }
}

template <typename Narrow> void narrowInto(std::int64_t whole, std::byte* into) noexcept {
    // Whole numbers keep their low bits: the slot holds the value the
    // program computed at its declared width, so nothing is lost.
    const auto kValue = static_cast<Narrow>(whole);
    std::memcpy(into, &kValue, sizeof kValue);
}

template <typename Narrow> std::int64_t widenFrom(const std::byte* from) noexcept {
    Narrow value{};
    std::memcpy(&value, from, sizeof value);
    return static_cast<std::int64_t>(value);
}

/// One slot into the bytes of one piece.
void toBytes(Crossing::Copy copy, const KestValue& slot, std::byte* into) noexcept {
    switch (copy) {
    case Crossing::Copy::Real32: {
        const auto kValue = static_cast<float>(slot.real);
        std::memcpy(into, &kValue, sizeof kValue);
        return;
    }
    case Crossing::Copy::Real64:
        std::memcpy(into, &slot.real, sizeof slot.real);
        return;
    case Crossing::Copy::Unsigned8:
    case Crossing::Copy::Signed8:
        narrowInto<std::uint8_t>(slot.integer, into);
        return;
    case Crossing::Copy::Unsigned16:
    case Crossing::Copy::Signed16:
        narrowInto<std::uint16_t>(slot.integer, into);
        return;
    case Crossing::Copy::Unsigned32:
    case Crossing::Copy::Signed32:
        narrowInto<std::uint32_t>(slot.integer, into);
        return;
    case Crossing::Copy::Whole64:
        std::memcpy(into, &slot.integer, sizeof slot.integer);
        return;
    }
}

/// The bytes of one piece into one slot, widened as the machine expects.
void fromBytes(Crossing::Copy copy, const std::byte* from, KestValue& slot) noexcept {
    switch (copy) {
    case Crossing::Copy::Real32: {
        float value = 0;
        std::memcpy(&value, from, sizeof value);
        slot.real = value;
        return;
    }
    case Crossing::Copy::Real64:
        std::memcpy(&slot.real, from, sizeof slot.real);
        return;
    case Crossing::Copy::Unsigned8:
        slot.integer = widenFrom<std::uint8_t>(from);
        return;
    case Crossing::Copy::Unsigned16:
        slot.integer = widenFrom<std::uint16_t>(from);
        return;
    case Crossing::Copy::Unsigned32:
        slot.integer = widenFrom<std::uint32_t>(from);
        return;
    case Crossing::Copy::Signed8:
        slot.integer = widenFrom<std::int8_t>(from);
        return;
    case Crossing::Copy::Signed16:
        slot.integer = widenFrom<std::int16_t>(from);
        return;
    case Crossing::Copy::Signed32:
        slot.integer = widenFrom<std::int32_t>(from);
        return;
    case Crossing::Copy::Whole64:
        std::memcpy(&slot.integer, from, sizeof slot.integer);
        return;
    }
}

} // namespace

bool crossesByValue(const KestLayout* layout) noexcept {
    if (layout == nullptr || layout->tagged || layout->count != layout->slots) {
        return false;
    }
    for (std::uint16_t index = 0; index < layout->count; ++index) {
        const KestPiece& piece = layout->pieces[index];
        if (widthOf(piece.kind) == 0 || piece.offset + widthOf(piece.kind) > layout->size) {
            return false;
        }
    }
    return true;
}

Crossing crossingOf(const KestLayout* layout) {
    Crossing crossing{.value = true, .size = layout->size, .pieces = {}, .gaps = false};
    std::size_t covered = 0;
    for (std::uint16_t index = 0; index < layout->count; ++index) {
        const KestPiece& piece = layout->pieces[index];
        crossing.pieces.push_back(Crossing::Piece{.offset = piece.offset, .copy = copyOf(piece.kind)});
        covered += widthOf(piece.kind);
    }
    // Pieces never overlap (`crossesByValue`), so fewer bytes than the size
    // leave a gap somewhere.
    crossing.gaps = covered != layout->size;
    return crossing;
}

Value* DoorCall::at(std::size_t argument) const noexcept {
    return frame_ + shape_->offsets[argument];
}

std::int64_t DoorCall::integer(std::size_t argument) const noexcept {
    return at(argument)->integer;
}

double DoorCall::real(std::size_t argument) const noexcept {
    return at(argument)->real;
}

bool DoorCall::boolean(std::size_t argument) const noexcept {
    return at(argument)->integer != 0;
}

std::string_view DoorCall::text(std::size_t argument) const noexcept {
    std::uint32_t length = 0;
    const char* const kBytes = kest_text_bytes(reinterpret_cast<const KestValue*>(at(argument)), &length);
    return kBytes == nullptr ? std::string_view{} : std::string_view{kBytes, length};
}

bool DoorCall::value(std::size_t argument, std::span<std::byte> into) const noexcept {
    const Crossing& kCrossing = shape_->takes[argument];
    if (!kCrossing.value || into.size() != kCrossing.size) {
        return false;
    }
    if (kCrossing.gaps) {
        std::memset(into.data(), 0, into.size());
    }
    const auto* slots = reinterpret_cast<const KestValue*>(at(argument));
    for (std::size_t index = 0; index < kCrossing.pieces.size(); ++index) {
        toBytes(kCrossing.pieces[index].copy, slots[index], into.data() + kCrossing.pieces[index].offset);
    }
    return true;
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

bool DoorCall::answerValue(std::span<const std::byte> from) noexcept {
    const Crossing& kCrossing = shape_->gives;
    if (!kCrossing.value || from.size() != kCrossing.size) {
        return false;
    }
    auto* slots = reinterpret_cast<KestValue*>(frame_);
    for (std::size_t index = 0; index < kCrossing.pieces.size(); ++index) {
        fromBytes(kCrossing.pieces[index].copy, from.data() + kCrossing.pieces[index].offset, slots[index]);
    }
    return true;
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
    bool typed = true;
    for (const std::span<const Parameter> kParameters : {door.takes, door.gives}) {
        for (const Parameter& parameter : kParameters) {
            typed = typed && (parameter.slot == Slot::Value) == !parameter.type.empty();
        }
    }
    if (door.name.empty() || door.function == nullptr || door.gives.size() > 1 || !typed ||
        (door.gives.size() == 1 && door.gives[0].slot == Slot::Text)) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kKestDomain,
                            code(KestError::DoorShapeMismatch),
                            "a door needs a name, a function, at most one answer that is not text, and a "
                            "type named for exactly its value slots");
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
