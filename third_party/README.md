# Third-party code

Everything here is pinned to an exact revision, never fetched during configure
or build (ADR-0005 as amended by ADR-0084), and unchanged from upstream except
for the build file we add beside it. Size, format, and em-dash checks do not
apply to vendored files; the code is upstream's, not ours.

| Name | Upstream | Revision | License | What is vendored |
| --- | --- | --- | --- | --- |
| Kest | `Rawframe-Project/kest` | `ed4d2ea5380ac113420b670180d3d9716c3ad8a6` | MIT | `include/`, `src/` except `main.c`, `lib/`, `LICENSE` |

To move a pin, run `tools/update_kest.sh <kest checkout> <revision>`, build,
run the full check, and commit the result with the new revision in this table.
