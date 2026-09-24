#ifndef KEST_LEXER_H
#define KEST_LEXER_H

#include "diag.h"

typedef enum {
    KEST_TOK_EOF,
    // A statement terminator. Emitted for a line break only where a statement
    // could actually have ended; see kest_lexer_next.
    KEST_TOK_NEWLINE,

    KEST_TOK_IDENT,
    KEST_TOK_INT,
    KEST_TOK_FLOAT,
    KEST_TOK_STRING,
    KEST_TOK_BYTE,

    KEST_TOK_BREAK,
    KEST_TOK_CONST,
    KEST_TOK_CONTINUE,
    KEST_TOK_DEFER,
    KEST_TOK_ELSE,
    KEST_TOK_ENUM,
    KEST_TOK_EXTERN,
    KEST_TOK_FALSE,
    KEST_TOK_FN,
    KEST_TOK_FOR,
    KEST_TOK_IF,
    KEST_TOK_IMPORT,
    KEST_TOK_IN,
    KEST_TOK_LET,
    KEST_TOK_MATCH,
    KEST_TOK_MODULE,
    KEST_TOK_NONE,
    KEST_TOK_RETURN,
    KEST_TOK_STRUCT,
    KEST_TOK_TRUE,
    KEST_TOK_WHILE,

    KEST_TOK_LPAREN,
    KEST_TOK_RPAREN,
    KEST_TOK_LBRACE,
    KEST_TOK_RBRACE,
    KEST_TOK_LBRACKET,
    KEST_TOK_RBRACKET,
    KEST_TOK_COMMA,
    KEST_TOK_SEMICOLON,
    KEST_TOK_DOT,
    KEST_TOK_DOTDOT,
    KEST_TOK_COLON,
    KEST_TOK_QUESTION,
    KEST_TOK_ARROW,

    KEST_TOK_EQ,
    KEST_TOK_EQEQ,
    KEST_TOK_BANGEQ,
    KEST_TOK_LT,
    KEST_TOK_LTEQ,
    KEST_TOK_GT,
    KEST_TOK_GTEQ,
    KEST_TOK_PLUS,
    KEST_TOK_MINUS,
    KEST_TOK_STAR,
    KEST_TOK_SLASH,
    KEST_TOK_PERCENT,
    KEST_TOK_BANG,
    KEST_TOK_AMPAMP,
    KEST_TOK_PIPEPIPE,
    KEST_TOK_AMP,
    KEST_TOK_PIPE,
    KEST_TOK_CARET,
    KEST_TOK_TILDE,
    KEST_TOK_LTLT,
    KEST_TOK_GTGT,
    KEST_TOK_PLUSEQ,
    KEST_TOK_MINUSEQ,
    KEST_TOK_STAREQ,
    KEST_TOK_SLASHEQ,

    // Produced where a diagnostic was already recorded, so the parser can keep
    // going without reporting the same byte twice.
    KEST_TOK_ERROR,
} KestTokenKind;

typedef struct {
    KestTokenKind kind;
    KestSpan span;
} KestToken;

typedef struct {
    const KestSource *source;
    KestDiags *diags;
    uint32_t offset;
    // Line breaks inside brackets continue the statement, so they are not
    // terminators. Braces do not count: a block holds statements.
    uint32_t bracket_depth;
    KestTokenKind previous;
    // Reading the inside of a hole in a string, which is code and not text.
    // The two are lexed by the same thing, so what is wrong with a character
    // depends on which it is reading.
    bool in_hole;
} KestLexer;

// Tokenises the whole source into arena memory. The parser needs to look
// further ahead than one token, and a file's token count is bounded by its
// size, so there is nothing to stream.
// How many tokens the last array had room for, against how many were put in
// it. A chunk says `room` beside `bytes` for the same reason: a reader with
// both can see what was taken against what was wanted. See D783.
uint32_t kest_lex_room(void);

KestToken *kest_lex_all(KestArena *arena, const KestSource *source,
                        KestDiags *diags, uint32_t *count);

// Tokenises one region of the source. The offsets a token carries are into
// the whole file either way, so what comes back from inside a string reports
// at the place it was written.
KestToken *kest_lex_range(KestArena *arena, const KestSource *source,
                          KestDiags *diags, uint32_t start, uint32_t end,
                          uint32_t *count);

// What a string literal holds: the characters between its quotes with the
// escapes read. The span is the content, without them.
// Whether a run of bytes is UTF-8 throughout, and the offset of the first byte
// that is not where it is not. Text is UTF-8: a source file is held to it while
// it is read and bytes arriving at runtime are held to it where they arrive.
bool kest_utf8_whole(const char *bytes, uint32_t length, uint32_t *bad);

const char *kest_literal_text(KestArena *arena, const KestSource *source,
                              KestSpan span, size_t *length);

// How far the escape written at this offset reaches, counted from the
// backslash it begins at. Two for every one that stands for a byte, and more
// for `\u{...}`, which carries a character's number after it. Anything that
// walks written text looking for what it is made of asks here rather than
// stepping over one character: the brace after a `\u` is not a hole, and the
// walk that read it as one made `"\u{645}"` a piece of text with the number
// 1605 written into it. See D1056.
uint32_t kest_escape_width(const KestSource *source, uint32_t at);

// What a number literal is worth, as a double. An `f32` is narrowed by
// whatever wanted it, because the narrowing is about the type and not about
// the spelling.
double kest_literal_real(const KestSource *source, KestSpan span);

// The value an integer literal spells. Sets `overflow` when it does not fit
// in sixty-four bits, which is the widest anything here can be.
uint64_t kest_token_integer(const char *text, size_t length, bool *overflow);

// The spelling used in diagnostics: `fn`, `identifier`, `end of file`.
const char *kest_token_name(KestTokenKind kind);

// And the same name without the backticks it carries for diagnostics, which is
// what everything that writes a program back or prints one for a reader wants.
// The buffer is the caller's, so nothing here holds state between calls;
// `KEST_TOKEN_NAME_ROOM` is room for the longest of them. Three places walked
// the name taking the backticks out, in three files, and three walks over one
// question are three answers the day any of them moves. See D771.
#define KEST_TOKEN_NAME_ROOM 16
const char *kest_token_bare(KestTokenKind kind, char *into, size_t room);

// Whether a line break after a token of this kind ends the statement. A break
// after an operator, an opening bracket or a comma carries on, because the
// statement cannot have finished there. `>` is the one operator this is false
// of, since `ref<Npc>` ends in one, and the formatter asks here rather than
// keeping a list of its own: what may end a line and where a line may be
// broken are the same question asked twice.
bool kest_lexer_ends_statement(KestTokenKind kind);

// The keyword this word is one or two mistakes from, or NULL when it is near
// none of them. `retrun` is `return` and `x` is nothing.
const char *kest_nearest_keyword(const char *name, size_t length);

// Every comment in a file, in the order they were written, and how many there
// are — which is the answer whether or not there was room for them all. A
// comment is not a token and the lexer steps over one; this is the one place
// that says where they were.
uint32_t kest_comments(const KestSource *source, KestSpan *into,
                       uint32_t room);

#endif
