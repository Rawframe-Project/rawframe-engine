#include "mod_handlers.h"

#include "rawframe/kest/doors.h"
#include "rawframe/world_kest/errors.h"

#include <algorithm>
#include <string>

namespace rawframe::world_kest {

namespace {

std::unexpected<result::Error> refused(std::string_view why, std::string_view mod, std::string_view name) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kWorldKestDomain, code(WorldKestError::ModRefused), why)
            .error()
            .withContext("mod", mod)
            .withContext("name", name)};
}

} // namespace

result::Result<std::vector<std::unique_ptr<KestSystems>>> modHandlers(const GameDescription& game,
                                                                      std::span<const kest::TypeLayout> layouts,
                                                                      const GameFiles& files,
                                                                      const kest::MachineLimits& limits) {
    std::vector<std::unique_ptr<KestSystems>> made;
    for (const GameModProgram& mod : files.modPrograms()) {
        std::string report;
        auto program = GameFiles::compileMod(mod, {}, &report);
        if (!program.has_value()) {
            return std::unexpected<result::Error>{
                refused("a mod's program does not compile", mod.mod, mod.entry).error().withContext("report", report)};
        }
        // Its types of the game's components are the game's shapes: the
        // World holds the game's values, and the mod reads and writes them.
        const auto kSameShape = [&](const std::string& name) -> result::Result<const GameComponent*> {
            const auto kComponent = std::ranges::find(game.components, name, &GameComponent::name);
            const kest::TypeLayout& expected = layouts[static_cast<std::size_t>(kComponent - game.components.begin())];
            const auto kLayout = (*program)->layout(kComponent->kestType);
            if (!kLayout.has_value() || kLayout->mark != expected.mark) {
                return refused("a mod's Kest type of a component is not shaped as the game's", mod.mod, name);
            }
            return &*kComponent;
        };
        // Each handler's columns and names, kept while the systems are made.
        std::vector<std::vector<KestColumn>> columns;
        std::vector<std::string> identities;
        std::vector<std::vector<std::string_view>> after;
        for (const ModHandler& handler : mod.handlers) {
            const auto kPoint = std::ranges::find(game.mods.points, handler.point, &GameExtensionPoint::name);
            std::vector<KestColumn>& handlerColumns = columns.emplace_back();
            RAWFRAME_TRY_ASSIGN(const GameComponent* read, kSameShape(kPoint->accepts));
            handlerColumns.push_back(KestColumn{.component = read->id, .element = read->kestType});
            for (const std::string& name : kPoint->writes) {
                RAWFRAME_TRY_ASSIGN(const GameComponent* written, kSameShape(name));
                handlerColumns.push_back(
                    KestColumn{.component = written->id, .element = written->kestType, .access = world::Access::Write});
            }
            identities.push_back(mod.mod + "/" + handler.point + "/" + handler.function);
            after.push_back({kPoint->after});
        }
        std::vector<KestSystemDeclaration> declarations;
        for (std::size_t at = 0; at < mod.handlers.size(); ++at) {
            declarations.push_back(KestSystemDeclaration{.identity = identities[at],
                                                         .phase = world::Phase::Simulation,
                                                         .entry = mod.handlers[at].function,
                                                         .columns = columns[at],
                                                         .after = after[at],
                                                         .before = {},
                                                         .randomStreams = {}});
        }
        kest::DoorTable doors;
        RAWFRAME_TRY(kest::addStandardMath(doors));
        auto systems = KestSystems::create(KestSystemsSettings{.program = std::move(*program),
                                                               .doors = std::move(doors),
                                                               .components = {},
                                                               .prefabs = {},
                                                               .limits = limits,
                                                               .trust = kest::Trust::Untrusted,
                                                               .systems = declarations});
        if (!systems.has_value()) {
            return std::unexpected<result::Error>{std::move(systems).error().withContext("mod", mod.mod)};
        }
        made.push_back(std::move(*systems));
    }
    return made;
}

} // namespace rawframe::world_kest
