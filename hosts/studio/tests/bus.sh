# Sourced by Studio's tests, once `work` names their folder: a private
# accessibility bus for the Studio they start, so tools/click.py finds its
# controls by name, where dbus-daemon exists. Sets AT_SPI_BUS_ADDRESS for
# what follows and `bus` to the daemon's pid, which the caller's trap ends.
# The daemon stays this script's child, never --fork: a daemon that forks
# away is handed to tools/reap.py, which kills it a minute later as left
# running by a test, while this test still talks over it.
bus=""
if command -v dbus-daemon >/dev/null 2>&1; then
    mkfifo "$work/bus-address"
    dbus-daemon --session --nofork --print-address=3 3>"$work/bus-address" 2>/dev/null &
    bus=$!
    if read -r address <"$work/bus-address" && [ -n "$address" ]; then
        export AT_SPI_BUS_ADDRESS="$address"
    else
        wait "$bus" 2>/dev/null
        bus=""
    fi
fi
