#!/usr/bin/env python3
"""Tests of check_repo.py's attribution rule: what attributes a commit is a
trailer in its message's last paragraph or a line that begins as a tool's
footer does, never prose that names them."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_repo import attribution_in  # noqa: E402

# The squash message of #38, which explains the rule and so names every
# attribution it refuses, quoted and wrapped.
EXPLAINING = """fix(check): pass only main's release commit through attribution (#38)

## Problem

Main's `ci` failed at e77003bd, the v0.0.1 release commit.
`tools/check_repo.py` checks every commit message in the history for AI
attribution (`Co-authored-by:`, `generated with`, `claude-session:`).
The release pull request was squashed with release-please's footer
("This PR was generated with Release Please") and GitHub's
`Co-authored-by` lines for the release bot and the owner. The rule reads
the whole history, so it fails on main and on every branch from v0.0.1
on.

The repository standard keeps those lines out of every commit. The
release merge is the fault, and from now on release pull requests are
merged with an explicit squash message that leaves them out. That commit
is on main for good, since history is not rewritten.

## Change

- `ATTRIBUTION_ALLOWED` in `tools/check_repo.py` lists the commits on
main that may carry such lines, by full SHA, each with its reason. It
holds one commit, `e77003bd55a41a3c0aa57a4718842a085c5541a6` (v0.0.1).
- The attribution rule skips those commits. Their em dashes are still
checked, and every other commit is checked as before, a future release
commit included.

## Verification

- `tools/check_repo.py` passes on main's history.
- In a throwaway worktree on main, an ordinary commit with
`Co-authored-by: Someone` was refused, and so was a new `chore: release
0.0.2 (#40)` commit with release-please's footer.
- `tools/check.sh changed` passed before the push.

Refs #5
"""


class Attribution(unittest.TestCase):
    def test_a_message_that_names_attribution_passes(self):
        self.assertEqual(attribution_in(EXPLAINING), [])

    def test_a_co_author_trailer_fails(self):
        message = "fix(x): y\n\nWhy.\n\nCo-authored-by: Someone <a@b.c>\n"
        self.assertEqual(attribution_in(message), ["Co-authored-by: Someone <a@b.c>"])

    def test_a_session_trailer_fails(self):
        message = "fix(x): y\n\nWhy.\n\nRefs #1\nclaude-session: abc\n"
        self.assertEqual(attribution_in(message), ["claude-session: abc"])

    def test_release_pleases_footer_fails(self):
        message = ("chore: release 0.0.2 (#40)\n\n## 0.0.2\n\n---\nThis PR was generated with [Release\n"
                   "Please](https://github.com/googleapis/release-please).\n")
        self.assertEqual(attribution_in(message), ["This PR was generated with [Release"])

    def test_a_generated_with_footer_fails(self):
        message = "fix(x): y\n\nGenerated with a tool\n\nRefs #1\n"
        self.assertEqual(attribution_in(message), ["Generated with a tool"])

    def test_a_trailer_outside_the_last_paragraph_is_prose(self):
        message = "fix(x): y\n\nCo-authored-by: lines are what GitHub adds.\n\nRefs #1\n"
        self.assertEqual(attribution_in(message), [])


if __name__ == "__main__":
    unittest.main()
