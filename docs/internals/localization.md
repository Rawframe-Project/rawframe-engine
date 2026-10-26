# Localization

How a client shows words in the player's language. The modules are `localization` (locales, messages, numbers, plurals and catalogs) and `world_localization` (a game's text read from its content). Localization is presentation: a dedicated server never links it, and gameplay never depends on the words shown.

## Documents

A game's `text` lines name its **string tables** (`*.strings`), each a key and a message in the source language, and their **translations** (`*.<locale>.translations`), each a key and its translated message. Both are documents in one canonical form. A catalog is built from them together and checks what needs more than one document: each translation's table is in the catalog, each translated key is in its table, a translated message reads no argument its source does not, no two translations share a table and a locale, and none is into its table's source locale. A translation whose source message changed since it was made (its `sourceHash` differs) is reported stale (`text_stale`), not dropped. A game whose text does not make a catalog does not start.

UI nodes and other presentation name their words by key, with up to two arguments ([ui.md](ui.md#text)).

## Messages

Messages are a closed subset of Unicode MessageFormat 2.0, read once and checked whole, then formatted as often as asked: text with `{$argument}` placeholders, `.input` and `.local` declarations, the functions `:string`, `:integer` and `:number`, and `.match` selection on a CLDR plural category, an integer, a quoted literal or `*`. Markup, attributes and every other function are refused. Anything outside the subset is refused when the document is read, never when a player sees it.

## Locales

A locale is a BCP 47 subset, `language[-Script][-REGION]` (`tr`, `zh-Hant`, `es-419`); variants, extensions and private use are refused. A tag from a player or the platform is canonicalized through CLDR's aliases and likely subtags, and a key resolves along the fallback chain from the requested locale, the project's default, and the table's source locale, which always has the message; it is formatted in the locale that served it. `localization.locale` picks the client's locale; the game says which a player may choose among.

## Numbers and plurals

CLDR 48.1.0 is compiled ahead of time into code and tables by `tools/generate_cldr.py`: number symbols, digits and grouping for every locale of modern coverage, and the cardinal and ordinal plural rules. No CLDR file is read and no rule text is evaluated at run time, and nothing asks the platform how to write a number, so the same value and locale give the same bytes everywhere. A plural category is chosen from the number as it is written, not as it is worth: `1` and `1.0` are different operands.

## Pseudo-localization

`localization.pseudo` makes every message look translated, changing only its text: each ASCII letter accented, `~` padding to the length a translation would take, and `[` `]` around it so a clipped end shows, and `localization.show_keys` shows keys in place of words, so untranslated strings, clipped layouts and mirrored text show up before a translator is asked. Pseudo-localization is development only: a shipping build refuses to compile anything that includes it.
