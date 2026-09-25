#include "rawframe/audio/decode.h"
#include "rawframe/audio/mixer.h"
#include "rawframe/audio/output.h"
#include "rawframe/audio/sounds.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/content/catalog.h"
#include "rawframe/content/manifest.h"
#include "rawframe/content/source.h"
#include "rawframe/content/store.h"
#include "rawframe/world_audio/errors.h"
#include "rawframe/world_audio/registrar.h"
#include "rawframe/world_audio/sound_loader.h"
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
constexpr EventIdentity kPlaying{"audio", "playing_summary"};
constexpr EventIdentity kUnheard{"audio", "output_unavailable"};
constexpr EventIdentity kUnread{"audio", "sounds_unavailable"};
constexpr EventIdentity kUnreadSound{"audio", "sound_unavailable"};
constexpr EventIdentity kSoundReloaded{"audio", "sound_reloaded"};
constexpr EventIdentity kSoundNotReloaded{"audio", "sound_reload_failed"};
constexpr EventIdentity kContentReloaded{"content", "content_reloaded"};
constexpr EventIdentity kContentRefused{"content", "content_reload_refused"};
constexpr std::string_view kMaybe[] = {world_replication::kClientWorlds.name};
constexpr std::uint32_t kRecordingRate = 48'000;

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
    /// The game's cooked content, and its sounds read from it by identity;
    /// the loader goes first, the store after it.
    std::unique_ptr<content::ContentStore> store;
    std::unique_ptr<SoundLoader> loader;
    bool loaded = false;
    /// Set once its sounds could not be read: the World goes unheard.
    bool failed = false;
    std::uint64_t tick = 0;
    diagnostics::Emitter emitter;
    /// Every this many Host iterations, the manifest is read again and a
    /// changed one published as the next catalog; none when zero.
    std::uint64_t reloadEvery = 0;
    std::filesystem::path root;
    std::string manifestRead;
    std::uint64_t catalogGeneration = 1;

    /// Loads the game's audio for a mixer at `rate` and starts reading its
    /// sounds from the cooked content at `content.root`. `key` names the
    /// configuration that asked, for the refusal; `live` decodes streams on
    /// the executor.
    result::Status load(composition::ParticipantContext& context, std::string_view key, std::uint32_t rate, bool live) {
        const composition::Configuration& configuration = context.configuration();
        const auto kGame = configuration.text("kest.game");
        const auto kContent = configuration.text("content.root");
        if (!kGame.has_value() || !kContent.has_value() || !context.has(world_replication::kClientWorlds.name) ||
            context.cpuExecutor() == nullptr || context.blockingIoExecutor() == nullptr) {
            return std::unexpected<result::Error>{
                refuse(result::ErrorClass::FailedPrecondition,
                       WorldAudioError::NoAudio,
                       "hearing a World needs a game, its cooked content, executors, and a process with clients")
                    .error()
                    .withContext("key", std::string{key})};
        }
        RAWFRAME_TRY_ASSIGN(clients, context.capability(world_replication::kClientWorlds));
        RAWFRAME_TRY_ASSIGN(client, configuration.unsignedInteger("audio.client", 0));

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
        RAWFRAME_TRY_ASSIGN(GameAudio game, loadGameAudio(std::string{*kGame}, **program));
        const auto kMaster = std::ranges::find(game.layout.buses, audio::Role::Master, &audio::Bus::role);
        master = static_cast<std::size_t>(kMaster - game.layout.buses.begin());
        RAWFRAME_TRY_ASSIGN(mixer, audio::Mixer::create(game.layout, {.rate = rate}));
        execution::Executor* const kExecutor = live ? context.cpuExecutor() : nullptr;
        streamer = kExecutor != nullptr ? std::make_unique<audio::Streamer>(*kExecutor, context.owner())
                                        : std::make_unique<audio::Streamer>();
        RAWFRAME_TRY_ASSIGN(sounds, audio::Sounds::create(*mixer, game.layout, {.streamer = streamer.get()}));
        settings.emitter = game.emitter;
        settings.listener = game.listener;

        // The cooked content: its manifest is the catalog, its directory the
        // one source.
        root = std::filesystem::path{std::string{*kContent}};
        RAWFRAME_TRY_ASSIGN(reloadEvery, configuration.unsignedInteger("content.reload_every", 0));
        RAWFRAME_TRY_ASSIGN(content::ContentSource source, content::ContentSource::directory(root));
        std::vector<content::ContentSource> sources;
        sources.push_back(std::move(source));
        RAWFRAME_TRY_ASSIGN(
            store,
            content::ContentStore::create(
                *context.blockingIoExecutor(), context.owner(), context.scope(), context.clock(), std::move(sources)));
        manifestRead = readManifestText();
        RAWFRAME_TRY_ASSIGN(auto catalog, catalogOf(manifestRead, catalogGeneration));
        store->publish(std::move(catalog));
        RAWFRAME_TRY_ASSIGN(loader,
                            SoundLoader::create(*store,
                                                *context.cpuExecutor(),
                                                context.owner(),
                                                context.scope(),
                                                context.clock(),
                                                std::move(game.sounds)));
        return {};
    }

    [[nodiscard]] std::string readManifestText() const {
        std::ifstream file{root / "content.manifest", std::ios::binary};
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }

    [[nodiscard]] result::Result<std::shared_ptr<const content::ContentCatalog>>
    catalogOf(std::string_view text, std::uint64_t generation) const {
        auto manifest = content::readManifest(text);
        if (!manifest.has_value()) {
            return std::unexpected<result::Error>{
                std::move(manifest).error().withContext("path", (root / "content.manifest").string())};
        }
        const std::vector<content::BoundManifest> kManifests = {{.entries = std::move(*manifest), .source = 0}};
        return content::ContentCatalog::build(kManifests, soundRepresentations(), generation, generation);
    }

    /// Every `reloadEvery` Host iterations: a manifest that changed since
    /// it was last read becomes the next catalog, and the asset sets
    /// replace what changed; one that does not read is refused and the
    /// running catalog stays.
    void watch(std::uint64_t iteration) noexcept {
        if (reloadEvery == 0 || iteration % reloadEvery != 0 || store == nullptr) {
            return;
        }
        std::string text = readManifestText();
        if (text == manifestRead) {
            return;
        }
        manifestRead = std::move(text);
        auto catalog = catalogOf(manifestRead, catalogGeneration + 1);
        if (!catalog.has_value()) {
            emitter.log(diagnostics::Severity::Warning,
                        kContentRefused,
                        "a changed manifest was not published; the running catalog stays",
                        {diagnostics::field("reason", std::string{catalog.error().description()})});
            return;
        }
        ++catalogGeneration;
        store->publish(std::move(*catalog));
        emitter.log(diagnostics::Severity::Info,
                    kContentReloaded,
                    "a changed manifest was published as the next catalog",
                    {diagnostics::field("generation", catalogGeneration)});
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
                        {diagnostics::field("sound", settings.sounds[kSound].first),
                         diagnostics::field("reason", std::string{kError.description()})});
        }
        for (const std::size_t kSound : kServed.reloaded) {
            emitter.log(diagnostics::Severity::Info,
                        kSoundReloaded,
                        "a sound's variant was replaced by its new revision",
                        {diagnostics::field("sound", settings.sounds[kSound].first)});
        }
        for (const auto& [kSound, kError] : kServed.notReloaded) {
            emitter.log(diagnostics::Severity::Warning,
                        kSoundNotReloaded,
                        "a sound's new revision could not be used: the old one plays on",
                        {diagnostics::field("sound", settings.sounds[kSound].first),
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
        watch(frame.iteration);
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
        std::ofstream file{path_, std::ios::binary};
        file.write(reinterpret_cast<const char*>(kWave.data()), static_cast<std::streamsize>(kWave.size()));
        const WorldAudioStatistics kHeard = hearing_.statistics();
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
        // Sounds are read on the blocking-I/O executor, decoded on the CPU.
        .executor = {.cpu = true, .blockingIo = true, .quota = {.maximumPendingTasks = 64}},
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
        // Sounds are read on the blocking-I/O executor and decoded on the
        // CPU one, where streamed sounds also decode ahead, a task a stream.
        .executor = {.cpu = true, .blockingIo = true, .quota = {.maximumPendingTasks = 64}},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(500)},
        .observabilityIdentity = "world_audio.player",
        .budgetOwner = "audio",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::PresentationExtract),
    });
}

} // namespace rawframe::world_audio
