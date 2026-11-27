---
name: verifying-a-change
description: Plays the changed engine the way a player would, with a sample game, headless bots and optionally a drawn frame, and reads what the run logged. Used before pushing any change a played game can show (gameplay, Kest doors, replication, physics, navigation, animation, audio, rendering, content and cooking), and to write the Verification section of its pull request.
---

# Verifying a change

Tests say the parts agree with what their tests expect. This says the engine still plays: a server, its replication, Kest and bots running one of the sample games with the tree as it is now. It is the step before the push, after `tools/check.sh changed` has passed.

Skip it for a change no played game can reach: documentation, the check tools, a module's internals already held by its tests with no caller in a game. Say in the pull request that no game reaches the change.

## 1. Choose the game

Pick the sample that runs the changed code; when two apply, run both.

| The change touches | Game | Notes |
|---|---|---|
| Admission, connections, replication, interest, prediction | `arena` | raise `network.loopback.latency_ms` with `--set` to stress prediction |
| 2D physics, characters, canvas drawing, HUD, audio, mods, Builds | `runners` | the most exercised sample |
| 2D bodies and contacts | `crates`, `ledges` | `ledges` adds a tilemap |
| 3D physics, cooked meshes, joints, 3D drawing | `plaza` | |
| Buoyancy, physics written in Kest, effects heard | `gale` | |
| Voxels, large replication, saves, game mods | `quarry` | `quarry-canyon` is its mod |
| Navigation, crowds steering, UI text and fonts, animation from cooked figures | `tycoon` | |
| Many entities, interest at scale | `crowd` | heavy: keep the run short |
| Lights, shadows, materials | `lighting` | always with `--draw` |
| Hands, headsets | `reach` | flat clients play it too |

The configuration keys a test uses for that game (`hosts/arena/tests/<game>*.conf*`) show which settings exercise what; pass any of them with `--set`.

## 2. Play it

From the repository root, after the tree is configured:

```sh
.agents/skills/verifying-a-change/verify_game.sh runners
.agents/skills/verifying-a-change/verify_game.sh --bots 8 --seconds 20 plaza
.agents/skills/verifying-a-change/verify_game.sh --draw lighting          # also writes frame.png
.agents/skills/verifying-a-change/verify_game.sh --set network.loopback.latency_ms=80 arena
```

The script builds the arena and the cook tool, cooks the game into a fresh folder, plays it, and prints:

- the exit code and the last lines of standard error,
- each warning, error and critical record, one line per code with its count,
- every `*_summary` record (ticks, bots admitted, bodies made, frames drawn, Kest's counts, memory),
- a verdict: `FAILED` when the arena failed, logged an error, or stopped before its summaries.

Builds and the run go through the workspace's `heavy.sh`, so they may wait for a slot. On Windows, run the script on Linux with `"$RAWFRAME_ROOT/tools/linux.sh" .agents/skills/verifying-a-change/verify_game.sh <game>`, or natively against the Direct3D 12 tree with `--tree out/windows-development`.

## 3. Judge the run

A clean verdict is not the goal; the numbers are. Before running, write down what the change should move and what it must leave alone, then compare:

- **What should move did.** A fix for refused bodies shows `bodiesRefused` at 0; a new door shows its calls in `kest_summary`; a replication change shows in the bytes or entities sent.
- **What should not move did not.** Bots admitted equals bots asked for, no new warning code appears, and the summaries of an unrelated system read as they do on `main`. When unsure what `main` gives, the pass condition of the game's test in `hosts/arena/CMakeLists.txt` is the reference.
- **The picture.** With `--draw`, read `frame.png` with the Read tool and look at it: the change is visible where it should be, and nothing else is missing, black or garbled. A visual change attaches this frame, before and after, to the pull request instead of a test.

A warning the change introduced is a finding, even when the verdict is clean. An error is a failure: fix the code, never the run's settings, and never run again hoping for a clean one. After three attempts that do not turn the run clean, stop and report what each showed, as the workspace guide says.

## 4. Changes a bot run cannot reach

- **Authoring and Studio.** Drive the same operations headless: `rawframe-author describe`, a `session` fed JSON records, or the `mcp` tools ([scenes and authoring](../../../docs/user/scenes-and-authoring.md)). Studio's own window is never the proof.
- **Windows, input devices, the client program, the browser.** Run the integration tests of the area through the wrapper: `"$RAWFRAME_ROOT/tools/heavy.sh" ctest --test-dir out/clang-development -L integration -R <area>` (on Windows, inside `tools/linux.sh`). They start their own Xvfb; kill only a PID you started.
- **A dedicated server.** `rawframe-server` with a game's configuration ([dedicated server](../../../docs/user/dedicated-server.md)) and `rawframe-author connect` to read its World.

## 5. Report it

The pull request's Verification section names what ran and what it showed, short enough to read in the diff view:

```text
verify_game.sh --bots 8 crates: clean, 8/8 admitted, bodiesMade 19, bodiesRefused 0
(main gives 19 and 0); no warning codes.
verify_game.sh --draw plaza: frame attached, mound and walkers drawn, shadows as before.
```

The work folder the script prints keeps the configuration, the full log and the frame; quote from it, never commit it.
