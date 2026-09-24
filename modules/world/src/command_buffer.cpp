#include "rawframe/world/command_buffer.h"

#include "rawframe/world/errors.h"

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
        if (command.value != nullptr) {
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

result::Status CommandBuffer::destroy(EntityHandle entity) {
    return record(Command{.kind = Kind::Destroy, .target = entity});
}

} // namespace rawframe::world
