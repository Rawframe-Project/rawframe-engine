#ifndef KEST_LOADER_H
#define KEST_LOADER_H

#include "parser.h"

// One file, parsed, with the name its declarations live under. A module
// `game.world` puts its names under `world`, so a file that imports it writes
// `world.Npc` and the file itself may write `Npc`.
//
// Everything here is filled in before the unit is handed on, and the fields
// that come off a `module` line have an answer for a file that has none: the
// loader zeroes a unit before it reads anything into it, so a reader of one
// never has to ask whether a field was reached.
typedef struct {
    // The path as it will be reported and the text as it was read. Always
    // both: a file that could not be read is not a unit.
    KestSource source;
    // What parsed, which is everything the parser could make of the file
    // rather than everything the file holds: a parse error is reported and the
    // walk goes on, so this is filled in for a file with mistakes in it.
    KestUnit unit;
    // The last part of what the file calls itself, or `""` for a file that
    // names no module. Those are legal and their names live under nothing,
    // which is what a program written on the spot to be run once does.
    const char *alias;
    // And the whole of it: `a.math` where the alias is `math`. Two modules
    // may share a last part and cannot share this, so this is what says which
    // of two `math` a name came from. `""` for a file that names no module,
    // the same as the alias. See D1039.
    const char *module;
    // Whether this file is the library's. `std` is the one name a program
    // cannot use, and which side of that a file is on decides where it is read
    // from — so it is decided here, once, rather than by reading the module
    // line again wherever the answer is wanted. False for a file that names no
    // module, which cannot be the library's.
    bool from_library;
    // The aliases this file may reach, which is what it imports and its own.
    // Filled in after the imports have been followed, so it holds what was
    // written even when one of them could not be read.
    const char **imports;
    // And the whole module name each of those was written as, in step with
    // them. An alias says what a file may write in front of a name and this
    // says which module that alias meant, which is the difference between a
    // program that may hold two `math` and one that may not. See D1039.
    const char **import_paths;
    uint32_t import_count;
    // And which of them a name in this file was reached through, marked where
    // the reach is decided. An import nothing reaches is a module read,
    // parsed, checked and compiled for a file that never writes its name, and
    // what that costs is the whole of it. See D725.
    bool *import_reached;
} KestUnitInfo;

typedef struct {
    KestUnitInfo *items;
    uint32_t count;
    uint32_t capacity;
    // Where `std` was read from, kept because the question a checker asks of
    // the library is about a module no file imported — which is a module that
    // is not here at all, and so cannot be found by looking at what is. NULL
    // for a read that follows no imports, which has no library to speak of.
    // See D734.
    const char *library;
    // Where the trees are, which is not where everything else is. A tree is
    // read by the checker and by the compiler and by nothing after them, so it
    // is given back when the last copy has been compiled. NULL for a read that
    // parses into whatever arena it was handed, which is what the commands
    // that stop at a tree want. See D748.
    KestArena *trees;
    // The files this read was handed, when it was handed them: then they are
    // every file there is, and nothing is read from a disk or looked for on
    // one. NULL for a read of files where they are. See D1172.
    const KestFile *handed;
    uint32_t handed_count;
    // The manifest that said where imports resolve from, where one did, and
    // what it said: part of what a program was read from, which a release
    // binary carries with the files. See D1172.
    const char *manifest_path;
    const char *manifest_text;
} KestUnits;

// Reads a file, follows its imports, and parses everything reachable. An
// import names a path relative to the file that wrote it: `import game.world`
// is `game/world.kest` beside it. Returns false when a file cannot be read or
// the host is out of memory; a parse error is reported and does not stop the
// walk.
// Reads every file named and everything they import. The first one sets the
// root that imports resolve from, so a project is checked as a project rather
// than as whatever its entry point happens to reach.
// `library` is where `std` lives, which is the one name a project cannot use
// for itself. Everything else resolves from the root the first file settles.
bool kest_load_many(KestArena *arena, KestDiags *diags, const char *library,
                    char **paths, int count, KestUnits *units);

// What the loader still holds when a build is done, by what asked for it. See
// D785.
void kest_units_hold(const KestUnits *units, uint32_t *files, uint32_t *lines,
                     uint32_t *paths, uint32_t *names);

// Whether the library has a module of this name, asked by looking for the file
// it would be read from. What this answers is about the installation and not
// about the program: a module nothing imports is in no program, so the only
// way to know it could have been imported is to ask where it would come from.
// False for a library that is nowhere, and for a name that could not be one.
// See D734.
bool kest_library_has(const char *library, const char *name, size_t length);

// Where the standard library is: what `KEST_LIB` says, or `lib/` beside the
// program, which is where it is when nothing has been installed.
// `arena` may be NULL, in which case the answer is not owned by one and is
// good until the next call.
const char *kest_library_path(KestArena *arena, const char *program);

// The two ways to read one file, named for how far each goes. A file that
// cannot be read is refused the same way by both.
//
// The source and nothing else. `lex` is the whole of what this is for: the
// token stream is what that command answers, and parsing to reach it is work
// nobody asked for and a second reading of the same file.
// Puts a buffer in front of the disk for one path, takes that one away again
// with a NULL text, and takes every one away with a NULL path. What it is for
// is a language server: the files open in an editor have not been saved, and
// answering about the saved copies is answering about different files. There
// is a set rather than one because a program is more than one file and an
// editor holds more than one open. The text is the caller's and has to outlive
// every read. See D977 and D1143.
void kest_loader_overlay(const char *path, const char *text, size_t length);

bool kest_read_source(KestArena *arena, KestDiags *diags, const char *path,
                      KestSource *into);

// The source and the tree it makes, following nothing it imports. `parse` and
// `fmt` want that: printing a file back does not depend on what it imports
// being there.
bool kest_read_unit(KestArena *arena, KestDiags *diags, const char *path,
                     KestUnits *units);

#endif
