#include "doc.h"

#include <string.h>

#include "lexer.h"

// A comment on a line of its own, which is the only kind a reader writes about
// the next thing: one at the end of a line of code is about that line.
typedef struct {
    KestSpan span;
    uint32_t line;
    bool said;
} Remark;

typedef struct {
    const KestSource *source;
    Remark *remarks;
    uint32_t count;
} Remarks;

static uint32_t line_of(const KestSource *source, uint32_t offset) {
    uint32_t line = 0;
    uint32_t column = 0;
    kest_source_locate(source, offset, &line, &column);
    return line;
}

static bool alone_on_its_line(const KestSource *source, uint32_t offset) {
    for (uint32_t at = offset; at > 0; at--) {
        char before = source->text[at - 1];
        if (before == '\n') {
            return true;
        }
        if (before != ' ' && before != '\t') {
            return false;
        }
    }
    return true;
}

static bool remarks_read(Remarks *into, const KestSource *source,
                         KestArena *arena) {
    into->source = source;
    into->count = 0;
    uint32_t there = kest_comments(source, NULL, 0);
    if (there == 0) {
        into->remarks = NULL;
        return true;
    }
    KestSpan *spans = KEST_ARENA_ARRAY(arena, KestSpan, there);
    into->remarks = KEST_ARENA_ARRAY(arena, Remark, there);
    if (spans == NULL || into->remarks == NULL) {
        return false;
    }
    kest_comments(source, spans, there);
    for (uint32_t i = 0; i < there; i++) {
        if (alone_on_its_line(source, spans[i].offset)) {
            Remark *remark = &into->remarks[into->count++];
            remark->span = spans[i];
            remark->line = line_of(source, spans[i].offset);
            remark->said = false;
        }
    }
    return true;
}

// The run of comments ending on the line above `line`, as indexes into the
// list, or an empty run: a blank line between a comment and a declaration is
// somebody saying the comment is not about it.
static void run_above(Remarks *remarks, uint32_t line, uint32_t *first,
                      uint32_t *end) {
    *first = 0;
    *end = 0;
    uint32_t after = 0;
    while (after < remarks->count && remarks->remarks[after].line < line) {
        after++;
    }
    uint32_t start = after;
    uint32_t wanted = line;
    while (start > 0 && remarks->remarks[start - 1].line + 1 == wanted &&
           !remarks->remarks[start - 1].said) {
        start--;
        wanted = remarks->remarks[start].line;
    }
    *first = start;
    *end = after;
}

// One comment's words: what follows the two slashes, less the one space a
// comment is written with, so a line indented inside a comment keeps its
// indent.
static void remark_words(const Remarks *remarks, uint32_t at,
                         const char **words, size_t *length) {
    const Remark *remark = &remarks->remarks[at];
    const char *text = kest_span_text(remarks->source, remark->span) + 2;
    size_t left = remark->span.length >= 2 ? remark->span.length - 2 : 0;
    if (left > 0 && text[0] == ' ') {
        text++;
        left--;
    }
    while (left > 0 && (text[left - 1] == '\r' || text[left - 1] == ' ')) {
        left--;
    }
    *words = text;
    *length = left;
}

// A run of comments as one piece of text, a line each, with a blank line
// where one run ends and the next begins. NUL-terminated, in the arena.
typedef struct {
    char *bytes;
    size_t used;
    size_t room;
    KestArena *arena;
    bool failed;
} Text;

static void text_add(Text *text, const char *bytes, size_t length) {
    if (text->failed) {
        return;
    }
    if (text->used + length + 1 > text->room) {
        size_t room = text->room == 0 ? 256 : text->room;
        while (text->used + length + 1 > room) {
            room *= 2;
        }
        char *grown = kest_arena_alloc(text->arena, room, 1);
        if (grown == NULL) {
            text->failed = true;
            return;
        }
        if (text->used > 0) {
            memcpy(grown, text->bytes, text->used);
        }
        text->bytes = grown;
        text->room = room;
    }
    memcpy(text->bytes + text->used, bytes, length);
    text->used += length;
    text->bytes[text->used] = '\0';
}

static void text_remarks(Text *text, Remarks *remarks, uint32_t first,
                         uint32_t end) {
    for (uint32_t at = first; at < end; at++) {
        if (at > first) {
            bool apart = remarks->remarks[at].line >
                         remarks->remarks[at - 1].line + 1;
            text_add(text, apart ? "\n\n" : "\n", apart ? 2 : 1);
        }
        const char *words = NULL;
        size_t length = 0;
        remark_words(remarks, at, &words, &length);
        text_add(text, words, length);
        remarks->remarks[at].said = true;
    }
}

static const char *kind_word(KestDeclKind kind) {
    switch (kind) {
    case KEST_DECL_MODULE:
        return "module";
    case KEST_DECL_IMPORT:
        return "import";
    case KEST_DECL_CONST:
        return "const";
    case KEST_DECL_STRUCT:
        return "struct";
    case KEST_DECL_ENUM:
        return "enum";
    case KEST_DECL_FLAGS:
        return "flags";
    case KEST_DECL_FN:
        return "fn";
    }
    return "fn";
}

// A declaration as it is written, which is the one form: the formatter has
// already decided how it reads. A function stops where its body starts,
// because what it does is the file's business and what it takes is the
// caller's; nothing before a body can hold a brace.
static KestSpan written_part(const KestSource *source, const KestDecl *decl) {
    KestSpan span = decl->span;
    const char *text = kest_span_text(source, span);
    if (decl->kind == KEST_DECL_FN && !decl->function.is_extern) {
        const char *body = memchr(text, '{', span.length);
        if (body != NULL) {
            span.length = (uint32_t)(body - text);
        }
    }
    while (span.length > 0 && (text[span.length - 1] == ' ' ||
                               text[span.length - 1] == '\n' ||
                               text[span.length - 1] == '\r')) {
        span.length--;
    }
    return span;
}

static bool declares(const KestDecl *decl) {
    return decl->kind != KEST_DECL_MODULE && decl->kind != KEST_DECL_IMPORT;
}

static const char *piece(KestArena *arena, const char *bytes, size_t length) {
    Text text = {NULL, 0, 0, arena, false};
    text_add(&text, bytes, length);
    return text.failed ? NULL : text.bytes == NULL ? "" : text.bytes;
}

bool kest_doc(const KestUnitInfo *file, KestArena *arena, bool json,
              FILE *out) {
    const KestSource *source = &file->source;
    const KestUnit *unit = &file->unit;
    Remarks remarks;
    if (!remarks_read(&remarks, source, arena)) {
        return false;
    }
    // What is above each declaration first, so that what is left over before
    // the first of them is what the file says about itself.
    const char **abouts = KEST_ARENA_ARRAY(arena, const char *,
                                           unit->count > 0 ? unit->count : 1);
    if (abouts == NULL) {
        return false;
    }
    uint32_t first_line = 0;
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        abouts[i] = "";
        if (!declares(decl)) {
            continue;
        }
        uint32_t line = line_of(source, decl->span.offset);
        if (first_line == 0) {
            first_line = line;
        }
        uint32_t first = 0;
        uint32_t end = 0;
        run_above(&remarks, line, &first, &end);
        Text text = {NULL, 0, 0, arena, false};
        text_remarks(&text, &remarks, first, end);
        if (text.failed) {
            return false;
        }
        abouts[i] = text.bytes == NULL ? "" : text.bytes;
    }
    Text itself = {NULL, 0, 0, arena, false};
    uint32_t from = 0;
    while (from < remarks.count &&
           (first_line == 0 || remarks.remarks[from].line < first_line)) {
        uint32_t end = from;
        while (end < remarks.count && !remarks.remarks[end].said &&
               (first_line == 0 || remarks.remarks[end].line < first_line)) {
            end++;
        }
        if (end > from) {
            if (itself.used > 0) {
                text_add(&itself, "\n\n", 2);
            }
            text_remarks(&itself, &remarks, from, end);
            from = end;
        } else {
            from++;
        }
    }
    if (itself.failed) {
        return false;
    }
    const char *about = itself.bytes == NULL ? "" : itself.bytes;
    const char *called = file->module[0] != '\0' ? file->module : source->path;

    if (json) {
        fputs("{\"module\":", out);
        kest_json_text(file->module, out);
        fputs(",\"file\":", out);
        kest_json_text(source->path, out);
        fputs(",\"about\":", out);
        kest_json_text(about, out);
        fputs(",\"declarations\":[", out);
    } else {
        fprintf(out, "# `%s`\n", called);
        if (about[0] != '\0') {
            fprintf(out, "\n%s\n", about);
        }
    }
    bool listed = false;
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (!declares(decl)) {
            continue;
        }
        KestSpan written = written_part(source, decl);
        const char *text = piece(arena, kest_span_text(source, written),
                                 written.length);
        const char *name = piece(arena, kest_span_text(source, decl->name),
                                 decl->name.length);
        if (text == NULL || name == NULL) {
            return false;
        }
        const KestSpan *receiver = decl->kind == KEST_DECL_FN &&
                                           decl->function.receiver.length > 0
                                       ? &decl->function.receiver
                                       : NULL;
        if (json) {
            fputs(listed ? ",{\"kind\":" : "{\"kind\":", out);
            kest_json_text(kind_word(decl->kind), out);
            fputs(",\"name\":", out);
            if (receiver != NULL) {
                Text joined = {NULL, 0, 0, arena, false};
                text_add(&joined, kest_span_text(source, *receiver),
                         receiver->length);
                text_add(&joined, ".", 1);
                text_add(&joined, name, strlen(name));
                if (joined.failed) {
                    return false;
                }
                kest_json_text(joined.bytes, out);
            } else {
                kest_json_text(name, out);
            }
            fprintf(out, ",\"line\":%u,\"written\":",
                    line_of(source, decl->span.offset));
            kest_json_text(text, out);
            fputs(",\"about\":", out);
            kest_json_text(abouts[i], out);
            fputc('}', out);
        } else {
            if (receiver != NULL) {
                fprintf(out, "\n## `%.*s.%s`\n", (int)receiver->length,
                        kest_span_text(source, *receiver), name);
            } else {
                fprintf(out, "\n## `%s`\n", name);
            }
            fprintf(out, "\n```kest\n%s\n```\n", text);
            if (abouts[i][0] != '\0') {
                fprintf(out, "\n%s\n", abouts[i]);
            }
        }
        listed = true;
    }
    if (json) {
        fputs("]}\n", out);
    }
    return true;
}
