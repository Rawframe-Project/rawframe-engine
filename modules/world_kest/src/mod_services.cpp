#include "mod_services.h"

#include "rawframe/world_kest/errors.h"

#include <algorithm>

namespace rawframe::world_kest {

namespace {

void serve(kest::DoorCall& call, void* context) noexcept {
    ModServices::Service& service = *static_cast<ModServices::Service*>(context);
    if (!call.value(0, service.value)) {
        call.fail("a service's value is not the point's type");
        return;
    }
    call.spendFuel(service.value.size());
    if (service.machine != nullptr) {
        // The provider works on a copy, which comes back only if its call
        // succeeded.
        service.lent = service.value;
        auto lent = service.machine->lend(service.lent.data(), 1, service.kestType, service.lent.size());
        bool answered = false;
        if (lent.has_value()) {
            service.frame[0] = *lent;
            const auto kOutcome = service.machine->call(service.entry, service.frame);
            service.machine->endLend(*lent);
            answered = !kOutcome.isCancelled() && !kOutcome.isError();
        }
        if (answered) {
            service.value = service.lent;
        } else {
            ++service.failures;
        }
    }
    static_cast<void>(call.answerValue(service.value));
}

} // namespace

ModServices::ModServices(const GameDescription& game, std::span<const std::size_t> sizes) {
    for (const GameExtensionPoint& point : game.mods.points) {
        if (point.kind != GameExtensionPoint::Kind::Service) {
            continue;
        }
        const auto kComponent = std::ranges::find(game.components, point.accepts, &GameComponent::name);
        auto& service = *services_.emplace_back(std::make_unique<Service>());
        service.door = "Mods." + point.name;
        service.point = point.name;
        service.kestType = kComponent->kestType;
        service.takes = {kest::Parameter{kest::Slot::Value, service.kestType}};
        service.gives = {kest::Parameter{kest::Slot::Value, service.kestType}};
        service.value.resize(sizes[static_cast<std::size_t>(kComponent - game.components.begin())]);
    }
}

result::Status ModServices::addDoors(kest::DoorTable& doors) {
    for (const std::unique_ptr<Service>& service : services_) {
        RAWFRAME_TRY(doors.add(kest::Door{.name = service->door,
                                          .function = &serve,
                                          .context = service.get(),
                                          .takes = service->takes,
                                          .gives = service->gives}));
    }
    return {};
}

result::Status ModServices::bind(std::span<const GameModProgram> programs,
                                 std::span<const std::unique_ptr<KestSystems>> machines) {
    for (std::size_t at = 0; at < programs.size() && at < machines.size(); ++at) {
        for (const ModProvider& provider : programs[at].providers) {
            const auto kService = std::ranges::find(services_, provider.point, [](const auto& each) {
                return each->point;
            });
            if (kService == services_.end()) {
                continue;
            }
            Service& service = **kService;
            kest::Machine& machine = machines[at]->machine();
            RAWFRAME_TRY_ASSIGN(service.entry, machine.entry(provider.function));
            service.machine = &machine;
            service.frame.assign(std::max<std::size_t>(service.entry.frameSlots, 1), kest::Value{.integer = 0});
        }
    }
    return {};
}

std::uint64_t ModServices::failures() const noexcept {
    std::uint64_t total = 0;
    for (const std::unique_ptr<Service>& service : services_) {
        total += service->failures;
    }
    return total;
}

} // namespace rawframe::world_kest
