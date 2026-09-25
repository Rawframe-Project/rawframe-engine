#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text;
    KestTokenKind kind;
} Keyword;

static const Keyword KEYWORDS[] = {
    {"break", KEST_TOK_BREAK}, {"const", KEST_TOK_CONST},
    {"continue", KEST_TOK_CONTINUE}, {"defer", KEST_TOK_DEFER},
    {"else", KEST_TOK_ELSE}, {"enum", KEST_TOK_ENUM},
    {"extern", KEST_TOK_EXTERN}, {"false", KEST_TOK_FALSE},
    {"fn", KEST_TOK_FN}, {"for", KEST_TOK_FOR},
    {"if", KEST_TOK_IF}, {"import", KEST_TOK_IMPORT},
    {"in", KEST_TOK_IN}, {"let", KEST_TOK_LET},
    {"match", KEST_TOK_MATCH}, {"module", KEST_TOK_MODULE},
    {"none", KEST_TOK_NONE}, {"return", KEST_TOK_RETURN},
    {"struct", KEST_TOK_STRUCT}, {"true", KEST_TOK_TRUE},
    {"while", KEST_TOK_WHILE},
};

static const char *const TOKEN_NAMES[] = {
    "end of file", "end of line", "identifier", "integer",  "float",
    "string",      "byte",        "`break`",     "`const`",    "`continue`",
    "`defer`",     "`else`",
    "`enum`",      "`extern`",    "`false`",    "`fn`",     "`for`",
    "`if`",        "`import`",    "`in`",       "`let`",    "`match`",
    "`module`",    "`none`",
    "`return`",    "`struct`",    "`true`",     "`while`",  "`(`",  "`)`",
    "`{`",         "`}`",         "`[`",        "`]`",      "`,`",
    "`;`",
    "`.`",         "`..`",        "`:`",        "`?`",      "`->`",
    "`=`",
    "`==`",        "`!=`",        "`<`",        "`<=`",     "`>`",
    "`>=`",        "`+`",         "`-`",        "`*`",      "`/`",
    "`%`",         "`!`",         "`&&`",       "`||`",     "`&`",
    "`|`",         "`^`",         "`~`",        "`<<`",     "`>>`",
    "`+=`",        "`-=`",        "`*=`",       "`/=`",     "invalid token",
};

// What an escape is written as and what it stands for. One list: what a piece
// of text may hold, what a byte written on its own may hold, what either turns
// into, and the message that names them are all read from here. A `default`
// beside a few cases is how a tenth would arrive without anybody deciding what
// it means.
typedef struct {
    char written;
    char means;
    bool reads;
} Escape;

// One of them stands for a character rather than for a byte, and reads the
// character's number after it. It is here because the file may not hold the
// character itself: a mark with no width is refused by `K0108` where it is
// written, and text that has to carry one -- a zero-width non-joiner between
// the parts of a Persian word, a non-breaking space in front of a French
// question mark -- has nowhere else to be written. See D1056.
static const Escape ESCAPES[] = {
    {'n', '\n', false},  {'t', '\t', false}, {'r', '\r', false},
    {'\\', '\\', false}, {'"', '"', false},  {'{', '{', false},
    {'}', '}', false},   {'0', '\0', false}, {'u', 0, true},
};

// The one written this way, or NULL for a character that is not an escape.
static const Escape *escape_named(char written) {
    for (size_t i = 0; i < sizeof(ESCAPES) / sizeof(ESCAPES[0]); i++) {
        if (ESCAPES[i].written == written) {
            return &ESCAPES[i];
        }
    }
    return NULL;
}

// The list a reader is given when they write one that is not there, built
// from the same table rather than written out beside it.
static const char *escapes_written(KestArena *arena) {
    size_t room = sizeof(ESCAPES) / sizeof(ESCAPES[0]) * 9 + 1;
    char *out = kest_arena_alloc(arena, room, 1);
    if (out == NULL) {
        return "";
    }
    size_t used = 0;
    for (size_t i = 0; i < sizeof(ESCAPES) / sizeof(ESCAPES[0]); i++) {
        if (i > 0) {
            out[used++] = ' ';
        }
        out[used++] = '\\';
        out[used++] = ESCAPES[i].written;
        if (ESCAPES[i].reads) {
            memcpy(out + used, "{...}", 5);
            used += 5;
        }
    }
    out[used] = '\0';
    return out;
}

typedef enum {
    NUMBER_READ,
    NUMBER_NO_BRACE,
    NUMBER_NO_DIGITS,
    NUMBER_TOO_LONG,
    NUMBER_NOT_CLOSED,
    NUMBER_NOT_A_CHARACTER,
} NumberRead;

// At most six, because the last character there is is `U+10FFFF`.
#define NUMBER_DIGITS 6u

// What the digit is worth, or sixteen for a character that is not one.
static uint32_t hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return (uint32_t)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (uint32_t)(c - 'a') + 10u;
    }
    if (c >= 'A' && c <= 'F') {
        return (uint32_t)(c - 'A') + 10u;
    }
    return 16u;
}

// What is written after a `\u`, read from the brace that has to follow it.
// Answers how many characters that took and which character is written there,
// or why what is written there is not one. The lexer and the reader of a
// literal walk it with this one function, so they cannot disagree about where
// an escape ends.
static NumberRead character_number(const char *text, uint32_t length,
                                   uint32_t at, uint32_t *width,
                                   uint32_t *code) {
    *width = 0;
    *code = 0;
    if (at >= length || text[at] != '{') {
        return NUMBER_NO_BRACE;
    }

    uint32_t digits = 0;
    uint32_t value = 0;
    uint32_t i = at + 1;
    for (; i < length; i++) {
        uint32_t digit = hex_digit(text[i]);
        if (digit > 15u) {
            break;
        }
        if (digits < NUMBER_DIGITS) {
            value = (value << 4) | digit;
        }
        digits++;
    }

    if (i >= length || text[i] != '}') {
        return digits == 0 ? NUMBER_NO_DIGITS : NUMBER_NOT_CLOSED;
    }
    // How far it reaches is answered even when what is written in it is
    // refused, so that everything else walking this text steps over the whole
    // escape rather than finding a hole where the brace is and saying
    // something about that instead.
    *width = i + 1 - at;
    if (digits == 0) {
        return NUMBER_NO_DIGITS;
    }
    if (digits > NUMBER_DIGITS) {
        return NUMBER_TOO_LONG;
    }
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        *code = value;
        return NUMBER_NOT_A_CHARACTER;
    }

    *code = value;
    return NUMBER_READ;
}

// The character written out as the bytes it is, and how many there are. The
// one encoder there is, the way `decoded` below is the one decoder: a
// character this writes is a character that reads back. See D971.
static uint32_t utf8_written(uint32_t code, char *out) {
    if (code < 0x80) {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800) {
        out[0] = (char)(0xc0u | (code >> 6));
        out[1] = (char)(0x80u | (code & 0x3fu));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = (char)(0xe0u | (code >> 12));
        out[1] = (char)(0x80u | ((code >> 6) & 0x3fu));
        out[2] = (char)(0x80u | (code & 0x3fu));
        return 3;
    }
    out[0] = (char)(0xf0u | (code >> 18));
    out[1] = (char)(0x80u | ((code >> 12) & 0x3fu));
    out[2] = (char)(0x80u | ((code >> 6) & 0x3fu));
    out[3] = (char)(0x80u | (code & 0x3fu));
    return 4;
}

// The bytes a written piece of text stands for, and how many there are. The
// count is handed back rather than measured afterwards, because a nought is a
// byte text may hold since D971 and measuring would stop at the first one.
const char *kest_literal_text(KestArena *arena, const KestSource *source,
                              KestSpan span, size_t *length_out) {
    const char *raw = kest_span_text(source, span);
    size_t length = span.length;

    char *text = kest_arena_alloc(arena, length + 1, 1);
    if (text == NULL) {
        if (length_out != NULL) {
            *length_out = 0;
        }
        return "";
    }

    size_t used = 0;
    for (size_t i = 0; i < length; i++) {
        if (raw[i] != '\\' || i + 1 == length) {
            text[used++] = raw[i];
            continue;
        }
        i++;
        const Escape *escape = escape_named(raw[i]);
        // One that is not an escape was refused where it was read, and what
        // is written here is what somebody wrote: a message about it says so
        // and this is not the place to say it twice.
        if (escape == NULL) {
            text[used++] = raw[i];
            continue;
        }
        if (!escape->reads) {
            text[used++] = escape->means;
            continue;
        }
        uint32_t width = 0;
        uint32_t code = 0;
        if (character_number(raw, (uint32_t)length, (uint32_t)(i + 1), &width,
                             &code) == NUMBER_READ) {
            used += utf8_written(code, text + used);
        } else {
            text[used++] = raw[i];
        }
        i += width;
    }
    // Still ended with a nought, for a host that reads the bytes as a C
    // string where it knows there is none inside. What says how long it is is
    // the count beside it. See D964 and D971.
    text[used] = '\0';
    if (length_out != NULL) {
        *length_out = used;
    }
    return text;
}

uint32_t kest_escape_width(const KestSource *source, uint32_t at) {
    const Escape *escape = at + 1 < source->length
                               ? escape_named(source->text[at + 1])
                               : NULL;
    if (escape == NULL || !escape->reads) {
        return 2;
    }
    uint32_t width = 0;
    uint32_t code = 0;
    character_number(source->text, (uint32_t)source->length, at + 2, &width,
                     &code);
    return 2 + width;
}

double kest_literal_real(const KestSource *source, KestSpan span) {
    char buffer[64];
    size_t length = span.length < sizeof(buffer) - 1 ? span.length : 0;
    memcpy(buffer, kest_span_text(source, span), length);
    buffer[length] = '\0';
    return strtod(buffer, NULL);
}

// One name a kind, and the compiler counts them. `check-tables.sh` holds the
// two to saying the same thing; this holds them to being the same length,
// which is the half that can be caught while building.
_Static_assert(sizeof(TOKEN_NAMES) / sizeof(TOKEN_NAMES[0]) ==
                   KEST_TOK_ERROR + 1,
               "every token kind has a name and nothing else does");

// The keyword a word was nearly, or nothing when it was near none of them. A
// misspelt keyword is a name as far as the lexer is concerned, and what
// happens next is a message about the token after it, so the parser asks this
// before it says anything.
//
// The limit is the one every suggestion in this compiler uses: a third of what
// was written, and nothing under three letters, because `in`, `if` and `fn`
// are one edit from most short words.
const char *kest_nearest_keyword(const char *name, size_t length) {
    if (length < 3) {
        return NULL;
    }
    uint32_t limit = length == 3 ? 1 : (uint32_t)length / 3;
    const char *best = NULL;
    uint32_t nearest = limit + 1;
    for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); i++) {
        uint32_t distance = kest_word_distance(name, length, KEYWORDS[i].text,
                                               strlen(KEYWORDS[i].text), limit);
        if (distance < nearest) {
            nearest = distance;
            best = KEYWORDS[i].text;
        }
    }
    return best;
}

// A comment runs to the end of its line, and a string may hold two slashes
// that begin nothing, which is the only reason this is not a search. The
// lexer throws them away — they are not tokens — and this is where anything
// that wants them asks, so that the formatter and a tool reading a file are
// not two answers to one question.
uint32_t kest_comments(const KestSource *source, KestSpan *into,
                       uint32_t room) {
    const char *text = source->text;
    size_t length = source->length;
    uint32_t found = 0;

    for (size_t i = 0; i < length; i++) {
        if (text[i] == '"') {
            // A hole may hold a string of its own, so the quote that closes
            // this one is the one found outside every brace.
            uint32_t depth = 0;
            for (i++; i < length; i++) {
                if (text[i] == '\\') {
                    i++;
                } else if (text[i] == '{') {
                    depth++;
                } else if (text[i] == '}' && depth > 0) {
                    depth--;
                } else if (text[i] == '"' && depth == 0) {
                    break;
                }
            }
            continue;
        }
        if (text[i] != '/' || i + 1 >= length || text[i + 1] != '/') {
            continue;
        }
        // A comment ends where the line does, and a line ends with one
        // character here and two on a machine that writes both. The first of
        // the two is where the comment stops either way: the return was never
        // something somebody wrote in it, and a file that ends its lines with
        // one of those and nothing else is a file this would otherwise read
        // as one comment from the first `//` to the end.
        size_t end = i;
        while (end < length && text[end] != '\n' && text[end] != '\r') {
            end++;
        }
        if (into != NULL && found < room) {
            KestSpan span = {(uint32_t)i, (uint32_t)(end - i)};
            into[found] = span;
        }
        found++;
        i = end;
    }
    return found;
}

const char *kest_token_name(KestTokenKind kind) {
    return TOKEN_NAMES[kind];
}

const char *kest_token_bare(KestTokenKind kind, char *into, size_t room) {
    const char *name = TOKEN_NAMES[kind];
    size_t used = 0;
    for (const char *c = name; *c != '\0' && used + 1 < room; c++) {
        if (*c != '`') {
            into[used++] = *c;
        }
    }
    into[used] = '\0';
    return into;
}

static void kest_lexer_init(KestLexer *lexer, const KestSource *source,
                     KestDiags *diags) {
    lexer->source = source;
    lexer->diags = diags;
    lexer->offset = 0;
    lexer->bracket_depth = 0;
    lexer->previous = KEST_TOK_NEWLINE;
    // Which nothing set, so it was whatever the stack held. The one thing
    // that read it was a suggestion — a file-level escape mistake could be
    // told that a hole holds code — so the mistake it made was to say
    // something wrong to somebody now and then, and a wrong suggestion costs
    // more than none. Nothing here catches an unset field: the sanitisers
    // this project builds under do not read memory that was never written.
    lexer->in_hole = false;
}

static char at(const KestLexer *lexer, uint32_t ahead) {
    uint32_t index = lexer->offset + ahead;
    if (index >= lexer->source->length) {
        return '\0';
    }
    return lexer->source->text[index];
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Any byte above ASCII starts or continues an identifier, which makes every
// UTF-8 sequence legal in a name without carrying Unicode tables.
static bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           (unsigned char)c >= 0x80;
}

static bool is_ident_part(char c) {
    return is_ident_start(c) || is_digit(c);
}

static KestSpan span_from(uint32_t start, uint32_t end) {
    KestSpan span = {start, end - start};
    return span;
}

// A character written by its number, read from the backslash it begins at.
// Answers how far it reaches, which is the two characters of `\u` when what
// follows them is not a number, so that the rest of the text is still walked.
static uint32_t scan_character_escape(KestLexer *lexer, uint32_t at) {
    uint32_t width = 0;
    uint32_t code = 0;
    NumberRead read = character_number(lexer->source->text,
                                       (uint32_t)lexer->source->length, at + 2,
                                       &width, &code);
    KestSpan span = span_from(at, at + 2);
    switch (read) {
    case NUMBER_READ:
        return 2 + width;
    case NUMBER_NO_BRACE:
        kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0110", span,
                       "`\\u` is written without a character after it");
        kest_diags_suggest(lexer->diags,
                           "the number goes in braces: `\\u{200c}`");
        break;
    case NUMBER_NO_DIGITS:
        kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0110", span,
                       "the braces after `\\u` hold no number");
        kest_diags_suggest(lexer->diags,
                           "a character's number is written in hexadecimal: "
                           "`\\u{200c}`");
        break;
    case NUMBER_TOO_LONG:
        kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0110", span,
                       "a character's number is at most %u digits",
                       NUMBER_DIGITS);
        kest_diags_suggest(lexer->diags,
                           "the last character there is is `\\u{10ffff}`");
        break;
    case NUMBER_NOT_CLOSED:
        kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0110", span,
                       "the braces after `\\u` are not closed");
        kest_diags_suggest(lexer->diags, "add a closing `}`");
        break;
    case NUMBER_NOT_A_CHARACTER:
        kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0110", span,
                       "`U+%04X` is not a character", code);
        kest_diags_suggest(lexer->diags,
                           "characters run up to `U+10FFFF`, and `U+D800` to "
                           "`U+DFFF` are not among them");
        break;
    }
    return 2 + width;
}

static KestToken make(KestLexer *lexer, KestTokenKind kind, uint32_t start) {
    lexer->previous = kind;
    KestToken token = {kind, span_from(start, lexer->offset)};
    return token;
}

// Whether a line break after this token ends a statement. A break after an
// operator, an opening bracket or a comma is a continuation, because the
// statement cannot have finished there.
// A newline after one of these ends the statement, and after anything else it
// does not. It is a list of what a value can end with, and every kind there is
// appears in it: a token kind added without a decision about this reads as an
// unfinished line and swallows the next one, which has happened twice.
bool kest_lexer_ends_statement(KestTokenKind kind) {
    switch (kind) {
    case KEST_TOK_IDENT:
    case KEST_TOK_INT:
    case KEST_TOK_FLOAT:
    case KEST_TOK_STRING:
    case KEST_TOK_BYTE:
    case KEST_TOK_TRUE:
    case KEST_TOK_FALSE:
    case KEST_TOK_RPAREN:
    case KEST_TOK_RBRACE:
    case KEST_TOK_RBRACKET:
    case KEST_TOK_RETURN:
    case KEST_TOK_BREAK:
    case KEST_TOK_CONTINUE:
    case KEST_TOK_QUESTION:
    case KEST_TOK_NONE:
    // A type that takes types ends in one: `ref<Npc>` and `store<Job>` are
    // what a field is written as, and a field ends where its line does. A
    // comparison written with its right side on the next line is the price,
    // and it is refused where it is written rather than read as two things.
    case KEST_TOK_GT:
        return true;

    // Everything else, written out rather than left to a `default`, because a
    // token kind added without a decision about this reads as an unfinished
    // line and swallows the next one. That has happened twice: `byte` and
    // `>`. Now the build stops until somebody says which of the two it is.
    case KEST_TOK_EOF:
    case KEST_TOK_NEWLINE:
    case KEST_TOK_CONST:
    case KEST_TOK_DEFER:
    case KEST_TOK_ELSE:
    case KEST_TOK_ENUM:
    case KEST_TOK_EXTERN:
    case KEST_TOK_FN:
    case KEST_TOK_FOR:
    case KEST_TOK_IF:
    case KEST_TOK_IMPORT:
    case KEST_TOK_IN:
    case KEST_TOK_LET:
    case KEST_TOK_MATCH:
    case KEST_TOK_MODULE:
    case KEST_TOK_STRUCT:
    case KEST_TOK_WHILE:
    case KEST_TOK_LPAREN:
    case KEST_TOK_LBRACE:
    case KEST_TOK_LBRACKET:
    case KEST_TOK_COMMA:
    case KEST_TOK_SEMICOLON:
    case KEST_TOK_DOT:
    case KEST_TOK_DOTDOT:
    case KEST_TOK_COLON:
    case KEST_TOK_ARROW:
    case KEST_TOK_EQ:
    case KEST_TOK_EQEQ:
    case KEST_TOK_BANGEQ:
    case KEST_TOK_LT:
    case KEST_TOK_LTEQ:
    case KEST_TOK_GTEQ:
    case KEST_TOK_PLUS:
    case KEST_TOK_MINUS:
    case KEST_TOK_STAR:
    case KEST_TOK_SLASH:
    case KEST_TOK_PERCENT:
    case KEST_TOK_BANG:
    case KEST_TOK_AMPAMP:
    case KEST_TOK_PIPEPIPE:
    case KEST_TOK_AMP:
    case KEST_TOK_PIPE:
    case KEST_TOK_CARET:
    case KEST_TOK_TILDE:
    case KEST_TOK_LTLT:
    case KEST_TOK_GTGT:
    case KEST_TOK_PLUSEQ:
    case KEST_TOK_MINUSEQ:
    case KEST_TOK_STAREQ:
    case KEST_TOK_SLASHEQ:
    case KEST_TOK_ERROR:
        return false;
    }
    // Nothing reaches this: every kind there is, is above. C wants a value.
    return false;
}

static KestToken scan_ident(KestLexer *lexer, uint32_t start) {
    while (is_ident_part(at(lexer, 0))) {
        lexer->offset++;
    }

    size_t length = lexer->offset - start;
    const char *text = lexer->source->text + start;
    for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); i++) {
        if (kest_word_same(KEYWORDS[i].text, text, length)) {
            return make(lexer, KEYWORDS[i].kind, start);
        }
    }
    return make(lexer, KEST_TOK_IDENT, start);
}

static KestToken scan_number(KestLexer *lexer, uint32_t start) {
    if (at(lexer, 0) == '0' && (at(lexer, 1) == 'x' || at(lexer, 1) == 'X')) {
        lexer->offset += 2;
        if (!is_hex_digit(at(lexer, 0))) {
            kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0104",
                           span_from(start, lexer->offset),
                           "hexadecimal literal has no digits");
            return make(lexer, KEST_TOK_ERROR, start);
        }
        while (is_hex_digit(at(lexer, 0))) {
            lexer->offset++;
        }
        return make(lexer, KEST_TOK_INT, start);
    }

    while (is_digit(at(lexer, 0))) {
        lexer->offset++;
    }

    bool is_float = false;
    if (at(lexer, 0) == '.' && is_digit(at(lexer, 1))) {
        is_float = true;
        lexer->offset++;
        while (is_digit(at(lexer, 0))) {
            lexer->offset++;
        }
    }

    if (at(lexer, 0) == 'e' || at(lexer, 0) == 'E') {
        uint32_t exponent = lexer->offset;
        lexer->offset++;
        if (at(lexer, 0) == '+' || at(lexer, 0) == '-') {
            lexer->offset++;
        }
        if (!is_digit(at(lexer, 0))) {
            // Back out, so `1e` reads as the number 1 followed by the name e
            // and the parser reports what is actually wrong there.
            lexer->offset = exponent;
        } else {
            is_float = true;
            while (is_digit(at(lexer, 0))) {
                lexer->offset++;
            }
        }
    }

    return make(lexer, is_float ? KEST_TOK_FLOAT : KEST_TOK_INT, start);
}

static KestToken scan_string(KestLexer *lexer, uint32_t start) {
    lexer->offset++;
    // A hole may hold a string of its own, so the quote that ends this one is
    // the one found outside every brace.
    uint32_t depth = 0;

    while (true) {
        char c = at(lexer, 0);
        if (c == '"' && depth == 0) {
            lexer->offset++;
            return make(lexer, KEST_TOK_STRING, start);
        }
        if (c == '\0' || c == '\n') {
            kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0101",
                           span_from(start, start + 1),
                           "string is not terminated");
            kest_diags_suggest(lexer->diags, "add a closing `\"`");
            return make(lexer, KEST_TOK_ERROR, start);
        }
        if (c == '\\') {
            char escape = at(lexer, 1);
            const Escape *known = escape == '\0' ? NULL : escape_named(escape);
            if (known == NULL) {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0103",
                               span_from(lexer->offset, lexer->offset + 2),
                               "unknown escape sequence `\\%c`", escape);
                kest_diags_suggest(lexer->diags, "known escapes are %s",
                                   escapes_written(lexer->diags->arena));
                lexer->offset += 2;
                continue;
            }
            lexer->offset += known->reads
                                 ? scan_character_escape(lexer, lexer->offset)
                                 : 2;
            continue;
        }
        // A byte that ends a line on another machine, written inside text as
        // itself. It is a byte like any other once the program runs, and it
        // is one nobody reading the file can see: two pieces of text that are
        // not the same look the same, and a file that crossed machines has
        // one in it without anybody having written it.
        if (c == '\r') {
            kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0109",
                           span_from(lexer->offset, lexer->offset + 1),
                           "a carriage return inside text, written as itself");
            kest_diags_suggest(lexer->diags,
                               "write `\\r`, which is the same byte and can "
                               "be read");
        }
        if (c == '{') {
            depth++;
        } else if (c == '}' && depth > 0) {
            depth--;
        }
        lexer->offset++;
    }
}

// Skips spaces, tabs and comments. Line breaks are left for the caller,
// because whether they matter depends on the previous token.
static void skip_blanks(KestLexer *lexer) {
    while (true) {
        char c = at(lexer, 0);
        if (c == ' ' || c == '\t' || c == '\r') {
            lexer->offset++;
        } else if (c == '/' && at(lexer, 1) == '/') {
            // Not inside a hole. A hole is code written inside text, and the
            // formatter prints it from what it means rather than copying it,
            // so a comment in one is a comment nothing can put back. Nothing
            // can read it either: at the level of the file the whole string
            // is one token, so no tool is told there is a comment there. A
            // comment nobody can read and nothing can keep is not a comment.
            // See D392.
            uint32_t said = lexer->offset;
            while (at(lexer, 0) != '\n' && at(lexer, 0) != '\r' &&
                   at(lexer, 0) != '\0' &&
                   !(lexer->in_hole && at(lexer, 0) == '}')) {
                lexer->offset++;
            }
            if (lexer->in_hole) {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0111",
                               span_from(said, lexer->offset),
                               "a comment inside a hole");
                kest_diags_suggest(lexer->diags,
                                   "a hole holds code and is written back "
                                   "from what it means, so a comment in one "
                                   "is kept by nothing and read by nobody: "
                                   "write it above the line");
            }
        } else {
            return;
        }
    }
}

static KestToken kest_lexer_next(KestLexer *lexer) {
    kest_diags_work(lexer->diags, 1);
    while (true) {
        skip_blanks(lexer);

        uint32_t start = lexer->offset;
        char c = at(lexer, 0);

        if (c == '\n') {
            lexer->offset++;
            if (lexer->bracket_depth == 0 &&
                kest_lexer_ends_statement(lexer->previous)) {
                return make(lexer, KEST_TOK_NEWLINE, start);
            }
            continue;
        }

        if (c == '\0') {
            return make(lexer, KEST_TOK_EOF, start);
        }

        if (is_ident_start(c)) {
            return scan_ident(lexer, start);
        }

        if (is_digit(c)) {
            return scan_number(lexer, start);
        }

        // `'a'` is one byte written the way it reads. Text is its bytes
        // (D021), so this is not a character type: it is a `u8` and anything
        // that is not exactly one byte is refused.
        if (c == '\'') {
            lexer->offset++;
            while (at(lexer, 0) != '\'' && at(lexer, 0) != '\n' &&
                   at(lexer, 0) != '\0') {
                if (at(lexer, 0) == '\\' && at(lexer, 1) != '\0') {
                    // The same escapes text has, said the same way: a byte
                    // written on its own and a byte written in a piece of
                    // text are one spelling, and one spelling is one list.
                    const Escape *known = escape_named(at(lexer, 1));
                    if (known == NULL) {
                        kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR,
                                       "K0103",
                                       span_from(lexer->offset,
                                                 lexer->offset + 2),
                                       "unknown escape sequence `\\%c`",
                                       at(lexer, 1));
                        kest_diags_suggest(lexer->diags,
                                           "known escapes are %s",
                                           escapes_written(
                                               lexer->diags->arena));
                    }
                    lexer->offset += known != NULL && known->reads
                                         ? scan_character_escape(lexer,
                                                                 lexer->offset)
                                               - 1
                                         : 1;
                } else if (at(lexer, 0) == '\r') {
                    kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0109",
                                   span_from(lexer->offset, lexer->offset + 1),
                                   "a carriage return inside text, written as "
                                   "itself");
                    kest_diags_suggest(lexer->diags,
                                       "write `\\r`, which is the same byte "
                                       "and can be read");
                }
                lexer->offset++;
            }
            if (at(lexer, 0) != '\'') {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0106",
                               span_from(start, lexer->offset),
                               "this byte has no closing quote");
                return make(lexer, KEST_TOK_ERROR, start);
            }
            lexer->offset++;
            return make(lexer, KEST_TOK_BYTE, start);
        }

        if (c == '"') {
            return scan_string(lexer, start);
        }

        lexer->offset++;
        char next = at(lexer, 0);

        switch (c) {
        case '(':
            lexer->bracket_depth++;
            return make(lexer, KEST_TOK_LPAREN, start);
        case '[':
            lexer->bracket_depth++;
            return make(lexer, KEST_TOK_LBRACKET, start);
        case ')':
            if (lexer->bracket_depth > 0) {
                lexer->bracket_depth--;
            }
            return make(lexer, KEST_TOK_RPAREN, start);
        case ']':
            if (lexer->bracket_depth > 0) {
                lexer->bracket_depth--;
            }
            return make(lexer, KEST_TOK_RBRACKET, start);
        case '{':
            return make(lexer, KEST_TOK_LBRACE, start);
        case '}':
            return make(lexer, KEST_TOK_RBRACE, start);
        case ',':
            return make(lexer, KEST_TOK_COMMA, start);
        case '.':
            if (next == '.') {
                lexer->offset++;
                return make(lexer, KEST_TOK_DOTDOT, start);
            }
            return make(lexer, KEST_TOK_DOT, start);
        case ':':
            return make(lexer, KEST_TOK_COLON, start);
        case '?':
            return make(lexer, KEST_TOK_QUESTION, start);
        case '%':
            return make(lexer, KEST_TOK_PERCENT, start);
        case '=':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_EQEQ, start);
            }
            return make(lexer, KEST_TOK_EQ, start);
        case '!':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_BANGEQ, start);
            }
            return make(lexer, KEST_TOK_BANG, start);
        case '<':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_LTEQ, start);
            }
            if (next == '<') {
                lexer->offset++;
                return make(lexer, KEST_TOK_LTLT, start);
            }
            return make(lexer, KEST_TOK_LT, start);
        case '>':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_GTEQ, start);
            }
            if (next == '>') {
                lexer->offset++;
                return make(lexer, KEST_TOK_GTGT, start);
            }
            return make(lexer, KEST_TOK_GT, start);
        case '+':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_PLUSEQ, start);
            }
            return make(lexer, KEST_TOK_PLUS, start);
        case '-':
            if (next == '>') {
                lexer->offset++;
                return make(lexer, KEST_TOK_ARROW, start);
            }
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_MINUSEQ, start);
            }
            return make(lexer, KEST_TOK_MINUS, start);
        case '*':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_STAREQ, start);
            }
            return make(lexer, KEST_TOK_STAR, start);
        case '/':
            if (next == '*') {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0106",
                               span_from(start, start + 2),
                               "block comments are not part of the language");
                kest_diags_suggest(lexer->diags, "use `//` on each line");
                lexer->offset++;
                return make(lexer, KEST_TOK_ERROR, start);
            }
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_SLASHEQ, start);
            }
            return make(lexer, KEST_TOK_SLASH, start);
        case '&':
            if (next == '&') {
                lexer->offset++;
                return make(lexer, KEST_TOK_AMPAMP, start);
            }
            return make(lexer, KEST_TOK_AMP, start);
        case '|':
            if (next == '|') {
                lexer->offset++;
                return make(lexer, KEST_TOK_PIPEPIPE, start);
            }
            return make(lexer, KEST_TOK_PIPE, start);
        case '^':
            return make(lexer, KEST_TOK_CARET, start);
        case '~':
            return make(lexer, KEST_TOK_TILDE, start);
        case ';':
            // Read as a token and refused where it is written. Whether a `;`
            // is a mistake depends on where it is, and that is the parser's
            // to know: inside `[f32; 16]` it separates a count.
            return make(lexer, KEST_TOK_SEMICOLON, start);
        default:
            break;
        }

        kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0102",
                       span_from(start, lexer->offset),
                       "unexpected character `%.*s`",
                       (int)(lexer->offset - start), lexer->source->text + start);
        if (lexer->source->text[start] == '\\') {
            // The mistake everybody makes once: escaping a quote inside a
            // hole. What is in one is code, so a string in it is written the
            // way a string is written anywhere.
            kest_diags_suggest(lexer->diags,
                               lexer->in_hole
                                   ? "a hole holds code, so a string inside "
                                     "one needs no escape: `{f(\"x\")}`"
                                   : "an escape is written inside text, and "
                                     "this is not inside any");
        }
        return make(lexer, KEST_TOK_ERROR, start);
    }
}

static KestToken *lex_from(KestArena *arena, KestLexer *lexer, uint32_t end,
                           uint32_t *count);

typedef struct {
    uint32_t from;
    uint32_t to;
    const char *what;
} Unseen;

// Characters that are in a file without being on the screen. A name may hold
// any character the writer's language has, and none of these are one: they
// either look like a space and are not one, or take no room at all, or say
// which way the rest of the line is to be read.
static const Unseen UNSEEN[] = {
    {0x00a0, 0x00a0, "a space that is not the space"},
    {0x00ad, 0x00ad, "a hyphen that is not shown"},
    {0x1680, 0x1680, "a space that is not the space"},
    {0x2000, 0x200a, "a space that is not the space"},
    {0x200b, 0x200d, "a mark with no width"},
    {0x200e, 0x200f, "a mark saying which way to read"},
    {0x2028, 0x2029, "a line break that no line ends with"},
    {0x202a, 0x202e, "a mark saying which way to read"},
    {0x202f, 0x202f, "a space that is not the space"},
    {0x205f, 0x205f, "a space that is not the space"},
    {0x2060, 0x2064, "a mark with no width"},
    {0x2066, 0x2069, "a mark saying which way to read"},
    {0x3000, 0x3000, "a space that is not the space"},
    {0xfeff, 0xfeff, "a mark with no width"},
};

static const char *unseen_what(uint32_t code) {
    for (size_t i = 0; i < sizeof(UNSEEN) / sizeof(UNSEEN[0]); i++) {
        if (code >= UNSEEN[i].from && code <= UNSEEN[i].to) {
            return UNSEEN[i].what;
        }
    }
    return NULL;
}

// How many bytes the character starting at `at` is written in, and which
// character it is. Nought is a byte that starts no character: a lead byte with
// the wrong bits, a sequence cut short, a character written in more bytes than
// it needs, half of a surrogate pair, or a number past the last character
// there is.
static uint32_t decoded(const char *text, uint32_t length, uint32_t at,
                        uint32_t *code) {
    unsigned char lead = (unsigned char)text[at];
    uint32_t width = 0;
    uint32_t value = 0;
    if ((lead & 0xe0) == 0xc0) {
        width = 2;
        value = lead & 0x1fu;
    } else if ((lead & 0xf0) == 0xe0) {
        width = 3;
        value = lead & 0x0fu;
    } else if ((lead & 0xf8) == 0xf0) {
        width = 4;
        value = lead & 0x07u;
    } else {
        return 0;
    }

    if (at + width > length) {
        return 0;
    }
    for (uint32_t i = 1; i < width; i++) {
        unsigned char next = (unsigned char)text[at + i];
        if ((next & 0xc0) != 0x80) {
            return 0;
        }
        value = (value << 6) | (next & 0x3fu);
    }

    static const uint32_t LEAST[] = {0, 0, 0x80, 0x800, 0x10000};
    if (value < LEAST[width] || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) {
        return 0;
    }

    *code = value;
    return width;
}

// What the whole file is made of, before anything is made of the file. A
// program is read by people as well as by this, and the two have to be reading
// the same thing: a byte that is no character at all, or a character that is
// in the file without being on the screen, is where they stop.
static void check_text(const KestSource *source, KestDiags *diags) {
    uint32_t at = 0;
    while (at < source->length) {
        if ((unsigned char)source->text[at] < 0x80) {
            at++;
            continue;
        }

        uint32_t code = 0;
        uint32_t width = decoded(source->text, (uint32_t)source->length, at,
                                 &code);
        if (width == 0) {
            KestSpan span = {at, 1};
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0107", span,
                           "the byte `0x%02x` starts no character",
                           (unsigned char)source->text[at]);
            kest_diags_suggest(diags,
                               "a file this language reads is UTF-8 throughout");
            at++;
            continue;
        }

        const char *what = unseen_what(code);
        if (what != NULL) {
            KestSpan span = {at, width};
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0108", span,
                           "`U+%04X` is %s", code, what);
            if (code == 0xfeff && at == 0) {
                kest_diags_suggest(diags,
                                   "a file here is UTF-8 already: save it "
                                   "without the mark");
            } else {
                kest_diags_suggest(diags, "take it out: what a file looks "
                                          "like is what it is");
            }
        }
        at += width;
    }
}


// Whether a run of bytes is UTF-8, and where it stops being so. Text in this
// language is UTF-8 -- a file is held to it by `K0107` and this is the same
// question asked of bytes that arrive at runtime, from an array or from a
// host. It is the one decoder there is, so a byte the compiler refuses and a
// byte the machine refuses are the same byte. See D971.
bool kest_utf8_whole(const char *bytes, uint32_t length, uint32_t *bad) {
    uint32_t at = 0;
    while (at < length) {
        // Eight bytes of which none has its top bit set are eight characters,
        // which is most of any text a program makes. See D1169.
        if (length - at >= 8) {
            uint64_t eight;
            memcpy(&eight, bytes + at, 8);
            if ((eight & UINT64_C(0x8080808080808080)) == 0) {
                at += 8;
                continue;
            }
        }
        if ((unsigned char)bytes[at] < 0x80) {
            at++;
            continue;
        }
        uint32_t code = 0;
        uint32_t width = decoded(bytes, length, at, &code);
        if (width == 0) {
            if (bad != NULL) {
                *bad = at;
            }
            return false;
        }
        at += width;
    }
    return true;
}

KestToken *kest_lex_range(KestArena *arena, const KestSource *source,
                          KestDiags *diags, uint32_t start, uint32_t end,
                          uint32_t *count) {
    KestLexer lexer;
    kest_lexer_init(&lexer, source, diags);
    lexer.offset = start;
    // The only range anything asks for is the inside of a hole.
    lexer.in_hole = true;
    return lex_from(arena, &lexer, end, count);
}

KestToken *kest_lex_all(KestArena *arena, const KestSource *source,
                        KestDiags *diags, uint32_t *count) {
    check_text(source, diags);

    KestLexer lexer;
    kest_lexer_init(&lexer, source, diags);
    return lex_from(arena, &lexer, (uint32_t)source->length, count);
}

static uint32_t LAST_ROOM;

uint32_t kest_lex_room(void) {
    return LAST_ROOM;
}

static KestToken *lex_from(KestArena *arena, KestLexer *lexer, uint32_t end,
                           uint32_t *count) {

    KestToken *tokens = NULL;
    uint32_t used = 0;
    uint32_t capacity = 0;
    // What the bracket count was where each unclosed brace opened, innermost
    // last, and how many of those there are.
    uint32_t held[160];
    uint32_t braces = 0;

    while (true) {
        if (used == capacity) {
            // A quarter more, not twice as much. Doubling is what an array
            // that copies itself grows by, because the copy has to be paid
            // for less often than it happens; this one does not copy — D746
            // made it the last thing in its arena so it grows where it
            // stands — so the factor is free to be chosen for the room it
            // leaves rather than for the copy it is not making. Twice as much
            // left a third of every token array never written to, which is
            // 15094 slots of the 46592 this tree asks for. A quarter more is
            // an eighth, and the number of extensions stays a logarithm
            // rather than becoming a count of the file. See D783.
            uint32_t grown =
                capacity == 0 ? 256 : capacity + capacity / 4;
            // And never room for more tokens than there are bytes left to
            // make them out of: the shortest token there is is one byte, so
            // what is still to come cannot outnumber what is still to be
            // read. Near the end of a file this is what is asked for rather
            // than the quarter, which is why a file of nineteen hundred
            // tokens no longer takes room for two thousand three hundred.
            uint32_t left = end > lexer->offset ? end - lexer->offset : 0;
            uint32_t most = used + left + 1;
            if (grown > most && most > capacity) {
                grown = most;
            }
            // Nothing else is handed out while a file is being read, so this
            // array is the last thing in the arena and can be made bigger
            // where it stands. What that saves is every size it passed
            // through: a file of two thousand tokens grew through two hundred
            // and fifty-six, five hundred and twelve, a thousand and two
            // thousand, and kept all four. See D746.
            KestToken *moved =
                capacity == 0
                    ? NULL
                    : kest_arena_extend(arena, tokens,
                                        sizeof(KestToken) * capacity,
                                        sizeof(KestToken) * grown);
            if (moved == NULL) {
                // A diagnostic was recorded since the last token, so something
                // else is the last thing handed out. Then it is what it was
                // before: take a new one and copy.
                moved = KEST_ARENA_ARRAY(arena, KestToken, grown);
                if (moved == NULL) {
                    return NULL;
                }
                if (used > 0) {
                    memcpy(moved, tokens, sizeof(KestToken) * used);
                }
            }
            tokens = moved;
            capacity = grown;
        }

        if (lexer->offset >= end) {
            KestSpan stop = {end, 0};
            KestToken done = {KEST_TOK_EOF, stop};
            tokens[used++] = done;
            break;
        }
        tokens[used] = kest_lexer_next(lexer);
        // A block holds statements, so a line inside one ends where it is
        // written even when the block is inside brackets: what the count of
        // brackets is for is a line break inside `(` and `[`, and a brace puts
        // that count aside until the brace that closes it. The field beside
        // the count has said so since it was written and nothing did it, so
        // `f(match d {` ran the arms together and the parser refused the
        // second one. See D1085.
        //
        // A brace inside brackets is a brace inside an expression, and 128
        // expressions one inside another is the most this language parses, so
        // a file with more of these than there is room for here is a file
        // already refused for its depth.
        if (tokens[used].kind == KEST_TOK_LBRACE) {
            if (braces < (uint32_t)(sizeof held / sizeof held[0])) {
                held[braces] = lexer->bracket_depth;
            }
            braces++;
            lexer->bracket_depth = 0;
        } else if (tokens[used].kind == KEST_TOK_RBRACE && braces > 0) {
            braces--;
            lexer->bracket_depth =
                braces < (uint32_t)(sizeof held / sizeof held[0])
                    ? held[braces]
                    : 0;
        }
        if (tokens[used++].kind == KEST_TOK_EOF) {
            break;
        }
    }

    *count = used;
    LAST_ROOM = capacity;
    return tokens;
}

uint64_t kest_token_integer(const char *text, size_t length, bool *overflow) {
    uint64_t value = 0;
    uint64_t base = 10;
    size_t at = 0;
    if (length > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        at = 2;
    }
    *overflow = false;
    for (; at < length; at++) {
        char c = text[at];
        uint64_t digit = c <= '9' ? (uint64_t)(c - '0')
                                  : (uint64_t)((c | 0x20) - 'a' + 10);
        if (value > (UINT64_MAX - digit) / base) {
            *overflow = true;
            return UINT64_MAX;
        }
        value = value * base + digit;
    }
    return value;
}
