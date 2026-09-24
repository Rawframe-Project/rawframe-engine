#include "lsp.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "diag.h"
#include "mem.h"
#include "fmt.h"
#include "types.h"
#include "wire.h"

// One file the client has opened: the uri it names the file by, the path the
// rest of this compiler knows it by, and the text as the person has it rather
// than as the disk has it.
typedef struct {
    char *uri;
    char *path;
    char *text;
    size_t length;
} Open;

// What `here` answers when the client has opened nothing, so that a handler
// reads a file rather than a null. Its text is empty and its path is no path,
// which every walk below already asks about.
static char no_text[1] = "";
static Open nothing_open = {NULL, NULL, no_text, 0};

// What this server keeps: every file the client has opened, which one the
// message in hand is about, and the last build of one of them.
//
// There is more than one file because an editor holds more than one open and
// addresses every message to the one it means. The file being typed in is not
// always the one opened last, and a server that kept a single file wrote a
// change to one into another: before D1143, opening A, opening B and then
// typing in A published A's mistakes against B's name and left A looking
// clean.
//
// There is one build, because building is what every answer is made of and
// building every open file at once is work nobody asked for. `built` says
// which file the build in hand is of, so a request about another rebuilds --
// which costs what a keystroke in that file costs, because a keystroke
// rebuilds too.
typedef struct {
    KestArena *arena;
    const char *library;
    FILE *out;
    Open *open;
    size_t count;
    size_t room;
    size_t at;
    KestBuild *build;
    char *built;
    bool shutting_down;
} Server;

// The file the message in hand is about. `handle` chooses it out of the open
// set before it dispatches anything, and a message naming a file nothing was
// opened for is answered before a handler is reached.
static Open *here(Server *server) {
    return server->at < server->count ? &server->open[server->at]
                                      : &nothing_open;
}

// A copy this server holds on to. Everything a message carries sits in an
// arena that is rewound the moment the message is answered, so a document kept
// between messages is a document copied out of it.
static char *copy_of(const char *text, size_t length) {
    char *held = malloc(length + 1);
    if (held == NULL) {
        return NULL;
    }
    memcpy(held, text, length);
    held[length] = '\0';
    return held;
}

static bool point_at(Server *server, const char *uri, size_t length) {
    for (size_t i = 0; i < server->count; i++) {
        if (server->open[i].uri != NULL &&
            kest_word_same(server->open[i].uri, uri, length)) {
            server->at = i;
            return true;
        }
    }
    server->at = server->count;
    return false;
}

static Open *room_for_another(Server *server) {
    if (server->count == server->room) {
        size_t grown = server->room == 0 ? 4 : server->room * 2;
        Open *moved = realloc(server->open, grown * sizeof(*moved));
        if (moved == NULL) {
            return NULL;
        }
        server->open = moved;
        server->room = grown;
    }
    Open *one = &server->open[server->count++];
    memset(one, 0, sizeof(*one));
    return one;
}

static void send(Server *server, const char *body, size_t length) {
    kest_wire_send(server->out, body, length);
}

// Where a byte offset is, as a client counts: lines from nought, and columns
// in sixteen-bit units, which is what LSP means by a character unless the
// client says otherwise.
static void locate(const char *text, size_t length, size_t offset,
                   uint32_t *line, uint32_t *column) {
    uint32_t at_line = 0;
    size_t line_start = 0;
    for (size_t i = 0; i < offset && i < length; i++) {
        if (text[i] == '\n') {
            at_line++;
            line_start = i + 1;
        }
    }
    uint32_t units = 0;
    for (size_t i = line_start; i < offset && i < length; ) {
        unsigned char c = (unsigned char)text[i];
        size_t wide = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2
                                 : (c & 0xf0) == 0xe0   ? 3
                                                        : 4;
        units += wide == 4 ? 2 : 1;
        i += wide;
    }
    *line = at_line;
    *column = units;
}

// And the other way: what a client called line and column, as a byte offset.
static size_t offset_of(const char *text, size_t length, uint32_t line,
                        uint32_t column) {
    size_t at = 0;
    for (uint32_t seen = 0; seen < line && at < length; at++) {
        if (text[at] == '\n') {
            seen++;
        }
    }
    uint32_t units = 0;
    while (at < length && text[at] != '\n' && units < column) {
        unsigned char c = (unsigned char)text[at];
        size_t wide = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2
                                 : (c & 0xf0) == 0xe0   ? 3
                                                        : 4;
        units += wide == 4 ? 2 : 1;
        at += wide;
    }
    return at;
}

static void put_range(KestSaid *out, const char *text, size_t length,
                      size_t from, size_t count) {
    uint32_t line = 0;
    uint32_t column = 0;
    locate(text, length, from, &line, &column);
    kest_wire_sayf(out, "{\"start\":{\"line\":%u,\"character\":%u},", line, column);
    locate(text, length, from + count, &line, &column);
    kest_wire_sayf(out, "\"end\":{\"line\":%u,\"character\":%u}}", line, column);
}

// A path out of `file:///...`, with the percent escapes read back. Anything
// that is not a `file:` URI is handed back as it is, which is what a client
// that sends a path rather than a URI wants.
static uint32_t nibble(char c) {
    if (c >= '0' && c <= '9') {
        return (uint32_t)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (uint32_t)(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return (uint32_t)(c - 'A' + 10);
    }
    return 0;
}

// The path is the server's own and outlives every message, because the arena a
// message is read into is rewound the moment it is answered. A path pointing
// into that is a path that reads as whatever the next message wrote there.
static char *path_of_uri(const char *uri, size_t length) {
    const char *from = uri;
    size_t left = length;
    if (left > 7 && memcmp(from, "file://", 7) == 0) {
        from += 7;
        left -= 7;
        // `file:///c:/x` on Windows has a slash before the drive letter that
        // is part of the URI and not part of the path.
        if (left > 2 && from[0] == '/' && from[2] == ':') {
            from++;
            left--;
        }
    }
    char *out = malloc(left + 1);
    if (out == NULL) {
        return NULL;
    }
    size_t used = 0;
    for (size_t i = 0; i < left; i++) {
        if (from[i] == '%' && i + 2 < left) {
            out[used++] = (char)((nibble(from[i + 1]) << 4) |
                                 nibble(from[i + 2]));
            i += 2;
            continue;
        }
        out[used++] = from[i];
    }
    out[used] = '\0';
    return out;
}

static void put_uri(KestSaid *out, const char *path) {
    kest_wire_say(out, "\"file://");
    for (const char *at = path; *at != '\0'; at++) {
        unsigned char c = (unsigned char)*at;
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '/' || c == '.' ||
                     c == '-' || c == '_' || c == '~' || c == ':';
        if (plain) {
            kest_wire_char(out, (char)c);
        } else {
            kest_wire_sayf(out, "%%%02X", c);
        }
    }
    kest_wire_char(out, '"');
}

// The file a source is, as text this server holds. A build reads every file a
// program imports, so an answer may be about a file that is not the one open.
static const char *text_of(const KestSource *source, size_t *length) {
    if (source == NULL) {
        *length = 0;
        return "";
    }
    *length = source->length;
    return source->text;
}

static void build_again(Server *server) {
    if (server->build != NULL) {
        kest_build_free(server->build);
        server->build = NULL;
    }
    free(server->built);
    server->built = NULL;
    Open *file = here(server);
    if (file->path == NULL) {
        return;
    }
    // Every open file goes in front of the disk, not only the one being built:
    // a file that imports one the person has edited and not saved has to be
    // checked against what they can see rather than against what was saved.
    for (size_t i = 0; i < server->count; i++) {
        if (server->open[i].path != NULL) {
            kest_loader_overlay(server->open[i].path, server->open[i].text,
                                server->open[i].length);
        }
    }
    char *paths[1] = {file->path};
    server->build = kest_build_open(server->library, paths, 1, 0);
    if (server->build != NULL) {
        kest_build_index_names(server->build, true);
        kest_build_check(server->build);
        server->built = copy_of(file->path, strlen(file->path));
    }
    kest_loader_overlay(NULL, NULL, 0);
}

// The build a request is answered out of, which has to be of the file the
// request names. A request about the file last built costs nothing here.
static void ready(Server *server) {
    Open *file = here(server);
    if (server->build != NULL && server->built != NULL &&
        file->path != NULL && strcmp(server->built, file->path) == 0) {
        return;
    }
    build_again(server);
}

// What is wrong with the file, sent whether or not anything is: a client that
// is told nothing leaves the last set on the screen.
static void publish(Server *server) {
    Open *file = here(server);
    KestSaid out = {0};
    kest_wire_say(&out, "{\"jsonrpc\":\"2.0\",\"method\":"
              "\"textDocument/publishDiagnostics\",\"params\":{\"uri\":");
    kest_wire_text(&out, file->uri);
    kest_wire_say(&out, ",\"diagnostics\":[");
    bool first = true;
    if (server->build != NULL) {
        const KestDiags *diags = &server->build->diags;
        for (uint32_t i = 0; i < diags->count; i++) {
            const KestDiag *one = &diags->items[i];
            // Only what is wrong with the file in front of the person. A
            // program is more than one file and a client is told about one.
            if (one->source == NULL || file->path == NULL ||
                strcmp(one->source->path, file->path) != 0) {
                continue;
            }
            size_t length = 0;
            const char *text = text_of(one->source, &length);
            kest_wire_say(&out, first ? "" : ",");
            first = false;
            kest_wire_say(&out, "{\"range\":");
            put_range(&out, text, length, one->span.offset,
                      one->span.length == 0 ? 1 : one->span.length);
            kest_wire_sayf(&out, ",\"severity\":%d,\"source\":\"kest\",\"code\":",
                    one->severity == KEST_SEVERITY_ERROR ? 1 : 2);
            kest_wire_text(&out, one->code);
            kest_wire_say(&out, ",\"message\":");
            if (one->suggestion != NULL && one->suggestion[0] != '\0') {
                size_t room = strlen(one->message) +
                              strlen(one->suggestion) + 4;
                char *both = kest_arena_alloc(server->arena, room, 1);
                if (both != NULL) {
                    snprintf(both, room, "%s\n%s", one->message,
                             one->suggestion);
                    kest_wire_text(&out, both);
                } else {
                    kest_wire_text(&out, one->message);
                }
            } else {
                kest_wire_text(&out, one->message);
            }
            kest_wire_char(&out, '}');
        }
    }
    kest_wire_say(&out, "]}}");
    if (!out.broke && out.bytes != NULL) {
        send(server, out.bytes, out.used);
    }
    kest_wire_let_go(&out);
}

// Which name is written under this offset, out of the index the checker wrote
// while it resolved. The nearest one that covers it, because a field access is
// a name inside a name.
static const KestUse *use_at(Server *server, size_t offset) {
    if (server->build == NULL || server->build->program == NULL) {
        return NULL;
    }
    uint32_t count = 0;
    const KestUse *uses = kest_program_uses(server->build->program, &count);
    const KestUse *best = NULL;
    const Open *file = here(server);
    for (uint32_t i = 0; i < count; i++) {
        const KestUse *one = &uses[i];
        if (one->source == NULL || file->path == NULL ||
            strcmp(one->source->path, file->path) != 0) {
            continue;
        }
        if (offset < one->span.offset ||
            offset >= (size_t)one->span.offset + one->span.length) {
            continue;
        }
        if (best == NULL || one->span.length < best->span.length) {
            best = one;
        }
    }
    return best;
}

static bool same_declaration(const KestUse *a, const KestUse *b) {
    return a->declared_in == b->declared_in &&
           a->declared.offset == b->declared.offset &&
           a->is_local == b->is_local;
}

static void put_id(KestSaid *out, const KestJson *id) {
    if (id == NULL) {
        kest_wire_say(out, "null");
    } else if (id->kind == KEST_JSON_TEXT) {
        kest_wire_escaped(out, id->text, id->length);
    } else {
        kest_wire_sayf(out, "%lld", (long long)id->number);
    }
}

// Every answer is written into a buffer of its own and framed the same way, so
// the framing is written once rather than once per request.
static void start_answer(KestSaid *out, const KestJson *id) {
    kest_wire_say(out, "{\"jsonrpc\":\"2.0\",\"id\":");
    put_id(out, id);
    kest_wire_say(out, ",\"result\":");
}

static void finish_answer(Server *server, KestSaid *out) {
    kest_wire_char(out, '}');
    if (!out->broke && out->bytes != NULL) {
        send(server, out->bytes, out->used);
    }
    kest_wire_let_go(out);
}

static void answer(Server *server, const KestJson *id, const char *result) {
    KestSaid out = {0};
    start_answer(&out, id);
    kest_wire_say(&out, result);
    finish_answer(server, &out);
}

static size_t position_in(Server *server, const KestJson *params) {
    const KestJson *position = kest_wire_member(params, "position");
    const KestJson *line = kest_wire_member(position, "line");
    const KestJson *column = kest_wire_member(position, "character");
    if (line == NULL || column == NULL) {
        return (size_t)-1;
    }
    Open *file = here(server);
    return offset_of(file->text, file->length, (uint32_t)line->number,
                     (uint32_t)column->number);
}

static void hover(Server *server, const KestJson *id, const KestJson *params) {
    size_t offset = position_in(server, params);
    const KestUse *one = offset == (size_t)-1 ? NULL : use_at(server, offset);
    KestSaid out = {0};
    start_answer(&out, id);
    if (one == NULL || server->build == NULL) {
        kest_wire_say(&out, "null");
    } else {
        const char *written =
            kest_type_name(server->build->arena, (KestType *)one->type);
        size_t room = strlen(written) + (size_t)one->span.length + 16;
        char *said = kest_arena_alloc(server->arena, room, 1);
        if (said == NULL) {
            kest_wire_say(&out, "null");
        } else {
            snprintf(said, room, "%.*s: %s", (int)one->span.length,
                     kest_span_text(one->source, one->span), written);
            kest_wire_say(&out, "{\"contents\":{\"kind\":\"plaintext\",\"value\":");
            kest_wire_text(&out, said);
            kest_wire_say(&out, "},\"range\":");
            put_range(&out, here(server)->text, here(server)->length,
                      one->span.offset, one->span.length);
            kest_wire_char(&out, '}');
        }
    }
    finish_answer(server, &out);
}

static void definition(Server *server, const KestJson *id, const KestJson *params) {
    size_t offset = position_in(server, params);
    const KestUse *one = offset == (size_t)-1 ? NULL : use_at(server, offset);
    KestSaid out = {0};
    start_answer(&out, id);
    if (one == NULL || one->declared_in == NULL) {
        kest_wire_say(&out, "null");
    } else {
        size_t length = 0;
        const char *text = text_of(one->declared_in, &length);
        kest_wire_say(&out, "{\"uri\":");
        put_uri(&out, one->declared_in->path);
        kest_wire_say(&out, ",\"range\":");
        put_range(&out, text, length, one->declared.offset,
                  one->declared.length);
        kest_wire_char(&out, '}');
    }
    finish_answer(server, &out);
}

static void references(Server *server, const KestJson *id, const KestJson *params) {
    size_t offset = position_in(server, params);
    const KestUse *wanted = offset == (size_t)-1 ? NULL
                                                 : use_at(server, offset);
    KestSaid out = {0};
    start_answer(&out, id);
    kest_wire_char(&out, '[');
    bool first = true;
    if (wanted != NULL && server->build != NULL &&
        server->build->program != NULL) {
        uint32_t count = 0;
        const KestUse *uses = kest_program_uses(server->build->program,
                                                &count);
        for (uint32_t i = 0; i < count; i++) {
            if (!same_declaration(&uses[i], wanted)) {
                continue;
            }
            size_t length = 0;
            const char *text = text_of(uses[i].source, &length);
            kest_wire_say(&out, first ? "" : ",");
            first = false;
            kest_wire_say(&out, "{\"uri\":");
            put_uri(&out, uses[i].source->path);
            kest_wire_say(&out, ",\"range\":");
            put_range(&out, text, length, uses[i].span.offset,
                      uses[i].span.length);
            kest_wire_char(&out, '}');
        }
    }
    kest_wire_char(&out, ']');
    finish_answer(server, &out);
}

static void rename_everywhere(Server *server, const KestJson *id,
                              const KestJson *params) {
    size_t offset = position_in(server, params);
    const KestUse *wanted = offset == (size_t)-1 ? NULL
                                                 : use_at(server, offset);
    const KestJson *fresh = kest_wire_member(params, "newName");
    KestSaid out = {0};
    start_answer(&out, id);
    if (wanted == NULL || fresh == NULL || fresh->kind != KEST_JSON_TEXT ||
        server->build == NULL || server->build->program == NULL) {
        kest_wire_say(&out, "null");
        finish_answer(server, &out);
        return;
    }
    // One file at a time, which is the file the request named. A rename that
    // reaches another file is a rename this says nothing about rather than one
    // it half does.
    Open *file = here(server);
    kest_wire_say(&out, "{\"changes\":{");
    put_uri(&out, file->path);
    kest_wire_say(&out, ":[");
    bool first = true;
    uint32_t count = 0;
    const KestUse *uses = kest_program_uses(server->build->program, &count);
    for (uint32_t i = 0; i < count; i++) {
        if (!same_declaration(&uses[i], wanted) ||
            uses[i].source == NULL || file->path == NULL ||
            strcmp(uses[i].source->path, file->path) != 0) {
            continue;
        }
        kest_wire_say(&out, first ? "" : ",");
        first = false;
        kest_wire_say(&out, "{\"range\":");
        put_range(&out, file->text, file->length, uses[i].span.offset,
                  uses[i].span.length);
        kest_wire_say(&out, ",\"newText\":");
        kest_wire_escaped(&out, fresh->text, fresh->length);
        kest_wire_char(&out, '}');
    }
    // And the declaration itself, when it is in this file.
    if (wanted->declared_in != NULL && file->path != NULL &&
        strcmp(wanted->declared_in->path, file->path) == 0) {
        kest_wire_say(&out, first ? "" : ",");
        kest_wire_say(&out, "{\"range\":");
        put_range(&out, file->text, file->length, wanted->declared.offset,
                  wanted->declared.length);
        kest_wire_say(&out, ",\"newText\":");
        kest_wire_escaped(&out, fresh->text, fresh->length);
        kest_wire_char(&out, '}');
    }
    kest_wire_say(&out, "]}}");
    finish_answer(server, &out);
}

// What a file declares, and what every file in the program declares, which are
// the same walk asked of one file or of all of them.
static void symbols(Server *server, const KestJson *id, bool one_file,
                    const char *query, size_t query_length) {
    KestSaid out = {0};
    start_answer(&out, id);
    kest_wire_char(&out, '[');
    bool first = true;
    if (server->build != NULL && server->build->program != NULL) {
        KestProgram *program = server->build->program;
        for (uint32_t i = 0; i < program->global_count; i++) {
            const KestSymbol *symbol = &program->globals[i];
            if (symbol->source == NULL) {
                continue;
            }
            if (one_file && (here(server)->path == NULL ||
                             strcmp(symbol->source->path,
                                    here(server)->path) != 0)) {
                continue;
            }
            if (query_length > 0 &&
                strstr(symbol->name, query) == NULL) {
                continue;
            }
            size_t length = 0;
            const char *text = text_of(symbol->source, &length);
            kest_wire_say(&out, first ? "" : ",");
            first = false;
            kest_wire_say(&out, "{\"name\":");
            kest_wire_text(&out, symbol->name);
            // Twelve is a function and fourteen is a constant, which is what
            // the two kinds here are.
            // Twelve is a function and fourteen is a constant, which is
            // what the two kinds here are.
            kest_wire_sayf(&out, ",\"kind\":%d,\"location\":{\"uri\":",
                 symbol->type != NULL && symbol->type->tag == KEST_T_FN ? 12
                                                                       : 14);
            put_uri(&out, symbol->source->path);
            kest_wire_say(&out, ",\"range\":");
            put_range(&out, text, length, symbol->span.offset,
                      symbol->span.length);
            kest_wire_say(&out, "}}");
        }
    }
    kest_wire_char(&out, ']');
    finish_answer(server, &out);
}

static void completion(Server *server, const KestJson *id) {
    KestSaid out = {0};
    start_answer(&out, id);
    kest_wire_say(&out, "{\"isIncomplete\":false,\"items\":[");
    bool first = true;
    static const char *const WORDS[] = {
        "break", "const",  "continue", "defer",  "else",  "enum",
        "extern", "false", "fn",       "for",    "if",    "import",
        "in",    "let",    "match",    "module", "none",  "return",
        "struct", "true",  "while",    "scratch", "flags",
        "own",
    };
    for (size_t i = 0; i < sizeof(WORDS) / sizeof(WORDS[0]); i++) {
        kest_wire_say(&out, first ? "" : ",");
        first = false;
        kest_wire_say(&out, "{\"label\":");
        kest_wire_text(&out, WORDS[i]);
        kest_wire_say(&out, ",\"kind\":14}");
    }
    if (server->build != NULL && server->build->program != NULL) {
        KestProgram *program = server->build->program;
        for (uint32_t i = 0; i < program->global_count; i++) {
            const KestSymbol *symbol = &program->globals[i];
            kest_wire_say(&out, first ? "" : ",");
            first = false;
            kest_wire_say(&out, "{\"label\":");
            kest_wire_text(&out, symbol->name);
            kest_wire_sayf(&out, ",\"kind\":%d,\"detail\":",
                 symbol->type != NULL && symbol->type->tag == KEST_T_FN ? 3
                                                                       : 21);
            kest_wire_text(&out,
                     kest_type_name(server->build->arena, symbol->type));
            kest_wire_char(&out, '}');
        }
    }
    kest_wire_say(&out, "]}");
    finish_answer(server, &out);
}

// The one form, which is the formatter this tree already has: an editor asking
// for a file to be formatted gets exactly what `kest fmt` writes, because it is
// the same call.
static void formatting(Server *server, const KestJson *id) {
    KestSaid out = {0};
    start_answer(&out, id);
    // The formatter hands back the file as it should be written, in arena
    // memory, which is the same call `kest fmt` makes: an editor formatting a
    // file gets exactly what the command line would have written.
    const char *formatted = NULL;
    size_t formatted_size = 0;
    if (server->build != NULL && server->build->units.count > 0) {
        formatted = kest_format(&server->build->units.items[0].unit,
                                &server->build->units.items[0].source,
                                server->build->arena, &formatted_size);
    }
    bool worked = formatted != NULL;
    if (!worked || formatted == NULL) {
        kest_wire_say(&out, "null");
    } else {
        Open *file = here(server);
        kest_wire_say(&out, "[{\"range\":");
        put_range(&out, file->text, file->length, 0, file->length);
        kest_wire_say(&out, ",\"newText\":");
        kest_wire_escaped(&out, formatted, formatted_size);
        kest_wire_say(&out, "}]");
    }
    finish_answer(server, &out);
}

// A file the client has opened, added to the set rather than put in place of
// what was there. Opening one the set already holds replaces its text, which
// is what a client does when it reopens a file it never told this it closed.
static void opened(Server *server, const KestJson *params) {
    const KestJson *uri = kest_wire_down(params, "textDocument", "uri");
    const KestJson *text = kest_wire_down(params, "textDocument", "text");
    if (uri == NULL || uri->kind != KEST_JSON_TEXT) {
        return;
    }
    Open *file;
    if (point_at(server, uri->text, uri->length)) {
        file = here(server);
    } else {
        file = room_for_another(server);
        if (file == NULL) {
            return;
        }
        server->at = server->count - 1;
        file->uri = copy_of(uri->text, uri->length);
        file->path = path_of_uri(uri->text, uri->length);
    }
    free(file->text);
    if (text != NULL && text->kind == KEST_JSON_TEXT) {
        file->text = copy_of(text->text, text->length);
    } else {
        file->text = copy_of("", 0);
    }
    file->length = file->text == NULL ? 0 : strlen(file->text);
    build_again(server);
    publish(server);
}

static void changed(Server *server, const KestJson *params) {
    const KestJson *changes = kest_wire_member(params, "contentChanges");
    if (changes == NULL || changes->kind != KEST_JSON_LIST || changes->count == 0) {
        return;
    }
    // Whole documents, which is what this server asks for: a range change is
    // an edit this would have to apply itself, and applying an edit twice is
    // the one way a language server can be wrong about what the file says.
    const KestJson *text = kest_wire_member(changes->items[changes->count - 1], "text");
    if (text == NULL || text->kind != KEST_JSON_TEXT) {
        return;
    }
    Open *file = here(server);
    char *fresh = copy_of(text->text, text->length);
    if (fresh == NULL) {
        return;
    }
    free(file->text);
    file->text = fresh;
    file->length = text->length;
    build_again(server);
    publish(server);
}

// A file the client has closed is one this no longer has the text of, and the
// disk has it again. The build goes with it when it was of that file, because
// what it was built from has gone.
static void closed(Server *server) {
    Open *file = here(server);
    if (file == &nothing_open) {
        return;
    }
    if (server->built != NULL && file->path != NULL &&
        strcmp(server->built, file->path) == 0) {
        kest_build_free(server->build);
        server->build = NULL;
        free(server->built);
        server->built = NULL;
    }
    free(file->uri);
    free(file->path);
    free(file->text);
    *file = server->open[server->count - 1];
    server->count--;
    server->at = server->count;
}

static bool method_starts(const KestJson *method, const char *prefix) {
    size_t length = strlen(prefix);
    return method != NULL && method->kind == KEST_JSON_TEXT &&
           method->length >= length &&
           memcmp(method->text, prefix, length) == 0;
}

// Which file a message is about, out of the set the client has opened. Every
// `textDocument` method carries the uri of the one it means, and a server that
// did not read it answered about whichever file it happened to be holding.
static bool pointed_at(Server *server, const KestJson *params) {
    const KestJson *uri = kest_wire_down(params, "textDocument", "uri");
    if (uri == NULL || uri->kind != KEST_JSON_TEXT) {
        server->at = server->count;
        return false;
    }
    return point_at(server, uri->text, uri->length);
}

static bool method_is(const KestJson *method, const char *name) {
    return method != NULL && method->kind == KEST_JSON_TEXT &&
           kest_word_same(name, method->text, method->length);
}

static void handle(Server *server, const KestJson *message) {
    const KestJson *method = kest_wire_member(message, "method");
    const KestJson *id = kest_wire_member(message, "id");
    const KestJson *params = kest_wire_member(message, "params");

    if (method_is(method, "initialize")) {
        answer(server, id,
               "{\"capabilities\":{"
               "\"textDocumentSync\":1,"
               "\"hoverProvider\":true,"
               "\"definitionProvider\":true,"
               "\"referencesProvider\":true,"
               "\"documentSymbolProvider\":true,"
               "\"workspaceSymbolProvider\":true,"
               "\"renameProvider\":true,"
               "\"documentFormattingProvider\":true,"
               "\"completionProvider\":{\"triggerCharacters\":[\".\"]}"
               "},\"serverInfo\":{\"name\":\"kest\"}}");
        return;
    }
    if (method_is(method, "initialized")) {
        return;
    }
    if (method_is(method, "shutdown")) {
        server->shutting_down = true;
        answer(server, id, "null");
        return;
    }
    if (method_is(method, "textDocument/didOpen")) {
        opened(server, params);
        return;
    }
    // Every message from here on is about the file its uri names. Choosing
    // once, before the dispatch below, is what makes that true of all of them
    // at once rather than of whichever handlers remembered to ask. A uri
    // nothing was opened for is answered rather than guessed at. See D1143.
    if (method_starts(method, "textDocument/")) {
        if (!pointed_at(server, params)) {
            if (id != NULL) {
                answer(server, id, "null");
            }
            return;
        }
        // A request is answered out of a build, and the build in hand may be
        // of another file. A notification builds for itself, and building
        // twice for one keystroke is what this stays out of.
        if (id != NULL) {
            ready(server);
        }
    }
    if (method_is(method, "textDocument/didChange")) {
        changed(server, params);
        return;
    }
    if (method_is(method, "textDocument/didSave")) {
        build_again(server);
        publish(server);
        return;
    }
    if (method_is(method, "textDocument/didClose")) {
        closed(server);
        return;
    }
    if (method_is(method, "textDocument/hover")) {
        hover(server, id, params);
        return;
    }
    if (method_is(method, "textDocument/definition")) {
        definition(server, id, params);
        return;
    }
    if (method_is(method, "textDocument/references")) {
        references(server, id, params);
        return;
    }
    if (method_is(method, "textDocument/documentSymbol")) {
        symbols(server, id, true, "", 0);
        return;
    }
    if (method_is(method, "workspace/symbol")) {
        const KestJson *query = kest_wire_member(params, "query");
        symbols(server, id, false,
                query != NULL && query->kind == KEST_JSON_TEXT ? query->text : "",
                query != NULL && query->kind == KEST_JSON_TEXT ? query->length : 0);
        return;
    }
    if (method_is(method, "textDocument/rename")) {
        rename_everywhere(server, id, params);
        return;
    }
    if (method_is(method, "textDocument/completion")) {
        completion(server, id);
        return;
    }
    if (method_is(method, "textDocument/formatting")) {
        formatting(server, id);
        return;
    }
    // A request nothing here answers still gets an answer, because a client
    // waiting on one waits for ever. A notification does not, because nobody
    // is waiting.
    if (id != NULL) {
        answer(server, id, "null");
    }
}

int kest_lsp_serve(const char *library, FILE *in, FILE *out) {
    Server server = {0};
    server.arena = kest_arena_new();
    if (server.arena == NULL) {
        return 1;
    }
    server.library = library;
    server.out = out;

    while (true) {
        size_t length = 0;
        char *body = kest_wire_next(in, &length);
        if (body == NULL) {
            break;
        }

        // One arena per message, rewound after it: a message is read into a
        // tree and the tree is no use once it is answered.
        KestMark before = kest_arena_mark(server.arena);
        KestJson *message = kest_wire_read(server.arena, body, length);
        if (message != NULL) {
            handle(&server, message);
        }
        kest_arena_rewind(server.arena, before);
        free(body);

        if (server.shutting_down) {
            // `exit` follows `shutdown`, and a client that closes the stream
            // instead is the same thing said another way.
            continue;
        }
    }
    if (server.build != NULL) {
        kest_build_free(server.build);
    }
    free(server.built);
    for (size_t i = 0; i < server.count; i++) {
        free(server.open[i].uri);
        free(server.open[i].path);
        free(server.open[i].text);
    }
    free(server.open);
    kest_arena_free(server.arena);
    return 0;
}
