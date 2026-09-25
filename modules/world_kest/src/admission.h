#pragma once

// A game's own admission rule (ADR-0043): a function of the game's program,
// named by an `admission` line, asked about each client the engine would
// admit. rawframe.admission, the engine's Kest module, says its shape and
// what it answers.

#include "rawframe/kest/machine.h"
#include "rawframe/kest/program.h"
#include "rawframe/network/admission.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

class KestAdmission {
public:
    /// Starts a machine of its own over `program` and finds `entry`. The
    /// rule is called once with an empty session and ticket, so a function
    /// of the wrong shape is refused here, with Kest's report, rather than
    /// refusing every client later.
    [[nodiscard]] static result::Result<std::unique_ptr<KestAdmission>>
    create(std::shared_ptr<const kest::Program> program, std::string_view entry, const kest::MachineLimits& limits);

    /// On the Host thread: a rejection, or none to admit. A rule that fails
    /// or answers what rawframe.admission does not name refuses the client
    /// as unavailable, and is counted.
    [[nodiscard]] std::optional<network::Reject> admit(const network::Hello& hello) noexcept;

    /// Rule calls that failed or answered out of turn, and what the
    /// machine said about the last.
    [[nodiscard]] std::uint64_t failures() const noexcept {
        return failures_;
    }
    [[nodiscard]] const std::string& lastReport() const noexcept {
        return report_;
    }

private:
    KestAdmission(std::unique_ptr<kest::Machine> machine, kest::Entry entry) noexcept;

    /// The rule's answer for these bytes, or none if the call failed.
    std::optional<std::int64_t> ask(std::span<const std::byte> session, std::span<const std::byte> ticket);

    std::unique_ptr<kest::Machine> machine_;
    kest::Entry entry_;
    std::vector<kest::Value> frame_;
    /// Copies the program may write through; a lend is never of the hello.
    std::vector<std::uint8_t> session_;
    std::vector<std::uint8_t> ticket_;
    std::uint64_t failures_ = 0;
    std::string report_;
};

} // namespace rawframe::world_kest
