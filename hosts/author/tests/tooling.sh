# What the tests that drive a dedicated server through its tooling endpoint
# share; each sources it. The endpoint answers before the World has run a
# tick and placed its scenes, so a test that reads the World first waits for
# what it is about to read.
#
#   await_tooling <rawframe-author> <endpoint> <fingerprint> <token> <record> <pattern>
#
# Asks <record> on a connection of its own every tenth of a second until a
# reply matches <pattern> (an extended regular expression), for at most five
# seconds. Says so and fails if none does.
await_tooling() {
    local author=$1 endpoint=$2 fingerprint=$3 token=$4 record=$5 pattern=$6
    for _ in $(seq 50); do
        echo "$record" | "$author" connect "$endpoint" "$fingerprint" "$token" 2>/dev/null | grep -qE "$pattern" &&
            return 0
        sleep 0.1
    done
    echo "await_tooling: no reply to $record matched $pattern within five seconds"
    return 1
}
