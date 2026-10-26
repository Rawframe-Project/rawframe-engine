# Mods

A game can let players' mods add content and behavior at points the game declares. A mod's code runs untrusted, on a Kest machine of its own with its own limits. How it works is in [Kest in the engine](../internals/kest.md#mods).

## What a game declares

```
mods curated
modapi mygame 2
approve someone/better-crates
extension crates data mygame.crate multi
extension crate_opened event mygame.opened after mygame.open write mygame.loot multi
extension pricing service mygame.price exclusive
extension spawn_rules replacement mygame.spawn exclusive
prefer pricing someone/economy
```

- `mods closed|curated|open`: no mods (the default), only the mods the game `approve`s, or any correctly signed mod that meets the Mod API.
- `modapi <namespace> <version>`: the game's Mod API, which mods are written against.
- `extension` lines declare points:
  - a **data** point takes values of one component, as scenes whose entities hold that component and nothing else, never executed;
  - an **event** point takes handlers, Kest functions run as systems right after the named game system, over the entities holding the event's component, writing only what the point lets them;
  - a **service** point takes one provider, a function the game calls through `Mods.<name>(value)`, which rewrites the value it is lent;
  - a **replacement** point lets one mod run its own function in place of a game system, over the same columns and in the same place; a predicted system is never replaceable.
- `multi` or `exclusive` says whether one or many mods may contribute; `required` makes a Composition invalid without one; `prefer` says which mod keeps an exclusive point when several claim it.

## What a mod is

A mod's description names its one target game, the Mod API versions it was written against, and what it contributes:

```
target someone/mygame
modapi >=2 <3
contribute crates crates.scene
program loot.kest
handle crate_opened opened
provide pricing price
```

A mod with code names one program, compiled from the `kest.project` beside it. It runs on its own machine under Kest's untrusted profile, with only doors marked safe for untrusted code, its own heap (`kest.mod_heap_bytes`) and fuel per handler (`kest.mod_fuel_per_handler`). A handler that fails or runs out of fuel changes nothing.

## Playing with mods

Mods are cooked and packed like games, and a Composition names the game's Build and the exact mods beside it (`rawframe-build compose ... --mod <mod root>`). Whether the game takes them is decided when the Composition is opened, under the game's policy. A checkpoint records the mods its World ran with.
