#ifndef KEST_PROJECT_H
#define KEST_PROJECT_H

#include <stdint.h>

#include "mem.h"

// What a project says about itself, which is a file of `name value` lines and
// nothing more: no sections, no quoting, no nesting, no parser worth the name.
// A manifest is read by a person as often as by a tool and there is nothing
// here that wants a shape, so what it is written in is the simplest thing that
// can say it. See D982.
//
// The whole grammar: a line is a word, a space, and the rest of the line. A
// line whose first character is `#` is a comment; a blank line is nothing. A
// name this does not know is refused, because a manifest with a misspelt line
// reads exactly like one without it.
#define KEST_PROJECT_FILE "kest.project"
#define KEST_MOST_SOURCES 16

typedef struct {
    // What the project is called, which is what `kest new` was given.
    const char *name;
    // The file to run, check or build when nothing else is named.
    const char *entry;
    // Which version of this language it was written against, so a reader and a
    // tool find out from the project rather than from a wiki.
    const char *needs_kest;
    // Which deterministic profile it is written under, said as the name and
    // the number `kest_profile` answers with.
    const char *profile;
    // Where its own modules are, and where the ones it depends on are. A
    // dependency here is a path and nothing else: there is no registry, so a
    // dependency is a directory somebody put there.
    const char *sources[KEST_MOST_SOURCES];
    uint32_t source_count;
    // Where the programs that check this project are.
    const char *tests;
    // Where it was read from, so a message about it says where.
    const char *path;
} KestProject;

// Reads a manifest. `where` is a directory or a file; a directory is looked in
// for `kest.project`. Answers NULL when there is none, which is not a mistake:
// a file on its own is a program and needs no project around it. `why` is what
// was wrong when there was a file and it could not be read.
KestProject *kest_project_read(KestArena *arena, const char *where,
                               const char **why);

// The same, from the text of a manifest handed over rather than read, which is
// how a release binary is a project with no files around it. `handed` NULL is
// `kest_project_read`. See D1172.
KestProject *kest_project_from(KestArena *arena, const char *where,
                               const char *handed, const char **why);

// Where the manifest for `where` is, written into `path`.
void kest_project_path(const char *where, char *path, size_t room);

// The one a project would be written as, for `kest new`. Answers the bytes,
// which the caller writes wherever it likes.
const char *kest_project_written(KestArena *arena, const char *name);

#endif
