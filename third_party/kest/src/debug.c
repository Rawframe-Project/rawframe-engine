#include "debug.h"

#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "value.h"

typedef KestWritten Written;
typedef KestDebugger Debugger;

// The program as it was written, for as long as it takes to ask it something.
// Everything that reads the code walks it an instruction at a time, and a walk
// that met a breakpoint would read a one-byte instruction where a three-byte
// one is and step into the middle of the next. See D991.
static void as_it_was(Debugger *held);
static void as_it_is(Debugger *held);

// Which file a body came from, and where in it. Every answer this gives about
// a place goes through here.
static const KestSource *source_of(Debugger *held, int32_t entry) {
    if (entry < 0 || held->build->units.count == 0) {
        return NULL;
    }
    // The chunk knows which file it was compiled from, which is what a
    // failure while running is already reported against.
    if ((uint32_t)entry >= held->build->module.count) {
        return NULL;
    }
    const KestSource *said = held->build->module.functions[entry]->source;
    return said != NULL ? said : &held->build->units.items[0].source;
}

static uint32_t line_of(Debugger *held, int32_t entry, uint32_t offset) {
    const KestSource *source = source_of(held, entry);
    if (source == NULL) {
        return 0;
    }
    uint32_t line = 0;
    uint32_t column = 0;
    kest_source_locate(source, offset, &line, &column);
    return line;
}

// Where every instruction in a body starts, and which line it came from. The
// walk is the one `kest_came_from` does, asked for every offset rather than
// for one.
static void say_where(Debugger *held, int32_t entry, uint32_t at,
                      const char *before) {
    if (held->out == NULL) {
        return;
    }
    const KestSource *source = source_of(held, entry);
    as_it_was(held);
    int64_t from = kest_came_from(held->runtime, entry, at);
    as_it_is(held);
    uint32_t line = 0;
    uint32_t column = 0;
    if (from >= 0 && source != NULL) {
        kest_source_locate(source, (uint32_t)from, &line, &column);
    }
    fprintf(held->out, "%s%s:%u:%u in %s\n", before,
            source == NULL ? "?" : source->path, line, column,
            (entry >= 0 && (uint32_t)entry < held->build->module.count
                 ? held->build->module.functions[entry]->name
                 : "?"));
}

static bool already_written(Debugger *held, int32_t entry, uint32_t at) {
    for (uint32_t i = 0; i < held->count; i++) {
        if (held->written[i].entry == entry && held->written[i].at == at) {
            return true;
        }
    }
    return false;
}

// The instruction that is really at this byte: what the code says, unless a
// breakpoint is written over it, in which case what was there.
static uint8_t really_at(Debugger *held, int32_t entry, uint32_t at,
                         const uint8_t *code) {
    for (uint32_t i = 0; i < held->count; i++) {
        if (held->written[i].entry == entry && held->written[i].at == at) {
            return held->written[i].was;
        }
    }
    return code[at];
}

static void as_it_was(Debugger *held) {
    for (uint32_t i = 0; i < held->count; i++) {
        uint32_t many = 0;
        uint8_t *code = kest_code_of(held->runtime, held->written[i].entry,
                                     &many);
        if (code != NULL && held->written[i].at < many) {
            code[held->written[i].at] = held->written[i].was;
        }
    }
}

static void as_it_is(Debugger *held) {
    for (uint32_t i = 0; i < held->count; i++) {
        uint32_t many = 0;
        uint8_t *code = kest_code_of(held->runtime, held->written[i].entry,
                                     &many);
        if (code != NULL && held->written[i].at < many) {
            code[held->written[i].at] = (uint8_t)KEST_OP_STOP;
        }
    }
}

static bool write_one(Debugger *held, int32_t entry, uint32_t at,
                      bool asked_for) {
    if (held->count == KEST_MOST_BREAKPOINTS || already_written(held, entry, at)) {
        return false;
    }
    uint32_t many = 0;
    uint8_t *code = kest_code_of(held->runtime, entry, &many);
    if (code == NULL || at >= many) {
        return false;
    }
    Written *one = &held->written[held->count++];
    one->entry = entry;
    one->at = at;
    one->was = code[at];
    one->asked_for = asked_for;
    code[at] = (uint8_t)KEST_OP_STOP;
    return true;
}

static void take_one_out(Debugger *held, uint32_t which) {
    Written *one = &held->written[which];
    uint32_t many = 0;
    uint8_t *code = kest_code_of(held->runtime, one->entry, &many);
    if (code != NULL && one->at < many) {
        code[one->at] = one->was;
    }
    held->written[which] = held->written[--held->count];
}

// Every breakpoint this put in on somebody's behalf, taken out again. The ones
// a person asked for stay.
static void take_the_steps_out(Debugger *held) {
    for (uint32_t i = held->count; i > 0; i--) {
        if (!held->written[i - 1].asked_for) {
            take_one_out(held, i - 1);
        }
    }
}

// The first instruction of every line of a body, which is what a step stops
// at. A line is where an instruction's origin is on a different line from the
// one before it.
static void write_every_line(Debugger *held, int32_t entry) {
    uint32_t many = 0;
    uint8_t *code = kest_code_of(held->runtime, entry, &many);
    if (code == NULL) {
        return;
    }
    uint32_t last = 0;
    bool first = true;
    as_it_was(held);
    for (uint32_t at = 0; at < many;) {
        int64_t from = kest_came_from(held->runtime, entry, at);
        uint32_t line = from < 0 ? 0 : line_of(held, entry, (uint32_t)from);
        if (first || line != last) {
            write_one(held, entry, at, false);
            last = line;
            first = false;
        }
        uint32_t wide = kest_op_wide(really_at(held, entry, at, code));
        at += wide == 0 ? 1 : wide;
    }
    as_it_is(held);
}

// What the machine did when it was let go, said to a person where there is
// one. A stop is not a refusal, so the two are told apart rather than both
// reported.
static KestDebugState carried_on(Debugger *held, bool went) {
    if (kest_stopped(held->runtime) >= 0) {
        int32_t in = kest_stopped_in(held->runtime);
        int64_t at = kest_stopped(held->runtime);
        say_where(held, in, (uint32_t)at, "stopped at ");
        return KEST_DEBUG_STOPPED;
    }
    held->running = false;
    if (went) {
        if (held->out != NULL) {
            fprintf(held->out, "the program finished\n");
        }
        return KEST_DEBUG_FINISHED;
    }
    if (held->out != NULL) {
        kest_report(held->runtime, held->out, KEST_FORM_TEXT);
        fprintf(held->out, "the program stopped and did not finish\n");
    }
    return KEST_DEBUG_FAILED;
}

// Let it go, having taken out the breakpoint it is standing on and put it back
// once it has moved. Standing on one and resuming would stop on it again for
// ever, which is why a debugger steps one line first and then carries on.
// Whatever is written over the instruction the machine is standing on, taken
// out: a breakpoint *is* the instruction that was there, so resuming while
// standing on one stops on it again and nothing moves. Answers whether one a
// person asked for came out, so it can go back.
static bool clear_where_it_stands(Debugger *held, Written *keeping) {
    int64_t at = kest_stopped(held->runtime);
    int32_t in = kest_stopped_in(held->runtime);
    if (at < 0 || in < 0) {
        return false;
    }
    for (uint32_t i = 0; i < held->count; i++) {
        if (held->written[i].entry != in ||
            held->written[i].at != (uint32_t)at) {
            continue;
        }
        bool asked_for = held->written[i].asked_for;
        *keeping = held->written[i];
        take_one_out(held, i);
        return asked_for;
    }
    return false;
}

// The instruction after the one the machine is standing on, patched. Without
// it the machine would run past a line it is already on -- a loop jumps back
// to the line it came from, and the patch that would have caught it is the one
// that had to come out for the machine to move at all.
static void write_the_next_one(Debugger *held) {
    int64_t at = kest_stopped(held->runtime);
    int32_t in = kest_stopped_in(held->runtime);
    if (at < 0 || in < 0) {
        return;
    }
    uint32_t many = 0;
    uint8_t *code = kest_code_of(held->runtime, in, &many);
    if (code == NULL) {
        return;
    }
    uint32_t wide = kest_op_wide(really_at(held, in, (uint32_t)at, code));
    uint32_t after = (uint32_t)at + (wide == 0 ? 1 : wide);
    if (after < many) {
        write_one(held, in, after, false);
    }
}

// Where the machine is, as a line and a body, so a step knows when it has
// moved. Nought and -1 for a machine that is not stopped.
static void standing(Debugger *held, int32_t *in, uint32_t *line,
                     uint32_t *deep) {
    *in = kest_stopped_in(held->runtime);
    int64_t at = kest_stopped(held->runtime);
    *deep = kest_frames_deep(held->runtime);
    as_it_was(held);
    int64_t from = *in < 0 ? -1 : kest_came_from(held->runtime, *in,
                                                 (uint32_t)(at < 0 ? 0 : at));
    as_it_is(held);
    *line = from < 0 ? 0 : line_of(held, *in, (uint32_t)from);
}

// One instruction of movement: everything that could be next is patched, the
// one it is standing on comes out, and the machine is let go. A step of a line
// is this until the line changes, which is what makes a loop that jumps back
// to the line it came from stop there rather than run to the end.
static bool one_move(Debugger *held, bool into, Written *keeping,
                     bool *putting_back, bool *went) {
    uint32_t deep = kest_frames_deep(held->runtime);
    if (into) {
        for (uint32_t i = 0; i < held->build->module.count; i++) {
            write_every_line(held, (int32_t)i);
        }
    } else {
        for (uint32_t i = 0; i < deep; i++) {
            write_every_line(held, kest_frame_in(held->runtime, i));
        }
    }
    write_the_next_one(held);
    Written came_out = {0, 0, 0, false};
    if (clear_where_it_stands(held, &came_out) && !*putting_back) {
        *keeping = came_out;
        *putting_back = true;
    }
    *went = kest_resume(held->runtime, held->answer, 8);
    take_the_steps_out(held);
    return kest_stopped(held->runtime) >= 0;
}

// To the next line. Into stops wherever the machine goes; over carries on
// while the machine is deeper than it was, which steps over a call; out
// carries on until it is shallower, which is the line after the call this
// body was called from.
static KestDebugState step_a_line(Debugger *held, KestStep how) {
    int32_t was_in = -1;
    uint32_t was_line = 0;
    uint32_t was_deep = 0;
    standing(held, &was_in, &was_line, &was_deep);

    Written keeping = {0, 0, 0, false};
    bool putting_back = false;
    bool went = false;
    // Bounded, because a step that could not find a line to stop at would
    // otherwise be a debugger that never comes back.
    for (uint32_t guard = 0; guard < 1000000; guard++) {
        if (!one_move(held, how == KEST_STEP_INTO, &keeping, &putting_back,
                      &went)) {
            if (putting_back) {
                write_one(held, keeping.entry, keeping.at, true);
            }
            return carried_on(held, went);
        }
        int32_t in = -1;
        uint32_t line = 0;
        uint32_t deep = 0;
        standing(held, &in, &line, &deep);
        if (how == KEST_STEP_OUT) {
            if (deep < was_deep) {
                break;
            }
            continue;
        }
        if (how == KEST_STEP_OVER && deep > was_deep) {
            continue;
        }
        if (in == was_in && line == was_line && deep == was_deep) {
            continue;
        }
        break;
    }
    if (putting_back) {
        write_one(held, keeping.entry, keeping.at, true);
    }
    return carried_on(held, went);
}

// Let it go to the next breakpoint a person asked for. One line of movement
// first, because a breakpoint is the instruction that was there and standing
// on one is standing on the instruction: it has to come out for the machine to
// move, and it goes back the moment it has.
static KestDebugState let_it_go(Debugger *held) {
    Written keeping = {0, 0, 0, false};
    bool putting_back = false;
    bool went = false;
    if (kest_stopped(held->runtime) >= 0) {
        if (!one_move(held, false, &keeping, &putting_back, &went)) {
            if (putting_back) {
                write_one(held, keeping.entry, keeping.at, true);
            }
            return carried_on(held, went);
        }
        if (putting_back) {
            write_one(held, keeping.entry, keeping.at, true);
            putting_back = false;
        }
        // And past whatever this landed on, which was a step rather than
        // something anybody asked for.
        Written passing = {0, 0, 0, false};
        clear_where_it_stands(held, &passing);
        if (passing.entry != 0 || passing.at != 0 || passing.was != 0) {
            // It was one a person asked for after all, which is a breakpoint
            // one instruction along: stop there rather than run past it.
            write_one(held, passing.entry, passing.at, passing.asked_for);
            if (passing.asked_for) {
                return carried_on(held, true);
            }
        }
    }
    went = kest_resume(held->runtime, held->answer, 8);
    take_the_steps_out(held);
    return carried_on(held, went);
}

static void say_frames(Debugger *held) {
    uint32_t deep = kest_frames_deep(held->runtime);
    if (deep == 0) {
        fprintf(held->out, "nothing is running\n");
        return;
    }
    for (uint32_t i = deep; i > 0; i--) {
        int32_t in = kest_frame_in(held->runtime, i - 1);
        int64_t at = kest_frame_ip(held->runtime, i - 1);
        char before[32];
        snprintf(before, sizeof(before), "  %s", i == deep ? "-> " : "   ");
        say_where(held, in, at < 0 ? 0 : (uint32_t)at, before);
    }
}

// What a slot holds, written the way this language writes a value. The kind
// comes from the name the body gave the slot, which is what makes this more
// than a number.
static void value_of(char *into, size_t room, KestValue value, uint8_t kind) {
    switch (kind) {
    case KEST_L_F32:
    case KEST_L_F64:
        snprintf(into, room, "%g", value.real);
        break;
    case KEST_L_BOOL:
        snprintf(into, room, "%s", value.integer != 0 ? "true" : "false");
        break;
    case KEST_L_TEXT:
        snprintf(into, room, "\"%s\"", value.text == NULL ? "" : value.text);
        break;
    default:
        snprintf(into, room, "%lld", (long long)value.integer);
        break;
    }
}

static void say_value(Debugger *held, KestValue value, uint8_t kind) {
    char said[256];
    value_of(said, sizeof(said), value, kind);
    fputs(said, held->out);
}

static void say_locals(Debugger *held, const char *only) {
    uint32_t deep = kest_frames_deep(held->runtime);
    if (deep == 0) {
        fprintf(held->out, "nothing is running\n");
        return;
    }
    uint16_t wide = kest_frame_wide(held->runtime, deep - 1);
    uint32_t said = 0;
    for (uint16_t slot = 0; slot < wide; slot++) {
        uint16_t slots = 1;
        uint8_t kind = KEST_L_WORD;
        const char *name =
            kest_frame_name(held->runtime, deep - 1, slot, &slots, &kind);
        if (name == NULL) {
            continue;
        }
        if (only != NULL && strcmp(only, name) != 0) {
            continue;
        }
        KestValue value = {0};
        if (!kest_frame_slot(held->runtime, deep - 1, slot, &value)) {
            continue;
        }
        fprintf(held->out, "  %-20s slot %u  ", name, slot);
        // A slot that holds where the value is rather than the value, which a
        // `for` does for an element the body never writes (D866). Saying the
        // number as though it were the value is saying an address is a
        // `Body`. See D1081.
        if (kest_frame_at_address(held->runtime, deep - 1, slot)) {
            fprintf(held->out, "at %p", value.object);
        } else {
            say_value(held, value, kind);
        }
        if (slots > 1) {
            fprintf(held->out, "  (and %u slot(s) more)", slots - 1);
        }
        fputc('\n', held->out);
        said++;
    }
    if (said == 0) {
        fprintf(held->out, only == NULL ? "this body named no slot\n"
                                        : "no name here is that\n");
    }
}

// Every line a body is written over, so `break 12` can say whether 12 is a
// line this program runs.
static bool break_at(Debugger *held, const char *in, uint32_t wanted) {
    uint32_t put = 0;
    // The file the command line named, unless the word before the line says
    // another: a line number on its own means the file in front of the person,
    // and line 13 of the standard library is not that. `break io.kest:13` is
    // how to mean the other one.
    const KestSource *root =
        held->build->units.count > 0 ? &held->build->units.items[0].source
                                     : NULL;
    for (uint32_t entry = 0; entry < held->build->module.count; entry++) {
        const KestSource *came_from = source_of(held, (int32_t)entry);
        if (came_from == NULL) {
            continue;
        }
        if (in == NULL) {
            if (root == NULL || came_from != root) {
                continue;
            }
        } else if (strstr(came_from->path, in) == NULL) {
            continue;
        }
        uint32_t many = 0;
        uint8_t *code = kest_code_of(held->runtime, (int32_t)entry, &many);
        if (code == NULL) {
            continue;
        }
        uint32_t last = 0;
        bool first = true;
        as_it_was(held);
        for (uint32_t at = 0; at < many;) {
            int64_t from = kest_came_from(held->runtime, (int32_t)entry, at);
            uint32_t line =
                from < 0 ? 0 : line_of(held, (int32_t)entry, (uint32_t)from);
            if ((first || line != last) && line == wanted) {
                if (write_one(held, (int32_t)entry, at, true)) {
                    say_where(held, (int32_t)entry, at, "  breakpoint at ");
                    put++;
                }
            }
            if (first || line != last) {
                last = line;
                first = false;
            }
            uint32_t wide =
                kest_op_wide(really_at(held, (int32_t)entry, at, code));
            at += wide == 0 ? 1 : wide;
        }
        as_it_is(held);
    }
    return put > 0;
}

void kest_debugger_open(KestDebugger *held, KestBuild *build,
                        KestRuntime *runtime, FILE *out) {
    memset(held, 0, sizeof(*held));
    held->build = build;
    held->runtime = runtime;
    held->out = out;
}

uint32_t kest_debugger_break(KestDebugger *held, const char *file,
                             uint32_t line) {
    uint32_t before = held->count;
    break_at(held, file, line);
    return held->count - before;
}

void kest_debugger_unbreak(KestDebugger *held, const char *file) {
    const KestSource *root =
        held->build->units.count > 0 ? &held->build->units.items[0].source
                                     : NULL;
    for (uint32_t i = held->count; i > 0; i--) {
        const KestSource *came_from = source_of(held, held->written[i - 1].entry);
        bool here = file == NULL
                        ? came_from == root
                        : came_from != NULL &&
                              strstr(came_from->path, file) != NULL;
        if (held->written[i - 1].asked_for && here) {
            take_one_out(held, i - 1);
        }
    }
}

KestDebugState kest_debugger_run(KestDebugger *held, int32_t entry) {
    held->running = true;
    memset(held->answer, 0, sizeof(held->answer));
    bool went = kest_call(held->runtime, entry, held->answer, 8);
    return carried_on(held, went);
}

KestDebugState kest_debugger_continue(KestDebugger *held) {
    return let_it_go(held);
}

KestDebugState kest_debugger_step(KestDebugger *held, KestStep how) {
    return step_a_line(held, how);
}

uint32_t kest_debugger_frames(KestDebugger *held) {
    return kest_frames_deep(held->runtime);
}

bool kest_debugger_frame(KestDebugger *held, uint32_t deep, const char **body,
                         const char **path, uint32_t *line,
                         uint32_t *column) {
    if (deep >= kest_frames_deep(held->runtime)) {
        return false;
    }
    int32_t in = kest_frame_in(held->runtime, deep);
    int64_t at = kest_frame_ip(held->runtime, deep);
    const KestSource *source = source_of(held, in);
    as_it_was(held);
    int64_t from = kest_came_from(held->runtime, in, at < 0 ? 0 : (uint32_t)at);
    as_it_is(held);
    *line = 0;
    *column = 0;
    if (from >= 0 && source != NULL) {
        kest_source_locate(source, (uint32_t)from, line, column);
    }
    *path = source == NULL ? NULL : source->path;
    *body = in >= 0 && (uint32_t)in < held->build->module.count
                ? held->build->module.functions[in]->name
                : NULL;
    return true;
}

uint16_t kest_debugger_wide(KestDebugger *held, uint32_t deep) {
    return kest_frame_wide(held->runtime, deep);
}

const char *kest_debugger_local(KestDebugger *held, uint32_t deep,
                                uint16_t slot, char *value, size_t room,
                                uint16_t *slots) {
    uint8_t kind = KEST_L_WORD;
    *slots = 1;
    const char *name = kest_frame_name(held->runtime, deep, slot, slots, &kind);
    if (name == NULL) {
        return NULL;
    }
    KestValue held_there = {0};
    if (!kest_frame_slot(held->runtime, deep, slot, &held_there)) {
        return NULL;
    }
    // A slot that holds where the value is rather than the value. See D1081.
    if (kest_frame_at_address(held->runtime, deep, slot)) {
        snprintf(value, room, "at %p", held_there.object);
    } else {
        value_of(value, room, held_there, kind);
    }
    return name;
}

void kest_debugger_close(KestDebugger *held) {
    // Everything written over, put back, so the program is the program again.
    while (held->count > 0) {
        take_one_out(held, held->count - 1);
    }
}

static void help(FILE *out) {
    fputs("  break <line>   stop where that line is run\n"
          "  run            start the program\n"
          "  continue       let it go to the next breakpoint\n"
          "  step           to the next line, whatever body it is in\n"
          "  next           to the next line of this body\n"
          "  where          the frames, innermost last\n"
          "  locals [name]  what this body called its slots, and what is in\n"
          "                 them\n"
          "  quit           stop and leave\n",
          out);
}

int kest_debug_serve(KestBuild *build, KestRuntime *runtime, const char *entry,
                     FILE *in, FILE *out) {
    Debugger held;
    kest_debugger_open(&held, build, runtime, out);

    int32_t start = kest_entry(runtime, entry);
    if (start < 0) {
        fprintf(out, "this program has no `%s` to run\n", entry);
        return 1;
    }
    fprintf(out, "kest debug. `help` says what there is.\n");

    char line[512];
    while (!held.over && fgets(line, sizeof(line), in) != NULL) {
        char *at = line;
        while (*at == ' ' || *at == '\t') {
            at++;
        }
        char *end = at + strlen(at);
        while (end > at && (end[-1] == '\n' || end[-1] == '\r' ||
                            end[-1] == ' ')) {
            *--end = '\0';
        }
        char *rest = strchr(at, ' ');
        if (rest != NULL) {
            *rest++ = '\0';
            while (*rest == ' ') {
                rest++;
            }
        }

        if (*at == '\0') {
            continue;
        }
        if (strcmp(at, "help") == 0) {
            help(out);
        } else if (strcmp(at, "break") == 0 || strcmp(at, "b") == 0) {
            if (rest == NULL) {
                fprintf(out, "`break` wants a line\n");
            } else {
                // `break 13` is this file, `break io.kest:13` is that one.
                char *colon = strrchr(rest, ':');
                const char *named_file = NULL;
                if (colon != NULL) {
                    *colon = '\0';
                    named_file = rest;
                    rest = colon + 1;
                }
                if (!break_at(&held, named_file,
                              (uint32_t)strtoul(rest, NULL, 10))) {
                    fprintf(out, "no instruction came from line %s%s%s\n",
                            named_file == NULL ? "" : named_file,
                            named_file == NULL ? "" : ":", rest);
                }
            }
        } else if (strcmp(at, "run") == 0 || strcmp(at, "r") == 0) {
            if (held.running) {
                fprintf(out, "it is already running\n");
                continue;
            }
            kest_debugger_run(&held, start);
        } else if (strcmp(at, "continue") == 0 || strcmp(at, "c") == 0) {
            if (!held.running) {
                fprintf(out, "nothing is running: `run` first\n");
            } else {
                kest_debugger_continue(&held);
            }
        } else if (strcmp(at, "step") == 0 || strcmp(at, "s") == 0 ||
                   strcmp(at, "next") == 0 || strcmp(at, "n") == 0) {
            if (!held.running) {
                fprintf(out, "nothing is running: `run` first\n");
                continue;
            }
            kest_debugger_step(&held, at[0] == 's' ? KEST_STEP_INTO
                                                   : KEST_STEP_OVER);
        } else if (strcmp(at, "where") == 0 || strcmp(at, "w") == 0) {
            say_frames(&held);
        } else if (strcmp(at, "locals") == 0 || strcmp(at, "l") == 0) {
            say_locals(&held, rest);
        } else if (strcmp(at, "quit") == 0 || strcmp(at, "q") == 0) {
            held.over = true;
        } else {
            fprintf(out, "`%s` is not a word here. `help` says what is.\n", at);
        }
    }
    kest_debugger_close(&held);
    return 0;
}
