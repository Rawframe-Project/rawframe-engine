# A sample mod of quarry: a canyon through the land. It provides
# quarry's ground service with canyon.kest's `carve`, which runs on a
# machine of its own under Kest's untrusted profile.
target rawframe/quarry
modapi 1
program canyon.kest
provide ground carve
