#include "admission.h"

#include "rawframe/world_kest/errors.h"

#include <algorithm>

namespace rawframe::world_kest {

namespace {

// rawframe.admission's answers.
constexpr std::int64_t kAdmitted = 0;

std::optional<network::RejectReason> reasonFor(std::int64_t answer) noexcept {
    switch (answer) {
    case static_cast<std::int64_t>(network::RejectReason::TicketInvalid):
        return network::RejectReason::TicketInvalid;
    case static_cast<std::int64_t>(network::RejectReason::Capacity):
        return network::RejectReason::Capacity;
    case static_cast<std::int64_t>(network::RejectReason::Unavailable):
        return network::RejectReason::Unavailable;
    default:
        return std::nullopt;
    }
}

} // namespace

KestAdmission::KestAdmission(std::unique_ptr<kest::Machine> machine, kest::Entry entry) noexcept
    : machine_(std::move(machine)), entry_(entry), frame_(entry.frameSlots) {
}

result::Result<std::unique_ptr<KestAdmission>> KestAdmission::create(std::shared_ptr<const kest::Program> program,
                                                                     std::string_view entry,
                                                                     const kest::MachineLimits& limits) {
    const kest::DoorTable kNoDoors;
    RAWFRAME_TRY_ASSIGN(std::unique_ptr<kest::Machine> machine,
                        kest::Machine::start(std::move(program), kNoDoors, kest::Trust::Trusted, limits));
    RAWFRAME_TRY_ASSIGN(const kest::Entry kEntry, machine->entry(entry));
    if (kEntry.frameSlots != 2) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kWorldKestDomain,
                            code(WorldKestError::EntryMismatch),
                            "an admission rule takes a session and a ticket, each [u8], and answers a u32");
    }
    std::unique_ptr<KestAdmission> made{new KestAdmission(std::move(machine), kEntry)};
    if (!made->ask({}, {}).has_value()) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kWorldKestDomain,
                            code(WorldKestError::EntryMismatch),
                            made->machine_->takeReport());
    }
    return made;
}

std::optional<std::int64_t> KestAdmission::ask(std::span<const std::byte> session, std::span<const std::byte> ticket) {
    // A lend needs somewhere to point even when there is nothing to lend.
    session_.assign(1, 0);
    ticket_.assign(1, 0);
    session_.resize(std::max<std::size_t>(1, session.size()));
    ticket_.resize(std::max<std::size_t>(1, ticket.size()));
    std::ranges::transform(session, session_.begin(), [](std::byte value) {
        return std::to_integer<std::uint8_t>(value);
    });
    std::ranges::transform(ticket, ticket_.begin(), [](std::byte value) {
        return std::to_integer<std::uint8_t>(value);
    });
    auto lentSession =
        machine_->lend(session_.data(), static_cast<std::uint32_t>(session.size()), "u8", sizeof(std::uint8_t));
    if (!lentSession.has_value()) {
        return std::nullopt;
    }
    auto lentTicket =
        machine_->lend(ticket_.data(), static_cast<std::uint32_t>(ticket.size()), "u8", sizeof(std::uint8_t));
    if (!lentTicket.has_value()) {
        machine_->endLend(*lentSession);
        return std::nullopt;
    }
    std::ranges::fill(frame_, kest::Value{});
    frame_[0] = *lentSession;
    frame_[1] = *lentTicket;
    auto outcome = machine_->call(entry_, frame_);
    machine_->endLend(*lentTicket);
    machine_->endLend(*lentSession);
    if (outcome.isCancelled() || outcome.isError()) {
        return std::nullopt;
    }
    return frame_[0].integer;
}

std::optional<network::Reject> KestAdmission::admit(const network::Hello& hello) noexcept {
    const auto kAnswer = ask(hello.requestedSession, hello.ticket);
    if (kAnswer == kAdmitted) {
        return std::nullopt;
    }
    if (kAnswer.has_value()) {
        if (const auto kReason = reasonFor(*kAnswer)) {
            return network::Reject{.reason = *kReason, .message = {}};
        }
    }
    ++failures_;
    report_ = kAnswer.has_value() ? "the rule answered what rawframe.admission does not name" : machine_->takeReport();
    return network::Reject{.reason = network::RejectReason::Unavailable, .message = {}};
}

} // namespace rawframe::world_kest
