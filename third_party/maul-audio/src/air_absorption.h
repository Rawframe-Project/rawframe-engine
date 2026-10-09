// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Air absorption by ISO 9613 part 1, per band (maudGetAirAbsorption's work).

#ifndef MAUL_AUDIO_SRC_AIR_ABSORPTION_H
#define MAUL_AUDIO_SRC_AIR_ABSORPTION_H

// Three amplitude exponents per metre at a temperature (degrees C) and a
// relative humidity (percent), unchecked.
void maudAirAbsorptionOf(double celsius, double humidity, float* absorption);

#endif // MAUL_AUDIO_SRC_AIR_ABSORPTION_H
