# A sample mod of runners (D200): time runs a thousand times faster. It
# replaces runners' age system through the aging point with swift.kest's
# `swift`, which runs in its place on a machine of its own under Kest's
# untrusted profile.
target rawframe/runners
modapi 1
program swift.kest
replace aging swift
