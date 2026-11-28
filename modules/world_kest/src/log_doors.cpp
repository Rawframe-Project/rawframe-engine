#include "rawframe/world_kest/log_doors.h"

#include "rawframe/composition/errors.h"

#include <algorithm>
#include <array>
#include <utility>

namespace rawframe::world_kest {

namespace {

constexpr std::array<kest::Parameter, 2> kWriteTakes = {kest::Parameter{kest::Slot::I32},
                                                        kest::Parameter{kest::Slot::Text}};
constexpr std::array<kest::Parameter, 3> kAboutTakes = {kest::Parameter{kest::Slot::I32},
                                                        kest::Parameter{kest::Slot::Value, kEntityType},
                                                        kest::Parameter{kest::Slot::Text}};
constexpr std::array<kest::Parameter, 1> kIoTakes = {kest::Parameter{kest::Slot::Text}};

/// `rawframe.log`'s levels, by place.
constexpr std::array<diagnostics::Severity, 4> kLevels = {diagnostics::Severity::Debug,
                                                          diagnostics::Severity::Info,
                                                          diagnostics::Severity::Warning,
                                                          diagnostics::Severity::Error};
constexpr std::array<std::string_view, 4> kLevelNames = {"debug", "info", "warning", "error"};

result::Status refuse(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument,
                        composition::kCompositionDomain,
                        code(composition::CompositionError::BadConfiguration),
                        why);
}

/// The longest start of `text` within `bytes` that ends on a whole UTF-8
/// character, as far as its own bytes say.
std::string_view cut(std::string_view text, std::size_t bytes) {
    if (text.size() <= bytes) {
        return text;
    }
    std::size_t end = bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) {
        --end;
    }
    return text.substr(0, end);
}

} // namespace

result::Result<LogSettings> readLogSettings(const composition::Configuration& configuration) {
    LogSettings settings;
    if (const auto kLevel = configuration.text("kest.log_level")) {
        const auto kFound = std::ranges::find(kLevelNames, *kLevel);
        if (kFound == kLevelNames.end()) {
            return std::unexpected<result::Error>{refuse("kest.log_level is debug, info, warning, or error").error()};
        }
        settings.least = kLevels[static_cast<std::size_t>(kFound - kLevelNames.begin())];
    }
    if (const auto kMuted = configuration.text("kest.log_mute")) {
        std::string_view rest = *kMuted;
        while (!rest.empty()) {
            const std::size_t kStart = rest.find_first_not_of(' ');
            if (kStart == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(kStart);
            const std::size_t kEnd = std::min(rest.find(' '), rest.size());
            settings.muted.emplace_back(rest.substr(0, kEnd));
            rest.remove_prefix(kEnd);
            if (settings.muted.size() > kMaximumMutedSystems) {
                return std::unexpected<result::Error>{refuse("kest.log_mute names at most 256 systems").error()};
            }
        }
    }
    return settings;
}

GameLog::GameLog(LogSettings settings) noexcept : settings_(std::move(settings)) {
}

result::Status GameLog::addDoors(kest::DoorTable& doors) {
    RAWFRAME_TRY(
        doors.add(kest::Door{.name = "Log.write", .function = &writeDoor, .context = this, .takes = kWriteTakes}));
    RAWFRAME_TRY(
        doors.add(kest::Door{.name = "Log.about", .function = &aboutDoor, .context = this, .takes = kAboutTakes}));
    return doors.add(kest::Door{.name = "Io.write", .function = &ioDoor, .context = this, .takes = kIoTakes});
}

void GameLog::begin(world::SystemContext& context) noexcept {
    emitter_ = context.emitter;
    system_.assign(context.system);
    tick_ = context.tick.value;
    muted_ = std::ranges::contains(settings_.muted, system_);
    runRecords_ = 0;
    runHeld_ = 0;
    runRepeats_ = 0;
    last_.clear();
    line_.clear();
}

void GameLog::end(bool /*kept*/) noexcept {
    // A run's records stand whether or not it succeeded: a failing run is
    // what they are most often for.
    flushLine();
    tellHeld();
    system_.clear();
    emitter_ = {};
}

void GameLog::write(diagnostics::Severity level, std::string_view message, const world::EntityHandle* about) {
    if (muted_ || level < settings_.least) {
        return;
    }
    const std::string_view kText = cut(message, kMaximumLogBytes);
    if (runRecords_ >= kMaximumLogRecordsPerRun) {
        ++runHeld_;
        ++held_;
        return;
    }
    if (level == lastLevel_ && kText == last_ && runRecords_ > 0) {
        ++runRepeats_;
        ++held_;
        return;
    }
    ++runRecords_;
    ++written_;
    last_.assign(kText);
    lastLevel_ = level;
    if (about != nullptr) {
        emitter_.log(level,
                     kGameLog,
                     "a game's log record",
                     {diagnostics::field("system", std::string_view{system_}),
                      diagnostics::field("tick", tick_),
                      diagnostics::field("text", kText),
                      diagnostics::field("entitySlot", about->slot),
                      diagnostics::field("entityGeneration", about->generation)});
    } else {
        emitter_.log(level,
                     kGameLog,
                     "a game's log record",
                     {diagnostics::field("system", std::string_view{system_}),
                      diagnostics::field("tick", tick_),
                      diagnostics::field("text", kText)});
    }
}

void GameLog::flushLine() {
    if (!line_.empty()) {
        const std::string kLine = std::exchange(line_, {});
        write(diagnostics::Severity::Info, kLine, nullptr);
    }
}

void GameLog::tellHeld() {
    if (runHeld_ != 0 || runRepeats_ != 0) {
        emitter_.log(diagnostics::Severity::Warning,
                     kGameLogHeld,
                     "a system's log records were held back: repeats of the one before, or past a run's bound",
                     {diagnostics::field("system", std::string_view{system_}),
                      diagnostics::field("tick", tick_),
                      diagnostics::field("repeated", runRepeats_),
                      diagnostics::field("overBound", runHeld_)});
    }
    runHeld_ = 0;
    runRepeats_ = 0;
}

void GameLog::writeDoor(kest::DoorCall& call, void* context) noexcept {
    auto& log = *static_cast<GameLog*>(context);
    const std::int64_t kLevel = call.integer(0);
    if (kLevel < 0 || static_cast<std::uint64_t>(kLevel) >= kLevels.size()) {
        call.fail("a log level is rawframe.log's DEBUG, INFO, WARNING, or ERROR");
        return;
    }
    log.write(kLevels[static_cast<std::size_t>(kLevel)], call.text(1), nullptr);
}

void GameLog::aboutDoor(kest::DoorCall& call, void* context) noexcept {
    auto& log = *static_cast<GameLog*>(context);
    const std::int64_t kLevel = call.integer(0);
    world::EntityHandle entity;
    if (kLevel < 0 || static_cast<std::uint64_t>(kLevel) >= kLevels.size()) {
        call.fail("a log level is rawframe.log's DEBUG, INFO, WARNING, or ERROR");
        return;
    }
    if (!call.value(1, std::as_writable_bytes(std::span{&entity, 1}))) {
        call.fail("the program's Entity is not the engine's");
        return;
    }
    log.write(kLevels[static_cast<std::size_t>(kLevel)], call.text(2), &entity);
}

void GameLog::ioDoor(kest::DoorCall& call, void* context) noexcept {
    auto& log = *static_cast<GameLog*>(context);
    // `print` writes its text and then a line's end: a line is a record.
    std::string_view rest = call.text(0);
    while (!rest.empty()) {
        const std::size_t kEnd = rest.find('\n');
        const std::string_view kPiece = rest.substr(0, kEnd);
        if (log.line_.size() < kMaximumLogBytes) {
            log.line_.append(cut(kPiece, kMaximumLogBytes - log.line_.size()));
        }
        if (kEnd == std::string_view::npos) {
            break;
        }
        log.flushLine();
        rest.remove_prefix(kEnd + 1);
    }
}

} // namespace rawframe::world_kest
