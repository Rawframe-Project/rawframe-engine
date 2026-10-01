// OTS's configuration for Rawframe's cook (D385), as its meson build would
// write it with Graphite off: COLRv1 paint graphs with cycles are refused, and
// a variable font without gvar is given an empty one.
#pragma once

#define OTS_COLR_CYCLE_CHECK 1
#define OTS_SYNTHESIZE_MISSING_GVAR 1
