#ifndef KEST_DIAG_H
#define KEST_DIAG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "kest.h"
#include "mem.h"

// A half-open byte range in a source file. A zero length means the diagnostic
// is about the file rather than about a place in it, and it renders without a
// source line or a caret.
typedef struct {
    uint32_t offset;
    uint32_t length;
} KestSpan;

typedef enum {
    KEST_SEVERITY_ERROR,
    KEST_SEVERITY_WARNING,
} KestSeverity;

typedef struct KestSource KestSource;

// A second place the diagnostic is about. A duplicate is about two
// declarations and a broken promise is about every call between the promise
// and the body that breaks it, and prose naming a line number is a worse way
// to say either.
typedef struct {
    KestSpan span;
    const KestSource *source;
    const char *label;
} KestNote;

typedef struct {
    KestSeverity severity;
    const char *code;
    const char *message;
    // The fix, when one is knowable. Rendered beside the caret.
    const char *suggestion;
    KestSpan span;
    // Which file the span is in. A program is more than one file, so a span
    // on its own does not say where it is.
    const KestSource *source;
    // Whether the room had already run out when this was worked out. A
    // compiler with none left goes on and says what a half-built program
    // suggests: a name nobody declared where a table could not be grown, a
    // type that cannot be told where a copy could not be made. Which of the
    // two it is cannot be known when it is said — an arena that refuses once
    // may be coped with and the build finish, and then nothing was wrong — so
    // it is marked here and read at the end, where whether the run starved is
    // known. See D880.
    bool after_the_room;
    KestNote notes[KEST_MOST_PLACES];
    uint8_t note_count;
    // Places there was no room to show. A diagnostic with more than
    // `KEST_MOST_PLACES` of them says how many it left out rather than stopping
    // where a reader would take it for the end of the list.
    uint32_t left_out;
} KestDiag;

// A source file, with its line offsets precomputed so a byte offset can be
// turned into a line and column without rescanning.
struct KestSource {
    const char *path;
    const char *text;
    size_t length;
    // A number that moves when the bytes move: FNV-1a over them, which is what
    // the language hashes text with. What it is for is a host asking whether
    // this is the same file it read before — a size is not an answer to that,
    // because two edits that keep the length look the same. See D658.
    uint64_t mark;
    uint32_t *line_offsets;
    uint32_t line_count;
};

// A collected run of diagnostics. Compilation never stops at the first error,
// so this holds everything one pass found.
typedef struct {
    KestArena *arena;
    KestDiag *items;
    uint32_t count;
    uint32_t capacity;
    uint32_t error_count;
    // Where the spans handed to kest_diags_add are, until it is set again.
    // Every stage works on one file at a time, so this is set once per file
    // rather than passed through every call that might report.
    const KestSource *source;
    bool muted;
    // Whether something could not be said for want of memory. A diagnostic is
    // written into the arena, so a run with none of it left records nothing,
    // says nothing, and answers with a count of nought — which every caller
    // reads as nothing having gone wrong. One bit is what a run can still
    // record when it can record nothing else. See D319.
    bool starved;
    // The most a machine keeps of what nobody has asked for, and how many it
    // did not keep. Nought is everything, which is what compiling does: a
    // program with five hundred things wrong with it has five hundred things
    // wrong with it, and the run that found them ends. A machine does not end
    // — a program refused every frame is refused every frame — so what it
    // holds for a host that never asks is capped at the size the list is made
    // at, and what it did not keep is counted and said. See D618.
    uint32_t most;
    uint32_t not_said;
    // Whether the last thing offered was not kept. A suggestion and a note go
    // on the last one recorded, and one offered after a refusal that was not
    // kept would go on somebody else's. See D618.
    bool held_back;
    // And what it was trying to say when it ran out, written here rather than
    // in the arena it ran out of. A run stopped by a ceiling used to answer
    // `there was not enough memory to finish` — true of the message it could
    // not write, and not what happened to the program. The code and the words
    // are the ones the stage was about to say, cut where they stop fitting,
    // because a sentence half said is more than no sentence at all. Empty
    // until something is lost. See D848.
    char last_code[8];
    char last_words[192];
} KestDiags;



// FNV-1a, which is the one arithmetic every mark in this compiler is made of:
// a file's own, a program's, and what the machine does for `hash` over text.
// Written once rather than four times, because four copies of one arithmetic
// are four numbers that agree until somebody changes one of them.
//
// `KEST_MARK_START` is where a fold begins, `kest_mark_bytes` folds a run of
// them and `kest_mark_number` folds a number low byte first, whatever order
// this machine keeps its bytes in. See D663.
#define KEST_MARK_START 0xcbf29ce484222325ULL

uint64_t kest_mark_bytes(uint64_t mark, const void *bytes, size_t length);
uint64_t kest_mark_number(uint64_t mark, uint64_t value, unsigned bytes);

bool kest_source_init(KestSource *source, KestArena *arena, const char *path,
                      const char *text, size_t length);

// Line and column are one-based. Column counts characters rather than bytes,
// so a caret lands under the right glyph in a UTF-8 identifier.
// How many mistakes apart two words are, counting a swap of two letters as
// one, and giving up as soon as they are further apart than `limit`. Every
// suggestion in this compiler is measured with it, from the parser to the
// machine, which is why it lives here with the diagnostics rather than beside
// one of them.
uint32_t kest_word_distance(const char *a, size_t a_len, const char *b,
                            size_t b_len, uint32_t limit);

// Whether a name this compiler holds is the same word as a run of bytes a
// file was written with. The two sides are spelled differently because they
// come from different places: a name the compiler knows ends at a nought, and
// a word a program wrote is an offset and a length into the source with no
// terminator anywhere near it. Asking the question is what everything from the
// lexer to the machine does with those two once it has them. See D773.
bool kest_word_same(const char *word, const char *bytes, size_t length);

void kest_source_locate(const KestSource *source, uint32_t offset,
                        uint32_t *line, uint32_t *column);

// Where a span begins in the file it was cut from. A span is an offset and a
// length into one source, so the text is not a copy and is not terminated at
// the span's end: every caller already knows the length and reads that many
// bytes. Four stages worked this out for themselves before this was written,
// which is four places that would have had to be found again had a span ever
// started counting from somewhere else. See D772.
const char *kest_span_text(const KestSource *source, KestSpan span);

void kest_diags_init(KestDiags *diags, KestArena *arena);

// Says which file the spans of the diagnostics reported next are in.
void kest_diags_in(KestDiags *diags, const KestSource *source);

// Stops anything being recorded, for a pass whose purpose is to find out
// rather than to report. Reporting the same thing twice is worse than not
// reporting it once.
void kest_diags_mute(KestDiags *diags, bool muted);

// What the words a message is written in say about the numbers put in them.
// A `%u` handed an `i64` prints a number nobody wrote, and it is the one kind
// of wrongness a message can have that reading it does not show: the message
// is a sentence either way. The compilers this is built with can say so, and
// what they say is an error like any other; a compiler that cannot is one this
// project is not built with.
#if defined(__GNUC__)
#define KEST_SAYS(words, first) __attribute__((format(printf, words, first)))
#else
#define KEST_SAYS(words, first)
#endif

// Formats and records a diagnostic. The message is copied into the arena and
// is as long as it is: a caller that wrote it into a buffer of its own first
// would cut it off in the middle of a name.
void kest_diags_add(KestDiags *diags, KestSeverity severity, const char *code,
                    KestSpan span, const char *format, ...) KEST_SAYS(5, 6);

// The same for a caller that has a `va_list` rather than arguments, which is
// every wrapper this compiler writes around these.
void kest_diags_addv(KestDiags *diags, KestSeverity severity, const char *code,
                     KestSpan span, const char *format, va_list args);
void kest_diags_suggestv(KestDiags *diags, const char *format, va_list args);

// Attaches a fix to the most recent diagnostic. Does nothing when there is
// none, so a caller need not check.
// What every fault in this compiler ends with. A fault is what this project
// got wrong rather than what a program did, and a reader who cannot tell the
// two apart goes looking in their own file: the sentence that says which is
// worth being one sentence, in one place, whichever half of the compiler
// noticed. `why` is what was expected, without a full stop. See D410.
void kest_diags_fault(KestDiags *diags, const char *why);

// And what the halves of this compiler say when one of them meets what the
// other allowed: the same refusal in the same words, said in one place because
// there are three of them now — the walk that writes a body, the backend that
// reads one, and the proof over what was emitted. `what` is what was found,
// without a full stop and without the clause that follows it.
void kest_diags_disagree(KestDiags *diags, KestSpan span, const char *what, ...)
    KEST_SAYS(3, 4);

void kest_diags_suggest(KestDiags *diags, const char *format, ...)
    KEST_SAYS(2, 3);

// Adds a second place to the most recent diagnostic, in the file given, or in
// the current one when that is NULL. Does nothing when there is no diagnostic
// or no room, so a caller need not check.
void kest_diags_note(KestDiags *diags, const KestSource *source, KestSpan span,
                     const char *format, ...) KEST_SAYS(4, 5);

// The same, to one further back. What is said about a copy of a generic is
// said about everything that copy's body reported, and a body reports more
// than one thing.
// A note on one further back takes the last place rather than being the one
// thing left out. What reaches back to a diagnostic that is already finished is
// a sentence about the whole of it -- which copy of a generic a body's
// sentences are about is not the ninth thing a reader wants, it is what the
// other eight are about -- and nothing reaches back to add one more place. The
// note it displaces is counted, the same as one that never fitted. See D762.
void kest_diags_note_at(KestDiags *diags, uint32_t which,
                        const KestSource *source, KestSpan span,
                        const char *format, ...) KEST_SAYS(5, 6);

// The one thing a run with no memory can say. The library records it as a bit
// and writes it out when it reports; the command line says it directly for the
// memory it wanted for itself, before there is a build to record anything in.
// One code and one sentence, because it is one thing that happened. The code
// is in the range a machine and a command line report in: what ran out is the
// machine this is running on rather than anything in the program.
#define KEST_STARVED_CODE "K0639"
#define KEST_STARVED_SAYS                                                      \
    "there was not enough memory to finish, or to say more about it"

// And the same run stopped by a ceiling rather than by the machine. The bit
// recorded is the one above — running out is running out — and what is said
// about it is not: a ceiling is a number somebody chose and can choose again,
// and a machine with nothing left is an afternoon spent somewhere else. The
// numbers go with it for the reason the heap's own do, which is that a run
// that missed by eight bytes and one that missed by a gigabyte read alike
// otherwise. See D843.
#define KEST_CRAMPED_CODE "K0658"
#define KEST_CRAMPED_SAYS                                                      \
    "this has taken %zu of the %zu bytes it was given, and wanted %zu more"
// And the same ceiling met before there is anywhere to write a diagnostic
// down, which is a build that could not be opened at all. Two doors reach it —
// the command line and the one a host compiles through — and a sentence
// written in two places is a sentence that comes apart. See D844.
#define KEST_CRAMPED_START                                                     \
    "this was given %zu bytes, which is not enough to begin reading a program"

// Says that something could not be said for want of memory, which is the one
// thing this can record without any. It counts as an error, because what a
// caller does with the count is decide whether anything went wrong, and what
// went wrong here is that the machine this is running on has no more room.
void kest_diags_starve(KestDiags *diags);

// One diagnostic from something that has no arena to make one in: a build that
// could not be opened at all, which is the only way to be here. The words are
// the ones every other diagnostic is written with because they are written
// beside them, so a name that changes in the writer changes in this too.
void kest_diags_say_one(FILE *out, bool as_json, const char *code,
                        const char *message);

// Adds everything one run holds to the end of another, for a caller that wants
// one sorted set out of two. Both have to be on the same arena, because what a
// diagnostic points at is not copied again.
void kest_diags_absorb(KestDiags *into, const KestDiags *from);

// Orders diagnostics by where they are in the file. Stages find problems in
// the order that suits the stage, and a reader scans in the order of the text.
void kest_diags_sort(KestDiags *diags);

// Renders for a person: severity, code, location, the source line, a caret
// under the span, and the suggestion.
void kest_diags_render(const KestDiags *diags, FILE *out);

// Writes a string the way JSON spells one, quotes and escapes and all. Three
// files compose JSON and the string is the part that has to be right, so there
// is one of these rather than one each.
void kest_json_text(const char *text, FILE *out);

// Renders the identical set as JSON, for tooling and for models repairing
// their own output.
void kest_diags_render_json(const KestDiags *diags, FILE *out);

// The same without the object around it, for when something else is going in
// beside it.
void kest_diags_write_json(const KestDiags *diags, FILE *out);

#endif
