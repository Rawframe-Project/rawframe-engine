#pragma once

#include "rawframe/composition/declaration.h"

#include <cstdint>
#include <string_view>

namespace rawframe::composition {

namespace detail {
struct Collector;
}

/// What a registrar receives: the one way to contribute participants. It
/// cannot be copied or moved, speaks only for the module whose registrar is
/// running, and closes when that registrar returns (SPEC-0049 Part 2).
class ParticipantRegistrar {
public:
    ParticipantRegistrar(detail::Collector& collector,
                         std::string_view owningModule,
                         std::uint8_t allowedScopes) noexcept
        : collector_(&collector), owningModule_(owningModule), allowedScopes_(allowedScopes) {
    }
    ParticipantRegistrar(const ParticipantRegistrar&) = delete;
    ParticipantRegistrar& operator=(const ParticipantRegistrar&) = delete;
    ParticipantRegistrar(ParticipantRegistrar&&) = delete;
    ParticipantRegistrar& operator=(ParticipantRegistrar&&) = delete;

    /// Copies the declaration into the plan being built. Never fails here:
    /// every problem is recorded and reported after all registrars ran.
    void submit(const ParticipantDeclaration& declaration) noexcept;

    [[nodiscard]] std::string_view owningModule() const noexcept {
        return owningModule_;
    }

    /// Called by composition when the registrar returns. A later submit through
    /// a retained reference records a problem and changes nothing.
    void close() noexcept {
        open_ = false;
    }

private:
    detail::Collector* collector_;
    std::string_view owningModule_;
    std::uint8_t allowedScopes_;
    bool open_ = true;
};

/// A module's single registration entry point. Deterministic and effect-free:
/// it only submits declarations. It constructs nothing, starts nothing, and
/// reads nothing but its argument (ADR-0068).
using RegistrarFunction = void (*)(ParticipantRegistrar& registrar) noexcept;

[[nodiscard]] constexpr std::uint8_t scopeBit(LifetimeScope scope) noexcept {
    return static_cast<std::uint8_t>(1U << static_cast<unsigned>(scope));
}

/// One contributing module in a host's registrar list. The list is maintained
/// by hand in each host, which is the composition root: naming a registrar that
/// does not exist fails to link (work/decisions.md D16).
struct RegistrarEntry {
    std::string_view moduleId;
    RegistrarFunction registrar = nullptr;
    /// The lifetime scopes this module's participants may have, as scopeBit
    /// values. A declaration outside them is refused.
    std::uint8_t scopes = 0;
};

} // namespace rawframe::composition
