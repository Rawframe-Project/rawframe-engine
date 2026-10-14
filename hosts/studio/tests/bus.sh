# Sourced by Studio's tests: a private accessibility bus for the Studio
# they start, so tools/click.py finds its controls by name (D774), where
# dbus-daemon exists. Sets AT_SPI_BUS_ADDRESS for what follows and `bus`
# to the daemon's pid, which the caller's trap ends.
bus=""
if started="$(dbus-daemon --session --fork --print-address=1 --print-pid=1 2>/dev/null)"; then
    export AT_SPI_BUS_ADDRESS="$(head -1 <<<"$started")"
    bus="$(tail -1 <<<"$started")"
fi
