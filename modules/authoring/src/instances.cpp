// Whole instances added and removed (D156). Adding one is SPEC-0006's two
// passes: the source resolved and its entities enumerated, then each given
// its id here; there are no values to remap, since an instance's entities
// are its source's until its patch says otherwise.

#include "operation_parts.h"
#include "rawframe/base/sha256.h"

#include <algorithm>
#include <array>
#include <set>

namespace rawframe::authoring {

namespace {

constexpr std::string_view kIdDomain = "rawframe.scene instance entity";

void putBig(std::array<std::byte, 32>& bytes, std::size_t at, std::uint64_t value) {
    for (std::size_t each = 0; each < 8; ++each) {
        bytes[at + each] = static_cast<std::byte>(value >> (56 - 8 * each));
    }
}

std::uint64_t getBig(const base::Sha256Digest& digest, std::size_t at) {
    std::uint64_t made = 0;
    for (std::size_t each = 0; each < 8; ++each) {
        made = (made << 8U) | std::to_integer<std::uint64_t>(digest[at + each]);
    }
    return made;
}

/// Every scene `scene` reaches through instances, itself included, up to
/// the depth a resolution allows; what does not read is left to the
/// resolution to refuse.
std::set<base::Bits128> reachedFrom(base::Bits128 scene, const scene::SceneSource& sources) {
    std::set<base::Bits128> reached{scene};
    std::vector<base::Bits128> level{scene};
    for (std::size_t depth = 0; depth <= scene::kMaximumInstanceDepth && !level.empty(); ++depth) {
        std::vector<base::Bits128> next;
        for (const base::Bits128 kEach : level) {
            const auto kSource = sources(kEach);
            if (!kSource.has_value()) {
                continue;
            }
            for (const scene::SceneInstance& instance : kSource->instances) {
                if (reached.insert(instance.scene).second) {
                    next.push_back(instance.scene);
                }
            }
        }
        level = std::move(next);
    }
    return reached;
}

/// Whether the scene holds `id` in any way: its own entity, or one an
/// instance maps, removed or not.
bool holds(const scene::Scene& scene, base::Bits128 id) {
    return std::ranges::contains(scene.entities, id, &scene::SceneEntity::id) ||
           std::ranges::any_of(scene.instances, [id](const scene::SceneInstance& instance) {
               return std::ranges::contains(instance.entities, id, &scene::IdentityMapping::instance);
           });
}

result::Result<Journal> add(const scene::Scene& scene,
                            const Operation& operation,
                            const AddInstance& add,
                            const scene::SceneSource* sources,
                            base::Bits128 document) {
    if (add.instance == base::Bits128{}) {
        return invalid(operation, "an instantiation's identity is not nought");
    }
    if (sources == nullptr) {
        return notFound(operation, "no scenes are at hand to instance");
    }
    const auto kSource = (*sources)(add.scene);
    if (!kSource.has_value()) {
        return notFound(operation, "no scene of that identity is at hand");
    }
    if (document != base::Bits128{} && reachedFrom(add.scene, *sources).contains(document)) {
        return invalid(operation, "a scene does not instance itself, however far down");
    }
    const auto kResolved = scene::resolveInstances(*kSource, *sources);
    if (!kResolved.has_value()) {
        return std::unexpected<result::Error>{invalid(operation, "the source scene resolves")
                                                  .error()
                                                  .withContext("cause", kResolved.error().description())};
    }
    if (kResolved->entities.empty()) {
        return invalid(operation, "a scene with no entities is not instanced");
    }
    // First pass: the source's entities, in the order of their ids, and
    // theirs here.
    InstanceRecord made{
        .place = scene.instances.size(), .scene = add.scene, .entities = {}, .overrides = {}, .marks = {}};
    for (const scene::SceneEntity& entity : kResolved->entities) {
        made.entities.push_back(
            scene::IdentityMapping{.source = entity.id, .instance = instanceEntityId(add.instance, entity.id)});
    }
    std::ranges::sort(made.entities, {}, &scene::IdentityMapping::source);
    for (const scene::IdentityMapping& mapping : made.entities) {
        if (holds(scene, mapping.instance)) {
            return conflict(operation, "an instance's entity would take an id the scene holds");
        }
    }
    const base::Bits128 kNamed = made.entities.front().instance;
    return Journal{Delta{.kind = DeltaKind::CreateInstance, .entity = kNamed, .after = {.instance = std::move(made)}}};
}

result::Result<Journal> remove(const scene::Scene& scene, const Operation& operation, const RemoveInstance& remove) {
    const auto kFound = std::ranges::find_if(scene.instances, [&remove](const scene::SceneInstance& instance) {
        return std::ranges::contains(instance.entities, remove.entity, &scene::IdentityMapping::instance);
    });
    if (kFound == scene.instances.end()) {
        return notFound(operation, "no instance of the scene brings that entity");
    }
    const scene::SceneInstance& instance = *kFound;
    // Nothing outside the instance may name what it brings.
    std::set<base::Bits128> brought;
    for (const scene::IdentityMapping& mapping : instance.entities) {
        brought.insert(mapping.instance);
    }
    const auto kNamesBrought = [&brought](const std::vector<scene::SceneField>& fields) {
        return std::ranges::any_of(fields, [&brought](const scene::SceneField& field) {
            return field.value.kind == scene::FieldValue::Kind::Entity && brought.contains(field.value.entity);
        });
    };
    for (const scene::SceneEntity& entity : scene.entities) {
        for (const scene::SceneComponent& component : entity.components) {
            if (kNamesBrought(component.fields)) {
                return conflict(operation, "an instance whose entities another names is not removed");
            }
        }
    }
    for (const scene::SceneInstance& other : scene.instances) {
        if (&other == &instance) {
            continue;
        }
        for (const scene::Override& each : other.overrides) {
            if (kNamesBrought(each.fields)) {
                return conflict(operation, "an instance whose entities another names is not removed");
            }
        }
    }
    InstanceRecord made{.place = static_cast<std::size_t>(kFound - scene.instances.begin()),
                        .scene = instance.scene,
                        .entities = instance.entities,
                        .overrides = instance.overrides,
                        .marks = {}};
    for (const scene::Override& each : instance.overrides) {
        if (!each.component.empty() &&
            !std::ranges::contains(made.marks, each.component, &scene::SchemaMark::component)) {
            made.marks.push_back(scene::SchemaMark{.component = each.component, .mark = markOf(scene, each.component)});
        }
    }
    std::ranges::sort(made.marks, {}, &scene::SchemaMark::component);
    const base::Bits128 kNamed = made.entities.front().instance;
    return Journal{
        Delta{.kind = DeltaKind::DestroyInstance, .entity = kNamed, .before = {.instance = std::move(made)}}};
}

} // namespace

base::Bits128 instanceEntityId(base::Bits128 instance, base::Bits128 source) noexcept {
    std::array<std::byte, 32> ids{};
    putBig(ids, 0, instance.high);
    putBig(ids, 8, instance.low);
    putBig(ids, 16, source.high);
    putBig(ids, 24, source.low);
    base::Sha256 digest;
    digest.update(kIdDomain);
    digest.update(ids);
    const base::Sha256Digest kDigest = digest.finish();
    // RFC 9562's version 8 and variant bits.
    return base::Bits128{.high = (getBig(kDigest, 0) & ~0xF000ULL) | 0x8000ULL,
                         .low = (getBig(kDigest, 8) & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL};
}

result::Result<Journal> deriveInstance(const scene::Scene& scene,
                                       const Operation& operation,
                                       const scene::SceneSource* sources,
                                       base::Bits128 document) {
    if (const auto* adding = std::get_if<AddInstance>(&operation)) {
        return add(scene, operation, *adding, sources, document);
    }
    return remove(scene, operation, std::get<RemoveInstance>(operation));
}

} // namespace rawframe::authoring
