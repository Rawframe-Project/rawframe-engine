# Authoring

A game is made without Studio. Every change to an authored document is one of a closed set of typed operations, run the same way by a command line, an agent, a test or Studio. The modules are `scene` (scene documents), `authoring` (deltas, transactions, histories and operations), `authoring_session` (a session holding a game open, shared over QUIC and offered over MCP), `world_tooling` (a running game's tooling endpoint) and `studio` (the editor). No runtime's closure reaches any of them ([overview.md](overview.md#closures)).

## Contents

- [Documents](#documents)
- [Operations, deltas and transactions](#operations-deltas-and-transactions)
- [Histories](#histories)
- [Sessions](#sessions)
- [Agents](#agents)
- [Sharing a session](#sharing-a-session)
- [Previews and running games](#previews-and-running-games)
- [Studio](#studio)

## Documents

Authored sources are text: scenes, prefabs, UI documents and styles, materials, animation documents, action sets, mixers and sounds, string tables, blockouts, terrains, tilemaps, scatters, splines and sequences, each in one canonical form, so the same meaning always gives the same bytes, diffs stay small and merges are meaningful. Binary data appears only where it is a natural external source (a PNG, a glTF, a WAVE) or a cooked artifact. Preview state is never source truth.

A scene's entities are named by a `SourceEntityId`, stable across edits; their components are keyed by type and recorded with the layout mark they were authored against, so a scene written against an older layout of a component can be migrated (`rawframe-author migrate`), field by field, or refused naming what does not carry over. Nothing is silently dropped.

## Operations, deltas and transactions

Operations are the only public way to change an authored document: create, rename, reparent and delete entities, add and remove components, set fields, instance prefabs, and the like, each listed with its arguments by `authoring.describe`. They address what they change by identity: an entity by its `SourceEntityId`, a component by its type, a field by its name in the component's layout. A game's `field` lines add hints (ranges, units, colors, value names, asset kinds) that an editor shows and an agent reads; an operation may still set any value the field's type holds.

Each operation decomposes into **deltas**: one slot of a document, its value before and after, both complete and explicit, so a delta applies forward or backward with nothing but itself, and a journal of them persists as it stands. The kinds are closed (create and destroy a node, reorder, set a field and the rest). Deltas are staged in a **transaction** bound to the document generation it read; a transaction against a changed document is refused as a conflict, never merged blindly. Between processes, a document's generation is its content digest, which a request's `expects` names.

## Histories

Each document keeps its history: entries oldest first, an undo point, where the saved document stands among them, and eviction by named limits, redo first, then oldest. Undo and redo put back the selection and the view from around the change they undo. Each entry has an author, and undo is per author: an author's undo takes back that author's latest change. Choosing and viewing never make a document dirty.

## Sessions

An authoring session reads a game once and keeps its documents open with their histories between requests. It speaks records, one JSON object a line each way, the same request, query and discovery documents the one-shot verbs take. `rawframe-author session <game>` holds one on standard input and output; Studio holds one in its own process and speaks exactly the same records, so Studio can do nothing a session client cannot. Each change is written to its file as it commits, whole, by rename. Long operations (cooking, importing) report progress and can be cancelled.

## Agents

`rawframe-author mcp` offers a session over the Model Context Protocol: every operation `authoring.describe` lists is a tool named for it, and so are the session's own verbs an agent needs (the catalog and assets, undo and redo, history, a new scene, batches, cooking, importing). The bridge only translates: each call becomes one session record and its reply the call's result, and the tools are read from the session's own discovery, never listed a second time, so an agent gains no operation a session client lacks and loses none.

## Sharing a session

A shared session is one session several collaborators speak to over QUIC, on its own application protocol (`rawframe-authoring-v1`), with the host ordering every record as it arrives. A collaborator's hello names a token the host's owner issued, which holds a name and grants (`read`, `edit`, `cook`, `play`, `claim`); a record its token does not grant is refused, and a record's author is the token's name, so no collaborator acts as another. After a record changes a document every other collaborator is told and reads it again from the host, the documents' only holder; presence and claims are broadcast at most ten times a second. A token removed from the tokens file disconnects its holder at once.

## Previews and running games

A running client or server can serve a tooling endpoint ([../operations/debugging.md](../operations/debugging.md#a-running-game)). A session attaches a client's endpoint to a scene: the view each scene is told or stepped to is shown there, edits to a scene reach the running World as the difference between what it authored and what it authors now, and the played game's pictures and input can stream through the editor's viewport. The profiler, network panel and live World read the same endpoint.

## Studio

Studio is a convenience over the session: a window drawn by the engine's own UI renderer, whose panels speak session records and tooling requests. Its surfaces (panels, viewport modes, toolbar commands and the rest) are listed in `modules/studio/src/surfaces.h`, each with the headless path it stands on, and `tools/check_repo.py` refuses a surface without one, as it refuses a table over its budgets (fourteen panels, eight viewport modes, ten toolbar commands). Studio extensions are Kest programs (`rawframe.studio`) run untrusted by the session, adding commands; there is no visual scripting and no editor-only form of a game.
