#include "game_persistence.h"

#include "field_kinds.h"
#include "rawframe/world_kest/errors.h"

#include <algorithm>
#include <string>
#include <vector>

namespace rawframe::world_kest {

namespace {

/// Whether `piece` of `component`'s layout is the slot or generation of an
/// entity field an `entity` line names, and which line.
const GameEntityField*
entityPiece(const GameDescription& game, const GameComponent& component, const kest::Field& piece) {
    const auto kEntity = std::ranges::find_if(game.entityFields, [&](const GameEntityField& each) {
        return each.component == component.name &&
               (piece.name == each.field + ".slot" || piece.name == each.field + ".generation");
    });
    return kEntity != game.entityFields.end() ? &*kEntity : nullptr;
}

/// A component's fields as a save names them: each scalar by its Kest path,
/// and each entity field an `entity` line names as one entity field in place
/// of its slot and generation. None, and so no migration, for a layout with
/// a field a save cannot name.
std::vector<world_save::SavedField>
savedFields(const GameDescription& game, const GameComponent& component, const kest::TypeLayout& layout) {
    std::vector<world_save::SavedField> fields;
    for (const kest::Field& field : layout.fields) {
        if (const GameEntityField* entity = entityPiece(game, component, field)) {
            if (field.name.ends_with(".slot")) {
                fields.push_back(world_save::SavedField{.name = entity->field,
                                                        .offset = static_cast<std::uint32_t>(field.offset),
                                                        .kind = world_save::FieldKind::Entity});
            }
            continue;
        }
        const auto kKind = savedKind(field.kind);
        if (!kKind.has_value()) {
            return {};
        }
        fields.push_back(world_save::SavedField{
            .name = field.name, .offset = static_cast<std::uint32_t>(field.offset), .kind = *kKind});
    }
    return fields;
}

world_save::SaveDeclaration
declaredSave(const GameDescription& game, std::span<const kest::TypeLayout> layouts, const GameSave& line) {
    world_save::SaveDeclaration save{.document = line.document, .components = {}};
    for (const std::string& name : line.components) {
        const auto kComponent = std::ranges::find(game.components, name, &GameComponent::name);
        const kest::TypeLayout& layout = layouts[static_cast<std::size_t>(kComponent - game.components.begin())];
        save.components.push_back(world_save::SavedComponent{
            .id = kComponent->id, .mark = layout.mark, .fields = savedFields(game, *kComponent, layout)});
    }
    return save;
}

} // namespace

result::Result<GamePersistence> planPersistence(const GameDescription& game,
                                                std::span<const kest::TypeLayout> layouts) {
    for (const GameEntityField& field : game.entityFields) {
        const auto kComponent = std::ranges::find(game.components, field.component, &GameComponent::name);
        const kest::TypeLayout& layout = layouts[static_cast<std::size_t>(kComponent - game.components.begin())];
        const auto kPiece = [&](std::string_view suffix) -> const kest::Field* {
            for (const kest::Field& piece : layout.fields) {
                if (piece.name == field.field + std::string{suffix} && piece.kind == kest::FieldKind::U32) {
                    return &piece;
                }
            }
            return nullptr;
        };
        const kest::Field* slot = kPiece(".slot");
        const kest::Field* generation = kPiece(".generation");
        if (slot == nullptr || generation == nullptr || generation->offset != slot->offset + 4) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               kWorldKestDomain,
                                                               code(WorldKestError::UnknownName),
                                                               "an entity line names a field that is not an Entity")
                                                      .error()
                                                      .withContext("field", field.field)};
        }
    }
    GamePersistence persistence;
    for (std::size_t index = 0; index < game.components.size() && !persistence.unwritable.has_value(); ++index) {
        const GameComponent& component = game.components[index];
        const kest::TypeLayout& layout = layouts[index];
        world_snapshot::SnapshotComponent projected{.id = component.id, .size = layout.size, .fields = {}};
        for (const kest::Field& piece : layout.fields) {
            if (entityPiece(game, component, piece) != nullptr) {
                if (piece.name.ends_with(".slot")) {
                    projected.fields.push_back({piece.offset, world_snapshot::FieldKind::Entity});
                }
                continue;
            }
            const std::optional<world_snapshot::FieldKind> kKind = snapshotKind(piece.kind);
            if (!kKind.has_value()) {
                persistence.unwritable = GameEntityField{.component = component.name, .field = piece.name};
                break;
            }
            projected.fields.push_back({piece.offset, *kKind});
        }
        if (!persistence.unwritable.has_value()) {
            persistence.projection.components.push_back(std::move(projected));
        }
    }
    persistence.save = declaredSave(game, layouts, game.save);
    persistence.playerSave = declaredSave(game, layouts, game.playerSave);
    return persistence;
}

} // namespace rawframe::world_kest
