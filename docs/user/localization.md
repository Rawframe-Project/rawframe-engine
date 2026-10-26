# Localization

Every word a game shows comes from a string table by key, so it can be translated without touching the game. How it works is in [localization internals](../internals/localization.md).

## String tables

A `text` line names a string table (`*.strings`): its source locale and, for each key, a message and a description for translators.

```json
{
  "formatVersion": 1,
  "kind": "text.strings",
  "sourceLocale": "en",
  "entries": {
    "hud.hit": { "message": "Hit!", "description": "Shown when the player's runner is hit." },
    "hud.score": {
      "message": ".input {$points :integer}\n.match $points\none {{{$points} point}}\n* {{{$points} points}}",
      "description": "The player's score, which counts the hits they took."
    }
  }
}
```

Messages are Unicode MessageFormat 2.0, in a closed subset: `{$argument}` placeholders, `:string`, `:integer` and `:number`, and `.match` on plural categories (`zero`, `one`, `two`, `few`, `many`, `other` as each language has them), integers or quoted literals, with `*` for the rest. Anything outside it is refused when the document is read, never when a player sees it.

## Translations

Another `text` line names a translation (`*.<locale>.translations`): the table it translates, its locale, and each key's translated message with the hash of the source it was made from.

```json
{
  "formatVersion": 1,
  "kind": "text.translations",
  "table": "aefdab1e47c086ddf189f56bbb92c4f0",
  "locale": "tr",
  "entries": { "hud.hit": { "message": "Vuruldun!", "sourceHash": "17ead5c6ce1de56f" } }
}
```

A translation whose source changed since is reported stale (`text_stale`) and still shown. A translated message may not read an argument its source does not, and a translation of a key the table lacks is refused.

## Locales

A `locale` line sets the game's default. `localization.locale` picks a client's locale; a key falls back from it to the game's default and then to the table's source locale, which always has the message. Locale tags are `language[-Script][-REGION]`: `tr`, `pt-BR`, `zh-Hant`, `es-419`. Numbers are written in the locale's own digits and grouping.

## Showing words

`label <16 hex digits> <table> <key> [<argument>]` lines name words a UI node shows, with up to two arguments the node gives ([UI](ui.md)).

## Checking

`localization.pseudo` makes every message look translated (accented, padded to a translation's length, bracketed), and `localization.show_keys` shows keys in place of words, so hard-coded text, clipped layouts and missing translations show before a translator is asked. Pseudo-localization exists in development builds only.
