#include "game_scenes.h"

#include "rawframe/scene/resolve.h"
#include "rawframe/scene/scene.h"
#include "rawframe/world/persistent.h"
#include "rawframe/world_kest/errors.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <iterator>
#include <optional>

namespace rawframe::world_kest {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, WorldKestError error, std::string_view why) {
    return result::fail(errorClass, kWorldKestDomain, code(error), why);
}

/// Writes `text` as a value of `kind` at `into`. False when it does not parse
/// or does not fit.
bool writeField(kest::FieldKind kind, std::string_view text, std::byte* into) {
    const auto kParse = [text](auto& value) {
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size();
    };
    const auto kStore = [into](auto value) {
        std::memcpy(into, &value, sizeof value);
        return true;
    };
    const auto kWhole = [&]<typename T>(T) {
        T value{};
        return kParse(value) && kStore(value);
    };
    switch (kind) {
    case kest::FieldKind::I8:
        return kWhole(std::int8_t{});
    case kest::FieldKind::I16:
        return kWhole(std::int16_t{});
    case kest::FieldKind::I32:
        return kWhole(std::int32_t{});
    case kest::FieldKind::I64:
        return kWhole(std::int64_t{});
    case kest::FieldKind::U8:
        return kWhole(std::uint8_t{});
    case kest::FieldKind::U16:
        return kWhole(std::uint16_t{});
    case kest::FieldKind::U32:
        return kWhole(std::uint32_t{});
    case kest::FieldKind::U64:
        return kWhole(std::uint64_t{});
    case kest::FieldKind::F32:
        return kWhole(float{});
    case kest::FieldKind::F64:
        return kWhole(double{});
    case kest::FieldKind::Bool:
        // A bool is one byte, nought or one.
        if (text == "true" || text == "false") {
            return kStore(static_cast<std::uint8_t>(text == "true" ? 1 : 0));
        }
        return false;
    case kest::FieldKind::Other:
        return false;
    }
    return false;
}

} // namespace

const GameComponent* GameScenes::componentNamed(std::string_view name) const noexcept {
    for (const GameComponent& component : game_.components) {
        if (component.name == name) {
            return &component;
        }
    }
    // parseGame has checked every name, so this is not reached.
    return game_.components.data();
}

result::Result<SceneReference>
GameScenes::referenceIn(std::string_view component, std::string_view field, std::string_view scene) const {
    const bool kDeclared = std::ranges::any_of(game_.entityFields, [&](const GameEntityField& declared) {
        return declared.component == component && declared.field == field;
    });
    const GameComponent* const kComponent = componentNamed(component);
    const kest::TypeLayout& layout = layouts_[static_cast<std::size_t>(kComponent - game_.components.data())];
    const auto kPart = [&layout, field](std::string_view part) -> const kest::Field* {
        const std::string kName = std::string{field} + "." + std::string{part};
        const auto kFound = std::ranges::find(layout.fields, kName, &kest::Field::name);
        return kFound != layout.fields.end() && kFound->kind == kest::FieldKind::U32 ? &*kFound : nullptr;
    };
    const kest::Field* const kSlot = kPart("slot");
    const kest::Field* const kGeneration = kPart("generation");
    if (!kDeclared || kSlot == nullptr || kGeneration == nullptr) {
        return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                     WorldKestError::UnknownName,
                                                     "a scene's reference is a field the game declares with an "
                                                     "entity line, holding a rawframe.world Entity")
                                                  .error()
                                                  .withContext("scene", scene)
                                                  .withContext("name", field)};
    }
    return SceneReference{.component = kComponent->id, .slot = kSlot->offset, .generation = kGeneration->offset};
}

result::Result<SceneSpawns> GameScenes::sceneSpawns(const GameFiles& files, const std::string& path) const {
    RAWFRAME_TRY_ASSIGN(const std::string_view kText, files.scene(path));
    return sceneSpawns(files, kText, path);
}

result::Result<SceneSpawns>
GameScenes::sceneSpawns(const GameFiles& files, std::string_view sceneText, const std::string& path) const {
    const auto kRefuse = [&path](WorldKestError error, std::string_view why, std::string_view name) {
        return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument, error, why)
                                                  .error()
                                                  .withContext("scene", path)
                                                  .withContext("name", name)};
    };
    // Its instances resolved: what spawns is the scene's entities and
    // every entity its instances bring.
    auto read = scene::readScene(sceneText).and_then([&files](const scene::Scene& authored) {
        return scene::resolveInstances(authored, [&files](base::Bits128 source) {
            return files.sceneById(source).and_then([](std::string_view text) {
                return scene::readScene(text);
            });
        });
    });
    if (!read.has_value()) {
        return std::unexpected<result::Error>{std::move(read).error().withContext("scene", path)};
    }
    for (const scene::SchemaMark& mark : read->schema) {
        const auto kComponent = std::ranges::find(game_.components, mark.component, &GameComponent::name);
        if (kComponent == game_.components.end()) {
            return kRefuse(
                WorldKestError::UnknownName, "a scene names a component the game does not declare", mark.component);
        }
        const std::size_t kIndex = static_cast<std::size_t>(kComponent - game_.components.begin());
        if (layouts_[kIndex].mark != mark.mark) {
            return kRefuse(WorldKestError::BadGameLine,
                           "a scene was authored against another layout of a component",
                           mark.component);
        }
    }
    SceneSpawns made;
    const auto kPlaceOf = [&read](base::Bits128 entity) {
        return static_cast<std::size_t>(std::ranges::find(read->entities, entity, &scene::SceneEntity::id) -
                                        read->entities.begin());
    };
    for (const scene::SceneEntity& entity : read->entities) {
        GameSpawn spawn{.count = 1, .components = {}};
        for (const scene::SceneComponent& component : entity.components) {
            GameSpawnComponent part{.component = component.name, .fields = {}};
            for (const scene::SceneField& field : component.fields) {
                if (field.value.kind == scene::FieldValue::Kind::Entity) {
                    RAWFRAME_TRY_ASSIGN(SceneReference reference, referenceIn(component.name, field.name, path));
                    reference.spawn = made.spawns.size();
                    reference.target = kPlaceOf(field.value.entity);
                    made.references.push_back(std::move(reference));
                    continue;
                }
                part.fields.push_back(GameFieldValue{.field = field.name,
                                                     .value = field.value.kind == scene::FieldValue::Kind::True
                                                                  ? std::string{"true"}
                                                                  : field.value.number});
            }
            spawn.components.push_back(std::move(part));
        }
        made.spawns.push_back(std::move(spawn));
        made.ids.push_back(entity.id);
    }
    return made;
}

result::Status GameScenes::addScenes(const GameFiles& files,
                                     std::vector<GameSpawn>& spawns,
                                     std::vector<SceneReference>& references) const {
    for (const std::string& path : game_.scenes) {
        RAWFRAME_TRY_ASSIGN(SceneSpawns scene, sceneSpawns(files, path));
        RAWFRAME_TRY(namePersistent(files, path, scene));
        const std::size_t kFirst = spawns.size();
        for (SceneReference& reference : scene.references) {
            reference.spawn += kFirst;
            reference.target += kFirst;
            references.push_back(reference);
        }
        std::ranges::move(scene.spawns, std::back_inserter(spawns));
    }
    return {};
}

result::Status GameScenes::addModScenes(const GameFiles& files, std::vector<GameSpawn>& spawns) const {
    for (const GameModScene& contributed : files.modScenes()) {
        const std::string kLabel = contributed.mod + ":" + contributed.point;
        const auto kPoint = std::ranges::find(game_.mods.points, contributed.point, &GameExtensionPoint::name);
        RAWFRAME_TRY_ASSIGN(const SceneSpawns kScene, sceneSpawns(files, contributed.text, kLabel));
        const bool kOnlyThePoints =
            kPoint != game_.mods.points.end() && kScene.references.empty() &&
            std::ranges::all_of(kScene.spawns, [&kPoint](const GameSpawn& spawn) {
                return spawn.components.size() == 1 && spawn.components[0].component == kPoint->accepts;
            });
        if (!kOnlyThePoints) {
            return std::unexpected<result::Error>{
                refuse(result::ErrorClass::InvalidArgument,
                       WorldKestError::ModRefused,
                       "every entity a mod contributes holds the point's component and nothing else")
                    .error()
                    .withContext("mod", contributed.mod)
                    .withContext("point", contributed.point)};
        }
        std::ranges::copy(kScene.spawns, std::back_inserter(spawns));
    }
    return {};
}

result::Status GameScenes::namePersistent(const GameFiles& files, const std::string& path, SceneSpawns& scene) const {
    const std::optional<base::Bits128> kScene = files.sceneIdentity(path);
    for (std::size_t index = 0; index < scene.spawns.size(); ++index) {
        const auto kPart = std::ranges::find(
            scene.spawns[index].components, world::Persistent::kComponentName, &GameSpawnComponent::component);
        if (kPart == scene.spawns[index].components.end()) {
            continue;
        }
        if (!kScene.has_value()) {
            return std::unexpected<result::Error>{
                refuse(result::ErrorClass::InvalidArgument,
                       WorldKestError::BadGameLine,
                       "a scene with persistent entities needs a resource identity, from its sidecar")
                    .error()
                    .withContext("scene", path)};
        }
        const world::PersistentEntityId kId = world::persistentFromSource(*kScene, scene.ids[index]);
        kPart->fields = {GameFieldValue{.field = "high", .value = std::to_string(kId.value.high)},
                         GameFieldValue{.field = "low", .value = std::to_string(kId.value.low)}};
    }
    return {};
}

result::Status GameScenes::addPrefabs(const GameFiles& files, std::vector<KestPrefab>& prefabs) const {
    for (const GamePrefab& declared : game_.prefabs) {
        RAWFRAME_TRY_ASSIGN(const SceneSpawns kScene, sceneSpawns(files, declared.path));
        KestPrefab& prefab = prefabs.emplace_back();
        prefab.id = declared.id;
        for (std::size_t index = 0; index < kScene.spawns.size(); ++index) {
            RAWFRAME_TRY_ASSIGN(const SpawnValues kValues, spawnValues(kScene.spawns[index]));
            KestPrefab::Entity& entity = prefab.entities.emplace_back();
            for (const auto& [kComponent, kBytes] : kValues) {
                entity.parts.push_back(KestPrefab::Part{.component = kComponent, .value = kBytes, .references = {}});
            }
            for (const SceneReference& reference : kScene.references) {
                if (reference.spawn != index) {
                    continue;
                }
                // A program's Entity is its slot and then its generation.
                if (reference.generation != reference.slot + sizeof(std::uint32_t)) {
                    return refuse(result::ErrorClass::InvalidArgument,
                                  WorldKestError::BadGameLine,
                                  "a prefab's reference is an Entity laid out as rawframe.world's");
                }
                const auto kPart = std::ranges::find(entity.parts, reference.component, &KestPrefab::Part::component);
                kPart->references.push_back(
                    KestPrefab::Reference{.offset = reference.slot, .target = reference.target});
            }
        }
    }
    return {};
}

result::Result<SpawnValues> GameScenes::spawnValues(const GameSpawn& spawn) const {
    SpawnValues values;
    for (const GameSpawnComponent& part : spawn.components) {
        const std::size_t kIndex = static_cast<std::size_t>(componentNamed(part.component) - game_.components.data());
        const kest::TypeLayout& layout = layouts_[kIndex];
        std::vector<std::byte> bytes(layout.size);
        for (GameFieldValue value : part.fields) {
            value.value = spawnValue(game_, part.component, value);
            const kest::Field* field = nullptr;
            for (const kest::Field& candidate : layout.fields) {
                field = candidate.name == value.field ? &candidate : field;
            }
            if (field == nullptr || !writeField(field->kind, value.value, bytes.data() + field->offset)) {
                return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                             WorldKestError::UnknownName,
                                                             "a spawn names a field its component lacks, or "
                                                             "gives a value that does not fit it")
                                                          .error()
                                                          .withContext("field", value.field)};
            }
        }
        values.emplace_back(game_.components[kIndex].id, std::move(bytes));
    }
    return values;
}

} // namespace rawframe::world_kest
