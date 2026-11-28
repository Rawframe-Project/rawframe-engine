#pragma once

// A game's own log records: `Log.write(level, message)` and
// `Log.about(level, entity, message)` from `rawframe.log`, and Kest's
// `Io.write(text)` that `std.io`'s `print` writes through, each a record in
// the process's diagnostics, naming the system that wrote it and the tick.
// What a game writes is its own text: diagnostics, never shown to a player.
//
// Records are bounded: a message is cut at kMaximumLogBytes, a run writes
// at most kMaximumLogRecordsPerRun, a record repeating the one before it is
// counted rather than written again, and what is held back is told in one
// record when the run ends. Configuration chooses the least level written
// (`kest.log_level`) and the systems whose records are left out
// (`kest.log_mute`). Every door here writes outside the World, so none is
// for untrusted code.

#include "rawframe/composition/configuration.h"
#include "rawframe/diagnostics/emitter.h"
#include "rawframe/kest/doors.h"
#include "rawframe/world_kest/kest_systems.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rawframe::world_kest {

/// What a game's records are filed under.
inline constexpr diagnostics::EventIdentity kGameLog{"world_kest", "game_log"};
/// The one record a run's held-back records are told in.
inline constexpr diagnostics::EventIdentity kGameLogHeld{"world_kest", "game_log_held"};

/// The most bytes one message keeps, and records one run writes.
inline constexpr std::size_t kMaximumLogBytes = 1024;
inline constexpr std::size_t kMaximumLogRecordsPerRun = 32;
/// The most systems `kest.log_mute` names.
inline constexpr std::size_t kMaximumMutedSystems = 256;

struct LogSettings {
    /// The least level written.
    diagnostics::Severity least = diagnostics::Severity::Info;
    /// Systems whose records are left out, by identity.
    std::vector<std::string> muted;
};

/// `kest.log_level` (debug, info, warning or error; info when absent) and
/// `kest.log_mute` (system identities apart by spaces). Refused for
/// another level, or more than kMaximumMutedSystems systems.
[[nodiscard]] result::Result<LogSettings> readLogSettings(const composition::Configuration& configuration);

class GameLog final : public KestStaging {
public:
    /// A log whose records go to each run's emitter; one that is never a
    /// staging of systems writes nothing, as a predicting client's.
    explicit GameLog(LogSettings settings) noexcept;

    /// Adds `Log.write`, `Log.about` and `Io.write`, answered by this log.
    [[nodiscard]] result::Status addDoors(kest::DoorTable& doors);

    void begin(world::SystemContext& context) noexcept override;
    void end(bool kept) noexcept override;

    /// Writes one record at `level` from the run under way, as the doors do.
    void write(diagnostics::Severity level, std::string_view message, const world::EntityHandle* about);

    /// Records written and held back since it began.
    [[nodiscard]] std::uint64_t written() const noexcept {
        return written_;
    }
    [[nodiscard]] std::uint64_t held() const noexcept {
        return held_;
    }

private:
    static void writeDoor(kest::DoorCall& call, void* context) noexcept;
    static void aboutDoor(kest::DoorCall& call, void* context) noexcept;
    static void ioDoor(kest::DoorCall& call, void* context) noexcept;

    /// Writes the line `print` has built so far, if any.
    void flushLine();
    /// Tells what this run held back, and starts counting again.
    void tellHeld();

    diagnostics::Emitter emitter_;
    LogSettings settings_;
    std::string system_;
    std::uint64_t tick_ = 0;
    bool muted_ = false;
    std::size_t runRecords_ = 0;
    std::size_t runHeld_ = 0;
    std::size_t runRepeats_ = 0;
    std::string last_;
    diagnostics::Severity lastLevel_ = diagnostics::Severity::Info;
    std::string line_;
    std::uint64_t written_ = 0;
    std::uint64_t held_ = 0;
};

} // namespace rawframe::world_kest
