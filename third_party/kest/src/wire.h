#ifndef KEST_WIRE_H
#define KEST_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "diag.h"
#include "mem.h"

// What the two protocols an editor speaks are made of: a message framed by
// its length, JSON read into a tree, and JSON written into a buffer. The
// language server and the debug adapter are two conversations in the same
// words, so the words are written once. See D1182.

// A JSON value, which is what a client sends and what has to be read without
// a library. The tree is in an arena that is thrown away with the message, so
// nothing here frees anything.
typedef enum {
    KEST_JSON_NOTHING,
    KEST_JSON_FALSE,
    KEST_JSON_TRUE,
    KEST_JSON_NUMBER,
    KEST_JSON_TEXT,
    KEST_JSON_LIST,
    KEST_JSON_OBJECT,
} KestJsonKind;

typedef struct KestJson KestJson;

struct KestJson {
    KestJsonKind kind;
    double number;
    // Text, already unescaped, and how long it is: a message may carry a
    // nought inside a string and text that ended at one would be a document
    // cut in half.
    const char *text;
    size_t length;
    // Members for an object, elements for a list. An object keeps its names
    // beside its values in the same order they arrived.
    const char **names;
    size_t *name_lengths;
    KestJson **items;
    uint32_t count;
    uint32_t capacity;
};


// The value a message holds, or NULL for one that is not JSON. The tree is in
// `arena` and goes with it.
KestJson *kest_wire_read(KestArena *arena, const char *bytes, size_t length);

// A member of an object by name, and a member of a member. NULL where there is
// none or where what was asked about is not an object.
const KestJson *kest_wire_member(const KestJson *object, const char *name);
const KestJson *kest_wire_down(const KestJson *object, const char *first,
                               const char *second);

// The next message on a stream, read by the length its header gives, and how
// long it is. NULL at the end of the stream or for a header with no length.
// The caller frees it.
char *kest_wire_next(FILE *in, size_t *length);

// A message written out framed by its length.
void kest_wire_send(FILE *out, const char *body, size_t length);

// A message and its length, which is how LSP frames one. Written into a buffer
// first because the header says how long the body is and a body written
// straight out cannot be measured afterwards. Grown by doubling and written to
// through three calls, because `open_memstream` is not in the standard this is
// held to and is not on every platform this builds for.
typedef struct {
    char *bytes;
    size_t used;
    size_t room;
    bool broke;
} KestSaid;

void kest_wire_say(KestSaid *said, const char *text);
void kest_wire_char(KestSaid *said, char c);
void kest_wire_sayf(KestSaid *said, const char *format, ...) KEST_SAYS(2, 3);
// A piece of text as a JSON string, escaped; NULL is the empty one.
void kest_wire_escaped(KestSaid *out, const char *text, size_t length);
void kest_wire_text(KestSaid *out, const char *text);
// What was written, given back.
void kest_wire_let_go(KestSaid *said);

#endif
