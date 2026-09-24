#include "fmt.h"

#include <string.h>


#define LINE_LIMIT 80

typedef struct {
    const KestSource *source;
    KestArena *arena;
    char *buffer;
    size_t used;
    size_t capacity;
    bool out_of_memory;
    int depth;
    // How far along the line the printer is, and whether it is printing at
    // all. Measuring is printing with the writing turned off, so there is one
    // description of what a thing looks like rather than two that can drift.
    uint32_t column;
    bool counting;
    // Set while a condition is being printed. A broken condition indents one
    // level further than a broken anything else, because a condition is the
    // only expression with a block starting one level in right after it.
    bool in_condition;
    // How much follows the thing being printed on the same line and has to fit
    // on it as well: a condition is followed by ` {`, and one that fills the
    // line exactly does not fit.
    uint32_t tail;
    // Set while the code inside a text hole is being printed. A text literal
    // is one line by what it is, so nothing inside one may break, however
    // long it runs.
    bool flat;
    // Comments in the order they appear, and how far through them the printer
    // has got. Each is emitted before the first thing that starts after it.
    KestSpan *comments;
    uint32_t comment_count;
    uint32_t comment_next;
    // Where the last thing printed ended, so a blank line the author left
    // between two things can be left there.
    uint32_t previous_line;
} Printer;

static void put_bytes(Printer *printer, const char *text, size_t length) {
    if (!printer->counting && !printer->out_of_memory) {
        if (printer->used + length + 1 > printer->capacity) {
            size_t capacity = printer->capacity == 0 ? 4096 : printer->capacity;
            while (printer->used + length + 1 > capacity) {
                capacity *= 2;
            }
            char *moved = kest_arena_alloc(printer->arena, capacity, 1);
            if (moved == NULL) {
                printer->out_of_memory = true;
                return;
            }
            if (printer->used > 0) {
                memcpy(moved, printer->buffer, printer->used);
            }
            printer->buffer = moved;
            printer->capacity = capacity;
        }
        memcpy(printer->buffer + printer->used, text, length);
        printer->used += length;
    }
    for (size_t i = 0; i < length; i++) {
        printer->column = text[i] == '\n' ? 0 : printer->column + 1;
    }
}

static void put(Printer *printer, const char *text) {
    put_bytes(printer, text, strlen(text));
}

static void put_char(Printer *printer, char c) {
    put_bytes(printer, &c, 1);
}

static void put_spaces(Printer *printer, int count) {
    for (int i = 0; i < count; i++) {
        put_char(printer, ' ');
    }
}

static uint32_t line_of(Printer *printer, uint32_t offset) {
    uint32_t line = 0;
    uint32_t column = 0;
    kest_source_locate(printer->source, offset, &line, &column);
    return line;
}

static void indent(Printer *printer) {
    put_spaces(printer, printer->depth * 4);
}

static void print_span(Printer *printer, KestSpan span) {
    put_bytes(printer, kest_span_text(printer->source, span), span.length);
}

// One blank line where the author left one or more, and none where they left
// none. Two blank lines are a preference; one is a paragraph.
static void separate(Printer *printer, uint32_t line) {
    if (printer->previous_line != 0 && line > printer->previous_line + 1) {
        put_char(printer, '\n');
    }
}

// Everything written before `offset` comes out first, at the indent of what it
// was written above.
// `at` is the line a comment is written above rather than the one it was
// written on, for the ones that are being lifted out of the middle of
// something. Nought where they are not: a comment left where it was keeps the
// blank line the author left above it, and one lifted out of a thing never
// gains one.
static void flush_comments_above(Printer *printer, uint32_t offset,
                                 uint32_t at) {
    while (printer->comment_next < printer->comment_count &&
           printer->comments[printer->comment_next].offset < offset) {
        KestSpan span = printer->comments[printer->comment_next++];
        uint32_t line = line_of(printer, span.offset);
        if (at != 0 && line > at) {
            line = at;
        }
        // Without whatever was left at the end of it. A comment is kept as it
        // was written, and space nobody can see is not something anybody
        // wrote: two files differing only in it are the same words, and a
        // form that keeps it is a form there are two of. See D395.
        while (span.length > 0) {
            char last = printer->source->text[span.offset + span.length - 1];
            if (last != ' ' && last != '\t') {
                break;
            }
            span.length--;
        }
        separate(printer, line);
        indent(printer);
        print_span(printer, span);
        put_char(printer, '\n');
        printer->previous_line = line;
    }
}

static void flush_comments(Printer *printer, uint32_t offset) {
    flush_comments_above(printer, offset, 0);
}

// Everything written on the line a thing starts on was written about that
// thing, so a comment there is put above it rather than above whatever comes
// after it. `let x = 1 // trailing` used to leave `// trailing` above the next
// statement, which is a comment about something the author did not write it
// about.
static uint32_t rest_of_line(const Printer *printer, uint32_t offset) {
    uint32_t at = offset;
    while (at < printer->source->length && printer->source->text[at] != '\n') {
        at++;
    }
    return at;
}

// The same, for a thing whose whole of itself is printed on one line however
// many the author wrote it over: a comment anywhere inside it was written
// about it.
static void lead_through(Printer *printer, uint32_t offset, uint32_t through) {
    flush_comments_above(printer, through, line_of(printer, offset));
    separate(printer, line_of(printer, offset));
    printer->previous_line = line_of(printer, offset);
}

// Whether anything in here is printed as a block. A comment written inside
// something that comes out on one line was written about that thing and is
// lifted above it; a comment written inside a body belongs where it is, and
// lifting one out of a `while` would put what was said about a line of the
// loop above the loop.
//
// Written out rather than left to a `default`, because a kind of expression
// added without a decision about this is one more thing a comment could be
// lifted out of quietly. See D398.
static bool holds_a_body(const KestExpr *expr) {
    if (expr == NULL) {
        return false;
    }
    switch (expr->kind) {
    case KEST_EXPR_INT:
    case KEST_EXPR_FLOAT:
    case KEST_EXPR_STRING:
    case KEST_EXPR_BYTE:
    case KEST_EXPR_BOOL:
    case KEST_EXPR_NAME:
    case KEST_EXPR_NONE:
        return false;
    case KEST_EXPR_UNARY:
        return holds_a_body(expr->unary.operand);
    case KEST_EXPR_BINARY:
        return holds_a_body(expr->binary.left) ||
               holds_a_body(expr->binary.right);
    case KEST_EXPR_CALL: {
        if (holds_a_body(expr->call.callee)) {
            return true;
        }
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            if (holds_a_body(expr->call.args[i])) {
                return true;
            }
        }
        return false;
    }
    case KEST_EXPR_FIELD:
        return holds_a_body(expr->field.object);
    case KEST_EXPR_INDEX:
        return holds_a_body(expr->index.object) ||
               holds_a_body(expr->index.index);
    case KEST_EXPR_ARRAY: {
        for (uint32_t i = 0; i < expr->array.count; i++) {
            if (holds_a_body(expr->array.items[i])) {
                return true;
            }
        }
        return false;
    }
    case KEST_EXPR_TEXT: {
        for (uint32_t i = 0; i < expr->text.count; i++) {
            if (holds_a_body(expr->text.parts[i].value)) {
                return true;
            }
        }
        return false;
    }
    case KEST_EXPR_MATCH:
        // Arms are lines of their own however they are written.
        return true;
    case KEST_EXPR_IF:
        return expr->branch->then_body.count > 0 ||
               expr->branch->else_body.count > 0 ||
               holds_a_body(expr->branch->condition) ||
               holds_a_body(expr->branch->then_value) ||
               holds_a_body(expr->branch->else_value) ||
               holds_a_body(expr->branch->otherwise);
    }
    return false;
}

// What comes before a thing: its comments, then a blank line if there was one.
static void lead(Printer *printer, uint32_t offset) {
    flush_comments(printer, rest_of_line(printer, offset));
    separate(printer, line_of(printer, offset));
    printer->previous_line = line_of(printer, offset);
}

// A dotted name, which is more than one token and is held in one span. What
// is between the pieces is whitespace a line may have been ended in — `.`
// carries on to the next line — and none of it is part of the name, so it is
// left out rather than copied. Everything else printed from a span is one
// token and has nothing inside it. See D397.
static void print_name(Printer *printer, KestSpan span) {
    for (uint32_t i = 0; i < span.length; i++) {
        char c = printer->source->text[span.offset + i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            put_char(printer, c);
        }
    }
}

static void print_type(Printer *printer, const KestTypeRef *type) {
    if (type == NULL) {
        return;
    }
    switch (type->kind) {
    case KEST_TYPE_NAMED:
        // A dotted one is more than one token, and what a line break left
        // between the pieces is not part of it.
        print_name(printer, type->name);
        break;
    case KEST_TYPE_GENERIC:
        print_name(printer, type->name);
        put_char(printer, '<');
        for (uint32_t i = 0; i < type->arg_count; i++) {
            put(printer, i > 0 ? ", " : "");
            print_type(printer, type->args[i]);
        }
        put_char(printer, '>');
        break;
    case KEST_TYPE_ARRAY:
        put_char(printer, '[');
        print_type(printer, type->element);
        if (type->count.length > 0) {
            put(printer, "; ");
            // A count may be a name with a dot in it, and what a line break
            // left between the pieces is not part of it — the same reason the
            // type beside it is printed this way. See D682.
            print_name(printer, type->count);
        }
        put_char(printer, ']');
        break;
    case KEST_TYPE_OPTIONAL:
        print_type(printer, type->element);
        put_char(printer, '?');
        break;
    case KEST_TYPE_FN:
        put(printer, "fn(");
        for (uint32_t i = 0; i < type->arg_count; i++) {
            put(printer, i > 0 ? ", " : "");
            print_type(printer, type->args[i]);
        }
        put_char(printer, ')');
        if (type->element != NULL) {
            put(printer, " -> ");
            print_type(printer, type->element);
        }
        // One order wherever they are written: two orders would be two forms
        // of one thing, and this language has one form. See D853.
        if (type->no_alloc) {
            put(printer, " no.alloc");
        }
        if (type->no_host) {
            put(printer, " no.host");
        }
        if (type->deterministic) {
            put(printer, " deterministic");
        }
        break;
    }
}

// The parser's, so the two cannot disagree about where brackets are needed.
// See D1236.
static int precedence_of(KestTokenKind op) {
    return kest_binary_precedence(op);
}

static void print_operator(Printer *printer, KestTokenKind op) {
    char bare[KEST_TOKEN_NAME_ROOM];
    put(printer, kest_token_bare(op, bare, sizeof(bare)));
}

static void print_expr(Printer *printer, const KestExpr *expr, int outer);
static void print_block(Printer *printer, const KestBlock *block,
                        uint32_t closing);
static void print_condition(Printer *printer, const KestExpr *expr,
                            uint32_t tail);
static void lead(Printer *printer, uint32_t offset);

// The condition of a block, which breaks one level deeper than anything else.
// `tail` is what comes after it on the line, which is always something: a
// brace, or the arrow of an `if` that gives a value.
static void print_condition(Printer *printer, const KestExpr *expr,
                            uint32_t tail) {
    bool was = printer->in_condition;
    uint32_t held = printer->tail;
    printer->in_condition = true;
    printer->tail = tail;
    print_expr(printer, expr, 0);
    printer->in_condition = was;
    printer->tail = held;
}

// What a line may hold here: everything up to the limit, less whatever is
// going to follow on the same line.
static uint32_t room(const Printer *printer) {
    return LINE_LIMIT - printer->tail;
}

// How wide this would be from here, found by printing it with the writing
// turned off.
static uint32_t measure(Printer *printer, const KestExpr *expr) {
    bool was_counting = printer->counting;
    uint32_t start = printer->column;
    printer->counting = true;
    print_expr(printer, expr, 0);
    uint32_t width = printer->column - start;
    printer->counting = was_counting;
    printer->column = start;
    return width;
}

// A list too long for the line goes one item to a line, all of them or none.
// Half of them on one line and half on the next is the arrangement nobody
// asked for.
static bool fits(Printer *printer, const KestExpr *expr, uint32_t count) {
    // While measuring, the answer is the width of the flat form, which is the
    // thing being measured. Asking again here is how this first went round
    // forever.
    if (printer->counting || printer->flat || count < 2) {
        return true;
    }
    return printer->column + measure(printer, expr) <= room(printer);
}

static void print_items(Printer *printer, KestExpr **items, uint32_t count,
                        bool broken) {
    bool was = printer->in_condition;
    printer->in_condition = false;
    if (!broken) {
        for (uint32_t i = 0; i < count; i++) {
            put(printer, i > 0 ? ", " : "");
            print_expr(printer, items[i], 0);
        }
        printer->in_condition = was;
        return;
    }
    printer->depth++;
    for (uint32_t i = 0; i < count; i++) {
        put_char(printer, '\n');
        indent(printer);
        print_expr(printer, items[i], 0);
        if (i + 1 < count) {
            put_char(printer, ',');
        }
    }
    printer->depth--;
    put_char(printer, '\n');
    indent(printer);
    printer->in_condition = was;
}

// A bracket goes back only where taking it away would change what binds to
// what, which is why the tree is what decides and not what was written.
// What an `if` or a `match` that gives a value does to whatever comes after
// it: the arm takes as much as it can, so `(if c -> a else -> b) - 2` without
// its brackets is an `else` arm of `b - 2` and a different program. Those have
// no precedence to compare against -- they end where the expression holding
// them ends -- so one written where something follows is always bracketed.
// Printing one without was a formatter that wrote a different program and said
// nothing about it. See D1085.
static bool takes_what_follows(const KestExpr *expr) {
    return expr != NULL &&
           (expr->kind == KEST_EXPR_IF || expr->kind == KEST_EXPR_MATCH);
}

static void print_operand(Printer *printer, const KestExpr *expr, int limit) {
    bool needs =
        expr != NULL &&
        ((expr->kind == KEST_EXPR_BINARY &&
          precedence_of(expr->binary.op) < limit) ||
         takes_what_follows(expr));
    if (needs) {
        put_char(printer, '(');
    }
    print_expr(printer, expr, needs ? 0 : limit);
    if (needs) {
        put_char(printer, ')');
    }
}

// How much room ` else -> ...` wants, so an `if` that gives a value knows
// whether the rest of it fits on the line it is on.
static uint32_t else_width(Printer *printer, const KestBranch *branch) {
    uint32_t width = (uint32_t)strlen(" else");
    if (branch->otherwise != NULL) {
        return width + 1 + measure(printer, branch->otherwise);
    }
    if (branch->else_value != NULL) {
        return width + (uint32_t)strlen(" -> ") +
           measure(printer, branch->else_value);
    }
    return width;
}

static void print_expr(Printer *printer, const KestExpr *expr, int outer) {
    (void)outer;
    if (expr == NULL) {
        return;
    }

    switch (expr->kind) {
    case KEST_EXPR_INT:
    case KEST_EXPR_FLOAT:
    case KEST_EXPR_NAME:
    case KEST_EXPR_BYTE:
    case KEST_EXPR_STRING:
        // As written. A number's spelling and a string's contents are the
        // author's, not the formatter's.
        print_span(printer, expr->span);
        break;
    case KEST_EXPR_TEXT: {
        // What is between the holes is the author's and is copied out as
        // written, escapes and all. What is in a hole is code, and code in
        // this language has one form, so it is printed like any other — flat,
        // because a text literal is one line by what it is.
        bool was = printer->flat;
        printer->flat = true;
        put_char(printer, '"');
        for (uint32_t i = 0; i < expr->text.count; i++) {
            const KestTextPart *part = &expr->text.parts[i];
            if (part->value == NULL) {
                print_span(printer, part->text);
                continue;
            }
            put_char(printer, '{');
            print_expr(printer, part->value, 0);
            put_char(printer, '}');
        }
        put_char(printer, '"');
        printer->flat = was;
        break;
    }
    case KEST_EXPR_BOOL:
        put(printer, expr->boolean ? "true" : "false");
        break;
    case KEST_EXPR_NONE:
        put(printer, "none");
        break;
    case KEST_EXPR_UNARY:
        print_operator(printer, expr->unary.op);
        print_operand(printer, expr->unary.operand, 6);
        break;
    case KEST_EXPR_BINARY: {
        int level = precedence_of(expr->binary.op);

        // The tree nests to the left, so `a || b || c` is two nodes and
        // breaking the top one alone would put `(a || b)` on a line by itself.
        // Everything at this precedence is one chain and breaks as one.
        //
        // As long as it is. This was a run of thirty-two, and a chain with
        // more came out as a chain of thirty-two whose left side was the rest
        // of itself: eight on the first line and one on every line after,
        // which is the arrangement nobody asked for, arrived at quietly.
        uint32_t count = 0;
        const KestExpr *head = expr;
        while (head->kind == KEST_EXPR_BINARY &&
               precedence_of(head->binary.op) == level) {
            count++;
            head = head->binary.left;
        }
        const KestExpr **rights =
            KEST_ARENA_ARRAY(printer->arena, const KestExpr *, count);
        KestTokenKind *operators =
            KEST_ARENA_ARRAY(printer->arena, KestTokenKind, count);
        if (rights == NULL || operators == NULL) {
            printer->out_of_memory = true;
            break;
        }
        uint32_t at = 0;
        for (const KestExpr *walk = expr; at < count;
             walk = walk->binary.left) {
            rights[at] = walk->binary.right;
            operators[at] = walk->binary.op;
            at++;
        }

        bool may_break = !printer->counting && !printer->flat;

        print_operand(printer, head, level);
        // All of them or none, like a list — but asked after the left is
        // printed, and about what is left to print. Asking beforehand, from
        // the flat width of the whole thing, broke chains whose left side had
        // already broken inside itself: `1.0) >` and `0.0001 {` on two lines
        // with thirty columns spare on the first. The flat width of the rest
        // is the whole less the head, because flat is exactly what those two
        // measure.
        // Measured only when it can be acted on: measuring is printing with
        // the writing off, and measuring this from inside itself is how it
        // first went round forever.
        // And only where the break would be legal. The operator ends the line,
        // so the line has to be one that carries on — and `>` is the one
        // operator a line may end after, because `ref<Npc>` ends in one. A
        // chain holding one of those is written on the line it is on however
        // long that is: breaking before the operator would end the line on a
        // value, which is the same refusal from the other side. See D384.
        bool carries_on = true;
        for (uint32_t i = 0; i < count; i++) {
            if (kest_lexer_ends_statement(operators[i])) {
                carries_on = false;
            }
        }
        uint32_t rest =
            may_break ? measure(printer, expr) - measure(printer, head) : 0;
        bool broken =
            may_break && carries_on && printer->column + rest > room(printer);
        printer->depth += printer->in_condition ? 2 : 1;
        for (uint32_t i = count; i > 0; i--) {
            // The operator ends the line rather than starting the next one,
            // because D003 is what makes the break legal: a line that ends in
            // an operator continues, and one that ends in a value does not.
            put_char(printer, ' ');
            print_operator(printer, operators[i - 1]);
            if (broken) {
                put_char(printer, '\n');
                indent(printer);
            } else {
                put_char(printer, ' ');
            }
            // The right side of a left-associative operator needs a bracket
            // at equal precedence, because without one it would regroup.
            print_operand(printer, rights[i - 1], level + 1);
        }
        printer->depth -= printer->in_condition ? 2 : 1;
        break;
    }
    case KEST_EXPR_CALL: {
        bool broken = !fits(printer, expr, expr->call.arg_count);
        print_operand(printer, expr->call.callee, 6);
        put_char(printer, '(');
        print_items(printer, expr->call.args, expr->call.arg_count, broken);
        put_char(printer, ')');
        break;
    }
    case KEST_EXPR_FIELD:
        print_operand(printer, expr->field.object, 6);
        put_char(printer, '.');
        print_span(printer, expr->field.name);
        break;
    case KEST_EXPR_INDEX:
        print_operand(printer, expr->index.object, 6);
        put_char(printer, '[');
        print_expr(printer, expr->index.index, 0);
        put_char(printer, ']');
        break;
    case KEST_EXPR_IF: {
        const KestBranch *branch = expr->branch;
        put(printer, "if ");
        if (branch->binding.length > 0) {
            put(printer, "let ");
            print_span(printer, branch->binding);
            put(printer, " = ");
        }
        print_condition(printer, branch->condition,
                        branch->then_value != NULL ? 4 : 2);
        uint32_t after = expr->span.offset + expr->span.length;
        if (branch->then_value != NULL) {
            put(printer, " -> ");
            print_expr(printer, branch->then_value, 0);
        } else {
            print_block(printer, &branch->then_body, after);
        }
        // An `if` that gives a value has one place it can break: before the
        // `else`, which is why the parser looks past a line break for one.
        // Everything else about it is one line by what it is.
        bool split = branch->then_value != NULL && !printer->counting &&
                     !printer->flat &&
                     printer->column + else_width(printer, branch) >
                         room(printer);
        if (split) {
            printer->depth++;
            put_char(printer, '\n');
            indent(printer);
            printer->depth--;
        }
        if (branch->otherwise != NULL) {
            // The chain is one thing to a reader, so its arms carry on the
            // same line rather than each starting one.
            put(printer, split ? "else " : " else ");
            print_expr(printer, branch->otherwise, 0);
        } else if (branch->has_else) {
            put(printer, split ? "else" : " else");
            if (branch->else_value != NULL) {
                put(printer, " -> ");
                print_expr(printer, branch->else_value, 0);
            } else {
                print_block(printer, &branch->else_body, after);
            }
        }
        break;
    }

    case KEST_EXPR_MATCH: {
        put(printer, "match ");
        for (uint32_t i = 0; i < expr->choose->subject_count; i++) {
            put(printer, i == 0 ? "" : ", ");
            print_condition(printer, expr->choose->subjects[i], 2);
        }
        put(printer, " {\n");
        printer->depth++;
        uint32_t was = printer->previous_line;
        printer->previous_line = 0;
        for (uint32_t i = 0; i < expr->choose->arm_count; i++) {
            const KestArm *arm = &expr->choose->arms[i];
            uint32_t begins = arm->span.length > 0 ? arm->span.offset
                                                   : expr->span.offset;
            // An arm that gives a value is one line when it is printed, so
            // everything written inside it — over however many lines the
            // author took — belongs above it.
            if (arm->value != NULL) {
                // To the end of the line the value ends on, not to the end of
                // the value: what is written after it on that line was written
                // about this arm, and stopping at the value left it above the
                // arm underneath — a comment about something the author did
                // not write it about, which is the mistake `rest_of_line`
                // exists to prevent and which arms were not going through.
                // See D389.
                lead_through(printer, begins,
                             rest_of_line(printer,
                                          arm->value->span.offset +
                                              arm->value->span.length));
            } else {
                lead(printer, begins);
            }
            indent(printer);
            for (uint32_t p = 0; p < arm->part_count; p++) {
                const KestArmPart *part = &arm->parts[p];
                put(printer, p == 0 ? "" : ", ");
                if (part->name.length == 0) {
                    put(printer, "else");
                    continue;
                }
                print_span(printer, part->name);
                if (part->binding_count > 0) {
                    put_char(printer, '(');
                    for (uint32_t b = 0; b < part->binding_count; b++) {
                        put(printer, b == 0 ? "" : ", ");
                        print_span(printer, part->bindings[b]);
                    }
                    put_char(printer, ')');
                }
            }
            if (arm->value != NULL) {
                // An arm that will not fit breaks after its arrow, which is
                // the one place it can: a line ending in `->` carries on, and
                // what follows is the whole of what the arm gives.
                put(printer, " ->");
                bool wrapped = !printer->counting && !printer->flat &&
                               printer->column + 1 +
                                       measure(printer, arm->value) >
                                   room(printer);
                if (wrapped) {
                    printer->depth++;
                    put_char(printer, '\n');
                    indent(printer);
                } else {
                    put_char(printer, ' ');
                }
                print_expr(printer, arm->value, 0);
                if (wrapped) {
                    printer->depth--;
                }
                put_char(printer, '\n');
                // Where the value ended, not where the arm began. An arm whose
                // value was written on a line of its own is two lines, and the
                // arm after it then looked a line further down than it was:
                // formatting the file again put a blank line under every
                // wrapped arm, and again under that. An arm with a body needs
                // none of this, because the block says where it ended itself.
                // It is the mistake a statement had, in the other place it
                // could be made.
                printer->previous_line = line_of(
                    printer, arm->value->span.offset + arm->value->span.length);
            } else {
                print_block(printer, &arm->body,
                            expr->span.offset + expr->span.length);
                put_char(printer, '\n');
            }
        }
        // And the brace that ends the arms, which is the last place a comment
        // can be written inside a `match`: what is on its line was written
        // about this, and nothing was flushing it at all. Before the indent
        // comes back out, so a comment about the end of the arms is written
        // where the arms are.
        flush_comments(printer,
                       rest_of_line(printer,
                                    expr->span.offset + expr->span.length));
        printer->depth--;
        indent(printer);
        put_char(printer, '}');
        printer->previous_line = was;
        break;
    }

    case KEST_EXPR_ARRAY: {
        bool broken = !fits(printer, expr, expr->array.count);
        put_char(printer, '[');
        print_items(printer, expr->array.items, expr->array.count, broken);
        put_char(printer, ']');
        break;
    }
    }
}

static void print_block(Printer *printer, const KestBlock *block,
                        uint32_t closing);

// `bare` is set for the arm of an `else if`, which continues a line rather
// than starting one, and therefore takes neither the comments above it nor the
// indent.
// Whether the whole of this statement comes out on one line, however many the
// author wrote it over. Then a comment anywhere inside it was written about it
// and is lifted above it, the way an arm's is; a statement with a body keeps
// what is written inside the body where it is.
static bool prints_flat(const KestStmt *stmt) {
    switch (stmt->kind) {
    case KEST_STMT_LET:
        return !holds_a_body(stmt->let.value);
    case KEST_STMT_ASSIGN:
        return !holds_a_body(stmt->assign.target) &&
               !holds_a_body(stmt->assign.value);
    case KEST_STMT_EXPR:
    case KEST_STMT_DEFER:
    case KEST_STMT_RETURN:
        return !holds_a_body(stmt->value);
    case KEST_STMT_BREAK:
    case KEST_STMT_CONTINUE:
        return true;
    case KEST_STMT_WHILE:
    case KEST_STMT_FOR:
    case KEST_STMT_SCRATCH:
    case KEST_STMT_BLOCK:
        return false;
    }
    return false;
}

static void print_stmt(Printer *printer, const KestStmt *stmt, bool bare) {
    if (!bare) {
        // Through the whole of it when the whole of it is one line. A
        // statement written over two lines had whatever was at the end of the
        // second one left behind, and it came out above the statement after —
        // a comment about something the author did not write it about, which
        // is what `rest_of_line` is for and what a statement longer than a
        // line was slipping past. See D398.
        if (prints_flat(stmt) && stmt->span.length > 0) {
            lead_through(printer, stmt->span.offset,
                         rest_of_line(printer, stmt->span.offset +
                                                   stmt->span.length));
        } else {
            lead(printer, stmt->span.offset);
        }
        indent(printer);
    }

    switch (stmt->kind) {
    case KEST_STMT_LET:
        put(printer, "let ");
        print_span(printer, stmt->let.name);
        if (stmt->let.type != NULL) {
            put(printer, ": ");
            print_type(printer, stmt->let.type);
        }
        put(printer, " = ");
        print_expr(printer, stmt->let.value, 0);
        put_char(printer, '\n');
        break;

    case KEST_STMT_ASSIGN:
        print_expr(printer, stmt->assign.target, 0);
        put_char(printer, ' ');
        print_operator(printer, stmt->assign.op);
        put_char(printer, ' ');
        print_expr(printer, stmt->assign.value, 0);
        put_char(printer, '\n');
        break;

    case KEST_STMT_EXPR:
        print_expr(printer, stmt->value, 0);
        put_char(printer, '\n');
        break;

    case KEST_STMT_DEFER:
        put(printer, "defer ");
        print_expr(printer, stmt->value, 0);
        put_char(printer, '\n');
        break;

    case KEST_STMT_WHILE:
        put(printer, "while ");
        if (stmt->loop.binding.length > 0) {
            put(printer, "let ");
            print_span(printer, stmt->loop.binding);
            put(printer, " = ");
        }
        print_condition(printer, stmt->loop.condition, 2);
        print_block(printer, &stmt->loop.body,
                    stmt->span.offset + stmt->span.length);
        put_char(printer, '\n');
        break;

    case KEST_STMT_FOR:
        put(printer, "for ");
        if (stmt->each->index.length > 0) {
            print_span(printer, stmt->each->index);
            put(printer, ", ");
        }
        print_span(printer, stmt->each->name);
        put(printer, " in ");
        print_expr(printer, stmt->each->sequence, 0);
        if (stmt->each->until != NULL) {
            put(printer, "..");
            print_expr(printer, stmt->each->until, 0);
        }
        print_block(printer, &stmt->each->body,
                    stmt->span.offset + stmt->span.length);
        put_char(printer, '\n');
        break;

    case KEST_STMT_RETURN:
        put(printer, "return");
        if (stmt->result != NULL) {
            put_char(printer, ' ');
            print_expr(printer, stmt->result, 0);
        }
        put_char(printer, '\n');
        break;

    case KEST_STMT_BREAK:
        put(printer, "break\n");
        break;

    case KEST_STMT_CONTINUE:
        put(printer, "continue\n");
        break;

    case KEST_STMT_SCRATCH:
    case KEST_STMT_BLOCK:
        put(printer, stmt->kind == KEST_STMT_SCRATCH ? "scratch {\n" : "{\n");
        printer->depth++;
        printer->previous_line = 0;
        for (uint32_t i = 0; i < stmt->block.count; i++) {
            print_stmt(printer, stmt->block.items[i], false);
        }
        printer->depth--;
        indent(printer);
        put(printer, "}\n");
        break;
    }

    // Where this statement ended, not where it began. A broken argument list
    // makes those different lines, and using the first one put a blank line
    // after every one of them.
    if (stmt->span.length > 0) {
        printer->previous_line =
            line_of(printer, stmt->span.offset + stmt->span.length - 1);
    }
}

static void print_block(Printer *printer, const KestBlock *block,
                        uint32_t closing) {
    put(printer, " {\n");
    printer->depth++;
    // Nothing is separated from the brace that opened it, so a blank line
    // right after `{` goes and a broken condition does not make one.
    printer->previous_line = 0;
    for (uint32_t i = 0; i < block->count; i++) {
        print_stmt(printer, block->items[i], false);
    }
    // To the end of the line the brace is on, not to the brace: what is
    // written after `}` on that line was written about the block that is
    // ending, and stopping at the brace left it above whatever came next —
    // the next declaration, the next statement, or the end of the file. It is
    // the same rule `lead` keeps and the same one `match` arms were missing.
    // See D390.
    flush_comments(printer, rest_of_line(printer, closing));
    printer->depth--;
    indent(printer);
    put_char(printer, '}');
    // Where the closing brace is, so a blank line the author left after it
    // survives. Forgetting this ate every blank line that followed a block.
    printer->previous_line = closing > 0 ? line_of(printer, closing - 1) : 0;
}

static void print_type_params(Printer *printer, const KestDecl *decl) {
    if (decl->type_param_count == 0) {
        return;
    }
    put_char(printer, '<');
    for (uint32_t i = 0; i < decl->type_param_count; i++) {
        put(printer, i > 0 ? ", " : "");
        print_span(printer, decl->type_params[i].name);
        // What it has to be able to do, in the order this file writes them in
        // rather than the order they were typed: one form of a file, and a
        // list whose order is a reader's choice is a list two files disagree
        // about. See D1043.
        uint8_t wants = decl->type_params[i].wants;
        if (wants == 0) {
            continue;
        }
        put(printer, ":");
        for (uint32_t at = 0; at < KEST_CAPABILITY_COUNT; at++) {
            if ((wants & kest_capability(at)->bit) == 0) {
                continue;
            }
            put_char(printer, ' ');
            put(printer, kest_capability(at)->word);
        }
    }
    put_char(printer, '>');
}

static void print_signature(Printer *printer, const KestDecl *decl) {
    put(printer, decl->function.is_extern ? "extern fn " : "fn ");
    if (decl->function.receiver.length > 0) {
        print_span(printer, decl->function.receiver);
        put_char(printer, '.');
    }
    print_span(printer, decl->name);
    print_type_params(printer, decl);
    put_char(printer, '(');
    for (uint32_t i = 0; i < decl->function.param_count; i++) {
        put(printer, i > 0 ? ", " : "");
        print_span(printer, decl->function.params[i]->name);
        put(printer, ": ");
        print_type(printer, decl->function.params[i]->type);
    }
    put_char(printer, ')');
    if (decl->function.result != NULL) {
        put(printer, " -> ");
        print_type(printer, decl->function.result);
    }
    if (decl->function.no_alloc) {
        put(printer, " no.alloc");
    }
    if (decl->function.no_host) {
        put(printer, " no.host");
    }
    if (decl->function.deterministic) {
        put(printer, " deterministic");
    }
}

// A declaration that occupies one line. A run of them is a list and reads as
// one; anything with a body is a paragraph.
static bool is_one_liner(const KestDecl *decl) {
    return decl->kind == KEST_DECL_MODULE || decl->kind == KEST_DECL_IMPORT ||
           decl->kind == KEST_DECL_CONST ||
           (decl->kind == KEST_DECL_FN && decl->function.is_extern);
}

static void print_decl(Printer *printer, const KestDecl *decl,
                       const KestDecl *previous) {
    // Declarations are paragraphs. Two of them run together only when both
    // are one line and the author had them that way.
    if (previous != NULL) {
        // From the first thing written above it rather than from the
        // declaration itself. A comment between two one-line declarations is
        // written about the second of them, so it is where the second one
        // begins as far as this is concerned — measuring from the declaration
        // counted the comment's own line as a gap, put a blank line in that
        // was not there, and made a file that had been formatted once come
        // out different when it was formatted again. See D394.
        uint32_t begins = decl->span.offset;
        if (printer->comment_next < printer->comment_count &&
            printer->comments[printer->comment_next].offset < begins) {
            begins = printer->comments[printer->comment_next].offset;
        }
        bool tight = is_one_liner(previous) && is_one_liner(decl) &&
                     line_of(printer, begins) <= printer->previous_line + 1;
        if (!tight) {
            // The blank goes above whatever was written about the
            // declaration, not between it and the declaration.
            put_char(printer, '\n');
            printer->previous_line = 0;
        }
    }
    lead(printer, decl->span.offset);

    switch (decl->kind) {
    case KEST_DECL_MODULE:
        put(printer, "module ");
        print_name(printer, decl->name);
        put_char(printer, '\n');
        break;
    case KEST_DECL_IMPORT:
        put(printer, "import ");
        print_name(printer, decl->name);
        put_char(printer, '\n');
        break;
    case KEST_DECL_CONST:
        put(printer, "const ");
        print_span(printer, decl->name);
        put(printer, ": ");
        print_type(printer, decl->constant.type);
        put(printer, " = ");
        print_expr(printer, decl->constant.value, 0);
        put_char(printer, '\n');
        break;
    case KEST_DECL_STRUCT:
        put(printer, "struct ");
        print_span(printer, decl->name);
        print_type_params(printer, decl);
        put(printer, " {\n");
        printer->depth++;
        printer->previous_line = 0;
        for (uint32_t i = 0; i < decl->record.field_count; i++) {
            const KestField *field = decl->record.fields[i];
            // Where the field before this one ended, which is where its type
            // ended. A field written over two lines — broken after the `:`,
            // which carries on — made the next one look further down than it
            // is, and a blank line went between them. See D397.
            if (i > 0) {
                const KestTypeRef *before = decl->record.fields[i - 1]->type;
                KestSpan ended = before != NULL
                                     ? before->span
                                     : decl->record.fields[i - 1]->name;
                printer->previous_line =
                    line_of(printer, ended.offset + ended.length);
            }
            // A marked field begins at the word and not at the name, and
            // the comments above it lead from where it begins.
            lead(printer, field->own.length != 0 ? field->own.offset
                                                 : field->name.offset);
            indent(printer);
            if (field->own.length != 0) {
                put(printer, "own ");
            }
            print_span(printer, field->name);
            put(printer, ": ");
            print_type(printer, field->type);
            put_char(printer, '\n');
        }
        flush_comments(printer,
                       rest_of_line(printer,
                                    decl->span.offset + decl->span.length));
        printer->depth--;
        put(printer, "}\n");
        break;
    case KEST_DECL_FLAGS:
        put(printer, "flags ");
        print_span(printer, decl->name);
        put(printer, ": ");
        print_type(printer, decl->choice.width);
        put(printer, " {\n");
        printer->depth++;
        printer->previous_line = 0;
        for (uint32_t i = 0; i < decl->choice.case_count; i++) {
            lead(printer, decl->choice.cases[i]->name.offset);
            indent(printer);
            print_span(printer, decl->choice.cases[i]->name);
            put_char(printer, '\n');
        }
        flush_comments(printer,
                       rest_of_line(printer,
                                    decl->span.offset + decl->span.length));
        printer->depth--;
        put(printer, "}\n");
        break;
    case KEST_DECL_ENUM:
        put(printer, "enum ");
        print_span(printer, decl->name);
        print_type_params(printer, decl);
        put(printer, " {\n");
        printer->depth++;
        printer->previous_line = 0;
        for (uint32_t i = 0; i < decl->choice.case_count; i++) {
            const KestVariant *variant = decl->choice.cases[i];
            // Where the case before this one ended, which is the end of the
            // last thing it carries. A case written over two lines — a
            // payload broken at its comma — made the one after it look two
            // lines down, and a blank line nobody wrote went between them. It
            // is the mistake a statement had, and an arm, and a declaration.
            // See D396.
            if (i > 0) {
                const KestVariant *before = decl->choice.cases[i - 1];
                KestSpan ended = before->payload_count > 0
                                     ? before->payload[before->payload_count -
                                                       1]->span
                                     : before->name;
                printer->previous_line =
                    line_of(printer, ended.offset + ended.length);
            }
            lead(printer, variant->name.offset);
            indent(printer);
            print_span(printer, variant->name);
            if (variant->payload_count > 0) {
                put_char(printer, '(');
                for (uint32_t p = 0; p < variant->payload_count; p++) {
                    put(printer, p == 0 ? "" : ", ");
                    print_type(printer, variant->payload[p]);
                }
                put_char(printer, ')');
            }
            put_char(printer, '\n');
        }
        flush_comments(printer,
                       rest_of_line(printer,
                                    decl->span.offset + decl->span.length));
        printer->depth--;
        put(printer, "}\n");
        break;
    case KEST_DECL_FN:
        print_signature(printer, decl);
        if (decl->function.is_extern) {
            put_char(printer, '\n');
            break;
        }
        print_block(printer, &decl->function.body,
                    decl->span.offset + decl->span.length);
        put_char(printer, '\n');
        break;
    }
    printer->previous_line = line_of(printer, decl->span.offset +
                                                  decl->span.length);
}

const char *kest_format(const KestUnit *unit, const KestSource *source,
                        KestArena *arena, size_t *length) {
    Printer printer = {0};
    printer.source = source;
    printer.arena = arena;
    // As many as there are. This was a run of four thousand and ninety-six,
    // and a file with more lost the rest without saying so — the count kept
    // was what fitted rather than what was there.
    printer.comment_count = kest_comments(source, NULL, 0);
    if (printer.comment_count > 0) {
        printer.comments =
            KEST_ARENA_ARRAY(arena, KestSpan, printer.comment_count);
        if (printer.comments == NULL) {
            return NULL;
        }
        kest_comments(source, printer.comments, printer.comment_count);
    }

    for (uint32_t i = 0; i < unit->count; i++) {
        print_decl(&printer, unit->items[i], i == 0 ? NULL : unit->items[i - 1]);
    }
    // Anything written after the last declaration is still the author's.
    flush_comments(&printer, (uint32_t)source->length);

    if (printer.out_of_memory) {
        return NULL;
    }
    if (printer.buffer == NULL) {
        *length = 0;
        return "";
    }
    printer.buffer[printer.used] = '\0';
    *length = printer.used;
    return printer.buffer;
}
