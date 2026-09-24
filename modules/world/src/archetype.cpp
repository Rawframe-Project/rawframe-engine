#include "rawframe/world/archetype.h"

#include "rawframe/base/assert.h"

#include <algorithm>
#include <new>

namespace rawframe::world::detail {

namespace {

// The first allocation for a column holds this many rows.
constexpr std::size_t kInitialRows = 8;

std::byte* allocateColumn(std::size_t bytes, std::size_t alignment) noexcept {
    void* memory = ::operator new(bytes, std::align_val_t{alignment}, std::nothrow);
    // World storage is not bounded fallible storage: running out is fatal
    // (ADR-0009). The World's entity ceiling is what bounds it.
    RAWFRAME_CHECK(memory != nullptr, "out of memory for component storage");
    return static_cast<std::byte*>(memory);
}

void freeColumn(std::byte* data, std::size_t alignment) noexcept {
    ::operator delete(data, std::align_val_t{alignment});
}

} // namespace

Archetype::Archetype(std::vector<schema::ComponentRuntimeId> components, const schema::SchemaRegistry& registry)
    : components_(std::move(components)) {
    RAWFRAME_ASSERT(std::is_sorted(components_.begin(), components_.end()), "archetype components are sorted");
    for (const schema::ComponentRuntimeId kComponent : components_) {
        const schema::ComponentDescriptor& descriptor = registry.descriptor(kComponent);
        if (descriptor.size != 0) {
            columns_.push_back(Column{.component = kComponent,
                                      .size = descriptor.size,
                                      .alignment = descriptor.alignment,
                                      .operations = descriptor.operations,
                                      .data = nullptr});
        }
    }
}

Archetype::~Archetype() {
    for (Column& column : columns_) {
        for (std::size_t row = 0; row < entities_.size(); ++row) {
            column.destroy(column.at(row));
        }
        if (column.data != nullptr) {
            freeColumn(column.data, column.alignment);
        }
    }
}

bool Archetype::has(schema::ComponentRuntimeId component) const noexcept {
    return std::binary_search(components_.begin(), components_.end(), component);
}

int Archetype::columnIndex(schema::ComponentRuntimeId component) const noexcept {
    const auto kFound = std::lower_bound(
        columns_.begin(), columns_.end(), component, [](const Column& column, schema::ComponentRuntimeId wanted) {
            return column.component < wanted;
        });
    if (kFound == columns_.end() || kFound->component != component) {
        return -1;
    }
    return static_cast<int>(kFound - columns_.begin());
}

void Archetype::grow() {
    const std::size_t kCapacity = capacity_ == 0 ? kInitialRows : capacity_ * 2;
    for (Column& column : columns_) {
        std::byte* data = allocateColumn(kCapacity * column.size, column.alignment);
        for (std::size_t row = 0; row < entities_.size(); ++row) {
            column.moveConstruct(data + (row * column.size), column.at(row));
            column.destroy(column.at(row));
        }
        if (column.data != nullptr) {
            freeColumn(column.data, column.alignment);
        }
        column.data = data;
    }
    entities_.reserve(kCapacity);
    capacity_ = kCapacity;
}

std::size_t Archetype::appendRow(EntityHandle entity) {
    if (entities_.size() == capacity_) {
        grow();
    }
    entities_.push_back(entity);
    return entities_.size() - 1;
}

EntityHandle Archetype::removeRow(std::size_t row) noexcept {
    const std::size_t kLast = entities_.size() - 1;
    for (Column& column : columns_) {
        column.destroy(column.at(row));
        if (row != kLast) {
            column.moveConstruct(column.at(row), column.at(kLast));
            column.destroy(column.at(kLast));
        }
    }
    EntityHandle moved{};
    if (row != kLast) {
        moved = entities_[kLast];
        entities_[row] = moved;
    }
    entities_.pop_back();
    return moved;
}

} // namespace rawframe::world::detail
