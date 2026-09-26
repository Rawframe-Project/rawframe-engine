# A sample mod of runners (D199): every hit costs twice. It provides
# runners' penalty service with penalty.kest's `double`, which runs on a
# machine of its own under Kest's untrusted profile.
target rawframe/runners
modapi 1
program penalty.kest
provide penalty double
