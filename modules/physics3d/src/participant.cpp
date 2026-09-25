#include "rawframe/composition/composition.h"
#include "rawframe/physics3d/physics.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/world_runtime/simulation.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

namespace rawframe::physics3d {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kSummary{"physics3d", "summary"};

constexpr std::string_view kNeeds[] = {world_runtime::kSimulation.name};
constexpr std::string_view kMaybe[] = {kPhysics3DPlan.name};

class WorldParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        RAWFRAME_TRY_ASSIGN(world_runtime::Simulation * simulation, context.capability(world_runtime::kSimulation));
        RAWFRAME_TRY_ASSIGN(plan_, context.capability(kPhysics3DPlan));
        if (!plan_->physics3d().has_value()) {
            plan_ = nullptr;
            return {};
        }
        RAWFRAME_TRY_ASSIGN(physics_, Physics3D::create(*plan_->physics3d()));
        RAWFRAME_TRY(simulation->addSystems(*physics_));
        plan_->attach(physics_.get());
        return {};
    }

    WorldParticipant() noexcept = default;
    WorldParticipant(const WorldParticipant&) = delete;
    WorldParticipant& operator=(const WorldParticipant&) = delete;
    ~WorldParticipant() override {
        if (plan_ != nullptr) {
            plan_->attach(nullptr);
        }
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void stop() noexcept override {
        if (physics_ == nullptr) {
            return;
        }
        const Physics3DStatistics kStatistics = physics_->statistics();
        // As sixteen hex digits: every bit, never a negative number.
        std::array<char, 16> digest{};
        const std::uint64_t kDigest = physics_->digest();
        for (std::size_t index = 0; index < digest.size(); ++index) {
            digest[index] = "0123456789abcdef"[(kDigest >> (4U * (15U - index))) & 0xFU];
        }
        emitter_.log(diagnostics::Severity::Info,
                     kSummary,
                     "3D physics totals",
                     {diagnostics::field("steps", kStatistics.steps),
                      diagnostics::field("bodiesMade", kStatistics.bodiesMade),
                      diagnostics::field("bodiesRemoved", kStatistics.bodiesRemoved),
                      diagnostics::field("bodiesRefused", kStatistics.bodiesRefused),
                      diagnostics::field("teleports", kStatistics.teleports),
                      diagnostics::field("velocitiesSet", kStatistics.velocitiesSet),
                      diagnostics::field("impulses", kStatistics.impulses),
                      diagnostics::field("targets", kStatistics.targets),
                      diagnostics::field("characterMoves", kStatistics.characterMoves),
                      diagnostics::field("jointsMade", kStatistics.jointsMade),
                      diagnostics::field("jointsRemoved", kStatistics.jointsRemoved),
                      diagnostics::field("jointsRefused", kStatistics.jointsRefused),
                      diagnostics::field("jointsBroken", kStatistics.jointsBroken),
                      diagnostics::field("attachmentsRefused", kStatistics.attachmentsRefused),
                      diagnostics::field("contactsBegun", kStatistics.contactsBegun),
                      diagnostics::field("overlapsBegun", kStatistics.overlapsBegun),
                      diagnostics::field("raysRewound", kStatistics.raysRewound),
                      diagnostics::field("rewindsClamped", kStatistics.rewindsClamped),
                      diagnostics::field("digest", std::string_view{digest.data(), digest.size()})});
    }

private:
    Physics3DPlan* plan_ = nullptr;
    std::unique_ptr<Physics3D> physics_;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> makeWorld(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<WorldParticipant>();
    if (context.has(kPhysics3DPlan.name)) {
        RAWFRAME_TRY(participant->load(context));
    }
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.physics3d.world",
        .factory = &makeWorld,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kNeeds,
        .optionalCapabilities = kMaybe,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "physics3d.world",
        .budgetOwner = "world",
    });
}

} // namespace rawframe::physics3d
