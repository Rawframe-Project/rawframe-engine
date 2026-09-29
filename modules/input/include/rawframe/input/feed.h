#pragma once

// What a client's devices reported between two deliveries, in order: the
// devices that came and went, their control events, and the moments every
// held control was let go. A client host writes it from its platform's
// records between Host iterations; a player's input source delivers it to
// its mapper at the player's tick. The other way, what the client asks its
// devices to feel waits for the host to take it (D251). Nothing here knows
// a window system.

#include "rawframe/input/controls.h"
#include "rawframe/input/mapper.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rawframe::input {

class Feed {
public:
    /// At most `capacity` records wait. Past it, the waiting control events
    /// are dropped, since a later one may depend on them, and a release of
    /// everything takes their place, so nothing stays held across the gap;
    /// devices coming and going are kept.
    explicit Feed(std::size_t capacity = 1024);

    void connect(DeviceId device, DeviceClass deviceClass);
    void disconnect(DeviceId device);
    void submit(const ControlEvent& event);
    /// Focus was lost, or the platform lost input: let go of everything.
    void releaseAll();

    /// Applies every record, in order, to `mapper`: devices are paired to
    /// `player` or unpaired, events submitted, releases made. Empties the
    /// feed. A device the mapper refuses (past its limit) is left unpaired,
    /// and its events count as unpaired there.
    void deliver(Mapper& mapper, PlayerSlot player);

    /// Asks a device to feel a haptic. One still waiting for the same
    /// device is replaced, as the device would replace it; past
    /// `kMostFelt` devices waiting, the command is dropped.
    void feel(const HapticCommand& command);
    /// The haptic commands waiting, oldest first, into `into`, cleared
    /// first; none wait after.
    void takeFelt(std::vector<HapticCommand>& into);

    static constexpr std::size_t kMostFelt = 64;

    /// Control events dropped for want of room since the feed was made.
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_;
    }
    [[nodiscard]] std::size_t waiting() const noexcept {
        return records_.size();
    }
    /// Haptic commands dropped for want of room.
    [[nodiscard]] std::uint64_t feltDropped() const noexcept {
        return feltDropped_;
    }

private:
    enum class Kind : std::uint8_t {
        Connect,
        Disconnect,
        Control,
        ReleaseAll
    };
    struct Record {
        Kind kind = Kind::Control;
        DeviceClass deviceClass = DeviceClass::Keyboard;
        ControlEvent event;
    };

    void add(const Record& record);

    std::size_t capacity_;
    std::vector<Record> records_;
    std::uint64_t dropped_ = 0;
    std::vector<HapticCommand> felt_;
    std::uint64_t feltDropped_ = 0;
};

} // namespace rawframe::input
