#pragma once

#include "rawframe/base/assert.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/component.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/entity.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace rawframe::world {

class World;

/// An entity a command buffer will create. Meaningful only inside the buffer
/// that returned it; it becomes an EntityHandle when the buffer is applied.
struct PendingEntity {
    std::uint32_t index = 0;
};

/// Where a command applies: a live entity, or one created earlier in the same
/// buffer.
using CommandTarget = std::variant<EntityHandle, PendingEntity>;

/// How a component value names an entity the same buffer creates: a handle
/// of generation 0, never live, whose slot is the pending index plus one.
/// An insert that says where its value holds entities has each such handle
/// replaced by the created entity's when the buffer is applied (D97).
[[nodiscard]] constexpr EntityHandle pendingReference(PendingEntity pending) noexcept {
    return EntityHandle{.slot = pending.index + 1U, .generation = 0};
}

struct CommandBufferSettings {
    std::size_t maximumCommands = 4096;
    /// Bytes for component values carried by insert commands.
    std::size_t maximumValueBytes = 64 * 1024;
};

/// Structural changes recorded while systems run and applied at a barrier
/// (SPEC-0005). Recording never allocates: both limits are reserved at
/// construction, and recording past either fails with `resource_exhausted`.
class CommandBuffer {
public:
    explicit CommandBuffer(CommandBufferSettings settings = {});
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    ~CommandBuffer();

    [[nodiscard]] result::Result<PendingEntity> create();
    [[nodiscard]] result::Status destroy(EntityHandle entity);

    template <schema::Component T>
    [[nodiscard]] result::Status insert(CommandTarget target, schema::ComponentKey<T> key, T value) {
        static_assert(alignof(T) <= alignof(std::max_align_t), "an over-aligned component cannot be buffered");
        void* storage = nullptr;
        if constexpr (!std::is_empty_v<T>) {
            RAWFRAME_TRY_ASSIGN(storage, reserveValue(sizeof(T), alignof(T)));
        }
        RAWFRAME_TRY(record(Command{.kind = Kind::Insert,
                                    .target = target,
                                    .component = key.id,
                                    .value = storage,
                                    .destroyValue = [](void* stored) noexcept {
                                        static_cast<T*>(stored)->~T();
                                    }}));
        if constexpr (!std::is_empty_v<T>) {
            ::new (storage) T(std::move(value));
        }
        return {};
    }

    template <schema::Component T>
    [[nodiscard]] result::Status remove(CommandTarget target, schema::ComponentKey<T> key) {
        return record(Command{.kind = Kind::Remove, .target = target, .component = key.id});
    }

    /// Records an insert of a plain-data component known only by runtime ID,
    /// from its bytes; `descriptor` is the registry's for `component`. For
    /// callers without the C++ type, such as a script. `entities` are the
    /// offsets in the value of EntityHandle fields: one holding a
    /// pendingReference to an entity this buffer creates earlier becomes that
    /// entity when applied. Refuses (`invalid_argument`) a component that is
    /// not plain data, bytes of another size, an entity field outside the
    /// value, and a pending reference to no entity created earlier.
    [[nodiscard]] result::Status insertBytes(CommandTarget target,
                                             schema::ComponentRuntimeId component,
                                             const schema::ComponentDescriptor& descriptor,
                                             std::span<const std::byte> value,
                                             std::span<const std::size_t> entities = {});
    [[nodiscard]] result::Status removeErased(CommandTarget target, schema::ComponentRuntimeId component) {
        return record(Command{.kind = Kind::Remove, .target = target, .component = component});
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return commands_.size();
    }
    [[nodiscard]] bool empty() const noexcept {
        return commands_.empty();
    }

    /// Drops every recorded command, destroying buffered values.
    void clear() noexcept;

private:
    friend class World;

    enum class Kind : std::uint8_t {
        Create,
        Destroy,
        Insert,
        Remove
    };

    struct Command {
        Kind kind = Kind::Create;
        CommandTarget target;
        schema::ComponentRuntimeId component;
        void* value = nullptr; // an insert's buffered value, or null for a tag
        // Null for plain data, which has nothing to destroy.
        void (*destroyValue)(void* value) noexcept = nullptr;
        // Where the value holds pending references, buffered beside it.
        const std::uint32_t* references = nullptr;
        std::uint32_t referenceCount = 0;
    };

    [[nodiscard]] result::Status record(const Command& command);
    [[nodiscard]] result::Result<void*> reserveValue(std::size_t size, std::size_t alignment);

    CommandBufferSettings settings_;
    std::vector<Command> commands_;
    std::byte* values_ = nullptr;
    std::size_t valueBytes_ = 0;
    std::uint32_t pending_ = 0;
};

/// What applying a buffer did.
struct CommitReport {
    std::size_t applied = 0;
    /// Commands whose target was already destroyed, typically by an earlier
    /// command in the same commit. Skipped, not failed: two systems may both
    /// decide an entity dies.
    std::size_t skippedStale = 0;
    /// The handles created, indexed by PendingEntity::index.
    std::vector<EntityHandle> created;
};

} // namespace rawframe::world
