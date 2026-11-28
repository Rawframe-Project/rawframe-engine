// Where this module opens no device: the web, whose page plays a client's
// sound from what its frames render. Opening refuses as a machine
// without a device does; nothing else can be reached.
#include "rawframe/audio/errors.h"
#include "rawframe/audio/output.h"

namespace rawframe::audio {

namespace {

std::unexpected<result::Error> noDevice() {
    return std::unexpected<result::Error>{result::fail(result::ErrorClass::Unavailable,
                                                       kAudioDomain,
                                                       code(AudioError::NoDevice),
                                                       "this platform gives no audio device yet")
                                              .error()};
}

} // namespace

struct Output::State {};

Output::Output(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Output::~Output() = default;

result::Result<std::unique_ptr<Output>> Output::open(const OutputSettings&) {
    return noDevice();
}

std::uint32_t Output::rate() const noexcept {
    return 0;
}

std::string Output::backendName() const {
    return "none";
}

result::Status Output::start(Mixer&) {
    return noDevice();
}

void Output::stop() noexcept {
}

OutputState Output::state() const noexcept {
    return OutputState::Stopped;
}

OutputStatistics Output::statistics() const noexcept {
    return {};
}

result::Result<std::vector<OutputDevice>> Output::devices() const {
    return std::vector<OutputDevice>{};
}

result::Status Output::choose(std::string_view) {
    return noDevice();
}

std::string Output::chosen() const {
    return {};
}

std::string Output::device() const {
    return {};
}

bool Output::follow() {
    return false;
}

result::Status Output::plug(std::string_view, std::string_view) {
    return noDevice();
}

result::Status Output::unplug(std::string_view) {
    return noDevice();
}

result::Status Output::makeDefault(std::string_view) {
    return noDevice();
}

} // namespace rawframe::audio
