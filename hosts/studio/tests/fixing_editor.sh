#!/usr/bin/env bash
# An author's editor in Studio's tests (D453): opened at a diagnostic as
# `fixing_editor.sh --goto <file>:<line>:<column>`, it says how it was
# opened, in the file STUDIO_EDITED names, and fixes the file as the
# author would, deleting its last line, which the test made the error.
set -euo pipefail

echo "editor opened $*" >>"$STUDIO_EDITED"
where=${2%:*:*}
sed -i '$d' "$where"
