#include "held.h"

#include "diag.h"

#include <stdlib.h>
#include <string.h>

// A door this host has called, found by name once and remembered. `beyond` is
// how many slots it takes past the world, which is what a reload holds it to:
// the world may change shape under a reload, and a door that takes the world
// takes as many more slots as the world now is.
typedef struct {
    char *name;
    int32_t entry;
    uint32_t beyond;
} Door;

// The files the build read, each with the mark of the bytes it read, so that a
// change is a change of bytes rather than of a time a file system kept.
typedef struct {
    char *path;
    uint64_t mark;
} Watched;

struct KestHeld {
    char *path;
    char *library;
    const KestHost *host;
    FILE *errors;
    KestBuild *build;
    KestRuntime *runtime;
    const KestLayout *shape;
    KestValue *world;
    uint32_t slots;
    Door *doors;
    uint32_t door_count;
    uint32_t door_room;
    Watched *watched;
    uint32_t watched_count;
    KestValue *frame;
    uint32_t frame_room;
};

static char *copied(const char *text) {
    if (text == NULL) {
        return NULL;
    }
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

static void forget_watched(KestHeld *held) {
    for (uint32_t i = 0; i < held->watched_count; i++) {
        free(held->watched[i].path);
    }
    free(held->watched);
    held->watched = NULL;
    held->watched_count = 0;
}

static void watch(KestHeld *held) {
    forget_watched(held);
    uint32_t many = 0;
    while (kest_build_read(held->build, many) != NULL) {
        many++;
    }
    held->watched = calloc(many == 0 ? 1 : many, sizeof(Watched));
    if (held->watched == NULL) {
        return;
    }
    for (uint32_t i = 0; i < many; i++) {
        held->watched[i].path = copied(kest_build_read(held->build, i));
        held->watched[i].mark = kest_build_read_mark(held->build, i);
        held->watched_count++;
    }
}

// A frame at least this wide, kept between calls.
static KestValue *frame_of(KestHeld *held, uint32_t wide) {
    if (wide > held->frame_room) {
        KestValue *bigger = realloc(held->frame, wide * sizeof(KestValue));
        if (bigger == NULL) {
            return NULL;
        }
        held->frame = bigger;
        held->frame_room = wide;
    }
    memset(held->frame, 0, held->frame_room * sizeof(KestValue));
    return held->frame;
}

// Every handle of a world said to the machine that made it.
static bool keep_world(KestRuntime *runtime, const KestLayout *shape,
                       const KestValue *world) {
    for (uint16_t i = 0; i < shape->count; i++) {
        if (kest_slot_of(shape->pieces[i].kind) == KEST_S_WORD &&
            !kest_keeps(runtime, world[i])) {
            return false;
        }
    }
    return true;
}

KestHeld *kest_held_new(const char *path, const char *library,
                        const KestHost *host, FILE *errors) {
    KestHeld *held = calloc(1, sizeof(KestHeld));
    if (held == NULL) {
        return NULL;
    }
    held->path = copied(path);
    held->library = copied(library);
    held->host = host;
    held->errors = errors;
    held->build = kest_build(path, library, errors, KEST_FORM_TEXT, 0);
    if (held->build != NULL) {
        held->runtime = kest_start(held->build, host, NULL);
    }
    if (held->path == NULL || held->runtime == NULL) {
        if (held->build != NULL && errors != NULL) {
            kest_build_report(held->build, errors, KEST_FORM_TEXT);
        }
        kest_held_free(held);
        return NULL;
    }
    watch(held);
    return held;
}

bool kest_held_free(KestHeld *held) {
    if (held == NULL) {
        return true;
    }
    bool freed = kest_runtime_free(held->runtime) &&
                 kest_build_free(held->build);
    for (uint32_t i = 0; i < held->door_count; i++) {
        free(held->doors[i].name);
    }
    free(held->doors);
    forget_watched(held);
    free(held->world);
    free(held->frame);
    free(held->path);
    free(held->library);
    free(held);
    return freed;
}

KestRuntime *kest_held_runtime(KestHeld *held) {
    return held == NULL ? NULL : held->runtime;
}

// Where the world goes in a frame, and what the frame has to be to hold a
// call of `entry` with `count` more values in it.
static bool call_with(KestHeld *held, KestRuntime *runtime, int32_t entry,
                      bool with_world, const KestValue *args, uint32_t count,
                      KestValue **answered) {
    uint32_t in_front = with_world ? held->slots : 0;
    uint32_t wide = kest_frame_slots(runtime, entry);
    if (wide < in_front + count) {
        wide = in_front + count;
    }
    KestValue *frame = frame_of(held, wide);
    if (frame == NULL) {
        return false;
    }
    if (in_front > 0) {
        memcpy(frame, held->world, in_front * sizeof(KestValue));
    }
    if (count > 0) {
        memcpy(frame + in_front, args, count * sizeof(KestValue));
    }
    if (!kest_call(runtime, entry, frame, wide)) {
        if (held->errors != NULL) {
            kest_report(runtime, held->errors, KEST_FORM_TEXT);
        }
        return false;
    }
    *answered = frame;
    return true;
}

bool kest_held_begin(KestHeld *held, const char *entry, const KestValue *args,
                     uint32_t count) {
    if (held == NULL || entry == NULL) {
        return false;
    }
    int32_t found = kest_entry(held->runtime, entry);
    const KestLayout *shape =
        found < 0 ? NULL : kest_frame_gives(held->runtime, found);
    KestValue *frame = NULL;
    if (shape == NULL ||
        !call_with(held, held->runtime, found, false, args, count, &frame)) {
        return false;
    }
    KestValue *world = malloc((shape->slots == 0 ? 1 : shape->slots) *
                              sizeof(KestValue));
    if (world == NULL) {
        return false;
    }
    memcpy(world, frame, shape->slots * sizeof(KestValue));
    if (!keep_world(held->runtime, shape, world)) {
        free(world);
        return false;
    }
    free(held->world);
    held->world = world;
    held->slots = shape->slots;
    held->shape = shape;
    return true;
}

// A door by name: remembered after the first time, with how much it takes
// beyond the world.
static Door *door_named(KestHeld *held, const char *name) {
    for (uint32_t i = 0; i < held->door_count; i++) {
        if (strcmp(held->doors[i].name, name) == 0) {
            return &held->doors[i];
        }
    }
    int32_t entry = kest_entry(held->runtime, name);
    if (entry < 0) {
        return NULL;
    }
    if (held->door_count == held->door_room) {
        uint32_t room = held->door_room == 0 ? 8 : held->door_room * 2;
        Door *bigger = realloc(held->doors, room * sizeof(Door));
        if (bigger == NULL) {
            return NULL;
        }
        held->doors = bigger;
        held->door_room = room;
    }
    char *kept = copied(name);
    if (kept == NULL) {
        return NULL;
    }
    uint32_t takes = kest_frame_takes(held->runtime, entry);
    Door *door = &held->doors[held->door_count++];
    door->name = kept;
    door->entry = entry;
    door->beyond = takes > held->slots ? takes - held->slots : 0;
    return door;
}

bool kest_held_call(KestHeld *held, const char *entry, const KestValue *args,
                    uint32_t count, KestValue *answer) {
    if (held == NULL || entry == NULL) {
        return false;
    }
    Door *door = door_named(held, entry);
    KestValue *frame = NULL;
    if (door == NULL ||
        !call_with(held, held->runtime, door->entry, true, args, count,
                   &frame)) {
        return false;
    }
    if (answer != NULL) {
        *answer = frame[0];
    }
    return true;
}

// A run's bytes, where the handle is one. What a handle holds is a run of
// elements, another kind of thing, or nothing, and only the first has bytes to
// read.
static const KestRun *run_of(KestValue value) {
    const KestRun *run = value.object;
    if (run == NULL || run->what != KEST_RUN_IS) {
        return NULL;
    }
    return run;
}

const void *kest_held_run(KestHeld *held, const char *piece, uint32_t *count,
                          uint16_t *stride) {
    if (held == NULL || piece == NULL || held->shape == NULL) {
        return NULL;
    }
    for (uint16_t i = 0; i < held->shape->count; i++) {
        const char *name = held->shape->pieces[i].name;
        if (name == NULL || strcmp(name, piece) != 0 ||
            kest_slot_of(held->shape->pieces[i].kind) != KEST_S_WORD) {
            continue;
        }
        const KestRun *run = run_of(held->world[i]);
        if (run == NULL) {
            return NULL;
        }
        if (count != NULL) {
            *count = run->length;
        }
        if (stride != NULL) {
            *stride = run->stride;
        }
        return run->bytes;
    }
    return NULL;
}

// The mark of a file as it is now, read with the C library and nothing else,
// in the arithmetic the build marked it with.
static bool marked(const char *path, uint64_t *mark) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    uint64_t folded = KEST_MARK_START;
    unsigned char chunk[4096];
    size_t got = 0;
    while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        folded = kest_mark_bytes(folded, chunk, got);
    }
    bool read = !ferror(file);
    fclose(file);
    *mark = folded;
    return read;
}

bool kest_held_changed(KestHeld *held) {
    if (held == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < held->watched_count; i++) {
        uint64_t now = 0;
        // A file that cannot be read now is a change as well: it was read
        // once, and what a reload would read is not that.
        if (!marked(held->watched[i].path, &now) ||
            now != held->watched[i].mark) {
            return true;
        }
    }
    return false;
}

static bool refused(char *said, size_t room, const char *why,
                    const char *name) {
    if (said != NULL && room > 0) {
        snprintf(said, room, why, name == NULL ? "" : name);
    }
    return false;
}

bool kest_held_reload(KestHeld *held, const char *path, char *said,
                      size_t room) {
    if (held == NULL) {
        return refused(said, room, "nothing is held%s", NULL);
    }
    const char *from = path == NULL ? held->path : path;
    // Built into a file of its own first, so that the first thing it said can
    // go into `said` as well as all of it to the errors this was made with: a
    // game draws `said` in a corner, and a refusal that says only that it was
    // refused sends the person back to a terminal to find out what for.
    FILE *heard = kest_scratch_file();
    KestBuild *candidate = kest_build(
        from, held->library, heard != NULL ? heard : held->errors,
        KEST_FORM_TEXT, 0);
    if (heard != NULL) {
        char first[512] = "";
        char where[512] = "";
        char line[512];
        rewind(heard);
        while (fgets(line, sizeof(line), heard) != NULL) {
            if (held->errors != NULL) {
                fputs(line, held->errors);
            }
            if (first[0] == '\0' && strncmp(line, "error", 5) == 0) {
                snprintf(first, sizeof(first), "%s", line);
            } else if (first[0] != '\0' && where[0] == '\0') {
                const char *arrow = strstr(line, "--> ");
                if (arrow != NULL) {
                    snprintf(where, sizeof(where), "%s", arrow + 4);
                }
            }
        }
        fclose(heard);
        first[strcspn(first, "\n")] = '\0';
        where[strcspn(where, "\n")] = '\0';
        if (candidate == NULL && first[0] != '\0') {
            if (said != NULL && room > 0) {
                snprintf(said, room, "refused at building: %s at %s", first,
                         where[0] != '\0' ? where : from);
            }
            return false;
        }
    }
    if (candidate == NULL) {
        return refused(said, room, "refused at building `%s`", from);
    }
    KestRuntime *fresh = kest_start(candidate, held->host, NULL);
    int32_t save = kest_entry(held->runtime, "save");
    int32_t restore = fresh == NULL ? -1 : kest_entry(fresh, "restore");
    const KestLayout *shape =
        restore < 0 ? NULL : kest_frame_gives(fresh, restore);
    if (fresh == NULL || save < 0 || shape == NULL) {
        kest_runtime_free(fresh);
        kest_build_free(candidate);
        return refused(said, room,
                       fresh == NULL ? "refused at starting a machine%s"
                       : save < 0    ? "refused: the running program has no "
                                       "`save`%s"
                                     : "refused: the new program has no "
                                       "`restore` that answers a world%s",
                       NULL);
    }

    // Every door this host has called, held to being there and taking what
    // it took beyond the world. Asked before anything is saved, because this
    // is the refusal that costs nothing to back out of.
    for (uint32_t i = 0; i < held->door_count; i++) {
        int32_t entry = kest_entry(fresh, held->doors[i].name);
        uint32_t takes = entry < 0 ? 0 : kest_frame_takes(fresh, entry);
        uint32_t beyond = takes > shape->slots ? takes - shape->slots : 0;
        if (entry < 0 || beyond != held->doors[i].beyond) {
            kest_runtime_free(fresh);
            kest_build_free(candidate);
            return refused(said, room,
                           entry < 0 ? "refused: the new program has no `%s`"
                                     : "refused: `%s` takes something other "
                                       "than it took",
                           held->doors[i].name);
        }
    }

    // Saved by the running machine, and copied out of it before it goes.
    KestValue *frame = NULL;
    const KestRun *bytes = NULL;
    if (call_with(held, held->runtime, save, true, NULL, 0, &frame)) {
        bytes = run_of(frame[0]);
    }
    uint8_t *copy = NULL;
    uint32_t length = 0;
    if (bytes != NULL && bytes->stride == 1) {
        length = bytes->length;
        copy = malloc(length == 0 ? 1 : length);
        if (copy != NULL && length > 0) {
            memcpy(copy, bytes->bytes, length);
        }
    }
    if (copy == NULL) {
        kest_runtime_free(fresh);
        kest_build_free(candidate);
        return refused(said, room,
                       "refused: `save` did not answer bytes%s", NULL);
    }

    // And made again by the new one out of nothing but those bytes.
    KestValue lent = kest_borrow(fresh, copy, length, "u8", 1);
    bool made = lent.object != NULL &&
                call_with(held, fresh, restore, false, &lent, 1, &frame);
    KestValue *world =
        malloc((shape->slots == 0 ? 1 : shape->slots) * sizeof(KestValue));
    if (made && world != NULL) {
        memcpy(world, frame, shape->slots * sizeof(KestValue));
    }
    if (lent.object != NULL) {
        kest_lend_ends(fresh, lent);
    }
    free(copy);
    if (!made || world == NULL || !keep_world(fresh, shape, world)) {
        free(world);
        kest_runtime_free(fresh);
        kest_build_free(candidate);
        return refused(said, room,
                       "refused: `restore` would not make a world of what "
                       "was saved%s",
                       NULL);
    }

    // Published: the old machine and its build go now and not before.
    kest_runtime_free(held->runtime);
    kest_build_free(held->build);
    held->runtime = fresh;
    held->build = candidate;
    free(held->world);
    held->world = world;
    held->slots = shape->slots;
    held->shape = shape;
    for (uint32_t i = 0; i < held->door_count; i++) {
        held->doors[i].entry = kest_entry(fresh, held->doors[i].name);
    }
    if (path != NULL && strcmp(path, held->path) != 0) {
        char *kept = copied(path);
        if (kept != NULL) {
            free(held->path);
            held->path = kept;
        }
    }
    watch(held);
    if (said != NULL && room > 0) {
        snprintf(said, room, "reloaded %s, %u byte(s) of world carried over",
                 held->path, (unsigned)length);
    }
    return true;
}
