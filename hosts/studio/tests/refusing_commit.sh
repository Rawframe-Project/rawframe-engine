#!/usr/bin/env bash
# A commit-msg hook for studio_commit_refused (D1002): a warning first, as
# git's own line-end warnings come, then a refusal of a message that asks
# for one; every other message is let through.
echo "warning: the trial's hook speaks first"
if grep -q refuse "$1"; then
    echo "error: the trial's hook refuses this commit"
    exit 1
fi
