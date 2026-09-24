#include "diag.h"

#include <stdarg.h>
#include <string.h>

static const char *severity_name(KestSeverity severity) {
    return severity == KEST_SEVERITY_ERROR ? "error" : "warning";
}

static char *format_into(KestArena *arena, const char *format, va_list args) {
    va_list measure;
    va_copy(measure, args);
    int length = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    if (length < 0) {
        return NULL;
    }
    char *text = kest_arena_alloc(arena, (size_t)length + 1, 1);
    if (text == NULL) {
        return NULL;
    }
    vsnprintf(text, (size_t)length + 1, format, args);
    return text;
}

// Whether the character at `at` is what ends the line it is on. The second
// character of a pair does it, so a pair ends one line and not two.
static bool ends_a_line(const char *text, size_t length, size_t at) {
    if (text[at] == '\n') {
        return true;
    }
    return text[at] == '\r' && (at + 1 == length || text[at + 1] != '\n');
}

uint64_t kest_mark_bytes(uint64_t mark, const void *bytes, size_t length) {
    const unsigned char *at = bytes;
    for (size_t i = 0; i < length; i++) {
        mark ^= at[i];
        mark *= 0x100000001b3ULL;
    }
    return mark;
}

uint64_t kest_mark_number(uint64_t mark, uint64_t value, unsigned bytes) {
    // Low byte first, so that the number a fold answers with is about what was
    // folded rather than about the machine that folded it. See D660.
    for (unsigned at = 0; at < bytes; at++) {
        unsigned char byte = (unsigned char)((value >> (at * 8)) & 0xffU);
        mark = kest_mark_bytes(mark, &byte, 1);
    }
    return mark;
}

bool kest_source_init(KestSource *source, KestArena *arena, const char *path,
                      const char *text, size_t length) {
    source->path = path;
    source->text = text;
    source->length = length;

    // FNV-1a over the bytes, the same way `hash` over text is written in the
    // machine: text is its bytes, and so is a file. Taken here because this
    // already walks the file once for its lines, so it costs the walk it was
    // going to make anyway. See D658.
    source->mark = kest_mark_bytes(KEST_MARK_START, text, length);

    // A line ends at a line feed, and at a carriage return that has no line
    // feed after it: a file written where lines end with two characters ends
    // each of them once, and one written where they end with the return alone
    // has lines at all. The lexer reads a return as space either way; this is
    // about where a message points, which is a thing a reader has to be able
    // to find.
    uint32_t lines = 1;
    for (size_t i = 0; i < length; i++) {
        if (ends_a_line(text, length, i)) {
            lines++;
        }
    }

    source->line_offsets = KEST_ARENA_ARRAY(arena, uint32_t, lines);
    if (source->line_offsets == NULL) {
        return false;
    }
    source->line_count = lines;

    uint32_t line = 0;
    source->line_offsets[line++] = 0;
    for (size_t i = 0; i < length; i++) {
        if (ends_a_line(text, length, i) && line < lines) {
            source->line_offsets[line++] = (uint32_t)i + 1;
        }
    }
    return true;
}

// The longest name this measures. A name longer than this is not near
// anything, which is a suggestion nobody gets rather than a wrong one.
#define FAR_ENOUGH 256

// Two letters the wrong way round is one mistake and not two. It is the way a
// word is mistyped most often, and a name of four letters is allowed one
// mistake, so counting a swap as two is the difference between a suggestion
// and none: `psuh` was near nothing, and `push` was right there. Which is why
// the row before last is kept as well.
uint32_t kest_word_distance(const char *a, size_t a_len, const char *b,
                            size_t b_len, uint32_t limit) {
    if (a_len > b_len + limit || b_len > a_len + limit) {
        return limit + 1;
    }

    // Three rows of a table, kept where a caller need not think about them.
    // What decides the width is what a name can be: names are compared
    // qualified — `examples.game.npc.Npc` against what somebody wrote — so
    // sixty-four letters was a ceiling a real name could reach, and a name
    // past it was near nothing without a word about why. Two hundred and
    // fifty-six is past anything a reader would write down twice, and three
    // rows of it is three kilobytes of a stack nothing else is using. Past
    // that, a name is answered for with nothing at all. See D300.
    uint32_t before[FAR_ENOUGH] = {0};
    uint32_t previous[FAR_ENOUGH];
    uint32_t current[FAR_ENOUGH];
    if (b_len >= FAR_ENOUGH) {
        return limit + 1;
    }

    for (size_t j = 0; j <= b_len; j++) {
        previous[j] = (uint32_t)j;
    }
    for (size_t i = 1; i <= a_len; i++) {
        current[0] = (uint32_t)i;
        uint32_t best = current[0];
        for (size_t j = 1; j <= b_len; j++) {
            uint32_t substitute = previous[j - 1] + (a[i - 1] != b[j - 1]);
            uint32_t remove = previous[j] + 1;
            uint32_t insert = current[j - 1] + 1;
            uint32_t least = substitute < remove ? substitute : remove;
            least = least < insert ? least : insert;
            // The two before this one, each standing where the other is.
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] &&
                a[i - 2] == b[j - 1]) {
                uint32_t swapped = before[j - 2] + 1;
                least = least < swapped ? least : swapped;
            }
            current[j] = least;
            if (least < best) {
                best = least;
            }
        }
        if (best > limit) {
            return limit + 1;
        }
        memcpy(before, previous, sizeof(uint32_t) * (b_len + 1));
        memcpy(previous, current, sizeof(uint32_t) * (b_len + 1));
    }
    return previous[b_len];
}

bool kest_word_same(const char *word, const char *bytes, size_t length) {
    return strlen(word) == length && memcmp(word, bytes, length) == 0;
}

const char *kest_span_text(const KestSource *source, KestSpan span) {
    return source->text + span.offset;
}

void kest_source_locate(const KestSource *source, uint32_t offset,
                        uint32_t *line, uint32_t *column) {
    uint32_t low = 0;
    uint32_t high = source->line_count - 1;
    while (low < high) {
        uint32_t middle = (low + high + 1) / 2;
        if (source->line_offsets[middle] <= offset) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }

    // Continuation bytes belong to the character their lead byte started, so
    // skipping them counts glyphs rather than bytes.
    uint32_t characters = 0;
    for (uint32_t i = source->line_offsets[low]; i < offset; i++) {
        if ((source->text[i] & 0xc0) != 0x80) {
            characters++;
        }
    }

    *line = low + 1;
    *column = characters + 1;
}

void kest_diags_init(KestDiags *diags, KestArena *arena) {
    diags->arena = arena;
    diags->items = NULL;
    diags->count = 0;
    diags->capacity = 0;
    diags->error_count = 0;
    diags->source = NULL;
    diags->muted = false;
    diags->starved = false;
    // Kept, counted, and what was held back: a run of diagnostics keeps
    // everything until somebody says otherwise, which is what compiling does
    // and what a machine says otherwise about. See D618.
    diags->most = 0;
    diags->not_said = 0;
    diags->held_back = false;
    diags->last_code[0] = '\0';
    diags->last_words[0] = '\0';
}


// What a run was trying to say when it found it had nowhere to say it. Kept in
// the list itself, which is the one place a run with no room left still has:
// the first one to be lost is the one kept, because what stopped a run is the
// first thing it could not say and everything after it is a consequence. See
// D848.
// What the machine was about to say when it found it had nowhere to say it. It
// is worth keeping when there was still room to work it out in and worth
// nothing when there was not: a name nobody declared, where the table the name
// would have been in could not be grown, is the compiler's afternoon written
// as the program's mistake. Which of the two it was is what the caller saw
// before it asked for room to keep this. See D880.
static void keep_the_last_words(KestDiags *diags, const char *code,
                                const char *format, va_list args,
                                bool after_the_room) {
    if (diags == NULL || diags->last_code[0] != '\0' || after_the_room) {
        return;
    }
    snprintf(diags->last_code, sizeof diags->last_code, "%s", code);
    vsnprintf(diags->last_words, sizeof diags->last_words, format, args);
}

void kest_diags_starve(KestDiags *diags) {
    if (diags == NULL || diags->starved) {
        return;
    }
    diags->starved = true;
    diags->error_count++;
}

void kest_diags_fault(KestDiags *diags, const char *why) {
    kest_diags_suggest(diags, "%s, which is a fault in the compiler", why);
}

void kest_diags_disagree(KestDiags *diags, KestSpan span, const char *what,
                         ...) {
    char said[256];
    va_list args;
    va_start(args, what);
    vsnprintf(said, sizeof said, what, args);
    va_end(args);
    kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0505", span,
                   "%s, which the checker allowed", said);
    kest_diags_fault(diags, "the two halves of the compiler disagree about "
                            "what a program is");
}

void kest_diags_in(KestDiags *diags, const KestSource *source) {
    diags->source = source;
}

void kest_diags_mute(KestDiags *diags, bool muted) {
    diags->muted = muted;
}

static bool diags_reserve(KestDiags *diags) {
    if (diags->count < diags->capacity) {
        return true;
    }
    uint32_t capacity =
        diags->capacity == 0 ? KEST_MOST_UNREAD : diags->capacity * 2;
    KestDiag *items = KEST_ARENA_ARRAY(diags->arena, KestDiag, capacity);
    if (items == NULL) {
        return false;
    }
    if (diags->count > 0) {
        memcpy(items, diags->items, sizeof(KestDiag) * diags->count);
    }
    diags->items = items;
    diags->capacity = capacity;
    return true;
}

// One place a diagnostic is recorded, so the two ways of formatting its
// message meet before anything is written down.
// Whether there is room to keep another. A machine holds what nobody has asked
// for up to the size the list is made at and counts the rest: it does not end,
// so a program refused every frame would otherwise hand a host that never asks
// a frame's words for as long as it ran. Nought is no limit, which is what
// compiling has. See D618.
static bool room_to_keep(KestDiags *diags) {
    if (diags->most != 0 && diags->count >= diags->most) {
        diags->not_said++;
        diags->held_back = true;
        return false;
    }
    return true;
}

static void add_formatted(KestDiags *diags, KestSeverity severity,
                          const char *code, KestSpan span,
                          const char *message, bool after_the_room) {
    diags->held_back = false;
    KestDiag *diag = &diags->items[diags->count++];
    diag->severity = severity;
    diag->code = code;
    diag->message = message;
    diag->suggestion = NULL;
    diag->span = span;
    diag->source = diags->source;
    diag->note_count = 0;
    diag->left_out = 0;
    diag->after_the_room = after_the_room;

    if (severity == KEST_SEVERITY_ERROR) {
        diags->error_count++;
    }
}

// The message is formatted into the arena and is as long as it is. A caller
// that wrote it into a buffer of its own first would cut a message off in the
// middle of a name, which is what every one of them used to do.
void kest_diags_addv(KestDiags *diags, KestSeverity severity,
                     const char *code, KestSpan span, const char *format,
                     va_list args) {
    if (diags->muted) {
        return;
    }
    // Asked before anything here takes room of its own, so that what it says
    // is whether the room had gone before this was worked out rather than
    // whether keeping it is what finished the room off.
    bool after_the_room = kest_arena_refused_anywhere();
    if (!room_to_keep(diags)) {
        return;
    }
    if (!diags_reserve(diags)) {
        keep_the_last_words(diags, code, format, args, after_the_room);
        kest_diags_starve(diags);
        return;
    }
    va_list again;
    va_copy(again, args);
    char *message = format_into(diags->arena, format, args);
    if (message == NULL) {
        keep_the_last_words(diags, code, format, again, after_the_room);
        va_end(again);
        kest_diags_starve(diags);
        return;
    }
    va_end(again);
    add_formatted(diags, severity, code, span, message, after_the_room);
}

void kest_diags_add(KestDiags *diags, KestSeverity severity, const char *code,
                    KestSpan span, const char *format, ...) {
    if (diags->muted) {
        return;
    }
    bool after_the_room = kest_arena_refused_anywhere();
    if (!room_to_keep(diags)) {
        return;
    }
    va_list args;
    if (!diags_reserve(diags)) {
        va_start(args, format);
        keep_the_last_words(diags, code, format, args, after_the_room);
        va_end(args);
        kest_diags_starve(diags);
        return;
    }

    va_start(args, format);
    char *message = format_into(diags->arena, format, args);
    va_end(args);
    if (message == NULL) {
        va_start(args, format);
        keep_the_last_words(diags, code, format, args, after_the_room);
        va_end(args);
        kest_diags_starve(diags);
        return;
    }
    add_formatted(diags, severity, code, span, message, after_the_room);
}

void kest_diags_suggestv(KestDiags *diags, const char *format, va_list args) {
    if (diags->muted || diags->held_back || diags->count == 0) {
        return;
    }
    diags->items[diags->count - 1].suggestion =
        format_into(diags->arena, format, args);
}

void kest_diags_suggest(KestDiags *diags, const char *format, ...) {
    if (diags->muted || diags->held_back || diags->count == 0) {
        return;
    }

    va_list args;
    va_start(args, format);
    char *text = format_into(diags->arena, format, args);
    va_end(args);

    diags->items[diags->count - 1].suggestion = text;
}

// One place both of these write a note, because a note about the last
// diagnostic and a note about one further back are the same thing said to a
// different item.
static void note_on(KestDiags *diags, KestDiag *diag, const KestSource *source,
                    KestSpan span, const char *format, va_list args, bool kept) {
    bool full = diag->note_count == KEST_MOST_PLACES;
    if (full && !kept) {
        // Counted rather than dropped. What a caller does about it is the
        // caller's — several of them keep room for a note that says what is
        // under it — and what happens to one that does not is this.
        diag->left_out++;
        return;
    }
    char *label = format_into(diags->arena, format, args);
    if (label == NULL) {
        return;
    }
    // A note that frames the whole diagnostic takes the last place rather than
    // being the one thing left out. Which copy of a generic a body's sentences
    // are about is not the ninth thing a reader wants: it is what the other
    // eight are about, and it used to go last and be dropped first. What is
    // counted instead is the candidate it displaced. See D762.
    KestNote *note = full ? &diag->notes[KEST_MOST_PLACES - 1]
                          : &diag->notes[diag->note_count++];
    if (full) {
        diag->left_out++;
    }
    note->span = span;
    note->source = source == NULL ? diags->source : source;
    note->label = label;
}

void kest_diags_note(KestDiags *diags, const KestSource *source, KestSpan span,
                     const char *format, ...) {
    if (diags->muted || diags->held_back || diags->count == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    note_on(diags, &diags->items[diags->count - 1], source, span, format,
            args, false);
    va_end(args);
}

void kest_diags_note_at(KestDiags *diags, uint32_t which,
                        const KestSource *source, KestSpan span,
                        const char *format, ...) {
    if (diags->muted || which >= diags->count) {
        return;
    }
    va_list args;
    va_start(args, format);
    // Kept, because a note put on a diagnostic that is already finished is a
    // note about the whole of it rather than one more place in it: nothing
    // reaches back to a diagnostic to add a ninth candidate. See D762.
    note_on(diags, &diags->items[which], source, span, format, args, true);
    va_end(args);
}

// The words of one, written again where they are being put. What is absorbed
// is a machine's, and what a machine says is written in the room it owns and
// handed back when the machine goes (D617): a diagnostic carried over as it
// stands is a sentence pointing into memory that went with the machine it came
// from. The code is this compiler's own and is never in that room.
static const char *said_again(KestArena *arena, const char *words) {
    if (words == NULL) {
        return NULL;
    }
    const char *copy = kest_arena_strndup(arena, words, strlen(words));
    return copy == NULL ? words : copy;
}

void kest_diags_absorb(KestDiags *into, const KestDiags *from) {
    if (from->starved) {
        kest_diags_starve(into);
    }
    // What it did not keep goes too: a count of what nobody will ever read is
    // still the news that there was more of it. See D618.
    into->not_said += from->not_said;
    for (uint32_t i = 0; i < from->count; i++) {
        if (!diags_reserve(into)) {
            kest_diags_starve(into);
            return;
        }
        KestDiag carried = from->items[i];
        carried.message = said_again(into->arena, carried.message);
        carried.suggestion = said_again(into->arena, carried.suggestion);
        for (uint8_t n = 0; n < carried.note_count; n++) {
            carried.notes[n].label =
                said_again(into->arena, carried.notes[n].label);
        }
        into->items[into->count++] = carried;
        if (carried.severity == KEST_SEVERITY_ERROR) {
            into->error_count++;
        }
    }
}

void kest_diags_sort(KestDiags *diags) {
    // Insertion sort: a run holds tens of diagnostics, and keeping equal
    // offsets in the order they were reported keeps a cause ahead of its
    // consequence.
    for (uint32_t i = 1; i < diags->count; i++) {
        KestDiag moving = diags->items[i];
        uint32_t j = i;
        // Within a file, by position. Between files, the order they were read
        // in, which is the order the imports were followed.
        while (j > 0 && diags->items[j - 1].source != NULL &&
               diags->items[j - 1].source == moving.source &&
               diags->items[j - 1].span.offset > moving.span.offset) {
            diags->items[j] = diags->items[j - 1];
            j--;
        }
        diags->items[j] = moving;
    }
}

// How much of a source line is shown. The formatter writes to eighty columns,
// so a line past this came from a file it could not read, or from a machine,
// which can put a whole program on one of them. What the reader is being shown
// is the span, so the line around it is what is kept.
#define SHOWN_COLUMNS 100
#define LEADING_COLUMNS 20
#define CUT_MARK "..."

typedef struct {
    uint32_t start;
    uint32_t end;
    bool cut_before;
    bool cut_after;
} Shown;

static Shown shown_part(const KestSource *source, uint32_t line, KestSpan span) {
    uint32_t start = source->line_offsets[line - 1];
    uint32_t end = line < source->line_count ? source->line_offsets[line]
                                             : (uint32_t)source->length;
    while (end > start && (source->text[end - 1] == '\n' ||
                           source->text[end - 1] == '\r')) {
        end--;
    }

    Shown shown = {start, end, false, false};
    if (end - start <= SHOWN_COLUMNS) {
        return shown;
    }

    uint32_t at = span.offset < start ? start : span.offset;
    if (at > end) {
        at = end;
    }
    uint32_t from = at - start > LEADING_COLUMNS ? at - LEADING_COLUMNS : start;
    if (from + SHOWN_COLUMNS > end) {
        from = end - SHOWN_COLUMNS;
    }
    shown.start = from;
    shown.end = from + SHOWN_COLUMNS;
    shown.cut_before = from > start;
    shown.cut_after = shown.end < end;
    return shown;
}

// A tab is shown as the spaces it stands for. A caret cannot be put under a
// tab: the caret line would have to guess what the terminal does with one, and
// be wrong wherever it guessed differently. Four is what this language is
// written with, and the guess is only about how wide the line looks, not about
// where the caret lands, because both lines are built the same way.
#define TAB_COLUMNS 4

// The column reached after showing the bytes between `from` and `to`, having
// written them. With nowhere to write to, it measures and writes nothing.
static uint32_t put_expanded(const KestSource *source, uint32_t from,
                             uint32_t to, uint32_t column, FILE *out) {
    for (uint32_t i = from; i < to; i++) {
        if (source->text[i] != '\t') {
            if (out != NULL) {
                fputc(source->text[i], out);
            }
            // A continuation byte is the rest of the character before it and
            // is shown where that one is, so it is not a column of its own.
            // This is what `kest_source_locate` counts, which is why the
            // number beside the path and the caret under the line agree.
            if ((source->text[i] & 0xc0) != 0x80) {
                column++;
            }
            continue;
        }
        uint32_t width = TAB_COLUMNS - column % TAB_COLUMNS;
        for (uint32_t n = 0; n < width && out != NULL; n++) {
            fputc(' ', out);
        }
        column += width;
    }
    return column;
}

static int line_width(const KestSource *source, KestSpan span) {
    uint32_t line = 0;
    uint32_t column = 0;
    kest_source_locate(source, span.offset, &line, &column);
    return snprintf(NULL, 0, "%u", line);
}

// The location, the source line and a caret under the span, with whatever is
// being said about it beside the caret.
static void render_frame(const KestSource *source, KestSpan span,
                         const char *label, int gutter, FILE *out) {
    uint32_t line = 0;
    uint32_t column = 0;
    kest_source_locate(source, span.offset, &line, &column);

    fprintf(out, "%*s--> %s:%u:%u\n", gutter, "", source->path, line, column);
    fprintf(out, "%*s|\n", gutter + 1, "");
    Shown shown = shown_part(source, line, span);
    fprintf(out, "%*u | ", gutter, line);
    uint32_t indent = shown.cut_before ? (uint32_t)strlen(CUT_MARK) : 0;
    if (shown.cut_before) {
        fputs(CUT_MARK, out);
    }
    put_expanded(source, shown.start, shown.end, indent, out);
    if (shown.cut_after) {
        fputs(CUT_MARK, out);
    }

    uint32_t at = span.offset < shown.start ? shown.start : span.offset;
    indent = put_expanded(source, shown.start, at, indent, NULL);
    fprintf(out, "\n%*s| %*s", gutter + 1, "", (int)indent, "");

    uint32_t width = span.length == 0 ? 1 : span.length;
    // A caret stops where the line does. A span over more than one line used
    // to caret to where it ended, which draws forty characters of `^` under a
    // line fourteen long and points at nothing for the other twenty-six: what
    // is on the next line is on the next line. Where the line itself was cut
    // it stops at the cut, because the mark after it already says there is
    // more. See D539.
    if (at + width > shown.end) {
        width = shown.end - at;
    }
    // A span with a tab in it is as wide as the tab was shown, so the carets
    // end where the span does. An empty span has no bytes to measure and is
    // one caret wherever it is.
    if (span.length != 0) {
        width = put_expanded(source, at, at + width, indent, NULL) - indent;
    }
    for (uint32_t caret = 0; caret < width; caret++) {
        fputc('^', out);
    }
    if (label != NULL) {
        fprintf(out, " %s", label);
    }
    fputc('\n', out);
}

// Which of the two ran out, and what to say about it. Running out is one bit
// on a list of diagnostics, because recording it is the one thing a run with
// no memory can do; which of the two it was is not recorded at all, because
// the arena the list is kept in is the arena that ran out and it remembers.
// See D843.
static const char *starved_code(const KestDiags *diags) {
    return kest_arena_refused_by_ceiling(diags->arena) ? KEST_CRAMPED_CODE
                                                       : KEST_STARVED_CODE;
}

// And the sentence, which carries numbers where there is a ceiling: what was
// taken of what was allowed, and what the allocation that crossed it wanted.
// A run that missed by eight bytes and one that missed by a gigabyte are the
// same sentence otherwise, and they are not the same thing to do about it.
// Written into the caller's room because there is none here to make it in.
static const char *starved_says(const KestDiags *diags, char *room,
                                size_t space) {
    if (!kest_arena_refused_by_ceiling(diags->arena)) {
        return KEST_STARVED_SAYS;
    }
    size_t taken = kest_arena_used(diags->arena);
    size_t given = kest_arena_ceiling(diags->arena);
    // Reading a file happens in an arena of its own and is charged back in one
    // lump when it is done, so the charge that finishes a build can land above
    // the ceiling that refused it. `4816 of the 4000` is two true numbers
    // reading as a mistake in the compiler, so what is said instead is that
    // there is none left, which is what both numbers mean.
    if (taken >= given) {
        snprintf(room, space,
                 "this has taken all %zu bytes it was given, and wanted %zu "
                 "more",
                 given, kest_arena_refused(diags->arena));
        return room;
    }
    snprintf(room, space, KEST_CRAMPED_SAYS, taken, given,
             kest_arena_refused(diags->arena));
    return room;
}

// What is worth saying of what was said. A run that starved says what it worked
// out while it still had room and nothing after: the rest is a guess at what
// the program would have said next and it reads like an answer. A run that did
// not starve says all of it, because an allocation refused and coped with is
// not a mistake in anybody's program. See D880.
static bool worth_saying(const KestDiags *diags, const KestDiag *one) {
    return !diags->starved || !one->after_the_room;
}

void kest_diags_render(const KestDiags *diags, FILE *out) {
    // What a program printed before this happened goes first. The two streams
    // are kept apart on purpose — what a program says is an answer and what
    // went wrong is not — and a shell that puts both in one pipe reads a
    // buffer that empties when the run ends, so the failure would arrive
    // before the lines that led to it. Which is a lie about the order things
    // happened in, told by the machine that watched them happen. See D308.
    if (out != stdout) {
        fflush(stdout);
    }
    for (uint32_t i = 0; i < diags->count; i++) {
        const KestDiag *diag = &diags->items[i];
        if (!worth_saying(diags, diag)) {
            continue;
        }
        const KestSource *source = diag->source;

        fprintf(out, "%s[%s]: %s\n", severity_name(diag->severity), diag->code,
                diag->message);

        // One gutter for every frame of one diagnostic, so the source lines
        // line up with each other rather than each with itself.
        bool framed = source != NULL && diag->span.length != 0;
        int gutter = framed ? line_width(source, diag->span) : 0;
        for (uint8_t n = 0; n < diag->note_count; n++) {
            if (diag->notes[n].source == NULL) {
                continue;
            }
            int width = line_width(diag->notes[n].source, diag->notes[n].span);
            if (width > gutter) {
                gutter = width;
            }
        }

        if (framed) {
            render_frame(source, diag->span, diag->suggestion, gutter, out);
        } else {
            // A diagnostic about the program rather than about a file has
            // nowhere of its own to point at, and inventing somewhere would be
            // worse. Its notes have their own places and are the whole of what
            // it has to show.
            if (source != NULL) {
                fprintf(out, "  --> %s\n", source->path);
            }
            if (diag->suggestion != NULL) {
                fprintf(out, "      %s\n", diag->suggestion);
            }
        }
        for (uint8_t n = 0; n < diag->note_count; n++) {
            if (diag->notes[n].source == NULL) {
                continue;
            }
            render_frame(diag->notes[n].source, diag->notes[n].span,
                         diag->notes[n].label, gutter, out);
        }
        if (diag->left_out > 0) {
            fprintf(out, "%*sand %u more place%s\n", gutter + 1, "",
                    diag->left_out, diag->left_out == 1 ? "" : "s");
        }
        fputc('\n', out);
    }
    // Last, because it is about the ones above it: what a run says when it ran
    // out is what it managed to say, and then that there was more. When there
    // is nothing above it, it is the whole of what happened.
    if (diags->starved) {
        // What it was about to say, before what it says about having had
        // nowhere to say it. A reader wants the program's problem first and
        // this machine's second, and the second without the first is a reader
        // sent to buy memory for a program that was over its own ceiling.
        if (diags->last_code[0] != '\0') {
            kest_diags_say_one(out, false, diags->last_code, diags->last_words);
        }
        char room[160];
        kest_diags_say_one(out, false, starved_code(diags),
                           starved_says(diags, room, sizeof room));
    }
    // And what was not kept, for the same reason and in the same place: a
    // machine holds so much of what nobody has asked for and counts the rest,
    // and a list that stopped where a reader would take it for the end would
    // be the one thing a report must not be. See D618 and D200.
    if (diags->not_said > 0) {
        fprintf(out, "and %u more since, which this machine did not keep\n\n",
                diags->not_said);
    }
}

void kest_json_text(const char *text, FILE *out) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        switch (*p) {
        case '"':
            fputs("\\\"", out);
            break;
        case '\\':
            fputs("\\\\", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (*p < 0x20) {
                fprintf(out, "\\u%04x", *p);
            } else {
                fputc(*p, out);
            }
        }
    }
    fputc('"', out);
}

void kest_diags_say_one(FILE *out, bool as_json, const char *code,
                        const char *message) {
    if (out == NULL) {
        return;
    }
    if (out != stdout) {
        fflush(stdout);
    }
    if (!as_json) {
        fprintf(out, "%s[%s]: %s\n", severity_name(KEST_SEVERITY_ERROR), code,
                message);
        return;
    }
    fprintf(out,
            "{\"schema\":%d,\"diagnostics\":[{\"severity\":\"%s\""
            ",\"code\":\"%s\"",
            KEST_JSON_SCHEMA, severity_name(KEST_SEVERITY_ERROR), code);
    fputs(",\"message\":", out);
    kest_json_text(message, out);
    fputs("}],\"errors\":1}\n", out);
}

void kest_diags_render_json(const KestDiags *diags, FILE *out) {
    fputc('{', out);
    kest_diags_write_json(diags, out);
    fputs("}\n", out);
}

void kest_diags_write_json(const KestDiags *diags, FILE *out) {
    // First, and in every object every command writes, because this is the one
    // field a tool reads before it knows what the rest of them mean. It is
    // written here rather than at each command because every one of those goes
    // through this door: a command that wrote its own would be a command that
    // could forget. See D947.
    fprintf(out, "\"schema\":%d,", KEST_JSON_SCHEMA);
    fputs("\"diagnostics\":[", out);
    uint32_t said = 0;
    for (uint32_t i = 0; i < diags->count; i++) {
        const KestDiag *diag = &diags->items[i];
        if (!worth_saying(diags, diag)) {
            continue;
        }
        const KestSource *source = diag->source;

        if (said++ > 0) {
            fputc(',', out);
        }
        fprintf(out, "{\"severity\":\"%s\",\"code\":\"%s\"",
                severity_name(diag->severity), diag->code);
        if (source != NULL) {
            fputs(",\"file\":", out);
            kest_json_text(source->path, out);
            // A span with nothing in it is a diagnostic about the whole file,
            // which the words show as a path and no line. Saying `1:1` here
            // would be a place nobody chose, and a tool would draw it.
            if (diag->span.length > 0) {
                uint32_t line = 0;
                uint32_t column = 0;
                kest_source_locate(source, diag->span.offset, &line, &column);
                fprintf(out,
                        ",\"line\":%u,\"column\":%u,\"offset\":%u,"
                        "\"length\":%u",
                        line, column, diag->span.offset, diag->span.length);
            }
        }
        fputs(",\"message\":", out);
        kest_json_text(diag->message, out);
        if (diag->suggestion != NULL) {
            fputs(",\"suggestion\":", out);
            kest_json_text(diag->suggestion, out);
        }
        if (diag->note_count > 0) {
            fputs(",\"notes\":[", out);
            for (uint8_t n = 0; n < diag->note_count; n++) {
                const KestNote *note = &diag->notes[n];
                fprintf(out, "%s{", n > 0 ? "," : "");
                if (note->source != NULL) {
                    uint32_t note_line = 0;
                    uint32_t note_column = 0;
                    kest_source_locate(note->source, note->span.offset,
                                       &note_line, &note_column);
                    fputs("\"file\":", out);
                    kest_json_text(note->source->path, out);
                    fprintf(out, ",\"line\":%u,\"column\":%u,", note_line,
                            note_column);
                }
                fputs("\"message\":", out);
                kest_json_text(note->label, out);
                fputc('}', out);
            }
            fputc(']', out);
        }
        if (diag->left_out > 0) {
            fprintf(out, ",\"leftOut\":%u", diag->left_out);
        }
        fputc('}', out);
    }
    if (diags->starved) {
        // Written out here rather than made and put in the list, because
        // making one is what there was no room for. Two of them where there
        // are two things to say: what it was about to say, and that it had
        // nowhere to say it. See D848.
        char room[160];
        if (diags->last_code[0] != '\0') {
            fprintf(out,
                    "%s{\"severity\":\"error\",\"code\":\"%s\",\"message\":",
                    diags->count > 0 ? "," : "", diags->last_code);
            kest_json_text(diags->last_words, out);
            fputc('}', out);
            fputc(',', out);
        } else {
            fputs(diags->count > 0 ? "," : "", out);
        }
        fprintf(out, "{\"severity\":\"error\",\"code\":\"%s\",\"message\":",
                starved_code(diags));
        kest_json_text(starved_says(diags, room, sizeof room), out);
        fputc('}', out);
    }
    fprintf(out, "],\"errors\":%u", diags->error_count);
    if (diags->not_said > 0) {
        fprintf(out, ",\"notKept\":%u", diags->not_said);
    }
}
