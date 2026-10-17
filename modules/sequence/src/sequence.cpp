#include "rawframe/sequence/sequence.h"

#include "rawframe/sequence/errors.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>

namespace rawframe::sequence {

namespace {

/// The most a loop is passed through in one advance; past it, the play
/// lands where it would without firing more.
constexpr std::size_t kMostPasses = 64;

std::unexpected<result::Error> refused(std::size_t line, std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument,
                        kSequenceDomain,
                        code(SequenceError::BadSource),
                        "sequence line " + std::to_string(line) + ": " + std::string{why});
}

/// The line's words, split on spaces and tabs, with a comment left out.
std::vector<std::string_view> wordsOf(std::string_view line) {
    if (const std::size_t kComment = line.find('#'); kComment != std::string_view::npos) {
        line = line.substr(0, kComment);
    }
    std::vector<std::string_view> words;
    std::size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) {
            ++at;
        }
        const std::size_t kStart = at;
        while (at < line.size() && line[at] != ' ' && line[at] != '\t' && line[at] != '\r') {
            ++at;
        }
        if (at > kStart) {
            words.push_back(line.substr(kStart, at - kStart));
        }
    }
    return words;
}

std::optional<double> secondsOf(std::string_view word) {
    double value = 0;
    const auto [kEnd, kError] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (kError != std::errc{} || kEnd != word.data() + word.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

/// Exactly 16 lowercase hex digits.
std::optional<std::uint64_t> identityOf(std::string_view word) {
    if (word.size() != 16 || !std::ranges::all_of(word, [](char each) {
            return (each >= '0' && each <= '9') || (each >= 'a' && each <= 'f');
        })) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    std::from_chars(word.data(), word.data() + word.size(), value, 16);
    return value;
}

/// A machine name: a lowercase letter, then lowercase letters, digits, and
/// underscores.
bool isMachineName(std::string_view word) {
    return !word.empty() && word.front() >= 'a' && word.front() <= 'z' && std::ranges::all_of(word, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_';
    });
}

std::string secondsText(double value) {
    std::array<char, 32> text{};
    const auto [kEnd, kWritten] = std::to_chars(text.data(), text.data() + text.size(), value);
    return std::string{text.data(), kEnd};
}

std::string identityText(std::uint64_t value) {
    std::array<char, 16> text{};
    text.fill('0');
    std::array<char, 16> digits{};
    const auto [kEnd, kWritten] = std::to_chars(digits.data(), digits.data() + digits.size(), value, 16);
    const auto kCount = static_cast<std::size_t>(kEnd - digits.data());
    std::copy(digits.data(), kEnd, text.data() + (16 - kCount));
    return std::string{text.data(), text.size()};
}

/// The keys from `from` up to `to`, or through it where `through`, in
/// order.
void passKeys(const Sequence& sequence, double from, double to, bool through, std::vector<std::size_t>& passed) {
    const auto kFirst = std::ranges::lower_bound(sequence.keys, from, {}, &Key::time);
    for (auto each = kFirst; each != sequence.keys.end() && (each->time < to || (through && each->time == to));
         ++each) {
        passed.push_back(static_cast<std::size_t>(each - sequence.keys.begin()));
    }
}

} // namespace

result::Result<Sequence> parseSequence(std::string_view text, const SequenceLimits& limits) {
    Sequence made;
    bool headed = false;
    bool lasting = false;
    std::map<std::uint64_t, std::string> names;
    std::map<std::string, std::uint64_t, std::less<>> identities;
    std::size_t line = 0;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t kEnd = std::min(text.find('\n', at), text.size());
        const std::vector<std::string_view> kWords = wordsOf(text.substr(at, kEnd - at));
        at = kEnd + 1;
        ++line;
        if (kWords.empty()) {
            continue;
        }
        if (!headed) {
            if (kWords.size() != 2 || kWords[0] != "sequence" || kWords[1] != "1") {
                return refused(line, "a sequence begins `sequence 1`");
            }
            headed = true;
        } else if (kWords[0] == "duration" && kWords.size() == 2 && !lasting && made.keys.empty()) {
            const auto kDuration = secondsOf(kWords[1]);
            if (!kDuration.has_value() || *kDuration < limits.leastDuration || *kDuration > limits.mostDuration) {
                return refused(line,
                               "a duration is " + secondsText(limits.leastDuration) + " to " +
                                   secondsText(limits.mostDuration) + " seconds");
            }
            made.duration = *kDuration;
            lasting = true;
        } else if (kWords[0] == "loop" && kWords.size() == 1 && !made.loop && made.keys.empty()) {
            made.loop = true;
        } else if (kWords[0] == "track" && kWords.size() == 2 && made.keys.empty()) {
            if (!isMachineName(kWords[1]) || std::ranges::find(made.tracks, kWords[1]) != made.tracks.end() ||
                made.tracks.size() == limits.mostTracks) {
                return refused(line,
                               "a track is a machine name of its own, at most " + std::to_string(limits.mostTracks));
            }
            made.tracks.emplace_back(kWords[1]);
        } else if ((kWords[0] == "play" || kWords[0] == "event") && kWords.size() == 4 && lasting) {
            const auto kTime = secondsOf(kWords[1]);
            if (!kTime.has_value() || *kTime < 0 || *kTime > made.duration || (made.loop && *kTime == made.duration) ||
                (!made.keys.empty() && *kTime < made.keys.back().time)) {
                return refused(line, "keys are in time order from nought, within the duration (before it, in a loop)");
            }
            if (made.keys.size() == limits.mostKeys) {
                return refused(line, "a sequence has at most " + std::to_string(limits.mostKeys) + " keys");
            }
            Key key{.time = *kTime};
            if (kWords[0] == "play") {
                const auto kTrack = std::ranges::find(made.tracks, kWords[2]);
                const auto kAnimator = identityOf(kWords[3]);
                if (kTrack == made.tracks.end() || !kAnimator.has_value()) {
                    return refused(line, "a play key is `play <seconds> <track> <animator>`, of a track named before");
                }
                key.track = static_cast<std::uint32_t>(kTrack - made.tracks.begin());
                key.animator = *kAnimator;
            } else {
                const auto kEvent = identityOf(kWords[2]);
                if (!kEvent.has_value() || !isMachineName(kWords[3])) {
                    return refused(line, "an event key is `event <seconds> <16 hex digits> <machine name>`");
                }
                const auto kNamed = names.find(*kEvent);
                const auto kIdentified = identities.find(kWords[3]);
                if ((kNamed != names.end() && kNamed->second != kWords[3]) ||
                    (kIdentified != identities.end() && kIdentified->second != *kEvent)) {
                    return refused(line, "an event's identity and name are one to each other");
                }
                names.emplace(*kEvent, std::string{kWords[3]});
                identities.emplace(std::string{kWords[3]}, *kEvent);
                key.kind = KeyKind::Event;
                key.event = *kEvent;
                key.name = std::string{kWords[3]};
            }
            made.keys.push_back(std::move(key));
        } else {
            return refused(line,
                           "a sequence's lines are duration and loop, once, then tracks, then keys, `play <seconds> "
                           "<track> <animator>` and `event <seconds> <identity> <name>`");
        }
    }
    if (!headed || !lasting) {
        return refused(line, "a sequence has a duration");
    }
    return made;
}

std::string writeSequence(const Sequence& sequence) {
    std::string made = "sequence 1\nduration " + secondsText(sequence.duration) + "\n";
    if (sequence.loop) {
        made += "loop\n";
    }
    for (const std::string& kTrack : sequence.tracks) {
        made += "track " + kTrack + "\n";
    }
    for (const Key& kKey : sequence.keys) {
        if (kKey.kind == KeyKind::Play) {
            made += "play " + secondsText(kKey.time) + " " + sequence.tracks.at(kKey.track) + " " +
                    identityText(kKey.animator) + "\n";
        } else {
            made += "event " + secondsText(kKey.time) + " " + identityText(kKey.event) + " " + kKey.name + "\n";
        }
    }
    return made;
}

Advance advance(const Sequence& sequence, double time, double seconds) {
    // A time outside the sequence is held to it, or in a loop wrapped into
    // it, and a step back is none.
    seconds = seconds > 0 ? seconds : 0;
    time = time > 0 ? time : 0;
    if (sequence.loop && time >= sequence.duration) {
        time = std::fmod(time, sequence.duration);
    }
    Advance made{.time = time};
    if (!sequence.loop) {
        if (time >= sequence.duration) {
            made.time = sequence.duration;
            made.ended = true;
            return made;
        }
        const double kTo = time + seconds;
        made.ended = kTo >= sequence.duration;
        made.time = std::min(kTo, sequence.duration);
        passKeys(sequence, time, made.time, made.ended, made.keys);
        return made;
    }
    double from = time;
    double left = seconds;
    for (std::size_t pass = 0; pass < kMostPasses; ++pass) {
        if (from + left < sequence.duration) {
            passKeys(sequence, from, from + left, false, made.keys);
            made.time = from + left;
            return made;
        }
        passKeys(sequence, from, sequence.duration, false, made.keys);
        left -= sequence.duration - from;
        from = 0;
    }
    made.time = std::fmod(left, sequence.duration);
    return made;
}

} // namespace rawframe::sequence
