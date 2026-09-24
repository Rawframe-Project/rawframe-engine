#include "loader.h"

#include <limits.h>

#include "project.h"

#ifndef KEST_LIB_DIR
#define KEST_LIB_DIR "/usr/local/lib/kest/"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Reads the whole file into arena memory, terminated so the lexer can look one
// byte past the end without a bounds check on every character.
// A stream that cannot be measured is read until it ends. A pipe is one, which
// is what a shell hands over for `kest check <(...)`, and it used to come back
// as a file this could not read.
// Whether what was read was read. A directory opens and refuses to be read, and
// which of the two ways of reading a file notices that is a thing about the
// disk: one filesystem says a directory is nought bytes and the next says nine
// quintillion, so one goes down the stream and the other down the sized read.
// Both ask here, so there is one answer rather than one for each way in. See
// D865.
static bool read_failed(FILE *file) {
    return ferror(file) != 0;
}

static char *read_stream(KestArena *arena, FILE *file, size_t *length) {
    size_t room = 4096;
    size_t used = 0;
    char *text = kest_arena_alloc(arena, room, 1);
    while (text != NULL) {
        size_t read = fread(text + used, 1, room - used - 1, file);
        used += read;
        if (read == 0 || feof(file)) {
            break;
        }
        if (used + 1 < room) {
            continue;
        }
        char *grown = kest_arena_alloc(arena, room * 2, 1);
        if (grown == NULL) {
            return NULL;
        }
        memcpy(grown, text, used);
        text = grown;
        room *= 2;
    }
    if (text == NULL || read_failed(file)) {
        return NULL;
    }
    text[used] = '\0';
    *length = used;
    return text;
}

// What an editor has in buffers that are not on the disk yet, read before the
// disk is: a language server that answered about the saved copy would answer
// about a file the person in front of it is not looking at. There is more than
// one, because an editor holds more than one open and a program is more than
// one file -- checking the file being typed in reads everything it imports, and
// an import that is itself open and unsaved has to be read as the person can
// see it rather than as it was last saved. Set and cleared by whoever is
// driving; nothing else in this tree ever sets one. See D977 and D1143.
typedef struct {
    const char *path;
    const char *text;
    size_t length;
} Overlaid;

static Overlaid *overlaid;
static size_t overlaid_count;
static size_t overlaid_room;

// A NULL path takes every one away, which is what a driver does when it has
// finished with the set. A NULL text takes away the one path names, which is
// what a client closing a document means. Giving room can fail, and a path
// that could not be held is read from the disk: the answer is then about the
// saved copy, which is where this began, rather than about nothing.
void kest_loader_overlay(const char *path, const char *text, size_t length) {
    if (path == NULL) {
        free(overlaid);
        overlaid = NULL;
        overlaid_count = 0;
        overlaid_room = 0;
        return;
    }
    for (size_t i = 0; i < overlaid_count; i++) {
        if (strcmp(overlaid[i].path, path) != 0) {
            continue;
        }
        if (text == NULL) {
            overlaid[i] = overlaid[overlaid_count - 1];
            overlaid_count--;
            return;
        }
        overlaid[i].text = text;
        overlaid[i].length = length;
        return;
    }
    if (text == NULL) {
        return;
    }
    if (overlaid_count == overlaid_room) {
        size_t grown = overlaid_room == 0 ? 4 : overlaid_room * 2;
        Overlaid *moved = realloc(overlaid, grown * sizeof(*moved));
        if (moved == NULL) {
            return;
        }
        overlaid = moved;
        overlaid_room = grown;
    }
    overlaid[overlaid_count].path = path;
    overlaid[overlaid_count].text = text;
    overlaid[overlaid_count].length = length;
    overlaid_count++;
}

static const char *tidied(KestArena *arena, const char *path);

// One of the files a read was handed, or NULL. A path is a path whichever way
// it was spelled, so both sides are tidied before they are compared.
static const KestFile *handed_file(KestArena *arena, const KestUnits *units,
                                   const char *path) {
    for (uint32_t i = 0; units != NULL && i < units->handed_count; i++) {
        const char *tidy = tidied(arena, units->handed[i].path);
        if (tidy != NULL && strcmp(tidy, path) == 0) {
            return &units->handed[i];
        }
    }
    return NULL;
}

static char *read_file(KestArena *arena, const KestUnits *units,
                       const char *path, size_t *length) {
    if (units != NULL && units->handed != NULL) {
        const KestFile *file = handed_file(arena, units, path);
        if (file == NULL || file->text == NULL) {
            return NULL;
        }
        char *held = kest_arena_alloc(arena, file->length + 1, 1);
        if (held == NULL) {
            return NULL;
        }
        memcpy(held, file->text, file->length);
        held[file->length] = '\0';
        *length = file->length;
        return held;
    }
    for (size_t i = 0; i < overlaid_count; i++) {
        if (strcmp(overlaid[i].path, path) != 0) {
            continue;
        }
        char *held = kest_arena_alloc(arena, overlaid[i].length + 1, 1);
        if (held == NULL) {
            return NULL;
        }
        memcpy(held, overlaid[i].text, overlaid[i].length);
        held[overlaid[i].length] = '\0';
        *length = overlaid[i].length;
        return held;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    // Not a mistake: a stream that cannot say how long it is is read to the end
    // instead of being refused for not knowing. `LONG_MAX` is the same answer
    // said the other way round — a directory on one filesystem here measures
    // nought and on the next measures nine quintillion, and nothing has that
    // many bytes in it. Asking the arena for them is how the second was caught
    // before this: not by the read failing but by an allocation nobody could
    // ever have made, which is the right answer for the wrong reason and a
    // different reason on every disk. See D865.
    if (size < 0 || size == LONG_MAX) {
        char *text = read_stream(arena, file, length);
        fclose(file);
        return text;
    }

    char *text = kest_arena_alloc(arena, (size_t)size + 1, 1);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(text, 1, (size_t)size, file);
    // A read that failed is not a file this read. A directory opens and refuses
    // to be read, and reading nought bytes of it fails at nothing — so one more
    // byte than there is said to be is asked for, whatever that number was.
    //
    // Always, rather than only when a file measured nought. How many bytes
    // there are to read is what the filesystem says a directory is, and that is
    // nought on one and four thousand and ninety-six on the next: on the first
    // this asked and caught it, and on the second it read four thousand bytes
    // that were not there, failed, and was caught by `ferror` instead. The same
    // compiler said `this file declares nothing` about a directory on one disk
    // and `cannot read` on another, and the hole written to catch that caught
    // it on one disk only. Asking past the end is the same question on both: a
    // file gives end-of-file and a directory gives an error. See D865.
    fgetc(file);
    bool broke = read_failed(file);
    fclose(file);
    if (broke) {
        return NULL;
    }
    text[read] = '\0';
    *length = read;
    return text;
}

// What a byte in a path means. A separator is the one thing about a path that
// belongs to the platform rather than to the program, and D953 said a port
// changes this and nothing else in this file. This is that port: on Windows a
// path is separated by either byte and a name may hold neither, so both are
// read; everywhere else a backslash is a character a filename may have and
// reading it as a separator would cut a name in half. What is *written* is
// always `/`, which every platform this builds on accepts. See D970.
#if defined(_WIN32)
#define KEST_PATH_SEPARATOR(c) ((c) == '/' || (c) == '\\')
#else
#define KEST_PATH_SEPARATOR(c) ((c) == '/')
#endif

// Where the last separator in a path is, or NULL. Every reading of a path in
// this file goes through it.
static const char *last_separator(const char *path) {
    const char *found = NULL;
    for (const char *at = path; *at != '\0'; at++) {
        if (KEST_PATH_SEPARATOR(*at)) {
            found = at;
        }
    }
    return found;
}

// The directory a path is in, with its separator, or an empty string.
static const char *directory_of(KestArena *arena, const char *path) {
    const char *slash = last_separator(path);
    if (slash == NULL) {
        return "";
    }
    const char *kept =
        kest_arena_strndup(arena, path, (size_t)(slash - path) + 1);
    // No room for the directory is the same answer as there being none: what
    // is written round it is a path, and a path built onto nothing is a path
    // that will not be read. See D510.
    return kept == NULL ? "" : kept;
}

// `game.world` under `root` is `root/game/world.kest`. Imports resolve from
// one place rather than from whoever wrote them, so a module path names one
// file however it is reached.
static const char *path_of_import(KestArena *arena, const char *directory,
                                  const char *dotted, size_t length) {
    size_t room = strlen(directory) + length + 6;
    char *path = kest_arena_alloc(arena, room, 1);
    if (path == NULL) {
        return NULL;
    }
    size_t used = (size_t)snprintf(path, room, "%s", directory);
    for (size_t i = 0; i < length; i++) {
        path[used++] = dotted[i] == '.' ? '/' : dotted[i];
    }
    snprintf(path + used, room - used, ".kest");
    return path;
}

// Where the package directories start, worked out from what the file calls
// itself: `module a.b.c` in `x/y/a/b/c.kest` means the root is `x/y/`. A file
// that names no module has only its own directory to go on.
static const char *root_of(KestArena *arena, const char *path,
                           const char *dotted, size_t length) {
    if (dotted == NULL) {
        return directory_of(arena, path);
    }

    size_t room = length + 6;
    char *suffix = kest_arena_alloc(arena, room, 1);
    if (suffix == NULL) {
        return directory_of(arena, path);
    }
    for (size_t i = 0; i < length; i++) {
        suffix[i] = dotted[i] == '.' ? '/' : dotted[i];
    }
    snprintf(suffix + length, room - length, ".kest");

    size_t path_length = strlen(path);
    size_t suffix_length = strlen(suffix);
    if (path_length < suffix_length ||
        strcmp(path + path_length - suffix_length, suffix) != 0) {
        return directory_of(arena, path);
    }
    const char *kept =
        kest_arena_strndup(arena, path, path_length - suffix_length);
    return kept == NULL ? directory_of(arena, path) : kept;
}

static const char *last_segment(KestArena *arena, const char *dotted,
                                size_t length) {
    size_t start = 0;
    for (size_t i = 0; i < length; i++) {
        if (dotted[i] == '.') {
            start = i + 1;
        }
    }
    const char *kept = kest_arena_strndup(arena, dotted + start, length - start);
    // An alias nobody could write is an alias nothing matches, which is what a
    // file that named no module already has. See D510.
    return kept == NULL ? "" : kept;
}

static KestUnitInfo *reserve(KestArena *arena, KestUnits *units) {
    if (units->count == units->capacity) {
        uint32_t grown = units->capacity == 0 ? 8 : units->capacity * 2;
        KestUnitInfo *moved = KEST_ARENA_ARRAY(arena, KestUnitInfo, grown);
        if (moved == NULL) {
            return NULL;
        }
        if (units->count > 0) {
            memcpy(moved, units->items, sizeof(KestUnitInfo) * units->count);
        }
        units->items = moved;
        units->capacity = grown;
    }
    return &units->items[units->count++];
}

// The same file spelled two ways is the same file. A command line names
// `lib/std/random.kest` and an import of it resolves to `./lib/std/random.kest`
// from the library root, and reading both would declare everything in it
// twice — which is what happened, and what it said was that the file disagreed
// with itself.
//
// Only the spellings that arise from putting paths together: a leading `./`, a
// doubled slash, and a step into a directory and back out of it. Two paths
// that reach one file by different routes through the file system are two
// files as far as this is concerned, which is the same answer a compiler that
// reads what it is given has to give.
static const char *tidied(KestArena *arena, const char *path) {
    size_t length = strlen(path);
    char *out = kest_arena_alloc(arena, length + 1, 1);
    if (out == NULL) {
        return path;
    }
    size_t used = 0;
    for (size_t i = 0; i < length;) {
        if (KEST_PATH_SEPARATOR(path[i]) && used > 0 && out[used - 1] == '/') {
            i++;
            continue;
        }
        if (path[i] == '.' && KEST_PATH_SEPARATOR(path[i + 1]) &&
            (used == 0 || out[used - 1] == '/')) {
            i += 2;
            continue;
        }
        // `a/b/../c` is `a/c`, and `../c` at the front is left as it is
        // because there is nothing above it to take away.
        if (path[i] == '.' && path[i + 1] == '.' &&
            KEST_PATH_SEPARATOR(path[i + 2]) && used > 1) {
            size_t back = used - 1;
            while (back > 0 && out[back - 1] != '/') {
                back--;
            }
            bool upwards = used - back == 3 && out[back] == '.' &&
                           out[back + 1] == '.';
            if (!upwards) {
                used = back;
                i += 3;
                continue;
            }
        }
        // A separator is written the one way, so that two spellings of one
        // path are one name here however the platform let them be typed.
        out[used++] = KEST_PATH_SEPARATOR(path[i]) ? '/' : path[i];
        i++;
    }
    out[used] = '\0';
    return out;
}

static bool already_loaded(const KestUnits *units, const char *path) {
    for (uint32_t i = 0; i < units->count; i++) {
        if (strcmp(units->items[i].source.path, path) == 0) {
            return true;
        }
    }
    return false;
}

// What the loader still holds when a build is done. `files` is the list of what
// was read and the shape kept for each of them; `lines` is where every line of
// every file begins, four bytes a line, held so that a message can say `12:7`
// without counting newlines from the top; `paths` and `names` are the text it
// kept that is not the files themselves — where each came from, what each calls
// itself, and what each imports. The source text is not counted here, because
// `read` already says it file by file. See D785.
void kest_units_hold(const KestUnits *units, uint32_t *files, uint32_t *lines,
                     uint32_t *paths, uint32_t *names) {
    *files = (uint32_t)(units->capacity * sizeof(KestUnitInfo));
    *lines = 0;
    *paths = 0;
    *names = 0;
    for (uint32_t i = 0; i < units->count; i++) {
        const KestUnitInfo *one = &units->items[i];
        *lines += (uint32_t)(one->source.line_count * sizeof(uint32_t));
        if (one->source.path != NULL) {
            *paths += (uint32_t)(strlen(one->source.path) + 1);
        }
        if (one->alias != NULL) {
            *names += (uint32_t)(strlen(one->alias) + 1);
        }
        *names += (uint32_t)(one->import_count *
                             (sizeof(const char *) + sizeof(bool)));
        for (uint32_t k = 0; k < one->import_count; k++) {
            if (one->imports[k] != NULL) {
                *names += (uint32_t)(strlen(one->imports[k]) + 1);
            }
        }
    }
}

// `std` is reserved: a module named that always comes from the library, so a
// program cannot shadow one and a reader always knows which is which.
static bool is_library(const char *dotted, size_t length) {
    return length >= 4 && memcmp(dotted, "std.", 4) == 0;
}

// The roots a project says its modules are under, and where the manifest that
// said so is. A file's own root -- worked out from what it calls itself and
// where it is -- is the first place an import is looked for, and these are the
// rest. See D1049.
typedef struct {
    const char *sources[KEST_MOST_SOURCES];
    uint32_t count;
    const char *where;
} Roots;

// The project above a file, if there is one: `kest.project` in the directory
// the file is in, or in one above it. Every file of a project has to resolve
// an import the same way, so which roots there are is a fact about where the
// program is rather than about who asked for it -- and a host embedding one
// file of a project then resolves what the command line resolves. Bounded,
// because a walk up a path is a walk with a machine at the end of it.
static void roots_above(KestArena *arena, KestUnits *units,
                        const char *path, Roots *roots) {
    roots->count = 0;
    roots->where = NULL;
    if (path == NULL) {
        return;
    }
    const char *at = directory_of(arena, path);
    if (at == NULL) {
        return;
    }
    for (uint32_t up = 0; up < 32; up++) {
        const char *why = NULL;
        KestProject *project = NULL;
        if (units != NULL && units->handed != NULL) {
            // Only the manifests the read was handed, and never one that
            // happens to be on a disk where the program is run.
            char manifest[1024];
            kest_project_path(at, manifest, sizeof(manifest));
            const KestFile *file =
                handed_file(arena, units, tidied(arena, manifest));
            const char *text =
                file == NULL || file->text == NULL
                    ? NULL
                    : kest_arena_strndup(arena, file->text, file->length);
            project = text == NULL ? NULL
                                   : kest_project_from(arena, at, text, &why);
        } else {
            project = kest_project_read(arena, at, &why);
        }
        if (project != NULL) {
            // Kept, as what it said, for whoever carries what a program was
            // read from.
            if (units != NULL) {
                size_t length = 0;
                char *text = read_file(arena, units, project->path, &length);
                if (text != NULL) {
                    units->manifest_path = project->path;
                    units->manifest_text = text;
                }
            }
            for (uint32_t i = 0; i < project->source_count; i++) {
                roots->sources[roots->count++] = project->sources[i];
            }
            roots->where = at[0] == '\0' ? "./" : at;
            return;
        }
        // The directory above this one, which is this one with its last piece
        // taken off. `src/` becomes the empty path, which is where the command
        // was run; the empty path has nothing above it without going somewhere
        // nobody named, so that is the top.
        if (at[0] == '\0') {
            return;
        }
        size_t length = strlen(at);
        while (length > 0 && at[length - 1] == '/') {
            length--;
        }
        while (length > 0 && at[length - 1] != '/') {
            length--;
        }
        const char *up_one = length == 0 ? "" : kest_arena_strndup(arena, at,
                                                                   length);
        if (up_one == NULL || strcmp(up_one, at) == 0) {
            return;
        }
        at = up_one;
    }
}

// A root a manifest names, put under the directory the manifest is in: a
// `source` line is written from where somebody stands to read it, which is
// beside the project and not beside whoever ran the command.
static const char *beneath(KestArena *arena, const char *where,
                           const char *source) {
    if (where == NULL) {
        return source;
    }
    size_t room = strlen(where) + strlen(source) + 3;
    char *joined = kest_arena_alloc(arena, room, 1);
    if (joined == NULL) {
        return source;
    }
    size_t used = (size_t)snprintf(joined, room, "%s%s", where, source);
    if (used > 0 && joined[used - 1] != '/') {
        snprintf(joined + used, room - used, "/");
    }
    return joined;
}

// Whether a file is there to be read, which is what says an import resolved
// under this root and not another.
static bool a_file_is_at(KestArena *arena, const KestUnits *units,
                         const char *path) {
    if (units != NULL && units->handed != NULL) {
        return handed_file(arena, units, tidied(arena, path)) != NULL;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

// The one place a file that could not be read is refused, whether a command
// named it or an import asked for it. What a reader is pointed at is the
// import when there is one and nothing when the path came from a command line,
// which has nowhere in a file to point.
static void refuse_to_read(KestDiags *diags, const char *path, KestSpan blame,
                           const KestSource *blamed_in) {
    KestSpan nowhere = {0, 0};
    kest_diags_in(diags, blamed_in);
    kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0701",
                   blamed_in == NULL ? nowhere : blame, "cannot read `%s`",
                   path);
}

static bool load_one(KestArena *arena, KestDiags *diags, const char *root,
                     const char *library, const Roots *roots,
                     const char *given, KestUnits *units,
                     KestSpan blame, const KestSource *blamed_in, bool follow,
                     bool from_library, const char **root_out) {
    // One spelling per file, whether it was named on a command line or worked
    // out from an import.
    const char *path = tidied(arena, given);
    // A file that imports itself. Its own names are already its own, so the
    // line asks for nothing and reads as though it did: two files importing
    // each other is a program, and one importing itself is a mistake nobody
    // means to write.
    if (blamed_in != NULL && strcmp(blamed_in->path, path) == 0) {
        kest_diags_in(diags, blamed_in);
        kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0704", blame,
                       "this file imports itself");
        kest_diags_suggest(diags,
                           "its own names are its own already, written "
                           "without a module in front of them");
        return true;
    }
    if (already_loaded(units, path)) {
        return true;
    }

    size_t length = 0;
    char *text = read_file(arena, units, path, &length);
    if (text == NULL) {
        // A missing import is reported where it was written, unless this is
        // the file the command named, which has nowhere to point at.
        refuse_to_read(diags, path, blame, blamed_in);
        // Which directory that path came from, for the reader who is looking
        // at the import and not at the loader. A program handed over as a
        // stream is the case this is really for: it is nowhere, so an import
        // of its own resolves under `/dev` and there is nothing there.
        if (blamed_in != NULL) {
            const char *slash = last_separator(path);
            if (from_library) {
                kest_diags_suggest(diags,
                                   "a `std` import resolves from the library, "
                                   "which is `%.*s`",
                                   slash == NULL ? 1 : (int)(slash - path),
                                   slash == NULL ? "." : path);
            } else if (roots != NULL && roots->count > 0) {
                // A project says where its modules are, so what a reader
                // needs is the list that was looked under rather than the one
                // path that happened to be tried first. See D1049.
                char under[512];
                size_t written = 0;
                for (uint32_t r = 0; r < roots->count && written + 1 < sizeof
                                                             under; r++) {
                    int said = snprintf(under + written,
                                        sizeof under - written, "%s`%s`",
                                        r == 0 ? "" : ", ",
                                        roots->sources[r]);
                    if (said < 0) {
                        break;
                    }
                    written += (size_t)said;
                }
                kest_diags_suggest(diags,
                                   "an import resolves from where the file "
                                   "that wrote it is, which is `%.*s`, and "
                                   "then from what this project says its "
                                   "sources are: %s",
                                   slash == NULL ? 1 : (int)(slash - path),
                                   slash == NULL ? "." : path, under);
            } else if (slash == NULL) {
                kest_diags_suggest(diags,
                                   "an import resolves from where the file "
                                   "that wrote it is, which is here");
            } else {
                kest_diags_suggest(diags,
                                   "an import resolves from where the file "
                                   "that wrote it is, which is `%.*s`",
                                   (int)(slash - path), path);
            }
        }
        return blamed_in != NULL;
    }

    KestUnitInfo *info = reserve(arena, units);
    if (info == NULL) {
        // Nowhere to write the file down, which is this build having no room
        // left rather than anything about the file. Said here because what the
        // stage above reads is a count of files and a count of errors, and no
        // files with nothing wrong is a program with nothing in it: a build
        // given a ceiling under what reading it costs came back nought and
        // said nothing at all. See D843.
        kest_diags_starve(diags);
        return false;
    }
    memset(info, 0, sizeof(*info));
    info->alias = "";
    info->module = "";

    const char *owned = kest_arena_strndup(arena, path, strlen(path));
    if (owned == NULL ||
        !kest_source_init(&info->source, arena, owned, text, length)) {
        kest_diags_starve(diags);
        return false;
    }

    kest_diags_in(diags, &info->source);
    // The tree goes where the trees go, and what it costs is charged here
    // rather than when they are given back: a ceiling refuses against what a
    // build asked for, and a build that asked while reading its fourth file
    // has to be refused at the fourth file. See D748.
    KestArena *into = units->trees == NULL ? arena : units->trees;
    size_t was = units->trees == NULL ? 0 : kest_arena_used(units->trees);
    size_t was_held = units->trees == NULL ? 0 : kest_arena_held(units->trees);
    // Nought is what an arena with no ceiling answers, and capping at what is
    // already there is a ceiling of nothing left. Only a build that has one
    // hands it on.
    size_t left = units->trees == NULL ? 0 : kest_arena_ceiling_left(arena);
    if (left != 0) {
        kest_arena_cap(units->trees, was + left);
    }
    bool read = kest_parse(into, &info->source, diags, &info->unit);
    if (units->trees != NULL) {
        // What reading the file asked for, and how much of that it has already
        // given back: the tokens go in an arena of their own inside the trees'
        // one, so the charge covers both and the return covers the tokens.
        // Without the second line a file that has been read holds the tokens
        // it gave back, until the trees go and they are given back twice.
        size_t asked = kest_arena_used(units->trees) - was;
        size_t holds = kest_arena_held(units->trees) - was_held;
        kest_arena_charge(arena, asked);
        kest_arena_returned(arena, asked - holds);
    }
    if (!read) {
        // A tree that could not be made is the host having nothing left, which
        // is the one thing `kest_parse` answers no for. It used to be the
        // build's own arena that ran out and the stage above that said so;
        // the trees have an arena of their own now, and nothing above it is
        // watching one. See D748.
        //
        // And the refusal comes back with it, because the ceiling that stopped
        // this one is written on the arena above and the refusal happened
        // below it. See D843.
        kest_arena_also_refused(arena, units->trees);
        kest_diags_starve(diags);
        return false;
    }

    // The array may move as more files are read, so nothing below holds the
    // pointer across a load.
    uint32_t self = units->count - 1;

    // What the file calls itself comes first, because the root that its
    // imports resolve from is worked out from it.
    const KestDecl *module = NULL;
    for (uint32_t i = 0; i < units->items[self].unit.count; i++) {
        if (units->items[self].unit.items[i]->kind == KEST_DECL_MODULE) {
            module = units->items[self].unit.items[i];
            units->items[self].alias = last_segment(
                arena,
                kest_span_text(&units->items[self].source, module->name),
                module->name.length);
            const char *whole = kest_arena_strndup(
                arena,
                kest_span_text(&units->items[self].source, module->name),
                module->name.length);
            units->items[self].module = whole == NULL ? "" : whole;
            units->items[self].from_library = is_library(
                kest_span_text(&units->items[self].source, module->name),
                module->name.length);
        }
    }
    // A file that names no module puts its names under nothing, which is what
    // a program written to be run once wants and is no use to anybody
    // importing it: its names would land in the importing file's own, and
    // where a name came from is written at every use of it in this language.
    if (blamed_in != NULL && units->items[self].alias[0] == '\0') {
        kest_diags_in(diags, blamed_in);
        kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0702", blame,
                       "`%s` names no module, so its names have nowhere to "
                       "live",
                       path);
        // The name this import asked for is the name it should have: an
        // import is a path, so the two are the same thing written twice.
        kest_diags_suggest(diags,
                           "a file that is imported says what it is called: "
                           "`module %.*s`",
                           (int)blame.length, kest_span_text(blamed_in, blame));
        return true;
    }

    // A file is imported by its path and says under what name its own names
    // live. Two spellings of one file is a file nothing can import: the names
    // land where nobody wrote them, and the only message was `unknown name`
    // at every use of one, in the file that did nothing wrong.
    if (blamed_in != NULL && module != NULL) {
        const char *called =
            kest_span_text(&units->items[self].source, module->name);
        const char *asked = kest_span_text(blamed_in, blame);
        if (module->name.length != blame.length ||
            memcmp(called, asked, blame.length) != 0) {
            kest_diags_in(diags, blamed_in);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0703", blame,
                           "`%s` calls itself `%.*s`", path,
                           (int)module->name.length, called);
            kest_diags_suggest(diags,
                               "an import is a path, so a file read by this "
                               "one says `module %.*s`",
                               (int)blame.length, asked);
            // The line somebody has to change is in the other file, and the
            // reader of this message is looking at their own.
            kest_diags_note(diags, &units->items[self].source, module->name,
                            "this is the name it says");
            return true;
        }
    }

    if (root_out != NULL) {
        *root_out = root_of(arena, path,
                            module == NULL
                                ? NULL
                                : kest_span_text(
                                      &units->items[self].source,
                                      module->name),
                            module == NULL ? 0 : module->name.length);
        root = *root_out;
    }

    for (uint32_t i = 0; i < units->items[self].unit.count; i++) {
        const KestDecl *decl = units->items[self].unit.items[i];
        const char *name =
            kest_span_text(&units->items[self].source, decl->name);

        if (decl->kind != KEST_DECL_IMPORT || !follow) {
            continue;
        }

        bool library_import = is_library(name, decl->name.length);
        const char *from = library_import ? library : root;
        const char *next =
            path_of_import(arena, from, name, decl->name.length);
        if (next == NULL) {
            return false;
        }
        // And where a project says its modules are, which is the whole answer
        // when there is one: every file of a project has to resolve an import
        // the same way, and a file's own directory is one of the sources or
        // the project is written wrongly. A module found under two of them is
        // two modules of one name -- which is the thing a vendored copy is
        // most likely to be -- so it is refused rather than taken from
        // whichever was looked in first. See D1049.
        if (!library_import && roots != NULL && roots->count > 0) {
            const char *found = NULL;
            const char *twice = NULL;
            for (uint32_t r = 0; r < roots->count; r++) {
                const char *under = path_of_import(
                    arena, beneath(arena, roots->where, roots->sources[r]),
                    name, decl->name.length);
                if (under == NULL || !a_file_is_at(arena, units, under)) {
                    continue;
                }
                if (found == NULL) {
                    found = under;
                } else if (twice == NULL) {
                    twice = under;
                }
            }
            if (twice != NULL) {
                kest_diags_in(diags, &units->items[self].source);
                kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0707", decl->name,
                               "`%.*s` is under two of this project's sources",
                               (int)decl->name.length, name);
                kest_diags_suggest(diags,
                                   "`%s` and `%s` are two modules of one "
                                   "name, and which one this is cannot be "
                                   "worked out from the line",
                                   found, twice);
                return true;
            }
            // Nothing under any of them is nothing, and what is refused is
            // the path the file's own root would have given: it is the one a
            // reader recognises, and the suggestion beside it says what was
            // looked under.
            if (found != NULL) {
                next = found;
            }
        }
        if (!load_one(arena, diags, root, library, roots, next, units,
                      decl->name, &units->items[self].source, follow,
                      library_import, NULL)) {
            return false;
        }
    }

    // Collected after the walk, because the source pointer is stable but the
    // unit array is not.
    KestUnitInfo *loaded = &units->items[self];
    uint32_t imports = 0;
    for (uint32_t i = 0; i < loaded->unit.count; i++) {
        if (loaded->unit.items[i]->kind == KEST_DECL_IMPORT) {
            imports++;
        }
    }
    loaded->imports = KEST_ARENA_ARRAY(arena, const char *, imports + 1);
    loaded->import_paths = KEST_ARENA_ARRAY(arena, const char *, imports + 1);
    loaded->import_reached = KEST_ARENA_ARRAY(arena, bool, imports + 1);
    if (loaded->imports == NULL || loaded->import_paths == NULL ||
        loaded->import_reached == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < loaded->unit.count; i++) {
        const KestDecl *decl = loaded->unit.items[i];
        if (decl->kind == KEST_DECL_IMPORT) {
            const char *written =
                kest_span_text(&loaded->source, decl->name);
            const char *whole =
                kest_arena_strndup(arena, written, decl->name.length);
            loaded->import_paths[loaded->import_count] =
                whole == NULL ? "" : whole;
            loaded->imports[loaded->import_count++] =
                last_segment(arena, written, decl->name.length);
        }
    }
    return true;
}

bool kest_load_many(KestArena *arena, KestDiags *diags, const char *library,
                    char **paths, int count, KestUnits *units) {
    // A library named without a separator at its end is the same directory as
    // one named with it, and every import is that and a module's path run
    // together. `KEST_LIB` has always been read that way; a host handing the
    // same words to `kest_build` was looked for in `libstd`. See D1145.
    size_t named = library == NULL ? 0 : strlen(library);
    if (named > 0 && !KEST_PATH_SEPARATOR(library[named - 1])) {
        char *ended = kest_arena_alloc(arena, named + 2, 1);
        if (ended != NULL) {
            memcpy(ended, library, named);
            ended[named] = '/';
            ended[named + 1] = '\0';
            library = ended;
        }
    }
    units->library = library;
    if (count <= 0) {
        return false;
    }
    units->trees = kest_arena_new();
    if (units->trees == NULL) {
        return false;
    }
    const char *root = directory_of(arena, paths[0]);
    Roots roots;
    roots_above(arena, units, paths[0], &roots);
    KestSpan nowhere = {0, 0};
    for (int i = 0; i < count; i++) {
        // The first file settles the root; the rest are read against it.
        if (!load_one(arena, diags, root, library, &roots, paths[i], units,
                      nowhere, NULL, true, false, i == 0 ? &root : NULL)) {
            return false;
        }
    }
    return true;
}

// Whether the standard library is here, asked by looking for a file that is
// always in it. A path that is merely plausible is worse than no path.
static bool library_is_at(const char *directory) {
    char probe[1024];
    int written = snprintf(probe, sizeof(probe), "%sstd/io.kest", directory);
    if (written <= 0 || (size_t)written >= sizeof(probe)) {
        return false;
    }
    FILE *file = fopen(probe, "rb");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

bool kest_library_has(const char *library, const char *name, size_t length) {
    // A module is one file, so a name with a separator in it is not one: the
    // library is flat and `..` is the only way a name could reach out of it.
    if (library == NULL || length == 0 || length > 64) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        bool plain = (name[i] >= 'a' && name[i] <= 'z') ||
                     (name[i] >= 'A' && name[i] <= 'Z') ||
                     (name[i] >= '0' && name[i] <= '9') || name[i] == '_';
        if (!plain) {
            return false;
        }
    }
    // The same path an import of it would resolve to: the library root holds
    // `std`, which holds the modules.
    char probe[1024];
    int written = snprintf(probe, sizeof(probe), "%sstd/%.*s.kest", library,
                           (int)length, name);
    if (written <= 0 || (size_t)written >= sizeof(probe)) {
        return false;
    }
    FILE *file = fopen(probe, "rb");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

// Where the program actually is, when what it was called by holds no
// separator at all. A shell that found `kest` on `PATH` hands over the bare
// name, and probing beside a bare name probes beside whatever directory
// somebody happened to be standing in -- which is how an unpacked archive
// with its `bin` on the path could not find the library sitting next to it,
// while the same binary named with a path could. The walk is the one the
// shell already did, in libc and nothing else. See D1128.
#if defined(_WIN32)
#define KEST_PATH_LIST_SEPARATOR ';'
#else
#define KEST_PATH_LIST_SEPARATOR ':'
#endif
static bool found_on_path(const char *program, char *into, size_t room) {
    // A host that hands over no program at all -- `kest_build` with no
    // library named does, through `build.c` -- is asking about the directory
    // it is standing in and not about any binary. Walking `PATH` for the
    // empty name would open each directory on it, which `fopen` on a
    // directory is happy to do on glibc, and answer with the first one.
    if (program == NULL || program[0] == '\0') {
        return false;
    }
    const char *path = getenv("PATH");
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    for (const char *at = path; *at != '\0';) {
        const char *end = at;
        while (*end != '\0' && *end != KEST_PATH_LIST_SEPARATOR) {
            end++;
        }
        // An empty entry means the directory somebody is standing in, which
        // is what a bare name already probed, so it is skipped rather than
        // read as the root.
        if (end != at) {
            size_t length = (size_t)(end - at);
            int written = snprintf(into, room, "%.*s%s%s", (int)length, at,
                                   KEST_PATH_SEPARATOR(at[length - 1]) ? ""
                                                                       : "/",
                                   program);
            if (written > 0 && (size_t)written < room) {
                FILE *file = fopen(into, "rb");
                if (file != NULL) {
                    fclose(file);
                    return true;
                }
            }
        }
        at = *end == '\0' ? end : end + 1;
    }
    return false;
}

const char *kest_library_path(KestArena *arena, const char *program) {
    // A caller that has no arena yet gets one answer at a time, which is all
    // anybody needs of this.
    static char scratch[1024];

    const char *given = getenv("KEST_LIB");
    if (given != NULL && given[0] != '\0') {
        size_t length = strlen(given);
        snprintf(scratch, sizeof(scratch), "%s%s", given,
                 given[length - 1] == '/' ? "" : "/");
    } else {
        char found[1024];
        if (last_separator(program) == NULL &&
            found_on_path(program, found, sizeof(found))) {
            program = found;
        }
        const char *slash = last_separator(program);
        int length = slash == NULL ? 0 : (int)(slash - program) + 1;

        // Beside the program, which is where it is in a source tree, and then
        // where it is once installed, which is beside the program's own
        // directory rather than inside it.
        snprintf(scratch, sizeof(scratch), "%.*slib/", length, program);
        if (!library_is_at(scratch)) {
            snprintf(scratch, sizeof(scratch), "%.*s../lib/kest/", length,
                     program);
        }
        // And where it was put when the language was installed, which is what
        // a host that is not the command line has to fall back on.
        if (!library_is_at(scratch)) {
            snprintf(scratch, sizeof(scratch), "%s", KEST_LIB_DIR);
        }
    }

    if (arena == NULL) {
        return scratch;
    }
    // The same answer the caller with no arena gets, and the same promise: one
    // at a time. A copy is what an arena is for and not what this needs to be
    // right. See D510.
    const char *kept = kest_arena_strndup(arena, scratch, strlen(scratch));
    return kept == NULL ? scratch : kept;
}

bool kest_read_source(KestArena *arena, KestDiags *diags, const char *path,
                      KestSource *into) {
    const char *tidy = tidied(arena, path);
    size_t length = 0;
    char *text = read_file(arena, NULL, tidy, &length);
    if (text == NULL) {
        KestSpan nowhere = {0, 0};
        refuse_to_read(diags, tidy, nowhere, NULL);
        return false;
    }
    const char *owned = kest_arena_strndup(arena, tidy, strlen(tidy));
    return owned != NULL &&
           kest_source_init(into, arena, owned, text, length);
}

bool kest_read_unit(KestArena *arena, KestDiags *diags, const char *path,
                     KestUnits *units) {
    KestSpan nowhere = {0, 0};
    return load_one(arena, diags, "", "", NULL, path, units, nowhere, NULL,
                    false,
                    false, NULL);
}
