#include "rawframe/audio/decode.h"
#include "rawframe/audio/mixer.h"
#include "rawframe/audio/output.h"
#include "rawframe/audio/sounds.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/content/source.h"
#include "rawframe/content/store.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/world_audio/errors.h"
#include "rawframe/world_audio/registrar.h"
#include "rawframe/world_audio/sound_loader.h"
#include "rawframe/world_audio/world_audio.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/client_worlds.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#if RAWFRAME_FILE_SYSTEM
#include <fstream>
#endif

namespace rawframe::world_audio {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kRecording{"audio", "recording_summary"};
constexpr EventIdentity kPlaying{"audio", "playing_summary"};
constexpr EventIdentity kUnheard{"audio", "output_unavailable"};
constexpr EventIdentity kUnread{"audio", "sounds_unavailable"};
constexpr EventIdentity kUnreadSound{"audio", "sound_unavailable"};
constexpr EventIdentity kSoundReloaded{"audio", "sound_reloaded"};
constexpr EventIdentity kSoundNotReloaded{"audio", "sound_reload_failed"};
constexpr std::string_view kMaybe[] = {
    world_replication::kClientWorlds.name, game_content::kGameContent.name, world_kest::kGameFiles.name};
constexpr std::uint32_t kRecordingRate = 48'000;

/// A sound's identity as its game writes it, 16 lowercase hexadecimal
/// digits: a log's integers are signed, and JSON readers round past 2^53.
std::string identityText(std::uint64_t id) {
    std::array<char, 16> digits{};
    const char* const kEnd = std::to_chars(digits.data(), digits.data() + digits.size(), id, 16).ptr;
    std::string text(digits.size() - static_cast<std::size_t>(kEnd - digits.data()), '0');
    text.append(std::string_view{digits.data(), kEnd});
    return text;
}

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, WorldAudioError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kWorldAudioDomain, code(error), why).error()};
}

/// One client's mirrored World heard as its player would hear it: the
/// game's sounds on a mixer at a rate, the listener bound to the player.
struct Hearing {
    const world_replication::ClientWorlds* clients = nullptr;
    std::uint64_t client = 0;
    std::unique_ptr<audio::Mixer> mixer;
    /// Keeps streamed sounds decoded ahead: on the participant's CPU
    /// executor when it plays live, on the frame's own thread when it
    /// renders there.
    std::unique_ptr<audio::Streamer> streamer;
    std::unique_ptr<audio::Sounds> sounds;
    std::size_t master = 0;
    WorldAudioSettings settings;
    std::unique_ptr<WorldAudio> heard;
    std::optional<execution::MonotonicInstant> last;
    /// The game's sounds, read by identity from the Runtime's cooked
    /// content, which outlives them.
    std::unique_ptr<SoundLoader> loader;
    bool loaded = false;
    /// Set once its sounds could not be read: the World goes unheard.
    bool failed = false;
    std::uint64_t tick = 0;
    diagnostics::Emitter emitter;

    /// Loads the game's audio for a mixer at `rate` and starts reading its
    /// sounds from the Runtime's cooked content (`rawframe.content.game`). `key` names the
    /// configuration that asked, for the refusal; `live` decodes streams on
    /// the executor.
    result::Status load(composition::ParticipantContext& context, std::string_view key, std::uint32_t rate, bool live) {
        const composition::Configuration& configuration = context.configuration();
        const world_kest::GameFiles* files = nullptr;
        if (context.has(world_kest::kGameFiles.name)) {
            RAWFRAME_TRY_ASSIGN(files, context.capability(world_kest::kGameFiles));
        }
        if (files == nullptr || !files->named() || !context.has(world_replication::kClientWorlds.name) ||
            !context.has(game_content::kGameContent.name) || context.cpuExecutor() == nullptr) {
            return std::unexpected<result::Error>{
                refuse(result::ErrorClass::FailedPrecondition,
                       WorldAudioError::NoAudio,
                       "hearing a World needs a game, its cooked content, a CPU executor, and a process with clients")
                    .error()
                    .withContext("key", std::string{key})};
        }
        RAWFRAME_TRY_ASSIGN(clients, context.capability(world_replication::kClientWorlds));
        RAWFRAME_TRY_ASSIGN(client, configuration.unsignedInteger("audio.client", 0));

        // The game's program, for the layout check, and its audio.
        std::string report;
        auto program = files->compile(files->description().program, {}, &report);
        if (!program.has_value()) {
            return std::unexpected<result::Error>{std::move(program)
                                                      .error()
                                                      .withContext("program", files->description().program)
                                                      .withContext("report", report)};
        }
        RAWFRAME_TRY_ASSIGN(GameAudio game, loadGameAudio(*files, **program));
        const auto kMaster = std::ranges::find(game.layout.buses, audio::Role::Master, &audio::Bus::role);
        master = static_cast<std::size_t>(kMaster - game.layout.buses.begin());
        RAWFRAME_TRY_ASSIGN(mixer, audio::Mixer::create(game.layout, {.rate = rate}));
        execution::Executor* const kExecutor = live ? context.cpuExecutor() : nullptr;
        streamer = kExecutor != nullptr ? std::make_unique<audio::Streamer>(*kExecutor, context.owner())
                                        : std::make_unique<audio::Streamer>();
        RAWFRAME_TRY_ASSIGN(sounds, audio::Sounds::create(*mixer, game.layout, {.streamer = streamer.get()}));
        settings.emitter = game.emitter;
        settings.listener = game.listener;

        // The game's cooked content, admitting sound clips.
        RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
        const std::vector<content::AdmittedRepresentation> kSounds = soundRepresentations();
        RAWFRAME_TRY(content->admit(kSounds));
        RAWFRAME_TRY_ASSIGN(loader,
                            SoundLoader::create(content->store(),
                                                *context.cpuExecutor(),
                                                context.owner(),
                                                context.scope(),
                                                context.clock(),
                                                std::move(game.sounds)));
        return {};
    }

    /// Reads the sounds until those not on demand are all there, then
    /// reads on-demand ones as they are first played; false before then, or
    /// for good once they cannot be.
    bool ready() noexcept {
        if (failed) {
            return false;
        }
        auto done = loader->update(tick);
        if (!done.has_value()) {
            return fail(done.error());
        }
        if (!loaded) {
            if (!*done) {
                return false;
            }
            auto made = loader->sounds(tick);
            if (!made.has_value()) {
                return fail(made.error());
            }
            for (auto& [kId, sound] : *made) {
                auto index = sounds->add(std::move(sound));
                if (!index.has_value()) {
                    return fail(index.error());
                }
                settings.sounds.emplace_back(kId, *index);
            }
            loaded = true;
        }
        demand();
        return true;
    }

    /// Asks for the on-demand sounds played since the last frame, and
    /// supplies the variants that arrived.
    void demand() noexcept {
        const Served kServed = loader->serve(*sounds, tick);
        for (const auto& [kSound, kError] : kServed.unread) {
            emitter.log(diagnostics::Severity::Warning,
                        kUnreadSound,
                        "an on-demand sound could not be read: it goes unheard",
                        {diagnostics::field("sound", identityText(settings.sounds[kSound].first)),
                         diagnostics::field("reason", std::string{kError.description()})});
        }
        for (const std::size_t kSound : kServed.reloaded) {
            emitter.log(diagnostics::Severity::Info,
                        kSoundReloaded,
                        "a sound's variant was replaced by its new revision",
                        {diagnostics::field("sound", identityText(settings.sounds[kSound].first))});
        }
        for (const auto& [kSound, kError] : kServed.notReloaded) {
            emitter.log(diagnostics::Severity::Warning,
                        kSoundNotReloaded,
                        "a sound's new revision could not be used: the old one plays on",
                        {diagnostics::field("sound", identityText(settings.sounds[kSound].first)),
                         diagnostics::field("reason", std::string{kError.description()})});
        }
    }

    bool fail(const result::Error& error) noexcept {
        failed = true;
        emitter.log(diagnostics::Severity::Warning,
                    kUnread,
                    "the game's sounds could not be read: the World goes unheard",
                    {diagnostics::field("reason", std::string{error.description()})});
        return false;
    }

    /// Hears the client's World at this frame; the seconds since the last
    /// frame heard, or none before its World exists and its sounds are read.
    std::optional<double> hear(const composition::HostFrame& frame) noexcept {
        ++tick;
        if (clients == nullptr || !ready()) {
            return std::nullopt;
        }
        const world_replication::ClientView kView = clients->client(client);
        if (kView.world == nullptr) {
            return std::nullopt;
        }
        if (heard == nullptr) {
            auto made = WorldAudio::create(kView.world->registry(), *sounds, settings);
            if (!made.has_value()) {
                clients = nullptr;
                return std::nullopt;
            }
            heard = std::move(*made);
            last = frame.now;
        }
        const double kSeconds = static_cast<double>((frame.now - *last).nanoseconds) / 1e9;
        last = frame.now;
        heard->bindListener(kView.owned.isNull() ? std::nullopt : std::optional{kView.owned});
        heard->update(*kView.world, static_cast<float>(kSeconds));
        return kSeconds;
    }

    [[nodiscard]] WorldAudioStatistics statistics() const noexcept {
        return heard != nullptr ? heard->statistics() : WorldAudioStatistics{};
    }
};

/// Hears one client's World and writes what it heard to a WAVE file when
/// the World stops.
class Recorder final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        const auto kPath = configuration.text("audio.record");
        if (!kPath.has_value()) {
            return {};
        }
#if !RAWFRAME_FILE_SYSTEM
        // A recording is a file, and there are none here.
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::FailedPrecondition,
                                                           composition::kCompositionDomain,
                                                           code(composition::CompositionError::BadConfiguration),
                                                           "audio.record names a file, and there are none here")
                                                  .error()};
#endif
        path_ = std::string{*kPath};
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kSeconds, configuration.unsignedInteger("audio.record_seconds", 60));
        limitFrames_ = kSeconds * kRecordingRate;
        return hearing_.load(context, "audio.record", kRecordingRate, false);
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        hearing_.emitter = emitter_;
        return {};
    }

    void runHostPhase(composition::HostPhase phase, const composition::HostFrame& frame) noexcept override {
        if (phase != composition::HostPhase::PresentationExtract) {
            return;
        }
        const std::optional<double> kSeconds = hearing_.hear(frame);
        if (!kSeconds.has_value()) {
            return;
        }
        // Render what the frame's time holds, until the recording is full.
        owed_ += *kSeconds * kRecordingRate;
        const auto kFrames = static_cast<std::size_t>(owed_);
        owed_ -= static_cast<double>(kFrames);
        const std::size_t kRoom = static_cast<std::size_t>(limitFrames_) - (recorded_.size() / 2);
        const std::size_t kTaken = std::min(kFrames, kRoom);
        if (kTaken != 0) {
            const std::size_t kAt = recorded_.size();
            recorded_.resize(kAt + (kTaken * 2));
            hearing_.mixer->render(std::span{recorded_}.subspan(kAt));
        }
    }

    void stop() noexcept override {
        if (hearing_.mixer == nullptr) {
            return;
        }
        audio::Clip clip;
        clip.channels = 2;
        clip.rate = kRecordingRate;
        clip.samples = std::move(recorded_);
        float peak = 0;
        for (const float kSample : clip.samples) {
            peak = std::max(peak, std::abs(kSample));
        }
        const std::vector<std::byte> kWave = audio::encodeWav(clip);
#if RAWFRAME_FILE_SYSTEM
        std::ofstream file{path_, std::ios::binary};
        file.write(reinterpret_cast<const char*>(kWave.data()), static_cast<std::streamsize>(kWave.size()));
        const bool kWritten = static_cast<bool>(file);
#else
        const bool kWritten = false;
#endif
        const WorldAudioStatistics kHeard = hearing_.statistics();
        emitter_.log(diagnostics::Severity::Info,
                     kRecording,
                     "what one client heard",
                     {diagnostics::field("path", path_),
                      diagnostics::field("written", kWritten),
                      diagnostics::field("frames", clip.frames()),
                      diagnostics::field("peak", static_cast<double>(peak)),
                      diagnostics::field("cues", kHeard.cues),
                      diagnostics::field("refused", kHeard.refused),
                      diagnostics::field("unknownSounds", kHeard.unknownSounds),
                      diagnostics::field("listenerConflicts", kHeard.listenerConflicts)});
    }

private:
    Hearing hearing_;
    std::uint64_t limitFrames_ = 0;
    std::string path_;
    double owed_ = 0;
    std::vector<float> recorded_;
    diagnostics::Emitter emitter_;
};

/// Hears one client's World and plays it on an output device, whose own
/// thread renders the mixer. A machine without a device runs on unheard.
class Player final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        const auto kPlay = configuration.text("audio.play");
        if (!kPlay.has_value()) {
            return {};
        }
        audio::OutputSettings settings;
        if (*kPlay == "null") {
            settings.backend = audio::OutputBackend::Null;
        } else if (*kPlay != "device") {
            return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                         WorldAudioError::NoAudio,
                                                         "audio.play is `device` or `null`")
                                                      .error()
                                                      .withContext("value", std::string{*kPlay})};
        }
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kPeriod, configuration.unsignedInteger("audio.play_period", 256));
        settings.periodFrames = static_cast<std::uint32_t>(std::min<std::uint64_t>(kPeriod, 1U << 16U));
        auto output = audio::Output::open(settings);
        if (!output.has_value()) {
            unavailable_ = std::string{output.error().description()};
            return {};
        }
        output_ = std::move(*output);
        RAWFRAME_TRY(hearing_.load(context, "audio.play", output_->rate(), true));
        return output_->start(*hearing_.mixer);
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        hearing_.emitter = emitter_;
        if (unavailable_.has_value()) {
            emitter_.log(diagnostics::Severity::Warning,
                         kUnheard,
                         "no output device: the World goes unheard",
                         {diagnostics::field("reason", *unavailable_)});
        }
        return {};
    }

    void runHostPhase(composition::HostPhase phase, const composition::HostFrame& frame) noexcept override {
        if (phase != composition::HostPhase::PresentationExtract || output_ == nullptr) {
            return;
        }
        if (!hearing_.hear(frame).has_value()) {
            return;
        }
        peak_ = std::max(
            {peak_, hearing_.mixer->meter(hearing_.master).peakLeft, hearing_.mixer->meter(hearing_.master).peakRight});
        if (!lostReported_ && output_->state() == audio::OutputState::Lost) {
            lostReported_ = true;
            emitter_.log(diagnostics::Severity::Warning,
                         kUnheard,
                         "the output device stopped: sound is suspended",
                         {diagnostics::field("backend", output_->backendName())});
        }
    }

    void stop() noexcept override {
        if (output_ == nullptr) {
            return;
        }
        const audio::OutputState kState = output_->state();
        output_->stop();
        const audio::OutputStatistics kOutput = output_->statistics();
        const WorldAudioStatistics kHeard = hearing_.statistics();
        emitter_.log(diagnostics::Severity::Info,
                     kPlaying,
                     "what one client played",
                     {diagnostics::field("backend", output_->backendName()),
                      diagnostics::field("rate", static_cast<std::uint64_t>(output_->rate())),
                      diagnostics::field("lost", kState == audio::OutputState::Lost),
                      diagnostics::field("callbacks", kOutput.callbacks),
                      diagnostics::field("frames", kOutput.frames),
                      diagnostics::field("longestCallbackNanoseconds", kOutput.longestCallbackNanoseconds),
                      diagnostics::field("peak", static_cast<double>(peak_)),
                      diagnostics::field("cues", kHeard.cues),
                      diagnostics::field("refused", kHeard.refused)});
    }

private:
    // The output is declared after what it renders, so it stops first.
    Hearing hearing_;
    std::unique_ptr<audio::Output> output_;
    std::optional<std::string> unavailable_;
    float peak_ = 0;
    bool lostReported_ = false;
    diagnostics::Emitter emitter_;
};

template <typename Kind>
result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<Kind>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.world_audio.recorder",
        .factory = &make<Recorder>,
        .scope = composition::LifetimeScope::World,
        .optionalCapabilities = kMaybe,
        // Sounds are decoded on the CPU executor.
        .executor = {.cpu = true, .quota = {.maximumPendingTasks = 64}},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(500)},
        .observabilityIdentity = "world_audio.recorder",
        .budgetOwner = "audio",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::PresentationExtract),
    });
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.world_audio.player",
        .factory = &make<Player>,
        .scope = composition::LifetimeScope::World,
        .optionalCapabilities = kMaybe,
        // Sounds are decoded on the CPU executor, where streamed sounds also
        // decode ahead, a task a stream.
        .executor = {.cpu = true, .quota = {.maximumPendingTasks = 64}},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(500)},
        .observabilityIdentity = "world_audio.player",
        .budgetOwner = "audio",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::PresentationExtract),
    });
}

} // namespace rawframe::world_audio
