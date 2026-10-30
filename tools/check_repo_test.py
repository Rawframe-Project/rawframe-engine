#!/usr/bin/env python3
"""Tests of check_repo.py's attribution rule: what attributes a commit is a
trailer in its message's last paragraph or a line that begins as a tool's
footer does, never prose that names them."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_repo import attribution_in, inline_python_in, run_blocks  # noqa: E402

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

    def test_an_emoji_before_a_footer_fails(self):
        message = "fix(x): y\n\nWhy.\n\n\U0001f916 Generated with [Claude Code](https://claude.com/claude-code)\n"
        self.assertEqual(attribution_in(message),
                         ["\U0001f916 Generated with [Claude Code](https://claude.com/claude-code)"])

    def test_trailers_before_a_line_of_blanks_fail(self):
        message = "fix(x): y\n\nWhy.\n\nCo-authored-by: Someone <a@b.c>\n   \n"
        self.assertEqual(attribution_in(message), ["Co-authored-by: Someone <a@b.c>"])

    def test_a_trailer_with_blanks_before_its_colon_fails(self):
        message = "fix(x): y\n\nWhy.\n\nCo-authored-by : Someone <a@b.c>\n"
        self.assertEqual(attribution_in(message), ["Co-authored-by : Someone <a@b.c>"])

    def test_a_footer_in_markdown_marks_fails(self):
        for line in ("[Generated with Claude Code](https://claude.com/claude-code)", "(Generated with Claude Code)",
                     "_Generated with Claude Code_"):
            with self.subTest(line=line):
                self.assertEqual(attribution_in(f"fix(x): y\n\nWhy.\n\n{line}\n"), [line])

    def test_a_quoted_footer_is_a_mention(self):
        message = 'fix(x): y\n\n("This PR was generated with Release Please") is its footer.\n\nRefs #1\n'
        self.assertEqual(attribution_in(message), [])

    def test_a_trailer_outside_the_last_paragraph_is_prose(self):
        message = "fix(x): y\n\nCo-authored-by: lines are what GitHub adds.\n\nRefs #1\n"
        self.assertEqual(attribution_in(message), [])


class InlinePython(unittest.TestCase):
    def test_a_program_that_compiles_passes(self):
        script = "set -e\nx=\"$(python3 -c '\nimport sys\nprint(\"the budget ceilings\")' \"$a\")\"\n"
        self.assertEqual(inline_python_in(script), [])

    def test_an_apostrophe_that_ends_the_quote_fails(self):
        # bench.sh's verdict as #14 left it: the shell's quote ends at
        # "budget's", and Python is given the program up to there.
        script = "\n".join([
            "verdict=\"$(python3 -c '",
            "import sys",
            "if p50 > 5:",
            "    print(\"past the budget's ceilings\")",
            "    sys.exit()' \"$results\")\"",
        ])
        self.assertEqual(inline_python_in(script), [(1, "unterminated string literal (detected at line 4)")])

    def test_options_before_the_program_are_read(self):
        self.assertEqual(len(inline_python_in("python3 -I -c 'print(\"x)' \n")), 1)

    def test_a_quote_spelled_as_the_shell_spells_it_passes(self):
        for quote in ("'\\''", "'\"'\"'"):
            script = "python3 -c 'print(\"the budget" + quote + "s ceilings\")' \"$a\"\n"
            self.assertEqual(inline_python_in(script), [], quote)


# The fuzz step of extended.yml, its program indented with the block.
WORKFLOW = """jobs:
  fuzz:
    steps:
      - name: Fuzz
        env:
          SHARD: ${{ matrix.shard }}
        run: |
          # This shard's targets.
          ctest --show-only=json-v1 | python3 -c '
          import json, sys
          for at, test in enumerate(json.load(sys.stdin)["tests"]):
              if at % 3 == int(sys.argv[1]) and ${{ matrix.on }}:
                  print(test["name"])
          ' "$SHARD"
      - run: |
          echo next
        shell: bash
"""


class WorkflowBlocks(unittest.TestCase):
    def test_a_blocks_script_loses_its_indentation_and_ends_with_it(self):
        blocks = run_blocks(WORKFLOW)
        self.assertEqual([first for first, _ in blocks], [8, 16])
        self.assertTrue(blocks[0][1].startswith("# This shard's targets.\nctest"))
        self.assertIn("\nimport json, sys\n", blocks[0][1])
        self.assertEqual(blocks[1][1], "echo next")

    def test_an_indented_program_compiles(self):
        script = run_blocks(WORKFLOW)[0][1].replace("${{ matrix.on }}", "0")
        self.assertEqual(inline_python_in(script), [])

    def test_an_apostrophe_in_a_blocks_program_fails(self):
        script = run_blocks(WORKFLOW.replace('print(test["name"])', 'print("the test\'s name")'))[0][1]
        self.assertEqual(len(inline_python_in(script.replace("${{ matrix.on }}", "0"))), 1)


if __name__ == "__main__":
    unittest.main()
