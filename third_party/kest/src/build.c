#include "build.h"

#include "emitc.h"

#include <stdlib.h>
#include <string.h>

// Reading nothing but the files it is handed, when it is handed any: every
// path a read asks for is one of those or is not there. See D1172.
static KestBuild *opened(const char *library, char **paths, int count,
                         const KestFile *handed, uint32_t handed_count,
                         size_t room) {
    // A build begins with nobody refused. What was refused before this one is
    // the last build's afternoon, and a host that compiles twice should not
    // have the first one's memory hold its tongue about the second. See D880.
    kest_arena_forget_refusals();
    KestArena *arena = kest_arena_new();
    if (arena == NULL) {
        return NULL;
    }
    // Before anything is read into it, which is the only place a ceiling can
    // go: what this arena hands out first is the build itself, and a ceiling
    // written after that is one the first allocation was never held to.
    kest_arena_cap(arena, room);
    KestBuild *build = KEST_ARENA_NEW(arena, KestBuild);
    if (build == NULL) {
        kest_arena_free(arena);
        return NULL;
    }

    build->arena = arena;
    build->reported = 0;
    kest_diags_init(&build->diags, arena);
    kest_module_init(&build->module, arena);
    build->units.handed = handed;
    build->units.handed_count = handed_count;
    kest_load_many(arena, &build->diags,
                   library == NULL ? kest_library_path(arena, "") : library,
                   paths, count, &build->units);
    return build;
}

KestBuild *kest_build_open(const char *library, char **paths, int count,
                           size_t room) {
    return opened(library, paths, count, NULL, 0, room);
}

void kest_build_clock(KestBuild *build, uint64_t (*now)(void *), void *context,
                      uint64_t reading) {
    if (build != NULL) {
        build->now = now;
        build->now_context = context;
        build->spent.reading = reading;
    }
}

const KestSpent *kest_build_spent(const KestBuild *build) {
    return &build->spent;
}

void kest_build_index_names(KestBuild *build, bool keep) {
    if (build != NULL) {
        build->index_names = keep;
    }
}

void kest_build_carries_sources(KestBuild *build) {
    if (build != NULL) {
        build->carrying = true;
    }
}

void kest_build_calls_as_written(KestBuild *build) {
    if (build != NULL) {
        build->module.carrying_off = true;
    }
}

bool kest_build_check(KestBuild *build) {
    if (build->diags.error_count > 0 || build->units.count == 0) {
        return false;
    }
    uint64_t at = kest_ir_ticked(build->now, build->now_context);
    if (!kest_check(build->arena, &build->diags, &build->units,
                    build->index_names,
                    &build->program)) {
        // Nothing to read and nothing said, which is a stage that could not
        // write down either the program or what was wrong with it. A caller
        // that is told no and given no reason is a command that stops and
        // prints nothing, and answers as though it had worked.
        if (build->diags.error_count == 0) {
            kest_diags_starve(&build->diags);
        }
        return false;
    }
    build->spent.naming += kest_ir_ticked(build->now, build->now_context) - at;
    at = kest_ir_ticked(build->now, build->now_context);
    kest_check_bodies(build->program, &build->units);
    build->spent.bodies += kest_ir_ticked(build->now, build->now_context) - at;
    if (build->diags.error_count > 0) {
        return false;
    }
    at = kest_ir_ticked(build->now, build->now_context);
    kest_check_contracts(build->program, &build->units);
    build->spent.promises +=
        kest_ir_ticked(build->now, build->now_context) - at;
    return build->diags.error_count == 0;
}

// What the optimizer found in one body, for whoever is writing a pass. It
// goes to the error stream because what a program wrote is the program's
// answer, the same rule the profile's own numbers follow. See D1024.
// The backends a body is handed to, in the order they read it. There is one
// door into a build for what a body means and two things that want it, and a
// second walk of the program would be a second reading of the same answer.
typedef struct {
    KestLower *lower;
    KestEmitC *c;
} Backends;

static bool write_body(void *reading, const KestIrBody *body) {
    Backends *both = reading;
    if (!kest_lower_body(both->lower, body)) {
        return false;
    }
    return both->c == NULL || kest_emitc_body(both->c, body);
}

static void say_what_the_optimizer_found(const KestIrBody *body,
                                         const KestIrFound *found) {
    if (found->slot_copies == 0 && found->reloads == 0 &&
        found->materialized == 0) {
        return;
    }
    fprintf(stderr,
            "ir %s ops %u copies %u/%u took %u reloads %u/%u slots %u "
            "dead %u/%u made %u/%u\n",
            body->symbol == NULL ? "(no name)" : body->symbol, found->ops,
            found->hot_copies, found->slot_copies, found->copies_taken,
            found->hot_reloads,
            found->reloads, found->reloaded_slots, found->hot_dead,
            found->dead_writes, found->hot_materialized, found->materialized);
}

bool kest_build_emit(KestBuild *build) {
    if (build->compiled) {
        return true;
    }
    if (!kest_build_check(build)) {
        return false;
    }
    // A body with the end missing reads as an instruction of the wrong width
    // to the proof below, which would say the two halves of this compiler
    // disagree about what a program is. What happened is that the host had
    // nothing left, and this is where that is noticed. See D750.
    //
    // Two halves of that: the module says whether what it was writing ran out,
    // and the compiler answers whether it did. The answer was thrown away, so
    // a compiler that had no room for a layout went on without one and the
    // module said nothing was wrong — `emit` under a ceiling wrote every
    // instruction of a program and one fewer layout than it has, and came back
    // nought. See D845.
    // The bodies go in an arena of their own, the way the trees do and for the
    // same reason: a backend reads them and nothing after it does, so a build
    // that is finished holds neither. See D748 and D962.
    KestIrProgram ir;
    KestArena *bodies = kest_arena_new();
    KestLower *writes = bodies == NULL
                            ? NULL
                            : kest_lower_new(build->program, &build->module,
                                             bodies);
    if (writes == NULL) {
        kest_arena_free(bodies);
        kest_diags_starve(&build->diags);
        return false;
    }
    // Both backends, when both were asked for. The C goes in the build's own
    // arena rather than the bodies' one: a body is let go as soon as it has
    // been written and what was written from it is read after the last of
    // them. See D1093.
    Backends both = {writes, NULL};
    if (build->wants_c) {
        both.c = kest_emitc_new(build->arena, &build->module);
        if (both.c == NULL) {
            kest_arena_free(bodies);
            kest_diags_starve(&build->diags);
            return false;
        }
    }
    kest_ir_program_init(&ir, bodies, write_body, &both);
    ir.now = build->now;
    ir.now_context = build->now_context;
    // What the optimizer found, said per body, when somebody asks. It is a
    // development question and not a command: what it prints is a count of
    // shapes in the resolved form of a program, which is of no use to anybody
    // who is not writing a pass. Read once so a compiler cannot change its
    // mind half way through. See D1024.
    {
        static int asked = -1;
        if (asked < 0) {
            asked = getenv("KEST_IRSAY") != NULL ? 1 : 0;
        }
        if (asked) {
            ir.say_found = say_what_the_optimizer_found;
        }
    }
    uint64_t at = kest_ir_ticked(build->now, build->now_context);
    bool compiled =
        kest_compile(build->program, &build->units, &build->module, &ir);
    uint64_t compiling = kest_ir_ticked(build->now, build->now_context) - at;
    // What the three stages inside a body took, taken off what compiling took:
    // what is left is the walk of the checked tree that wrote the resolved
    // form. Taken off rather than timed on its own, because the walk and the
    // three are interleaved a body at a time.
    build->spent.verifying += ir.verifying;
    build->spent.optimizing += ir.optimizing;
    build->spent.lowering += ir.lowering;
    build->spent.copies += ir.copies;
    uint64_t inside = ir.verifying + ir.optimizing + ir.lowering;
    build->spent.writing += compiling > inside ? compiling - inside : 0;
    // Written once every body has been through, because what one body calls
    // may be a body that had not arrived yet: a call to something this
    // backend did not write is a call with nowhere to go, and which those are
    // is a question about the whole program.
    if (both.c != NULL && compiled) {
        // Every file the program was read from, and the manifest that said
        // where its imports resolve, when the program is to carry them. See
        // D1172.
        KestFile *carried = NULL;
        uint32_t carried_count = 0;
        if (build->carrying) {
            uint32_t room = build->units.count + 1;
            carried = KEST_ARENA_ARRAY(build->arena, KestFile, room);
            for (uint32_t i = 0; carried != NULL && i < build->units.count;
                 i++) {
                const KestSource *source = &build->units.items[i].source;
                carried[carried_count++] =
                    (KestFile){source->path, source->text, source->length};
            }
            if (carried != NULL && build->units.manifest_path != NULL) {
                carried[carried_count++] = (KestFile){
                    build->units.manifest_path, build->units.manifest_text,
                    strlen(build->units.manifest_text)};
            }
        }
        build->c_wrote = kest_emitc_done(
            both.c, kest_build_name(build, KEST_MAIN),
            build->units.count > 0 ? build->units.items[0].source.path : NULL,
            carried, carried_count, build->units.library);
        if (build->c_wrote == NULL) {
            kest_diags_starve(&build->diags);
        }
    }
    kest_arena_free(bodies);
    if (!compiled || build->module.out_of_room) {
        kest_diags_starve(&build->diags);
    }
    // The promise was checked against the tree; this holds it against what was
    // emitted. If the two disagree the tree walk missed something, and finding
    // that out here beats finding it out in a frame (D058).
    if (build->diags.error_count == 0) {
        at = kest_ir_ticked(build->now, build->now_context);
        kest_module_prove(&build->module, build->arena, &build->diags);
        build->spent.finishing +=
            kest_ir_ticked(build->now, build->now_context) - at;
    }
    build->compiled = build->diags.error_count == 0;
    // And the trees, which nothing reads once every copy has been compiled and
    // the promise has been held against what was emitted. What it cost is
    // already counted, where each file was read. See D748.
    if (build->units.trees != NULL) {
        kest_arena_returned(build->arena, kest_arena_held(build->units.trees));
        kest_arena_free(build->units.trees);
        build->units.trees = NULL;
    }
    return build->compiled;
}

static KestBuild *finished(KestBuild *build, FILE *errors, KestForm form,
                           size_t room);

KestBuild *kest_build(const char *path, const char *library, FILE *errors,
                      KestForm form, size_t room) {
    char *paths[1] = {(char *)path};
    return finished(kest_build_open(library, paths, 1, room), errors, form,
                    room);
}

KestBuild *kest_build_from(const KestFile *files, uint32_t count,
                           const char *library, FILE *errors, KestForm form,
                           size_t room) {
    if (files == NULL || count == 0 || files[0].path == NULL) {
        kest_diags_say_one(errors, form == KEST_FORM_JSON, "K0701",
                           "a build handed no files has no program to read");
        return NULL;
    }
    char *paths[1] = {(char *)files[0].path};
    // A library a build was not told of is looked for on no disk: everything
    // it reads is what it was handed. See D1172.
    return finished(opened(library == NULL ? "" : library, paths, 1, files,
                           count, room),
                    errors, form, room);
}

static KestBuild *finished(KestBuild *build, FILE *errors, KestForm form,
                           size_t room) {
    if (build == NULL) {
        // Nothing was made, so there is nothing to ask what went wrong: a
        // host that got NULL here and called `kest_build_report` would be
        // handing it the nothing it was given. There is one reason to be here
        // and it is said in the form the caller asked for, through the writer
        // every other diagnostic goes through, because a shape written twice
        // is a shape that comes apart.
        //
        // Two reasons now, and which of them is known here without asking
        // anything: a build given a ceiling and refused before it had a list
        // to write in was refused by the ceiling, and the arena that would
        // have said so is already gone. See D844.
        if (room > 0) {
            char said[120];
            snprintf(said, sizeof said, KEST_CRAMPED_START, room);
            kest_diags_say_one(errors, form == KEST_FORM_JSON,
                               KEST_CRAMPED_CODE, said);
            return NULL;
        }
        kest_diags_say_one(errors, form == KEST_FORM_JSON, "K0705",
                           "there is not enough memory to read a program");
        return NULL;
    }
    if (!kest_build_emit(build)) {
        if (errors != NULL) {
            kest_diags_sort(&build->diags);
            if (form == KEST_FORM_JSON) {
                kest_diags_render_json(&build->diags, errors);
            } else {
                kest_diags_render(&build->diags, errors);
            }
        }
        kest_build_free(build);
        return NULL;
    }
    return build;
}

size_t kest_build_cost(const KestBuild *build) {
    // Nought for no build, which is the same answer as a build that has read
    // nothing: a host that was handed NULL asked about a thing that is not
    // there, and there is nothing for it to have cost.
    return build == NULL ? 0 : kest_arena_used(build->arena);
}

size_t kest_build_held(const KestBuild *build) {
    return build == NULL ? 0 : kest_arena_held(build->arena);
}

const char *kest_build_read(const KestBuild *build, uint32_t at) {
    // Past the last one is NULL rather than a refusal, because walking to the
    // end is how a host learns how many there are: a walk that has to ask the
    // count first is two questions for one answer, which is what
    // `kest_build_extern` above it settled.
    if (build == NULL || at >= build->units.count) {
        return NULL;
    }
    return build->units.items[at].source.path;
}

size_t kest_build_read_bytes(const KestBuild *build, uint32_t at) {
    if (build == NULL || at >= build->units.count) {
        return 0;
    }
    return build->units.items[at].source.length;
}

uint64_t kest_build_read_mark(const KestBuild *build, uint32_t at) {
    if (build == NULL || at >= build->units.count) {
        return 0;
    }
    return build->units.items[at].source.mark;
}

uint64_t kest_build_mark(const KestBuild *build) {
    if (build == NULL || build->units.count == 0) {
        return 0;
    }
    // The files' own marks, folded in the order they were read. Folded here
    // rather than left to a host: two hosts that combined them their own way
    // would have two numbers for one program, and the point of the number is
    // that it is the same everywhere. See D658.
    uint64_t mark = KEST_MARK_START;
    for (uint32_t at = 0; at < build->units.count; at++) {
        mark = kest_mark_number(mark, build->units.items[at].source.mark, 8);
    }
    return mark;
}

uint64_t kest_build_code_mark(const KestBuild *build) {
    if (build == NULL || !build->compiled) {
        return 0;
    }
    return kest_module_mark(&build->module);
}

size_t kest_build_source(const KestBuild *build) {
    if (build == NULL) {
        return 0;
    }
    size_t bytes = 0;
    for (uint32_t at = 0; at < build->units.count; at++) {
        bytes += build->units.items[at].source.length;
    }
    return bytes;
}

void kest_build_report(KestBuild *build, FILE *out, KestForm form) {
    if (build == NULL || out == NULL) {
        return;
    }
    // A run that ran out has one thing to say and nowhere it was written down,
    // so it is said here rather than found in the list. It is said once, like
    // everything else in the list is.
    bool starving = build->diags.starved && !build->starve_said;
    if (build->reported >= build->diags.count && !starving) {
        return;
    }
    // The tail rather than anything taken out, the way a machine reports what
    // it has said: what came before was written when it was said.
    KestDiags tail = build->diags;
    tail.items = build->diags.items + build->reported;
    tail.count = build->diags.count - build->reported;
    tail.error_count = 0;
    for (uint32_t i = 0; i < tail.count; i++) {
        if (tail.items[i].severity == KEST_SEVERITY_ERROR) {
            tail.error_count++;
        }
    }
    tail.starved = starving;
    tail.error_count += starving ? 1 : 0;
    if (form == KEST_FORM_JSON) {
        kest_diags_render_json(&tail, out);
    } else {
        kest_diags_render(&tail, out);
    }
    build->reported = build->diags.count;
    build->starve_said = build->starve_said || starving;
}

// The same rule the frame questions follow, one crossing over. What a host is
// asked for is walked with `kest_build_extern` until it answers nothing, and
// every other question about the one at a place answers a host past the end
// the way it answers a host about a real one that takes nothing or gives
// nothing back. So the end of the walk is silent and asking past it is not.
// See D436.
static bool no_extern_at(const KestBuild *build, uint32_t at) {
    if (build == NULL) {
        return true;
    }
    if (at < build->module.extern_count) {
        return false;
    }
    KestSpan nowhere = {0, 0};
    KestBuild *said = (KestBuild *)build;
    kest_diags_in(&said->diags, NULL);
    kest_diags_add(&said->diags, KEST_SEVERITY_ERROR, "K0648", nowhere,
                   "this program asks the host for %u function%s and there is "
                   "nothing at %u",
                   build->module.extern_count,
                   build->module.extern_count == 1 ? "" : "s", at);
    kest_diags_suggest(&said->diags,
                       "`kest_build_extern` gives the name of the one at a "
                       "place and nothing past the last, which is where a walk "
                       "of them ends");
    return true;
}

const char *kest_build_extern(const KestBuild *build, uint32_t at) {
    if (build == NULL || at >= build->module.extern_count) {
        return NULL;
    }
    return build->module.externs[at].name;
}

// How much of a name is the receiver: everything before the last dot, or
// nothing for a name with none. `Io.write` is `Io`; a bare `write` is the
// empty capability.
static size_t receiver_of(const char *name, const char **from) {
    const char *dot = strrchr(name, '.');
    *from = name;
    return dot == NULL ? 0 : (size_t)(dot - name);
}

const char *kest_build_capability(const KestBuild *build, uint32_t at) {
    if (build == NULL) {
        return NULL;
    }
    // Walked rather than kept: a program asks the host for a few names and a
    // host asks this a few times, so a list built and held would be a list to
    // keep in step with the module for no reader in a hurry.
    uint32_t seen = 0;
    for (uint32_t i = 0; i < build->module.extern_count; i++) {
        const char *from = NULL;
        size_t length = receiver_of(build->module.externs[i].name, &from);
        bool first = true;
        for (uint32_t before = 0; before < i && first; before++) {
            const char *earlier = NULL;
            size_t was = receiver_of(build->module.externs[before].name,
                                     &earlier);
            first = was != length || memcmp(earlier, from, length) != 0;
        }
        if (!first) {
            continue;
        }
        if (seen == at) {
            // Kept on the build's own arena, which lives as long as the build
            // and therefore as long as a host may hold what it was handed.
            const char *held = kest_arena_strndup(build->arena, from, length);
            return held == NULL ? "" : held;
        }
        seen++;
    }
    return NULL;
}

uint32_t kest_extern_takes(const KestBuild *build, uint32_t at) {
    if (no_extern_at(build, at)) {
        return 0;
    }
    return build->module.externs[at].takes_count;
}

const KestLayout *kest_extern_layout(const KestBuild *build, uint32_t at,
                                     uint32_t which) {
    if (no_extern_at(build, at)) {
        return NULL;
    }
    const KestExtern *one = &build->module.externs[at];
    // Past the last argument is the walk ending, which is not the same news.
    if (which >= one->takes_count) {
        return NULL;
    }
    return &build->module.layouts[one->takes[which]];
}

const KestLayout *kest_extern_gives(const KestBuild *build, uint32_t at) {
    if (no_extern_at(build, at)) {
        return NULL;
    }
    const KestExtern *one = &build->module.externs[at];
    return one->gives_value ? &build->module.layouts[one->gives] : NULL;
}

uint32_t kest_build_layout(const KestBuild *build, const char *name,
                           const KestLayout **layout) {
    if (build == NULL) {
        if (layout != NULL) {
            *layout = NULL;
        }
        return 0;
    }
    const KestLayout *only = NULL;
    uint32_t named = kest_module_layout_of(&build->module, name, &only, 1);
    if (layout != NULL) {
        // One is what a lend can be held to. Two of them is a name and not a
        // type, and none is nothing to hand over.
        *layout = named == 1 ? only : NULL;
    }
    return named;
}

bool kest_build_free(KestBuild *build) {
    if (build == NULL) {
        // Nothing to free is not a refusal, the same as freeing no machine.
        return true;
    }
    // Acquired against the release a machine counts itself off with: a build
    // freed on one thread has to see what a machine on another did before it
    // went. See D952.
    unsigned standing = atomic_load_explicit(&build->module.machines,
                                             memory_order_acquire);
    if (standing > 0) {
        // The program is in here and the machines are standing on it: what
        // they run, what their layouts say, and every piece of text a
        // diagnostic points at are all on this arena. Freeing it under them is
        // not something they survive, so it is refused where it is asked for.
        KestSpan nowhere = {0, 0};
        kest_diags_in(&build->diags, NULL);
        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0640", nowhere,
                       "this build cannot be freed while %u machine%s standing "
                       "on it",
                       standing, standing == 1 ? " is" : "s are");
        kest_diags_suggest(&build->diags,
                           "free every machine this build made, and then the "
                           "build");
        return false;
    }
    // A build that was never compiled still has its trees, because what gives
    // them back is the last stage that reads them. See D748.
    if (build->units.trees != NULL) {
        kest_arena_free(build->units.trees);
        build->units.trees = NULL;
    }
    kest_arena_free(build->arena);
    return true;
}

// The name something lives under in the file that was named, which is what a
// host has to ask for and does not otherwise know.
void kest_build_writes_c(KestBuild *build, bool on) {
    build->wants_c = on;
}

const char *kest_build_c(const KestBuild *build) {
    return build->c_wrote;
}

const char *kest_build_name(KestBuild *build, const char *name) {
    // The module's own, which is the field `kest_entry` reads when it looks
    // for the qualified form of what a host asked for. One field, so the two
    // directions of one rule cannot come apart.
    const char *alias = build->module.alias;
    if (alias == NULL || alias[0] == '\0') {
        return name;
    }
    // Already under a module, which is how `check` prints it and therefore how
    // somebody types it: `math.factorial` is not `math.math.factorial`. Any
    // dot at all, and not this module's own name, because a program is the
    // file that was named and everything it imports — `shapes.doubled` in a
    // file that imports `shapes` is a function of this program, and putting
    // the root module in front of it made a name nobody could have typed and
    // then said that name back. See D341.
    //
    // And written the way the file writes it, which is a module's last part:
    // somebody typing `math.min` against a program that imports `std.math`
    // means `std.math.min`, because that is the line they read. Expanded
    // through the root file's own imports, which is the same question the
    // checker asks of every name in that file. See D1039.
    const char *dot = strchr(name, '.');
    if (dot != NULL) {
        size_t head = (size_t)(dot - name);
        const KestUnitInfo *root =
            build->units.count > 0 ? &build->units.items[0] : NULL;
        // The root file's own last part first: `math.gcd` against a file that
        // calls itself `examples.math` is `examples.math.gcd`.
        if (root != NULL && kest_word_same(root->alias, name, head)) {
            size_t room = strlen(alias) + strlen(dot) + 1;
            char *own = kest_arena_alloc(build->arena, room, 1);
            if (own == NULL) {
                return name;
            }
            snprintf(own, room, "%s%s", alias, dot);
            return own;
        }
        for (uint32_t i = 0; root != NULL && i < root->import_count; i++) {
            const char *whole = root->import_paths[i];
            if (!kest_word_same(root->imports[i], name, head) ||
                whole == NULL || whole[0] == '\0') {
                continue;
            }
            size_t room = strlen(whole) + strlen(dot) + 1;
            char *under = kest_arena_alloc(build->arena, room, 1);
            if (under == NULL) {
                return name;
            }
            snprintf(under, room, "%s%s", whole, dot);
            return under;
        }
        return name;
    }
    size_t room = strlen(alias) + strlen(name) + 2;
    char *qualified = kest_arena_alloc(build->arena, room, 1);
    if (qualified == NULL) {
        return name;
    }
    snprintf(qualified, room, "%s.%s", alias, name);
    return qualified;
}

// The whole program's walk, worked out the first time anybody asks for it and
// answered out of the build after. What it costs is scratch — six arrays a
// function wide — and for `examples/embed.kest` that is 1596 bytes against the
// 600 a machine is made of, so a host that makes a machine a frame was paying
// for the same walk every frame. The module it walks does not change after it
// is compiled, so neither does the answer. See D607.
static const KestWalk *walk_it(KestBuild *build) {
    if (!build->walked.taken) {
        build->walked.taken = true;
        build->walked.measured = kest_module_needs(
            &build->module, build->arena, -1, &build->walked.slots,
            &build->walked.frames, &build->walked.host_slots,
            &build->walked.host_frames, NULL, &build->walked.why);
        // And what a program with no least is bounded by, worked out only
        // when there is no least to have: the widest body, which one walk
        // answers along with which functions go round and how wide the ones
        // that do not are altogether. See D815 and D816.
        if (!build->walked.measured) {
            kest_module_cycles(&build->module, build->arena, -1, false,
                               &build->walked.widest, &build->walked.in_a_turn,
                               &build->walked.off_the_turns);
        }
    }
    return &build->walked;
}

bool kest_needs(KestBuild *build, KestLimits *least, KestReason *why) {
    KestReason ignored;
    if (why == NULL) {
        why = &ignored;
    }
    why->reach = KEST_REACH_UNASKED;
    why->where = NULL;
    // A build that did not compile is not one a host can be holding —
    // `kest_build` frees it and answers NULL — so this is the same nothing as
    // a NULL: what could be said about it is a program's worth of diagnostics
    // and none of them is a reason there is no least. See D566.
    if (build == NULL || least == NULL || !build->compiled) {
        return false;
    }
    const KestWalk *walked = walk_it(build);
    if (!walked->measured) {
        *why = walked->why;
        return false;
    }
    // The name the walk found is where the program calls into the host, which
    // is what `kest_needs_from` was asked and this was not.
    why->reach = KEST_REACH_KNOWN;
    least->stack_slots = walked->slots;
    least->call_depth = walked->frames;
    // And nought for the heap, which is not a number a program has: what one
    // allocates is what it is given to work on, and a loop over four events
    // and a loop over four thousand are the same program. Written rather than
    // left alone, because a field an answer does not touch is one a caller
    // cannot tell from one it did — and these three doors answer into the same
    // shape, so all three say the same nothing about the one thing none of
    // them knows. A host's own cap goes on after asking. See D724.
    least->heap_bytes = 0;
    return true;
}

bool kest_needs_of(KestBuild *build, const char *name, KestLimits *least,
                   KestReason *why) {
    KestReason ignored;
    if (why == NULL) {
        why = &ignored;
    }
    why->reach = KEST_REACH_UNASKED;
    why->where = NULL;
    if (build == NULL || name == NULL || least == NULL || !build->compiled) {
        return false;
    }
    // The name a host writes, which is the one the file wrote: the same lookup
    // `kest_entry` does, so a host cannot ask about a function it cannot call.
    int32_t found = kest_module_entry(&build->module, name);
    if (found < 0) {
        why->reach = KEST_REACH_NO_NAME;
        return false;
    }
    if (!kest_module_needs(&build->module, build->arena, found,
                           &least->stack_slots, &least->call_depth, NULL, NULL,
                           NULL, why)) {
        return false;
    }
    least->heap_bytes = 0;
    return true;
}

// What the three bound doors have in common: two readings of a chain of frames,
// the smaller of them, and never more than the usual number. See D816.
static uint32_t a_chain_of(uint32_t widest, uint32_t in_a_turn,
                           uint32_t off_the_turns, uint32_t allowed) {
    uint64_t a_frame_each = (uint64_t)widest * allowed;
    uint64_t a_turn_each =
        (uint64_t)off_the_turns + (uint64_t)in_a_turn * allowed;
    if (in_a_turn > 0 && a_turn_each < a_frame_each) {
        a_frame_each = a_turn_each;
    }
    return a_frame_each < (uint64_t)KEST_STACK_SLOTS ? (uint32_t)a_frame_each
                                                     : KEST_STACK_SLOTS;
}

bool kest_bound(KestBuild *build, uint32_t frames, KestLimits *most,
                KestReason *why) {
    KestReason ignored;
    if (why == NULL) {
        why = &ignored;
    }
    why->reach = KEST_REACH_UNASKED;
    why->where = NULL;
    if (build == NULL || most == NULL || !build->compiled) {
        return false;
    }
    if (kest_needs(build, most, why)) {
        why->reach = KEST_REACH_KNOWN;
        return true;
    }
    if (why->reach == KEST_REACH_NO_ROOM) {
        return false;
    }
    const KestWalk *walked = walk_it(build);
    if (walked->widest == 0) {
        why->reach = KEST_REACH_NO_ROOM;
        return false;
    }
    most->heap_bytes = 0;
    most->call_depth = frames == 0 ? KEST_CALL_DEPTH : (uint32_t)frames;
    most->stack_slots = a_chain_of(walked->widest, walked->in_a_turn,
                                   walked->off_the_turns, most->call_depth);
    return true;
}

bool kest_bound_of(KestBuild *build, const char *name, uint32_t frames,
                   KestLimits *most, KestReason *why) {
    KestReason ignored;
    if (why == NULL) {
        why = &ignored;
    }
    why->reach = KEST_REACH_UNASKED;
    why->where = NULL;
    if (build == NULL || name == NULL || most == NULL || !build->compiled) {
        return false;
    }
    // The same lookup the two above do, which is the one `kest_entry` does:
    // a host cannot be bounded for a function it cannot call.
    int32_t about = kest_module_entry(&build->module, name);
    if (about < 0) {
        why->reach = KEST_REACH_NO_NAME;
        return false;
    }
    most->heap_bytes = 0;
    // A least is better than a bound, so a name that has one is answered with
    // it and the frames are not looked at.
    if (kest_module_needs(&build->module, build->arena, about,
                          &most->stack_slots, &most->call_depth, NULL, NULL,
                          NULL, why)) {
        why->reach = KEST_REACH_KNOWN;
        return true;
    }
    // And a name that has none is bounded by the two readings of a chain of
    // frames, over what this one reaches rather than over the whole program.
    // The reason the walk gave is kept: a host is told a bound and why there
    // was nothing better. See D817.
    if (why->reach == KEST_REACH_NO_ROOM) {
        return false;
    }
    uint32_t widest = 0;
    uint32_t in_a_turn = 0;
    uint32_t off_the_turns = 0;
    kest_module_cycles(&build->module, build->arena, about, false, &widest,
                       &in_a_turn, &off_the_turns);
    if (widest == 0) {
        why->reach = KEST_REACH_NO_ROOM;
        return false;
    }
    uint32_t allowed = frames == 0 ? KEST_CALL_DEPTH : frames;
    most->stack_slots = a_chain_of(widest, in_a_turn, off_the_turns, allowed);
    most->call_depth = allowed;
    return true;
}

bool kest_needs_from(KestBuild *build, const char *name, KestLimits *inside,
                     KestReason *why) {
    KestReason ignored;
    if (why == NULL) {
        why = &ignored;
    }
    why->reach = KEST_REACH_UNASKED;
    why->where = NULL;
    if (build == NULL || inside == NULL || !build->compiled) {
        return false;
    }
    int32_t found = -1;
    if (name != NULL) {
        found = kest_module_entry(&build->module, name);
        if (found < 0) {
            why->reach = KEST_REACH_NO_NAME;
            return false;
        }
    }
    // The two a host is being told about are where a call into the host
    // happens; what that call itself costs the machine is nothing, because a
    // host function runs on the host's own stack.
    if (found < 0) {
        const KestWalk *walked = walk_it(build);
        *why = walked->why;
        if (!walked->measured) {
            return false;
        }
        inside->stack_slots = walked->host_slots;
        inside->call_depth = walked->host_frames;
        inside->heap_bytes = 0;
        return true;
    }
    uint32_t reached = 0;
    uint32_t deep = 0;
    if (!kest_module_needs(&build->module, build->arena, found, &reached, &deep,
                           &inside->stack_slots, &inside->call_depth, NULL,
                           why)) {
        return false;
    }
    inside->heap_bytes = 0;
    return true;
}

bool kest_bound_from(KestBuild *build, const char *name, uint32_t frames,
                     KestLimits *inside, KestReason *why) {
    KestReason ignored;
    if (why == NULL) {
        why = &ignored;
    }
    why->reach = KEST_REACH_UNASKED;
    why->where = NULL;
    if (build == NULL || inside == NULL || !build->compiled) {
        return false;
    }
    int32_t from = -1;
    if (name != NULL) {
        from = kest_module_entry(&build->module, name);
        if (from < 0) {
            why->reach = KEST_REACH_NO_NAME;
            return false;
        }
    }
    // The answer where there is one, which is better than any bound.
    if (kest_needs_from(build, name, inside, why)) {
        why->reach = KEST_REACH_KNOWN;
        return true;
    }
    if (why->reach == KEST_REACH_NO_ROOM) {
        return false;
    }
    uint32_t widest = 0;
    uint32_t in_a_turn = 0;
    uint32_t off_the_turns = 0;
    kest_module_cycles(&build->module, build->arena, from, true, &widest,
                       &in_a_turn, &off_the_turns);
    inside->heap_bytes = 0;
    // Nothing reaches a host function, so there is nowhere to call back in
    // from and nothing to make room for. The same nothing `kest_needs_from`
    // gives, and true for the same reason. See D818.
    if (widest == 0) {
        inside->stack_slots = 0;
        inside->call_depth = 0;
        return true;
    }
    uint32_t allowed = frames == 0 ? KEST_CALL_DEPTH : frames;
    inside->stack_slots = a_chain_of(widest, in_a_turn, off_the_turns, allowed);
    inside->call_depth = allowed;
    return true;
}

KestRuntime *kest_start(KestBuild *build, const KestHost *host,
                        const KestLimits *limits) {
    // A build that is not there is the more likely of the two: `kest_build`
    // answers NULL for a program that did not compile, which is the first
    // thing a host meets, and the next line a host writes is this one. No
    // machine comes of no build, and there is nowhere to say more -- a report
    // belongs to a build, and there is none. See D895.
    if (build == NULL || !build->compiled) {
        return NULL;
    }
    // A machine says what it said, out of its own memory. It used to come out
    // of the build's arena, which is a bump pointer: two machines started on
    // two threads read and wrote it at once, and the thread sanitiser says so
    // the moment anything asks. The reference says a host may start a machine
    // from any thread, so the report is the machine's from here. See D1071.
    KestArena *own = kest_arena_new();
    if (own == NULL) {
        kest_diags_starve(&build->diags);
        return NULL;
    }
    KestDiags *said = KEST_ARENA_NEW(own, KestDiags);
    if (said == NULL) {
        // Nowhere to put what this machine would have said, which is the one
        // refusal that cannot be written down. The build is told the one thing
        // that can be recorded without room to record it.
        kest_arena_free(own);
        kest_diags_starve(&build->diags);
        return NULL;
    }
    kest_diags_init(said, own);
    KestRuntime *runtime = kest_runtime_new(own, &build->module, host, said,
                                            limits, walk_it(build));
    if (runtime == NULL) {
        // A machine that never started has nothing to be asked, so what it
        // said on the way out is given to the build: that is what a host has
        // when there is no machine, and a refusal nobody can read is a refusal
        // that did not happen. `absorb` copies the words into the build's own
        // arena, so the machine's goes back here.
        //
        // This is the one write to a build a start makes, and it is the one a
        // host may not make from two threads at once. A start that works
        // touches nothing of the build's but the count of how many machines
        // are standing on it, which is an atomic. See D1071.
        kest_diags_absorb(&build->diags, said);
        kest_arena_free(own);
    }
    return runtime;
}
