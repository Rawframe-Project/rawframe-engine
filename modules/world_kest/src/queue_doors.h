#pragma once

// A game's queues as doors: for each queue a game declares,
// `Queue.<name>(value: T)` writes one from a system whose line says `writes
// <name>`, `QueuedCount.<name>() -> i32` says how many a system whose line
// says `reads <name>` has not read, and `Queued.<name>(index: i32) -> T`
// reads one, oldest first. What a run writes is kept only when the run
// succeeds, and then goes to the World's queue at once, so a reader running
// later in the tick reads it; a reader that succeeds has read everything
// its run could. The writing door is trusted code's alone, as it writes
// the World; reading is safe for untrusted code.

#include "rawframe/kest/doors.h"
#include "rawframe/kest/program.h"
#include "rawframe/world/queues.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/kest_systems.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace rawframe::world_kest {

class QueueDoors final : public KestStaging {
public:
    /// Refuses a queue whose type the program does not lay out, holds an
    /// entity, or is past world::kMaximumQueuedValueBytes.
    [[nodiscard]] static result::Result<std::unique_ptr<QueueDoors>> create(const GameDescription& game,
                                                                            const kest::Program& program);

    [[nodiscard]] result::Status addDoors(kest::DoorTable& doors);

    /// Each queue as a World declares it, its readers the systems that read
    /// it.
    [[nodiscard]] std::vector<world::QueueDeclaration> declarations() const;

    void begin(world::SystemContext& context) noexcept override;
    void end(bool kept) noexcept override;

    struct Kind {
        QueueDoors* owner = nullptr;
        world::QueueDeclaration declaration;
        std::string kestType;
        std::vector<std::string> writers;
        std::string write;
        std::string count;
        std::string read;
        std::array<kest::Parameter, 1> writeTakes{kest::Parameter{kest::Slot::Value, ""}};
        std::array<kest::Parameter, 1> readGives{kest::Parameter{kest::Slot::Value, ""}};
        /// What the run under way wrote, value after value.
        std::vector<std::byte> pending;
    };

private:
    QueueDoors() noexcept = default;

    static void writeDoor(kest::DoorCall& call, void* context) noexcept;
    static void countDoor(kest::DoorCall& call, void* context) noexcept;
    static void readDoor(kest::DoorCall& call, void* context) noexcept;

    /// The World's queue of `kind`, while a run of one of its readers or
    /// writers is under way; null otherwise.
    [[nodiscard]] world::MessageQueue* queueOf(const Kind& kind) const noexcept;

    std::vector<std::unique_ptr<Kind>> kinds_;
    world::SystemContext* context_ = nullptr;
};

} // namespace rawframe::world_kest
