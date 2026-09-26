#pragma once

// Seeded mutations for holding a reader against hostile input (ADR-0084):
// a real record, one to four edits at a time, the same edits on every run
// and every target, so a failure found once is found again.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rawframe::test {

class Mutations {
public:
    /// The next of the xorshift sequence.
    std::uint64_t next() noexcept {
        state_ ^= state_ << 13U;
        state_ ^= state_ >> 7U;
        state_ ^= state_ << 17U;
        return state_;
    }

    /// `seed` with one to four edits: a byte replaced by any byte, a run of
    /// up to eight erased, one of `inserted` inserted, or, when `lines` is
    /// not empty, one of them inserted at the start of a line.
    std::string mutate(std::string_view seed, std::string_view inserted, std::span<const std::string_view> lines = {}) {
        std::string text{seed};
        const int kEdits = 1 + static_cast<int>(next() % 4);
        for (int edit = 0; edit < kEdits && !text.empty(); ++edit) {
            const auto kAt = static_cast<std::size_t>(next() % text.size());
            switch (next() % (lines.empty() ? 3 : 4)) {
            case 0:
                text[kAt] = static_cast<char>(next() & 0xFFU);
                break;
            case 1:
                text.erase(kAt, 1 + static_cast<std::size_t>(next() % 8));
                break;
            case 2:
                text.insert(kAt, 1, inserted[static_cast<std::size_t>(next() % inserted.size())]);
                break;
            default: {
                const std::size_t kLine = text.find('\n', kAt);
                text.insert(kLine == std::string::npos ? text.size() : kLine + 1,
                            lines[static_cast<std::size_t>(next() % lines.size())]);
                break;
            }
            }
        }
        return text;
    }

private:
    std::uint64_t state_ = 0x9E3779B97F4A7C15ULL;
};

/// What a run of `readOnlyAsWritten` saw.
struct WrittenRun {
    std::size_t read = 0;
    /// Texts read that `write` did not give back byte for byte.
    std::size_t differing = 0;
};

/// `rounds` mutations of `written`, the canonical text of a document, each
/// handed to `read`; whatever is read must be what `write` makes of it. A
/// reader of one written form holds when `differing` is nought.
template <typename Read, typename Write>
WrittenRun
readOnlyAsWritten(std::string_view written, std::string_view inserted, Read read, Write write, int rounds = 5'000) {
    Mutations mutations;
    WrittenRun run;
    for (int round = 0; round < rounds; ++round) {
        const std::string kText = mutations.mutate(written, inserted);
        const auto kRead = read(kText);
        if (!kRead.has_value()) {
            continue;
        }
        ++run.read;
        run.differing += write(*kRead) == kText ? 0 : 1;
    }
    return run;
}

} // namespace rawframe::test
