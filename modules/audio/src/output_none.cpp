// Where the platform gives this module no device: the web, until Maul Audio
// brings one (D174). Opening refuses as a machine without a device does, and
// a client runs on without sound; nothing else can be reached.
#include "rawframe/audio/errors.h"
#include "rawframe/audio/output.h"

namespace rawframe::audio {

struct Output::State {};

Output::Output(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Output::~Output() = default;

result::Result<std::unique_ptr<Output>> Output::open(const OutputSettings&) {
    return std::unexpected<result::Error>{result::fail(result::ErrorClass::Unavailable,
                                                       kAudioDomain,
                                                       code(AudioError::NoDevice),
                                                       "this platform gives no audio device yet")
                                              .error()};
}

std::uint32_t Output::rate() const noexcept {
    return 0;
}

std::string Output::backendName() const {
    return "none";
}

result::Status Output::start(Mixer&) {
    return std::unexpected<result::Error>{result::fail(result::ErrorClass::Unavailable,
                                                       kAudioDomain,
                                                       code(AudioError::NoDevice),
                                                       "this platform gives no audio device yet")
                                              .error()};
}

void Output::stop() noexcept {
}

OutputState Output::state() const noexcept {
    return OutputState::Stopped;
}

OutputStatistics Output::statistics() const noexcept {
    return {};
}

} // namespace rawframe::audio
