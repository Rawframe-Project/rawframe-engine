#include "wire.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *at;
    const char *end;
    KestArena *arena;
    bool broke;
} Reading;

static KestJson *read_value(Reading *reading);

static void skip_space(Reading *reading) {
    while (reading->at < reading->end &&
           (*reading->at == ' ' || *reading->at == '\t' ||
            *reading->at == '\n' || *reading->at == '\r')) {
        reading->at++;
    }
}

static KestJson *made(Reading *reading, KestJsonKind kind) {
    KestJson *one = KEST_ARENA_ARRAY(reading->arena, KestJson, 1);
    if (one == NULL) {
        reading->broke = true;
        return NULL;
    }
    memset(one, 0, sizeof(*one));
    one->kind = kind;
    return one;
}

static bool room_for_one(Reading *reading, KestJson *held) {
    if (held->count < held->capacity) {
        return true;
    }
    uint32_t grown = held->capacity == 0 ? 8 : held->capacity * 2;
    KestJson **items = KEST_ARENA_ARRAY(reading->arena, KestJson *, grown);
    const char **names = KEST_ARENA_ARRAY(reading->arena, const char *, grown);
    size_t *lengths = KEST_ARENA_ARRAY(reading->arena, size_t, grown);
    if (items == NULL || names == NULL || lengths == NULL) {
        reading->broke = true;
        return false;
    }
    for (uint32_t i = 0; i < held->count; i++) {
        items[i] = held->items[i];
        names[i] = held->names == NULL ? NULL : held->names[i];
        lengths[i] = held->name_lengths == NULL ? 0 : held->name_lengths[i];
    }
    held->items = items;
    held->names = names;
    held->name_lengths = lengths;
    held->capacity = grown;
    return true;
}

// A codepoint written out as UTF-8, which is what `\u` in a message means and
// what the rest of this compiler reads.
static size_t put_utf8(char *out, uint32_t code) {
    if (code < 0x80) {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800) {
        out[0] = (char)(0xc0 | (code >> 6));
        out[1] = (char)(0x80 | (code & 0x3f));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = (char)(0xe0 | (code >> 12));
        out[1] = (char)(0x80 | ((code >> 6) & 0x3f));
        out[2] = (char)(0x80 | (code & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (code >> 18));
    out[1] = (char)(0x80 | ((code >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((code >> 6) & 0x3f));
    out[3] = (char)(0x80 | (code & 0x3f));
    return 4;
}

static uint32_t hex_of(const char *at) {
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        char c = at[i];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= (uint32_t)(c - 'A' + 10);
        }
    }
    return value;
}

static KestJson *read_text(Reading *reading) {
    reading->at++;
    const char *from = reading->at;
    size_t most = (size_t)(reading->end - from) + 1;
    char *out = kest_arena_alloc(reading->arena, most, 1);
    if (out == NULL) {
        reading->broke = true;
        return NULL;
    }
    size_t used = 0;
    while (reading->at < reading->end && *reading->at != '"') {
        if (*reading->at != '\\') {
            out[used++] = *reading->at++;
            continue;
        }
        reading->at++;
        if (reading->at >= reading->end) {
            break;
        }
        char what = *reading->at++;
        switch (what) {
        case 'n': out[used++] = '\n'; break;
        case 't': out[used++] = '\t'; break;
        case 'r': out[used++] = '\r'; break;
        case 'b': out[used++] = '\b'; break;
        case 'f': out[used++] = '\f'; break;
        case 'u': {
            if (reading->end - reading->at < 4) {
                reading->broke = true;
                return NULL;
            }
            uint32_t code = hex_of(reading->at);
            reading->at += 4;
            // A pair of halves is one character. A client writes anything past
            // the first sixty-five thousand as two, so reading the first on
            // its own would put half a character in a file.
            if (code >= 0xd800 && code <= 0xdbff &&
                reading->end - reading->at >= 6 && reading->at[0] == '\\' &&
                reading->at[1] == 'u') {
                uint32_t low = hex_of(reading->at + 2);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                    reading->at += 6;
                }
            }
            used += put_utf8(out + used, code);
            break;
        }
        default: out[used++] = what; break;
        }
    }
    if (reading->at < reading->end) {
        reading->at++;
    }
    out[used] = '\0';
    KestJson *one = made(reading, KEST_JSON_TEXT);
    if (one == NULL) {
        return NULL;
    }
    one->text = out;
    one->length = used;
    return one;
}

static KestJson *read_value(Reading *reading) {
    skip_space(reading);
    if (reading->at >= reading->end || reading->broke) {
        reading->broke = true;
        return NULL;
    }
    char c = *reading->at;
    if (c == '"') {
        return read_text(reading);
    }
    if (c == '{' || c == '[') {
        bool object = c == '{';
        char closing = object ? '}' : ']';
        reading->at++;
        KestJson *held = made(reading, object ? KEST_JSON_OBJECT : KEST_JSON_LIST);
        if (held == NULL) {
            return NULL;
        }
        skip_space(reading);
        if (reading->at < reading->end && *reading->at == closing) {
            reading->at++;
            return held;
        }
        while (reading->at < reading->end && !reading->broke) {
            const char *name = NULL;
            size_t name_length = 0;
            if (object) {
                skip_space(reading);
                KestJson *key = read_value(reading);
                if (key == NULL || key->kind != KEST_JSON_TEXT) {
                    reading->broke = true;
                    return NULL;
                }
                name = key->text;
                name_length = key->length;
                skip_space(reading);
                if (reading->at >= reading->end || *reading->at != ':') {
                    reading->broke = true;
                    return NULL;
                }
                reading->at++;
            }
            KestJson *value = read_value(reading);
            if (value == NULL || !room_for_one(reading, held)) {
                reading->broke = true;
                return NULL;
            }
            held->names[held->count] = name;
            held->name_lengths[held->count] = name_length;
            held->items[held->count] = value;
            held->count++;
            skip_space(reading);
            if (reading->at < reading->end && *reading->at == ',') {
                reading->at++;
                continue;
            }
            if (reading->at < reading->end && *reading->at == closing) {
                reading->at++;
                return held;
            }
            reading->broke = true;
            return NULL;
        }
        reading->broke = true;
        return NULL;
    }
    if (c == 't' && reading->end - reading->at >= 4) {
        reading->at += 4;
        return made(reading, KEST_JSON_TRUE);
    }
    if (c == 'f' && reading->end - reading->at >= 5) {
        reading->at += 5;
        return made(reading, KEST_JSON_FALSE);
    }
    if (c == 'n' && reading->end - reading->at >= 4) {
        reading->at += 4;
        return made(reading, KEST_JSON_NOTHING);
    }
    char *after = NULL;
    double value = strtod(reading->at, &after);
    if (after == reading->at) {
        reading->broke = true;
        return NULL;
    }
    reading->at = after;
    KestJson *one = made(reading, KEST_JSON_NUMBER);
    if (one == NULL) {
        return NULL;
    }
    one->number = value;
    return one;
}

const KestJson *kest_wire_member(const KestJson *object, const char *name) {
    if (object == NULL || object->kind != KEST_JSON_OBJECT) {
        return NULL;
    }
    size_t length = strlen(name);
    for (uint32_t i = 0; i < object->count; i++) {
        if (object->name_lengths[i] == length &&
            memcmp(object->names[i], name, length) == 0) {
            return object->items[i];
        }
    }
    return NULL;
}

const KestJson *kest_wire_down(const KestJson *object, const char *first,
                        const char *second) {
    return kest_wire_member(kest_wire_member(object, first), second);
}


KestJson *kest_wire_read(KestArena *arena, const char *bytes, size_t length) {
    Reading reading = {bytes, bytes + length, arena, false};
    KestJson *message = read_value(&reading);
    return reading.broke ? NULL : message;
}

char *kest_wire_next(FILE *in, size_t *length) {
    char header[512];
    size_t wanted = 0;
    bool saw_length = false;
    // The header, line by line, to the blank one. Anything that is not a
    // length is skipped: a client may send a content type and this does not
    // care what it says.
    while (fgets(header, sizeof(header), in) != NULL) {
        if (header[0] == '\r' || header[0] == '\n') {
            break;
        }
        unsigned long said = 0;
        if (sscanf(header, "Content-Length: %lu", &said) == 1) {
            wanted = (size_t)said;
            saw_length = true;
        }
    }
    if (!saw_length || wanted == 0) {
        return NULL;
    }
    char *body = malloc(wanted + 1);
    if (body == NULL) {
        return NULL;
    }
    if (fread(body, 1, wanted, in) != wanted) {
        free(body);
        return NULL;
    }
    body[wanted] = '\0';
    *length = wanted;
    return body;
}

void kest_wire_send(FILE *out, const char *body, size_t length) {
    fprintf(out, "Content-Length: %zu\r\n\r\n", length);
    fwrite(body, 1, length, out);
    fflush(out);
}


static void said_bytes(KestSaid *said, const char *bytes, size_t length) {
    if (said->broke) {
        return;
    }
    if (said->used + length + 1 > said->room) {
        size_t grown = said->room == 0 ? 1024 : said->room;
        while (grown < said->used + length + 1) {
            grown *= 2;
        }
        char *moved = realloc(said->bytes, grown);
        if (moved == NULL) {
            said->broke = true;
            return;
        }
        said->bytes = moved;
        said->room = grown;
    }
    memcpy(said->bytes + said->used, bytes, length);
    said->used += length;
    said->bytes[said->used] = '\0';
}

void kest_wire_say(KestSaid *said, const char *text) {
    said_bytes(said, text, strlen(text));
}

void kest_wire_char(KestSaid *said, char c) {
    said_bytes(said, &c, 1);
}

void kest_wire_sayf(KestSaid *said, const char *format, ...) {
    char room[512];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(room, sizeof(room), format, args);
    va_end(args);
    if (written < 0) {
        said->broke = true;
        return;
    }
    if ((size_t)written < sizeof(room)) {
        said_bytes(said, room, (size_t)written);
        return;
    }
    char *wider = malloc((size_t)written + 1);
    if (wider == NULL) {
        said->broke = true;
        return;
    }
    va_start(args, format);
    vsnprintf(wider, (size_t)written + 1, format, args);
    va_end(args);
    said_bytes(said, wider, (size_t)written);
    free(wider);
}

void kest_wire_let_go(KestSaid *said) {
    free(said->bytes);
    said->bytes = NULL;
    said->used = 0;
    said->room = 0;
}

void kest_wire_escaped(KestSaid *out, const char *text, size_t length) {
    kest_wire_char(out, '"');
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        switch (c) {
        case '"': kest_wire_say(out, "\\\""); break;
        case '\\': kest_wire_say(out, "\\\\"); break;
        case '\n': kest_wire_say(out, "\\n"); break;
        case '\r': kest_wire_say(out, "\\r"); break;
        case '\t': kest_wire_say(out, "\\t"); break;
        default:
            if (c < 0x20) {
                kest_wire_sayf(out, "\\u%04x", c);
            } else {
                kest_wire_char(out, (char)c);
            }
        }
    }
    kest_wire_char(out, '"');
}

void kest_wire_text(KestSaid *out, const char *text) {
    kest_wire_escaped(out, text == NULL ? "" : text, text == NULL ? 0 : strlen(text));
}

