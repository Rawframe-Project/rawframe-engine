// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Duplex streams: an output and a hidden input of the backend joined by
// a ring, the output's callback calling the host's with both.

#ifndef MAUL_AUDIO_SRC_DUPLEX_H
#define MAUL_AUDIO_SRC_DUPLEX_H

#include "context_core.h"

// Opens the halves of a duplex stream from a checked def whose
// direction is maud_directionDuplex: the output on def's device, the
// input on its inputDevice at the output's rate. On success the output
// half is live and named by *streamIdOut.
maudResult maudCreateDuplex(maudContext* context, const maudStreamDef* def,
                            maudStreamId* streamIdOut);

#endif // MAUL_AUDIO_SRC_DUPLEX_H
