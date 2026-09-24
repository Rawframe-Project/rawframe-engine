#include "parser.h"

#include <stdarg.h>
#include <string.h>

typedef struct {
    KestArena *arena;
    const KestSource *source;
    KestDiags *diags;
    KestToken *tokens;
    uint32_t count;
    uint32_t position;
    // Set when an error is reported, cleared at a recovery point. One broken
    // construct reports once rather than at every token it goes on to confuse.
    bool recovering;
    // Whether the last thing this parser tried to say was said. A suggestion
    // and a note are attached to the diagnostic that came last, whoever made
    // it -- so a message held back while recovering leaves them landing on
    // somebody else's, which is a reader told the rule behind a mistake they
    // did not make. See D885.
    bool spoke;
    bool out_of_memory;
    // What the statement being parsed began with, so a message about where it
    // ended can point back at the word that started it.
    KestToken began_with;
    // Every node made for this file, counted where it is made: a tree is what
    // reading a file mostly costs, and what it is made of is this. See D641.
    uint32_t nodes;
    // How deep the expression being read is written inside others. See D645.
    uint32_t nesting;
} Parser;

// How many a list holds before it needs the arena at all. A block, an argument
// list, a set of fields: almost all of them are shorter than this, and one that
// is not is a list where a copy costs nothing beside what it holds. Sixteen
// pointers is a hundred and twenty-eight bytes of the C stack per list being
// read, and lists are read one inside another only as deep as an expression
// nests, which has a ceiling. See D745.
#define LIST_HELD 16

// A pointer list that fills up here and spills into the arena, and is handed
// over as exactly what it holds. Nothing it grew through is kept: the arena
// never gives anything back, so a list that grew in it left every size it
// passed through behind, and a list of two left eight. See D745.
typedef struct {
    void **items;
    uint32_t count;
    uint32_t capacity;
    void *held[LIST_HELD];
} List;

static void list_push(Parser *parser, List *list, void *item) {
    if (list->count == list->capacity) {
        if (list->capacity == 0) {
            list->items = list->held;
            list->capacity = LIST_HELD;
        } else {
            uint32_t capacity = list->capacity * 2;
            void **items = KEST_ARENA_ARRAY(parser->arena, void *, capacity);
            if (items == NULL) {
                parser->out_of_memory = true;
                return;
            }
            memcpy(items, list->items, sizeof(void *) * list->count);
            list->items = items;
            list->capacity = capacity;
        }
    }
    list->items[list->count++] = item;
}

// What the tree keeps, which is the list and nothing it grew through. A list
// still being read is on the C stack, so this is the one way what it holds may
// outlive the reading of it. Nothing for a list of nothing, which is what an
// empty one was before there was anywhere else to put it.
static void **list_taken(Parser *parser, List *list) {
    if (list->count == 0) {
        return NULL;
    }
    void **items = KEST_ARENA_ARRAY(parser->arena, void *, list->count);
    if (items == NULL) {
        parser->out_of_memory = true;
        // Nothing, and nothing in it. Every caller writes the count beside
        // what it was handed, and a count beside no list is a walk over
        // nothing that reads whatever is at nought.
        list->count = 0;
        return NULL;
    }
    memcpy(items, list->items, sizeof(void *) * list->count);
    return items;
}

static KestToken peek(Parser *parser) {
    return parser->tokens[parser->position];
}

static KestToken peek_at(Parser *parser, uint32_t ahead) {
    uint32_t index = parser->position + ahead;
    if (index >= parser->count) {
        index = parser->count - 1;
    }
    return parser->tokens[index];
}

static bool check(Parser *parser, KestTokenKind kind) {
    return peek(parser).kind == kind;
}


static KestToken advance(Parser *parser) {
    KestToken token = peek(parser);
    if (token.kind != KEST_TOK_EOF) {
        parser->position++;
    }
    return token;
}

static bool match(Parser *parser, KestTokenKind kind) {
    if (!check(parser, kind)) {
        return false;
    }
    parser->position++;
    return true;
}

static const char *span_text(Parser *parser, KestSpan span) {
    return kest_span_text(parser->source, span);
}

// Whether the identifier at `ahead` is spelled `word`.
static bool is_word(Parser *parser, uint32_t ahead, const char *word) {
    KestToken token = peek_at(parser, ahead);
    return token.kind == KEST_TOK_IDENT &&
           kest_word_same(word, span_text(parser, token.span),
                          token.span.length);
}

static void error_at(Parser *parser, KestSpan span, const char *code,
                     const char *format, ...) {
    if (parser->recovering) {
        parser->spoke = false;
        return;
    }
    // A parser with nothing left says nothing about what it was reading. The
    // walk out of a node that could not be made looks exactly like the walk
    // out of a file that is wrong -- a NULL where a node goes -- so what a
    // reader would be told is that a line they wrote is bad, about a machine
    // that ran out. The one thing that happened is said by whoever notices
    // the answer is no. See D748.
    if (parser->out_of_memory) {
        parser->recovering = true;
        parser->spoke = false;
        return;
    }
    // A token the lexer could not read has already been reported, by the one
    // that knows what is wrong with it. Saying something else about the same
    // place is saying it twice, and the second thing is always vaguer.
    KestToken here = peek(parser);
    if (here.kind == KEST_TOK_ERROR && here.span.offset == span.offset) {
        parser->recovering = true;
        parser->spoke = false;
        return;
    }
    parser->recovering = true;

    // The end of a file is a place, and a span with nothing in it is shown as
    // no place at all: a path with no line under it, on the one message a
    // reader most needs pointed at. So it points just past the last character
    // there is, which is where the file ran out.
    if (span.length == 0 && parser->source->length > 0) {
        uint32_t last = (uint32_t)parser->source->length;
        while (last > 0 && (parser->source->text[last - 1] == '\n' ||
                            parser->source->text[last - 1] == '\r')) {
            last--;
        }
        span.offset = last;
        span.length = 1;
    }

    uint32_t before = parser->diags->count;
    va_list args;
    va_start(args, format);
    kest_diags_addv(parser->diags, KEST_SEVERITY_ERROR, code, span, format,
                    args);
    va_end(args);
    parser->spoke = parser->diags->count > before;
}

// The rule behind a refusal, said to whoever made the refusal. It goes to the
// diagnostic that came last, so it is only worth saying when the message it
// belongs under is the one this parser has just made.
static void suggest(Parser *parser, const char *format, ...) {
    if (!parser->spoke) {
        return;
    }
    va_list args;
    va_start(args, format);
    kest_diags_suggestv(parser->diags, format, args);
    va_end(args);
}

// Whether what comes next is a condition written inside brackets and nothing
// else. `if (x < 3) { }` means what `if x < 3 { }` means, and the formatter
// takes the brackets away — so a file written the first way is a second
// spelling of one thing, and this parser accepts one. What says the brackets
// are the whole of it is what follows the one that closes them: a brace, or
// the arrow of an `if` that gives a value. Anything else and they are a
// grouping inside a bigger condition, which is a reader's to write.
static bool wrapped_whole(Parser *parser) {
    if (!check(parser, KEST_TOK_LPAREN)) {
        return false;
    }
    uint32_t deep = 0;
    for (uint32_t at = 0; at < parser->count; at++) {
        KestTokenKind kind = peek_at(parser, at).kind;
        if (kind == KEST_TOK_LPAREN) {
            deep++;
        } else if (kind == KEST_TOK_RPAREN) {
            deep--;
            if (deep == 0) {
                KestTokenKind after = peek_at(parser, at + 1).kind;
                return after == KEST_TOK_LBRACE || after == KEST_TOK_ARROW;
            }
        } else if (kind == KEST_TOK_EOF) {
            return false;
        }
    }
    return false;
}

// `%=` and its like. Four of them are written — `+=`, `-=`, `*=` and `/=` —
// and the rest are not, because a fifth that appears once in a file is written
// out. What a reader who writes one gets otherwise is the parser meeting an `=`
// where a value belongs, which says where it stopped and not what is wrong.
static const char *no_compound(Parser *parser, uint32_t *at_out) {
    uint32_t deep = 0;
    for (uint32_t at = 0; at < parser->count; at++) {
        KestTokenKind kind = peek_at(parser, at).kind;
        if (kind == KEST_TOK_LPAREN || kind == KEST_TOK_LBRACKET) {
            deep++;
            continue;
        }
        if (kind == KEST_TOK_RPAREN || kind == KEST_TOK_RBRACKET) {
            deep--;
            continue;
        }
        // One statement's worth. A brace begins a block and a line end ends a
        // statement, and neither is somewhere this could still be about.
        if (kind == KEST_TOK_NEWLINE || kind == KEST_TOK_EOF ||
            kind == KEST_TOK_LBRACE) {
            return NULL;
        }
        if (deep != 0 || peek_at(parser, at + 1).kind != KEST_TOK_EQ) {
            continue;
        }
        *at_out = at;
        switch (kind) {
        case KEST_TOK_PERCENT:
            return "%";
        case KEST_TOK_AMP:
            return "&";
        case KEST_TOK_PIPE:
            return "|";
        case KEST_TOK_CARET:
            return "^";
        case KEST_TOK_LTLT:
            return "<<";
        case KEST_TOK_GTGT:
            return ">>";
        default:
            break;
        }
    }
    return NULL;
}

// Said where the brackets are, because that is what to take away.
static void refuse_wrapped(Parser *parser, const char *what) {
    error_at(parser, peek(parser).span, "K0213",
             "a condition is written without brackets round the whole of it");
    suggest(parser, "write `%s x < 3 {`", what);
}

static bool expect(Parser *parser, KestTokenKind kind) {
    if (match(parser, kind)) {
        return true;
    }
    KestToken found = peek(parser);
    error_at(parser, found.span, "K0201", "expected %s, found %s",
             kest_token_name(kind), kest_token_name(found.kind));
    return false;
}

static void skip_newlines(Parser *parser) {
    while (match(parser, KEST_TOK_NEWLINE)) {
    }
}

static bool starts_statement(KestTokenKind kind) {
    switch (kind) {
    case KEST_TOK_LET:
    case KEST_TOK_IF:
    case KEST_TOK_WHILE:
    case KEST_TOK_FOR:
    case KEST_TOK_RETURN:
    case KEST_TOK_BREAK:
    case KEST_TOK_CONTINUE:
        return true;
    default:
        return false;
    }
}

static bool starts_declaration(KestTokenKind kind) {
    switch (kind) {
    case KEST_TOK_MODULE:
    case KEST_TOK_IMPORT:
    case KEST_TOK_CONST:
    case KEST_TOK_STRUCT:
    case KEST_TOK_FN:
    case KEST_TOK_EXTERN:
        return true;
    default:
        return false;
    }
}

// Discards tokens until the next place a statement could begin, so the rest of
// the block is still parsed and still reports its own errors.
//
// A line that opened a block is followed to the brace that closes it. That
// block belonged to the statement that failed, and reading it as statements of
// the block around it puts every brace after it out of step: one mistake in an
// `if let` line became a second message about a `let` three lines down, saying
// a file holds `module`, `import` and `fn` — in the middle of a function.
static void recover_statement(Parser *parser) {
    uint32_t depth = 0;
    while (!check(parser, KEST_TOK_EOF)) {
        if (check(parser, KEST_TOK_LBRACE)) {
            depth++;
            advance(parser);
            continue;
        }
        if (check(parser, KEST_TOK_RBRACE)) {
            if (depth == 0) {
                break;
            }
            depth--;
            advance(parser);
            continue;
        }
        if (check(parser, KEST_TOK_NEWLINE)) {
            advance(parser);
            if (depth == 0) {
                break;
            }
            continue;
        }
        if (depth == 0 && starts_statement(peek(parser).kind)) {
            break;
        }
        advance(parser);
    }
    parser->recovering = false;
}

static void recover_declaration(Parser *parser) {
    while (!check(parser, KEST_TOK_EOF) &&
           !starts_declaration(peek(parser).kind)) {
        advance(parser);
    }
    parser->recovering = false;
}

// Recovering from a refusal in a loop, which has to leave the loop further on
// than `was` or the loop is not a loop. Recovery on its own is allowed to
// stand still: it stops at whatever could begin the next statement, and a word
// this language keeps is one of those wherever it is written. `break` where a
// field name belongs is refused, recovery stops on it because a statement
// could begin there, the loop asks for a field again at the same token, and
// that is the whole of the program from then on — one more copy of one message
// at a time, until the host has no memory left to give it. Six lines did that.
//
// So a pass that has eaten nothing eats one and recovers from where that
// leaves it, which also puts the rest of the line where it belongs: whatever
// follows a name that cannot be one is not a second mistake. See D841.
static void recover_from(Parser *parser, uint32_t was) {
    recover_statement(parser);
    if (parser->position == was) {
        advance(parser);
        recover_statement(parser);
    }
}

// A statement ends at a line break, or at the brace that closes its block.
static void end_statement(Parser *parser) {
    if (match(parser, KEST_TOK_NEWLINE)) {
        parser->recovering = false;
        return;
    }
    if (check(parser, KEST_TOK_RBRACE) || check(parser, KEST_TOK_EOF)) {
        parser->recovering = false;
        return;
    }
    KestToken found = peek(parser);
    if (found.kind == KEST_TOK_SEMICOLON) {
        error_at(parser, found.span, "K0105",
                 "statements are not separated by `;`");
        suggest(parser, "remove it; a line break ends a "
                        "statement");
        advance(parser);
        recover_statement(parser);
        return;
    }
    error_at(parser, found.span, "K0201", "expected end of line, found %s",
             kest_token_name(found.kind));
    // A ternary, which is what somebody writes who has met a language with
    // one. The statement before it was whole, so what is left is a `?` where a
    // line should have ended — and `?` means something else here, so a reader
    // shown only the token is being told their `?` is in the wrong place
    // rather than that there is none. The reference says there is no ternary;
    // this is the reader who has not read it yet. See D884.
    if (found.kind == KEST_TOK_QUESTION) {
        suggest(parser,
                "there is no ternary here and `?` means optional: "
                "an `if` gives a value with `->`, as "
                "`if c -> a else -> b`");
    }
    // `Thing { x: 1.0 }`, a struct built the way a language with struct
    // literals builds one. Here it is built by position, the way a function
    // is called, so the brace is where the line ended. See D1148.
    if (found.kind == KEST_TOK_LBRACE && parser->position >= 1 &&
        parser->tokens[parser->position - 1].kind == KEST_TOK_IDENT &&
        peek_at(parser, 1).kind == KEST_TOK_IDENT &&
        peek_at(parser, 2).kind == KEST_TOK_COLON) {
        KestSpan named = parser->tokens[parser->position - 1].span;
        suggest(parser,
                "a struct is built by position, in the order its fields are "
                "declared: `%.*s(...)`",
                (int)named.length, span_text(parser, named));
    }
    // A statement that begins with a word this language nearly has is a
    // misspelt keyword, and the message above is about the token after it —
    // which is the one thing in the line that is not wrong. So the word is
    // pointed at as well.
    if (parser->spoke && parser->began_with.kind == KEST_TOK_IDENT) {
        const char *nearly = kest_nearest_keyword(
            span_text(parser, parser->began_with.span),
            parser->began_with.span.length);
        if (nearly != NULL) {
            kest_diags_note(parser->diags, parser->source,
                            parser->began_with.span, "did you mean `%s`?",
                            nearly);
        }
    }
    recover_statement(parser);
}

static KestSpan span_between(KestSpan from, KestSpan to) {
    KestSpan span = {from.offset, to.offset + to.length - from.offset};
    return span;
}

static KestSpan current_span(Parser *parser) {
    return peek(parser).span;
}

// A dotted path such as `game.player`, reported as one name.
static KestSpan parse_path(Parser *parser) {
    KestSpan start = current_span(parser);
    if (!expect(parser, KEST_TOK_IDENT)) {
        return start;
    }
    KestSpan end = parser->tokens[parser->position - 1].span;
    while (check(parser, KEST_TOK_DOT) &&
           peek_at(parser, 1).kind == KEST_TOK_IDENT) {
        advance(parser);
        end = advance(parser).span;
    }
    return span_between(start, end);
}

// The promises this language has, in the one place that says which they are: a
// word, and whether it is written after `no.`. Two are about what a body does
// not do -- `no.alloc` about the heap and `no.host` about calling out of the
// program -- and the third is about what it does. A list rather than a branch
// each, because a promise is a thing this language has and a fourth one is a
// row here rather than a rewrite of what reads them. `check-tables.sh` holds
// it to what the header names, so a promise a program can write and a host
// cannot ask about is a check that fails. See D857 and D942.
static const struct {
    const char *word;
    bool after_no;
} PROMISES[] = {
    {"alloc", true},
    {"host", true},
    {"deterministic", false},
};

#define PROMISE_COUNT (sizeof(PROMISES) / sizeof(PROMISES[0]))

// How they are written together, for the message that says what there is. Made
// from the list above rather than written beside it, because the two would
// disagree the day one of them changed and the wrong one is the one a reader
// is shown.
static void promise_list(char *out, size_t room) {
    size_t written = 0;
    for (size_t at = 0; at < PROMISE_COUNT && written + 1 < room; at++) {
        const char *between = at == 0                    ? ""
                              : at + 1 == PROMISE_COUNT  ? " and "
                                                         : ", ";
        int said = snprintf(out + written, room - written, "%s`%s%s`", between,
                            PROMISES[at].after_no ? "no." : "",
                            PROMISES[at].word);
        if (said < 0) {
            break;
        }
        written += (size_t)said;
    }
}

// What is said about anything else written where a promise goes.
//
// A word that is not one of them went unread until D852: the parser found no
// promise, went looking for a body, found an identifier and said `expected `{``.
// `no` and a dot is somebody writing a promise whatever follows it, so what
// follows it is read and answered for. See D852 and D853.
static void match_promises(Parser *parser, bool *no_alloc, bool *no_host,
                           bool *deterministic) {
    bool *written[] = {no_alloc, no_host, deterministic};
    _Static_assert(sizeof(written) / sizeof(written[0]) == PROMISE_COUNT,
                   "one flag per promise this language has");
    for (size_t at = 0; at < PROMISE_COUNT; at++) {
        *written[at] = false;
    }
    // One loop over all of them, in whatever order they are written. A promise
    // written as a word on its own is unambiguous here, because what follows a
    // signature is a promise or a body and a body begins with a brace.
    for (;;) {
        bool took = false;
        for (size_t at = 0; at < PROMISE_COUNT && !took; at++) {
            if (PROMISES[at].after_no || !is_word(parser, 0, PROMISES[at].word)) {
                continue;
            }
            if (*written[at]) {
                error_at(parser, peek(parser).span, "K0216",
                         "`%s` is written once", PROMISES[at].word);
            }
            *written[at] = true;
            advance(parser);
            took = true;
        }
        if (took) {
            continue;
        }
        if (!is_word(parser, 0, "no") ||
            peek_at(parser, 1).kind != KEST_TOK_DOT) {
            break;
        }
        KestToken word = peek_at(parser, 2);
        KestSpan whole = span_between(peek(parser).span, word.span);
        bool *which = NULL;
        for (size_t at = 0; at < PROMISE_COUNT; at++) {
            if (PROMISES[at].after_no && is_word(parser, 2, PROMISES[at].word)) {
                which = written[at];
            }
        }
        if (which == NULL) {
            if (word.kind != KEST_TOK_IDENT) {
                return;
            }
            char there_are[96];
            promise_list(there_are, sizeof(there_are));
            error_at(parser, whole, "K0216",
                     "`no.%.*s` is not a promise this language has",
                     (int)word.span.length, span_text(parser, word.span));
            suggest(parser, "this language has %s", there_are);
            parser->position += 3;
            continue;
        }
        // And once each. A second one is somebody who wrote it twice rather
        // than somebody promising twice as much.
        if (*which) {
            error_at(parser, whole, "K0216", "`no.%.*s` is written once",
                     (int)word.span.length, span_text(parser, word.span));
        }
        *which = true;
        parser->position += 3;
    }
    // And a promise with its first half left off. `alloc` where a body goes is
    // not a name this language has any use for, so saying `expected `{`` about
    // it is true and no help at all.
    for (size_t at = 0; at < PROMISE_COUNT; at++) {
        if (!PROMISES[at].after_no || !is_word(parser, 0, PROMISES[at].word)) {
            continue;
        }
        error_at(parser, peek(parser).span, "K0216",
                 "a promise is written `no.%s`", PROMISES[at].word);
        advance(parser);
        break;
    }
}

// `store<ref<Npc>>` ends in one token that is two closers. The first half
// closes this type and the second is left where it is, so the type around it
// closes on what is still a `>`.
static void close_generic(Parser *parser) {
    if (check(parser, KEST_TOK_GTGT)) {
        KestToken *token = &parser->tokens[parser->position];
        token->kind = KEST_TOK_GT;
        token->span.offset++;
        token->span.length--;
        return;
    }
    expect(parser, KEST_TOK_GT);
}

static KestTypeRef *parse_type(Parser *parser) {
    KestSpan start = current_span(parser);
    KestTypeRef *type = KEST_ARENA_NEW(parser->arena, KestTypeRef);
    if (type == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }

    if (check(parser, KEST_TOK_FN)) {
        advance(parser);
        type->kind = KEST_TYPE_FN;
        if (!expect(parser, KEST_TOK_LPAREN)) {
            return NULL;
        }
        List args = {0};
        if (!check(parser, KEST_TOK_RPAREN)) {
            do {
                KestTypeRef *param = parse_type(parser);
                if (param == NULL) {
                    return NULL;
                }
                list_push(parser, &args, param);
            } while (match(parser, KEST_TOK_COMMA));
        }
        expect(parser, KEST_TOK_RPAREN);
        type->args = (KestTypeRef **)list_taken(parser, &args);
        type->arg_count = args.count;
        if (match(parser, KEST_TOK_ARROW)) {
            type->element = parse_type(parser);
            if (type->element == NULL) {
                return NULL;
            }
        }
        // The same words a declaration uses through the same door, because
        // they are the same promises and two readings of one promise are two
        // things that agree until somebody changes one.
        match_promises(parser, &type->no_alloc, &type->no_host,
                       &type->deterministic);
        type->span =
            span_between(start, parser->tokens[parser->position - 1].span);
        return type;
    }

    if (match(parser, KEST_TOK_LBRACKET)) {
        type->kind = KEST_TYPE_ARRAY;
        type->element = parse_type(parser);
        // `[f32; 16]` is that many, where it stands. `[f32]` is a handle to
        // something that can grow.
        if (match(parser, KEST_TOK_SEMICOLON)) {
            // A number, or the name of a constant that is one. Which it is,
            // is the type layer's to say: it is the thing that can work a
            // constant out.
            if (check(parser, KEST_TOK_IDENT)) {
                // A constant from another module is one name with a dot in it,
                // the same as a type from one is. What it names is looked up
                // before the constants are symbols, so the type layer finds
                // the file rather than the name. See D681.
                type->count = parse_path(parser);
            } else {
                type->count = current_span(parser);
                KestToken found = peek(parser);
                if (!expect(parser, KEST_TOK_INT)) {
                    // How many there are, which the message above says
                    // nothing about: it names the token that came, and a
                    // reader who wrote a run of minus one is being told `-`
                    // is unexpected rather than that a count is a count. A
                    // store says the same thing in its own words at K0351.
                    // See D885.
                    suggest(parser,
                            found.kind == KEST_TOK_MINUS
                                ? "how many there are is more than nought; a "
                                  "run written with no count is the one that "
                                  "grows"
                                : "write how many there are, or the name of "
                                  "a constant that is one");
                }
            }
        }
        expect(parser, KEST_TOK_RBRACKET);
    } else if (check(parser, KEST_TOK_IDENT)) {
        type->kind = KEST_TYPE_NAMED;
        // A type from another module is one name with a dot in it, the same
        // way a call into one is.
        type->name = parse_path(parser);
        if (match(parser, KEST_TOK_LT)) {
            type->kind = KEST_TYPE_GENERIC;
            List args = {0};
            do {
                list_push(parser, &args, parse_type(parser));
            } while (match(parser, KEST_TOK_COMMA));
            close_generic(parser);
            type->args = (KestTypeRef **)list_taken(parser, &args);
            type->arg_count = args.count;
        }
    } else {
        KestToken found = peek(parser);
        error_at(parser, found.span, "K0203", "expected a type, found %s",
                 kest_token_name(found.kind));
        // Which of the three it is, because what somebody wrote says which
        // language they came from and each has a different answer here. See
        // D515.
        if (found.kind == KEST_TOK_STAR || found.kind == KEST_TOK_AMP) {
            suggest(parser,
                    "there are no pointers here: what names a slot "
                    "in a store is `ref<T>`");
        } else if (found.kind == KEST_TOK_LPAREN) {
            suggest(parser,
                    "there are no tuples here: a `struct` is what "
                    "holds several things");
        } else {
            suggest(parser,
                    "a type is a name, `[T]`, `[T; N]` or "
                    "`fn(...)`, and `?` after any of them");
        }
        return NULL;
    }

    type->span = span_between(start, parser->tokens[parser->position - 1].span);

    while (check(parser, KEST_TOK_QUESTION)) {
        KestSpan mark = advance(parser).span;
        KestTypeRef *optional = KEST_ARENA_NEW(parser->arena, KestTypeRef);
        if (optional == NULL) {
            parser->out_of_memory = true;
            return type;
        }
        optional->kind = KEST_TYPE_OPTIONAL;
        optional->element = type;
        optional->span = span_between(start, mark);
        type = optional;
    }
    return type;
}

static KestExpr *parse_expr(Parser *parser);
static KestExpr *parse_match(Parser *parser);
static KestExpr *parse_if(Parser *parser);
static bool parse_block(Parser *parser, KestBlock *block);

static KestExpr *new_expr(Parser *parser, KestExprKind kind, KestSpan span) {
    parser->nodes++;
    KestExpr *expr = KEST_ARENA_NEW(parser->arena, KestExpr);
    if (expr == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    expr->kind = kind;
    expr->span = span;
    return expr;
}

// Finds the `}` that closes a hole. Braces nest and a string inside a hole
// may hold either brace, so both are followed rather than counted blindly.
static uint32_t close_of_hole(Parser *parser, uint32_t open, uint32_t end) {
    const char *text = parser->source->text;
    uint32_t depth = 0;
    for (uint32_t i = open; i < end; i++) {
        if (text[i] == '\\') {
            i++;
        } else if (text[i] == '"') {
            for (i++; i < end && text[i] != '"'; i++) {
                if (text[i] == '\\') {
                    i++;
                }
            }
        } else if (text[i] == '{') {
            depth++;
        } else if (text[i] == '}') {
            if (--depth == 0) {
                return i;
            }
        }
    }
    return end;
}

// A string with `{}` in it is a run of pieces rather than one value. The holes
// are parsed from the source they were written in, so a mistake inside one
// reports where it is.
static KestExpr *parse_string(Parser *parser, KestSpan span) {
    uint32_t start = span.offset + 1;
    uint32_t end = span.offset + span.length - 1;

    bool interpolated = false;
    for (uint32_t i = start; i < end; i++) {
        if (parser->source->text[i] == '\\') {
            i += kest_escape_width(parser->source, i) - 1;
        } else if (parser->source->text[i] == '{') {
            interpolated = true;
            break;
        }
    }
    if (!interpolated) {
        return new_expr(parser, KEST_EXPR_STRING, span);
    }

    List parts = {0};
    uint32_t chunk = start;
    for (uint32_t i = start; i < end; i++) {
        if (parser->source->text[i] == '\\') {
            i += kest_escape_width(parser->source, i) - 1;
            continue;
        }
        if (parser->source->text[i] != '{') {
            continue;
        }

        uint32_t close = close_of_hole(parser, i, end);
        KestTextPart *literal = KEST_ARENA_NEW(parser->arena, KestTextPart);
        KestTextPart *hole = KEST_ARENA_NEW(parser->arena, KestTextPart);
        if (literal == NULL || hole == NULL) {
            parser->out_of_memory = true;
            return NULL;
        }

        literal->text.offset = chunk;
        literal->text.length = i - chunk;
        if (literal->text.length > 0) {
            list_push(parser, &parts, literal);
        }

        if (close == end || close == i + 1) {
            KestSpan where = {i, close == end ? 1 : 2};
            error_at(parser, where, "K0207",
                     close == end ? "this hole is not closed"
                                  : "this hole is empty");
            // What a hole is for, which is the part a reader is missing: the
            // message says what is wrong with the one they wrote and not what
            // one holds, and somebody who wanted a brace in their text has
            // written the only thing that cannot go in one. See D885.
            suggest(parser,
                    close == end
                        ? "close it with `}`, or write `\\{` for a brace "
                          "that is just text"
                        : "write what fills it, or `\\{}` for two braces "
                          "that are just text");
            return NULL;
        }

        uint32_t count = 0;
        KestToken *tokens = kest_lex_range(parser->arena, parser->source,
                                           parser->diags, i + 1, close, &count);
        if (tokens == NULL) {
            parser->out_of_memory = true;
            return NULL;
        }
        Parser inner = *parser;
        inner.tokens = tokens;
        inner.count = count;
        inner.position = 0;
        inner.recovering = false;
        hole->value = parse_expr(&inner);
        parser->out_of_memory = inner.out_of_memory;
        if (hole->value == NULL) {
            return NULL;
        }
        list_push(parser, &parts, hole);

        i = close;
        chunk = close + 1;
    }

    if (chunk < end) {
        KestTextPart *tail = KEST_ARENA_NEW(parser->arena, KestTextPart);
        if (tail == NULL) {
            parser->out_of_memory = true;
            return NULL;
        }
        tail->text.offset = chunk;
        tail->text.length = end - chunk;
        list_push(parser, &parts, tail);
    }

    KestExpr *expr = new_expr(parser, KEST_EXPR_TEXT, span);
    if (expr == NULL) {
        return NULL;
    }
    // The list holds pointers; the tree holds the parts themselves, so a
    // reader of the tree does not chase one pointer per character run.
    KestTextPart *flat =
        KEST_ARENA_ARRAY(parser->arena, KestTextPart, parts.count + 1);
    if (flat == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    for (uint32_t i = 0; i < parts.count; i++) {
        flat[i] = *(KestTextPart *)parts.items[i];
    }
    expr->text.parts = flat;
    expr->text.count = parts.count;
    return expr;
}

// `match` is one thing whether it is used for its value or for what its arms
// do, so it is parsed once, here, and a statement that is a match is a match
// that was not used for anything.
// Everything up to the `}` that closes the arms, for an arm nothing could
// read. Without it the arms under a bad one are read as statements, and a
// reader who wrote one thing wrong is told about three. See D512.
static void skip_arms(Parser *parser) {
    uint32_t depth = 1;
    while (depth > 0 && !check(parser, KEST_TOK_EOF)) {
        KestTokenKind kind = advance(parser).kind;
        if (kind == KEST_TOK_LBRACE) {
            depth++;
        } else if (kind == KEST_TOK_RBRACE) {
            depth--;
        }
    }
}

static KestExpr *parse_match(Parser *parser) {
    KestSpan start = advance(parser).span;

    // One subject, or several answered together. The list stops at the brace,
    // because no expression in the grammar begins with one.
    List subjects = {0};
    do {
        KestExpr *subject = parse_expr(parser);
        if (subject == NULL) {
            return NULL;
        }
        list_push(parser, &subjects, subject);
    } while (match(parser, KEST_TOK_COMMA));
    if (!expect(parser, KEST_TOK_LBRACE)) {
        return NULL;
    }

    List arms = {0};
    bool gives = false;
    bool blocks = false;
    skip_newlines(parser);
    while (!check(parser, KEST_TOK_RBRACE) && !check(parser, KEST_TOK_EOF)) {
        KestArm *arm = KEST_ARENA_NEW(parser->arena, KestArm);
        if (arm == NULL) {
            parser->out_of_memory = true;
            return NULL;
        }
        KestSpan arm_start = current_span(parser);

        List parts = {0};
        do {
            KestArmPart *part = KEST_ARENA_NEW(parser->arena, KestArmPart);
            if (part == NULL) {
                parser->out_of_memory = true;
                return NULL;
            }
            // `else` is the position with no case, which is the only way to
            // leave one out.
            if (!match(parser, KEST_TOK_ELSE)) {
                part->name = current_span(parser);
                if (!expect(parser, KEST_TOK_IDENT)) {
                    // The rule behind the expectation. Somebody who has met a
                    // language where `match` chooses between values writes a
                    // number or `true` here and hears about a token. What it
                    // chooses between is said where the subject is read, and
                    // the subject is not read until the arms parse. See D512.
                    suggest(parser,
                            "a `match` arm names a case of an "
                            "enum, and `else` answers the rest");
                    skip_arms(parser);
                    return NULL;
                }
                if (match(parser, KEST_TOK_LPAREN)) {
                    List names = {0};
                    if (!check(parser, KEST_TOK_RPAREN)) {
                        do {
                            KestSpan *held =
                                KEST_ARENA_NEW(parser->arena, KestSpan);
                            if (held == NULL) {
                                parser->out_of_memory = true;
                                return NULL;
                            }
                            *held = current_span(parser);
                            if (!expect(parser, KEST_TOK_IDENT)) {
                                skip_arms(parser);
                                return NULL;
                            }
                            list_push(parser, &names, held);
                        } while (match(parser, KEST_TOK_COMMA));
                    }
                    expect(parser, KEST_TOK_RPAREN);
                    part->bindings =
                        KEST_ARENA_ARRAY(parser->arena, KestSpan,
                                         names.count == 0 ? 1 : names.count);
                    if (part->bindings == NULL) {
                        parser->out_of_memory = true;
                        return NULL;
                    }
                    for (uint32_t i = 0; i < names.count; i++) {
                        part->bindings[i] = *(KestSpan *)names.items[i];
                    }
                    part->binding_count = names.count;
                }
            }
            list_push(parser, &parts, part);
        } while (match(parser, KEST_TOK_COMMA));

        arm->parts = KEST_ARENA_ARRAY(parser->arena, KestArmPart,
                                      parts.count == 0 ? 1 : parts.count);
        if (arm->parts == NULL) {
            parser->out_of_memory = true;
            return NULL;
        }
        for (uint32_t i = 0; i < parts.count; i++) {
            arm->parts[i] = *(KestArmPart *)parts.items[i];
        }
        arm->part_count = parts.count;
        arm->span =
            span_between(arm_start, parser->tokens[parser->position - 1].span);

        if (match(parser, KEST_TOK_ARROW) && check(parser, KEST_TOK_LBRACE)) {
            // `Case -> {` is an arm from a language whose blocks are values.
            // Here an arm that does something is a block with no arrow, so this
            // says that, and reads the block as the arm it was meant to be:
            // every arm written this way is one mistake, rather than the first
            // one and then a line about an arrow for every arm after it. The
            // sentence the expression parser has for a stray block is about
            // `if`, which is not what was written. See D1147.
            error_at(parser, current_span(parser), "K0204",
                     "expected an expression, found %s",
                     kest_token_name(KEST_TOK_LBRACE));
            suggest(parser,
                    "an arm that does something is a block with no `->` "
                    "before it: `%.*s {`",
                    (int)arm->span.length, span_text(parser, arm->span));
            blocks = true;
            if (!parse_block(parser, &arm->body)) {
                return NULL;
            }
        } else if (parser->tokens[parser->position - 1].kind ==
                   KEST_TOK_ARROW) {
            gives = true;
            arm->value = parse_expr(parser);
            if (arm->value == NULL) {
                return NULL;
            }
        } else {
            blocks = true;
            if (!parse_block(parser, &arm->body)) {
                return NULL;
            }
        }
        list_push(parser, &arms, arm);
        end_statement(parser);
        skip_newlines(parser);
        if (parser->out_of_memory) {
            return NULL;
        }
    }
    KestSpan close = current_span(parser);
    expect(parser, KEST_TOK_RBRACE);

    if (gives && blocks) {
        error_at(parser, start, "K0208",
                 "every arm gives a value or none does");
        suggest(parser,
                "an arm gives one with `-> value` and does "
                "something with a block");
    }

    KestExpr *expr = new_expr(parser, KEST_EXPR_MATCH, span_between(start, close));
    if (expr == NULL) {
        return NULL;
    }
    expr->choose = kest_arena_alloc(parser->arena, sizeof *expr->choose,
                                    _Alignof(KestChoose));
    if (expr->choose == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    expr->choose->subjects = (KestExpr **)list_taken(parser, &subjects);
    expr->choose->subject_count = subjects.count;
    expr->choose->gives = gives;
    expr->choose->arms =
        KEST_ARENA_ARRAY(parser->arena, KestArm, arms.count == 0 ? 1 : arms.count);
    if (expr->choose->arms == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    for (uint32_t i = 0; i < arms.count; i++) {
        expr->choose->arms[i] = *(KestArm *)arms.items[i];
    }
    expr->choose->arm_count = arms.count;
    return expr;
}

// `if let` and `while let` give a name to what an optional holds. They are not
// patterns: nothing is compared and nothing is taken apart, so the only thing
// between `let` and `=` is a name. Said here because the expectation on its own
// is a message about a token, and what a reader who wrote `if let Some(x) =`
// needs is the rule. See D513.
static bool parse_binding(Parser *parser, KestSpan *name, const char *what) {
    *name = current_span(parser);
    if (!expect(parser, KEST_TOK_IDENT)) {
        suggest(parser,
                "`%s let` names what is held rather than comparing "
                "with it", what);
        return false;
    }
    if (!expect(parser, KEST_TOK_EQ)) {
        suggest(parser,
                "`%s let` names what an optional holds: "
                "`%s let held = ...`", what, what);
        return false;
    }
    return true;
}

// Like `match`, parsed once whether it is used for its value or for what its
// arms do. An arm that gives one says so with `->`.
static KestExpr *parse_if(Parser *parser) {
    KestSpan start = advance(parser).span;
    KestBranch branch = {0};

    if (match(parser, KEST_TOK_LET) &&
        !parse_binding(parser, &branch.binding, "if")) {
        return NULL;
    }
    if (wrapped_whole(parser)) {
        refuse_wrapped(parser, "if");
        return NULL;
    }
    // The condition stops at the brace or the arrow on its own: no expression
    // in the grammar begins with either.
    branch.condition = parse_expr(parser);
    if (branch.condition == NULL) {
        return NULL;
    }

    bool blocks = false;
    if (match(parser, KEST_TOK_ARROW)) {
        branch.gives = true;
        branch.then_value = parse_expr(parser);
        if (branch.then_value == NULL) {
            return NULL;
        }
    } else {
        blocks = true;
        if (!parse_block(parser, &branch.then_body)) {
            return NULL;
        }
    }

    // An `if` that gives a value is one expression and may be written over two
    // lines, because the one it is written on may not be long enough. Nothing
    // else can follow a value with `else`, so looking past the break for it
    // takes nothing away from anybody.
    if (branch.gives && check(parser, KEST_TOK_NEWLINE)) {
        uint32_t ahead = 1;
        while (peek_at(parser, ahead).kind == KEST_TOK_NEWLINE) {
            ahead++;
        }
        if (peek_at(parser, ahead).kind == KEST_TOK_ELSE) {
            skip_newlines(parser);
        }
    }

    if (match(parser, KEST_TOK_ELSE)) {
        branch.has_else = true;
        if (check(parser, KEST_TOK_IF)) {
            branch.otherwise = parse_if(parser);
            if (branch.otherwise == NULL) {
                return NULL;
            }
            if (branch.otherwise->branch->gives) {
                branch.gives = true;
            } else {
                blocks = true;
            }
        } else if (match(parser, KEST_TOK_ARROW)) {
            branch.gives = true;
            branch.else_value = parse_expr(parser);
            if (branch.else_value == NULL) {
                return NULL;
            }
        } else {
            blocks = true;
            if (!parse_block(parser, &branch.else_body)) {
                return NULL;
            }
        }
    }

    KestSpan whole =
        span_between(start, parser->tokens[parser->position - 1].span);
    if (branch.gives && blocks) {
        error_at(parser, whole, "K0208",
                 "every arm gives a value or none does");
        suggest(parser,
                "an arm gives one with `-> value` and does "
                "something with a block");
    }

    KestExpr *expr = new_expr(parser, KEST_EXPR_IF, whole);
    if (expr == NULL) {
        return NULL;
    }
    KestBranch *held = kest_arena_alloc(parser->arena, sizeof *held, _Alignof(KestBranch));
    if (held == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    *held = branch;
    expr->branch = held;
    return expr;
}

static KestExpr *parse_primary(Parser *parser) {
    KestToken token = peek(parser);
    switch (token.kind) {
    case KEST_TOK_INT:
        advance(parser);
        return new_expr(parser, KEST_EXPR_INT, token.span);
    case KEST_TOK_FLOAT:
        advance(parser);
        return new_expr(parser, KEST_EXPR_FLOAT, token.span);
    case KEST_TOK_STRING:
        advance(parser);
        return parse_string(parser, token.span);
    case KEST_TOK_BYTE:
        advance(parser);
        return new_expr(parser, KEST_EXPR_BYTE, token.span);
    case KEST_TOK_IDENT:
        advance(parser);
        return new_expr(parser, KEST_EXPR_NAME, token.span);
    case KEST_TOK_NONE:
        advance(parser);
        return new_expr(parser, KEST_EXPR_NONE, token.span);
    case KEST_TOK_MATCH:
        return parse_match(parser);
    case KEST_TOK_IF:
        return parse_if(parser);
    case KEST_TOK_TRUE:
    case KEST_TOK_FALSE: {
        advance(parser);
        KestExpr *expr = new_expr(parser, KEST_EXPR_BOOL, token.span);
        if (expr != NULL) {
            expr->boolean = token.kind == KEST_TOK_TRUE;
        }
        return expr;
    }
    case KEST_TOK_LPAREN: {
        advance(parser);
        KestExpr *inner = parse_expr(parser);
        expect(parser, KEST_TOK_RPAREN);
        return inner;
    }
    case KEST_TOK_LBRACKET: {
        // Only a primary can start with a bracket, because indexing needs an
        // expression in front of it, so nothing has to be disambiguated.
        advance(parser);
        List items = {0};
        if (!check(parser, KEST_TOK_RBRACKET)) {
            do {
                KestExpr *item = parse_expr(parser);
                if (item == NULL) {
                    return NULL;
                }
                list_push(parser, &items, item);
            } while (match(parser, KEST_TOK_COMMA));
        }
        KestSpan close = current_span(parser);
        expect(parser, KEST_TOK_RBRACKET);

        KestExpr *array = new_expr(parser, KEST_EXPR_ARRAY,
                                   span_between(token.span, close));
        if (array == NULL) {
            return NULL;
        }
        array->array.items = (KestExpr **)list_taken(parser, &items);
        array->array.count = items.count;
        return array;
    }
    default:
        error_at(parser, token.span, "K0204", "expected an expression, found %s",
                 kest_token_name(token.kind));
        // A block where a value was wanted, which is what somebody writes who
        // has met a language whose blocks are expressions. Here a block is a
        // statement and an `if` is the one thing that gives a value out of
        // arms. See D515.
        if (token.kind == KEST_TOK_LBRACE) {
            suggest(parser,
                    "a block is not a value: an `if` gives one "
                    "with `->`");
        }
        // A comparison with its right side on the next line, which is the one
        // thing a reader writes that this language will not take. A line ends
        // after `>` because a type ends in one — `giver: ref<Npc>` is a whole
        // field — so the line ended and what was to be compared with is a
        // statement of its own. It reads as two mistakes and is one, and the
        // reference spends four paragraphs on the rule; the reader who meets
        // it is owed the sentence rather than the rule. See D883.
        bool ended_after_gt =
            token.kind == KEST_TOK_NEWLINE && parser->position >= 1 &&
            parser->tokens[parser->position - 1].kind == KEST_TOK_GT;
        // And the same mistake written the other way round: the line ended on
        // a value, so what was on the next one starts with an operator and is
        // a statement of its own. One sentence for both, because a reader who
        // broke a comparison did one thing and is looking at one rule.
        bool began_with_compare =
            token.kind == KEST_TOK_GT || token.kind == KEST_TOK_LT ||
            token.kind == KEST_TOK_GTEQ || token.kind == KEST_TOK_LTEQ;
        if (ended_after_gt || began_with_compare) {
            suggest(parser,
                    "a line may end after `>` because a type may: "
                    "`ref<Npc>` is a whole field. So a comparison "
                    "stays on the line it is on");
        }
        return NULL;
    }
}

// What a type is made of, so that `store<Node>()` can be told apart from
// `a < b > (c)`. Anything else between the angles stops the scan, and what
// stops it stays the comparison it reads as.
static bool part_of_a_type(KestTokenKind kind) {
    switch (kind) {
    case KEST_TOK_IDENT:
    case KEST_TOK_INT:
    case KEST_TOK_LBRACKET:
    case KEST_TOK_RBRACKET:
    case KEST_TOK_COMMA:
    case KEST_TOK_SEMICOLON:
    case KEST_TOK_QUESTION:
    case KEST_TOK_DOT:
    case KEST_TOK_LT:
    case KEST_TOK_GT:
        return true;
    default:
        return false;
    }
}

// How many tokens `<...>` takes when a call follows it, and nought when this
// is a comparison like any other. The name and the `<` have to be written
// against each other, which is how somebody writes a type argument and not
// how anybody writes a comparison.
static uint32_t type_arguments(Parser *parser, const KestExpr *name) {
    if (name->kind != KEST_EXPR_NAME || !check(parser, KEST_TOK_LT) ||
        name->span.offset + name->span.length != peek(parser).span.offset) {
        return 0;
    }
    uint32_t depth = 0;
    for (uint32_t ahead = 0; ahead < 32; ahead++) {
        KestTokenKind kind = peek_at(parser, ahead).kind;
        if (!part_of_a_type(kind)) {
            return 0;
        }
        if (kind == KEST_TOK_LT) {
            depth++;
        } else if (kind == KEST_TOK_GT && --depth == 0) {
            return peek_at(parser, ahead + 1).kind == KEST_TOK_LPAREN
                       ? ahead + 1
                       : 0;
        }
    }
    return 0;
}

// Calls, field access and indexing, which bind tighter than any operator.
static KestExpr *parse_postfix(Parser *parser) {
    KestExpr *expr = parse_primary(parser);
    if (expr == NULL) {
        return NULL;
    }

    while (true) {
        // A type written at a call is read as two comparisons and refused at
        // the `)`, which is nowhere near what is wrong. This language takes
        // types from what is passed, or from what a binding is written as,
        // and that is worth saying where it was written.
        uint32_t angles = type_arguments(parser, expr);
        if (angles > 0) {
            KestSpan written = span_between(
                expr->span, peek_at(parser, angles - 1).span);
            bool nothing_passed =
                peek_at(parser, angles + 1).kind == KEST_TOK_RPAREN;
            error_at(parser, written, "K0211",
                     "`%.*s` is not given its types where it is called",
                     (int)expr->span.length, span_text(parser, expr->span));
            // Which way the type gets there depends on whether anything is
            // passed, and naming the wrong one of the two is worse than
            // naming neither.
            suggest(parser,
                    nothing_passed
                        ? "write `%.*s()`, and the type on the binding it "
                          "goes to"
                        : "write `%.*s(...)`: the copy is made from what is "
                          "passed",
                    (int)expr->span.length, span_text(parser, expr->span));
            for (uint32_t i = 0; i < angles; i++) {
                advance(parser);
            }
            continue;
        }
        if (match(parser, KEST_TOK_LPAREN)) {
            List args = {0};
            if (!check(parser, KEST_TOK_RPAREN)) {
                do {
                    KestExpr *arg = parse_expr(parser);
                    if (arg == NULL) {
                        return NULL;
                    }
                    list_push(parser, &args, arg);
                } while (match(parser, KEST_TOK_COMMA));
            }
            KestSpan close = current_span(parser);
            // `Thing(x: 1.0)` is a call from a language that passes by name.
            // Nothing does here: a struct is built, and a function called,
            // with what it takes in the order it is declared. See D1148.
            if (!expect(parser, KEST_TOK_RPAREN) &&
                check(parser, KEST_TOK_COLON) &&
                parser->tokens[parser->position - 1].kind == KEST_TOK_IDENT) {
                suggest(parser,
                        "nothing is passed by name: `%.*s(...)` takes what "
                        "it takes in the order it is declared",
                        (int)expr->span.length, span_text(parser, expr->span));
            }

            KestExpr *call = new_expr(parser, KEST_EXPR_CALL,
                                      span_between(expr->span, close));
            if (call == NULL) {
                return NULL;
            }
            call->call.callee = expr;
            call->call.args = (KestExpr **)list_taken(parser, &args);
            call->call.arg_count = args.count;
            expr = call;
        } else if (match(parser, KEST_TOK_DOT)) {
            KestSpan name = current_span(parser);
            if (!expect(parser, KEST_TOK_IDENT)) {
                // `t.0` is what somebody writes who has met tuples. There are
                // none here: what a struct holds is named. See D514.
                suggest(parser,
                        "a field is named, so there is nothing at "
                        "a position to read");
                return NULL;
            }
            KestExpr *field = new_expr(parser, KEST_EXPR_FIELD,
                                       span_between(expr->span, name));
            if (field == NULL) {
                return NULL;
            }
            field->field.object = expr;
            field->field.name = name;
            expr = field;
        } else if (match(parser, KEST_TOK_LBRACKET)) {
            KestExpr *subscript = parse_expr(parser);
            KestSpan close = current_span(parser);
            expect(parser, KEST_TOK_RBRACKET);

            KestExpr *index = new_expr(parser, KEST_EXPR_INDEX,
                                       span_between(expr->span, close));
            if (index == NULL) {
                return NULL;
            }
            index->index.object = expr;
            index->index.index = subscript;
            expr = index;
        } else {
            return expr;
        }
    }
}

static KestExpr *parse_unary(Parser *parser) {
    if (check(parser, KEST_TOK_MINUS) || check(parser, KEST_TOK_BANG) ||
        check(parser, KEST_TOK_TILDE)) {
        KestToken op = advance(parser);
        KestExpr *operand = parse_unary(parser);
        if (operand == NULL) {
            return NULL;
        }
        KestExpr *expr = new_expr(parser, KEST_EXPR_UNARY,
                                  span_between(op.span, operand->span));
        if (expr == NULL) {
            return NULL;
        }
        expr->unary.op = op.kind;
        expr->unary.operand = operand;
        return expr;
    }
    return parse_postfix(parser);
}

// The bitwise operators bind tighter than the comparisons, which is the one
// place C is known to be wrong: `flags & MASK == 0` reads as one thing and
// means another there. Shifts keep C's place, above the bitwise operators and
// below the arithmetic, because `1 << n + 1` has never been the trap.
int kest_binary_precedence(KestTokenKind kind) {
    switch (kind) {
    case KEST_TOK_PIPEPIPE:
        return 1;
    case KEST_TOK_AMPAMP:
        return 2;
    case KEST_TOK_EQEQ:
    case KEST_TOK_BANGEQ:
        return 3;
    case KEST_TOK_LT:
    case KEST_TOK_LTEQ:
    case KEST_TOK_GT:
    case KEST_TOK_GTEQ:
        return 4;
    case KEST_TOK_PIPE:
        return 5;
    case KEST_TOK_CARET:
        return 6;
    case KEST_TOK_AMP:
        return 7;
    case KEST_TOK_LTLT:
    case KEST_TOK_GTGT:
        return 8;
    case KEST_TOK_PLUS:
    case KEST_TOK_MINUS:
        return 9;
    case KEST_TOK_STAR:
    case KEST_TOK_SLASH:
    case KEST_TOK_PERCENT:
        return 10;
    default:
        return 0;
    }
}

static KestExpr *parse_binary(Parser *parser, int minimum) {
    KestExpr *left = parse_unary(parser);
    if (left == NULL) {
        return NULL;
    }

    while (true) {
        int precedence = kest_binary_precedence(peek(parser).kind);
        if (precedence == 0 || precedence < minimum) {
            return left;
        }
        KestToken op = advance(parser);
        // Every operator is left associative, so the right side stops at the
        // first operator of equal precedence.
        KestExpr *right = parse_binary(parser, precedence + 1);
        if (right == NULL) {
            return NULL;
        }
        KestExpr *expr = new_expr(parser, KEST_EXPR_BINARY,
                                  span_between(left->span, right->span));
        if (expr == NULL) {
            return NULL;
        }
        expr->binary.op = op.kind;
        expr->binary.left = left;
        expr->binary.right = right;
        left = expr;
    }
}

// How deep one expression may be written inside another. A parser of this
// shape follows nesting with the machine's own stack, and so does everything
// that walks the tree after it — so a program nested deeper than the stack is
// tall is a crash rather than a refusal, and no amount of memory makes it not
// one. The deepest expression anything in this tree writes is seven.
//
// A number rather than a guard on the stack, because a stack that has run out
// cannot be asked about portably and a program that is refused knows where it
// stands. See D645.
#define MAX_NESTING 128

static KestExpr *parse_expr(Parser *parser) {
    if (parser->nesting >= MAX_NESTING) {
        // Said once for the whole run of them: recovering from here walks back
        // out through every level, and one message a level is a page of the
        // same sentence.
        if (!parser->recovering) {
            error_at(parser, peek(parser).span, "K0215",
                     "expressions nest more than %d deep", MAX_NESTING);
            suggest(parser,
                    "give a piece of it a name: a `let` is a place "
                    "to stop and the checker reads it the same way");
        }
        return NULL;
    }
    parser->nesting++;
    KestExpr *expr = parse_binary(parser, 1);
    parser->nesting--;
    return expr;
}

static bool parse_block(Parser *parser, KestBlock *block);

static KestStmt *new_stmt(Parser *parser, KestStmtKind kind, KestSpan span) {
    KestStmt *stmt = (parser->nodes++, KEST_ARENA_NEW(parser->arena, KestStmt));
    if (stmt == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    stmt->kind = kind;
    stmt->span = span;
    return stmt;
}

static bool is_assignable(const KestExpr *expr) {
    return expr->kind == KEST_EXPR_NAME || expr->kind == KEST_EXPR_FIELD ||
           expr->kind == KEST_EXPR_INDEX;
}

static bool is_assignment(KestTokenKind kind) {
    switch (kind) {
    case KEST_TOK_EQ:
    case KEST_TOK_PLUSEQ:
    case KEST_TOK_MINUSEQ:
    case KEST_TOK_STAREQ:
    case KEST_TOK_SLASHEQ:
        return true;
    default:
        return false;
    }
}

static KestStmt *parse_statement(Parser *parser) {
    KestSpan start = current_span(parser);
    parser->began_with = peek(parser);

    if (match(parser, KEST_TOK_LET)) {
        KestSpan name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            return NULL;
        }
        KestTypeRef *type = NULL;
        if (match(parser, KEST_TOK_COLON)) {
            type = parse_type(parser);
        }
        if (!expect(parser, KEST_TOK_EQ)) {
            // The rule behind the expectation, which is the part worth
            // hearing: a name is given its value where it is written, and
            // there is no declaring one now and filling it in later. See D506.
            suggest(parser,
                    "a `let` gives its value where it is written");
            return NULL;
        }
        KestExpr *value = parse_expr(parser);
        if (value == NULL) {
            return NULL;
        }
        KestStmt *stmt =
            new_stmt(parser, KEST_STMT_LET, span_between(start, value->span));
        if (stmt == NULL) {
            return NULL;
        }
        stmt->let.name = name;
        stmt->let.type = type;
        stmt->let.value = value;
        // Written until the checker says otherwise, because what turns on this
        // is whether the compiler may put the value in the chunk and leave the
        // frame without it. A name nobody has looked at has to read as one that
        // is written. See D887.
        stmt->let.name_written = true;
        return stmt;
    }

    // `defer f(x)` runs when the block it is in ends, however it ends. It
    // takes a call and not a statement: a block would want its own scope
    // rules and nothing has asked for one.
    if (match(parser, KEST_TOK_DEFER)) {
        KestStmt *stmt = new_stmt(parser, KEST_STMT_DEFER, start);
        if (stmt == NULL) {
            return NULL;
        }
        // A block is what a reader who has met another language writes here,
        // and it is the one thing the expression parser has nothing to say
        // about: it would report a `{` where an expression was wanted, which
        // is the token and not the rule. The rule is the same one below.
        if (check(parser, KEST_TOK_LBRACE)) {
            error_at(parser, current_span(parser), "K0210",
                     "a `defer` runs something, and this is not a call");
            return NULL;
        }
        stmt->value = parse_expr(parser);
        if (stmt->value == NULL) {
            return NULL;
        }
        if (stmt->value->kind != KEST_EXPR_CALL) {
            error_at(parser, stmt->value->span, "K0210",
                     "a `defer` runs something, and this is not a call");
        }
        stmt->span =
            span_between(start, parser->tokens[parser->position - 1].span);
        return stmt;
    }

    if (match(parser, KEST_TOK_WHILE)) {
        // `while let one = next()` runs while there is something, the same
        // way `if let` runs when there is.
        KestSpan binding = {0, 0};
        if (match(parser, KEST_TOK_LET) &&
            !parse_binding(parser, &binding, "while")) {
            return NULL;
        }
        if (wrapped_whole(parser)) {
            refuse_wrapped(parser, "while");
            return NULL;
        }
        KestExpr *condition = parse_expr(parser);
        if (condition == NULL) {
            return NULL;
        }
        KestStmt *stmt = new_stmt(parser, KEST_STMT_WHILE, start);
        if (stmt == NULL) {
            return NULL;
        }
        stmt->loop.binding = binding;
        stmt->loop.condition = condition;
        parse_block(parser, &stmt->loop.body);
        stmt->span =
            span_between(start, parser->tokens[parser->position - 1].span);
        return stmt;
    }

    if (match(parser, KEST_TOK_FOR)) {
        KestSpan index = {0, 0};
        KestSpan name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            // The same rule in the third place it is written: what stands
            // here is a name for what comes out, and there is nothing else it
            // could be. See D513.
            suggest(parser,
                    "a `for` names what it walks over: "
                    "`for one in ...`");
            return NULL;
        }
        // `for i, x in a`: the position first, because that is the order it
        // is asked for in.
        if (match(parser, KEST_TOK_COMMA)) {
            index = name;
            name = current_span(parser);
            if (!expect(parser, KEST_TOK_IDENT)) {
                suggest(parser,
                        "a `for` names the position first and what "
                        "it walks over second: `for at, one in ...`");
                return NULL;
            }
        }
        if (!expect(parser, KEST_TOK_IN)) {
            return NULL;
        }
        KestExpr *sequence = parse_expr(parser);
        if (sequence == NULL) {
            return NULL;
        }
        // `from..to` is a way to write a walk rather than a value, so it is
        // read here and nowhere else.
        KestExpr *until = NULL;
        if (match(parser, KEST_TOK_DOTDOT)) {
            until = parse_expr(parser);
            if (until == NULL) {
                return NULL;
            }
        }
        KestStmt *stmt = new_stmt(parser, KEST_STMT_FOR, start);
        if (stmt == NULL) {
            return NULL;
        }
        stmt->each = kest_arena_alloc(parser->arena, sizeof *stmt->each,
                                      _Alignof(KestEach));
        if (stmt->each == NULL) {
            parser->out_of_memory = true;
            return NULL;
        }
        stmt->each->index = index;
        stmt->each->name = name;
        stmt->each->sequence = sequence;
        stmt->each->until = until;
        // Until the checker has read the body, a name is one the body writes:
        // what a walk does about that is safe for every program, and what it
        // does when nothing writes is safe only for the programs the checker
        // has said so about. See D866.
        stmt->each->name_written = true;
        stmt->each->index_written = true;
        parse_block(parser, &stmt->each->body);
        stmt->span =
            span_between(start, parser->tokens[parser->position - 1].span);
        return stmt;
    }

    if (match(parser, KEST_TOK_RETURN)) {
        KestStmt *stmt = new_stmt(parser, KEST_STMT_RETURN, start);
        if (stmt == NULL) {
            return NULL;
        }
        if (!check(parser, KEST_TOK_NEWLINE) && !check(parser, KEST_TOK_RBRACE) &&
            !check(parser, KEST_TOK_EOF)) {
            stmt->result = parse_expr(parser);
            if (stmt->result != NULL) {
                stmt->span = span_between(start, stmt->result->span);
            }
        }
        return stmt;
    }

    if (check(parser, KEST_TOK_BREAK) || check(parser, KEST_TOK_CONTINUE)) {
        KestTokenKind kind = advance(parser).kind;
        return new_stmt(parser,
                        kind == KEST_TOK_BREAK ? KEST_STMT_BREAK
                                               : KEST_STMT_CONTINUE,
                        start);
    }

    // A word rather than a keyword: `scratch` followed by a brace is a thing
    // no other statement can be, and a keyword is paid for by everybody who
    // wanted the name. See the rule about words in `CLAUDE.md`.
    if (is_word(parser, 0, "scratch") &&
        peek_at(parser, 1).kind == KEST_TOK_LBRACE) {
        advance(parser);
        KestStmt *stmt = new_stmt(parser, KEST_STMT_SCRATCH, start);
        if (stmt == NULL) {
            return NULL;
        }
        parse_block(parser, &stmt->block);
        return stmt;
    }

    if (check(parser, KEST_TOK_LBRACE)) {
        KestStmt *stmt = new_stmt(parser, KEST_STMT_BLOCK, start);
        if (stmt == NULL) {
            return NULL;
        }
        parse_block(parser, &stmt->block);
        return stmt;
    }

    uint32_t compound_at = 0;
    const char *compound = no_compound(parser, &compound_at);
    if (compound != NULL) {
        error_at(parser, peek_at(parser, compound_at).span, "K0214",
                 "`%s=` is not one of the four this language has", compound);
        suggest(parser,
                "they are `+=`, `-=`, `*=` and `/=`; write it out: "
                "`x = x %s y`",
                compound);
        return NULL;
    }

    KestExpr *expr = parse_expr(parser);
    if (expr == NULL) {
        return NULL;
    }

    if (is_assignment(peek(parser).kind)) {
        KestToken op = advance(parser);
        KestExpr *value = parse_expr(parser);
        if (value == NULL) {
            return NULL;
        }
        if (!is_assignable(expr)) {
            error_at(parser, expr->span, "K0205",
                     "this expression cannot be assigned to");
            suggest(parser,
                    "only a name, a field or an element can be a "
                    "target");
        }
        KestStmt *stmt = new_stmt(parser, KEST_STMT_ASSIGN,
                                  span_between(start, value->span));
        if (stmt == NULL) {
            return NULL;
        }
        stmt->assign.op = op.kind;
        stmt->assign.target = expr;
        stmt->assign.value = value;
        return stmt;
    }

    KestStmt *stmt = new_stmt(parser, KEST_STMT_EXPR, expr->span);
    if (stmt == NULL) {
        return NULL;
    }
    stmt->value = expr;
    return stmt;
}

static bool parse_block(Parser *parser, KestBlock *block) {
    if (!expect(parser, KEST_TOK_LBRACE)) {
        return false;
    }

    List items = {0};
    skip_newlines(parser);
    while (!check(parser, KEST_TOK_RBRACE) && !check(parser, KEST_TOK_EOF)) {
        uint32_t was = parser->position;
        KestStmt *stmt = parse_statement(parser);
        if (stmt == NULL) {
            recover_from(parser, was);
        } else {
            list_push(parser, &items, stmt);
            end_statement(parser);
        }
        skip_newlines(parser);
        if (parser->out_of_memory) {
            return false;
        }
    }
    expect(parser, KEST_TOK_RBRACE);

    block->items = (KestStmt **)list_taken(parser, &items);
    block->count = items.count;
    return true;
}

static KestField *parse_field(Parser *parser) {
    KestField *field = KEST_ARENA_NEW(parser->arena, KestField);
    if (field == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    field->name = current_span(parser);
    if (!expect(parser, KEST_TOK_IDENT)) {
        return NULL;
    }
    if (!expect(parser, KEST_TOK_COLON)) {
        // `x i32` is what somebody writes who has met Go, and the token this
        // wanted says nothing about which of the two orders is right. See
        // D514.
        suggest(parser,
                "a field is written `name: type`");
        return NULL;
    }
    field->type = parse_type(parser);
    return field->type == NULL ? NULL : field;
}

static KestDecl *new_decl(Parser *parser, KestDeclKind kind, KestSpan span) {
    KestDecl *decl = (parser->nodes++, KEST_ARENA_NEW(parser->arena, KestDecl));
    if (decl == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    decl->kind = kind;
    decl->span = span;
    return decl;
}

// `fn sort<T>` and `struct Pair<A, B>` ask for the same thing, so they are
// read the same way. A name here stands for one type per copy.
// `T: compares hashes`, read after a parameter's name. Words rather than
// keywords, the way `flags` and `scratch` and `own` are: they stand here and
// are names everywhere else, so a program that called something `orders` keeps
// it.
static uint8_t parse_wants(Parser *parser) {
    if (!match(parser, KEST_TOK_COLON)) {
        return 0;
    }
    uint8_t wants = 0;
    for (;;) {
        bool took = false;
        for (uint32_t at = 0; at < KEST_CAPABILITY_COUNT && !took; at++) {
            if (!is_word(parser, 0, kest_capability(at)->word)) {
                continue;
            }
            KestSpan where = current_span(parser);
            advance(parser);
            if ((wants & kest_capability(at)->bit) != 0) {
                error_at(parser, where, "K0217", "`%s` is written twice",
                         kest_capability(at)->word);
            }
            wants |= (uint8_t)kest_capability(at)->bit;
            took = true;
        }
        if (!took) {
            break;
        }
    }
    if (wants == 0) {
        char list[128];
        kest_capability_list(list, sizeof(list));
        error_at(parser, current_span(parser), "K0217",
                 "expected what this type has to be able to do, found %s",
                 kest_token_name(peek_at(parser, 0).kind));
        suggest(parser, "there are %u: %s", KEST_CAPABILITY_COUNT, list);
    }
    return wants;
}

static bool parse_type_params(Parser *parser, KestDecl *decl) {
    if (!match(parser, KEST_TOK_LT)) {
        return true;
    }
    List names = {0};
    do {
        KestTypeParam *held = KEST_ARENA_NEW(parser->arena, KestTypeParam);
        if (held == NULL) {
            parser->out_of_memory = true;
            return false;
        }
        held->name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            return false;
        }
        held->wants = parse_wants(parser);
        list_push(parser, &names, held);
    } while (match(parser, KEST_TOK_COMMA));
    close_generic(parser);

    decl->type_params = KEST_ARENA_ARRAY(parser->arena, KestTypeParam,
                                         names.count == 0 ? 1 : names.count);
    if (decl->type_params == NULL) {
        parser->out_of_memory = true;
        return false;
    }
    for (uint32_t i = 0; i < names.count; i++) {
        decl->type_params[i] = *(KestTypeParam *)names.items[i];
    }
    decl->type_param_count = names.count;
    return true;
}

static KestDecl *parse_function(Parser *parser, KestSpan start, bool is_extern) {
    advance(parser);

    KestDecl *decl = new_decl(parser, KEST_DECL_FN, start);
    if (decl == NULL) {
        return NULL;
    }
    decl->function.is_extern = is_extern;

    decl->name = current_span(parser);
    if (!expect(parser, KEST_TOK_IDENT)) {
        return NULL;
    }
    if (match(parser, KEST_TOK_DOT)) {
        decl->function.receiver = decl->name;
        decl->name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            return NULL;
        }
        if (!is_extern) {
            error_at(parser, decl->function.receiver, "K0206",
                     "only an extern function names a receiver");
        }
    }

    if (!parse_type_params(parser, decl)) {
        return NULL;
    }
    if (is_extern && decl->type_param_count > 0) {
        error_at(parser, decl->name, "K0209",
                 "an extern function is the host's, so it takes no types");
    }

    if (!expect(parser, KEST_TOK_LPAREN)) {
        return NULL;
    }
    List params = {0};
    if (!check(parser, KEST_TOK_RPAREN)) {
        do {
            KestField *param = parse_field(parser);
            if (param == NULL) {
                // A signature that did not parse makes its body meaningless,
                // so recovery goes to the next declaration rather than
                // reporting the body against a signature nobody has.
                return NULL;
            }
            list_push(parser, &params, param);
        } while (match(parser, KEST_TOK_COMMA));
    }
    expect(parser, KEST_TOK_RPAREN);
    decl->function.params = (KestField **)list_taken(parser, &params);
    decl->function.param_count = params.count;

    if (match(parser, KEST_TOK_ARROW)) {
        decl->function.result = parse_type(parser);
    }
    match_promises(parser, &decl->function.no_alloc,
                   &decl->function.no_host, &decl->function.deterministic);

    if (is_extern) {
        decl->span =
            span_between(start, parser->tokens[parser->position - 1].span);
        return decl;
    }

    if (!parse_block(parser, &decl->function.body)) {
        return NULL;
    }
    decl->span = span_between(start, parser->tokens[parser->position - 1].span);
    return decl;
}

static KestDecl *parse_declaration(Parser *parser) {
    KestSpan start = current_span(parser);

    if (match(parser, KEST_TOK_MODULE)) {
        KestDecl *decl = new_decl(parser, KEST_DECL_MODULE, start);
        if (decl != NULL) {
            decl->name = parse_path(parser);
            decl->span = span_between(start, decl->name);
        }
        return decl;
    }

    if (match(parser, KEST_TOK_IMPORT)) {
        KestDecl *decl = new_decl(parser, KEST_DECL_IMPORT, start);
        if (decl != NULL) {
            decl->name = parse_path(parser);
            decl->span = span_between(start, decl->name);
        }
        return decl;
    }

    if (match(parser, KEST_TOK_CONST)) {
        KestDecl *decl = new_decl(parser, KEST_DECL_CONST, start);
        if (decl == NULL) {
            return NULL;
        }
        decl->name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            return NULL;
        }
        // A module-level constant is visible outside the body that defines it,
        // and D005 declares at every boundary rather than inferring across one.
        if (!expect(parser, KEST_TOK_COLON)) {
            // `const N = 1` is what a reader writes first, and the rule it
            // meets is the one above this line: the type is written because
            // the name crosses a boundary. See D514.
            suggest(parser,
                    "a `const` is written with its type: "
                    "`const N: i32 = 1`");
            return NULL;
        }
        decl->constant.type = parse_type(parser);
        if (!expect(parser, KEST_TOK_EQ)) {
            suggest(parser,
                    "a `const` gives its value where it is "
                    "written");
            return NULL;
        }
        decl->constant.value = parse_expr(parser);
        if (decl->constant.value != NULL) {
            decl->span = span_between(start, decl->constant.value->span);
        }
        return decl;
    }

    if (match(parser, KEST_TOK_STRUCT)) {
        KestDecl *decl = new_decl(parser, KEST_DECL_STRUCT, start);
        if (decl == NULL) {
            return NULL;
        }
        decl->name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            return NULL;
        }
        if (!parse_type_params(parser, decl) ||
            !expect(parser, KEST_TOK_LBRACE)) {
            return NULL;
        }

        List fields = {0};
        skip_newlines(parser);
        while (!check(parser, KEST_TOK_RBRACE) && !check(parser, KEST_TOK_EOF)) {
            uint32_t was = parser->position;
            // A word rather than a keyword, for the reason `scratch` is one:
            // a field is a name and a colon, so `own keys: [K]` is a shape no
            // field could have had, and a field may still be called `own`.
            // See D1041.
            KestSpan own = {0};
            if (is_word(parser, 0, "own") &&
                peek_at(parser, 1).kind == KEST_TOK_IDENT &&
                peek_at(parser, 2).kind == KEST_TOK_COLON) {
                own = current_span(parser);
                advance(parser);
            }
            KestField *field = parse_field(parser);
            if (field == NULL) {
                recover_from(parser, was);
            } else {
                field->own = own;
                list_push(parser, &fields, field);
                end_statement(parser);
            }
            skip_newlines(parser);
            if (parser->out_of_memory) {
                return decl;
            }
        }
        KestSpan close = current_span(parser);
        expect(parser, KEST_TOK_RBRACE);

        decl->record.fields = (KestField **)list_taken(parser, &fields);
        decl->record.field_count = fields.count;
        decl->span = span_between(start, close);
        return decl;
    }

    if (match(parser, KEST_TOK_ENUM)) {
        KestDecl *decl = new_decl(parser, KEST_DECL_ENUM, start);
        if (decl == NULL) {
            return NULL;
        }
        decl->name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            return NULL;
        }
        // An enum takes types the way a struct does, and for the same reason:
        // `Answer<T>` is one shape written once, and the language already has
        // the other half of it in `T?`. See D1048.
        if (!parse_type_params(parser, decl) ||
            !expect(parser, KEST_TOK_LBRACE)) {
            return NULL;
        }

        List cases = {0};
        skip_newlines(parser);
        while (!check(parser, KEST_TOK_RBRACE) && !check(parser, KEST_TOK_EOF)) {
            KestVariant *variant = KEST_ARENA_NEW(parser->arena, KestVariant);
            if (variant == NULL) {
                parser->out_of_memory = true;
                return NULL;
            }
            variant->name = current_span(parser);
            if (!expect(parser, KEST_TOK_IDENT)) {
                return NULL;
            }
            // What a case carries is a list of types by position, the way
            // what it is built with is a list of values by position.
            if (match(parser, KEST_TOK_LPAREN)) {
                List types = {0};
                if (!check(parser, KEST_TOK_RPAREN)) {
                    do {
                        KestTypeRef *type = parse_type(parser);
                        if (type == NULL) {
                            return NULL;
                        }
                        list_push(parser, &types, type);
                    } while (match(parser, KEST_TOK_COMMA));
                }
                expect(parser, KEST_TOK_RPAREN);
                variant->payload = (KestTypeRef **)list_taken(parser, &types);
                variant->payload_count = types.count;
            }
            list_push(parser, &cases, variant);
            end_statement(parser);
            skip_newlines(parser);
            if (parser->out_of_memory) {
                return decl;
            }
        }
        KestSpan close = current_span(parser);
        expect(parser, KEST_TOK_RBRACE);

        decl->choice.cases = (KestVariant **)list_taken(parser, &cases);
        decl->choice.case_count = cases.count;
        decl->span = span_between(start, close);
        return decl;
    }

    // `flags` is a word rather than a keyword: it means a declaration only
    // where one begins, and everywhere else it is a name, so `npc.flags` and
    // a module called `flags` keep working. `no.alloc` is read the same way.
    if (is_word(parser, 0, "flags") &&
        peek_at(parser, 1).kind == KEST_TOK_IDENT &&
        peek_at(parser, 2).kind == KEST_TOK_COLON) {
        advance(parser);
        KestDecl *decl = new_decl(parser, KEST_DECL_FLAGS, start);
        if (decl == NULL) {
            return NULL;
        }
        decl->name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT) || !expect(parser, KEST_TOK_COLON)) {
            return NULL;
        }
        // The width is what a host sees, so it is written rather than counted
        // off the names: a ninth flag has to be a decision, not a surprise.
        decl->choice.width = parse_type(parser);
        if (decl->choice.width == NULL || !expect(parser, KEST_TOK_LBRACE)) {
            return NULL;
        }

        List cases = {0};
        skip_newlines(parser);
        while (!check(parser, KEST_TOK_RBRACE) && !check(parser, KEST_TOK_EOF)) {
            KestVariant *variant = KEST_ARENA_NEW(parser->arena, KestVariant);
            if (variant == NULL) {
                parser->out_of_memory = true;
                return NULL;
            }
            variant->name = current_span(parser);
            if (!expect(parser, KEST_TOK_IDENT)) {
                return NULL;
            }
            list_push(parser, &cases, variant);
            end_statement(parser);
            skip_newlines(parser);
            if (parser->out_of_memory) {
                return decl;
            }
        }
        KestSpan close = current_span(parser);
        expect(parser, KEST_TOK_RBRACE);

        decl->choice.cases = (KestVariant **)list_taken(parser, &cases);
        decl->choice.case_count = cases.count;
        decl->span = span_between(start, close);
        return decl;
    }

    if (check(parser, KEST_TOK_EXTERN)) {
        advance(parser);
        if (!check(parser, KEST_TOK_FN)) {
            KestToken found = peek(parser);
            error_at(parser, found.span, "K0201", "expected %s, found %s",
                     kest_token_name(KEST_TOK_FN),
                     kest_token_name(found.kind));
            return NULL;
        }
        return parse_function(parser, start, true);
    }

    if (check(parser, KEST_TOK_FN)) {
        return parse_function(parser, start, false);
    }

    // A flag set that does not say how wide it is. It is refused either way;
    // what changes is whether the reader is told which of the eight things a
    // file holds they nearly wrote.
    if (is_word(parser, 0, "flags") && peek_at(parser, 1).kind == KEST_TOK_IDENT &&
        peek_at(parser, 2).kind == KEST_TOK_LBRACE) {
        KestSpan name = peek_at(parser, 1).span;
        error_at(parser, peek(parser).span, "K0212",
                 "a flag set says how wide it is");
        suggest(parser,
                "the width is what a host sees, so it is written "
                "rather than counted off the names: `flags %.*s: "
                "u8 {`",
                (int)name.length, span_text(parser, name));
        return NULL;
    }

    // `flags` beginning a declaration that neither arm above could read.
    // Without this it falls through to the end, where the caret sits on the
    // word `flags` under a line saying that a file holds `flags`. See D514.
    if (is_word(parser, 0, "flags")) {
        advance(parser);
        if (expect(parser, KEST_TOK_IDENT)) {
            KestToken after = peek(parser);
            error_at(parser, after.span, "K0201", "expected `:`, found %s",
                     kest_token_name(after.kind));
            suggest(parser,
                    "a flag set says how wide it is: "
                    "`flags Name: u8 {`");
        }
        return NULL;
    }

    KestToken found = peek(parser);
    error_at(parser, found.span, "K0202",
             "expected a declaration, found %s", kest_token_name(found.kind));
    const char *nearly =
        found.kind == KEST_TOK_IDENT
            ? kest_nearest_keyword(span_text(parser, found.span),
                                   found.span.length)
            : NULL;
    if (nearly != NULL) {
        suggest(parser, "did you mean `%s`?", nearly);
    } else {
        suggest(parser,
                "a file holds `module`, `import`, `const`, "
                "`struct`, `enum`, `flags`, `fn` and `extern fn`");
    }
    return NULL;
}

bool kest_parse(KestArena *arena, const KestSource *source, KestDiags *diags,
                KestUnit *unit) {
    Parser parser = {0};
    parser.arena = arena;
    parser.source = source;
    parser.diags = diags;

    // The tokens go in an arena of their own, because nothing wants them once
    // this returns: a node holds a span into the source and never a token, and
    // the source outlives everything. What the parser is holding while it
    // works is one file's tokens rather than every file's. See D747.
    KestArena *reading = kest_arena_new();
    if (reading == NULL) {
        return false;
    }
    // The same ceiling, so a file too big to read is refused where it was
    // refused before: work moved out of an arena is not work moved out of what
    // a host allowed.
    kest_arena_cap(reading, kest_arena_ceiling_left(arena));
    parser.tokens = kest_lex_all(reading, source, diags, &parser.count);
    if (parser.tokens == NULL) {
        // The bytes it did ask for before it ran out, which is what a ceiling
        // was refusing against — and what it was refused, which belongs to the
        // arena the ceiling is written on rather than to this one. See D843.
        kest_arena_charge(arena, kest_arena_used(reading));
        kest_arena_returned(arena, kest_arena_used(reading));
        kest_arena_also_refused(arena, reading);
        kest_arena_free(reading);
        return false;
    }

    List items = {0};
    skip_newlines(&parser);
    while (!check(&parser, KEST_TOK_EOF)) {
        KestDecl *decl = parse_declaration(&parser);
        if (decl == NULL) {
            recover_declaration(&parser);
        } else {
            list_push(&parser, &items, decl);
            end_statement(&parser);
        }
        skip_newlines(&parser);
        if (parser.out_of_memory) {
            kest_arena_charge(arena, kest_arena_used(reading));
            kest_arena_returned(arena, kest_arena_used(reading));
            kest_arena_free(reading);
            return false;
        }
    }

    unit->items = (KestDecl **)list_taken(&parser, &items);
    unit->count = items.count;
    unit->nodes = parser.nodes;
    bool read = !parser.out_of_memory;
    kest_arena_charge(arena, kest_arena_used(reading));
    kest_arena_returned(arena, kest_arena_used(reading));
    kest_arena_free(reading);
    return read;
}
