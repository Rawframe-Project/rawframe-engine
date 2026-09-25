#include "checkpoints.h"

#include "rawframe/composition/composition.h"
#include "rawframe/world_runtime/checkpoint.h"
#include "rawframe/world_runtime/errors.h"
#include "rawframe/world_runtime/simulation.h"
#include "rawframe/world_snapshot/checkpoint.h"

#include <array>
#include <charconv>
#include <cstdio>
#include <string>
#include <vector>

namespace rawframe::world_runtime {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kCaptured{"world_runtime", "checkpoint_captured"};
constexpr EventIdentity kRestored{"world_runtime", "checkpoint_restored"};
constexpr EventIdentity kCaptureFailed{"world_runtime", "checkpoint_capture_failed"};

constexpr std::string_view kNeeds[] = {kSimulation.name};
constexpr std::string_view kMaybe[] = {kCheckpointPlan.name};

std::unexpected<result::Error> failed(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kWorldRuntimeDomain, code(WorldRuntimeError::CheckpointFailed), why);
}

std::string hex(const world_snapshot::Fingerprint& digest) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    for (const std::byte kByte : digest) {
        text.push_back(kDigits[std::to_integer<unsigned>(kByte) >> 4U]);
        text.push_back(kDigits[std::to_integer<unsigned>(kByte) & 0xFU]);
    }
    return text;
}

result::Result<std::vector<std::byte>> readArtifact(const std::string& path, std::size_t maximum) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return failed("the checkpoint to restore cannot be opened");
    }
    std::vector<std::byte> bytes;
    std::array<std::byte, 65536> chunk{};
    std::size_t got = 0;
    while ((got = std::fread(chunk.data(), 1, chunk.size(), file)) != 0 && bytes.size() <= maximum) {
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
    }
    const bool kFailed = std::ferror(file) != 0;
    std::fclose(file);
    if (kFailed || bytes.size() > maximum) {
        return failed("the checkpoint to restore cannot be read, or is larger than the snapshot profile allows");
    }
    return bytes;
}

/// Writes beside the target and renames, so a checkpoint is at its path
/// only once it is whole.
result::Status writeArtifact(const std::string& path, std::span<const std::byte> bytes) {
    const std::string kPartial = path + ".partial";
    std::FILE* file = std::fopen(kPartial.c_str(), "wb");
    if (file == nullptr) {
        return failed("the checkpoint cannot be written");
    }
    const bool kWritten = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    if (std::fclose(file) != 0 || !kWritten || std::rename(kPartial.c_str(), path.c_str()) != 0) {
        std::remove(kPartial.c_str());
        return failed("the checkpoint cannot be written");
    }
    return {};
}

/// Restores a checkpoint before the first tick, and captures at exact ticks.
class Checkpoints final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        restorePath_ = std::string{configuration.text("checkpoint.restore").value_or("")};
        capturePrefix_ = std::string{configuration.text("checkpoint.capture_prefix").value_or("")};
        std::string_view ticks = configuration.text("checkpoint.capture_ticks").value_or("");
        while (!ticks.empty()) {
            const std::size_t kSpace = ticks.find(' ');
            const std::string_view kWord = ticks.substr(0, kSpace);
            std::uint64_t tick = 0;
            const auto [end, error] = std::from_chars(kWord.data(), kWord.data() + kWord.size(), tick);
            if (error != std::errc{} || end != kWord.data() + kWord.size() ||
                (!captureTicks_.empty() && tick <= captureTicks_.back())) {
                return failed("checkpoint.capture_ticks is increasing tick numbers separated by spaces");
            }
            captureTicks_.push_back(tick);
            ticks = kSpace == std::string_view::npos ? std::string_view{} : ticks.substr(kSpace + 1);
        }
        if (captureTicks_.empty() != capturePrefix_.empty()) {
            return failed("checkpoint.capture_ticks and checkpoint.capture_prefix go together");
        }
        if (restorePath_.empty() && captureTicks_.empty()) {
            return {};
        }
        if (!context.has(kCheckpointPlan.name)) {
            return failed("checkpoints need a plan of what persists, which a game provides");
        }
        RAWFRAME_TRY_ASSIGN(simulation_, context.capability(kSimulation));
        RAWFRAME_TRY_ASSIGN(plan_, context.capability(kCheckpointPlan));
        RAWFRAME_TRY_ASSIGN(projection_, plan_->projection());
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        if (simulation_ == nullptr) {
            return {};
        }
        if (!restorePath_.empty()) {
            RAWFRAME_TRY(restore());
        }
        holdNext();
        return {};
    }

    /// Between ticks: a capture due now happens, then the next one is held for.
    void runHostPhase(composition::HostPhase, const composition::HostFrame&) noexcept override {
        if (simulation_ == nullptr || next_ >= captureTicks_.size() ||
            simulation_->tick().value != captureTicks_[next_]) {
            return;
        }
        const auto kStatus = capture();
        if (!kStatus.has_value()) {
            emitter_.log(diagnostics::Severity::Error,
                         kCaptureFailed,
                         "a checkpoint was not captured",
                         {diagnostics::field("tick", simulation_->tick().value),
                          diagnostics::field("error", kStatus.error().description())});
        }
        ++next_;
        holdNext();
    }

private:
    void holdNext() noexcept {
        // Ticks already past are not waited for: a restore may start later.
        while (next_ < captureTicks_.size() && captureTicks_[next_] < simulation_->tick().value) {
            ++next_;
        }
        simulation_->holdAt(next_ < captureTicks_.size() ? std::optional{world::TickIndex{captureTicks_[next_]}}
                                                         : std::nullopt);
    }

    result::Status restore() {
        RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kBytes,
                            readArtifact(restorePath_, limits_.maximumArtifactBytes));
        RAWFRAME_TRY_ASSIGN(std::unique_ptr<world::World> candidate, simulation_->candidate());
        RAWFRAME_TRY_ASSIGN(const world_snapshot::CheckpointFacts kFacts,
                            world_snapshot::restore(kBytes, *projection_, plan_->identity(), limits_, *candidate));
        const world::TickRate kRate = simulation_->rate();
        // Both reduced: equal rates are equal fractions.
        if (static_cast<std::uint64_t>(kFacts.rate.ticks) * kRate.seconds !=
            static_cast<std::uint64_t>(kRate.ticks) * kFacts.rate.seconds) {
            return failed("the checkpoint was taken at another tick rate");
        }
        simulation_->replace(std::move(candidate), kFacts.tick);
        emitter_.log(diagnostics::Severity::Info,
                     kRestored,
                     "the World was restored from a checkpoint",
                     {diagnostics::field("tick", kFacts.tick.value),
                      diagnostics::field("entities", kFacts.entities),
                      diagnostics::field("rows", kFacts.rows),
                      diagnostics::field("digest", std::string_view{hex(kFacts.digest)})});
        return {};
    }

    result::Status capture() {
        const world::TickIndex kTick = simulation_->tick();
        RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kBytes,
                            world_snapshot::capture(*simulation_->world(),
                                                    *projection_,
                                                    world_snapshot::CaptureSettings{.tick = kTick,
                                                                                    .rate = simulation_->rate(),
                                                                                    .identity = plan_->identity(),
                                                                                    .limits = limits_}));
        // The SnapshotDigest: everything before the 128-byte footer.
        const world_snapshot::Fingerprint kDigest = base::sha256(std::span{kBytes}.first(kBytes.size() - 128));
        const std::string kPath = capturePrefix_ + std::to_string(kTick.value) + ".rfsn";
        RAWFRAME_TRY(writeArtifact(kPath, kBytes));
        emitter_.log(diagnostics::Severity::Info,
                     kCaptured,
                     "a checkpoint was captured",
                     {diagnostics::field("tick", kTick.value),
                      diagnostics::field("bytes", kBytes.size()),
                      diagnostics::field("digest", std::string_view{hex(kDigest)})});
        return {};
    }

    Simulation* simulation_ = nullptr;
    const CheckpointPlan* plan_ = nullptr;
    const world_snapshot::SnapshotProjection* projection_ = nullptr;
    world_snapshot::SnapshotLimits limits_;
    diagnostics::Emitter emitter_;
    std::string restorePath_;
    std::string capturePrefix_;
    std::vector<std::uint64_t> captureTicks_;
    std::size_t next_ = 0;
};

result::Result<composition::ParticipantOwner> makeCheckpoints(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<Checkpoints>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerCheckpoints(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.world_runtime.checkpoints",
        .factory = &makeCheckpoints,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kNeeds,
        .optionalCapabilities = kMaybe,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "world_runtime.checkpoints",
        .budgetOwner = "world",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Maintenance),
    });
}

} // namespace rawframe::world_runtime
