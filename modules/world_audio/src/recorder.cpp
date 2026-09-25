#include "rawframe/audio/decode.h"
#include "rawframe/audio/mixer.h"
#include "rawframe/audio/sounds.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/world_audio/errors.h"
#include "rawframe/world_audio/registrar.h"
#include "rawframe/world_audio/world_audio.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_replication/client_worlds.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace rawframe::world_audio {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kRecording{"audio", "recording_summary"};
constexpr std::string_view kMaybe[] = {world_replication::kClientWorlds.name};
constexpr std::uint32_t kRate = 48'000;

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, WorldAudioError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kWorldAudioDomain, code(error), why).error()};
}

/// Hears one client's mirrored World each frame, as its player would, and
/// writes what it heard to a WAVE file when the World stops.
class Recorder final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        const auto kPath = configuration.text("audio.record");
        if (!kPath.has_value()) {
            return {};
        }
        path_ = std::string{*kPath};
        const auto kGame = configuration.text("kest.game");
        if (!kGame.has_value() || !context.has(world_replication::kClientWorlds.name)) {
            return refuse(result::ErrorClass::FailedPrecondition,
                          WorldAudioError::NoAudio,
                          "audio.record needs a game and a process with clients");
        }
        RAWFRAME_TRY_ASSIGN(clients_, context.capability(world_replication::kClientWorlds));
        RAWFRAME_TRY_ASSIGN(client_, configuration.unsignedInteger("audio.record_client", 0));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kSeconds, configuration.unsignedInteger("audio.record_seconds", 60));
        limitFrames_ = kSeconds * kRate;

        // The game's program, for the layout check, and its audio.
        std::ifstream file{std::string{*kGame}, std::ios::binary};
        std::ostringstream text;
        text << file.rdbuf();
        RAWFRAME_TRY_ASSIGN(const world_kest::GameDescription kDescription, world_kest::parseGame(text.str()));
        kest::CompileSettings compile;
        if (const auto kLibrary = configuration.text("kest.library")) {
            compile.library = std::string{*kLibrary};
        }
        const std::string kProgram = (std::filesystem::path{*kGame}.parent_path() / kDescription.program).string();
        std::string report;
        auto program = kest::Program::compileFile(kProgram, compile, &report);
        if (!program.has_value()) {
            return std::unexpected<result::Error>{
                std::move(program).error().withContext("path", kProgram).withContext("report", report)};
        }
        RAWFRAME_TRY_ASSIGN(GameAudio loaded, loadGameAudio(std::string{*kGame}, **program));
        RAWFRAME_TRY_ASSIGN(mixer_, audio::Mixer::create(loaded.layout, {.rate = kRate}));
        RAWFRAME_TRY_ASSIGN(sounds_, audio::Sounds::create(*mixer_, loaded.layout, {}));
        for (auto& [kId, sound] : loaded.sounds) {
            RAWFRAME_TRY_ASSIGN(const std::size_t kIndex, sounds_->add(std::move(sound)));
            settings_.sounds.emplace_back(kId, kIndex);
        }
        settings_.emitter = loaded.emitter;
        settings_.listener = loaded.listener;
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void runHostPhase(composition::HostPhase phase, const composition::HostFrame& frame) noexcept override {
        if (phase != composition::HostPhase::PresentationExtract || clients_ == nullptr) {
            return;
        }
        const world_replication::ClientView kView = clients_->client(client_);
        if (kView.world == nullptr) {
            return;
        }
        if (heard_ == nullptr) {
            auto made = WorldAudio::create(kView.world->registry(), *sounds_, settings_);
            if (!made.has_value()) {
                clients_ = nullptr;
                return;
            }
            heard_ = std::move(*made);
            last_ = frame.now;
        }
        const double kSeconds = static_cast<double>((frame.now - *last_).nanoseconds) / 1e9;
        last_ = frame.now;
        heard_->bindListener(kView.owned.isNull() ? std::nullopt : std::optional{kView.owned});
        heard_->update(*kView.world, static_cast<float>(kSeconds));
        // Render what the frame's time holds, until the recording is full.
        owed_ += kSeconds * kRate;
        const auto kFrames = static_cast<std::size_t>(owed_);
        owed_ -= static_cast<double>(kFrames);
        const std::size_t kRoom = static_cast<std::size_t>(limitFrames_) - (recorded_.size() / 2);
        const std::size_t kTaken = std::min(kFrames, kRoom);
        if (kTaken != 0) {
            const std::size_t kAt = recorded_.size();
            recorded_.resize(kAt + (kTaken * 2));
            mixer_->render(std::span{recorded_}.subspan(kAt));
        }
    }

    void stop() noexcept override {
        if (mixer_ == nullptr) {
            return;
        }
        audio::Clip clip;
        clip.channels = 2;
        clip.rate = kRate;
        clip.samples = std::move(recorded_);
        float peak = 0;
        for (const float kSample : clip.samples) {
            peak = std::max(peak, std::abs(kSample));
        }
        const std::vector<std::byte> kWave = audio::encodeWav(clip);
        std::ofstream file{path_, std::ios::binary};
        file.write(reinterpret_cast<const char*>(kWave.data()), static_cast<std::streamsize>(kWave.size()));
        const WorldAudioStatistics kHeard = heard_ != nullptr ? heard_->statistics() : WorldAudioStatistics{};
        emitter_.log(diagnostics::Severity::Info,
                     kRecording,
                     "what one client heard",
                     {diagnostics::field("path", path_),
                      diagnostics::field("written", static_cast<bool>(file)),
                      diagnostics::field("frames", clip.frames()),
                      diagnostics::field("peak", static_cast<double>(peak)),
                      diagnostics::field("cues", kHeard.cues),
                      diagnostics::field("refused", kHeard.refused),
                      diagnostics::field("unknownSounds", kHeard.unknownSounds),
                      diagnostics::field("listenerConflicts", kHeard.listenerConflicts)});
    }

private:
    const world_replication::ClientWorlds* clients_ = nullptr;
    std::uint64_t client_ = 0;
    std::uint64_t limitFrames_ = 0;
    std::string path_;
    std::unique_ptr<audio::Mixer> mixer_;
    std::unique_ptr<audio::Sounds> sounds_;
    WorldAudioSettings settings_;
    std::unique_ptr<WorldAudio> heard_;
    std::optional<execution::MonotonicInstant> last_;
    double owed_ = 0;
    std::vector<float> recorded_;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> makeRecorder(composition::ParticipantContext& context) noexcept {
    auto recorder = std::make_unique<Recorder>();
    RAWFRAME_TRY(recorder->load(context));
    return composition::ParticipantOwner{recorder.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.world_audio.recorder",
        .factory = &makeRecorder,
        .scope = composition::LifetimeScope::World,
        .optionalCapabilities = kMaybe,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(500)},
        .observabilityIdentity = "world_audio.recorder",
        .budgetOwner = "audio",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::PresentationExtract),
    });
}

} // namespace rawframe::world_audio
