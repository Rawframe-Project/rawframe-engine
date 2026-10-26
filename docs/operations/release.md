# Release

Versions follow Semantic Versioning, tagged `vX.Y.Z`. Before 1.0 any release may change the API, the file formats and the wire protocol; there is no stability promise.

1. release-please keeps a draft release pull request, `chore: release X.Y.Z`, open from the conventional commits on `main` (`release.yml`). A title with `!` and a `BREAKING CHANGE:` footer makes it a minor release before 1.0, never a patch.
2. Before merging it, rewrite its entry in `CHANGELOG.md` into what changed for someone making a game with the engine, grouped as Added, Changed, Fixed, Removed and Security, leaving out refactors, tests and CI. Put the same text in the pull request body between release-please's `---` lines: the GitHub release takes its notes from there. release-please rewrites both whenever anything else lands on `main`, so this is the last change before the merge.
3. The manager checks that the tag does not exist yet and merges. The merge tags the version and publishes the GitHub release; `release.yml` then builds, on a hosted runner from a clean checkout of the tag and never on the shared self-hosted runner, the source archive of the tagged tree, its `SHA256SUMS`, an SPDX SBOM and a build provenance attestation, and attaches them.

The version is written once, in the root `CMakeLists.txt` (`project(... VERSION ...)`, marked for release-please), and reaches the programs that report it from there.
