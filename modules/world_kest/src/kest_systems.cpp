#include "rawframe/world_kest/kest_systems.h"

#include "rawframe/world_kest/errors.h"

#include <cstring>
#include <limits>
#include <optional>

namespace rawframe::world_kest {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, WorldKestError error, std::string_view why) {
    return result::fail(errorClass, kWorldKestDomain, code(error), why);
}

bool carriesData(world::Access access) noexcept {
    return access == world::Access::Read || access == world::Access::Write;
}

std::size_t roundUp(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1) / alignment * alignment;
}

} // namespace

/// A declaration with every view it borrowed copied into strings it owns.
struct KestSystems::Declared {
    struct Column {
        schema::ComponentTypeId component;
        std::string element;
        world::Access access;
    };

    std::string identity;
    world::Phase phase;
    kest::Entry entry;
    std::vector<Column> columns;
    std::vector<std::string> after;
    std::vector<std::string> before;
    std::vector<std::string_view> afterViews;
    std::vector<std::string_view> beforeViews;
};

class KestSystems::KestSystem final : public world::System {
public:
    struct DataColumn {
        std::string element;
        std::size_t size;
        std::size_t alignment;
        bool writes;
    };

    KestSystem(kest::Machine& machine,
               kest::Entry entry,
               world::ColumnQuery query,
               std::vector<DataColumn> columns) noexcept
        : machine_(&machine), entry_(entry), query_(std::move(query)), columns_(std::move(columns)) {
        reads_ = query_.reads();
        writes_ = query_.writes();
        frame_.resize(entry_.frameSlots);
    }

    [[nodiscard]] std::span<const schema::ComponentRuntimeId> reads() const noexcept {
        return reads_;
    }
    [[nodiscard]] std::span<const schema::ComponentRuntimeId> writes() const noexcept {
        return writes_;
    }

    result::Status run(world::SystemContext& context) noexcept override {
        chunks_.clear();
        lendFrom_.clear();
        std::size_t journalBytes = 0;
        bool tooMany = false;
        query_.forEachChunk(context.world, [&](const world::ColumnChunk& chunk) {
            const std::size_t kRows = chunk.entities.size();
            tooMany = tooMany || kRows > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
            chunks_.push_back(Chunk{.rows = kRows, .firstColumn = lendFrom_.size()});
            for (std::size_t index = 0; index < columns_.size(); ++index) {
                const DataColumn& column = columns_[index];
                if (column.writes) {
                    journalBytes = roundUp(journalBytes, column.alignment);
                    lendFrom_.push_back(Lent{.world = chunk.columns[index], .journal = journalBytes});
                    journalBytes += kRows * column.size;
                } else {
                    lendFrom_.push_back(Lent{.world = chunk.columns[index], .journal = kInPlace});
                }
            }
        });
        if (tooMany) {
            return refuse(result::ErrorClass::OutOfRange,
                          WorldKestError::TooManyRows,
                          "an archetype holds more rows than a Kest count can say");
        }
        if (chunks_.empty()) {
            return {};
        }
        // The journal grows to the largest tick seen and is then reused.
        if (journal_.size() < journalBytes) {
            journal_.resize(journalBytes);
        }
        for (const Chunk& chunk : chunks_) {
            for (std::size_t index = 0; index < columns_.size(); ++index) {
                const Lent& lent = lendFrom_[chunk.firstColumn + index];
                if (lent.journal != kInPlace) {
                    std::memcpy(journalAt(lent), lent.world, chunk.rows * columns_[index].size);
                }
            }
        }

        for (std::size_t at = 0; at < chunks_.size(); ++at) {
            RAWFRAME_TRY(callOnce(chunks_[at], at == 0 ? kest::Fuel::Refill : kest::Fuel::Continue));
        }

        // Every call succeeded: the journal becomes the World's values.
        for (const Chunk& chunk : chunks_) {
            for (std::size_t index = 0; index < columns_.size(); ++index) {
                const Lent& lent = lendFrom_[chunk.firstColumn + index];
                if (lent.journal != kInPlace) {
                    std::memcpy(lent.world, journalAt(lent), chunk.rows * columns_[index].size);
                }
            }
        }
        return {};
    }

private:
    static constexpr std::size_t kInPlace = std::numeric_limits<std::size_t>::max();

    struct Chunk {
        std::size_t rows;
        std::size_t firstColumn;
    };

    /// Where one column of one chunk is lent from: the World in place, or an
    /// offset into the journal.
    struct Lent {
        std::byte* world;
        std::size_t journal;
    };

    [[nodiscard]] std::byte* journalAt(const Lent& lent) noexcept {
        return journal_.data() + lent.journal;
    }

    [[nodiscard]] result::Status callOnce(const Chunk& chunk, kest::Fuel fuel) {
        std::vector<kest::Value>& frame = frame_;
        std::fill(frame.begin(), frame.end(), kest::Value{});
        frame[0].integer = static_cast<std::int64_t>(chunk.rows);
        std::size_t made = 0;
        std::optional<result::Error> failure;
        for (; made < columns_.size(); ++made) {
            const Lent& lent = lendFrom_[chunk.firstColumn + made];
            auto handle = machine_->lend(lent.journal == kInPlace ? lent.world : journalAt(lent),
                                         static_cast<std::uint32_t>(chunk.rows),
                                         columns_[made].element,
                                         columns_[made].size);
            if (!handle.has_value()) {
                failure.emplace(std::move(handle).error());
                break;
            }
            frame[1 + made] = *handle;
        }
        if (!failure) {
            auto outcome = machine_->call(entry_, frame, fuel);
            if (outcome.isCancelled()) {
                failure.emplace(refuse(result::ErrorClass::FailedPrecondition,
                                       WorldKestError::Cancelled,
                                       "the Kest machine was cancelled while the system ran")
                                    .error());
            } else if (outcome.isError()) {
                failure.emplace(std::move(outcome).takeError());
            }
        }
        // Lends end whatever happened: the program must hold nothing over
        // World memory once the call is over.
        for (std::size_t index = 0; index < made; ++index) {
            machine_->endLend(frame_[1 + index]);
        }
        if (failure) {
            return std::unexpected<result::Error>{std::move(*failure)};
        }
        return {};
    }

    kest::Machine* machine_;
    kest::Entry entry_;
    world::ColumnQuery query_;
    std::vector<DataColumn> columns_;
    std::vector<schema::ComponentRuntimeId> reads_;
    std::vector<schema::ComponentRuntimeId> writes_;
    std::vector<kest::Value> frame_;
    std::vector<Chunk> chunks_;
    std::vector<Lent> lendFrom_;
    std::vector<std::byte> journal_;
};

KestSystems::KestSystems(std::shared_ptr<const kest::Program> program,
                         std::unique_ptr<kest::Machine> machine,
                         std::vector<Declared> declared) noexcept
    : program_(std::move(program)), machine_(std::move(machine)), declared_(std::move(declared)) {
}

KestSystems::~KestSystems() = default;

result::Result<std::unique_ptr<KestSystems>> KestSystems::create(KestSystemsSettings settings) {
    RAWFRAME_TRY_ASSIGN(std::unique_ptr<kest::Machine> machine,
                        kest::Machine::start(settings.program, settings.doors, kest::Trust::Trusted, settings.limits));
    std::vector<Declared> declared;
    declared.reserve(settings.systems.size());
    for (const KestSystemDeclaration& system : settings.systems) {
        RAWFRAME_TRY_ASSIGN(const kest::Entry kEntry, machine->entry(system.entry));
        Declared copy{.identity = std::string{system.identity},
                      .phase = system.phase,
                      .entry = kEntry,
                      .columns = {},
                      .after = {system.after.begin(), system.after.end()},
                      .before = {system.before.begin(), system.before.end()},
                      .afterViews = {},
                      .beforeViews = {}};
        std::size_t data = 0;
        for (const KestColumn& column : system.columns) {
            if (carriesData(column.access)) {
                ++data;
                RAWFRAME_TRY(settings.program->layout(column.element));
            }
            copy.columns.push_back(Declared::Column{
                .component = column.component, .element = std::string{column.element}, .access = column.access});
        }
        // The count, then one array per data column; an answer, if any, is
        // one slot and fits over the count.
        if (kEntry.frameSlots != 1 + data) {
            return refuse(result::ErrorClass::InvalidArgument,
                          WorldKestError::EntryMismatch,
                          "a Kest system takes a count and one array per read or written column");
        }
        declared.push_back(std::move(copy));
    }
    for (Declared& copy : declared) {
        copy.afterViews.assign(copy.after.begin(), copy.after.end());
        copy.beforeViews.assign(copy.before.begin(), copy.before.end());
    }
    return std::make_unique<KestSystems>(std::move(settings.program), std::move(machine), std::move(declared));
}

result::Status KestSystems::declareSystems(const schema::SchemaRegistry& registry,
                                           std::vector<world::SystemDeclaration>& systems) noexcept {
    // A World that starts again gets systems of its own: queries serve one
    // World.
    systems_.clear();
    for (const Declared& declared : declared_) {
        std::vector<world::ColumnTerm> terms;
        std::vector<KestSystem::DataColumn> data;
        for (const Declared::Column& column : declared.columns) {
            RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kId, registry.find(column.component));
            terms.push_back(world::ColumnTerm{.component = kId, .access = column.access});
            if (!carriesData(column.access)) {
                continue;
            }
            const schema::ComponentDescriptor& descriptor = registry.descriptor(kId);
            RAWFRAME_TRY_ASSIGN(const kest::TypeLayout kLayout, program_->layout(column.element));
            if (!descriptor.plainData || descriptor.size != kLayout.size || descriptor.alignment != kLayout.alignment) {
                return refuse(result::ErrorClass::InvalidArgument,
                              WorldKestError::ColumnMismatch,
                              "a Kest column's component is not plain data shaped like its Kest type");
            }
            data.push_back(KestSystem::DataColumn{.element = column.element,
                                                  .size = descriptor.size,
                                                  .alignment = descriptor.alignment,
                                                  .writes = column.access == world::Access::Write});
        }
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, world::ColumnQuery::resolve(terms, registry));
        auto system = std::make_unique<KestSystem>(*machine_, declared.entry, std::move(query), std::move(data));
        systems.push_back(world::SystemDeclaration{.identity = declared.identity,
                                                   .phase = declared.phase,
                                                   .reads = system->reads(),
                                                   .writes = system->writes(),
                                                   .after = declared.afterViews,
                                                   .before = declared.beforeViews,
                                                   .system = system.get()});
        systems_.push_back(std::move(system));
    }
    return {};
}

} // namespace rawframe::world_kest
