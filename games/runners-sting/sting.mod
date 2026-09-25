# A sample mod of runners (D181): every hit stings, counted twice. It handles
# runners' hits point with sting.kest's `sting`, which runs on a machine of
# its own under Kest's untrusted profile.
target rawframe/runners
modapi 1
program sting.kest
handle hits sting
