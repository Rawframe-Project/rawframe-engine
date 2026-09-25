// Made by tools/generate_cldr.py from CLDR 48; do not edit.

#include "../plural_rules.h"

#include <array>
#include <span>
#include <string_view>

namespace rawframe::localization::cldr {

namespace {

PluralCategory rule0([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule1([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i <= 1ULL))))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule2([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.i == 0ULL)) || ((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule3([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 0ULL)))) {
        return PluralCategory::Zero;
    }
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && (((o.i % 100ULL) >= 3ULL && (o.i % 100ULL) <= 10ULL))))) {
        return PluralCategory::Few;
    }
    if (((o.t == 0 && (((o.i % 100ULL) >= 11ULL && (o.i % 100ULL) <= 99ULL))))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule4([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.i == 1ULL) && (o.v == 0ULL))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule5([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i % 10ULL) == 1ULL)) && !(o.t == 0 && ((o.i % 100ULL) == 11ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (((o.i % 10ULL) >= 2ULL && (o.i % 10ULL) <= 4ULL))) &&
         !(o.t == 0 && (((o.i % 100ULL) >= 12ULL && (o.i % 100ULL) <= 14ULL))))) {
        return PluralCategory::Few;
    }
    if (((o.t == 0 && ((o.i % 10ULL) == 0ULL))) || ((o.t == 0 && (((o.i % 10ULL) >= 5ULL && (o.i % 10ULL) <= 9ULL)))) ||
        ((o.t == 0 && (((o.i % 100ULL) >= 11ULL && (o.i % 100ULL) <= 14ULL))))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule6([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 0ULL)))) {
        return PluralCategory::Zero;
    }
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule7([[maybe_unused]] const PluralOperands& o) noexcept {
    return PluralCategory::Other;
}

PluralCategory rule8([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i % 10ULL) == 1ULL)) &&
         !(o.t == 0 && ((o.i % 100ULL) == 11ULL || (o.i % 100ULL) == 71ULL || (o.i % 100ULL) == 91ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && ((o.i % 10ULL) == 2ULL)) &&
         !(o.t == 0 && ((o.i % 100ULL) == 12ULL || (o.i % 100ULL) == 72ULL || (o.i % 100ULL) == 92ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && (((o.i % 10ULL) >= 3ULL && (o.i % 10ULL) <= 4ULL) || (o.i % 10ULL) == 9ULL)) &&
         !(o.t == 0 && (((o.i % 100ULL) >= 10ULL && (o.i % 100ULL) <= 19ULL) ||
                        ((o.i % 100ULL) >= 70ULL && (o.i % 100ULL) <= 79ULL) ||
                        ((o.i % 100ULL) >= 90ULL && (o.i % 100ULL) <= 99ULL))))) {
        return PluralCategory::Few;
    }
    if ((!(o.t == 0 && (o.i == 0ULL)) && (o.t == 0 && ((o.i % 1000000ULL) == 0ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule9([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.v == 0ULL) && ((o.i % 10ULL) == 1ULL) && !((o.i % 100ULL) == 11ULL)) ||
        (((o.f % 10ULL) == 1ULL) && !((o.f % 100ULL) == 11ULL))) {
        return PluralCategory::One;
    }
    if (((o.v == 0ULL) && (((o.i % 10ULL) >= 2ULL && (o.i % 10ULL) <= 4ULL)) &&
         !(((o.i % 100ULL) >= 12ULL && (o.i % 100ULL) <= 14ULL))) ||
        ((((o.f % 10ULL) >= 2ULL && (o.f % 10ULL) <= 4ULL)) &&
         !(((o.f % 100ULL) >= 12ULL && (o.f % 100ULL) <= 14ULL)))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule10([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.i == 1ULL) && (o.v == 0ULL))) {
        return PluralCategory::One;
    }
    if (((o.e == 0ULL) && !(o.i == 0ULL) && ((o.i % 1000000ULL) == 0ULL) && (o.v == 0ULL)) || (!((o.e <= 5ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule11([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.v == 0ULL) && (o.i == 1ULL || o.i == 2ULL || o.i == 3ULL)) ||
        ((o.v == 0ULL) && !((o.i % 10ULL) == 4ULL || (o.i % 10ULL) == 6ULL || (o.i % 10ULL) == 9ULL)) ||
        (!(o.v == 0ULL) && !((o.f % 10ULL) == 4ULL || (o.f % 10ULL) == 6ULL || (o.f % 10ULL) == 9ULL))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule12([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.i == 1ULL) && (o.v == 0ULL))) {
        return PluralCategory::One;
    }
    if ((((o.i >= 2ULL && o.i <= 4ULL)) && (o.v == 0ULL))) {
        return PluralCategory::Few;
    }
    if ((!(o.v == 0ULL))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule13([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 0ULL)))) {
        return PluralCategory::Zero;
    }
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && (o.i == 3ULL)))) {
        return PluralCategory::Few;
    }
    if (((o.t == 0 && (o.i == 6ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule14([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL))) || (!(o.t == 0ULL) && (o.i == 0ULL || o.i == 1ULL))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule15([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.v == 0ULL) && ((o.i % 100ULL) == 1ULL)) || (((o.f % 100ULL) == 1ULL))) {
        return PluralCategory::One;
    }
    if (((o.v == 0ULL) && ((o.i % 100ULL) == 2ULL)) || (((o.f % 100ULL) == 2ULL))) {
        return PluralCategory::Two;
    }
    if (((o.v == 0ULL) && (((o.i % 100ULL) >= 3ULL && (o.i % 100ULL) <= 4ULL))) ||
        ((((o.f % 100ULL) >= 3ULL && (o.f % 100ULL) <= 4ULL)))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule16([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.e == 0ULL) && !(o.i == 0ULL) && ((o.i % 1000000ULL) == 0ULL) && (o.v == 0ULL)) || (!((o.e <= 5ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule17([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.i == 0ULL || o.i == 1ULL))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule18([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.i == 0ULL || o.i == 1ULL))) {
        return PluralCategory::One;
    }
    if (((o.e == 0ULL) && !(o.i == 0ULL) && ((o.i % 1000000ULL) == 0ULL) && (o.v == 0ULL)) || (!((o.e <= 5ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule19([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && ((o.i >= 3ULL && o.i <= 6ULL))))) {
        return PluralCategory::Few;
    }
    if (((o.t == 0 && ((o.i >= 7ULL && o.i <= 10ULL))))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule20([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL || o.i == 11ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL || o.i == 12ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && ((o.i >= 3ULL && o.i <= 10ULL) || (o.i >= 13ULL && o.i <= 19ULL))))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule21([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.v == 0ULL) && ((o.i % 10ULL) == 1ULL))) {
        return PluralCategory::One;
    }
    if (((o.v == 0ULL) && ((o.i % 10ULL) == 2ULL))) {
        return PluralCategory::Two;
    }
    if (((o.v == 0ULL) && ((o.i % 100ULL) == 0ULL || (o.i % 100ULL) == 20ULL || (o.i % 100ULL) == 40ULL ||
                           (o.i % 100ULL) == 60ULL || (o.i % 100ULL) == 80ULL))) {
        return PluralCategory::Few;
    }
    if ((!(o.v == 0ULL))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule22([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.i == 1ULL) && (o.v == 0ULL)) || ((o.i == 0ULL) && !(o.v == 0ULL))) {
        return PluralCategory::One;
    }
    if (((o.i == 2ULL) && (o.v == 0ULL))) {
        return PluralCategory::Two;
    }
    return PluralCategory::Other;
}

PluralCategory rule23([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0ULL) && ((o.i % 10ULL) == 1ULL) && !((o.i % 100ULL) == 11ULL)) ||
        (((o.t % 10ULL) == 1ULL) && !((o.t % 100ULL) == 11ULL))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule24([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL)))) {
        return PluralCategory::Two;
    }
    return PluralCategory::Other;
}

PluralCategory rule25([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 0ULL)))) {
        return PluralCategory::Zero;
    }
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && ((o.i % 100ULL) == 2ULL || (o.i % 100ULL) == 22ULL || (o.i % 100ULL) == 42ULL ||
                       (o.i % 100ULL) == 62ULL || (o.i % 100ULL) == 82ULL))) ||
        ((o.t == 0 && ((o.i % 1000ULL) == 0ULL)) &&
         (o.t == 0 &&
          (((o.i % 100000ULL) >= 1000ULL && (o.i % 100000ULL) <= 20000ULL) || (o.i % 100000ULL) == 40000ULL ||
           (o.i % 100000ULL) == 60000ULL || (o.i % 100000ULL) == 80000ULL))) ||
        (!(o.t == 0 && (o.i == 0ULL)) && (o.t == 0 && ((o.i % 1000000ULL) == 100000ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && ((o.i % 100ULL) == 3ULL || (o.i % 100ULL) == 23ULL || (o.i % 100ULL) == 43ULL ||
                       (o.i % 100ULL) == 63ULL || (o.i % 100ULL) == 83ULL)))) {
        return PluralCategory::Few;
    }
    if ((!(o.t == 0 && (o.i == 1ULL)) &&
         (o.t == 0 && ((o.i % 100ULL) == 1ULL || (o.i % 100ULL) == 21ULL || (o.i % 100ULL) == 41ULL ||
                       (o.i % 100ULL) == 61ULL || (o.i % 100ULL) == 81ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule26([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 0ULL)))) {
        return PluralCategory::Zero;
    }
    if (((o.i == 0ULL || o.i == 1ULL) && !(o.t == 0 && (o.i == 0ULL)))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule27([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i % 10ULL) == 1ULL)) &&
         !(o.t == 0 && (((o.i % 100ULL) >= 11ULL && (o.i % 100ULL) <= 19ULL))))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (((o.i % 10ULL) >= 2ULL && (o.i % 10ULL) <= 9ULL))) &&
         !(o.t == 0 && (((o.i % 100ULL) >= 11ULL && (o.i % 100ULL) <= 19ULL))))) {
        return PluralCategory::Few;
    }
    if ((!(o.f == 0ULL))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule28([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i % 10ULL) == 0ULL))) ||
        ((o.t == 0 && (((o.i % 100ULL) >= 11ULL && (o.i % 100ULL) <= 19ULL)))) ||
        ((o.v == 2ULL) && (((o.f % 100ULL) >= 11ULL && (o.f % 100ULL) <= 19ULL)))) {
        return PluralCategory::Zero;
    }
    if (((o.t == 0 && ((o.i % 10ULL) == 1ULL)) && !(o.t == 0 && ((o.i % 100ULL) == 11ULL))) ||
        ((o.v == 2ULL) && ((o.f % 10ULL) == 1ULL) && !((o.f % 100ULL) == 11ULL)) ||
        (!(o.v == 2ULL) && ((o.f % 10ULL) == 1ULL))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule29([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.v == 0ULL) && ((o.i % 10ULL) == 1ULL) && !((o.i % 100ULL) == 11ULL)) ||
        (((o.f % 10ULL) == 1ULL) && !((o.f % 100ULL) == 11ULL))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule30([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.i == 1ULL) && (o.v == 0ULL))) {
        return PluralCategory::One;
    }
    if ((!(o.v == 0ULL)) || ((o.t == 0 && (o.i == 0ULL))) ||
        (!(o.t == 0 && (o.i == 1ULL)) && (o.t == 0 && (((o.i % 100ULL) >= 1ULL && (o.i % 100ULL) <= 19ULL))))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule31([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && (o.i == 0ULL))) || ((o.t == 0 && (((o.i % 100ULL) >= 3ULL && (o.i % 100ULL) <= 10ULL))))) {
        return PluralCategory::Few;
    }
    if (((o.t == 0 && (((o.i % 100ULL) >= 11ULL && (o.i % 100ULL) <= 19ULL))))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule32([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.i == 1ULL) && (o.v == 0ULL))) {
        return PluralCategory::One;
    }
    if (((o.v == 0ULL) && (((o.i % 10ULL) >= 2ULL && (o.i % 10ULL) <= 4ULL)) &&
         !(((o.i % 100ULL) >= 12ULL && (o.i % 100ULL) <= 14ULL)))) {
        return PluralCategory::Few;
    }
    if (((o.v == 0ULL) && !(o.i == 1ULL) && (((o.i % 10ULL) <= 1ULL))) ||
        ((o.v == 0ULL) && (((o.i % 10ULL) >= 5ULL && (o.i % 10ULL) <= 9ULL))) ||
        ((o.v == 0ULL) && (((o.i % 100ULL) >= 12ULL && (o.i % 100ULL) <= 14ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule33([[maybe_unused]] const PluralOperands& o) noexcept {
    if ((((o.i <= 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.e == 0ULL) && !(o.i == 0ULL) && ((o.i % 1000000ULL) == 0ULL) && (o.v == 0ULL)) || (!((o.e <= 5ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule34([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.v == 0ULL) && ((o.i % 10ULL) == 1ULL) && !((o.i % 100ULL) == 11ULL))) {
        return PluralCategory::One;
    }
    if (((o.v == 0ULL) && (((o.i % 10ULL) >= 2ULL && (o.i % 10ULL) <= 4ULL)) &&
         !(((o.i % 100ULL) >= 12ULL && (o.i % 100ULL) <= 14ULL)))) {
        return PluralCategory::Few;
    }
    if (((o.v == 0ULL) && ((o.i % 10ULL) == 0ULL)) ||
        ((o.v == 0ULL) && (((o.i % 10ULL) >= 5ULL && (o.i % 10ULL) <= 9ULL))) ||
        ((o.v == 0ULL) && (((o.i % 100ULL) >= 11ULL && (o.i % 100ULL) <= 14ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule35([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i % 10ULL) == 1ULL)) && !(o.t == 0 && ((o.i % 100ULL) == 11ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL)))) {
        return PluralCategory::Two;
    }
    if ((!(o.t == 0 && (o.i == 2ULL)) && (o.t == 0 && (((o.i % 10ULL) >= 2ULL && (o.i % 10ULL) <= 9ULL))) &&
         !(o.t == 0 && (((o.i % 100ULL) >= 11ULL && (o.i % 100ULL) <= 19ULL))))) {
        return PluralCategory::Few;
    }
    if ((!(o.f == 0ULL))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule36([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.i == 0ULL)) || ((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && ((o.i >= 2ULL && o.i <= 10ULL))))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule37([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 0ULL || o.i == 1ULL))) || ((o.i == 0ULL) && (o.f == 1ULL))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule38([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.v == 0ULL) && ((o.i % 100ULL) == 1ULL))) {
        return PluralCategory::One;
    }
    if (((o.v == 0ULL) && ((o.i % 100ULL) == 2ULL))) {
        return PluralCategory::Two;
    }
    if (((o.v == 0ULL) && (((o.i % 100ULL) >= 3ULL && (o.i % 100ULL) <= 4ULL))) || (!(o.v == 0ULL))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule39([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i <= 1ULL)))) || ((o.t == 0 && ((o.i >= 11ULL && o.i <= 99ULL))))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule40([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL || o.i == 5ULL || o.i == 7ULL || o.i == 8ULL || o.i == 9ULL || o.i == 10ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL || o.i == 3ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && (o.i == 4ULL)))) {
        return PluralCategory::Few;
    }
    if (((o.t == 0 && (o.i == 6ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule41([[maybe_unused]] const PluralOperands& o) noexcept {
    if ((((o.i % 10ULL) == 1ULL || (o.i % 10ULL) == 2ULL || (o.i % 10ULL) == 5ULL || (o.i % 10ULL) == 7ULL ||
          (o.i % 10ULL) == 8ULL)) ||
        (((o.i % 100ULL) == 20ULL || (o.i % 100ULL) == 50ULL || (o.i % 100ULL) == 70ULL || (o.i % 100ULL) == 80ULL))) {
        return PluralCategory::One;
    }
    if ((((o.i % 10ULL) == 3ULL || (o.i % 10ULL) == 4ULL)) ||
        (((o.i % 1000ULL) == 100ULL || (o.i % 1000ULL) == 200ULL || (o.i % 1000ULL) == 300ULL ||
          (o.i % 1000ULL) == 400ULL || (o.i % 1000ULL) == 500ULL || (o.i % 1000ULL) == 600ULL ||
          (o.i % 1000ULL) == 700ULL || (o.i % 1000ULL) == 800ULL || (o.i % 1000ULL) == 900ULL))) {
        return PluralCategory::Few;
    }
    if (((o.i == 0ULL)) || (((o.i % 10ULL) == 6ULL)) ||
        (((o.i % 100ULL) == 40ULL || (o.i % 100ULL) == 60ULL || (o.i % 100ULL) == 90ULL))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule42([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i % 10ULL) == 2ULL || (o.i % 10ULL) == 3ULL)) &&
         !(o.t == 0 && ((o.i % 100ULL) == 12ULL || (o.i % 100ULL) == 13ULL)))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule43([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.i == 0ULL))) {
        return PluralCategory::Zero;
    }
    if (((o.i == 1ULL))) {
        return PluralCategory::One;
    }
    if (((o.i == 2ULL || o.i == 3ULL || o.i == 4ULL || o.i == 5ULL || o.i == 6ULL))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule44([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL || o.i == 3ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && (o.i == 4ULL)))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule45([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 0ULL || o.i == 7ULL || o.i == 8ULL || o.i == 9ULL)))) {
        return PluralCategory::Zero;
    }
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && (o.i == 3ULL || o.i == 4ULL)))) {
        return PluralCategory::Few;
    }
    if (((o.t == 0 && (o.i == 5ULL || o.i == 6ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule46([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i % 10ULL) == 1ULL)) && !(o.t == 0 && ((o.i % 100ULL) == 11ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && ((o.i % 10ULL) == 2ULL)) && !(o.t == 0 && ((o.i % 100ULL) == 12ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && ((o.i % 10ULL) == 3ULL)) && !(o.t == 0 && ((o.i % 100ULL) == 13ULL)))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule47([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL || o.i == 11ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL || o.i == 12ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && (o.i == 3ULL || o.i == 13ULL)))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule48([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL || o.i == 3ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && (o.i == 4ULL)))) {
        return PluralCategory::Few;
    }
    if (((o.t == 0 && (o.i == 6ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule49([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL || o.i == 5ULL)))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule50([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 11ULL || o.i == 8ULL || o.i == 80ULL || o.i == 800ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule51([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.i == 1ULL))) {
        return PluralCategory::One;
    }
    if (((o.i == 0ULL)) || ((((o.i % 100ULL) >= 2ULL && (o.i % 100ULL) <= 20ULL) || (o.i % 100ULL) == 40ULL ||
                             (o.i % 100ULL) == 60ULL || (o.i % 100ULL) == 80ULL))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule52([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i % 10ULL) == 6ULL))) || ((o.t == 0 && ((o.i % 10ULL) == 9ULL))) ||
        ((o.t == 0 && ((o.i % 10ULL) == 0ULL)) && !(o.t == 0 && (o.i == 0ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule53([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL || o.i == 3ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && (o.i == 4ULL)))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule54([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i >= 1ULL && o.i <= 4ULL)))) ||
        ((o.t == 0 &&
          (((o.i % 100ULL) >= 1ULL && (o.i % 100ULL) <= 4ULL) || ((o.i % 100ULL) >= 21ULL && (o.i % 100ULL) <= 24ULL) ||
           ((o.i % 100ULL) >= 41ULL && (o.i % 100ULL) <= 44ULL) ||
           ((o.i % 100ULL) >= 61ULL && (o.i % 100ULL) <= 64ULL) ||
           ((o.i % 100ULL) >= 81ULL && (o.i % 100ULL) <= 84ULL))))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 5ULL))) || ((o.t == 0 && ((o.i % 100ULL) == 5ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule55([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 &&
          (o.i == 11ULL || o.i == 8ULL || (o.i >= 80ULL && o.i <= 89ULL) || (o.i >= 800ULL && o.i <= 899ULL))))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule56([[maybe_unused]] const PluralOperands& o) noexcept {
    if ((((o.i % 10ULL) == 1ULL) && !((o.i % 100ULL) == 11ULL))) {
        return PluralCategory::One;
    }
    if ((((o.i % 10ULL) == 2ULL) && !((o.i % 100ULL) == 12ULL))) {
        return PluralCategory::Two;
    }
    if ((((o.i % 10ULL) == 7ULL || (o.i % 10ULL) == 8ULL) && !((o.i % 100ULL) == 17ULL || (o.i % 100ULL) == 18ULL))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule57([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i >= 1ULL && o.i <= 4ULL))))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule58([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL || o.i == 5ULL || (o.i >= 7ULL && o.i <= 9ULL))))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && (o.i == 2ULL || o.i == 3ULL)))) {
        return PluralCategory::Two;
    }
    if (((o.t == 0 && (o.i == 4ULL)))) {
        return PluralCategory::Few;
    }
    if (((o.t == 0 && (o.i == 6ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule59([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && (o.i == 1ULL)))) {
        return PluralCategory::One;
    }
    if (((o.t == 0 && ((o.i % 10ULL) == 4ULL)) && !(o.t == 0 && ((o.i % 100ULL) == 14ULL)))) {
        return PluralCategory::Many;
    }
    return PluralCategory::Other;
}

PluralCategory rule60([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i % 10ULL) == 1ULL || (o.i % 10ULL) == 2ULL)) &&
         !(o.t == 0 && ((o.i % 100ULL) == 11ULL || (o.i % 100ULL) == 12ULL)))) {
        return PluralCategory::One;
    }
    return PluralCategory::Other;
}

PluralCategory rule61([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i % 10ULL) == 6ULL || (o.i % 10ULL) == 9ULL))) || ((o.t == 0 && (o.i == 10ULL)))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

PluralCategory rule62([[maybe_unused]] const PluralOperands& o) noexcept {
    if (((o.t == 0 && ((o.i % 10ULL) == 3ULL)) && !(o.t == 0 && ((o.i % 100ULL) == 13ULL)))) {
        return PluralCategory::Few;
    }
    return PluralCategory::Other;
}

} // namespace

constexpr std::array<RuleSet, 224> kCardinal{{
    RuleSet{"af", &rule0},   RuleSet{"ak", &rule1},   RuleSet{"am", &rule2},       RuleSet{"an", &rule0},
    RuleSet{"ar", &rule3},   RuleSet{"ars", &rule3},  RuleSet{"as", &rule2},       RuleSet{"asa", &rule0},
    RuleSet{"ast", &rule4},  RuleSet{"az", &rule0},   RuleSet{"bal", &rule0},      RuleSet{"be", &rule5},
    RuleSet{"bem", &rule0},  RuleSet{"bez", &rule0},  RuleSet{"bg", &rule0},       RuleSet{"bho", &rule1},
    RuleSet{"blo", &rule6},  RuleSet{"bm", &rule7},   RuleSet{"bn", &rule2},       RuleSet{"bo", &rule7},
    RuleSet{"br", &rule8},   RuleSet{"brx", &rule0},  RuleSet{"bs", &rule9},       RuleSet{"ca", &rule10},
    RuleSet{"ce", &rule0},   RuleSet{"ceb", &rule11}, RuleSet{"cgg", &rule0},      RuleSet{"chr", &rule0},
    RuleSet{"ckb", &rule0},  RuleSet{"cs", &rule12},  RuleSet{"csw", &rule1},      RuleSet{"cv", &rule6},
    RuleSet{"cy", &rule13},  RuleSet{"da", &rule14},  RuleSet{"de", &rule4},       RuleSet{"doi", &rule2},
    RuleSet{"dsb", &rule15}, RuleSet{"dv", &rule0},   RuleSet{"dz", &rule7},       RuleSet{"ee", &rule0},
    RuleSet{"el", &rule0},   RuleSet{"en", &rule4},   RuleSet{"eo", &rule0},       RuleSet{"es", &rule16},
    RuleSet{"et", &rule4},   RuleSet{"eu", &rule0},   RuleSet{"fa", &rule2},       RuleSet{"ff", &rule17},
    RuleSet{"fi", &rule4},   RuleSet{"fil", &rule11}, RuleSet{"fo", &rule0},       RuleSet{"fr", &rule18},
    RuleSet{"fur", &rule0},  RuleSet{"fy", &rule4},   RuleSet{"ga", &rule19},      RuleSet{"gd", &rule20},
    RuleSet{"gl", &rule4},   RuleSet{"gsw", &rule0},  RuleSet{"gu", &rule2},       RuleSet{"guw", &rule1},
    RuleSet{"gv", &rule21},  RuleSet{"ha", &rule0},   RuleSet{"haw", &rule0},      RuleSet{"he", &rule22},
    RuleSet{"hi", &rule2},   RuleSet{"hnj", &rule7},  RuleSet{"hr", &rule9},       RuleSet{"hsb", &rule15},
    RuleSet{"hu", &rule0},   RuleSet{"hy", &rule17},  RuleSet{"ia", &rule4},       RuleSet{"id", &rule7},
    RuleSet{"ie", &rule4},   RuleSet{"ig", &rule7},   RuleSet{"ii", &rule7},       RuleSet{"io", &rule4},
    RuleSet{"is", &rule23},  RuleSet{"it", &rule10},  RuleSet{"iu", &rule24},      RuleSet{"ja", &rule7},
    RuleSet{"jbo", &rule7},  RuleSet{"jgo", &rule0},  RuleSet{"jmc", &rule0},      RuleSet{"jv", &rule7},
    RuleSet{"jw", &rule7},   RuleSet{"ka", &rule0},   RuleSet{"kab", &rule17},     RuleSet{"kaj", &rule0},
    RuleSet{"kcg", &rule0},  RuleSet{"kde", &rule7},  RuleSet{"kea", &rule7},      RuleSet{"kk", &rule0},
    RuleSet{"kkj", &rule0},  RuleSet{"kl", &rule0},   RuleSet{"km", &rule7},       RuleSet{"kn", &rule2},
    RuleSet{"ko", &rule7},   RuleSet{"kok", &rule2},  RuleSet{"kok-Latn", &rule2}, RuleSet{"ks", &rule0},
    RuleSet{"ksb", &rule0},  RuleSet{"ksh", &rule6},  RuleSet{"ku", &rule0},       RuleSet{"kw", &rule25},
    RuleSet{"ky", &rule0},   RuleSet{"lag", &rule26}, RuleSet{"lb", &rule0},       RuleSet{"lg", &rule0},
    RuleSet{"lij", &rule4},  RuleSet{"lkt", &rule7},  RuleSet{"lld", &rule10},     RuleSet{"ln", &rule1},
    RuleSet{"lo", &rule7},   RuleSet{"lt", &rule27},  RuleSet{"lv", &rule28},      RuleSet{"mas", &rule0},
    RuleSet{"mg", &rule1},   RuleSet{"mgo", &rule0},  RuleSet{"mk", &rule29},      RuleSet{"ml", &rule0},
    RuleSet{"mn", &rule0},   RuleSet{"mo", &rule30},  RuleSet{"mr", &rule0},       RuleSet{"ms", &rule7},
    RuleSet{"mt", &rule31},  RuleSet{"my", &rule7},   RuleSet{"nah", &rule0},      RuleSet{"naq", &rule24},
    RuleSet{"nb", &rule0},   RuleSet{"nd", &rule0},   RuleSet{"ne", &rule0},       RuleSet{"nl", &rule4},
    RuleSet{"nn", &rule0},   RuleSet{"nnh", &rule0},  RuleSet{"no", &rule0},       RuleSet{"nqo", &rule7},
    RuleSet{"nr", &rule0},   RuleSet{"nso", &rule1},  RuleSet{"ny", &rule0},       RuleSet{"nyn", &rule0},
    RuleSet{"om", &rule0},   RuleSet{"or", &rule0},   RuleSet{"os", &rule0},       RuleSet{"osa", &rule7},
    RuleSet{"pa", &rule1},   RuleSet{"pap", &rule0},  RuleSet{"pcm", &rule2},      RuleSet{"pl", &rule32},
    RuleSet{"prg", &rule28}, RuleSet{"ps", &rule0},   RuleSet{"pt", &rule33},      RuleSet{"pt-PT", &rule10},
    RuleSet{"rm", &rule0},   RuleSet{"ro", &rule30},  RuleSet{"rof", &rule0},      RuleSet{"ru", &rule34},
    RuleSet{"rwk", &rule0},  RuleSet{"sah", &rule7},  RuleSet{"saq", &rule0},      RuleSet{"sat", &rule24},
    RuleSet{"sc", &rule4},   RuleSet{"scn", &rule10}, RuleSet{"sd", &rule0},       RuleSet{"sdh", &rule0},
    RuleSet{"se", &rule24},  RuleSet{"seh", &rule0},  RuleSet{"ses", &rule7},      RuleSet{"sg", &rule7},
    RuleSet{"sgs", &rule35}, RuleSet{"sh", &rule9},   RuleSet{"shi", &rule36},     RuleSet{"si", &rule37},
    RuleSet{"sk", &rule12},  RuleSet{"sl", &rule38},  RuleSet{"sma", &rule24},     RuleSet{"smi", &rule24},
    RuleSet{"smj", &rule24}, RuleSet{"smn", &rule24}, RuleSet{"sms", &rule24},     RuleSet{"sn", &rule0},
    RuleSet{"so", &rule0},   RuleSet{"sq", &rule0},   RuleSet{"sr", &rule9},       RuleSet{"ss", &rule0},
    RuleSet{"ssy", &rule0},  RuleSet{"st", &rule0},   RuleSet{"su", &rule7},       RuleSet{"sv", &rule4},
    RuleSet{"sw", &rule4},   RuleSet{"syr", &rule0},  RuleSet{"ta", &rule0},       RuleSet{"te", &rule0},
    RuleSet{"teo", &rule0},  RuleSet{"th", &rule7},   RuleSet{"ti", &rule1},       RuleSet{"tig", &rule0},
    RuleSet{"tk", &rule0},   RuleSet{"tl", &rule11},  RuleSet{"tn", &rule0},       RuleSet{"to", &rule7},
    RuleSet{"tpi", &rule7},  RuleSet{"tr", &rule0},   RuleSet{"ts", &rule0},       RuleSet{"tzm", &rule39},
    RuleSet{"ug", &rule0},   RuleSet{"uk", &rule34},  RuleSet{"und", &rule7},      RuleSet{"ur", &rule4},
    RuleSet{"uz", &rule0},   RuleSet{"ve", &rule0},   RuleSet{"vec", &rule10},     RuleSet{"vi", &rule7},
    RuleSet{"vo", &rule0},   RuleSet{"vun", &rule0},  RuleSet{"wa", &rule1},       RuleSet{"wae", &rule0},
    RuleSet{"wo", &rule7},   RuleSet{"xh", &rule0},   RuleSet{"xog", &rule0},      RuleSet{"yi", &rule4},
    RuleSet{"yo", &rule7},   RuleSet{"yue", &rule7},  RuleSet{"zh", &rule7},       RuleSet{"zu", &rule2},
}};

std::span<const RuleSet> cardinalRules() noexcept {
    return kCardinal;
}

constexpr std::array<RuleSet, 108> kOrdinal{{
    RuleSet{"af", &rule7},  RuleSet{"am", &rule7},   RuleSet{"an", &rule7},        RuleSet{"ar", &rule7},
    RuleSet{"as", &rule40}, RuleSet{"ast", &rule7},  RuleSet{"az", &rule41},       RuleSet{"bal", &rule0},
    RuleSet{"be", &rule42}, RuleSet{"bg", &rule7},   RuleSet{"blo", &rule43},      RuleSet{"bn", &rule40},
    RuleSet{"bs", &rule7},  RuleSet{"ca", &rule44},  RuleSet{"ce", &rule7},        RuleSet{"cs", &rule7},
    RuleSet{"cv", &rule7},  RuleSet{"cy", &rule45},  RuleSet{"da", &rule7},        RuleSet{"de", &rule7},
    RuleSet{"dsb", &rule7}, RuleSet{"el", &rule7},   RuleSet{"en", &rule46},       RuleSet{"es", &rule7},
    RuleSet{"et", &rule7},  RuleSet{"eu", &rule7},   RuleSet{"fa", &rule7},        RuleSet{"fi", &rule7},
    RuleSet{"fil", &rule0}, RuleSet{"fr", &rule0},   RuleSet{"fy", &rule7},        RuleSet{"ga", &rule0},
    RuleSet{"gd", &rule47}, RuleSet{"gl", &rule7},   RuleSet{"gsw", &rule7},       RuleSet{"gu", &rule48},
    RuleSet{"he", &rule7},  RuleSet{"hi", &rule48},  RuleSet{"hr", &rule7},        RuleSet{"hsb", &rule7},
    RuleSet{"hu", &rule49}, RuleSet{"hy", &rule0},   RuleSet{"ia", &rule7},        RuleSet{"id", &rule7},
    RuleSet{"ie", &rule7},  RuleSet{"is", &rule7},   RuleSet{"it", &rule50},       RuleSet{"ja", &rule7},
    RuleSet{"ka", &rule51}, RuleSet{"kk", &rule52},  RuleSet{"km", &rule7},        RuleSet{"kn", &rule7},
    RuleSet{"ko", &rule7},  RuleSet{"kok", &rule53}, RuleSet{"kok-Latn", &rule53}, RuleSet{"kw", &rule54},
    RuleSet{"ky", &rule7},  RuleSet{"lij", &rule55}, RuleSet{"lld", &rule50},      RuleSet{"lo", &rule0},
    RuleSet{"lt", &rule7},  RuleSet{"lv", &rule7},   RuleSet{"mk", &rule56},       RuleSet{"ml", &rule7},
    RuleSet{"mn", &rule7},  RuleSet{"mo", &rule0},   RuleSet{"mr", &rule53},       RuleSet{"ms", &rule0},
    RuleSet{"my", &rule7},  RuleSet{"nb", &rule7},   RuleSet{"ne", &rule57},       RuleSet{"nl", &rule7},
    RuleSet{"no", &rule7},  RuleSet{"or", &rule58},  RuleSet{"pa", &rule7},        RuleSet{"pl", &rule7},
    RuleSet{"prg", &rule7}, RuleSet{"ps", &rule7},   RuleSet{"pt", &rule7},        RuleSet{"ro", &rule0},
    RuleSet{"ru", &rule7},  RuleSet{"sc", &rule50},  RuleSet{"scn", &rule55},      RuleSet{"sd", &rule7},
    RuleSet{"sh", &rule7},  RuleSet{"si", &rule7},   RuleSet{"sk", &rule7},        RuleSet{"sl", &rule7},
    RuleSet{"sq", &rule59}, RuleSet{"sr", &rule7},   RuleSet{"sv", &rule60},       RuleSet{"sw", &rule7},
    RuleSet{"ta", &rule7},  RuleSet{"te", &rule7},   RuleSet{"th", &rule7},        RuleSet{"tk", &rule61},
    RuleSet{"tl", &rule0},  RuleSet{"tpi", &rule7},  RuleSet{"tr", &rule7},        RuleSet{"uk", &rule62},
    RuleSet{"und", &rule7}, RuleSet{"ur", &rule7},   RuleSet{"uz", &rule7},        RuleSet{"vec", &rule50},
    RuleSet{"vi", &rule0},  RuleSet{"yue", &rule7},  RuleSet{"zh", &rule7},        RuleSet{"zu", &rule7},
}};

std::span<const RuleSet> ordinalRules() noexcept {
    return kOrdinal;
}

} // namespace rawframe::localization::cldr
