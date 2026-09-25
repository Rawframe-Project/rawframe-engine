#include "rawframe/world/command_buffer.h"

#include "rawframe/world/errors.h"

#include <cstring>

namespace rawframe::world {

CommandBuffer::CommandBuffer(CommandBufferSettings settings) : settings_(settings) {
    commands_.reserve(settings_.maximumCommands);
    if (settings_.maximumValueBytes != 0) {
        void* memory = ::operator new(settings_.maximumValueBytes, std::align_val_t{alignof(std::max_align_t)});
        values_ = static_cast<std::byte*>(memory);
    }
}

CommandBuffer::~CommandBuffer() {
    clear();
    if (values_ != nullptr) {
        ::operator delete(values_, std::align_val_t{alignof(std::max_align_t)});
    }
}

void CommandBuffer::clear() noexcept {
    for (const Command& command : commands_) {
        if (command.value != nullptr && command.destroyValue != nullptr) {
            command.destroyValue(command.value);
        }
    }
    commands_.clear();
    valueBytes_ = 0;
    pending_ = 0;
}

result::Status CommandBuffer::record(const Command& command) {
    if (commands_.size() == settings_.maximumCommands) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kWorldDomain,
                            code(WorldError::CommandCapacity),
                            "the command buffer holds its maximum number of commands");
    }
    commands_.push_back(command);
    return {};
}

result::Result<void*> CommandBuffer::reserveValue(std::size_t size, std::size_t alignment) {
    const std::size_t kStart = (valueBytes_ + alignment - 1) / alignment * alignment;
    if (kStart + size > settings_.maximumValueBytes || commands_.size() == settings_.maximumCommands) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kWorldDomain,
                            code(WorldError::CommandCapacity),
                            "the command buffer is out of command or value space");
    }
    valueBytes_ = kStart + size;
    return static_cast<void*>(values_ + kStart);
}

result::Result<PendingEntity> CommandBuffer::create() {
    const PendingEntity kPending{pending_};
    RAWFRAME_TRY(record(Command{.kind = Kind::Create, .target = kPending}));
    ++pending_;
    return kPending;
}

result::Status CommandBuffer::insertBytes(CommandTarget target,
                                          schema::ComponentRuntimeId component,
                                          const schema::ComponentDescriptor& descriptor,
                                          std::span<const std::byte> value,
                                          std::span<const std::size_t> entities) {
    const auto kInvalid = [](std::string_view why) {
        return result::fail(result::ErrorClass::InvalidArgument, kWorldDomain, code(WorldError::InvalidValue), why);
    };
    if (!descriptor.plainData || value.size() != descriptor.size || descriptor.alignment > alignof(std::max_align_t)) {
        return kInvalid("only plain data of the component's own size is inserted from bytes");
    }
    // Which entity fields hold a pending reference, each to an entity
    // created earlier in this buffer.
    std::uint32_t references = 0;
    for (const std::size_t kOffset : entities) {
        if (kOffset > value.size() || value.size() - kOffset < sizeof(EntityHandle)) {
            return kInvalid("an entity field lies outside the value");
        }
        EntityHandle held;
        std::memcpy(&held, value.data() + kOffset, sizeof held);
        if (held.generation == 0 && held.slot != 0) {
            if (held.slot > pending_) {
                return kInvalid("a value names an entity this buffer does not create before it");
            }
            ++references;
        }
    }
    void* storage = nullptr;
    if (descriptor.size != 0) {
        RAWFRAME_TRY_ASSIGN(storage, reserveValue(descriptor.size, descriptor.alignment));
        std::memcpy(storage, value.data(), value.size());
    }
    std::uint32_t* offsets = nullptr;
    if (references != 0) {
        RAWFRAME_TRY_ASSIGN(void* reserved, reserveValue(references * sizeof(std::uint32_t), alignof(std::uint32_t)));
        offsets = static_cast<std::uint32_t*>(reserved);
        std::uint32_t at = 0;
        for (const std::size_t kOffset : entities) {
            EntityHandle held;
            std::memcpy(&held, value.data() + kOffset, sizeof held);
            if (held.generation == 0 && held.slot != 0) {
                offsets[at++] = static_cast<std::uint32_t>(kOffset);
            }
        }
    }
    return record(Command{.kind = Kind::Insert,
                          .target = target,
                          .component = component,
                          .value = storage,
                          .references = offsets,
                          .referenceCount = references});
}

result::Status CommandBuffer::destroy(EntityHandle entity) {
    return record(Command{.kind = Kind::Destroy, .target = entity});
}

} // namespace rawframe::world
