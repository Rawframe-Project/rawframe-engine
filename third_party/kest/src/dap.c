#include "dap.h"

#include <stdlib.h>
#include <string.h>

#include "wire.h"

// What the adapter keeps between messages: its own count of what it has sent,
// where it writes, the program once it is launched, and the two files the
// program writes into and the machine says what went wrong into, each read
// back from where it was last read to and handed to the editor as output.
typedef struct {
    FILE *out;
    uint32_t sent;
    const char *library;
    KestHostMaker make;
    KestBuild *build;
    KestHost *host;
    KestRuntime *runtime;
    KestDebugger debugger;
    bool debugging;
    int32_t entry;
    FILE *wrote;
    FILE *nothing;
    long read_to;
    bool over;
    int exit_code;
} Adapter;

static void send(Adapter *adapter, KestSaid *said) {
    if (said->broke) {
        kest_wire_let_go(said);
        return;
    }
    kest_wire_send(adapter->out, said->bytes, said->used);
    kest_wire_let_go(said);
}

// A reply to one request, with what goes in its body already written as JSON,
// or NULL for none.
static void answer(Adapter *adapter, const KestJson *request, bool success,
                   const char *message, const char *body) {
    const KestJson *seq = kest_wire_member(request, "seq");
    const KestJson *command = kest_wire_member(request, "command");
    KestSaid said = {0};
    kest_wire_sayf(&said, "{\"seq\":%u,\"type\":\"response\",\"request_seq\":%u,"
                          "\"success\":%s,\"command\":",
                   ++adapter->sent,
                   seq == NULL ? 0u : (unsigned)seq->number,
                   success ? "true" : "false");
    kest_wire_escaped(&said, command == NULL ? "" : command->text,
                      command == NULL ? 0 : command->length);
    if (message != NULL) {
        kest_wire_say(&said, ",\"message\":");
        kest_wire_text(&said, message);
    }
    if (body != NULL) {
        kest_wire_say(&said, ",\"body\":");
        kest_wire_say(&said, body);
    }
    kest_wire_char(&said, '}');
    send(adapter, &said);
}

static void event(Adapter *adapter, const char *name, const char *body) {
    KestSaid said = {0};
    kest_wire_sayf(&said, "{\"seq\":%u,\"type\":\"event\",\"event\":",
                   ++adapter->sent);
    kest_wire_text(&said, name);
    if (body != NULL) {
        kest_wire_say(&said, ",\"body\":");
        kest_wire_say(&said, body);
    }
    kest_wire_char(&said, '}');
    send(adapter, &said);
}

// Words for the editor's console, in the category it shows them under.
static void output(Adapter *adapter, const char *category, const char *text,
                   size_t length) {
    if (length == 0) {
        return;
    }
    KestSaid said = {0};
    kest_wire_say(&said, "{\"category\":");
    kest_wire_text(&said, category);
    kest_wire_say(&said, ",\"output\":");
    kest_wire_escaped(&said, text, length);
    kest_wire_char(&said, '}');
    if (!said.broke) {
        event(adapter, "output", said.bytes);
    }
    kest_wire_let_go(&said);
}

// Everything written into a file since it was last read, handed over as
// output, and the file left where the next write goes.
static void relay(Adapter *adapter, FILE *from, long *read_to,
                  const char *category) {
    if (from == NULL) {
        return;
    }
    fflush(from);
    if (fseek(from, 0, SEEK_END) != 0) {
        return;
    }
    long end = ftell(from);
    if (end <= *read_to) {
        return;
    }
    size_t many = (size_t)(end - *read_to);
    char *bytes = malloc(many);
    if (bytes == NULL || fseek(from, *read_to, SEEK_SET) != 0) {
        free(bytes);
        return;
    }
    size_t got = fread(bytes, 1, many, from);
    output(adapter, category, bytes, got);
    free(bytes);
    *read_to = end;
    fseek(from, 0, SEEK_END);
}

// What happened when the machine was let go, told to the editor: where it
// stopped and why, or that the program is over and how.
static void after(Adapter *adapter, KestDebugState state, const char *why) {
    relay(adapter, adapter->wrote, &adapter->read_to, "stdout");
    if (state == KEST_DEBUG_STOPPED) {
        char body[160];
        snprintf(body, sizeof body,
                 "{\"reason\":\"%s\",\"threadId\":1,\"allThreadsStopped\":true}",
                 why);
        event(adapter, "stopped", body);
        return;
    }
    if (state == KEST_DEBUG_FAILED) {
        // What the machine said about why, which the words say to a person
        // at a terminal and the editor shows on its console.
        FILE *report = tmpfile();
        if (report != NULL) {
            long nothing_read = 0;
            kest_report(adapter->runtime, report, KEST_FORM_TEXT);
            relay(adapter, report, &nothing_read, "stderr");
            fclose(report);
        }
        adapter->exit_code = 1;
    }
    // What `main` answered is the program's exit status, as it is at a
    // terminal.
    if (state == KEST_DEBUG_FINISHED &&
        kest_frame_gives(adapter->runtime, adapter->entry) != NULL) {
        adapter->exit_code = (int)adapter->debugger.answer[0].integer;
    }
    char body[64];
    snprintf(body, sizeof body, "{\"exitCode\":%d}", adapter->exit_code);
    event(adapter, "exited", body);
    event(adapter, "terminated", NULL);
}

// The program the editor named, built the way `kest debug` builds one -- every
// call a call, so a frame stands for every body the program wrote -- and a
// machine started on it with the command line's own host. Answers why not, or
// NULL.
static const char *launch(Adapter *adapter, const char *program,
                          FILE *diagnostics) {
    char *paths[1] = {(char *)program};
    adapter->build = kest_build_open(adapter->library, paths, 1, 0);
    if (adapter->build == NULL) {
        return "there is not enough memory to read the program";
    }
    kest_build_calls_as_written(adapter->build);
    if (!kest_build_emit(adapter->build)) {
        kest_build_report(adapter->build, diagnostics, KEST_FORM_TEXT);
        return "the program does not compile";
    }
    adapter->wrote = tmpfile();
    adapter->nothing = tmpfile();
    if (adapter->wrote == NULL || adapter->nothing == NULL) {
        return "there is nowhere to put what the program writes";
    }
    adapter->host = adapter->make(adapter->wrote, adapter->nothing);
    if (adapter->host == NULL) {
        return "there is not enough memory for a host";
    }
    adapter->runtime = kest_start(adapter->build, adapter->host, NULL);
    if (adapter->runtime == NULL) {
        kest_build_report(adapter->build, diagnostics, KEST_FORM_TEXT);
        return "the program did not start";
    }
    adapter->entry = kest_entry(adapter->runtime,
                                kest_build_name(adapter->build, KEST_MAIN));
    if (adapter->entry < 0) {
        return "this program has no `main` to run";
    }
    kest_debugger_open(&adapter->debugger, adapter->build, adapter->runtime,
                       NULL);
    adapter->debugging = true;
    return NULL;
}

static void breakpoints(Adapter *adapter, const KestJson *request) {
    const KestJson *arguments = kest_wire_member(request, "arguments");
    const KestJson *path = kest_wire_down(arguments, "source", "path");
    const KestJson *wanted = kest_wire_member(arguments, "breakpoints");
    KestSaid said = {0};
    kest_wire_say(&said, "{\"breakpoints\":[");
    const char *file = path != NULL && path->kind == KEST_JSON_TEXT
                           ? path->text
                           : NULL;
    // The editor sends a file's whole list every time, so what was there
    // before is taken out first.
    if (adapter->debugging && file != NULL) {
        kest_debugger_unbreak(&adapter->debugger, file);
    }
    for (uint32_t i = 0; wanted != NULL && wanted->kind == KEST_JSON_LIST &&
                         i < wanted->count;
         i++) {
        const KestJson *line = kest_wire_member(wanted->items[i], "line");
        uint32_t at = line == NULL ? 0 : (uint32_t)line->number;
        bool put = adapter->debugging && file != NULL &&
                   kest_debugger_break(&adapter->debugger, file, at) > 0;
        kest_wire_sayf(&said, "%s{\"verified\":%s,\"line\":%u}",
                       i == 0 ? "" : ",", put ? "true" : "false", at);
    }
    kest_wire_say(&said, "]}");
    answer(adapter, request, !said.broke, NULL, said.bytes);
    kest_wire_let_go(&said);
}

// The last piece of a path, which is what an editor puts on a tab.
static const char *last_piece(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    if (back != NULL && (slash == NULL || back > slash)) {
        slash = back;
    }
    return slash == NULL ? path : slash + 1;
}

// The frames, innermost first, which is the order an editor lists them in. A
// frame's number is how deep it is plus one, so nought is never one, and it is
// also the number its locals are asked for by.
static void stack(Adapter *adapter, const KestJson *request) {
    KestSaid said = {0};
    uint32_t deep = adapter->debugging
                        ? kest_debugger_frames(&adapter->debugger)
                        : 0;
    kest_wire_say(&said, "{\"stackFrames\":[");
    for (uint32_t i = deep; i > 0; i--) {
        const char *body = NULL;
        const char *path = NULL;
        uint32_t line = 0;
        uint32_t column = 0;
        if (!kest_debugger_frame(&adapter->debugger, i - 1, &body, &path, &line,
                                 &column)) {
            continue;
        }
        kest_wire_sayf(&said, "%s{\"id\":%u,\"name\":", i == deep ? "" : ",",
                       i);
        kest_wire_text(&said, body == NULL ? "?" : body);
        if (path != NULL) {
            kest_wire_say(&said, ",\"source\":{\"name\":");
            kest_wire_text(&said, last_piece(path));
            kest_wire_say(&said, ",\"path\":");
            kest_wire_text(&said, path);
            kest_wire_char(&said, '}');
        }
        kest_wire_sayf(&said, ",\"line\":%u,\"column\":%u}", line, column);
    }
    kest_wire_sayf(&said, "],\"totalFrames\":%u}", deep);
    answer(adapter, request, !said.broke, NULL, said.bytes);
    kest_wire_let_go(&said);
}

static void scopes(Adapter *adapter, const KestJson *request) {
    const KestJson *frame = kest_wire_down(request, "arguments", "frameId");
    char body[160];
    snprintf(body, sizeof body,
             "{\"scopes\":[{\"name\":\"Locals\",\"variablesReference\":%u,"
             "\"expensive\":false}]}",
             frame == NULL ? 0u : (unsigned)frame->number);
    answer(adapter, request, true, NULL, body);
}

// What a body called its slots and what is in them. A value wider than a slot
// is said at its first slot, the way `kest debug` says it.
static void variables(Adapter *adapter, const KestJson *request) {
    const KestJson *which =
        kest_wire_down(request, "arguments", "variablesReference");
    uint32_t frame = which == NULL ? 0 : (uint32_t)which->number;
    KestSaid said = {0};
    kest_wire_say(&said, "{\"variables\":[");
    if (adapter->debugging && frame > 0 &&
        frame <= kest_debugger_frames(&adapter->debugger)) {
        uint32_t deep = frame - 1;
        uint16_t wide = kest_debugger_wide(&adapter->debugger, deep);
        bool first = true;
        for (uint16_t slot = 0; slot < wide;) {
            char value[256];
            uint16_t slots = 1;
            const char *name = kest_debugger_local(
                &adapter->debugger, deep, slot, value, sizeof value, &slots);
            if (name != NULL) {
                kest_wire_say(&said, first ? "{\"name\":" : ",{\"name\":");
                kest_wire_text(&said, name);
                kest_wire_say(&said, ",\"value\":");
                kest_wire_text(&said, value);
                kest_wire_say(&said, ",\"variablesReference\":0}");
                first = false;
            }
            slot = (uint16_t)(slot + (slots == 0 ? 1 : slots));
        }
    }
    kest_wire_say(&said, "]}");
    answer(adapter, request, !said.broke, NULL, said.bytes);
    kest_wire_let_go(&said);
}

// A request that moves the machine, answered before it moves: the editor is
// told the request was taken and then told where the machine ended up.
static void moving(Adapter *adapter, const KestJson *request, KestStep how,
                   bool stepping) {
    if (!adapter->debugging || !adapter->debugger.running) {
        answer(adapter, request, false, "nothing is running", NULL);
        return;
    }
    answer(adapter, request, true, NULL,
           stepping ? NULL : "{\"allThreadsContinued\":true}");
    KestDebugState state = stepping
                               ? kest_debugger_step(&adapter->debugger, how)
                               : kest_debugger_continue(&adapter->debugger);
    after(adapter, state, stepping ? "step" : "breakpoint");
}

static bool command_is(const KestJson *command, const char *name) {
    return command != NULL && command->kind == KEST_JSON_TEXT &&
           strcmp(command->text, name) == 0;
}

static void handle(Adapter *adapter, const KestJson *request) {
    const KestJson *command = kest_wire_member(request, "command");
    if (command_is(command, "initialize")) {
        answer(adapter, request, true, NULL,
               "{\"supportsConfigurationDoneRequest\":true,"
               "\"supportsTerminateRequest\":true}");
        event(adapter, "initialized", NULL);
    } else if (command_is(command, "launch")) {
        const KestJson *program = kest_wire_down(request, "arguments",
                                                 "program");
        if (program == NULL || program->kind != KEST_JSON_TEXT) {
            answer(adapter, request, false,
                   "`launch` names the program to run as `program`", NULL);
            return;
        }
        FILE *diagnostics = tmpfile();
        const char *why = launch(adapter, program->text, diagnostics);
        long nothing_read = 0;
        relay(adapter, diagnostics, &nothing_read, "stderr");
        if (diagnostics != NULL) {
            fclose(diagnostics);
        }
        answer(adapter, request, why == NULL, why, NULL);
    } else if (command_is(command, "setBreakpoints")) {
        breakpoints(adapter, request);
    } else if (command_is(command, "setExceptionBreakpoints")) {
        answer(adapter, request, true, NULL, "{\"breakpoints\":[]}");
    } else if (command_is(command, "configurationDone")) {
        answer(adapter, request, true, NULL, NULL);
        if (adapter->debugging && !adapter->debugger.running) {
            after(adapter,
                  kest_debugger_run(&adapter->debugger, adapter->entry),
                  "breakpoint");
        }
    } else if (command_is(command, "threads")) {
        answer(adapter, request, true, NULL,
               "{\"threads\":[{\"id\":1,\"name\":\"main\"}]}");
    } else if (command_is(command, "stackTrace")) {
        stack(adapter, request);
    } else if (command_is(command, "scopes")) {
        scopes(adapter, request);
    } else if (command_is(command, "variables")) {
        variables(adapter, request);
    } else if (command_is(command, "continue")) {
        moving(adapter, request, KEST_STEP_OVER, false);
    } else if (command_is(command, "next")) {
        moving(adapter, request, KEST_STEP_OVER, true);
    } else if (command_is(command, "stepIn")) {
        moving(adapter, request, KEST_STEP_INTO, true);
    } else if (command_is(command, "stepOut")) {
        moving(adapter, request, KEST_STEP_OUT, true);
    } else if (command_is(command, "disconnect") ||
               command_is(command, "terminate")) {
        answer(adapter, request, true, NULL, NULL);
        adapter->over = true;
    } else {
        // Said rather than left unanswered: an editor waits for a reply to
        // every request, and one that never comes is a debugger that hangs.
        answer(adapter, request, false, "this adapter does not do that", NULL);
    }
}

int kest_dap_serve(const char *library, KestHostMaker make, FILE *in,
                   FILE *out) {
    Adapter adapter;
    memset(&adapter, 0, sizeof adapter);
    adapter.out = out;
    adapter.library = library;
    adapter.make = make;
    KestArena *arena = kest_arena_new();
    if (arena == NULL) {
        return 1;
    }
    while (!adapter.over) {
        size_t length = 0;
        char *body = kest_wire_next(in, &length);
        if (body == NULL) {
            break;
        }
        KestMark before = kest_arena_mark(arena);
        KestJson *request = kest_wire_read(arena, body, length);
        if (request != NULL) {
            handle(&adapter, request);
        }
        kest_arena_rewind(arena, before);
        free(body);
    }
    if (adapter.debugging) {
        kest_debugger_close(&adapter.debugger);
    }
    if (adapter.runtime != NULL) {
        kest_runtime_free(adapter.runtime);
    }
    if (adapter.build != NULL) {
        kest_build_free(adapter.build);
    }
    if (adapter.host != NULL) {
        kest_host_free(adapter.host);
    }
    if (adapter.wrote != NULL) {
        fclose(adapter.wrote);
    }
    if (adapter.nothing != NULL) {
        fclose(adapter.nothing);
    }
    kest_arena_free(arena);
    return 0;
}
