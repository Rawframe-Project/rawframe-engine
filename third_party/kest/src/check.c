#include "check.h"

#include <stdarg.h>
#include <string.h>

// A name visible at a point in a body. Depth is the block nesting it was
// declared at, so a scope can be dropped by rewinding the count.
typedef struct {
    const char *name;
    KestType *type;
    KestSpan span;
    uint32_t depth;
    // `for x in a` binds a copy of each element, and `for i, x` binds a copy
    // of the position. Assigning to either is legal and changes nothing that
    // outlives the turn, which is worth saying out loud.
    bool is_loop_element;
    bool is_loop_index;
    // A parameter, which is a value the caller handed over. Writing a field of
    // one changes this frame's copy and nothing the caller can see.
    bool is_parameter;
    // Whether a `let` declared it, and whether anything has read it since. The
    // two together are what says a name in a body was written for nothing: a
    // local is the one name in this language nobody outside the body can be
    // relying on. A loop's binding and a case's are not `let`s — one is how a
    // program says how many times to go round and the other is the only way to
    // write the case at all — so neither is asked. See D726.
    bool from_let;
    bool read;
    // And whether it was an `if let` rather than a `let`, which changes what
    // to do about it: a `let` nothing reads comes out, and an `if let` nothing
    // reads is a question asked the long way. See D728.
    bool from_if_let;
    // Whether the body assigns to this name. Asked about the two a walk binds,
    // because a name nothing writes can be the walk's own count rather than a
    // copy of it made every turn. A write to a field of it is not one of
    // these: this is the name itself on the left of an `=`. See D866.
    bool written;
    // Whether anything assigns to the name or to anything inside it: a field
    // of it, one of a run it holds, a field of one of those. Wider than
    // `written` on purpose, and for a different question: a walk's binding is
    // a copy made every turn, so writing a field of it changes nothing that
    // outlives the turn, while a name the frame does not hold at all has
    // nowhere for any of those writes to go. See D887.
    bool written_into;
    // The `let` that declared it, so what was learned about the name can be
    // put where the compiler reads it. Whether a name is written is known when
    // the scope holding it ends and not before. See D887.
    KestStmt *declared_by;
    // Declared `= array()`, which is an array with nothing in it and no room
    // for anything: `array(n, v)` is the one that comes with room. `fit`
    // writes where there is room and answers false where there is not, so a
    // `fit` into one of these writes nothing, answers false, and says nothing
    // at all unless somebody reads the answer. It is the shape a body under
    // `no.alloc` falls into, because `push` is refused there and `fit` is
    // what is left. See D1135.
    bool made_with_no_room;
    bool fitted;
    // And whether anything could have given it room since: `room` and `push`
    // by name, and being handed to any other function, which is where this
    // stops being able to see. Any one of them and nothing is said.
    bool given_room;
} Local;

typedef struct {
    KestProgram *program;
    Local *locals;
    uint32_t local_count;
    uint32_t local_capacity;
    uint32_t depth;
    // The current function's declared result, so `return` has something to be
    // measured against.
    KestType *result;
    uint32_t loop_depth;
    // Set while the operand of a unary minus is being checked, so `-128` is
    // read as one number rather than as the negation of one that does not fit.
    bool negating;
    bool out_of_memory;
    // Set while the thing being called is worked out, because a generic
    // function may be named there and nowhere else.
    bool naming_callee;
    // Set while the whole of an assignment's target is worked out, where that
    // target is a name and nothing else: writing to a local is not reading it,
    // and the same walk checks both. A field or an index is not one of these —
    // `p.x = 1` reads `p` to find the field. See D726.
    bool writing_to_a_name;
    // Whether what is being checked is an argument of a call to something
    // other than the builtins that read an array without giving it room. A
    // name handed over is a name this body can no longer say anything about.
    // See D1135.
    bool handing_a_name_over;
    // Where the copy being checked was asked for, when one is. A copy asked
    // for inside a copy is asked for by whoever asked for that one: the reader
    // wrote `table.set(t, Key(1), 5)` and the library wrote everything under
    // it, so the line to point at is theirs.
    KestSpan asking;
    const KestSource *asking_source;
    // Set while a block's body is checked, where `return` would leave a
    // function that is not the one the block is written in. See D1257.
    bool in_a_block;
    // Set while an argument that is a block is checked, where a block the
    // body was handed may be handed on. See D1257.
    bool handing_a_block;
    // The declaration whose body is being checked, which a function that
    // takes a block may not call: it is written into where it is called, and
    // written into itself it has no end. See D1257.
    const KestDecl *function;
    // A body that resumes: the enum whose cases it waits at, how many names
    // were in reach when the body began -- the parameters, and nothing else
    // may be in reach of a `wait` -- how many times each case is waited at,
    // and how many `scratch` blocks are open. See D1263.
    const KestType *resume_cases;
    uint32_t resume_mark;
    uint32_t *waited;
    uint32_t waits;
    uint32_t scratch_depth;
} Checker;

static KestType *check_expr(Checker *checker, KestExpr *expr,
                            const KestType *expected);
static KestStmt *tail_value(const KestBlock *block);
static KestSpan word_of(const KestExpr *expr);

static const char *type_name(Checker *checker, const KestType *type) {
    return kest_type_name(checker->program->arena, type);
}

static KestType *builtin(Checker *checker, const char *name) {
    return kest_find_type(checker->program, name, strlen(name));
}

static KestType *error_type(Checker *checker) {
    KestType *type = KEST_ARENA_NEW(checker->program->arena, KestType);
    if (type == NULL) {
        checker->out_of_memory = true;
        return NULL;
    }
    type->tag = KEST_T_ERROR;
    checker->program->types_made++;
    return type;
}

static const char *span_text(Checker *checker, KestSpan span) {
    return kest_span_text(checker->program->source, span);
}

// A number the compiler can work out where it stands, for the places where
// what it says is a mistake on the line it is written on. The machine catches
// the rest while it runs, which is right for a number that came from
// somewhere; it is late for one that is written down.
static bool written_number(Checker *checker, const KestExpr *expr,
                           int64_t *value) {
    KestValue held = {0};
    const char *unfoldable = NULL;
    if (expr == NULL || expr->type == NULL || expr->type->tag != KEST_T_INT ||
        kest_fold_const(checker->program, expr, &held, 1, &unfoldable,
                        NULL) != 1) {
        return false;
    }
    *value = held.integer;
    return true;
}

// A place written down and below nought. Where it is in text and where it is
// in an array are the same mistake with two ways of saying it, because what a
// reader has in front of them is one or the other.
static bool written_place(Checker *checker, const KestExpr *expr, bool in_text,
                          int64_t *at);

static void report(Checker *checker, KestSpan span, const char *code,
                   const char *format, ...) {
    va_list args;
    va_start(args, format);
    kest_diags_addv(checker->program->diags, KEST_SEVERITY_ERROR, code, span,
                    format, args);
    va_end(args);
}

static void suggest(Checker *checker, const char *format, ...) {
    va_list args;
    va_start(args, format);
    kest_diags_suggestv(checker->program->diags, format, args);
    va_end(args);
}

// An optional is the one type in the language whose refusal has an answer that
// is a piece of syntax rather than a different value: what it holds is right
// there and `if let` is how it comes out. Every place one is met where what it
// holds would have done says so, and only there — an `i32?` handed where a
// `text` was wanted is a different mistake and gets no such line. See D505.
static bool say_if_let(Checker *checker, const KestType *got,
                       const KestType *want) {
    if (got == NULL || got->tag != KEST_T_OPTIONAL || got->element == NULL) {
        return false;
    }
    if (want != NULL && !kest_type_equal(want, got->element)) {
        return false;
    }
    kest_diags_suggest(checker->program->diags,
                       "take what it holds out with `if let`");
    return true;
}

// The other way round: a value standing where an optional of an optional is
// wanted. The conversion happens once, so the two are one step apart and the
// message says only that the types differ -- which reads, to whoever wrote it,
// as though the language made no conversion at all. What is missing is the
// middle one, and it has to be named. See D885.
static bool say_wrapped_once(Checker *checker, const KestType *got,
                             const KestType *want) {
    if (got == NULL || want == NULL || want->tag != KEST_T_OPTIONAL ||
        want->element == NULL || want->element->tag != KEST_T_OPTIONAL ||
        want->element->element == NULL ||
        !kest_type_equal(got, want->element->element)) {
        return false;
    }
    suggest(checker,
            "a value becomes an optional once: name it as `%s` first, and "
            "that is what goes where the `%s` is wanted",
            type_name(checker, want->element), type_name(checker, want));
    return true;
}

// A number or a truth where text is wanted. What makes text of one here is a
// hole, and a reader arriving from a language whose `print` takes anything
// writes `io.print(3)` and is told only the two types. See D1148.
static bool say_made_text(Checker *checker, const KestType *got,
                          const KestType *want) {
    if (got == NULL || want == NULL || want->tag != KEST_T_TEXT ||
        (got->tag != KEST_T_INT && got->tag != KEST_T_FLOAT &&
         got->tag != KEST_T_BOOL)) {
        return false;
    }
    suggest(checker, "a hole makes text of it: `\"{...}\"`");
    return true;
}

// Reports a mismatch in the one shape every mismatch is reported in, so a
// reader learns to read it once.
static void expected_but(Checker *checker, KestSpan span, const KestType *want,
                         const KestType *got, const char *where) {
    report(checker, span, "K0310", "%s expects `%s`, found `%s`", where,
           type_name(checker, want), type_name(checker, got));
    if (!say_if_let(checker, got, want) &&
        !say_made_text(checker, got, want)) {
        say_wrapped_once(checker, got, want);
    }
}

// The same thing said with a name that is written down somewhere other than a
// declaration: what one of the language's own functions calls what it takes.
static void expected_called(Checker *checker, KestSpan span,
                            const KestType *want, const KestType *got,
                            const char *called) {
    report(checker, span, "K0310", "`%s` expects `%s`, found `%s`", called,
           type_name(checker, want), type_name(checker, got));
    if (!say_if_let(checker, got, want) &&
        !say_made_text(checker, got, want)) {
        say_wrapped_once(checker, got, want);
    }
}

// The same thing said with the name of what is being given to, which is worth
// more than `this argument` and is only knowable where a declaration was read.
// The name is written in the file that declared it and the span is in the file
// that wrote the call, which are not always the same file.
static void expected_for(Checker *checker, KestSpan span, const KestType *want,
                         const KestType *got, const KestSource *declared_in,
                         KestSpan name) {
    // A shape with no file behind it has no name to read, which is what a
    // built-in one is.
    if (declared_in == NULL) {
        expected_but(checker, span, want, got, "this one");
        return;
    }
    report(checker, span, "K0310", "`%.*s` expects `%s`, found `%s`",
           (int)name.length, kest_span_text(declared_in, name),
           type_name(checker, want), type_name(checker, got));
    if (!say_if_let(checker, got, want) &&
        !say_made_text(checker, got, want)) {
        say_wrapped_once(checker, got, want);
    }
}

static Local *find_local(Checker *checker, const char *name, size_t length) {
    // Backwards, so the innermost declaration of a name is the one found.
    for (uint32_t i = checker->local_count; i > 0; i--) {
        Local *local = &checker->locals[i - 1];
        if (kest_word_same(local->name, name, length)) {
            return local;
        }
    }
    return NULL;
}

// Dropping a scope, and what nothing in it read. A local is a name for a value
// in one body: no host can ask for one and no other file can name one, so a
// Whether a `let`'s value is `array()` with no count in it, which is the one
// shape that makes an array with no room. Written as a question of its own
// because what it is for is asked where the scope ends and not here. See
// D1135.
static bool made_with_no_room(Checker *checker, const KestExpr *value) {
    if (value == NULL || value->kind != KEST_EXPR_CALL ||
        value->call.arg_count != 0 ||
        value->call.callee->kind != KEST_EXPR_NAME) {
        return false;
    }
    const char *called = span_text(checker, value->call.callee->span);
    return value->call.callee->span.length == 5 &&
           strncmp(called, "array", 5) == 0;
}

// `let` nothing reads is the one name in this language nobody at all can be
// relying on. See D726.
static void drop_locals(Checker *checker, uint32_t mark) {
    for (uint32_t i = checker->local_count; i > mark; i--) {
        const Local *local = &checker->locals[i - 1];
        // What was learned about the name, put where the compiler reads it.
        // Said here because this is where a scope ends, which is the first
        // moment anybody knows. See D887.
        if (local->declared_by != NULL) {
            local->declared_by->let.name_written = local->written_into;
        }
        // An array made with no room, filled with `fit`, and never given
        // any. Said where the scope ends because that is the first moment
        // anybody knows nothing gave it room, and said at the `let` because
        // that is the line to change. A warning rather than a refusal, for
        // the reason K0346 is one: the shape that works is one word away.
        // See D1135.
        if (local->made_with_no_room && local->fitted && !local->given_room) {
            kest_diags_add(checker->program->diags, KEST_SEVERITY_WARNING,
                           "K0347", local->span,
                           "`%s` has no room, so every `fit` into it writes "
                           "nothing", local->name);
            kest_diags_suggest(checker->program->diags,
                               "make room for what is coming: `room(xs, n)` "
                               "after it, or `array(n, v)` instead of "
                               "`array()`");
        }
        // A `let`, an `if let`, and the position a `for` binds beside an
        // element: the three a program had another way to write. What a `for`
        // binds on its own and what a `match` case binds are not asked, because
        // there is no other way to write either. See D728 and D729.
        if (local->read || !(local->from_let || local->is_loop_index)) {
            continue;
        }
        kest_diags_add(checker->program->diags, KEST_SEVERITY_WARNING, "K0512",
                       local->span, "nothing in this body reads `%s`",
                       local->name);
        if (local->is_loop_index) {
            kest_diags_suggest(checker->program->diags,
                               "write the walk without it: a `for` over one "
                               "name walks the same and binds no position");
        } else if (local->from_if_let) {
            kest_diags_suggest(checker->program->diags,
                               "ask whether it holds anything instead: "
                               "`if what != none`");
        } else {
            kest_diags_suggest(checker->program->diags,
                               "take it out: a local is a name for a value in "
                               "one body, and one nothing reads is a value "
                               "nobody asked for");
        }
    }
    checker->local_count = mark;
}

static void declare_local(Checker *checker, KestSpan span, KestType *type) {
    const char *name = kest_arena_strndup(
        checker->program->arena, span_text(checker, span), span.length);
    if (name == NULL) {
        checker->out_of_memory = true;
        return;
    }

    Local *existing = find_local(checker, name, span.length);
    if (existing != NULL) {
        // Shadowing a visible local is refused: at any point in a body one
        // name means one thing. A sibling scope may reuse the name, because
        // the first is gone by then.
        report(checker, span, "K0318", "`%s` is already declared here", name);
        kest_diags_note(checker->program->diags, NULL, existing->span,
                        "the first one");
        return;
    }

    if (checker->local_count == checker->local_capacity) {
        uint32_t grown =
            checker->local_capacity == 0 ? 16 : checker->local_capacity * 2;
        Local *moved = KEST_ARENA_ARRAY(checker->program->arena, Local, grown);
        if (moved == NULL) {
            checker->out_of_memory = true;
            return;
        }
        if (checker->local_count > 0) {
            memcpy(moved, checker->locals,
                   sizeof(Local) * checker->local_count);
        }
        checker->locals = moved;
        checker->local_capacity = grown;
    }

    // Written whole rather than field by field. The array outlives the body it
    // was filled for — a scope is dropped by rewinding a count, not by
    // clearing what is past it — so a field this forgets is one the last name
    // at that place left behind, and the last name at that place was read.
    // See D726.
    Local *local = &checker->locals[checker->local_count++];
    Local fresh = {name, type, span, checker->depth, false, false, false,
                   false, false, false, false, false, NULL,
                   false, false, false};
    *local = fresh;
}

// Whether writing through this path can be seen after the statement. An array
// anywhere along it is a handle, and writing through a handle is visible
// however the path reached it.
static bool writes_only_a_copy(Checker *checker, const KestExpr *target,
                               const KestExpr **root, bool *is_index) {
    const KestExpr *step = target;
    while (step->kind == KEST_EXPR_FIELD || step->kind == KEST_EXPR_INDEX) {
        if (step->kind == KEST_EXPR_INDEX) {
            return false;
        }
        step = step->field.object;
    }
    if (step->kind != KEST_EXPR_NAME) {
        return false;
    }
    Local *local =
        find_local(checker, span_text(checker, step->span), step->span.length);
    *root = step;
    if (local == NULL || !local->is_loop_element) {
        return false;
    }
    *is_index = local->is_loop_index;
    return true;
}

// Whether this writes a field of a value the caller handed over, in a function
// that has no way to hand it back. A struct is a value (D006), so the write is
// on this frame's copy: a function using its parameter as a place to work
// gives it back, and one that does not is writing where nobody will look.
//
// What says it can hand it back is the type it answers with. Any answer at all
// used to say it, which turned this off for every function that answers
// anything: a body that kept a world in a struct and wrote `world.tick += 1`
// while answering how many things moved lost the write and was told nothing.
// The shape the suggestion names is `fn f(one: T) -> T`, and an optional of
// `T` is the same shape with a way to say no, so those two are what is
// allowed. See D1065.
static bool answers_with(const KestType *result, const KestType *given) {
    if (result == NULL || given == NULL) {
        return false;
    }
    if (kest_type_equal(result, given)) {
        return true;
    }
    return result->tag == KEST_T_OPTIONAL &&
           kest_type_equal(result->element, given);
}

static bool writes_a_handed_copy(Checker *checker, const KestExpr *target,
                                 const KestExpr **root) {
    const KestExpr *step = target;
    bool through_a_field = false;
    while (step->kind == KEST_EXPR_FIELD || step->kind == KEST_EXPR_INDEX) {
        if (step->kind == KEST_EXPR_INDEX) {
            // An array or a run reached through the path is a handle or is
            // this frame's own, and either way this is not about it.
            return false;
        }
        through_a_field = true;
        step = step->field.object;
    }
    if (!through_a_field || step->kind != KEST_EXPR_NAME) {
        return false;
    }
    Local *local =
        find_local(checker, span_text(checker, step->span), step->span.length);
    *root = step;
    if (local == NULL || !local->is_parameter || local->type == NULL ||
        local->type->tag != KEST_T_STRUCT) {
        return false;
    }
    return !answers_with(checker->result, local->type);
}

// An optional is a place a value can go, not a hint about the value itself,
// so an operand is measured against what the optional holds.
static const KestType *inside(const KestType *expected) {
    if (expected != NULL && expected->tag == KEST_T_OPTIONAL) {
        return expected->element;
    }
    return expected;
}

// What `for` walks. The same list the chain below branches on, and the reason
// that chain ends without an `else`: everything not on this list is refused
// before it is reached.
static bool walks(const KestType *type) {
    return type->tag == KEST_T_ARRAY || type->tag == KEST_T_FIXED ||
           type->tag == KEST_T_STORE || type->tag == KEST_T_FLAGS ||
           type->tag == KEST_T_TEXT;
}

// What `len` counts. One list, read twice: once to refuse what is not on it
// and once to ask whether what an optional holds would have been.
static bool has_length(const KestType *type) {
    return type->tag == KEST_T_ARRAY || type->tag == KEST_T_FIXED ||
           type->tag == KEST_T_STORE || type->tag == KEST_T_TEXT;
}

static bool is_numeric(const KestType *type) {
    return type != NULL &&
           (type->tag == KEST_T_INT || type->tag == KEST_T_FLOAT);
}

static bool is_error(const KestType *type) {
    return type == NULL || type->tag == KEST_T_ERROR;
}

// What `+`, `-`, `*` or `/` between these two gives when one of them is a
// vector, and NULL when it gives nothing: a vector and one of its own width,
// a component at a time, or a vector and an `f32` on either side of `*` and
// `/`, which is every component and that one number. What a shading language
// has, and nothing a program can add to. See D1250.
static KestType *vector_answer(KestTokenKind op, KestType *left,
                               KestType *right) {
    bool scales = op == KEST_TOK_STAR || op == KEST_TOK_SLASH ||
                  op == KEST_TOK_STAREQ || op == KEST_TOK_SLASHEQ;
    if (kest_is_vector(left) && kest_type_equal(left, right)) {
        return left;
    }
    if (scales && kest_is_vector(left) && kest_is_narrow(right)) {
        return left;
    }
    if (scales && kest_is_narrow(left) && kest_is_vector(right)) {
        return right;
    }
    return NULL;
}

// Whether a literal can take a type from its context rather than its default.
// `let x: f64 = 1.5` and `let n: u8 = 200` both work because of this, and
// nothing else in the language converts silently.
// What a literal is, before anything has told it otherwise. A number with a
// minus in front of it is one: the minus is how it is written rather than
// something done to it afterwards.
static const KestExpr *literal_of(const KestExpr *expr) {
    if (expr->kind == KEST_EXPR_INT || expr->kind == KEST_EXPR_FLOAT) {
        return expr;
    }
    if (expr->kind == KEST_EXPR_UNARY && expr->unary.op == KEST_TOK_MINUS) {
        return literal_of(expr->unary.operand);
    }
    return NULL;
}

// And whether there is one. This was written out twice more -- once under this
// name and once as `takes_a_type`, which is what a literal does rather than
// what it is -- and three walks over one question is three answers the day any
// of them moves. See D770.
static bool is_literal(const KestExpr *expr) {
    return literal_of(expr) != NULL;
}

// A float written down beside a vector is one of its components, which are
// `f32`, the way `2.0 * dt` is `dt`'s. The node is corrected as well as the
// type, because the compiler reads which width to make it from there.
static void as_component(Checker *checker, KestExpr *side, KestType **type) {
    if (!is_literal(side) || *type == NULL || (*type)->tag != KEST_T_FLOAT ||
        (*type)->width == 32) {
        return;
    }
    KestType *number = kest_find_type(checker->program, "f32", 3);
    *type = number;
    side->type = number;
    if (side->kind == KEST_EXPR_UNARY) {
        side->unary.operand->type = number;
    }
}

// A name that is several functions is one of them here, and which one is
// settled by what is wanted. A call settles it by what is passed instead; see
// D023, which this is the other half of.
static KestType *named_function(Checker *checker, const char *name,
                                size_t length, const KestType *expected) {
    KestSymbol *all[32];
    uint32_t count = kest_overloads(checker->program, name, length, all, 32);
    if (count <= 1 || expected == NULL || expected->tag != KEST_T_FN) {
        return NULL;
    }
    for (uint32_t i = 0; i < count; i++) {
        // One that takes types is not one of them: its shape holds names that
        // compare equal to anything, so it would answer for every wanted
        // shape. Which copy of it is meant is settled after this, by what is
        // wanted (`copy_for_shape`).
        if (all[i]->type->type_param_count > 0) {
            continue;
        }
        if (kest_type_equal(all[i]->type, expected)) {
            all[i]->named = true;
            return all[i]->type;
        }
    }
    return NULL;
}

// The names the language answers to on its own, which is the same list
// `is_builtin` is asked about and the one the compiler emits for.
// `check-tables.sh` holds the three of them together. Two messages read this
// one: what somebody wrote a field for, and what they nearly spelt.
// What the language's own functions call the things they take, in the words
// the reference prints: `slice(t, from, count)`. A message says `from` rather
// than `this argument` because a reader has met that name, and it has to be
// that name — `check-tables.sh` holds this against the reference, the way it
// holds the keywords.
//
// Only the ones a message names are here. The reference calls some of them by
// a letter, and `this position` says more than `i` would.
static const struct {
    const char *name;
    const char *takes[3];
} BUILTIN_TAKES[] = {
    {"find", {"t", "needle", "from"}},
    {"matches", {"t", "at", "needle"}},
    {"rest", {"t", "at", NULL}},
    {"slice", {"t", "from", "count"}},
};

// What that function calls the thing in its nth place.
static const char *takes_called(const char *name, uint32_t nth) {
    for (uint32_t i = 0; i < sizeof(BUILTIN_TAKES) / sizeof(BUILTIN_TAKES[0]);
         i++) {
        if (strcmp(BUILTIN_TAKES[i].name, name) == 0) {
            return nth < 3 ? BUILTIN_TAKES[i].takes[nth] : NULL;
        }
    }
    return NULL;
}

static const char *const BUILTINS[] = {
    "add", "array", "bits", "clear", "find",  "float", "get",   "hash",
    "len", "matches", "fit", "pop",   "push",   "remove", "rest", "room",
    "set", "slice", "store",
};

// The nearest thing a reader could have meant by a name that is not there: a
// name this body declared, a name the language answers to, or a name declared
// where this file can reach it. A name from another module is suggested the
// way it would have to be written, under its module, because that is what
// leaving the module off looks like.
//
// A wrong suggestion costs more than none, so the limit is the one every other
// suggestion uses: a third of what was written, and nothing under three
// characters is suggested for at all, because every short name is one edit
// from every other.
// What this knows about one candidate: nearer than everything so far, or as
// near as the nearest. The same word offered twice is one answer — a name
// reachable under its module and by its last piece is written two ways and
// meant once — so what is compared is the words and not the pointers.
typedef struct {
    const char *best;
    const char *second;
    uint32_t nearest;
    uint32_t level;
} Nearest;

static void offer(Nearest *found, const char *candidate, uint32_t distance) {
    if (distance < found->nearest) {
        found->nearest = distance;
        found->best = candidate;
        found->second = NULL;
        found->level = 1;
        return;
    }
    if (distance != found->nearest || found->best == NULL ||
        strcmp(candidate, found->best) == 0 ||
        (found->second != NULL && strcmp(candidate, found->second) == 0)) {
        return;
    }
    found->level++;
    if (found->second == NULL) {
        found->second = candidate;
    }
}

// The nearest name, and whether something else is exactly as near. A reader
// told one of two equally good answers is being chosen for; a reader told both
// is being told what this knows. `also` is the second when there are two and
// NULL when there is one, and nothing is answered at all when more than two
// are level, because a list of names is not a suggestion. See D297.
static const char *nearest_name(Checker *checker, const char *name,
                                size_t length, const char **also) {
    if (also != NULL) {
        *also = NULL;
    }
    // A name of one letter is not written back for: everything that size is one
    // edit from everything else, and what a reader would get is noise. Two is
    // the shortest worth answering about — `io` and `os` are names a file
    // writes — and what keeps those from being answered wrongly is the rule
    // above, which says nothing when more than two are level. See D298.
    if (length < 2) {
        return NULL;
    }
    uint32_t limit = length <= 5 ? 1 : (uint32_t)length / 3;
    Nearest found = {NULL, NULL, limit + 1, 0};

    for (uint32_t i = 0; i < checker->local_count; i++) {
        const char *candidate = checker->locals[i].name;
        offer(&found, candidate,
              kest_word_distance(name, length, candidate, strlen(candidate),
                                 limit));
    }
    for (uint32_t i = 0; i < sizeof(BUILTINS) / sizeof(BUILTINS[0]); i++) {
        offer(&found, BUILTINS[i],
              kest_word_distance(name, length, BUILTINS[i],
                                 strlen(BUILTINS[i]), limit));
    }

    bool written_plain = memchr(name, '.', length) == NULL;
    for (uint32_t i = 0; i < checker->program->global_count; i++) {
        const char *whole = checker->program->globals[i].name;
        if (kest_needs_import(checker->program, whole, strlen(whole))) {
            continue;
        }
        // A global is held under its module. What was written is compared
        // with the part that was written the same way: the last piece when
        // the name has no dot in it, and the whole of it when it has.
        const char *dot = strrchr(whole, '.');
        const char *tail = dot == NULL ? whole : dot + 1;
        // Compared against the way a file writes it, because that is what was
        // written: `io.print` and not `std.io.print`. See D1039.
        const char *as_written = kest_written_as(checker->program, whole);
        const char *against = written_plain ? tail : as_written;
        // Reachable by the last piece alone means this file declared it, and
        // that is how it is written back.
        const char *written =
            kest_lookup_global(checker->program, tail, strlen(tail)) != NULL
                ? tail
                : as_written;
        offer(&found, written,
              kest_word_distance(name, length, against, strlen(against),
                                 limit));
    }

    // And the modules themselves, which are names a file writes as often as it
    // writes anything under them: `ioo.print` is one letter wrong and the
    // wrong letter is in the part that says where to look. A module is not
    // declared anywhere to be found in a list, so what says one is there is a
    // name registered under it.
    if (written_plain) {
        for (uint32_t i = 0; i < checker->program->global_count; i++) {
            const char *whole = checker->program->globals[i].name;
            if (kest_needs_import(checker->program, whole, strlen(whole))) {
                continue;
            }
            const char *dot = strrchr(whole, '.');
            if (dot == NULL) {
                continue;
            }
            // The word a file writes, which is the last part of the module
            // and not the whole of it: a name lives under `std.io` and what
            // goes in front of `print` is `io`. See D1039.
            const char *from = whole;
            for (const char *at = whole; at < dot; at++) {
                if (*at == '.') {
                    from = at + 1;
                }
            }
            const char *module = kest_arena_strndup(checker->program->arena,
                                                    from,
                                                    (size_t)(dot - from));
            // A name written into the arena, so when there is no room there
            // is no name. A suggestion is the one thing a compiler with
            // nothing left can do without, so this one is left out rather
            // than measured. See D508, and D510 for what else was.
            if (module == NULL) {
                break;
            }
            offer(&found, module,
                  kest_word_distance(name, length, module, strlen(module),
                                     limit));
        }
    }

    // A name offered back as itself says nothing: what was written is what is
    // unknown. It is the module loop above that reaches this, where a file
    // writes the name it calls itself somewhere a value goes -- and what is
    // wrong there is not the spelling. Nothing else can be level with it,
    // since a word is no distance from itself. See D737.
    if (found.best != NULL && kest_word_same(found.best, name, length)) {
        return NULL;
    }
    if (found.level > 2) {
        return NULL;
    }
    if (also != NULL) {
        *also = found.second;
    }
    return found.best;
}

static bool has_equality(const KestProgram *program, const KestType *type,
                         const KestType **without);

// Whether a type has an order, which is the second of the two things a generic
// may ask of what it is given. Numbers have one and text has one, by its
// bytes; a struct has as many as it has fields and so has none of its own,
// which is what `sort.by` takes a function for.
static bool has_order(const KestProgram *program, const KestType *type) {
    if (type == NULL) {
        return false;
    }
    if (type->tag == KEST_T_PARAM) {
        return kest_wants((KestProgram *)program, type, KEST_WANTS_ORDERS);
    }
    return is_numeric(type) || type->tag == KEST_T_TEXT;
}

// What a copy was asked for with, held to what the generic says it needs. The
// body was checked against those words where it was written, so this is the
// other half of the same contract and the place a reader meets it: the line
// that asked, rather than a line inside somebody else's library. See D1043.
static bool wants_met(Checker *checker, const KestType *callee,
                      KestSpan named, KestType **bindings,
                      uint32_t generics, KestSpan where) {
    if (callee->type_param_wants == NULL) {
        return true;
    }
    bool met = true;
    for (uint32_t g = 0; g < generics; g++) {
        uint8_t wants = callee->type_param_wants[g];
        for (uint32_t at = 0; at < KEST_CAPABILITY_COUNT; at++) {
            if ((wants & kest_capability(at)->bit) == 0) {
                continue;
            }
            const KestType *without = NULL;
            bool has = kest_capability(at)->bit == KEST_WANTS_COMPARES
                           ? has_equality(checker->program, bindings[g],
                                          &without)
                           : has_order(checker->program, bindings[g]);
            if (has || is_error(bindings[g])) {
                continue;
            }
            met = false;
            report(checker, where, "K0366",
                   "`%.*s` wants a `%s` that %s, and `%s` does not",
                   (int)named.length, span_text(checker, named),
                   callee->type_param_names[g], kest_capability(at)->word,
                   type_name(checker, bindings[g]));
            if (without != NULL && without != bindings[g]) {
                suggest(checker, "`%s` carries a `%s`, which does not compare",
                        type_name(checker, bindings[g]),
                        type_name(checker, without));
            } else if (kest_capability(at)->bit == KEST_WANTS_ORDERS) {
                suggest(checker,
                        "there are as many orders as fields, so write the one "
                        "you mean: `fn(%s, %s) -> bool`, handed to what sorts",
                        type_name(checker, bindings[g]),
                        type_name(checker, bindings[g]));
            }
            if (callee->decl != NULL && callee->unit != NULL) {
                kest_diags_note(checker->program->diags,
                                &callee->unit->source,
                                callee->decl->type_params[g].name,
                                "`%s` is written here",
                                kest_capability(at)->word);
            }
        }
    }
    return met;
}

// A copy asked for with a type name in it, which is not a copy. Nothing can be
// compiled for a name that stands for itself, and the body such a copy would
// be made from is the one being checked where it is written. What the caller
// needs is the callee's own signature with the names put through, so that is
// what it gets -- and no instance is made, because an instance is a thing the
// compiler is going to emit. Without this, checking a generic's body left one
// uncompilable copy per call in the list, and the promise proof judged each of
// them twice. See D1043.
static KestType *shape_of_call(Checker *checker, const KestType *callee,
                               const char **names, KestType **bindings,
                               uint32_t generics) {
    bool abstract = false;
    for (uint32_t g = 0; g < generics && !abstract; g++) {
        abstract = kest_mentions_name(bindings[g]);
    }
    if (!abstract) {
        return NULL;
    }
    KestType *shape = kest_substitute(checker->program, (KestType *)callee,
                                      names, bindings, generics);
    if (shape == NULL) {
        checker->out_of_memory = true;
        return error_type(checker);
    }
    shape->type_param_count = 0;
    return shape;
}

// A type name bound to a type name. Which one is meant is the one bound where
// the call is written, and what came out of an argument may be another: a copy
// of a generic shape is found by its name, so `Box<K, V>` is one copy in a
// module and the `K` inside it belongs to whichever generic asked for it
// first. That one said what its own `K` can do, and this one says what this
// one's can. See D1043.
static void settle_names(KestProgram *program, KestType **bindings,
                         uint32_t count) {
    for (uint32_t g = 0; g < count; g++) {
        if (bindings[g] == NULL || bindings[g]->tag != KEST_T_PARAM ||
            bindings[g]->name == NULL) {
            continue;
        }
        KestType *here = kest_bound_type(program, bindings[g]->name,
                                         strlen(bindings[g]->name));
        if (here != NULL && here->tag == KEST_T_PARAM) {
            bindings[g] = here;
        }
    }
}

static KestType *copy_for_shape(Checker *checker, const KestType *callee,
                                const KestType *expected, KestSpan where);
static bool literal_fits(Checker *checker, const KestExpr *expr,
                         const KestType *type);

static KestType *check_name(Checker *checker, KestExpr *expr,
                            const KestType *expected) {
    const char *name = span_text(checker, expr->span);
    size_t length = expr->span.length;

    Local *local = find_local(checker, name, length);
    if (local != NULL) {
        // Read, unless this is the whole of what is being written to. The same
        // walk checks a target and a value, and a name written to is not a
        // name read. See D726.
        if (!checker->writing_to_a_name) {
            local->read = true;
        }
        if (checker->handing_a_name_over) {
            local->given_room = true;
        }
        kest_program_used(checker->program, checker->program->source,
                          expr->span, checker->program->source, local->span,
                          local->type, true);
        // A block is called, or handed to what takes one, and is nothing
        // else: held, it would outlive the frame it runs in. See D1257.
        if (local->type != NULL && local->type->tag == KEST_T_FN &&
            local->type->block && !checker->naming_callee &&
            !checker->handing_a_block) {
            report(checker, expr->span, "K0367",
                   "`%.*s` is a block, which is called or handed on and "
                   "nothing else",
                   (int)length, name);
            return error_type(checker);
        }
        return local->type;
    }

    KestType *chosen = named_function(checker, name, length, expected);
    if (chosen != NULL && kest_takes_a_block(chosen) && !checker->naming_callee) {
        report(checker, expr->span, "K0367",
               "`%.*s` takes a block, so it is written into where it is called "
               "and is not a value",
               (int)length, name);
        return error_type(checker);
    }
    if (chosen != NULL) {
        return chosen;
    }
    KestSymbol *global = kest_lookup_global(checker->program, name, length);
    if (global != NULL) {
        global->named = true;
        kest_program_used(checker->program, checker->program->source,
                          expr->span, global->source, global->span,
                          global->type, false);
        if (!checker->naming_callee && kest_takes_a_block(global->type)) {
            report(checker, expr->span, "K0367",
                   "`%.*s` takes a block, so it is written into where it is "
                   "called and is not a value",
                   (int)length, name);
            return error_type(checker);
        }
        // A generic function is not one function, so there is nothing to
        // hand around: which copy would it be?
        if (!checker->naming_callee && global->type->tag == KEST_T_FN &&
            global->type->type_param_count > 0) {
            KestType *copy =
                copy_for_shape(checker, global->type, expected,
                               expr->span);
            if (copy != NULL) {
                return copy;
            }
            report(checker, expr->span, "K0362",
                   "`%.*s` takes a type, so it is called and not named",
                   (int)length, name);
            kest_diags_suggest(checker->program->diags,
                               "a copy exists per set of types it is called "
                               "with, and a value would be one of them");
            return error_type(checker);
        }
        return global->type;
    }

    // A type where a value is wanted: a shape named rather than built. Saying
    // the types at the call is the other half of the same mistake, and the
    // parser says that one where it is written.
    //
    // Except where the word in front is a module this file did not import:
    // `text.chars` with no `import std.text` is a missing import and not the
    // builtin `text` named where a value goes, and saying the second sends a
    // reader to fix a line that is right. See D1039.
    KestType *named = kest_lookup_type(checker->program, name, length);
    if (named != NULL && !is_error(named) &&
        kest_out_of_reach(checker->program, name, length)) {
        named = NULL;
    }
    if (named != NULL && !is_error(named)) {
        report(checker, expr->span, "K0344",
               "`%.*s` is a type, and this wants a value", (int)length, name);
        if (named->type_param_count > 0) {
            // Where a generic's types come from is what K0211 refuses a
            // reader for getting wrong, and it is what is passed: a copy is
            // made from the values a builder is handed, and from where the
            // value is going only when those cannot tell. This used to say
            // the second of those and to ask for type names at a call, which
            // is the thing K0211 exists to refuse. See D758.
            suggest(checker,
                    "build one: `%.*s(...)`, or name a value of it: a copy is "
                    "made from what is passed, and from where it is going when "
                    "that cannot tell",
                    (int)length, name);
        } else {
            suggest(checker, "build one: `%.*s(...)`, or name a value of it",
                    (int)length, name);
        }
        return error_type(checker);
    }

    // And a type name a generic brought into scope, which is not in the type
    // table at all: it stands for whatever this copy was given. Saying
    // `unknown` about it sends a reader looking for a declaration of `T`,
    // which is the one thing there will never be. See D741.
    KestType *bound = kest_bound_type(checker->program, name, length);
    if (bound != NULL) {
        report(checker, expr->span, "K0361",
               "`%.*s` is a type name, and this wants a value", (int)length,
               name);
        suggest(checker,
                "a generic's type name stands for a type and not for a value: "
                "name a value of it instead");
        return error_type(checker);
    }

    // A module named where a value goes: `io` is the half of `io.print` that
    // says where to look, written without the half that says what. It is not
    // an unknown name -- the program has it -- and it is not a spelling to
    // guess at either. The neighbouring mistake, a type named where a value
    // goes, is refused just above. See D738.
    if (kest_module_named(checker->program, name, length)) {
        report(checker, expr->span, "K0358",
               "`%.*s` is a module, and this wants a value", (int)length,
               name);
        // Naming it is writing to it, so the import it came through is not an
        // import nothing writes. See D735.
        kest_import_reached_by(checker->program, name, length);
        const KestSymbol *under = kest_first_under(checker->program, name, length);
        if (under != NULL) {
            suggest(checker,
                    "a module is a place to look and not a value: `%s` is one "
                    "of the names under it",
                    under->name);
        }
        return error_type(checker);
    }

    report(checker, expr->span, "K0306", "unknown name `%.*s`", (int)length,
           name);
    // Saying something is the host's to do, and it is the first thing anybody
    // reaches for, so the one place it lives is worth naming outright.
    if (kest_word_same("print", name, length) ||
        kest_word_same("write", name, length)) {
        suggest(checker, "`import std.io` and call `io.%.*s`", (int)length,
                name);
        return error_type(checker);
    }
    // And a name this program has, under a module this file has not asked for.
    // The walk below offers nothing a file cannot write, which is right for a
    // spelling — but a name that is exactly right and one import away is not a
    // spelling mistake, and saying nothing about it sends a reader looking for
    // something they have already written. See D732.
    for (uint32_t i = 0; i < checker->program->global_count; i++) {
        const KestSymbol *symbol = &checker->program->globals[i];
        const char *whole = symbol->name;
        // Where the module ends is wherever this name begins, which is the
        // only split that answers the question being asked: a name lives under
        // the whole of its module and what comes after may hold dots of its
        // own — `std.math.Math.floor` is a crossing the file declares, and
        // what a reader would write for that is `math.Math.floor` rather than
        // `floor`. So the tail is matched and the head is whatever is left.
        // See D732 and D1039.
        size_t reach = strlen(whole);
        const char *dot = reach > length + 1 &&
                                  whole[reach - length - 1] == '.' &&
                                  kest_word_same(whole + reach - length, name,
                                                 length)
                              ? whole + reach - length - 1
                              : NULL;
        if (dot == NULL ||
            !kest_needs_import(checker->program, whole, strlen(whole))) {
            continue;
        }
        suggest(checker,
                "`%s` is in this program, and this file does not import `%.*s`",
                whole, (int)(dot - whole), whole);
        kest_diags_note(checker->program->diags, symbol->source, symbol->span,
                        "declared here");
        return error_type(checker);
    }

    // And a module the library has, which no walk over this program can find:
    // a module nothing imported is in no program. A reader who wrote
    // `io.print` wrote the call exactly, and what was missing was the line
    // above it — so the library is asked for a file of the name they wrote.
    // See D734.
    if (checker->program->files != NULL &&
        !kest_file_reaches(checker->program, name, length) &&
        kest_library_has(checker->program->files->library, name, length)) {
        suggest(checker,
                "`std.%.*s` is in the library, and this file does not import "
                "it",
                (int)length, name);
        return error_type(checker);
    }

    const char *also = NULL;
    const char *nearest = nearest_name(checker, name, length, &also);
    if (nearest != NULL && also != NULL) {
        suggest(checker, "did you mean `%s` or `%s`?", nearest, also);
    } else if (nearest != NULL) {
        suggest(checker, "did you mean `%s`?", nearest);
    }
    return error_type(checker);
}

// `Vec3(1.0, 2.0, 3.0)` builds a struct. Call syntax rather than a braced
// literal, because `if p.y < 0 {` only parses without a rule about where a
// brace may start an expression, and there is no rule to write if no
// expression ever begins with one.
static void report_unimported(Checker *checker, KestSpan name) {
    const char *text = span_text(checker, name);
    if (!kest_needs_import(checker->program, text, name.length) &&
        !kest_out_of_reach(checker->program, text, name.length)) {
        // Reached, and this is where every name from another module is asked
        // about: what says an import is worth its place is a name written
        // through it. See D725.
        kest_import_reached(checker->program, text, name.length);
        return;
    }
    const char *dot = memchr(text, '.', name.length);
    report(checker, name, "K0325", "this file does not import `%.*s`",
           (int)(dot - text), text);
    suggest(checker, "a name is only reachable from a module this file asked "
                     "for");

    // Where it came from, which is the thing the reader has to go and look at.
    KestType *type = kest_lookup_type(checker->program, text, name.length);
    if (type != NULL && type->declared_in != NULL) {
        kest_diags_note(checker->program->diags, type->declared_in, type->span,
                        "declared here");
        return;
    }
    KestSymbol *symbol = kest_lookup_global(checker->program, text, name.length);
    if (symbol != NULL && symbol->source != NULL) {
        kest_diags_note(checker->program->diags, symbol->source, symbol->span,
                        "declared here");
    }
}

// One case of an enum, found by name, or nothing with a diagnostic that lists
// what the enum does have.
// The nearest case of an enum or bit of a set, held to the same limit as every
// other suggestion: a third of what was written, and nothing under three
// characters, because every short name is one edit from every other.
static const char *nearest_case(const KestType *choice, const char *name,
                                size_t length) {
    if (length < 3) {
        return NULL;
    }
    uint32_t limit = length == 3 ? 1 : (uint32_t)length / 3;
    const char *best = NULL;
    uint32_t nearest_so_far = limit + 1;
    for (uint32_t i = 0; i < choice->case_count; i++) {
        const char *candidate = choice->cases[i].name;
        uint32_t distance = kest_word_distance(name, length, candidate,
                                               strlen(candidate), limit);
        if (distance < nearest_so_far) {
            nearest_so_far = distance;
            best = candidate;
        }
    }
    return best;
}

static const KestVariantType *find_case(Checker *checker, const KestType *choice,
                                        KestSpan name) {
    const char *text = span_text(checker, name);
    for (uint32_t i = 0; i < choice->case_count; i++) {
        if (kest_word_same(choice->cases[i].name, text, name.length)) {
            // The cases belong to the type and this is the one door to them,
            // whether the name is being built, tested or answered.
            choice->cases[i].named = true;
            return &choice->cases[i];
        }
    }
    // A set names bits and an enum names cases, and a reader is told which
    // of the two they are looking at by the word the declaration uses.
    report(checker, name, "K0330", "`%s` has no %s `%.*s`", choice->name,
           choice->tag == KEST_T_FLAGS ? "bit" : "case", (int)name.length,
           text);

    // One that is nearly it is what was meant, and the whole list beside it is
    // noise. Everything it has is worth showing when nothing is nearly it.
    const char *nearest = nearest_case(choice, text, name.length);
    if (nearest != NULL) {
        suggest(checker, "did you mean `%s`?", nearest);
        return NULL;
    }

    // Every one of them, and what there is no room to show is counted by the
    // diagnostic itself (D208) rather than by a rule written here: a list that
    // stops without saying so is a list a reader believes, and where the
    // stopping happens is where it is known.
    for (uint32_t i = 0; i < choice->case_count; i++) {
        kest_diags_note(checker->program->diags, choice->declared_in,
                        choice->cases[i].span, "this one it has");
    }
    return NULL;
}

static KestType *check_case(Checker *checker, KestExpr *expr, KestType *choice,
                            KestSpan name) {
    expr->call.callee->type = choice;
    const KestVariantType *variant = find_case(checker, choice, name);
    if (variant == NULL) {
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            check_expr(checker, expr->call.args[i], NULL);
        }
        return error_type(checker);
    }

    if (expr->call.arg_count != variant->payload_count) {
        report(checker, expr->span, "K0309",
               "`%s` carries %u thing%s, found %u", variant->name,
               variant->payload_count, variant->payload_count == 1 ? "" : "s",
               expr->call.arg_count);
    }
    uint32_t checked = expr->call.arg_count < variant->payload_count
                           ? expr->call.arg_count
                           : variant->payload_count;
    for (uint32_t i = 0; i < checked; i++) {
        KestType *given =
            check_expr(checker, expr->call.args[i], variant->payload[i]);
        if (!kest_type_equal(given, variant->payload[i])) {
            expected_but(checker, expr->call.args[i]->span, variant->payload[i],
                         given, "this one");
        }
    }
    for (uint32_t i = checked; i < expr->call.arg_count; i++) {
        check_expr(checker, expr->call.args[i], NULL);
    }
    return choice;
}

// Where the name of the nth thing a call has to be given was written. A
// parameter has one in the declaration it was parsed from, a field has one on
// the type, and neither is in what the call itself knows.
typedef KestSpan (*NameAt)(const void *of, uint32_t nth);

static KestSpan param_span(const void *of, uint32_t nth) {
    const KestDecl *decl = of;
    return decl->function.params[nth]->name;
}

static KestSpan member_span(const void *of, uint32_t nth) {
    const KestType *type = of;
    return type->members[nth].span;
}

// Which of them was not written, pointed at where it was declared. The count
// says how many are wanted and the name says which one is missing, and the
// second is what a reader has to work out for themselves otherwise.
static void note_written(Checker *checker, const KestExpr *expr, uint32_t want,
                         const KestSource *declared_in, KestSpan declared,
                         NameAt name_at, const void *of) {
    KestDiags *diags = checker->program->diags;
    if (expr->call.arg_count > want) {
        // The first one there is nothing to take, in the file that wrote it
        // rather than the file that declared what it was written for.
        kest_diags_note(diags, checker->program->source,
                        expr->call.args[want]->span,
                        "there is nothing to take this one");
        kest_diags_note(diags, declared_in, declared, "declared here");
        return;
    }

    for (uint32_t i = expr->call.arg_count; i < want; i++) {
        uint32_t left = want - i - 1;
        // The last note there is room for counts the rest, the way every
        // other list in these messages does (D200).
        if (i - expr->call.arg_count + 1 == KEST_MOST_PLACES && left > 0) {
            kest_diags_note(diags, declared_in, name_at(of, i),
                            "this one was not written, and %u more", left);
            return;
        }
        kest_diags_note(diags, declared_in, name_at(of, i),
                        "this one was not written");
    }
}

// Whether a field its module keeps to itself is this file's to name. A module
// is the whole of what a file calls itself, so the question is whether the
// shape was declared in the module being checked. A file that names no module
// declares under nothing, and nothing else in a program is under nothing with
// it. See D1041.
static bool reaches_own(const Checker *checker, const KestType *type) {
    return type->unit != NULL &&
           strcmp(checker->program->module, type->unit->module) == 0;
}

// A field the module that declared it keeps to itself. Reading is what is
// refused rather than writing, because reading is the whole of it: `pop(t.keys)`
// reads the field and changes what it names through the handle it was given,
// so a field that can be read is a field that can be written. See D1041.
static void refuse_own(Checker *checker, KestSpan where, const KestType *type,
                       const KestMember *member) {
    report(checker, where, "K0365", "`%s` is `%s`'s own", member->name,
           type->unit->module);
    kest_diags_note(checker->program->diags, type->declared_in, member->span,
                    "written `own` here");
    suggest(checker, "what reaches it is what `%s` declares",
            type->unit->module);
}

static KestType *check_construction(Checker *checker, KestExpr *expr,
                                    KestType *type) {
    expr->call.callee->type = type;

    // A shape holding one of those is built where it is declared: every field
    // is named by position, so building one anywhere else is naming them all.
    for (uint32_t i = 0; i < type->member_count; i++) {
        if (type->members[i].own && !reaches_own(checker, type)) {
            report(checker, expr->call.callee->span, "K0365",
                   "`%s` holds `%s`, which is `%s`'s own", type->name,
                   type->members[i].name, type->unit->module);
            kest_diags_note(checker->program->diags, type->declared_in,
                            type->members[i].span, "written `own` here");
            suggest(checker, "what makes one is what `%s` declares",
                    type->unit->module);
            return error_type(checker);
        }
    }

    if (expr->call.arg_count != type->member_count) {
        report(checker, expr->span, "K0309",
               "`%s` has %u field%s, found %u", type->name, type->member_count,
               type->member_count == 1 ? "" : "s", expr->call.arg_count);
        // A vector was declared by nobody, so there is nowhere to point at:
        // what it is made of is said instead. See D1250.
        if (kest_is_vector(type)) {
            suggest(checker, "`%s` is made of `%s`", type->name,
                    type->member_count == 2   ? "x` and `y"
                    : type->member_count == 3 ? "x`, `y` and `z"
                                              : "x`, `y`, `z` and `w");
        } else {
            note_written(checker, expr, type->member_count, type->declared_in,
                         type->span, member_span, type);
        }
    }

    uint32_t checked = expr->call.arg_count < type->member_count
                           ? expr->call.arg_count
                           : type->member_count;
    for (uint32_t i = 0; i < checked; i++) {
        KestType *argument =
            check_expr(checker, expr->call.args[i], type->members[i].type);
        if (!kest_type_equal(argument, type->members[i].type)) {
            expected_for(checker, expr->call.args[i]->span,
                         type->members[i].type, argument, type->declared_in,
                         type->members[i].span);
        }
    }
    for (uint32_t i = checked; i < expr->call.arg_count; i++) {
        check_expr(checker, expr->call.args[i], NULL);
    }
    return type;
}

// The builtins are checked here rather than declared, because nothing in the
// type system can yet say "an array of anything" or "whatever this store
// holds". A file that declares its own function of the same name gets that
// one, so none of these is a reserved word.
// Whether a declared parameter could be what was passed. A parameter that
// mentions a type name is not settled yet, so it is asked about its shape:
// `Box<T>` could take a `Box<i32>` and could not take a `[i32]`.
static bool could_take(const KestType *given, const KestType *declared) {
    if (given == NULL || declared == NULL || is_error((KestType *)given)) {
        return true;
    }
    if (!kest_mentions_name(declared)) {
        return kest_type_equal((KestType *)given, (KestType *)declared);
    }
    if (declared->tag == KEST_T_PARAM) {
        return true;
    }
    if (given->tag != declared->tag) {
        return false;
    }
    if (declared->tag == KEST_T_STRUCT) {
        return given->shape == declared->shape;
    }
    return true;
}

// A builtin is one more thing a name could mean, and which one is meant is
// settled by what is passed (D023). A file that declares its own `remove`
// gets that one where it fits and the builtin where it does not, so a module
// can name a function after what it does without losing the builtin.
// Whether two values of this type are one question with one answer, and if
// not, what it was that is not. An enum is its case and what that case
// carries, so it compares exactly when everything it carries does.
static bool has_equality(const KestProgram *program, const KestType *type,
                         const KestType **without) {
    if (type == NULL) {
        return false;
    }
    // A name standing for itself while its generic's body is checked. What it
    // can do is what the declaration said it can, and nothing is inferred from
    // the types somebody might hand it: a body that compares two of them has
    // to say so, or it is a body that is right for some copies and not for
    // others. Asked here rather than in the walk below, because that walk is
    // held to the machine's own lists and the machine never meets one of
    // these: a type name is gone before anything runs. See D1043.
    if (type->tag == KEST_T_PARAM) {
        return kest_wants((KestProgram *)program, type, KEST_WANTS_COMPARES);
    }
    switch (type->tag) {
    case KEST_T_ERROR:
    case KEST_T_INT:
    case KEST_T_FLOAT:
    case KEST_T_BOOL:
    case KEST_T_TEXT:
    case KEST_T_FLAGS:
        return true;
    case KEST_T_ENUM:
        for (uint32_t c = 0; c < type->case_count; c++) {
            for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
                if (!has_equality(program, type->cases[c].payload[p],
                                  without)) {
                    return false;
                }
            }
        }
        return true;
    // And a struct, for the same reason and by the same walk: it is a value
    // laid out flat, so what it is is its fields and nothing else. A field
    // that does not compare is what stops it, and that field is what is said —
    // a struct holding an array holds a handle, and comparing handles is the
    // wrong answer to the question somebody asked. See D874.
    case KEST_T_STRUCT:
        for (uint32_t m = 0; m < type->member_count; m++) {
            if (!has_equality(program, type->members[m].type, without)) {
                return false;
            }
        }
        return true;
    // And that many of something where it stands, which is the third of the
    // three: `[T; N]` is N of them in a row inside whatever holds it, not a
    // handle to N of them elsewhere. `[T]` is the handle and is below.
    case KEST_T_FIXED:
        return has_equality(program, type->element, without);
    // Written out rather than left to a `default`, for the reason its twin in
    // `kest_type_has_text` gives: a tag added to the language would otherwise
    // land on this side without anybody deciding it should. An optional is the
    // one that parts them — it can be written and it cannot be compared,
    // because the one way to ask an optional anything is to take what it holds
    // out. See D541.
    // A reference is the one handle that is an identity rather than a way to
    // reach something, so it is the one that compares. Two of them are equal
    // when they name the same place with the same stamp, which is the question
    // a program asks about a reference and the only one it can ask: is this the
    // same entity, and is it still that entity. A stamp is the build's own
    // counter, so no two places in any two stores of any two machines from one
    // build are ever stamped alike (D314, D316) and comparing two from
    // different stores is answered rather than guessed.
    //
    // Reading both and comparing what they name is what this used to say to do,
    // and it answers something else: two settlers with the same fields are one
    // settler under that reading, and one settler whose hunger went up is two.
    // See D923.
    case KEST_T_REF:
        return true;
    case KEST_T_VOID:
    case KEST_T_OPTIONAL:
    case KEST_T_ARRAY:
    case KEST_T_STORE:
    case KEST_T_FN:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        *without = type;
        return false;
    }
    *without = type;
    return false;
}

static bool is_builtin(Checker *checker, KestExpr *expr, KestSpan name,
                       const char *word) {
    size_t length = strlen(word);
    if (!kest_word_same(word, span_text(checker, name), name.length)) {
        return false;
    }
    // Written bare here, registered under the module it was declared in, so
    // the name to ask about is the one the lookup found.
    KestSymbol *found = kest_lookup_global(checker->program, word, length);
    if (found == NULL) {
        return true;
    }
    KestSymbol *all[32];
    uint32_t count = kest_overloads(checker->program, found->name,
                                    strlen(found->name), all, 32);
    if (count == 0) {
        return false;
    }
    if (expr == NULL || expr->call.arg_count == 0) {
        return false;
    }

    KestDiags *diags = checker->program->diags;
    kest_diags_mute(diags, true);
    KestType *first = check_expr(checker, expr->call.args[0], NULL);
    kest_diags_mute(diags, false);
    for (uint32_t i = 0; i < count; i++) {
        const KestType *type = all[i]->type;
        // What it takes first is what says which of the two was meant. How
        // many it takes does not: a call with the wrong number of arguments is
        // a mistake in the call, and answering it with what a builtin of the
        // same name would have wanted is answering a question nobody asked.
        if (type->param_count == 0 && expr->call.arg_count == 0) {
            return false;
        }
        if (type->param_count > 0 && could_take(first, type->params[0])) {
            return false;
        }
    }
    return true;
}

// What is being called, when it is a name: `push` in `push(a)`. Every builtin
// is called by one, so the message about how many it takes can say which it
// was rather than leaving the reader to look up the line.
static uint32_t check_arity(Checker *checker, KestExpr *expr, uint32_t want) {
    if (expr->call.arg_count != want) {
        KestSpan name = expr->call.callee->span;
        report(checker, expr->span, "K0309",
               "`%.*s` takes %u argument%s, found %u", (int)name.length,
               span_text(checker, name), want, want == 1 ? "" : "s",
               expr->call.arg_count);
    }
    return expr->call.arg_count < want ? expr->call.arg_count : want;
}

// The store and what it holds, or NULL when the first argument is not one.
static KestType *check_store_argument(Checker *checker, KestExpr *expr,
                                      const char *word) {
    KestType *store = check_expr(checker, expr->call.args[0], NULL);
    if (is_error(store)) {
        return NULL;
    }
    if (store->tag != KEST_T_STORE) {
        report(checker, expr->call.args[0]->span, "K0310",
               "`%s` works on a store, found `%s`", word,
               type_name(checker, store));
        return NULL;
    }
    return store;
}

// A run of bytes, which is the one array a piece of text goes on the end of
// whole. See D1068.
static bool bytes_run(const KestType *array) {
    return array != NULL && array->tag == KEST_T_ARRAY &&
           array->element != NULL && array->element->tag == KEST_T_INT &&
           array->element->width == 8 && !array->element->is_signed;
}

static void check_ref_argument(Checker *checker, KestExpr *expr, uint32_t at,
                               const KestType *store) {
    KestType *wanted = kest_ref_of(checker->program, store->element);
    KestType *handle = check_expr(checker, expr->call.args[at], wanted);
    if (!kest_type_equal(handle, wanted)) {
        expected_but(checker, expr->call.args[at]->span, wanted, handle,
                     "this reference");
    }
}

// A builtin is not somebody else. Reading a name inside one -- `len(xs)`,
// `fit(xs, v)`, an index -- says nothing about whether it was given room, and
// the two that do give room say so themselves. Cleared across the whole of
// one rather than around each argument, because a call to somebody's own
// function nested inside a builtin sets it again on its own way down.
// See D1135.
static KestType *check_builtin_here(Checker *checker, KestExpr *expr,
                                    const KestType *expected, bool *handled);

static KestType *check_builtin(Checker *checker, KestExpr *expr,
                               const KestType *expected, bool *handled) {
    bool was_handing = checker->handing_a_name_over;
    checker->handing_a_name_over = false;
    KestType *answered = check_builtin_here(checker, expr, expected, handled);
    checker->handing_a_name_over = was_handing;
    return answered;
}

static KestType *check_builtin_here(Checker *checker, KestExpr *expr,
                               const KestType *expected, bool *handled) {
    *handled = true;
    KestSpan name = expr->call.callee->span;

    if (is_builtin(checker, expr, name, "store")) {
        // Room for that many before anything is put in, or none said and none
        // made. A store grows by doubling, so the frame that pays for the next
        // eight is a frame a host cannot move; saying how many there will be
        // moves it out of the loop and changes nothing else.
        uint32_t wanted = expr->call.arg_count > 0 ? 1 : 0;
        check_arity(checker, expr, wanted);
        if (wanted == 1) {
            KestType *count = check_expr(checker, expr->call.args[0],
                                         builtin(checker, "i32"));
            if (!is_error(count) && count->tag != KEST_T_INT) {
                report(checker, expr->call.args[0]->span, "K0310",
                       "a count is an integer, found `%s`",
                       type_name(checker, count));
            }
            // The same reading `array` does: a count written down here is
            // worth reading here, rather than running to be told.
            int64_t written = 0;
            if (written_number(checker, expr->call.args[0], &written) &&
                written < 0) {
                report(checker, expr->call.args[0]->span, "K0351",
                       "a store cannot have room for %lld",
                       (long long)written);
                suggest(checker, "a count is nought or more, and nought is a "
                                 "store with no room made yet");
            }
        }
        if (expected == NULL || expected->tag != KEST_T_STORE) {
            report(checker, expr->span, "K0322", "`store()` has no type here");
            kest_diags_suggest(checker->program->diags,
                               "write what it holds: "
                               "`let w: store<Npc> = store()`");
            return error_type(checker);
        }
        return (KestType *)expected;
    }

    if (is_builtin(checker, expr, name, "array")) {
        // An empty one takes what it holds from where it is going, the same
        // way `store()` does, because there is nothing to read it off.
        if (expr->call.arg_count == 0) {
            if (expected == NULL || expected->tag != KEST_T_ARRAY) {
                report(checker, expr->span, "K0335",
                       "`array()` has no type here");
                kest_diags_suggest(checker->program->diags,
                                   "write what it holds: "
                                   "`let bytes: [u8] = array()`");
                return error_type(checker);
            }
            return (KestType *)expected;
        }
        if (check_arity(checker, expr, 2) < 2) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return error_type(checker);
        }
        KestType *count = check_expr(checker, expr->call.args[0],
                                     builtin(checker, "i32"));
        if (!is_error(count) && count->tag != KEST_T_INT) {
            report(checker, expr->call.args[0]->span, "K0310",
                   "a count is an integer, found `%s`",
                   type_name(checker, count));
        }
        // A count that can be worked out here is worth reading here. The
        // machine refuses one below nought while it runs, and a program that
        // says how many it wants in the line itself should not have to run to
        // be told.
        int64_t written = 0;
        if (written_number(checker, expr->call.args[0], &written) &&
            written < 0) {
            report(checker, expr->call.args[0]->span, "K0351",
                   "an array cannot have %lld elements", (long long)written);
            suggest(checker, "a count is nought or more, and nought is an "
                             "array with nothing in it");
        }
        // What it holds comes from what it is filled with, so nothing has to
        // be written down twice.
        KestType *element = check_expr(checker, expr->call.args[1], NULL);
        return kest_array_of(checker->program, element);
    }

    if (is_builtin(checker, expr, name, "push")) {
        // And this one gives it room, so nothing is said about it. See
        // D1135.
        if (expr->call.arg_count > 0 &&
            expr->call.args[0]->kind == KEST_EXPR_NAME) {
            Local *held = find_local(
                checker, span_text(checker, expr->call.args[0]->span),
                expr->call.args[0]->span.length);
            if (held != NULL) {
                held->given_room = true;
            }
        }
        if (check_arity(checker, expr, 2) < 2) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return builtin(checker, "void");
        }
        KestType *array = check_expr(checker, expr->call.args[0], NULL);
        if (is_error(array) || array->tag != KEST_T_ARRAY) {
            if (!is_error(array)) {
                report(checker, expr->call.args[0]->span, "K0310",
                       "`push` puts something on an array, found `%s`",
                       type_name(checker, array));
            }
            check_expr(checker, expr->call.args[1], NULL);
            return builtin(checker, "void");
        }
        // Text is its bytes (D021), so a run of bytes and a piece of text are
        // the same bytes: `push(out, "ab")` puts the piece on the end whole,
        // and that is the one thing the machine can do in a move where a
        // program has to write a loop. Every other element takes one of
        // itself. See D1068.
        KestType *value =
            check_expr(checker, expr->call.args[1], array->element);
        if (bytes_run(array) && value != NULL && value->tag == KEST_T_TEXT) {
            return builtin(checker, "void");
        }
        if (!kest_type_equal(value, array->element)) {
            expected_but(checker, expr->call.args[1]->span, array->element,
                         value, "this value");
        }
        return builtin(checker, "void");
    }

    // The same append with the growth taken out, which is what a body under a
    // promise can do: it answers whether it fitted rather than making room.
    // See D940.
    if (is_builtin(checker, expr, name, "fit")) {
        // The name itself, so what this body did to it can be read where the
        // scope ends. See D1135.
        if (expr->call.arg_count > 0 &&
            expr->call.args[0]->kind == KEST_EXPR_NAME) {
            Local *held = find_local(
                checker, span_text(checker, expr->call.args[0]->span),
                expr->call.args[0]->span.length);
            if (held != NULL) {
                held->fitted = true;
            }
        }
        if (check_arity(checker, expr, 2) < 2) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return builtin(checker, "bool");
        }
        KestType *array = check_expr(checker, expr->call.args[0], NULL);
        if (is_error(array) || array->tag != KEST_T_ARRAY) {
            if (!is_error(array)) {
                report(checker, expr->call.args[0]->span, "K0310",
                       "`fit` puts something on an array if there is room, "
                       "found `%s`",
                       type_name(checker, array));
            }
            check_expr(checker, expr->call.args[1], NULL);
            return builtin(checker, "bool");
        }
        KestType *value =
            check_expr(checker, expr->call.args[1], array->element);
        if (bytes_run(array) && value != NULL && value->tag == KEST_T_TEXT) {
            return builtin(checker, "bool");
        }
        if (!kest_type_equal(value, array->element)) {
            expected_but(checker, expr->call.args[1]->span, array->element,
                         value, "this value");
        }
        return builtin(checker, "bool");
    }

    // Room for what is coming, which is the other half of `array(n, v)`: that
    // one makes a new array with room and this one gives an array it already
    // has. What it does not change is what is in it or how many there are, so
    // a program that asks for less than it holds has asked for nothing. See
    // D912.
    if (is_builtin(checker, expr, name, "room")) {
        // And this one gives it room, so nothing is said about it. See
        // D1135.
        if (expr->call.arg_count > 0 &&
            expr->call.args[0]->kind == KEST_EXPR_NAME) {
            Local *held = find_local(
                checker, span_text(checker, expr->call.args[0]->span),
                expr->call.args[0]->span.length);
            if (held != NULL) {
                held->given_room = true;
            }
        }
        if (check_arity(checker, expr, 2) < 2) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return builtin(checker, "void");
        }
        KestType *array = check_expr(checker, expr->call.args[0], NULL);
        if (is_error(array) ||
            (array->tag != KEST_T_ARRAY && array->tag != KEST_T_STORE)) {
            if (!is_error(array)) {
                report(checker, expr->call.args[0]->span, "K0310",
                       "`room` makes room in an array or a store, found `%s`",
                       type_name(checker, array));
            }
            check_expr(checker, expr->call.args[1], NULL);
            return builtin(checker, "void");
        }
        KestType *many =
            check_expr(checker, expr->call.args[1], builtin(checker, "i32"));
        if (!is_error(many) && many->tag != KEST_T_INT) {
            expected_but(checker, expr->call.args[1]->span,
                         builtin(checker, "i32"), many, "`room`");
        }
        return builtin(checker, "void");
    }

    // Taking things out of an array. A store answers this with `remove` and a
    // reference; here a position means something, so what is after what went
    // keeps its order and the cost of that is on `remove` where it is written.
    if (is_builtin(checker, expr, name, "pop") || is_builtin(checker, expr, name, "remove") ||
        is_builtin(checker, expr, name, "clear")) {
        bool taking = is_builtin(checker, expr, name, "remove");
        bool emptying = is_builtin(checker, expr, name, "clear");
        uint32_t wanted = taking ? 2 : 1;
        if (check_arity(checker, expr, wanted) < wanted) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return emptying ? builtin(checker, "void") : error_type(checker);
        }
        KestType *array = check_expr(checker, expr->call.args[0], NULL);
        // A store answers `remove` too, and with a reference rather than a
        // position, so which one is meant is settled by what is handed in.
        if (taking && !is_error(array) && array->tag == KEST_T_STORE) {
            check_ref_argument(checker, expr, 1, array);
            return builtin(checker, "bool");
        }
        for (uint32_t i = 1; i < expr->call.arg_count; i++) {
            const KestType *want = builtin(checker, "i32");
            KestType *given = check_expr(checker, expr->call.args[i], want);
            if (i < wanted && !kest_type_equal(given, want)) {
                expected_but(checker, expr->call.args[i]->span, want, given,
                             "this position");
            }
            if (i < wanted) {
                written_place(checker, expr->call.args[i], false, NULL);
            }
        }
        if (is_error(array) || array->tag != KEST_T_ARRAY) {
            if (!is_error(array)) {
                report(checker, expr->call.args[0]->span, "K0310",
                       "`%s` works on an array%s, found `%s`",
                       emptying ? "clear" : (taking ? "remove" : "pop"),
                       taking ? " or a store" : "", type_name(checker, array));
            }
            return emptying ? builtin(checker, "void") : error_type(checker);
        }
        if (emptying) {
            return builtin(checker, "void");
        }
        // Taking a named position out is a position that is there; taking the
        // end off an array that may be empty is a lookup like any other.
        return taking ? array->element
                      : kest_optional_of(checker->program, array->element);
    }

    // Whether a piece of text sits at a place in another. Comparing two
    // places in text cannot be written in the language for what it should
    // cost, because an index into a piece of text costs the index.
    if (is_builtin(checker, expr, name, "matches")) {
        uint32_t wanted = check_arity(checker, expr, 3);
        if (wanted == 3) {
            KestType *subject = check_expr(checker, expr->call.args[0],
                                           builtin(checker, "text"));
            if (!is_error(subject) && subject->tag != KEST_T_TEXT) {
                report(checker, expr->call.args[0]->span, "K0310",
                       "`matches` works on text, found `%s`",
                       type_name(checker, subject));
            }
            const KestType *place = builtin(checker, "i32");
            KestType *given = check_expr(checker, expr->call.args[1], place);
            if (!kest_type_equal(given, place)) {
                // The words the reference prints for it: `matches(t, at,
                // needle)`. A reader who has read that line knows which one
                // this is without counting along the call.
                expected_called(checker, expr->call.args[1]->span, place,
                                given, takes_called("matches", 1));
            }
            written_place(checker, expr->call.args[1], true, NULL);
            const KestType *piece = builtin(checker, "text");
            KestType *needle = check_expr(checker, expr->call.args[2], piece);
            if (!kest_type_equal(needle, piece)) {
                expected_called(checker, expr->call.args[2]->span, piece,
                                needle, takes_called("matches", 2));
            }
        }
        for (uint32_t i = wanted; i < expr->call.arg_count; i++) {
            check_expr(checker, expr->call.args[i], NULL);
        }
        return builtin(checker, "bool");
    }

    // What is left of a piece of text from a place in it. It is not `slice`
    // with one argument missing: nothing is copied, because a piece ends where
    // it ends and the rest of one is a place inside it.
    if (is_builtin(checker, expr, name, "rest")) {
        uint32_t wanted = check_arity(checker, expr, 2);
        if (wanted == 2) {
            KestType *subject = check_expr(checker, expr->call.args[0],
                                           builtin(checker, "text"));
            if (!is_error(subject) && subject->tag != KEST_T_TEXT) {
                report(checker, expr->call.args[0]->span, "K0310",
                       "`rest` works on text, found `%s`",
                       type_name(checker, subject));
            }
            const KestType *want = builtin(checker, "i32");
            KestType *given = check_expr(checker, expr->call.args[1], want);
            if (!kest_type_equal(given, want)) {
                expected_called(checker, expr->call.args[1]->span, want,
                                given, takes_called("rest", 1));
            }
            written_place(checker, expr->call.args[1], true, NULL);
        }
        for (uint32_t i = wanted; i < expr->call.arg_count; i++) {
            check_expr(checker, expr->call.args[i], NULL);
        }
        return builtin(checker, "text");
    }

    if (is_builtin(checker, expr, name, "slice") || is_builtin(checker, expr, name, "find")) {
        bool slicing = is_builtin(checker, expr, name, "slice");
        // `find` takes where to start looking, or starts at the beginning.
        // Scanning a piece of text for every place something is in it is then
        // a walk rather than a slice per step, and a slice reaches the heap.
        uint32_t wanted =
            slicing ? 3 : (expr->call.arg_count > 2 ? 3 : 2);
        if (check_arity(checker, expr, wanted) < wanted) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return error_type(checker);
        }

        KestType *subject =
            check_expr(checker, expr->call.args[0], builtin(checker, "text"));
        if (!is_error(subject) && subject->tag != KEST_T_TEXT) {
            report(checker, expr->call.args[0]->span, "K0310",
                   "`%s` works on text, found `%s`", slicing ? "slice" : "find",
                   type_name(checker, subject));
        }

        for (uint32_t i = 1; i < wanted; i++) {
            const KestType *want = slicing || i > 1 ? builtin(checker, "i32")
                                                    : builtin(checker, "text");
            KestType *given = check_expr(checker, expr->call.args[i], want);
            if (!kest_type_equal(given, want)) {
                expected_called(checker, expr->call.args[i]->span, want,
                                given,
                                takes_called(slicing ? "slice" : "find", i));
            }
            // Where it starts and how many bytes, both written down as often
            // as not. How long the text is is not known here; that neither of
            // these can be below nought is.
            int64_t written = 0;
            if (slicing && i == 2) {
                if (written_number(checker, expr->call.args[i], &written) &&
                    written < 0) {
                    report(checker, expr->call.args[i]->span, "K0351",
                           "a piece of text cannot be %lld bytes long",
                           (long long)written);
                }
            } else if (want->tag == KEST_T_INT) {
                written_place(checker, expr->call.args[i], true, NULL);
            }
        }
        for (uint32_t i = wanted; i < expr->call.arg_count; i++) {
            check_expr(checker, expr->call.args[i], NULL);
        }

        // Finding something that is not there is a lookup like any other.
        return slicing ? builtin(checker, "text")
                       : kest_optional_of(checker->program,
                                          builtin(checker, "i32"));
    }

    // A number standing for a value. It applies exactly where `==` does, and
    // that is the whole rule: a type that compares has one and a type that
    // does not has neither.
    // A float as the bits it is made of, and back: what a float written into
    // bytes and read out of them has to be to come back as itself, which a
    // conversion to a whole number is not. The width goes with it: an `f32`
    // is thirty-two bits and an `f64` sixty-four. See D1171.
    if (is_builtin(checker, expr, name, "bits")) {
        uint32_t checked = check_arity(checker, expr, 1);
        KestType *of = expr->call.arg_count > 0
                           ? check_expr(checker, expr->call.args[0], NULL)
                           : NULL;
        for (uint32_t i = 1; i < expr->call.arg_count; i++) {
            check_expr(checker, expr->call.args[i], NULL);
        }
        if (checked > 0 && of != NULL && !is_error(of)) {
            if (of->tag == KEST_T_FLOAT) {
                return builtin(checker, of->width == 32 ? "u32" : "u64");
            }
            report(checker, expr->call.args[0]->span, "K0310",
                   "`bits` is what an `f32` or an `f64` is made of, found "
                   "`%s`",
                   type_name(checker, of));
            kest_diags_suggest(checker->program->diags,
                               "a whole number is its own bits: `u64(n)` "
                               "is what it is as one without a sign");
        }
        return error_type(checker);
    }
    if (is_builtin(checker, expr, name, "float")) {
        uint32_t checked = check_arity(checker, expr, 1);
        KestType *of = expr->call.arg_count > 0
                           ? check_expr(checker, expr->call.args[0], NULL)
                           : NULL;
        for (uint32_t i = 1; i < expr->call.arg_count; i++) {
            check_expr(checker, expr->call.args[i], NULL);
        }
        if (checked > 0 && of != NULL && !is_error(of)) {
            if (of->tag == KEST_T_INT && !of->is_signed &&
                (of->width == 32 || of->width == 64)) {
                return builtin(checker, of->width == 32 ? "f32" : "f64");
            }
            report(checker, expr->call.args[0]->span, "K0310",
                   "`float` is the float a `u32` or a `u64` holds the bits "
                   "of, found `%s`",
                   type_name(checker, of));
            kest_diags_suggest(checker->program->diags,
                               "a number to be turned into a float is "
                               "`f32(n)` or `f64(n)`");
        }
        return error_type(checker);
    }

    if (is_builtin(checker, expr, name, "hash")) {
        uint32_t checked = check_arity(checker, expr, 1);
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            KestType *of = check_expr(checker, expr->call.args[i], NULL);
            const KestType *without = NULL;
            if (i == 0 && checked > 0 && !is_error(of) &&
                !has_equality(checker->program, of, &without)) {
                report(checker, expr->call.args[i]->span, "K0310",
                       "`hash` stands for what compares, and `%s` does not",
                       type_name(checker, of));
                kest_diags_suggest(checker->program->diags,
                                   "combine the fields that decide it: "
                                   "`hash(a) * 31 ^ hash(b)`");
            }
        }
        return builtin(checker, "u64");
    }

    if (is_builtin(checker, expr, name, "len")) {
        uint32_t checked = check_arity(checker, expr, 1);
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            KestType *argument = check_expr(checker, expr->call.args[i], NULL);
            if (i == 0 && checked > 0 && !is_error(argument) &&
                !has_length(argument)) {
                report(checker, expr->call.args[i]->span, "K0310",
                       "`len` counts an array, a store or text, found `%s`",
                       type_name(checker, argument));
                if (argument->element != NULL &&
                    has_length(argument->element)) {
                    say_if_let(checker, argument, NULL);
                }
            }
        }
        return builtin(checker, "i32");
    }

    if (is_builtin(checker, expr, name, "add")) {
        if (check_arity(checker, expr, 2) < 2) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return error_type(checker);
        }
        KestType *store = check_store_argument(checker, expr, "add");
        if (store == NULL) {
            check_expr(checker, expr->call.args[1], NULL);
            return error_type(checker);
        }
        KestType *value = check_expr(checker, expr->call.args[1], store->element);
        if (!kest_type_equal(value, store->element)) {
            expected_but(checker, expr->call.args[1]->span, store->element,
                         value, "this value");
        }
        return kest_ref_of(checker->program, store->element);
    }

    if (is_builtin(checker, expr, name, "get")) {
        bool getting = true;
        if (check_arity(checker, expr, 2) < 2) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return error_type(checker);
        }
        KestType *store =
            check_store_argument(checker, expr, getting ? "get" : "remove");
        if (store == NULL) {
            check_expr(checker, expr->call.args[1], NULL);
            return error_type(checker);
        }
        check_ref_argument(checker, expr, 1, store);
        // Reading through a reference is a lookup that can fail, so what comes
        // back is an optional and D013 is what opens it.
        return getting ? kest_optional_of(checker->program, store->element)
                       : builtin(checker, "bool");
    }

    if (is_builtin(checker, expr, name, "set")) {
        if (check_arity(checker, expr, 3) < 3) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return error_type(checker);
        }
        KestType *store = check_store_argument(checker, expr, "set");
        if (store == NULL) {
            check_expr(checker, expr->call.args[1], NULL);
            check_expr(checker, expr->call.args[2], NULL);
            return error_type(checker);
        }
        check_ref_argument(checker, expr, 1, store);
        KestType *value = check_expr(checker, expr->call.args[2], store->element);
        if (!kest_type_equal(value, store->element)) {
            expected_but(checker, expr->call.args[2]->span, store->element,
                         value, "this value");
        }
        return builtin(checker, "bool");
    }

    *handled = false;
    return NULL;
}

// Every function the callee could name. A dotted callee is one name with a dot
// in it, so both shapes are looked up the same way.
static uint32_t find_callable(Checker *checker, const KestExpr *expr,
                              KestSymbol **found, uint32_t room) {
    const KestExpr *callee = expr->call.callee;
    if (callee->kind != KEST_EXPR_NAME &&
        !(callee->kind == KEST_EXPR_FIELD &&
          callee->field.object->kind == KEST_EXPR_NAME)) {
        return 0;
    }

    const char *text = span_text(checker, callee->span);
    size_t length = callee->span.length;
    const char *alias = checker->program->module;
    if (alias[0] != '\0') {
        char joined[256];
        int written = snprintf(joined, sizeof(joined), "%s.%.*s", alias,
                               (int)length, text);
        if (written > 0 && (size_t)written < sizeof(joined)) {
            uint32_t count = kest_overloads(checker->program, joined,
                                            (size_t)written, found, room);
            if (count > 0) {
                return count;
            }
        }
    }
    uint32_t count = kest_overloads(checker->program, text, length, found, room);
    if (count > 0) {
        // A name with a dot in it, resolved: the other way a module is
        // reached, and the one a name several functions share takes — the
        // lookups that ask for one symbol answer nothing for those, so this is
        // where `math.abs` says the import it came through is worth its place.
        // See D725.
        kest_import_reached(checker->program, text, length);
    }
    return count;
}

// A literal has no type of its own to lose, so it is the one thing that may
// take a type from the function that was chosen rather than the other way
// round.
static KestType *check_arguments(Checker *checker, KestExpr *expr,
                                 const KestType *callee);
static KestType *arguments_checked(Checker *checker, KestExpr *expr,
                                   const KestType *callee);


// What a literal is when nothing says otherwise: a whole number is an `i32`,
// and a number with a fraction is an `f32`, which is the working precision of
// the workloads this language is for. Said here and nowhere else. The walk that
// gives a bare literal its type and the pass that settles a call between two
// widths are asking one question, and it was answered in two places -- a
// default changed in one of them would have left the other preferring the old
// one, and nothing would have said so. See D766.
static KestType *literal_alone(Checker *checker, const KestExpr *literal) {
    return builtin(checker, literal->kind == KEST_EXPR_INT ? "i32" : "f32");
}

static bool literal_suits(Checker *checker, const KestExpr *expr,
                          const KestType *want, bool exactly) {
    const KestExpr *literal = literal_of(expr);
    if (want == NULL) {
        return false;
    }
    bool integer = literal->kind == KEST_EXPR_INT;
    if (!exactly) {
        // Any width of the right family will take it.
        return want->tag == (integer ? KEST_T_INT : KEST_T_FLOAT);
    }
    return kest_type_equal((KestType *)want, literal_alone(checker, literal));
}

static KestType *check_overloaded(Checker *checker, KestExpr *expr,
                                  KestSymbol **candidates, uint32_t count) {
    // What each argument is, found out rather than reported, because the
    // wrong choice would report against the wrong function.
    KestDiags *diags = checker->program->diags;
    kest_diags_mute(diags, true);
    KestType *given[16];
    uint32_t argument_count = expr->call.arg_count < 16 ? expr->call.arg_count
                                                        : 16;
    for (uint32_t i = 0; i < argument_count; i++) {
        given[i] = check_expr(checker, expr->call.args[i], NULL);
    }
    kest_diags_mute(diags, false);

    // Once by family, so a literal fits any width of the right kind, and
    // again exactly, for when the literals are all there is to go on. The
    // second is a tie-breaker and not a second chance: what fits exactly fits
    // the family too, so a family pass that found nothing is an exact pass
    // that would find nothing, and it is asked only where the first left more
    // than one standing. See D765.
    KestSymbol *chosen = NULL;
    uint32_t matches = 0;
    uint32_t by_family = 0;
    bool fitted[16] = {false};
    for (uint32_t pass = 0; pass < 2; pass++) {
        chosen = NULL;
        matches = 0;
        for (uint32_t c = 0; c < count; c++) {
            const KestType *type = candidates[c]->type;
            if (type->param_count != expr->call.arg_count ||
                expr->call.arg_count > 16) {
                continue;
            }
            bool fits = true;
            for (uint32_t i = 0; i < argument_count && fits; i++) {
                if (is_literal(expr->call.args[i])) {
                    fits = literal_suits(checker, expr->call.args[i],
                                         type->params[i], pass == 1);
                } else if (given[i] != NULL && given[i]->tag == KEST_T_FN &&
                           type->params[i] != NULL &&
                           type->params[i]->tag == KEST_T_FN) {
                    // A name that is several functions is like a literal: it
                    // takes the shape of the place it is going, so it is
                    // asked again with that shape in hand.
                    kest_diags_mute(diags, true);
                    fits = kest_type_equal(
                        check_expr(checker, expr->call.args[i],
                                   type->params[i]),
                        type->params[i]);
                    kest_diags_mute(diags, false);
                } else {
                    fits = kest_type_equal(given[i], type->params[i]);
                }
            }
            if (fits) {
                chosen = candidates[c];
                chosen->named = true;
                matches++;
                if (pass == 0 && c < 16) {
                    fitted[c] = true;
                }
            }
        }
        if (pass == 0) {
            by_family = matches;
            if (matches < 2) {
                break;
            }
        }
    }

    if (matches != 1) {
        // What was passed, in the notation the candidates are written in. It
        // said `these` and left a reader to work out which of the shapes below
        // their own call was -- which is the one thing already known here,
        // because every argument was settled before any candidate was tried.
        // A literal is said as what it is rather than as the type it would
        // have taken alone: `pick(1)` is a whole number, and calling it `i32`
        // would be this compiler answering a question nobody asked. See D764.
        char passed[256];
        int wrote = 0;
        for (uint32_t i = 0; i < argument_count && wrote >= 0 &&
                             (size_t)wrote < sizeof(passed);
             i++) {
            const KestExpr *literal = literal_of(expr->call.args[i]);
            // `none` is written as `none` and has no type of its own until
            // something says what it is the absence of, so it is said as
            // itself rather than as the `<unknown>` a nameless type answers
            // with.
            const char *what =
                expr->call.args[i]->kind == KEST_EXPR_NONE ? "none"
                : literal != NULL
                    ? (literal->kind == KEST_EXPR_INT ? "a whole number"
                                                      : "a number with a "
                                                        "fraction")
                    : kest_type_name(checker->program->arena, given[i]);
            wrote += snprintf(passed + wrote, sizeof(passed) - (size_t)wrote,
                              "%s%s", i == 0 ? "" : ", ", what);
        }
        if (wrote >= 0 && expr->call.arg_count > argument_count &&
            (size_t)wrote < sizeof(passed)) {
            wrote += snprintf(passed + wrote, sizeof(passed) - (size_t)wrote,
                              ", and %u more",
                              expr->call.arg_count - argument_count);
        }
        passed[wrote < 0 ? 0 : wrote] = '\0';
        // And which of the two happened. A pass over the families can find
        // several where a pass over the exact types finds none -- two `pick`s
        // taking `u8` and `u16`, called with `1` -- and what a reader was told
        // then was that nothing takes it. More than one does, and no one of
        // them is the one. See D764.
        bool several = matches > 1 || (matches == 0 && by_family > 1);
        report(checker, expr->span, "K0329",
               several ? "more than one `%.*s` takes these"
                       : "no `%.*s` takes these",
               (int)expr->call.callee->span.length,
               span_text(checker, expr->call.callee->span));
        // Beside the caret rather than in the sentence, because the sentence
        // is what the log quotes and what a reader greps: `these` is what
        // happened and this is what `these` are. See D764.
        kest_diags_suggest(diags, "these are (%s)", passed);
        // Which eight. A diagnostic holds eight places and counts the rest,
        // and the first eight declared are eight in an order that has nothing
        // to do with what was called: a reader is looking for the one they
        // meant, so the near misses go first. Taking as many as were passed
        // counts for more than agreeing about any of them, because a call of
        // the wrong length is a different mistake from a call of the wrong
        // kinds. Ties keep the order they were declared in, which is the only
        // order that is not this compiler's opinion. See D763.
        uint32_t order[16];
        uint32_t scored[16];
        uint32_t listed = count < 16 ? count : 16;
        for (uint32_t c = 0; c < listed; c++) {
            const KestType *type = candidates[c]->type;
            uint32_t agrees = 0;
            for (uint32_t i = 0; i < argument_count && i < type->param_count;
                 i++) {
                if (given[i] != NULL &&
                    kest_type_equal(given[i], type->params[i])) {
                    agrees++;
                }
            }
            scored[c] = (type->param_count == expr->call.arg_count ? 64 : 0) +
                        agrees;
            order[c] = c;
        }
        for (uint32_t a = 0; a + 1 < listed; a++) {
            uint32_t best = a;
            for (uint32_t b = a + 1; b < listed; b++) {
                if (scored[order[b]] > scored[order[best]]) {
                    best = b;
                }
            }
            uint32_t moved = order[best];
            for (uint32_t b = best; b > a; b--) {
                order[b] = order[b - 1];
            }
            order[a] = moved;
        }
        for (uint32_t c = 0; c < listed; c++) {
            // When more than one takes these, the places to show are the ones
            // that do. The others are what a reader is not looking at: the
            // sentence says several take it, and a list holding the ones that
            // do not is a list answering a different sentence. See D765.
            if (several && !fitted[order[c]]) {
                continue;
            }
            const KestSymbol *one = candidates[order[c]];
            char shape[256];
            int used = 0;
            for (uint32_t p = 0; p < one->type->param_count && used >= 0 &&
                                 (size_t)used < sizeof(shape);
                 p++) {
                used += snprintf(shape + used, sizeof(shape) - (size_t)used,
                                 "%s%s", p == 0 ? "" : ", ",
                                 kest_type_name(checker->program->arena,
                                                one->type->params[p]));
            }
            shape[used < 0 ? 0 : used] = '\0';
            kest_diags_note(diags, one->source, one->span,
                            "this one takes (%s)", shape);
        }
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            check_expr(checker, expr->call.args[i], NULL);
        }
        return error_type(checker);
    }

    expr->call.callee->type = chosen->type;
    return check_arguments(checker, expr, chosen->type);
}

// What a copy is compiled under: the name the generic was compiled under with
// what it was given written into it, so two copies never share a name.
static const char *instance_symbol(KestProgram *program, const char *base,
                                   KestType **bindings, uint32_t count) {
    // As long as it is. This was two hundred and fifty-six bytes, and two
    // copies whose type names agreed that far were compiled under one name:
    // the second one written won, and a program calling the first ran the
    // other one's code over its own values.
    size_t room = strlen(base) + 1;
    for (uint32_t i = 0; i < count; i++) {
        room += strlen(kest_type_name(program->arena, bindings[i])) + 1;
    }
    char *written = kest_arena_alloc(program->arena, room, 1);
    if (written == NULL) {
        return base;
    }
    size_t used = (size_t)snprintf(written, room, "%s", base);
    for (uint32_t i = 0; i < count; i++) {
        used += (size_t)snprintf(written + used, room - used, "$%s",
                                 kest_type_name(program->arena, bindings[i]));
    }
    return written;
}

// A generic function named where a function type is wanted: the copy that
// fits. A call settles what its type names are by what is passed (D023); this
// settles them by what is wanted, which is the same question from the other
// side — and without it, `sort.by(items, sort.ascending)` cannot be written
// with an `ascending` that works for every type that has an order.
static KestType *copy_for_shape(Checker *checker, const KestType *callee,
                                const KestType *expected, KestSpan where) {
    KestProgram *program = checker->program;
    if (expected == NULL || expected->tag != KEST_T_FN ||
        expected->type_param_count > 0 ||
        expected->param_count != callee->param_count ||
        callee->decl == NULL) {
        return NULL;
    }

    uint32_t generics = callee->type_param_count;
    const char **names =
        KEST_ARENA_ARRAY(program->arena, const char *, generics);
    KestType **bindings = KEST_ARENA_ARRAY(program->arena, KestType *, generics);
    if (names == NULL || bindings == NULL) {
        return NULL;
    }
    for (uint32_t g = 0; g < generics; g++) {
        names[g] = callee->type_param_names[g];
        bindings[g] = NULL;
    }

    bool agreed = true;
    for (uint32_t i = 0; i < callee->param_count; i++) {
        agreed = kest_unify(callee->params[i], expected->params[i], names,
                            bindings, generics) && agreed;
    }
    agreed = kest_unify(callee->result, expected->result, names, bindings,
                        generics) && agreed;
    for (uint32_t g = 0; g < generics && agreed; g++) {
        agreed = bindings[g] != NULL;
    }
    if (!agreed) {
        return NULL;
    }

    settle_names(program, bindings, generics);
    // A copy taken from the shape a value is going into rather than from what
    // was passed, and held to the same contract: what the generic says it
    // needs is needed however the copy was asked for. What comes back is a
    // type that is wrong rather than nothing, because nothing here reads as
    // "there is no copy of this shape" and the caller says so as well.
    if (!wants_met(checker, callee, where, bindings, generics, where)) {
        return error_type(checker);
    }
    KestType *shape = shape_of_call(checker, callee, names, bindings, generics);
    if (shape != NULL) {
        return shape;
    }
    KestInstance *instance = kest_instance_of(program, callee->decl,
                                              callee->unit, names, bindings,
                                              generics);
    if (instance == NULL) {
        checker->out_of_memory = true;
        return NULL;
    }
    if (instance->site.length == 0) {
        bool inside = checker->asking.length > 0;
        instance->site = inside ? checker->asking : where;
        instance->site_source = inside ? checker->asking_source
                                       : program->source;
    }
    if (instance->type == NULL) {
        instance->type = kest_substitute(program, (KestType *)callee, names,
                                         bindings, generics);
        // A copy that could not be made is nothing to write a name into, and
        // what is under this reads it as a signature: `symbol` is sixty-four
        // bytes into a type, which is where a run with nothing left used to
        // stop. Out of memory is a refusal like any other. See D645.
        if (instance->type == NULL) {
            checker->out_of_memory = true;
            return error_type(checker);
        }
        instance->type->symbol = instance_symbol(program, callee->symbol,
                                                 bindings, generics);
        instance->type->type_param_count = 0;
        instance->symbol = instance->type->symbol;
    }
    return instance->type;
}

// One sentence for a type name nothing settles, and one for two places that
// settle it differently. Four mistakes shared `K0343` -- these two, a generic
// named rather than called, and a copy whose declaration is not there -- which
// is D756's fault the other way round: a code is what a reader looks up, so one
// code over four things is four answers to one question. `K0343` stays with
// this one because the log quotes it saying this, and what varies between the
// two places is where the compiler looked, which is an argument rather than a
// sentence. See D759.
static void cannot_be_told(Checker *checker, KestSpan where, const char *name,
                           const char *from) {
    report(checker, where, "K0343",
           "what `%s` is here cannot be told from %s", name, from);
    // And the rule, which is one rule and was said two ways: a builder was
    // told to say it where the value is going and a call that it had to
    // appear in an argument or where what the call gives is written down.
    // Measured, both are true of both -- a builder settles its names from a
    // `return` type and from an argument position, and a call settles them
    // from an argument position as readily as from an annotation. So the
    // sentence says the rule once and names the name it is about. See D760.
    kest_diags_suggest(checker->program->diags,
                       "what tells `%s` is what is passed, or where the value "
                       "is going",
                       name);
}

// Which two, and what each of them makes it. A reader told only that two
// arguments disagree has to work out which two and what each said, and both are
// known here: the walk remembers the position that bound each name, and what
// this position makes it comes of unifying this one on its own. The name is
// what the sentence is about, so it is in the sentence; the two places are what
// the notes are for. See D761.
//
// `what` is `argument` or `field`, because the two walks are two and a builder's
// places are its fields. `at` is the position that would not agree, and `was`
// is what the bindings were before it was asked.
static void told_two_ways(Checker *checker, KestExpr *expr, const char *what,
                          const KestType *wanted, KestType *given,
                          const char **names, KestType **was, uint32_t *from,
                          uint32_t generics, uint32_t at) {
    KestType *alone[16] = {0};
    uint32_t room = generics < 16 ? generics : 16;
    kest_unify(wanted, given, names, alone, room);
    for (uint32_t g = 0; g < room; g++) {
        if (was[g] == NULL || alone[g] == NULL || from[g] == 0 ||
            kest_type_equal(was[g], alone[g])) {
            continue;
        }
        report(checker, expr->span, "K0363",
               "two %ss disagree about what `%s` is", what, names[g]);
        kest_diags_note(checker->program->diags, checker->program->source,
                        expr->call.args[from[g] - 1]->span,
                        "this one makes it `%s`", type_name(checker, was[g]));
        kest_diags_note(checker->program->diags, checker->program->source,
                        expr->call.args[at]->span, "and this one `%s`",
                        type_name(checker, alone[g]));
        return;
    }
    // Nothing to pin it to. Every way of reaching this that anybody has
    // written is a name two places bound differently, because a shape that
    // does not fit at all is what `K0310` answers with the type it wanted
    // written out -- so this is the sentence with the name and without the
    // places, rather than a second sentence nobody can be made to read.
    report(checker, expr->span, "K0363",
           "two %ss disagree about what `%s` is", what, names[0]);
}

// The position that bound each name, so a disagreement can say which two. Set
// where a name goes from nothing to something, and read where one is given
// something else.
static void bound_at(uint32_t *from, KestType **was, KestType **bindings,
                     uint32_t generics, uint32_t at) {
    uint32_t room = generics < 16 ? generics : 16;
    for (uint32_t g = 0; g < room; g++) {
        if (was[g] == NULL && bindings[g] != NULL) {
            from[g] = at + 1;
        }
    }
}

// Which copy of a generic struct is being built. What each type name stands
// for comes from what it is built with, so `Pair(1, "a")` is a
// `Pair<i32, text>` without anything being written twice.
static KestType *copy_wanted(Checker *checker, KestExpr *expr, KestType *shape,
                             const KestType *expected) {
    KestProgram *program = checker->program;
    if (expected != NULL && expected->tag == KEST_T_STRUCT &&
        expected->decl == shape->decl && expected->type_param_count == 0) {
        return (KestType *)expected;
    }

    KestDiags *diags = program->diags;
    kest_diags_mute(diags, true);
    KestType *given[16];
    uint32_t count = expr->call.arg_count < 16 ? expr->call.arg_count : 16;
    for (uint32_t i = 0; i < count; i++) {
        given[i] = check_expr(checker, expr->call.args[i], NULL);
    }
    kest_diags_mute(diags, false);

    uint32_t generics = shape->type_param_count;
    const char **names = KEST_ARENA_ARRAY(checker->program->arena,
                                          const char *,
                                          generics == 0 ? 1 : generics);
    KestType **bindings = KEST_ARENA_ARRAY(checker->program->arena, KestType *,
                                           generics == 0 ? 1 : generics);
    if (names == NULL || bindings == NULL) {
        return NULL;
    }
    for (uint32_t g = 0; g < generics; g++) {
        names[g] = shape->type_param_names[g];
        bindings[g] = NULL;
    }
    bool agreed = true;
    uint32_t from[16] = {0};
    uint32_t said = 0;
    for (uint32_t i = 0; i < count && i < shape->member_count; i++) {
        KestType *was[16] = {0};
        for (uint32_t g = 0; g < generics && g < 16; g++) {
            was[g] = bindings[g];
        }
        if (!kest_unify(shape->members[i].type, given[i], names, bindings,
                        generics)) {
            agreed = false;
            if (said == 0) {
                told_two_ways(checker, expr, "field",
                              shape->members[i].type, given[i], names, was,
                              from, generics, i);
                said = 1;
            }
        }
        bound_at(from, was, bindings, generics, i);
    }
    for (uint32_t g = 0; g < generics; g++) {
        if (bindings[g] == NULL) {
            cannot_be_told(checker, expr->span, names[g],
                           "what this is built with");
            return NULL;
        }
    }
    if (!agreed) {
        return NULL;
    }
    return kest_struct_of(program, shape, bindings, generics);
}

// Which copy of an enum shape a case is building. What it is built with says
// as much as it can -- `Answer.Held(5)` is an `Answer<i32>` -- and where a case
// carries nothing of the shape's own names, the copy comes from where the value
// is going, which is the same rule `array()` and `store()` already keep.
// See D1048.
static KestType *copy_of_case(Checker *checker, KestExpr *expr,
                              KestType *shape, KestSpan case_name,
                              const KestType *expected) {
    KestProgram *program = checker->program;
    if (expected != NULL && expected->tag == KEST_T_ENUM &&
        expected->decl == shape->decl && expected->type_param_count == 0) {
        return (KestType *)expected;
    }
    const KestVariantType *one = NULL;
    for (uint32_t c = 0; c < shape->case_count; c++) {
        if (kest_word_same(shape->cases[c].name,
                           span_text(checker, case_name), case_name.length)) {
            one = &shape->cases[c];
            break;
        }
    }
    if (one == NULL) {
        // Not a case of this shape, which is what `check_case` says and says
        // better: it knows the ones there are.
        return shape;
    }

    KestDiags *diags = program->diags;
    kest_diags_mute(diags, true);
    KestType *given[16];
    uint32_t count = expr == NULL ? 0
                     : expr->call.arg_count < 16 ? expr->call.arg_count : 16;
    for (uint32_t i = 0; i < count; i++) {
        given[i] = check_expr(checker, expr->call.args[i], NULL);
    }
    kest_diags_mute(diags, false);

    uint32_t generics = shape->type_param_count;
    const char **names = KEST_ARENA_ARRAY(program->arena, const char *,
                                          generics == 0 ? 1 : generics);
    KestType **bindings = KEST_ARENA_ARRAY(program->arena, KestType *,
                                           generics == 0 ? 1 : generics);
    if (names == NULL || bindings == NULL) {
        checker->out_of_memory = true;
        return NULL;
    }
    for (uint32_t g = 0; g < generics; g++) {
        names[g] = shape->type_param_names[g];
        bindings[g] = NULL;
    }
    for (uint32_t i = 0; i < count && i < one->payload_count; i++) {
        kest_unify(one->payload[i], given[i], names, bindings, generics);
    }
    // And what the case carries nothing of, from where the value is going.
    if (expected != NULL && expected->tag == KEST_T_ENUM &&
        expected->decl == shape->decl &&
        expected->type_arg_count == generics) {
        for (uint32_t g = 0; g < generics; g++) {
            if (bindings[g] == NULL) {
                bindings[g] = expected->type_args[g];
            }
        }
    }
    for (uint32_t g = 0; g < generics; g++) {
        if (bindings[g] == NULL) {
            cannot_be_told(checker, expr == NULL ? case_name : expr->span,
                           names[g], "what this is built with");
            return NULL;
        }
    }
    return kest_struct_of(program, shape, bindings, generics);
}

// A call to a generic function makes the copy it needs. What each type name
// stands for is worked out from what was passed, and the copy is checked and
// compiled as if it had been written out. See D040.
static KestType *check_generic(Checker *checker, KestExpr *expr,
                               const KestType *callee,
                               const KestType *expected) {
    KestProgram *program = checker->program;
    if (callee->decl == NULL || callee->unit == NULL) {
        // A copy asked for from somewhere its own source is not, which is
        // this project's mistake and not the program's -- so it says so, the
        // way the compiler's own faults do. See D759.
        report(checker, expr->call.callee->span, "K0364",
               "`%s` cannot be made here", type_name(checker, callee));
        kest_diags_fault(program->diags,
                         "a generic was reached without the declaration it is "
                         "copied from");
        return error_type(checker);
    }

    // What was passed, found out rather than reported: a mismatch is said
    // once, against the copy, after the names are known.
    KestDiags *diags = program->diags;
    kest_diags_mute(diags, true);
    KestType *given[16];
    uint32_t count = expr->call.arg_count < 16 ? expr->call.arg_count : 16;
    for (uint32_t i = 0; i < count; i++) {
        given[i] = check_expr(checker, expr->call.args[i], NULL);
    }
    kest_diags_mute(diags, false);

    uint32_t generics = callee->type_param_count;
    const char **names = KEST_ARENA_ARRAY(checker->program->arena,
                                          const char *,
                                          generics == 0 ? 1 : generics);
    KestType **bindings = KEST_ARENA_ARRAY(checker->program->arena, KestType *,
                                           generics == 0 ? 1 : generics);
    if (names == NULL || bindings == NULL) {
        return error_type(checker);
    }
    for (uint32_t g = 0; g < generics; g++) {
        names[g] = callee->type_param_names[g];
        bindings[g] = NULL;
    }
    bool agreed = true;
    uint32_t from[16] = {0};
    uint32_t said = 0;
    // A function passed here may be one of several with that name, and which
    // one it is depends on what the other arguments settled. So the ones that
    // say plainly what they are go first, and a function is asked again with
    // the shape those settled in hand.
    // Which pass an argument goes in is decided by what the parameter is, not
    // by what came back for the argument: a generic named where a function is
    // wanted comes back as an error until it is asked again with the shape in
    // hand, and asking by what came back never asked it again.
    for (uint32_t i = 0; i < count && i < callee->param_count; i++) {
        if (callee->params[i] != NULL &&
            callee->params[i]->tag == KEST_T_FN) {
            continue;
        }
        KestType *was[16] = {0};
        for (uint32_t g = 0; g < generics && g < 16; g++) {
            was[g] = bindings[g];
        }
        if (!kest_unify(callee->params[i], given[i], names,
                        bindings, generics)) {
            agreed = false;
            if (said == 0) {
                told_two_ways(checker, expr, "argument",
                              callee->params[i], given[i], names,
                              was, from, generics, i);
                said = 1;
            }
        }
        bound_at(from, was, bindings, generics, i);
    }
    for (uint32_t i = 0; i < count && i < callee->param_count; i++) {
        if (callee->params[i] == NULL ||
            callee->params[i]->tag != KEST_T_FN) {
            continue;
        }
        KestType *wanted = kest_substitute(program, callee->params[i], names,
                                           bindings, generics);
        kest_diags_mute(diags, true);
        given[i] = check_expr(checker, expr->call.args[i], wanted);
        kest_diags_mute(diags, false);
        KestType *was[16] = {0};
        for (uint32_t g = 0; g < generics && g < 16; g++) {
            was[g] = bindings[g];
        }
        if (!kest_unify(callee->params[i], given[i], names,
                        bindings, generics)) {
            agreed = false;
            if (said == 0) {
                told_two_ways(checker, expr, "argument",
                              callee->params[i], given[i], names,
                              was, from, generics, i);
                said = 1;
            }
        }
        bound_at(from, was, bindings, generics, i);
    }
    // A name that appears only in what it gives back is taken from where the
    // value is going, which is what `array()` and `store()` already do.
    bool wanting = false;
    for (uint32_t g = 0; g < generics; g++) {
        wanting = wanting || bindings[g] == NULL;
    }
    if (wanting && expected != NULL) {
        agreed = kest_unify(callee->result, expected, names, bindings,
                            generics) && agreed;
    }
    for (uint32_t g = 0; g < generics; g++) {
        if (bindings[g] == NULL) {
            cannot_be_told(checker, expr->span, names[g],
                           "what was passed");
            return error_type(checker);
        }
    }
    if (!agreed) {
        return error_type(checker);
    }

    settle_names(program, bindings, generics);
    // What the generic said it needs of what it is given, asked here, where
    // the reader wrote the call. A body checked against those words is a body
    // that works for every type that has them; this is the other half.
    if (!wants_met(checker, callee, expr->call.callee->span, bindings, generics,
                   expr->span)) {
        return error_type(checker);
    }
    KestType *shape = shape_of_call(checker, callee, names, bindings, generics);
    if (shape != NULL) {
        expr->call.callee->type = shape;
        return check_arguments(checker, expr, shape);
    }
    KestInstance *instance = kest_instance_of(program, callee->decl,
                                              callee->unit, names, bindings,
                                              generics);
    if (instance == NULL) {
        checker->out_of_memory = true;
        return error_type(checker);
    }
    // Where this copy was asked for, kept from the first call that asked: a
    // mistake in the body is reported at the body, and the reader wants to
    // know which set of types made it.
    if (instance->site.length == 0) {
        bool inside = checker->asking.length > 0;
        instance->site = inside ? checker->asking : expr->span;
        instance->site_source = inside ? checker->asking_source
                                       : program->source;
    }
    if (instance->type == NULL) {
        instance->type = kest_substitute(program, (KestType *)callee, names,
                                         bindings, generics);
        // The same guard as the one above, at the other place a copy is made:
        // a type that could not be made is read as a signature under this, and
        // `symbol` is sixty-four bytes into one. See D645.
        if (instance->type == NULL) {
            checker->out_of_memory = true;
            return error_type(checker);
        }
        instance->type->symbol = instance_symbol(program, callee->symbol,
                                                 bindings, generics);
        instance->type->type_param_count = 0;
        instance->symbol = instance->type->symbol;
    }
    expr->call.callee->type = instance->type;
    return check_arguments(checker, expr, instance->type);
}

// `Doing` or `world.Doing`: a name for a type, written the way a file writes
// one — bare for its own and with the module in front for anybody else's.
// Either way it is one span, and `kest_lookup_type` reads either. A case of an
// enum another module declared could be matched and could not be made until
// this said so: the name in front of the case was read as a value, and a type
// is not one. See D1062.
static bool names_a_type(const KestExpr *expr) {
    if (expr->kind == KEST_EXPR_NAME) {
        return true;
    }
    return expr->kind == KEST_EXPR_FIELD &&
           expr->field.object->kind == KEST_EXPR_NAME;
}

// Whether `x.f(...)` is a function called on a value rather than a function
// under a module, an extern under its host type or a case under its enum:
// what the dotted chain starts from is a name this body holds, a constant, or
// something worked out -- a call, an index -- and nothing that only names a
// place. And not a field the value has, which is called as the value it is.
// See D1256.
static bool is_method_call(Checker *checker, const KestExpr *expr) {
    const KestExpr *callee = expr->call.callee;
    if (callee->kind != KEST_EXPR_FIELD) {
        return false;
    }
    const KestExpr *root = callee->field.object;
    while (root->kind == KEST_EXPR_FIELD || root->kind == KEST_EXPR_INDEX) {
        root = root->kind == KEST_EXPR_FIELD ? root->field.object
                                             : root->index.object;
    }
    if (root->kind != KEST_EXPR_NAME) {
        // A call, a literal, anything worked out: a value.
        return true;
    }
    const char *text = span_text(checker, root->span);
    if (find_local(checker, text, root->span.length) != NULL) {
        return true;
    }
    const KestSymbol *global =
        kest_lookup_global(checker->program, text, root->span.length);
    return global != NULL && global->is_const;
}

// Whether the value has a field of that name, which is a function held in the
// value and called as one rather than a function taking the value: `r.apply(3)`
// on a `Rule` that holds `apply`. Asked quietly, because the value is checked
// again on the way that is taken. See D1256.
static bool calls_a_field(Checker *checker, KestExpr *expr) {
    const KestExpr *callee = expr->call.callee;
    KestDiags *diags = checker->program->diags;
    kest_diags_mute(diags, true);
    KestType *object = check_expr(checker, callee->field.object, NULL);
    kest_diags_mute(diags, false);
    if (is_error(object) || object->tag != KEST_T_STRUCT) {
        return false;
    }
    const char *name = span_text(checker, callee->field.name);
    for (uint32_t i = 0; i < object->member_count; i++) {
        if (kest_word_same(object->members[i].name, name,
                           callee->field.name.length)) {
            return true;
        }
    }
    return false;
}

// The module a type was declared in, which is the other place a function
// taking it is looked for: `t.get(k)` on a `table.Table` is `table.get(t, k)`.
// A copy of a shape is looked for where the shape was declared. Two of the
// language's own have a module that is theirs the same way: what is built on
// the vectors is `std.vec`, and on text `std.text`.
static size_t module_of(const KestType *type, const char **module) {
    if (kest_is_vector(type)) {
        *module = "std.vec";
        return strlen(*module);
    }
    if (type != NULL && type->tag == KEST_T_TEXT) {
        *module = "std.text";
        return strlen(*module);
    }
    const KestType *named = type;
    if (named != NULL && named->shape != NULL) {
        named = named->shape;
    }
    if (named == NULL || named->name == NULL ||
        (named->tag != KEST_T_STRUCT && named->tag != KEST_T_ENUM &&
         named->tag != KEST_T_FLAGS)) {
        return 0;
    }
    const char *end = strchr(named->name, '<');
    size_t length = end == NULL ? strlen(named->name)
                                : (size_t)(end - named->name);
    while (length > 0 && named->name[length - 1] != '.') {
        length--;
    }
    if (length == 0) {
        return 0;
    }
    *module = named->name;
    return length - 1;
}

// `x.f(a)` read as `f(x, a)`. The function is looked for where the file's own
// are and where `x`'s type was declared, and taken if the first thing it takes
// could be `x`; the language's own come last, so a module's `get` is the one a
// value of that module's type means. What is checked after that is the call it
// would have been written as. See D1256.
static KestType *check_method(Checker *checker, KestExpr *expr,
                              const KestType *expected) {
    if (!expr->call.method) {
        KestExpr *callee = expr->call.callee;
        KestExpr **moved = KEST_ARENA_ARRAY(checker->program->arena,
                                            KestExpr *,
                                            expr->call.arg_count + 1);
        KestExpr *named = KEST_ARENA_NEW(checker->program->arena, KestExpr);
        if (moved == NULL || named == NULL) {
            checker->out_of_memory = true;
            return error_type(checker);
        }
        moved[0] = callee->field.object;
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            moved[i + 1] = expr->call.args[i];
        }
        memset(named, 0, sizeof *named);
        named->kind = KEST_EXPR_NAME;
        named->span = callee->field.name;
        expr->call.callee = named;
        expr->call.args = moved;
        expr->call.arg_count++;
        expr->call.method = true;
    }
    KestType *object = check_expr(checker, expr->call.args[0], NULL);
    if (is_error(object)) {
        for (uint32_t i = 1; i < expr->call.arg_count; i++) {
            check_expr(checker, expr->call.args[i], NULL);
        }
        return error_type(checker);
    }
    KestSpan name = expr->call.callee->span;
    const char *text = span_text(checker, name);

    KestSymbol *candidates[16];
    uint32_t count = 0;
    const char *places[2] = {checker->program->module, NULL};
    size_t lengths[2] = {strlen(checker->program->module), 0};
    lengths[1] = module_of(object, &places[1]);
    for (int at = 0; at < 2; at++) {
        // A file that names no module has its own functions under nothing.
        if (places[at] == NULL || (at == 1 && lengths[1] == 0) ||
            (at == 1 && lengths[1] == lengths[0] &&
             strncmp(places[1], places[0], lengths[0]) == 0)) {
            continue;
        }
        char joined[256];
        int written = lengths[at] == 0
                          ? snprintf(joined, sizeof joined, "%.*s",
                                     (int)name.length, text)
                          : snprintf(joined, sizeof joined, "%.*s.%.*s",
                                     (int)lengths[at], places[at],
                                     (int)name.length, text);
        if (written <= 0 || (size_t)written >= sizeof joined) {
            continue;
        }
        KestSymbol *found[16];
        uint32_t there = kest_overloads(checker->program, joined,
                                        (size_t)written, found, 16);
        for (uint32_t i = 0; i < there && count < 16; i++) {
            const KestType *takes = found[i]->type;
            if (takes != NULL && takes->tag == KEST_T_FN &&
                takes->param_count == expr->call.arg_count &&
                could_take(object, takes->params[0])) {
                candidates[count++] = found[i];
            }
        }
        if (count > 0 && at == 1) {
            // Reached through the module the type came from, which the file
            // has to have asked for like any other name under it.
            if (!kest_import_by_path(checker->program, places[at],
                                     lengths[at])) {
                report(checker, name, "K0325",
                       "this file does not import `%.*s`", (int)lengths[at],
                       places[at]);
                suggest(checker, "`%.*s` is where `%s` takes a `%s`",
                        (int)lengths[at], places[at], joined,
                        type_name(checker, object));
                return error_type(checker);
            }
        }
        if (count > 0) {
            break;
        }
    }
    if (count > 1) {
        return check_overloaded(checker, expr, candidates, count);
    }
    if (count == 1) {
        candidates[0]->named = true;
        KestType *callee = candidates[0]->type;
        expr->call.callee->type = callee;
        if (callee->type_param_count > 0) {
            return check_generic(checker, expr, callee, expected);
        }
        return check_arguments(checker, expr, callee);
    }
    // The language's own, which take what they work on first too.
    bool handled = false;
    KestType *answered = check_builtin(checker, expr, expected, &handled);
    if (handled) {
        return answered;
    }
    report(checker, name, "K0307",
           "nothing called `%.*s` takes a `%s` first", (int)name.length, text,
           type_name(checker, object));
    // Where it would have been looked for, and whether that is a module this
    // file reads at all: one it did not import is one nothing was looked for
    // in.
    if (lengths[1] == 0) {
        suggest(checker, "`x.f(a)` is `f(x, a)` for a function this file "
                         "declares");
    } else if (!kest_import_by_path(checker->program, places[1],
                                    lengths[1])) {
        suggest(checker, "`x.f(a)` is `f(x, a)` for a function this file or "
                         "`%.*s` declares, and this file does not import "
                         "`%.*s`",
                (int)lengths[1], places[1], (int)lengths[1], places[1]);
    } else {
        suggest(checker, "`x.f(a)` is `f(x, a)` for a function this file or "
                         "`%.*s` declares",
                (int)lengths[1], places[1]);
    }
    for (uint32_t i = 1; i < expr->call.arg_count; i++) {
        check_expr(checker, expr->call.args[i], NULL);
    }
    return error_type(checker);
}

// What else this file calls by a name, said beside a refusal about the other
// one. The note is the same sentence a body that gives a name away is told
// (D730), because it is the same situation one step out: two things answer to
// one name and the message named the one the reader was not asking about.
static void note_the_other(Checker *checker, KestSpan where) {
    const char *name = span_text(checker, where);
    char joined[256];
    int written = checker->program->module[0] == '\0'
                      ? 0
                      : snprintf(joined, sizeof(joined), "%s.%.*s",
                                 checker->program->module, (int)where.length,
                                 name);
    const KestSymbol *other =
        kest_lookup_global(checker->program, name, where.length);
    if (other == NULL && written > 0 && (size_t)written < sizeof(joined)) {
        other = kest_lookup_global(checker->program, joined, (size_t)written);
    }
    if (other == NULL || other->type == NULL ||
        other->type->tag != KEST_T_FN) {
        return;
    }
    kest_diags_note(checker->program->diags, other->source, other->span,
                    "this file calls something else by that name");
}

static KestType *check_method(Checker *checker, KestExpr *expr,
                              const KestType *expected);
static bool is_method_call(Checker *checker, const KestExpr *expr);
static bool calls_a_field(Checker *checker, KestExpr *expr);

static KestType *check_call(Checker *checker, KestExpr *expr,
                            const KestType *expected) {
    if (expr->call.method ||
        (is_method_call(checker, expr) && !calls_a_field(checker, expr))) {
        return check_method(checker, expr, expected);
    }
    if (expr->call.callee->kind == KEST_EXPR_NAME) {
        bool handled = false;
        uint32_t said = checker->program->diags->count;
        KestType *result = check_builtin(checker, expr, expected, &handled);
        if (handled) {
            // And what else the file calls by that name, when the language's
            // own refused. A file may declare `len` or `get` — this tree does
            // it six times, in `table` and two examples — and what a
            // name of that kind means is settled by what it is handed: the
            // file's one for the shapes it takes, the language's for the rest.
            // So a call that fits neither is told what the language wanted and
            // left to notice the other, which is the one the reader is most
            // likely to have meant. See D731.
            if (checker->program->diags->count > said) {
                note_the_other(checker, expr->call.callee->span);
            }
            return result;
        }
    }

    // A case of an enum is built by naming it after its enum, which is one
    // name with a dot in it like everything else that has one — and an enum
    // another module declared is named with the module in front, which is two
    // dots and the same one name. See D1062.
    if (expr->call.callee->kind == KEST_EXPR_FIELD &&
        names_a_type(expr->call.callee->field.object)) {
        KestSpan owner = expr->call.callee->field.object->span;
        KestType *choice = kest_lookup_type(checker->program,
                                            span_text(checker, owner),
                                            owner.length);
        if (choice != NULL && choice->tag == KEST_T_ENUM) {
            report_unimported(checker, owner);
            if (choice->type_param_count > 0) {
                choice = copy_of_case(checker, expr, choice,
                                      expr->call.callee->field.name, expected);
                if (choice == NULL) {
                    return error_type(checker);
                }
            }
            return check_case(checker, expr, choice,
                              expr->call.callee->field.name);
        }
    }

    // A struct is built by naming it, and a struct from another module is
    // named with a dot, which is one name and not a field of anything.
    if (expr->call.callee->kind == KEST_EXPR_NAME ||
        (expr->call.callee->kind == KEST_EXPR_FIELD &&
         expr->call.callee->field.object->kind == KEST_EXPR_NAME)) {
        KestSpan name = expr->call.callee->span;
        KestType *type = kest_lookup_type(checker->program,
                                          span_text(checker, name), name.length);
        if (type != NULL && type->tag == KEST_T_STRUCT) {
            report_unimported(checker, name);
            // A shape is not a type. Which copy is meant comes from what it
            // is built with, the same way a generic call works.
            if (type->type_param_count > 0) {
                type = copy_wanted(checker, expr, type, expected);
                if (type == NULL) {
                    return error_type(checker);
                }
            }
            return check_construction(checker, expr, type);
        }
        // A set of bits with none of them set, or one made out of a number
        // the host handed over. `array()` and `store()` already read "an
        // empty one" from a name with nothing in the brackets (D030).
        if (type != NULL && type->tag == KEST_T_FLAGS) {
            expr->call.callee->type = type;
            if (expr->call.arg_count > 1) {
                check_arity(checker, expr, 1);
            }
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                KestType *from = check_expr(checker, expr->call.args[i], NULL);
                if (i == 0 && !is_error(from) &&
                    (from->tag != KEST_T_INT || from->is_signed ||
                     from->width != type->width)) {
                    report(checker, expr->call.args[i]->span, "K0327",
                           "`%s` is made from a `u%u`, found `%s`", type->name,
                           type->width, type_name(checker, from));
                    kest_diags_suggest(checker->program->diags,
                                       "`%s()` is the empty one", type->name);
                }
            }
            return type;
        }
        // Naming a number type makes one, the same way naming a struct does.
        // Nothing converts on its own, so every one of these is written down.
        if (type != NULL &&
            (type->tag == KEST_T_INT || type->tag == KEST_T_FLOAT)) {
            expr->call.callee->type = type;
            check_arity(checker, expr, 1);
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                // A literal takes the shape of where it is going, and where
                // this one is going is into this: `i64(9223372036854775807)`
                // is that number as an `i64` rather than an `i32` too small to
                // hold it, which is what it was read as and refused for. Only
                // where the two are the same kind of number, so `i32(3.7)` is
                // the question it always was.
                const KestExpr *literal = literal_of(expr->call.args[i]);
                bool same_kind =
                    literal != NULL &&
                    ((literal->kind == KEST_EXPR_INT &&
                      type->tag == KEST_T_INT &&
                      // One that does not fit is a narrowing, which is what
                      // `i8(300)` is written for and what D018 answers.
                      literal_fits(checker, literal, type)) ||
                     (literal->kind == KEST_EXPR_FLOAT &&
                      type->tag == KEST_T_FLOAT));
                KestType *from = check_expr(checker, expr->call.args[i],
                                            same_kind ? type : NULL);
                // A flag set is bits over an integer, so a number of that
                // width is what it already is. A narrower one would lose
                // flags silently, which is what nothing here does.
                bool bits = !is_error(from) && from->tag == KEST_T_FLAGS &&
                            type->tag == KEST_T_INT && !type->is_signed &&
                            type->width == from->width;
                if (i == 0 && !bits && !is_error(from)) {
                    if (from->tag == KEST_T_FLAGS) {
                        report(checker, expr->call.args[i]->span, "K0327",
                               "`%s` is %u bits, and `%s` is not",
                               type_name(checker, from), from->width,
                               type->name);
                    } else if (from->tag != KEST_T_INT &&
                               from->tag != KEST_T_FLOAT &&
                               from->tag != KEST_T_BOOL) {
                        report(checker, expr->call.args[i]->span, "K0327",
                               "there is no `%s` for `%s`", type->name,
                               type_name(checker, from));
                    }
                }
            }
            return type;
        }
        // Text is its bytes (D021), so a run of them is the one thing it can
        // be made from. That is what lets text be built a piece at a time:
        // the pieces go on an array and become text once.
        if (type != NULL && type->tag == KEST_T_TEXT &&
            expr->call.arg_count == 1) {
            expr->call.callee->type = type;
            KestType *from = check_expr(checker, expr->call.args[0], NULL);
            if (!is_error(from) &&
                (from->tag != KEST_T_ARRAY || from->element == NULL ||
                 from->element->tag != KEST_T_INT ||
                 from->element->width != 8 || from->element->is_signed)) {
                report(checker, expr->call.args[0]->span, "K0327",
                       "text is made from `[u8]`, found `%s`",
                       type_name(checker, from));
                kest_diags_suggest(checker->program->diags,
                                   "a string with a hole in it makes one "
                                   "from a value: `\"{x}\"`");
            }
            return type;
        }
        // A type that is not a number and not a struct is not something a
        // value turns into, and saying so beats reporting the name as unknown.
        if (type != NULL && type->tag != KEST_T_ERROR) {
            report(checker, expr->span, "K0327",
                   "there is no way to make a `%s` from a value",
                   type_name(checker, type));
            if (type->tag == KEST_T_TEXT) {
                kest_diags_suggest(checker->program->diags,
                                   "a string with a hole in it does that: "
                                   "`\"{x}\"`");
            }
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return error_type(checker);
        }
    }

    // Which function is meant is settled by what is passed, and only when
    // there is more than one to choose between.
    KestSymbol *candidates[16];
    uint32_t candidate_count = find_callable(checker, expr, candidates, 16);
    if (candidate_count > 1) {
        return check_overloaded(checker, expr, candidates, candidate_count);
    }
    // One of them is the one that was meant, whether it is a name, a name
    // under a module, or a host type and the function it belongs to. What
    // this records is the only reading of "something names this" that is not
    // a reader's: the checker has just resolved it.
    if (candidate_count == 1) {
        candidates[0]->named = true;
    }

    // `Clock.now()` is one name with a dot in it, not a field of a `Clock`.
    // An extern is declared against the host type it belongs to, so the
    // receiver is part of what it is called.
    KestType *callee = NULL;
    if (expr->call.callee->kind == KEST_EXPR_FIELD &&
        expr->call.callee->field.object->kind == KEST_EXPR_NAME) {
        KestSpan whole = expr->call.callee->span;
        // `world.spawn()` and `Clock.now()` are both one name with a dot in
        // it: a module qualifier and a host type read the same way.
        KestSymbol *host = kest_lookup_global(
            checker->program, span_text(checker, whole), whole.length);
        if (host != NULL && host->type->tag == KEST_T_FN) {
            report_unimported(checker, whole);
            expr->call.callee->type = host->type;
            callee = host->type;
        } else if (kest_out_of_reach(checker->program,
                                     span_text(checker, whole),
                                     whole.length)) {
            // A name this program holds under a module this file did not ask
            // for. Said before the halves are looked at on their own, because
            // `text.chars` with no import is a missing line and not the
            // builtin `text` named where a value goes. See D1039.
            report_unimported(checker, whole);
            return error_type(checker);
        }
    }
    if (callee == NULL) {
        bool was = checker->naming_callee;
        checker->naming_callee = true;
        callee = check_expr(checker, expr->call.callee, NULL);
        checker->naming_callee = was;
    }
    for (uint32_t i = 0; i < expr->call.arg_count; i++) {
        if (is_error(callee)) {
            check_expr(checker, expr->call.args[i], NULL);
        }
    }
    if (is_error(callee)) {
        return error_type(checker);
    }

    if (callee->tag != KEST_T_FN) {
        report(checker, expr->call.callee->span, "K0308",
               "`%s` is not a function", type_name(checker, callee));
        // And whether the file has a function of that name that this body has
        // given the name to something else. A local may take a name the file
        // uses — a body's names are its own — and the file's one is still
        // there, written with the module in front of it. Without this the
        // reader is told the type of the local and left to work out that the
        // function they wrote is the one three lines up. See D730.
        if (expr->call.callee->kind == KEST_EXPR_NAME) {
            KestSpan where = expr->call.callee->span;
            const char *name = span_text(checker, where);
            // Under the module the file names, where its own declarations
            // live, and under nothing for a file that names none.
            char joined[256];
            int written = checker->program->module[0] == '\0'
                              ? 0
                              : snprintf(joined, sizeof(joined), "%s.%.*s",
                                         checker->program->module,
                                         (int)where.length, name);
            const KestSymbol *shadowed =
                kest_lookup_global(checker->program, name, where.length);
            if (shadowed == NULL && written > 0 &&
                (size_t)written < sizeof(joined)) {
                shadowed = kest_lookup_global(checker->program, joined,
                                              (size_t)written);
            }
            if (shadowed != NULL && shadowed->type != NULL &&
                shadowed->type->tag == KEST_T_FN) {
                kest_diags_note(checker->program->diags, shadowed->source,
                                shadowed->span,
                                "this file calls something else by that name");
                // The way to write it here, which a file that names a module
                // has and one that does not has not: there is nothing to put
                // in front of a name that lives under nothing.
                if (written > 0 && (size_t)written < sizeof(joined)) {
                    suggest(checker, "write `%s` for that one", joined);
                } else {
                    suggest(checker, "give one of them a name of its own");
                }
            }
        }
        return error_type(checker);
    }

    if (callee->type_param_count > 0) {
        return check_generic(checker, expr, callee, expected);
    }
    return check_arguments(checker, expr, callee);
}

// Where the function being called was declared, so a message about how it is
// called can show what it takes. A name may be several functions, and the one
// to point at is the one whose type is being called.
static const KestSymbol *declared_at(Checker *checker, const KestType *callee) {
    if (callee->symbol == NULL) {
        return NULL;
    }
    // What a function is compiled under has what it takes written into it,
    // because two functions may share a name; what it is declared under is
    // the part before that.
    const char *marked = strchr(callee->symbol, '#');
    size_t length = marked == NULL ? strlen(callee->symbol)
                                   : (size_t)(marked - callee->symbol);
    KestSymbol *all[32];
    uint32_t count =
        kest_overloads(checker->program, callee->symbol, length, all, 32);
    for (uint32_t i = 0; i < count; i++) {
        if (all[i]->type == callee) {
            return all[i];
        }
    }
    return NULL;
}

static KestType *check_arguments(Checker *checker, KestExpr *expr,
                                 const KestType *callee) {
    // Everything checked from here down is being handed to somebody else, so
    // a name among it is a name this body can no longer say anything about --
    // an array with no room handed over may come back with room. Set here
    // rather than at each argument because every path below reaches one.
    // See D1135.
    bool was_handing = checker->handing_a_name_over;
    checker->handing_a_name_over = true;
    KestType *answered = arguments_checked(checker, expr, callee);
    checker->handing_a_name_over = was_handing;
    return answered;
}

static void check_block(Checker *checker, KestBlock *block);

// What is handed to a `block` parameter: one written here, `|x| x * 2`, or a
// block this body was handed, handed on. The block is checked where it is
// written, in the frame it will run in, with its names standing for what the
// function says it calls it with. See D1257.
static KestType *check_block_argument(Checker *checker, KestExpr *argument,
                                      const KestType *wanted) {
    if (argument->kind == KEST_EXPR_NAME) {
        bool was = checker->handing_a_block;
        checker->handing_a_block = true;
        KestType *given = check_expr(checker, argument, wanted);
        checker->handing_a_block = was;
        if (!is_error(given) && !(given->tag == KEST_T_FN && given->block)) {
            report(checker, argument->span, "K0367",
                   "a block is written where it is handed over, and this is "
                   "`%s`",
                   type_name(checker, given));
            suggest(checker, "write it here: `|x| ...`");
            return error_type(checker);
        }
        return given;
    }
    if (argument->kind != KEST_EXPR_BLOCK) {
        KestType *given = check_expr(checker, argument, NULL);
        if (!is_error(given)) {
            report(checker, argument->span, "K0367",
                   "a block is written where it is handed over, and this is "
                   "`%s`",
                   type_name(checker, given));
            suggest(checker, "write it here: `|x| ...`");
        }
        return error_type(checker);
    }
    const KestLambda *lambda = argument->lambda;
    if (lambda->param_count != wanted->param_count) {
        report(checker, argument->span, "K0367",
               "this block takes %u, and it is called with %u",
               lambda->param_count, wanted->param_count);
        return error_type(checker);
    }
    bool gives = wanted->result != NULL && wanted->result->tag != KEST_T_VOID;
    if (gives && lambda->value == NULL) {
        report(checker, argument->span, "K0367",
               "a block that gives `%s` gives it as `|x| value`",
               type_name(checker, wanted->result));
        return error_type(checker);
    }
    uint32_t mark = checker->local_count;
    checker->depth++;
    for (uint32_t i = 0; i < lambda->param_count; i++) {
        declare_local(checker, lambda->params[i], wanted->params[i]);
    }
    // A block runs inside whatever the function does with it, so a loop
    // around where it is written is not one it can leave, and `return` is
    // the function's, which the block is not.
    bool was_in = checker->in_a_block;
    uint32_t was_looping = checker->loop_depth;
    checker->in_a_block = true;
    checker->loop_depth = 0;
    if (lambda->value != NULL) {
        KestType *value =
            check_expr(checker, lambda->value, gives ? wanted->result : NULL);
        if (gives && !is_error(value) &&
            !kest_type_equal(value, wanted->result)) {
            expected_but(checker, lambda->value->span, wanted->result, value,
                         "this block");
        }
    } else {
        KestLambda *body = argument->lambda;
        check_block(checker, &body->body);
    }
    checker->in_a_block = was_in;
    checker->loop_depth = was_looping;
    checker->depth--;
    drop_locals(checker, mark);
    argument->type = (KestType *)wanted;
    return (KestType *)wanted;
}

// The declaration a function or a copy of one was written at.
static const KestDecl *written_at(Checker *checker, const KestType *callee) {
    KestProgram *program = checker->program;
    if (callee->symbol == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < program->instance_count; i++) {
        if (program->instances[i].symbol != NULL &&
            strcmp(program->instances[i].symbol, callee->symbol) == 0) {
            return program->instances[i].decl;
        }
    }
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestType *type = program->globals[i].type;
        if (type != NULL && type->symbol != NULL &&
            strcmp(type->symbol, callee->symbol) == 0) {
            return program->globals[i].decl;
        }
    }
    return NULL;
}

static KestType *arguments_checked(Checker *checker, KestExpr *expr,
                                   const KestType *callee) {
    // A function that takes a block is written into where it is called, so
    // one calling itself is one written into itself for ever. See D1257.
    if (kest_takes_a_block(callee) && checker->function != NULL &&
        written_at(checker, callee) == checker->function) {
        report(checker, expr->span, "K0367",
               "a function that takes a block is written into every place it "
               "is called, so it cannot call itself");
        suggest(checker, "walk what it works on with a loop instead");
    }
    // A call through a value has no name and nowhere it was declared: the
    // shape is all there is to say. Everything else is a function somebody
    // wrote, and the line they wrote it on says what it takes and what each
    // of them is called.
    const KestSymbol *declared = declared_at(checker, callee);
    const KestDecl *written = declared == NULL ? NULL : declared->decl;
    if (written != NULL && (written->kind != KEST_DECL_FN ||
                            written->function.param_count !=
                                callee->param_count)) {
        written = NULL;
    }

    if (expr->call.arg_count != callee->param_count) {
        if (declared == NULL) {
            report(checker, expr->span, "K0309",
                   "expected %u argument%s, found %u", callee->param_count,
                   callee->param_count == 1 ? "" : "s", expr->call.arg_count);
        } else {
            report(checker, expr->span, "K0309",
                   "`%s` takes %u argument%s, found %u", declared->name,
                   callee->param_count, callee->param_count == 1 ? "" : "s",
                   expr->call.arg_count);
            // A declaration the checker can read gives the names; anything
            // else gives the line it was written on and no more.
            if (written != NULL) {
                note_written(checker, expr, callee->param_count,
                             declared->source, declared->span, param_span,
                             written);
            } else {
                kest_diags_note(checker->program->diags, declared->source,
                                declared->span, "declared here");
            }
        }
    }

    uint32_t checked = expr->call.arg_count < callee->param_count
                           ? expr->call.arg_count
                           : callee->param_count;
    for (uint32_t i = 0; i < checked; i++) {
        KestType *argument =
            callee->params[i] != NULL && callee->params[i]->block
                ? check_block_argument(checker, expr->call.args[i],
                                       callee->params[i])
                : check_expr(checker, expr->call.args[i], callee->params[i]);
        if (is_error(argument) && callee->params[i] != NULL &&
            callee->params[i]->block) {
            continue;
        }
        if (!kest_type_equal(argument, callee->params[i])) {
            if (written == NULL) {
                expected_but(checker, expr->call.args[i]->span,
                             callee->params[i], argument, "this argument");
            } else {
                expected_for(checker, expr->call.args[i]->span,
                             callee->params[i], argument, declared->source,
                             param_span(written, i));
            }
        }
    }
    for (uint32_t i = checked; i < expr->call.arg_count; i++) {
        check_expr(checker, expr->call.args[i], NULL);
    }
    return callee->result;
}

// The function this name would be, if it is one: the language's own, one this
// file declared, or one under a module it imported. A field that is not a
// field and is one of those is somebody writing `p.len()`, and what to suggest
// is what they would have to write instead — `len(p)` for the first two and
// `text.upper(t)` for the third.
static const char *names_a_function(Checker *checker, KestSpan name) {
    const char *written = span_text(checker, name);
    for (uint32_t i = 0; i < sizeof(BUILTINS) / sizeof(BUILTINS[0]); i++) {
        if (kest_word_same(BUILTINS[i], written, name.length)) {
            return BUILTINS[i];
        }
    }
    KestSymbol *found =
        kest_lookup_global(checker->program, written, name.length);
    if (found != NULL && found->type != NULL &&
        found->type->tag == KEST_T_FN) {
        return found->name;
    }
    // And under whatever this file imported: `upper` is `text.upper`, and
    // what somebody has to write is the whole of that.
    for (uint32_t i = 0; i < checker->program->global_count; i++) {
        const KestSymbol *one = &checker->program->globals[i];
        if (one->type == NULL || one->type->tag != KEST_T_FN) {
            continue;
        }
        const char *dot = strrchr(one->name, '.');
        const char *last = dot == NULL ? one->name : dot + 1;
        if (kest_word_same(last, written, name.length)) {
            return one->name;
        }
    }
    return NULL;
}

// The nearest name under one module, given back the way it is written: under
// its module, because that is where it was being written.
static const char *nearest_under(Checker *checker, const char *module,
                                 size_t module_length, const char *name,
                                 size_t length) {
    if (length < 3) {
        return NULL;
    }
    uint32_t limit = length == 3 ? 1 : (uint32_t)length / 3;
    const char *best = NULL;
    uint32_t nearest_so_far = limit + 1;

    for (uint32_t i = 0; i < checker->program->global_count +
                                 checker->program->type_count; i++) {
        // The two lists in one walk, because what somebody meant may be
        // either: `shape.Point` is a type and `shape.zero` is not.
        const char *whole =
            i < checker->program->global_count
                ? checker->program->globals[i].name
                : checker->program->types[i - checker->program->global_count]
                      ->name;
        // Under the module the word means rather than under the word: a name
        // lives under the whole of its module. See D1039.
        const char *under = kest_module_for(checker->program, module,
                                            module_length);
        size_t under_length = under == NULL ? module_length : strlen(under);
        if (under == NULL) {
            under = module;
        }
        if (whole == NULL || !kest_under_module(whole, under, under_length)) {
            continue;
        }
        const char *member = whole + under_length + 1;
        uint32_t distance = kest_word_distance(name, length, member,
                                               strlen(member), limit);
        if (distance < nearest_so_far) {
            nearest_so_far = distance;
            // Written the way the reader writes it, which is the word they
            // put in front and not the whole module: a file that imported
            // `std.io` writes `io.print`. See D1039.
            size_t room = module_length + strlen(member) + 2;
            char *written = kest_arena_alloc(checker->program->arena, room, 1);
            if (written == NULL) {
                return whole;
            }
            snprintf(written, room, "%.*s.%s", (int)module_length, module,
                     member);
            best = written;
        }
    }
    return best;
}

static KestType *check_field(Checker *checker, KestExpr *expr,
                             const KestType *expected) {
    // `Clock.now` outside a call. An extern is a name the host answers when
    // it is called, and there is no value to hand around: which function the
    // host bound is settled when the program starts, not when it compiles.
    if (expr->field.object->kind == KEST_EXPR_NAME) {
        KestSymbol *host = kest_lookup_global(
            checker->program, span_text(checker, expr->span), expr->span.length);
        if (host != NULL && host->type->tag == KEST_T_FN &&
            host->type->is_foreign) {
            report(checker, expr->span, "K0342",
                   "`%.*s` is the host's, so it is called and not named",
                   (int)expr->span.length, span_text(checker, expr->span));
            kest_diags_suggest(checker->program->diags,
                               "write a function here that calls it");
            return error_type(checker);
        }
        // `box.CELLS`: a constant from another module. A `const` crosses out
        // of the file it is in — which is what the reference says it does and
        // what a program that imports one expects — and the lookup here had
        // only ever answered for functions, so the name was found, shown in a
        // note, offered as a suggestion spelled exactly as it was written, and
        // refused. See D665.
        if (host != NULL && host->is_const && host->type != NULL &&
            host->type->tag != KEST_T_FN) {
            report_unimported(checker, expr->span);
            host->named = true;
            return host->type;
        }
        // `sort.ascending` outside a call: a function from another module
        // named as a value, which is one name with a dot in it like every
        // other name from another module.
        if (host != NULL && host->type->tag == KEST_T_FN) {
            report_unimported(checker, expr->span);
            host->named = true;
            KestType *chosen = named_function(
                checker, span_text(checker, expr->span), expr->span.length,
                expected);
            if (chosen != NULL) {
                return chosen;
            }
            // One that takes types is the copy that fits where it is going,
            // the same as one named without a module in front of it.
            if (host->type->type_param_count > 0) {
                KestType *copy = copy_for_shape(checker, host->type, expected,
                                                expr->span);
                if (copy != NULL) {
                    return copy;
                }
                report(checker, expr->span, "K0362",
                       "`%.*s` takes a type, so it is called and not named",
                       (int)expr->span.length, span_text(checker, expr->span));
                kest_diags_suggest(checker->program->diags,
                                   "a copy exists per set of types it is "
                                   "called with, and a value would be one of "
                                   "them");
                return error_type(checker);
            }
            return host->type;
        }
    }

    // A case that carries nothing is written without brackets, so it looks
    // like a field of the enum and is the enum.
    if (names_a_type(expr->field.object)) {
        KestSpan owner = expr->field.object->span;
        KestType *choice = kest_lookup_type(checker->program,
                                            span_text(checker, owner),
                                            owner.length);
        // One named bit, which is a value of the set it was named in.
        if (choice != NULL && choice->tag == KEST_T_FLAGS) {
            report_unimported(checker, owner);
            expr->field.object->type = choice;
            if (find_case(checker, choice, expr->field.name) == NULL) {
                return error_type(checker);
            }
            return choice;
        }
        if (choice != NULL && choice->tag == KEST_T_ENUM) {
            report_unimported(checker, owner);
            // A case of a shape carrying nothing: which copy it is comes from
            // where the value is going, because the case itself says nothing
            // about the names. See D1048.
            if (choice->type_param_count > 0) {
                choice = copy_of_case(checker, NULL, choice, expr->field.name,
                                      expected);
                if (choice == NULL) {
                    return error_type(checker);
                }
            }
            expr->field.object->type = choice;
            const KestVariantType *variant =
                find_case(checker, choice, expr->field.name);
            if (variant == NULL) {
                return error_type(checker);
            }
            if (variant->payload_count > 0) {
                report(checker, expr->span, "K0309",
                       "`%s` carries %u thing%s and was named with none",
                       variant->name, variant->payload_count,
                       variant->payload_count == 1 ? "" : "s");
            }
            return choice;
        }
    }

    // `io.prnt(...)`: the module is there and the name under it is not. What
    // was said before this was that `io` was an unknown name, or — when a
    // module is spelt like a type, as `text` is — that a type had been named
    // where a value goes. Both blame the half that was written correctly.
    if (expr->field.object->kind == KEST_EXPR_NAME) {
        KestSpan owner = expr->field.object->span;
        const char *module = span_text(checker, owner);
        if (find_local(checker, module, owner.length) == NULL &&
            kest_lookup_global(checker->program, module, owner.length) == NULL &&
            kest_module_named(checker->program, module, owner.length)) {
            const char *member = span_text(checker, expr->field.name);
            report(checker, expr->field.name, "K0353",
                   "`%.*s` has nothing called `%.*s`", (int)owner.length,
                   module, (int)expr->field.name.length, member);
            const char *instead =
                kest_retired(checker->program, module, owner.length, member,
                             expr->field.name.length);
            const char *nearest =
                instead != NULL ? NULL
                                : nearest_under(checker, module, owner.length,
                                                member,
                                                expr->field.name.length);
            if (instead != NULL) {
                suggest(checker, "%s", instead);
            } else if (nearest != NULL) {
                suggest(checker, "did you mean `%s`?", nearest);
            }
            // And which `io` this is. A program read with a library that is
            // not the one it was written against asks for a name that is not
            // there, and the file it is not in is the whole of what a reader
            // needs to know.
            // The import this reached through was written and is written to,
            // so a file whose only use of it is the one that was spelt wrong
            // is not a file with an import nothing writes. See D735.
            kest_import_reached_by(checker->program, module, owner.length);
            const KestSymbol *read = kest_first_under(checker->program, module, owner.length);
            if (read != NULL && read->source != NULL) {
                kest_diags_note(checker->program->diags, read->source, read->span,
                                "this is the `%.*s` that was read",
                                (int)owner.length, module);
            }
            return error_type(checker);
        }
    }

    KestType *object = check_expr(checker, expr->field.object, NULL);
    if (is_error(object)) {
        return error_type(checker);
    }

    const char *name = span_text(checker, expr->field.name);
    size_t length = expr->field.name.length;

    if (object->tag == KEST_T_STRUCT) {
        for (uint32_t i = 0; i < object->member_count; i++) {
            if (kest_word_same(object->members[i].name, name, length)) {
                if (object->members[i].own && !reaches_own(checker, object)) {
                    refuse_own(checker, expr->field.name, object,
                               &object->members[i]);
                    return error_type(checker);
                }
                return object->members[i].type;
            }
        }
        report(checker, expr->field.name, "K0307", "`%s` has no field `%.*s`",
               type_name(checker, object), (int)length, name);
        const char *nearest = kest_nearest_member(object, name, length);
        // One this file cannot name is not one to suggest: a reader told to
        // write it would be refused for writing it.
        if (nearest != NULL && !reaches_own(checker, object)) {
            for (uint32_t i = 0; i < object->member_count; i++) {
                if (object->members[i].own &&
                    strcmp(object->members[i].name, nearest) == 0) {
                    nearest = NULL;
                    break;
                }
            }
        }
        if (nearest != NULL) {
            suggest(checker, "did you mean `%s`?", nearest);
        } else {
            // A function named where a field would be, without the brackets
            // that call it: `x.f()` is `f(x)` (D1256), and `x.f` is nothing.
            const char *elsewhere = names_a_function(checker, expr->field.name);
            if (elsewhere != NULL) {
                suggest(checker, "`%s` is a function, and one is called: "
                                 "`%s(...)`",
                        elsewhere, elsewhere);
            }
        }
        return error_type(checker);
    }

    report(checker, expr->field.name, "K0307", "`%s` has no fields",
           type_name(checker, object));
    // A walk over a store gives a reference, so this is what somebody writes
    // the first time they walk one: the field is on what the reference names
    // and not on the reference. Saying only that a reference has no fields is
    // true and leaves the reader where they were. See D504.
    if ((object->tag == KEST_T_REF || object->tag == KEST_T_OPTIONAL) &&
        object->element != NULL &&
        object->element->tag == KEST_T_STRUCT) {
        const KestType *named = object->element;
        for (uint32_t i = 0; i < named->member_count; i++) {
            if (kest_word_same(named->members[i].name, name, length)) {
                if (object->tag == KEST_T_OPTIONAL) {
                    say_if_let(checker, object, NULL);
                } else {
                    suggest(checker,
                            "read what it names with `get` and take `%.*s` "
                            "off that", (int)length, name);
                }
                return error_type(checker);
            }
        }
    }
    const char *elsewhere = names_a_function(checker, expr->field.name);
    if (elsewhere != NULL) {
        suggest(checker, "`%s` is a function, and one is called: `%s(...)`",
                elsewhere, elsewhere);
    }
    return error_type(checker);
}

// The elements decide the type, so the first one that resolves sets it and
// the rest are measured against it.
static KestType *check_array(Checker *checker, KestExpr *expr,
                             const KestType *expected) {
    // A literal is a handle to something that can grow, unless where it is
    // going says how many: `let m: [f32; 3] = [1.0, 2.0, 3.0]` lays it out
    // where it stands.
    bool fixed = expected != NULL && expected->tag == KEST_T_FIXED;
    const KestType *wanted =
        expected != NULL &&
                (expected->tag == KEST_T_ARRAY || expected->tag == KEST_T_FIXED)
            ? expected->element
            : NULL;
    if (fixed && expected->count != expr->array.count) {
        report(checker, expr->span, "K0320",
               "this holds %u and %u %s written",
               expected->count, expr->array.count,
               expr->array.count == 1 ? "is" : "are");
        fixed = false;
    }

    KestType *element = (KestType *)wanted;
    for (uint32_t i = 0; i < expr->array.count; i++) {
        uint32_t said = checker->program->diags->count;
        KestType *item = check_expr(checker, expr->array.items[i], element);
        // The one place a value that is not one went in without a word. Every
        // other place something is taken says so — an argument, an operand, a
        // field, a walk — and this held them, laid them out, and counted them.
        // See D518.
        if (item != NULL && item->tag == KEST_T_VOID &&
            checker->program->diags->count == said) {
            report(checker, word_of(expr->array.items[i]), "K0356",
                   "this gives nothing back, and an array holds values");
            item = error_type(checker);
        }
        if (element == NULL || element->tag == KEST_T_ERROR) {
            element = item;
        } else if (!kest_type_equal(item, element)) {
            expected_but(checker, expr->array.items[i]->span, element, item,
                         "this element");
        }
    }

    if (element == NULL) {
        report(checker, expr->span, "K0320",
               "an empty array has no element type here");
        kest_diags_suggest(checker->program->diags,
                           "write it down: `let a: [i32] = []`");
        return error_type(checker);
    }
    if (fixed) {
        return kest_fixed_of(checker->program, element, expr->array.count);
    }
    return kest_array_of(checker->program, element);
}


// True when the number is known here, whatever it is, so that a caller with
// something else to ask of it — how many there are, for a `[T; N]` — has it
// without working it out twice.
static bool written_place(Checker *checker, const KestExpr *expr, bool in_text,
                          int64_t *at) {
    int64_t held = 0;
    if (!written_number(checker, expr, &held)) {
        return false;
    }
    if (at != NULL) {
        *at = held;
    }
    if (held >= 0) {
        return true;
    }
    if (in_text) {
        report(checker, expr->span, "K0352",
               "text is read from nought, and %lld is before it",
               (long long)held);
    } else {
        report(checker, expr->span, "K0352",
               "an index is nought or more, and %lld is not", (long long)held);
    }
    return true;
}

static KestType *check_index(Checker *checker, KestExpr *expr) {
    KestType *object = check_expr(checker, expr->index.object, NULL);
    KestType *index = check_expr(checker, expr->index.index, NULL);

    if (!is_error(index) && index->tag != KEST_T_INT) {
        report(checker, expr->index.index->span, "K0315",
               "an index must be an integer, found `%s`",
               type_name(checker, index));
    }
    if (is_error(object)) {
        return error_type(checker);
    }
    // Where it is is read where it is written, whatever is being indexed.
    int64_t at = 0;
    bool known =
        written_place(checker, expr->index.index,
                      object->tag == KEST_T_TEXT, &at);

    // A piece of text is its bytes. There is no character type, so what comes
    // out is a `u8` and decoding is the program's business.
    if (object->tag == KEST_T_TEXT) {
        return builtin(checker, "u8");
    }
    if (object->tag != KEST_T_ARRAY && object->tag != KEST_T_FIXED) {
        report(checker, expr->index.object->span, "K0315",
               "`%s` cannot be indexed", type_name(checker, object));
        if (object->element != NULL &&
            (object->element->tag == KEST_T_ARRAY ||
             object->element->tag == KEST_T_FIXED ||
             object->element->tag == KEST_T_TEXT)) {
            say_if_let(checker, object, NULL);
        }
        return error_type(checker);
    }
    // An index written down is read where it is written. That it is below
    // nought is known wherever it is; how many there are is known for a
    // `[T; N]` and not for an array, which can be any length by then.
    // How many there are is written down for a `[T; N]` and not for an array,
    // which can be any length by the time this runs.
    if (object->tag == KEST_T_FIXED && known && at >= 0 &&
        (uint64_t)at >= object->count) {
        report(checker, expr->index.index->span, "K0315",
               "%lld is outside %u of them", (long long)at, object->count);
    }
    return object->element;
}

static bool is_bitwise(KestTokenKind op) {
    return op == KEST_TOK_AMP || op == KEST_TOK_PIPE || op == KEST_TOK_CARET;
}

static bool is_comparison(KestTokenKind op) {
    return op == KEST_TOK_LT || op == KEST_TOK_LTEQ || op == KEST_TOK_GT ||
           op == KEST_TOK_GTEQ;
}

// Numbers have an order and so does text, by its bytes, and only the
// comparisons use text's.
static bool applies(const KestProgram *program, const KestType *type,
                    KestTokenKind op) {
    if (type == NULL) {
        return false;
    }
    // And a type name that says it orders, which is the one thing a generic
    // can say about what it is handed. Only the comparisons: arithmetic on a
    // type name is a body written for numbers and declared for anything.
    // See D1043.
    if (type->tag == KEST_T_PARAM) {
        return is_comparison(op) &&
               kest_wants((KestProgram *)program, type, KEST_WANTS_ORDERS);
    }
    return is_numeric(type) ||
           (is_comparison(op) && type->tag == KEST_T_TEXT);
}

static KestType *check_binary(Checker *checker, KestExpr *expr,
                              const KestType *expected) {
    KestTokenKind op = expr->binary.op;
    char spelling[8];

    if (op == KEST_TOK_AMPAMP || op == KEST_TOK_PIPEPIPE) {
        KestType *boolean = builtin(checker, "bool");
        KestType *left = check_expr(checker, expr->binary.left, boolean);
        KestType *right = check_expr(checker, expr->binary.right, boolean);
        if (!kest_type_equal(left, boolean)) {
            expected_but(checker, expr->binary.left->span, boolean, left,
                         kest_token_bare(op, spelling, sizeof(spelling)));
        }
        if (!kest_type_equal(right, boolean)) {
            expected_but(checker, expr->binary.right->span, boolean, right,
                         kest_token_bare(op, spelling, sizeof(spelling)));
        }
        return boolean;
    }

    // A shift has a value and a count, not two operands: the count says how
    // far, the same way an index says how deep, and neither has to be the
    // type of the thing it is applied to.
    if (op == KEST_TOK_LTLT || op == KEST_TOK_GTGT) {
        KestType *value = check_expr(checker, expr->binary.left, inside(expected));
        KestType *by = check_expr(checker, expr->binary.right,
                                  builtin(checker, "i32"));
        if (!is_error(value) && value->tag != KEST_T_INT) {
            report(checker, expr->span, "K0314", "`%s` does not apply to `%s`",
                   kest_token_bare(op, spelling, sizeof(spelling)),
                   type_name(checker, value));
            return error_type(checker);
        }
        if (!is_error(by) && by->tag != KEST_T_INT) {
            report(checker, expr->binary.right->span, "K0336",
                   "a shift counts, and a count is an integer, found `%s`",
                   type_name(checker, by));
            return is_error(value) ? error_type(checker) : value;
        }
        return value;
    }

    bool logical = is_comparison(op) || op == KEST_TOK_EQEQ ||
                   op == KEST_TOK_BANGEQ;
    const KestType *hint = logical ? NULL : inside(expected);

    // `none` on the left takes its type from the other side, the way a number
    // literal does: `none != x` and `x != none` are one question written two
    // ways, and a literal with no type of its own is the one thing that has to
    // be worked out second. See D727.
    KestType *left = NULL;
    KestType *right = NULL;
    if (expr->binary.left->kind == KEST_EXPR_NONE &&
        expr->binary.right->kind != KEST_EXPR_NONE) {
        right = check_expr(checker, expr->binary.right, hint);
        left = check_expr(checker, expr->binary.left, right);
    } else {
        left = check_expr(checker, expr->binary.left, hint);
        right = check_expr(checker, expr->binary.right, left);
    }

    // A literal on the left takes its type from the other side, so `2.0 * dt`
    // reads the same as `dt * 2.0`. The node is corrected too: the compiler
    // reads the type from there to choose between an `f32` and an `f64`
    // instruction, and a type only the checker knows is one nobody applies.
    if (!kest_type_equal(left, right) && is_literal(expr->binary.left) &&
        !is_error(right) && left != NULL && left->tag == right->tag) {
        left = right;
        expr->binary.left->type = right;
        if (expr->binary.left->kind == KEST_EXPR_UNARY) {
            expr->binary.left->unary.operand->type = right;
        }
    }

    // A vector, which has four of these and no others. Asked before the rule
    // that both sides are one type, because a vector and the number it is
    // scaled by are two. See D1250.
    bool arithmetic = op == KEST_TOK_PLUS || op == KEST_TOK_MINUS ||
                      op == KEST_TOK_STAR || op == KEST_TOK_SLASH;
    if (arithmetic && (kest_is_vector(left) || kest_is_vector(right))) {
        if (kest_is_vector(left)) {
            as_component(checker, expr->binary.right, &right);
        } else {
            as_component(checker, expr->binary.left, &left);
        }
        KestType *answer = vector_answer(op, left, right);
        if (answer != NULL) {
            return answer;
        }
        if (is_error(left) || is_error(right)) {
            return error_type(checker);
        }
        report(checker, expr->span, "K0314",
               "`%s` does not apply to `%s` and `%s`",
               kest_token_bare(op, spelling, sizeof(spelling)),
               type_name(checker, left), type_name(checker, right));
        kest_diags_suggest(checker->program->diags,
                           "a vector takes `+`, `-`, `*` and `/` with one of "
                           "its own width, and `*` and `/` with an `f32`");
        return error_type(checker);
    }

    if (!kest_type_equal(left, right)) {
        report(checker, expr->span, "K0314",
               "`%s` needs both sides to have one type, found `%s` and `%s`",
               kest_token_bare(op, spelling, sizeof(spelling)), type_name(checker, left),
               type_name(checker, right));
        return logical ? builtin(checker, "bool") : error_type(checker);
    }

    // Something already broken compared with something else is broken too,
    // rather than a truth. One bad name is one message: a comparison that
    // answered `bool` here would hand a truth to whatever it is written
    // inside, and that would have its own opinion about it.
    if (is_error(left) || is_error(right)) {
        return error_type(checker);
    }

    if (op == KEST_TOK_EQEQ || op == KEST_TOK_BANGEQ) {
        // An optional against `none`, which is the one question an optional
        // answers without being taken apart: whether it holds anything. Two
        // optionals still do not compare — that is a question about what they
        // hold, and one of them may hold nothing — but this is a question
        // about the flag beside the value and nothing else. See D727.
        bool left_is_none = expr->binary.left->kind == KEST_EXPR_NONE;
        bool right_is_none = expr->binary.right->kind == KEST_EXPR_NONE;
        if (left->tag == KEST_T_OPTIONAL && left_is_none != right_is_none) {
            return builtin(checker, "bool");
        }
        // A value laid out flat compares when everything in it compares — a
        // struct is its fields, an enum is its case and what that case
        // carries, `[T; N]` is N of them in a row. A handle never does: two
        // arrays are equal when they hold the same things and comparing the
        // handles answers a different question. See D874.
        const KestType *without = NULL;
        if (!is_error(left) && !has_equality(checker->program, left, &without)) {
            report(checker, expr->span, "K0314",
                   "`%s` does not apply to `%s`",
                   kest_token_bare(op, spelling, sizeof(spelling)),
                   type_name(checker, left));
            if (without != NULL && without != left) {
                suggest(checker, "`%s` carries a `%s`, which does not compare",
                        type_name(checker, left), type_name(checker, without));
            } else if (left->tag == KEST_T_OPTIONAL) {
                // Two optionals are a question about what they hold, and one
                // of them may hold nothing. Against `none` is the other
                // question and it is answered above. See D727.
                kest_diags_suggest(checker->program->diags,
                                   "take what it holds out with `if let`, or "
                                   "ask whether it holds anything with "
                                   "`== none`");
            } else if (left->tag == KEST_T_REF) {
                kest_diags_suggest(checker->program->diags,
                                   "read what they name with `get` and compare "
                                   "that");
            } else if (left->tag == KEST_T_ARRAY || left->tag == KEST_T_FIXED ||
                       left->tag == KEST_T_STORE) {
                kest_diags_suggest(checker->program->diags,
                                   "walk them and compare what they hold");
            } else if (left->tag == KEST_T_FN) {
                kest_diags_suggest(checker->program->diags,
                                   "which body a name stands for is not a "
                                   "value; compare what they answer");
            } else {
                // What is left is a function that gives nothing back, a
                // module, and a type name inside a copy that was never made:
                // none of the three is a value with anything in it to compare.
                // A struct used to land here and does not since D874.
                kest_diags_suggest(checker->program->diags,
                                   "there is nothing in one of these that two "
                                   "of them could differ by");
            }
        }
        return builtin(checker, "bool");
    }

    // A set of bits is what `&`, `|` and `^` are for, and combining two of
    // one set gives that set rather than the number under it.
    if (is_bitwise(op) && !is_error(left) && left->tag == KEST_T_FLAGS) {
        return left;
    }
    // Bits are what an integer is made of and what nothing else is made of.
    // A `bool` has `&&` and `||`, which say what they mean about one bit.
    if (is_bitwise(op) && !is_error(left) && left->tag != KEST_T_INT) {
        report(checker, expr->span, "K0314", "`%s` does not apply to `%s`",
               kest_token_bare(op, spelling, sizeof(spelling)),
               type_name(checker, left));
        if (left != NULL && left->element != NULL &&
            left->element->tag == KEST_T_INT) {
            say_if_let(checker, left, NULL);
        } else if (left != NULL && left->tag == KEST_T_BOOL) {
            kest_diags_suggest(checker->program->diags,
                               op == KEST_TOK_AMP ? "`&&` is the one for "
                                                    "`bool`"
                                                  : "`||` is the one for "
                                                    "`bool`");
        }
        return error_type(checker);
    }
    // Text has an order, by its bytes, and only the comparisons use it.
    bool orderable = applies(checker->program, left, op);
    if (!is_error(left) && !orderable) {
        report(checker, expr->span, "K0314", "`%s` does not apply to `%s`",
               kest_token_bare(op, spelling, sizeof(spelling)),
               type_name(checker, left));
        // And what to write instead, which for an order is a different sort of
        // answer from the one `==` gets. Two of a struct are equal in exactly
        // one way and there is nothing for a program to decide (D874); which
        // of them comes first is a choice, and there are as many orders as
        // fields. So this does not offer a default — it says the choice is
        // there and what shape the answer takes. See D875.
        bool told = left != NULL && applies(checker->program, left->element, op) &&
                    say_if_let(checker, left, NULL);
        if (told) {
            // An optional said the one thing there is to say about one.
        } else if (left != NULL && left->tag == KEST_T_STRUCT) {
            suggest(checker,
                    "there are as many orders as fields, so write the one you "
                    "mean: `fn(%s, %s) -> bool`, handed to what sorts",
                    type_name(checker, left), type_name(checker, left));
        } else if (left != NULL && left->tag == KEST_T_ENUM) {
            kest_diags_suggest(checker->program->diags,
                               "a case is a name rather than a place in a "
                               "line; `match` on it, or carry the number you "
                               "mean");
        } else if (left != NULL &&
                   (left->tag == KEST_T_ARRAY || left->tag == KEST_T_FIXED ||
                    left->tag == KEST_T_STORE)) {
            kest_diags_suggest(checker->program->diags,
                               "walk them and compare what they hold");
        } else if (left != NULL && left->tag == KEST_T_BOOL) {
            kest_diags_suggest(checker->program->diags,
                               "one of two is not an order; `!a && b` is the "
                               "one somebody usually means");
        } else if (left != NULL && left->tag == KEST_T_TEXT &&
                   op == KEST_TOK_PLUS) {
            // Text and `+` is what everybody writes first. The reference says
            // there is no `+` on text and why — building a string reaches the
            // heap, so a `no.alloc` body may hold one and may not build one —
            // and the reader who wrote it is looking for what to write
            // instead. See D884.
            kest_diags_suggest(checker->program->diags,
                               "a hole joins them: `\"{a}{b}\"`, and "
                               "`text.join` joins a run of them");
        }
        return logical ? builtin(checker, "bool") : error_type(checker);
    }
    // `/` and `%` go together, which is the rule the whole numbers already
    // keep. A float keeps it too: `kest_left_over` answers for every pair C
    // has an answer for, and for the pair it has none for the answer is what
    // is not a number rather than a program that stops. See D776.
    if (op == KEST_TOK_PERCENT && !is_error(left) &&
        left->tag != KEST_T_INT && left->tag != KEST_T_FLOAT) {
        report(checker, expr->span, "K0314",
               "`%%` does not apply to `%s`", type_name(checker, left));
        if (left->element != NULL && left->element->tag == KEST_T_INT) {
            say_if_let(checker, left, NULL);
        }
        return error_type(checker);
    }

    return logical ? builtin(checker, "bool") : left;
}

// Whether a whole number written down fits the type it is being written as.
// Asked twice: to refuse one that does not, and to decide whether a
// conversion of one is a number in that type or a narrowing of a wider one.
static bool literal_fits(Checker *checker, const KestExpr *expr,
                         const KestType *type) {
    bool overflow = false;
    uint64_t value = kest_token_integer(span_text(checker, expr->span),
                                        expr->span.length, &overflow);
    if (overflow) {
        return false;
    }
    if (!type->is_signed) {
        if (checker->negating) {
            return false;
        }
        return type->width == 64 ||
               value <= ((uint64_t)1 << type->width) - 1;
    }
    uint64_t limit = (uint64_t)1 << (type->width - 1);
    return value <= (checker->negating ? limit : limit - 1);
}

// A literal is written in a type, and one that does not fit in it is a
// mistake the reader made rather than a value the machine should wrap.
//
// Whether it fits is `literal_fits` and nothing else. This worked the limits
// out again -- the width, the sign, the one further down than up -- and the two
// agreed by having been written the same way twice, which is what D766 took out
// of the defaults. What is left here is the words: which of the two things
// happened, and what to say about it. See D767.
static void check_literal_fits(Checker *checker, const KestExpr *expr,
                               const KestType *type) {
    if (type == NULL || type->tag != KEST_T_INT) {
        return;
    }
    if (literal_fits(checker, expr, type)) {
        return;
    }
    // A number below nought where there is no room below nought at all. What
    // is wrong is not the size of it, so it is not told a size.
    if (!type->is_signed && checker->negating) {
        report(checker, expr->span, "K0326", "`%s` holds no negative numbers",
               type_name(checker, type));
        return;
    }
    bool overflow = false;
    kest_token_integer(span_text(checker, expr->span), expr->span.length,
                       &overflow);
    {
        report(checker, expr->span, "K0326", "%s%.*s does not fit in `%s`",
               checker->negating ? "-" : "", (int)expr->span.length,
               span_text(checker, expr->span), type_name(checker, type));
        // The two questions that look like one: this says the number is one
        // of these, and a conversion says to make one of these out of it. The
        // second keeps what it has room for, which is a thing somebody may
        // mean and has to write.
        if (!overflow) {
            // No article in front of the type name: `a i8` and `an u8` are
            // both wrong, and which one a name wants is a question about how
            // it is said out loud.
            kest_diags_suggest(checker->program->diags,
                               "a number written down as `%s` has to fit in "
                               "one; `%s(n)` makes one out of any number, "
                               "keeping what it has room for",
                               type_name(checker, type),
                               type_name(checker, type));
        }
    }
}

static void check_block(Checker *checker, KestBlock *block);

// Whether every arm gives the same thing, which is what makes the match one
// thing rather than several.
static KestType *check_branch(Checker *checker, KestExpr *expr,
                              const KestType *expected);

// The number of combinations a `match` over several subjects has to answer.
// Beyond this it is asked for an `else` rather than for a list nobody would
// write out.
#define MAX_COMBINATIONS 256
// How many unanswered combinations a refusal names before it counts the rest.
#define NAMED_AT_MOST 8

// How many things one `match` chooses between at once. Written once: the run
// it fills and the message that says the number were two literals in two
// lines, and a check reading the message read the second of them.
#define MAX_SUBJECTS 8

static KestType *check_match(Checker *checker, KestExpr *expr,
                             const KestType *expected) {
    KestChoose *choose = expr->choose;
    KestType *subjects[MAX_SUBJECTS];
    uint32_t count = choose->subject_count;
    if (count > MAX_SUBJECTS) {
        report(checker, expr->span, "K0339",
               "a `match` chooses between at most %d things, found %u",
               MAX_SUBJECTS, count);
        count = MAX_SUBJECTS;
    }

    bool any_error = false;
    uint32_t combinations = 1;
    for (uint32_t i = 0; i < count; i++) {
        subjects[i] = check_expr(checker, choose->subjects[i], NULL);
        if (!is_error(subjects[i]) && subjects[i]->tag != KEST_T_ENUM) {
            report(checker, choose->subjects[i]->span, "K0331",
                   "`match` chooses between the cases of an enum, found `%s`",
                   type_name(checker, subjects[i]));
            // And what to write instead, which is knowable here: an optional
            // holds one thing or nothing and `if let` is how it comes out;
            // anything else has no list of cases to exhaust, which is what
            // `if` is for and what the reference says. Somebody who reaches
            // this reached it by guessing that `match` is a `switch`.
            if (!say_if_let(checker, subjects[i], NULL)) {
                suggest(checker, "there is no list of cases to exhaust here: "
                                 "`if` asks about a value");
            }
            subjects[i] = error_type(checker);
        }
        if (is_error(subjects[i])) {
            any_error = true;
        } else {
            combinations *= subjects[i]->case_count == 0
                                ? 1
                                : subjects[i]->case_count;
        }
    }
    for (uint32_t i = count; i < choose->subject_count; i++) {
        check_expr(checker, choose->subjects[i], NULL);
    }

    // What a refusal about the whole `match` points at: the word and what it
    // chooses between, not every line of every arm.
    KestSpan head = expr->span;
    if (choose->subject_count > 0) {
        const KestSpan last = choose->subjects[choose->subject_count - 1]->span;
        head.length = last.offset + last.length - head.offset;
    }

    bool countable = !any_error && combinations <= MAX_COMBINATIONS;
    bool seen[MAX_COMBINATIONS] = {false};
    bool has_else = false;
    KestType *given = NULL;

    // The same shape an `if` has, and the same one mistake: every arm written
    // as a block and every one of them ending in a value. Said once at the
    // word, before the arms are walked, so the arms themselves can be passed
    // over rather than each saying it again. See D516.
    if (!choose->gives && choose->arm_count > 0) {
        bool ends_in_one = true;
        for (uint32_t a = 0; a < choose->arm_count && ends_in_one; a++) {
            ends_in_one = choose->arms[a].value == NULL &&
                          tail_value(&choose->arms[a].body) != NULL;
        }
        if (ends_in_one) {
            report(checker, word_of(expr), "K0345",
                   "this `match` gives nothing, and every arm ends in a "
                   "value");
            kest_diags_suggest(checker->program->diags,
                               "an arm gives a value with `->`: "
                               "`Shut -> \"shut\"`");
            for (uint32_t a = 0; a < choose->arm_count; a++) {
                tail_value(&choose->arms[a].body)->passed_over = true;
            }
        }
    }

    for (uint32_t a = 0; a < choose->arm_count; a++) {
        KestArm *arm = &choose->arms[a];

        // One `else` on its own stands for every position, which is what an
        // `else` has always meant. Anything else answers a case per subject.
        bool blanket = arm->part_count == 1 && arm->parts[0].name.length == 0;
        if (!blanket && arm->part_count != count && !any_error) {
            report(checker, arm->span, "K0340",
                   "this `match` chooses between %u things, and this arm "
                   "answers %u",
                   count, arm->part_count);
            kest_diags_suggest(checker->program->diags,
                               "`else` in a position answers any case there");
            any_error = true;
            countable = false;
        }
        if (blanket) {
            if (has_else) {
                report(checker, arm->span, "K0332",
                       "this `match` has two `else` arms");
            }
            has_else = true;
        }

        // Which combinations this arm answers: each `else` position widens it
        // to every case there, so one arm may cover many.
        uint32_t covered[MAX_COMBINATIONS];
        uint32_t covered_count = 0;
        if (countable && !blanket) {
            covered[covered_count++] = 0;
        }

        uint32_t mark = checker->local_count;
        checker->depth++;
        uint32_t stride = combinations;
        for (uint32_t p = 0; p < arm->part_count && p < count; p++) {
            const KestArmPart *part = &arm->parts[p];
            const KestType *of = subjects[p];
            uint32_t cases = is_error(of) || of->case_count == 0
                                 ? 1
                                 : of->case_count;
            stride /= cases;

            const KestVariantType *variant = NULL;
            if (part->name.length > 0 && !is_error(of)) {
                variant = find_case(checker, of, part->name);
            }
            if (countable && !blanket) {
                uint32_t was = covered_count;
                for (uint32_t c = 0; c < cases; c++) {
                    if (part->name.length > 0 &&
                        (variant == NULL ||
                         c != (uint32_t)(variant - of->cases))) {
                        continue;
                    }
                    for (uint32_t k = 0; k < was; k++) {
                        if (covered_count < MAX_COMBINATIONS) {
                            covered[covered_count++] =
                                covered[k] + c * stride;
                        }
                    }
                }
                // The first `was` entries were the prefixes, now replaced.
                for (uint32_t k = 0; k + was < covered_count; k++) {
                    covered[k] = covered[k + was];
                }
                covered_count = covered_count > was ? covered_count - was : 0;
            }

            if (variant != NULL &&
                part->binding_count != variant->payload_count) {
                report(checker, part->name, "K0309",
                       "`%s` carries %u thing%s, and %u name%s given",
                       variant->name, variant->payload_count,
                       variant->payload_count == 1 ? "" : "s",
                       part->binding_count,
                       part->binding_count == 1 ? " was" : "s were");
            }
            for (uint32_t b = 0; b < part->binding_count; b++) {
                declare_local(checker, part->bindings[b],
                              variant != NULL && b < variant->payload_count
                                  ? variant->payload[b]
                                  : error_type(checker));
            }
        }

        // Arms are tried in order, so a later one catching what an earlier
        // one left is the point. What is refused is an arm that can never be
        // reached, which is every combination it answers already answered.
        if (blanket && countable) {
            for (uint32_t c = 0; c < combinations; c++) {
                seen[c] = true;
            }
        }
        bool reachable = covered_count == 0;
        for (uint32_t k = 0; k < covered_count; k++) {
            if (!seen[covered[k]]) {
                reachable = true;
            }
            seen[covered[k]] = true;
        }
        if (!reachable && countable && !blanket) {
            report(checker, arm->span, "K0332",
                   "this arm is already answered above");
            kest_diags_suggest(checker->program->diags,
                               "arms are tried in order, so nothing reaches "
                               "this one");
        }

        if (arm->value != NULL) {
            KestType *value = check_expr(checker, arm->value,
                                         given != NULL ? given : expected);
            // The first arm that has a type of its own settles what the match
            // is; a literal takes it, the way a literal always does.
            if (given == NULL ||
                (!is_literal(arm->value) && is_literal(
                     choose->arms[0].value) && given->tag == value->tag)) {
                given = value;
            }
        } else {
            check_block(checker, &arm->body);
        }
        checker->depth--;
        drop_locals(checker, mark);
    }

    // Every combination answered, or an `else` saying the rest are one answer.
    choose->total = has_else;
    if (!has_else && !any_error) {
        if (!countable) {
            report(checker, head, "K0333",
                   "this `match` has %u combinations to answer, which is "
                   "more than %u",
                   combinations, (uint32_t)MAX_COMBINATIONS);
            kest_diags_suggest(checker->program->diags,
                               "`else` answers the rest in one place");
        } else {
            // Every combination nothing answered, not the first of them: the
            // checker knows all of them before it says anything, and a reader
            // told one at a time compiles once per mistake. See D507.
            char missing[240];
            size_t used = 0;
            uint32_t unanswered = 0;
            uint32_t named = 0;
            for (uint32_t c = 0; c < combinations; c++) {
                if (seen[c]) {
                    continue;
                }
                unanswered++;
                // Which combination it was, named the way it is written.
                char one[128];
                size_t at = 0;
                uint32_t rest = c;
                uint32_t stride = combinations;
                for (uint32_t i = 0; i < count; i++) {
                    uint32_t cases = subjects[i]->case_count == 0
                                         ? 1
                                         : subjects[i]->case_count;
                    stride /= cases;
                    uint32_t which = stride == 0 ? 0 : rest / stride;
                    rest = stride == 0 ? 0 : rest % stride;
                    if (which >= subjects[i]->case_count) {
                        continue;
                    }
                    at += (size_t)snprintf(
                        one + at, sizeof(one) - at, "%s%s",
                        at > 0 ? ", " : "", subjects[i]->cases[which].name);
                    if (at >= sizeof(one)) {
                        break;
                    }
                }
                // A list long enough to stop reading is a list that has
                // stopped helping, so what is left over is counted instead.
                if (named < NAMED_AT_MOST &&
                    used + at + 8 < sizeof(missing)) {
                    used += (size_t)snprintf(missing + used,
                                             sizeof(missing) - used, "%s`%s`",
                                             used > 0 ? ", " : "", one);
                    named++;
                }
            }
            if (unanswered > 0 && named == unanswered) {
                report(checker, head, "K0333",
                       "this `match` does not answer %s", missing);
            } else if (unanswered > 0) {
                report(checker, head, "K0333",
                       "this `match` does not answer %s and %u more", missing,
                       unanswered - named);
            }
            choose->total = unanswered == 0;
        }
    }

    if (!choose->gives) {
        return builtin(checker, "void");
    }
    if (given == NULL) {
        return error_type(checker);
    }
    // Now that what it gives is settled, every arm has to give that.
    for (uint32_t a = 0; a < choose->arm_count; a++) {
        KestExpr *value = choose->arms[a].value;
        if (value == NULL) {
            continue;
        }
        if (is_literal(value) && value->type != NULL &&
            value->type->tag == given->tag) {
            value->type = given;
            continue;
        }
        if (!kest_type_equal(value->type, given)) {
            expected_but(checker, value->span, given, value->type, "this arm");
        }
    }
    return given;
}

// What is between the quotes, or -1 with a reason given. The escapes are the
// ones a string has, because a byte written in a string and a byte written on
// its own should not be two spellings.
static int64_t byte_of(Checker *checker, KestSpan span) {
    const char *raw = span_text(checker, span);
    uint32_t length = span.length;
    if (length < 3) {
        report(checker, span, "K0344", "a byte literal holds one byte");
        kest_diags_suggest(checker->program->diags,
                           "text is its bytes and there is no character type");
        return -1;
    }
    const char *inside = raw + 1;
    uint32_t held = length - 2;

    if (inside[0] == '\\') {
        // What the escape stands for is read the one way it is read anywhere,
        // rather than by a second list beside the lexer's: `\u{41}` is a
        // character that happens to be one byte, and a list of its own here
        // would be a list that did not know about it.
        KestSpan content = {span.offset + 1, held};
        size_t bytes = 0;
        const char *stands_for =
            kest_literal_text(checker->program->diags->arena,
                              checker->program->source, content, &bytes);
        if (bytes != 1) {
            report(checker, span, "K0344", "a byte literal holds one byte");
            kest_diags_suggest(checker->program->diags,
                               "text is its bytes and there is no character "
                               "type");
            return -1;
        }
        return (unsigned char)stands_for[0];
    }
    if (held != 1) {
        report(checker, span, "K0344",
               "a byte literal holds one byte, and this is %u", held);
        kest_diags_suggest(checker->program->diags,
                           "text is its bytes and there is no character type");
        return -1;
    }
    return (unsigned char)inside[0];
}

static KestType *check_expr_kind(Checker *checker, KestExpr *expr,
                                 const KestType *expected) {
    switch (expr->kind) {
    // A block is handed to what takes one, which checks it there; anywhere
    // else it is a value, which it is not. See D1257.
    case KEST_EXPR_BLOCK:
        report(checker, expr->span, "K0367",
               "a block is handed to a function that takes one, and is "
               "nothing anywhere else");
        suggest(checker, "a function that takes one says so: `f: "
                         "block(i32) -> i32`");
        return error_type(checker);
    case KEST_EXPR_INT: {
        KestType *type = expected != NULL && expected->tag == KEST_T_INT
                             ? (KestType *)expected
                             : literal_alone(checker, expr);
        check_literal_fits(checker, expr, type);
        return type;
    }

    case KEST_EXPR_FLOAT:
        if (expected != NULL && expected->tag == KEST_T_FLOAT) {
            return (KestType *)expected;
        }
        return literal_alone(checker, expr);

    case KEST_EXPR_STRING:
        return builtin(checker, "text");

    case KEST_EXPR_BYTE: {
        // One byte, and exactly one. Text is its bytes and there is no
        // character type, so `'a'` is a `u8` and `'ı'` is two of them and
        // therefore not one of these.
        int64_t value = byte_of(checker, expr->span);
        if (value < 0) {
            return error_type(checker);
        }
        return builtin(checker, "u8");
    }

    case KEST_EXPR_BOOL:
        return builtin(checker, "bool");

    case KEST_EXPR_NONE:
        if (expected == NULL || expected->tag != KEST_T_OPTIONAL) {
            report(checker, expr->span, "K0322",
                   "`none` has no type here");
            kest_diags_suggest(checker->program->diags,
                               "write what it is missing: `let x: i32? = none`");
            return error_type(checker);
        }
        return (KestType *)expected;

    case KEST_EXPR_NAME:
        return check_name(checker, expr, expected);

    case KEST_EXPR_UNARY: {
        if (expr->unary.op == KEST_TOK_BANG) {
            KestType *boolean = builtin(checker, "bool");
            KestType *operand = check_expr(checker, expr->unary.operand, boolean);
            if (!kest_type_equal(operand, boolean)) {
                expected_but(checker, expr->unary.operand->span, boolean,
                             operand, "`!`");
            }
            return boolean;
        }
        if (expr->unary.op == KEST_TOK_TILDE) {
            KestType *operand =
                check_expr(checker, expr->unary.operand, inside(expected));
            if (!is_error(operand) && operand->tag != KEST_T_INT &&
                operand->tag != KEST_T_FLAGS) {
                report(checker, expr->span, "K0314",
                       "`~` does not apply to `%s`",
                       type_name(checker, operand));
                if (operand != NULL && operand->tag == KEST_T_BOOL) {
                    kest_diags_suggest(checker->program->diags,
                                       "`!` is the one for `bool`");
                }
                return error_type(checker);
            }
            return operand;
        }
        bool was_negating = checker->negating;
        checker->negating = expr->unary.operand->kind == KEST_EXPR_INT;
        KestType *operand =
            check_expr(checker, expr->unary.operand, inside(expected));
        checker->negating = was_negating;
        // A vector is turned round a component at a time. See D1250.
        if (!is_error(operand) && !is_numeric(operand) &&
            !kest_is_vector(operand)) {
            report(checker, expr->span, "K0314", "`-` does not apply to `%s`",
                   type_name(checker, operand));
            return error_type(checker);
        }
        return operand;
    }

    case KEST_EXPR_BINARY:
        return check_binary(checker, expr, expected);

    case KEST_EXPR_CALL:
        return check_call(checker, expr, expected);

    case KEST_EXPR_FIELD:
        return check_field(checker, expr, expected);

    case KEST_EXPR_INDEX:
        return check_index(checker, expr);

    case KEST_EXPR_ARRAY:
        return check_array(checker, expr, expected);

    case KEST_EXPR_MATCH:
        return check_match(checker, expr, expected);

    case KEST_EXPR_IF:
        return check_branch(checker, expr, expected);

    case KEST_EXPR_TEXT: {
        for (uint32_t i = 0; i < expr->text.count; i++) {
            KestExpr *hole = expr->text.parts[i].value;
            if (hole == NULL) {
                continue;
            }
            KestType *type = check_expr(checker, hole, NULL);
            // Only what has one obvious spelling is written for you. A struct
            // has several and the author knows which one they meant.
            // What has one obvious spelling is written. A set of bits and
            // the cases of an enum both have one now, and it is the same one
            // every other value has: the source that builds them.
            const KestType *without = NULL;
            if (!is_error(type) && !kest_type_has_text(type, &without)) {
                report(checker, hole->span, "K0324",
                       "there is no text for `%s`", type_name(checker, type));
                if (without != NULL && without != type) {
                    suggest(checker, "`%s` carries a `%s`, which has none",
                            type_name(checker, type),
                            type_name(checker, without));
                } else {
                    kest_diags_suggest(checker->program->diags,
                                       "write the fields you want to see");
                }
            }
        }
        return builtin(checker, "text");
    }
    }
    return error_type(checker);
}

// Every expression is typed here and nowhere else, so the compiler can read
// `expr->type` for any node the checker walked.
static KestType *check_expr(Checker *checker, KestExpr *expr,
                            const KestType *expected) {
    if (expr == NULL) {
        return error_type(checker);
    }
    // Every copy of a generic is checked again, so what copies cost is
    // counted here too. See D1248.
    kest_diags_work(checker->program->diags, 1);
    KestType *type = check_expr_kind(checker, expr, expected);

    // A value standing where an optional is wanted becomes one. It is the
    // only conversion the language does, and it loses nothing.
    //
    // What it does not do is wrap one twice, and nothing here says so because
    // nothing has to: what is wanted is `T?` and what would be wrapped has to
    // be a `T`, so a value that is already `T?` is not one. A test for it was
    // written here and could not be made to fail — a condition that cannot be
    // false is a reader's second guess about what the rule is. See D414.
    if (expected != NULL && expected->tag == KEST_T_OPTIONAL && type != NULL &&
        type->tag != KEST_T_ERROR &&
        kest_type_equal(type, expected->element)) {
        expr->wrapped = true;
        type = (KestType *)expected;
    }

    expr->type = type;
    return type;
}

static void check_block(Checker *checker, KestBlock *block);

static bool is_constant_target(Checker *checker, KestExpr *target) {
    if (target->kind != KEST_EXPR_NAME) {
        return false;
    }
    const char *name = span_text(checker, target->span);
    if (find_local(checker, name, target->span.length) != NULL) {
        return false;
    }
    KestSymbol *global =
        kest_find_global(checker->program, name, target->span.length);
    return global != NULL && global->is_const;
}

static void check_condition(Checker *checker, KestExpr *condition,
                            const char *where) {
    KestType *boolean = builtin(checker, "bool");
    KestType *type = check_expr(checker, condition, boolean);
    if (!kest_type_equal(type, boolean)) {
        report(checker, condition->span, "K0312",
               "a %s condition must be `bool`, found `%s`", where,
               type_name(checker, type));
    }
}

// What to point at when a whole expression is wrong: the word it begins with,
// for the two that hold blocks and can be written over as many lines as
// somebody likes. An `if` written over six is six lines of caret, and what is
// wrong with it is not inside it. K0333 points at `match d` for the same
// reason, and K0212 at `flags`. See D516.
static KestSpan word_of(const KestExpr *expr) {
    KestSpan word = expr->span;
    if (expr->kind == KEST_EXPR_IF) {
        word.length = 2;
    } else if (expr->kind == KEST_EXPR_MATCH) {
        word.length = 5;
    }
    return word;
}

// The last statement of a block, when it is a bare value. That is what an arm
// looks like in a language whose blocks give values, and it is what a reader
// carrying one of those writes here. A call is not one: a block ending in
// `release(world)` is a block doing its work.
static KestStmt *tail_value(const KestBlock *block) {
    if (block->count == 0) {
        return NULL;
    }
    KestStmt *last = block->items[block->count - 1];
    if (last->kind != KEST_STMT_EXPR || last->value == NULL ||
        last->value->kind == KEST_EXPR_CALL) {
        return NULL;
    }
    return last;
}

// An `if` is checked the same whichever it is used as, because the arms say
// which it is. Giving arms have to agree on a type and there has to be an
// `else`, since a value has to exist on both ways through.
static KestType *check_branch(Checker *checker, KestExpr *expr,
                              const KestType *expected) {
    KestBranch *branch = expr->branch;
    KestType *held = NULL;

    if (branch->binding.length == 0) {
        check_condition(checker, branch->condition, "`if`");
    } else {
        KestType *optional = check_expr(checker, branch->condition, NULL);
        held = error_type(checker);
        if (!is_error(optional)) {
            if (optional->tag == KEST_T_OPTIONAL) {
                held = optional->element;
            } else {
                report(checker, branch->condition->span, "K0323",
                       "`if let` opens an optional, found `%s`",
                       type_name(checker, optional));
            }
        }
    }

    // `if c { 1 } else { 2 }`, which is the shape every language whose blocks
    // are expressions writes. Arms that are blocks give nothing here, so both
    // ends are a value nothing takes: one mistake, said once, where the `if`
    // is rather than twice inside it. See D516.
    if (branch->then_value == NULL && branch->has_else &&
        branch->else_value == NULL && branch->otherwise == NULL) {
        KestStmt *first = tail_value(&branch->then_body);
        KestStmt *second = tail_value(&branch->else_body);
        if (first != NULL && second != NULL) {
            report(checker, word_of(expr), "K0345",
                   "this `if` gives nothing, and both its arms end in a "
                   "value");
            kest_diags_suggest(checker->program->diags,
                               "an `if` gives a value with `->`: "
                               "`if c -> 1 else -> 2`");
            first->passed_over = true;
            second->passed_over = true;
        }
    }

    // The name exists only where the value did, which is what makes the
    // failure impossible to ignore rather than merely rude to.
    uint32_t mark = checker->local_count;
    checker->depth++;
    if (held != NULL) {
        declare_local(checker, branch->binding, held);
        // Asked about like a `let`, because an `if let` whose name nothing
        // reads is a program that meant `!= none` and had no way to write it
        // until D727. A `while let` is not asked: a loop that runs while there
        // is something has no other form, since a condition that only asks
        // takes nothing out and runs for ever. See D728.
        if (checker->local_count > 0) {
            checker->locals[checker->local_count - 1].from_let = true;
            checker->locals[checker->local_count - 1].from_if_let = true;
        }
    }
    KestType *given = NULL;
    if (branch->then_value != NULL) {
        given = check_expr(checker, branch->then_value, expected);
    } else {
        check_block(checker, &branch->then_body);
    }
    checker->depth--;
    drop_locals(checker, mark);

    KestType *other = NULL;
    if (branch->otherwise != NULL) {
        other = check_expr(checker, branch->otherwise,
                           given != NULL ? given : expected);
    } else if (branch->has_else) {
        if (branch->else_value != NULL) {
            other = check_expr(checker, branch->else_value,
                               given != NULL ? given : expected);
        } else {
            checker->depth++;
            check_block(checker, &branch->else_body);
            checker->depth--;
            drop_locals(checker, mark);
        }
    }

    if (!branch->gives) {
        return builtin(checker, "void");
    }
    if (!branch->has_else) {
        report(checker, expr->span, "K0334",
               "an `if` that gives a value needs an `else`");
        kest_diags_suggest(checker->program->diags,
                           "there has to be a value on both ways through");
        return given != NULL ? given : error_type(checker);
    }
    if (given == NULL) {
        return error_type(checker);
    }
    // A literal in one arm takes the shape the other arm settled on, which is
    // what makes `if c -> 1 else -> x` work when `x` is an `f32`.
    if (is_literal(branch->then_value) && other != NULL &&
        !is_error(other) && other->tag == given->tag) {
        given = other;
        branch->then_value->type = given;
    }
    KestExpr *second = branch->otherwise != NULL ? branch->otherwise
                                                 : branch->else_value;
    if (second != NULL) {
        if (is_literal(second) && second->type != NULL &&
            second->type->tag == given->tag) {
            second->type = given;
        } else if (!kest_type_equal(second->type, given)) {
            expected_but(checker, second->span, given, second->type,
                         "this arm");
        }
    }
    return given;
}

// `wait Walking`: where the next call carries on from, which is a case of the
// enum the body resumes from, one that carries nothing and is waited at once.
// What is kept across it is the parameter and nothing else, so nothing else
// may be in reach of it. See D1263.
static void check_wait(Checker *checker, KestStmt *stmt) {
    const KestType *cases = checker->resume_cases;
    if (checker->in_a_block) {
        report(checker, stmt->span, "K0368",
               "a block cannot wait, because the body it is handed to is not "
               "the one that resumes");
        return;
    }
    if (cases == NULL) {
        report(checker, stmt->span, "K0368",
               "`wait` is written in a body that resumes");
        kest_diags_suggest(checker->program->diags,
                           "say what it resumes from after what it gives back: "
                           "`fn step(c: Chore) -> Chore resumes c.at`");
        return;
    }
    const char *written = span_text(checker, stmt->wait.name);
    uint32_t tag = cases->case_count;
    for (uint32_t i = 0; i < cases->case_count; i++) {
        if (kest_word_same(cases->cases[i].name, written,
                           stmt->wait.name.length)) {
            tag = i;
        }
    }
    const char *enum_name = type_name(checker, cases);
    if (tag == cases->case_count) {
        report(checker, stmt->wait.name, "K0368",
               "`%.*s` is not a case of `%s`", (int)stmt->wait.name.length,
               written, enum_name);
        return;
    }
    cases->cases[tag].named = true;
    if (cases->cases[tag].payload_count > 0) {
        report(checker, stmt->wait.name, "K0368",
               "`%.*s` carries something, and a body waits at a case that "
               "carries nothing",
               (int)stmt->wait.name.length, written);
        return;
    }
    if (checker->waited != NULL && checker->waited[tag]++ > 0) {
        report(checker, stmt->wait.name, "K0368",
               "`%.*s` is waited at twice, and the next call would have two "
               "places to carry on from",
               (int)stmt->wait.name.length, written);
        kest_diags_suggest(checker->program->diags,
                           "give each `wait` a case of its own");
        return;
    }
    if (checker->scratch_depth > 0) {
        report(checker, stmt->span, "K0368",
               "a `scratch` block is open at this `wait`, and it would give "
               "back what the next call reads");
        return;
    }
    if (checker->local_count > checker->resume_mark) {
        const Local *reached = &checker->locals[checker->resume_mark];
        report(checker, stmt->span, "K0368",
               "`%.*s` is in reach of this `wait`, and nothing is kept across "
               "one but what the body resumes from",
               (int)reached->span.length, span_text(checker, reached->span));
        kest_diags_suggest(checker->program->diags,
                           "keep it in a field of what the body resumes from");
        return;
    }
    stmt->wait.tag = tag;
    stmt->wait.ordinal = ++checker->waits;
}

// `for ... in fields(x)`, which is the language's walk over a struct's fields
// unless the file can reach a `fields` of its own. See D1264.
static bool is_fields_walk(Checker *checker, const KestStmt *stmt) {
    const KestExpr *sequence = stmt->each->sequence;
    if (stmt->each->until != NULL || sequence == NULL ||
        sequence->kind != KEST_EXPR_CALL || sequence->call.arg_count != 1 ||
        sequence->call.method || sequence->call.callee == NULL ||
        sequence->call.callee->kind != KEST_EXPR_NAME ||
        !kest_word_same("fields", span_text(checker, sequence->call.callee->span),
                        sequence->call.callee->span.length)) {
        return false;
    }
    return find_local(checker, "fields", 6) == NULL &&
           kest_lookup_global(checker->program, "fields", 6) == NULL;
}

// The name a walk over fields reaches its struct through: a name, or a field
// of something that is one. Writing a field is writing that name's struct, so
// it has to be somewhere a write can go.
static bool names_a_place(const KestExpr *expr) {
    while (expr != NULL && expr->kind == KEST_EXPR_FIELD) {
        expr = expr->field.object;
    }
    return expr != NULL && expr->kind == KEST_EXPR_NAME;
}

// The walk written out: the body read again from the source once a field,
// each copy checked with `value` standing for that field and `name` for its
// name, and the copies put where the body was. See D1264.
static void check_fields_walk(Checker *checker, KestStmt *stmt) {
    KestEach *each = stmt->each;
    KestExpr *walked = each->sequence->call.args[0];
    KestType *held = check_expr(checker, walked, NULL);
    each->fields = true;
    each->sequence->type = held;
    if (is_error(held)) {
        each->body.count = 0;
        return;
    }
    if (held->tag != KEST_T_STRUCT) {
        report(checker, walked->span, "K0369",
               "`fields` walks a struct, and this is `%s`",
               type_name(checker, held));
        each->body.count = 0;
        return;
    }
    if (!names_a_place(walked)) {
        report(checker, walked->span, "K0369",
               "`fields` walks a struct a name holds, because a field written "
               "in the walk is written there");
        kest_diags_suggest(checker->program->diags,
                           "give it a name first: `let held = ...`");
        each->body.count = 0;
        return;
    }
    // What a field written in the walk writes into is the name's own, so the
    // name is one the body writes into and not a value the chunk can hold.
    const KestExpr *root = walked;
    while (root->kind == KEST_EXPR_FIELD) {
        root = root->field.object;
    }
    Local *rooted = find_local(checker, span_text(checker, root->span),
                               root->span.length);
    if (rooted != NULL) {
        rooted->written_into = true;
        rooted->read = true;
    }
    // Where the body is: from the first brace after what is walked to the end
    // of the statement, which is a block and nothing else.
    const char *text = span_text(checker, stmt->span);
    uint32_t from = each->sequence->span.offset + each->sequence->span.length -
                    stmt->span.offset;
    while (from < stmt->span.length && text[from] != '{') {
        from++;
    }
    KestSpan written = {stmt->span.offset + from, stmt->span.length - from};
    KestStmt **copies =
        held->member_count == 0
            ? NULL
            : KEST_ARENA_ARRAY(checker->program->arena, KestStmt *,
                               held->member_count);
    if (held->member_count > 0 && copies == NULL) {
        checker->out_of_memory = true;
        return;
    }
    for (uint32_t m = 0; m < held->member_count; m++) {
        KestStmt *copy = KEST_ARENA_NEW(checker->program->arena, KestStmt);
        if (copy == NULL ||
            !kest_parse_block_again(checker->program->arena,
                                    checker->program->source,
                                    checker->program->diags, written,
                                    &copy->block)) {
            checker->out_of_memory = true;
            return;
        }
        copy->kind = KEST_STMT_BLOCK;
        copy->span = written;
        copies[m] = copy;
        uint32_t mark = checker->local_count;
        checker->depth++;
        if (each->index.length > 0) {
            declare_local(checker, each->index, builtin(checker, "text"));
        }
        declare_local(checker, each->name, held->members[m].type);
        uint32_t said = checker->program->diags->count;
        checker->loop_depth++;
        check_block(checker, &copy->block);
        checker->loop_depth--;
        // Said about the copy, which the body alone does not say: the same
        // line reads the same for every field, and which field it was is what
        // tells them apart.
        for (uint32_t d = said; d < checker->program->diags->count; d++) {
            kest_diags_note_at(checker->program->diags, d, NULL, each->name,
                               "in the walk's copy for `%s`, which is `%s`",
                               held->members[m].name,
                               type_name(checker, held->members[m].type));
        }
        checker->depth--;
        drop_locals(checker, mark);
    }
    each->body.items = copies;
    each->body.count = held->member_count;
    each->name_written = true;
    each->index_written = true;
}

static void check_stmt(Checker *checker, KestStmt *stmt) {
    kest_diags_work(checker->program->diags, 1);
    switch (stmt->kind) {
    case KEST_STMT_LET: {
        KestType *declared = NULL;
        if (stmt->let.type != NULL) {
            declared = kest_resolve_type_ref(checker->program, stmt->let.type);
        }
        uint32_t said = checker->program->diags->count;
        KestType *value = check_expr(checker, stmt->let.value, declared);
        if (declared != NULL && !kest_type_equal(value, declared)) {
            expected_but(checker, stmt->let.value->span, declared, value,
                         "this binding");
        }
        // A name that holds nothing is a name that cannot be read, and every
        // reading of it was refused somewhere else with a message about
        // `void`. It is refused where it is written now — but only where
        // nothing has been said already: a type written on the binding says it
        // better, and an `if` whose arms are blocks has said it itself. See
        // D517.
        if (declared == NULL && checker->program->diags->count == said &&
            value != NULL && value->tag == KEST_T_VOID) {
            report(checker, word_of(stmt->let.value), "K0356",
                   "this gives nothing back, and a `let` names a value");
            kest_diags_suggest(checker->program->diags,
                               "call it on its own if what was wanted is what "
                               "it does");
        }
        // A name bound to nothing is named all the same, so the reader is not
        // also told the name does not exist — but what it holds is the error
        // type, so every reading of it is quiet. One mistake, one message.
        if (declared == NULL && value != NULL && value->tag == KEST_T_VOID) {
            value = error_type(checker);
        }
        declare_local(checker, stmt->let.name,
                      declared != NULL ? declared : value);
        if (checker->local_count > 0) {
            checker->locals[checker->local_count - 1].from_let = true;
            checker->locals[checker->local_count - 1].declared_by = stmt;
            checker->locals[checker->local_count - 1].made_with_no_room =
                made_with_no_room(checker, stmt->let.value);
        }
        break;
    }

    case KEST_STMT_ASSIGN: {
        char spelling[8];
        bool was_writing = checker->writing_to_a_name;
        checker->writing_to_a_name =
            stmt->assign.target->kind == KEST_EXPR_NAME;
        KestType *target = check_expr(checker, stmt->assign.target, NULL);
        checker->writing_to_a_name = was_writing;
        // Written down where the name is known, because a walk that is about
        // to bind its count to a name needs to know before it does it, and
        // only the checker ever resolved the name. See D866.
        //
        // The name itself and the name something inside it was reached
        // through are two answers to two questions, so both are kept: a walk
        // asks whether the count was written and a `let` asks whether
        // anything went into the value at all. See D887.
        const KestExpr *assigned_to = stmt->assign.target;
        while (assigned_to != NULL && assigned_to->kind != KEST_EXPR_NAME) {
            assigned_to = assigned_to->kind == KEST_EXPR_FIELD
                              ? assigned_to->field.object
                          : assigned_to->kind == KEST_EXPR_INDEX
                              ? assigned_to->index.object
                              : NULL;
        }
        if (assigned_to != NULL) {
            Local *assigned =
                find_local(checker, span_text(checker, assigned_to->span),
                           assigned_to->span.length);
            if (assigned != NULL) {
                assigned->written_into = true;
                if (stmt->assign.target->kind == KEST_EXPR_NAME) {
                    assigned->written = true;
                }
            }
        }
        KestType *value = check_expr(checker, stmt->assign.value, target);
        const KestExpr *root = NULL;
        bool is_index = false;
        const KestExpr *handed = NULL;
        if (writes_a_handed_copy(checker, stmt->assign.target, &handed)) {
            kest_diags_add(checker->program->diags, KEST_SEVERITY_WARNING,
                           "K0346", stmt->assign.target->span,
                           "`%.*s` is a value here, so this is discarded",
                           (int)handed->span.length,
                           span_text(checker, handed->span));
            // What not to say here: *hold what changes behind a handle*.
            // That was the suggestion and it taught the thing this project
            // was asked about from outside -- a struct is a value and a
            // handle inside one is not, so moving the field behind a handle
            // turns a write the caller cannot see into a write the caller
            // cannot see coming. Giving the changed one back is the answer,
            // and a caller that wants the change is a caller that takes it.
            // See D1038.
            kest_diags_suggest(checker->program->diags,
                               "answer with the changed one and let the "
                               "caller take it: `fn f(one: T) -> T`");
        }
        if (writes_only_a_copy(checker, stmt->assign.target, &root,
                               &is_index)) {
            kest_diags_add(checker->program->diags, KEST_SEVERITY_WARNING,
                           "K0321", stmt->assign.target->span,
                           "`%.*s` is the loop's own, so this is discarded",
                           (int)root->span.length,
                           span_text(checker, root->span));
            // What to do instead depends on what is being walked, and the
            // three answers are different enough that one of them would be
            // wrong for the other two.
            const KestType *walked = target;
            kest_diags_suggest(
                checker->program->diags,
                is_index ? "the walk keeps its own count, which this is a "
                           "copy of"
                : walked != NULL && walked->tag == KEST_T_FLAGS
                    ? "the walk gives one bit at a time; build the set you "
                      "want"
                    : "index the array to write to it: `a[i]` names the "
                      "element");
        }
        if (is_constant_target(checker, stmt->assign.target)) {
            report(checker, stmt->assign.target->span, "K0311",
                   "`%.*s` is a constant",
                   (int)stmt->assign.target->span.length,
                   span_text(checker, stmt->assign.target->span));
        }
        // A name bound to a function is that function, and the cost proof
        // follows it there. A variable that holds any function of a shape is
        // a different thing and is written as one: the shape is on the `let`.
        // Without this a promise could be proved through the function a name
        // was bound to and broken by the one assigned to it later.
        if (target != NULL && target->tag == KEST_T_FN &&
            target->symbol != NULL &&
            stmt->assign.target->kind == KEST_EXPR_NAME) {
            report(checker, stmt->assign.target->span, "K0350",
                   "`%.*s` names one function, and a name for one is not a "
                   "variable that holds any",
                   (int)stmt->assign.target->span.length,
                   span_text(checker, stmt->assign.target->span));
            suggest(checker,
                    "write the shape on the `let`: `let %.*s: %s = ...`",
                    (int)stmt->assign.target->span.length,
                    span_text(checker, stmt->assign.target->span),
                    type_name(checker, target));
        }
        // `+=` and its like on a vector, which are what the operators are:
        // `*=` and `/=` scale it by an `f32`. See D1250.
        bool vector_step = stmt->assign.op != KEST_TOK_EQ && kest_is_vector(target);
        if (vector_step) {
            as_component(checker, stmt->assign.value, &value);
        }
        if (vector_step && vector_answer(stmt->assign.op, target, value) ==
                               target) {
            // One of the four, with what it may take.
        } else if (!kest_type_equal(target, value)) {
            expected_but(checker, stmt->assign.value->span, target, value,
                         "this assignment");
        }
        if (stmt->assign.op != KEST_TOK_EQ && !is_error(target) &&
            !is_numeric(target) && !kest_is_vector(target)) {
            report(checker, stmt->span, "K0314", "`%s` does not apply to `%s`",
                   kest_token_bare(stmt->assign.op, spelling, sizeof(spelling)), type_name(checker, target));
        }
        break;
    }

    case KEST_STMT_EXPR:
    case KEST_STMT_DEFER: {
        // A body that resumes leaves at every `wait`, and what a `defer`
        // would run there would run again on every call. See D1263.
        if (stmt->kind == KEST_STMT_DEFER && checker->resume_cases != NULL) {
            report(checker, stmt->span, "K0368",
                   "a body that resumes runs no `defer`: it leaves at every "
                   "`wait` and comes back to where it left");
        }
        // A statement that is only an expression has to do something. A call
        // does — what it gives back may be worth ignoring — and an `if` or a
        // `match` whose arms are blocks does. Anything else works a value out
        // and leaves it lying there, which is `a == b` written where `a = b`
        // was meant, and every other slip of that shape.
        const KestExpr *value = stmt->value;
        bool does_something =
            value == NULL || value->kind == KEST_EXPR_CALL ||
            (value->kind == KEST_EXPR_MATCH && !value->choose->gives) ||
            (value->kind == KEST_EXPR_IF && value->branch != NULL &&
             !value->branch->gives);
        // One that gives a value, written where a statement belongs, is a
        // `return` with the word left off — which is what the function is
        // told. So it is measured against what the function gives back rather
        // than against nothing: `none` has a type there, and a reader who
        // forgot the word is not also told that the value they meant to give
        // back is not a value at all.
        KestType *made = check_expr(
            checker, stmt->value,
            !does_something && stmt->kind == KEST_STMT_EXPR ? checker->result
                                                            : NULL);
        if (!does_something && !is_error(made) &&
            (made == NULL || made->tag != KEST_T_VOID) && !stmt->passed_over) {
            report(checker, stmt->span, "K0345",
                   "this works out a value and nothing takes it");
            kest_diags_suggest(checker->program->diags,
                               "give it a name with `let`, return it, or "
                               "write the call that does something");
        }
        break;
    }

    case KEST_STMT_WHILE: {
        uint32_t mark = checker->local_count;
        if (stmt->loop.binding.length == 0) {
            check_condition(checker, stmt->loop.condition, "`while`");
        } else {
            // The same shape `if let` has: what it holds is named for as long
            // as there was something to name.
            KestType *optional = check_expr(checker, stmt->loop.condition, NULL);
            KestType *held = error_type(checker);
            if (!is_error(optional)) {
                if (optional->tag == KEST_T_OPTIONAL) {
                    held = optional->element;
                } else {
                    report(checker, stmt->loop.condition->span, "K0323",
                           "`while let` opens an optional, found `%s`",
                           type_name(checker, optional));
                }
            }
            checker->depth++;
            declare_local(checker, stmt->loop.binding, held);
        }
        checker->loop_depth++;
        check_block(checker, &stmt->loop.body);
        checker->loop_depth--;
        if (stmt->loop.binding.length > 0) {
            checker->depth--;
            drop_locals(checker, mark);
        }
        break;
    }

    case KEST_STMT_FOR: {
        if (is_fields_walk(checker, stmt)) {
            check_fields_walk(checker, stmt);
            break;
        }
        // `for i in from..to` counts rather than walks. Both ends are one
        // type, and the name is that type, so a walk of an array's positions
        // reads the same as an index into it.
        if (stmt->each->until != NULL) {
            KestType *from = check_expr(checker, stmt->each->sequence, NULL);
            KestType *to = check_expr(checker, stmt->each->until, from);
            // A literal at one end takes the type of the other, so
            // `0..count` counts in whatever `count` is. It is the rule an
            // operator already follows, and the node is corrected so the
            // compiler reads the same type the checker settled on.
            if (!kest_type_equal(from, to) && is_literal(stmt->each->sequence) &&
                !is_error(to) && from != NULL && from->tag == to->tag) {
                from = to;
                stmt->each->sequence->type = to;
            }
            if (!is_error(from) && from->tag != KEST_T_INT) {
                report(checker, stmt->each->sequence->span, "K0341",
                       "a count runs between integers, found `%s`",
                       type_name(checker, from));
                from = error_type(checker);
            } else if (!kest_type_equal(from, to)) {
                expected_but(checker, stmt->each->until->span, from, to,
                             "this end");
            }
            if (stmt->each->index.length > 0) {
                report(checker, stmt->each->index, "K0317",
                       "a count has no positions to walk by");
                kest_diags_suggest(checker->program->diags,
                                   "the number is the position");
            }
            uint32_t counted = checker->local_count;
            checker->depth++;
            declare_local(checker, stmt->each->name, from);
            if (checker->local_count > counted) {
                checker->locals[checker->local_count - 1].is_loop_element =
                    true;
                checker->locals[checker->local_count - 1].is_loop_index = true;
            }
            checker->loop_depth++;
            check_block(checker, &stmt->each->body);
            checker->loop_depth--;
            checker->depth--;
            // A name the body never assigns to can be the count itself. A
            // name that was refused before it was declared is one the body's
            // writes went somewhere else, so it counts as written. See D866.
            stmt->each->name_written = checker->local_count <= counted ||
                                       checker->locals[counted].written;
            checker->local_count = counted;
            break;
        }
        KestType *sequence = check_expr(checker, stmt->each->sequence, NULL);
        KestType *element = error_type(checker);
        if (!is_error(sequence)) {
            if (!walks(sequence)) {
                report(checker, stmt->each->sequence->span, "K0317",
                       "`for` walks an array, text, a store or a set of bits, "
                       "found `%s`",
                       type_name(checker, sequence));
                if (sequence->element != NULL && walks(sequence->element)) {
                    say_if_let(checker, sequence, NULL);
                }
            } else if (sequence->tag == KEST_T_ARRAY ||
                       sequence->tag == KEST_T_FIXED) {
                element = sequence->element;
            } else if (sequence->tag == KEST_T_STORE) {
                if (stmt->each->index.length > 0) {
                    report(checker, stmt->each->index, "K0317",
                           "a store has no positions to walk by");
                    kest_diags_suggest(checker->program->diags,
                                       "the reference is what names a slot");
                }
                // What a walk of a store has to give is a reference, because
                // a reference is what removing and writing take. The value is
                // a `get` away, and that `get` returns an optional it cannot
                // fail, which is the noise probe 4 asked about; see D020.
                element = kest_ref_of(checker->program, sequence->element);
            } else if (sequence->tag == KEST_T_FLAGS) {
                if (stmt->each->index.length > 0) {
                    report(checker, stmt->each->index, "K0317",
                           "a set of bits has no positions to walk by");
                    kest_diags_suggest(checker->program->diags,
                                       "the flag is what names the bit");
                }
                // What is walked is the flags that are set, each one a value
                // of the set with that one bit in it, so nothing has to be
                // asked about what came out.
                element = sequence;
            } else if (sequence->tag == KEST_T_TEXT) {
                // Text is its bytes, so walking it gives them. There is no
                // character type and this does not invent one.
                element = builtin(checker, "u8");
            }
        }
        uint32_t mark = checker->local_count;
        checker->depth++;
        if (stmt->each->index.length > 0) {
            declare_local(checker, stmt->each->index, builtin(checker, "i32"));
            if (checker->local_count > mark) {
                checker->locals[checker->local_count - 1].is_loop_element =
                    true;
                checker->locals[checker->local_count - 1].is_loop_index = true;
            }
        }
        uint32_t before_element = checker->local_count;
        declare_local(checker, stmt->each->name, element);
        if (checker->local_count > before_element) {
            checker->locals[checker->local_count - 1].is_loop_element = true;
        }
        checker->loop_depth++;
        check_block(checker, &stmt->each->body);
        checker->loop_depth--;
        checker->depth--;
        stmt->each->index_written = stmt->each->index.length == 0 ||
                                    before_element <= mark ||
                                    checker->locals[mark].written;
        drop_locals(checker, mark);
        break;
    }

    case KEST_STMT_RETURN: {
        if (checker->in_a_block) {
            report(checker, stmt->span, "K0367",
                   "a block runs inside the function it is handed to, so "
                   "`return` has nowhere to go");
            suggest(checker, "a block that gives a value is written "
                             "`|x| value`");
            break;
        }
        KestType *want = checker->result;
        if (stmt->result == NULL) {
            if (want != NULL && want->tag != KEST_T_VOID) {
                report(checker, stmt->span, "K0310",
                       "this function returns `%s`, so `return` needs a value",
                       type_name(checker, want));
            }
            break;
        }
        KestType *value = check_expr(checker, stmt->result, want);
        if (want != NULL && want->tag == KEST_T_VOID) {
            report(checker, stmt->result->span, "K0310",
                   "this function returns nothing, so `return` takes no value");
        } else if (!kest_type_equal(value, want)) {
            expected_but(checker, stmt->result->span, want, value,
                         "this return");
        }
        break;
    }

    case KEST_STMT_BREAK:
    case KEST_STMT_CONTINUE:
        if (checker->loop_depth == 0) {
            report(checker, stmt->span, "K0313", "`%s` is outside a loop",
                   stmt->kind == KEST_STMT_BREAK ? "break" : "continue");
        }
        break;

    case KEST_STMT_SCRATCH:
        checker->scratch_depth++;
        check_block(checker, &stmt->block);
        checker->scratch_depth--;
        break;

    case KEST_STMT_BLOCK:
        check_block(checker, &stmt->block);
        break;

    case KEST_STMT_WAIT:
        check_wait(checker, stmt);
        break;
    }
}

static void check_block(Checker *checker, KestBlock *block) {
    uint32_t mark = checker->local_count;
    checker->depth++;
    for (uint32_t i = 0; i < block->count; i++) {
        check_stmt(checker, block->items[i]);
        if (checker->out_of_memory) {
            break;
        }
    }
    checker->depth--;
    // Dropping the scope is what lets a sibling block reuse a name.
    drop_locals(checker, mark);
}

// Whether control cannot fall off the end of a block. A branch counts only
// when both of its arms do, because a missing `else` is a path.
static bool stmt_returns(const KestStmt *stmt);

static bool always_returns(const KestBlock *block) {
    return block->count > 0 && stmt_returns(block->items[block->count - 1]);
}

// A `match` or an `if` that leaves through every arm is a thing that returns,
// and the line after it is unreachable rather than required.
static bool expr_returns(const KestExpr *value) {
    if (value == NULL) {
        return false;
    }
    if (value->kind == KEST_EXPR_IF) {
        const KestBranch *branch = value->branch;
        if (!branch->has_else || branch->gives ||
            !always_returns(&branch->then_body)) {
            return false;
        }
        if (branch->otherwise != NULL) {
            return expr_returns(branch->otherwise);
        }
        return always_returns(&branch->else_body);
    }
    if (value->kind != KEST_EXPR_MATCH || !value->choose->total ||
        value->choose->arm_count == 0) {
        return false;
    }
    for (uint32_t a = 0; a < value->choose->arm_count; a++) {
        if (value->choose->arms[a].value != NULL ||
            !always_returns(&value->choose->arms[a].body)) {
            return false;
        }
    }
    return true;
}

// Whether a `break` written anywhere in here leaves *this* loop. A `break`
// inside a loop nested in it leaves that one, so its body is not walked --
// what is walked is everything else, including the conditions and sequences
// of those nested loops, because those are read where this loop's body is.
// An `if` and a `match` are expressions with blocks in them, so the walk goes
// through expressions as well: a `break` inside the arm of a `match` is a
// `break` in the body it is written in. See D1080.
static bool block_leaves(const KestBlock *block);

static bool expr_leaves(const KestExpr *expr) {
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
    // A block's own `break` has no loop outside it to leave: it is refused
    // there, and runs where it is called rather than where it is written.
    case KEST_EXPR_BLOCK:
        return false;
    case KEST_EXPR_UNARY:
        return expr_leaves(expr->unary.operand);
    case KEST_EXPR_BINARY:
        return expr_leaves(expr->binary.left) ||
               expr_leaves(expr->binary.right);
    case KEST_EXPR_CALL:
        if (expr_leaves(expr->call.callee)) {
            return true;
        }
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            if (expr_leaves(expr->call.args[i])) {
                return true;
            }
        }
        return false;
    case KEST_EXPR_FIELD:
        return expr_leaves(expr->field.object);
    case KEST_EXPR_INDEX:
        return expr_leaves(expr->index.object) ||
               expr_leaves(expr->index.index);
    case KEST_EXPR_ARRAY:
        for (uint32_t i = 0; i < expr->array.count; i++) {
            if (expr_leaves(expr->array.items[i])) {
                return true;
            }
        }
        return false;
    case KEST_EXPR_TEXT:
        for (uint32_t i = 0; i < expr->text.count; i++) {
            if (expr_leaves(expr->text.parts[i].value)) {
                return true;
            }
        }
        return false;
    case KEST_EXPR_MATCH:
        for (uint32_t i = 0; i < expr->choose->subject_count; i++) {
            if (expr_leaves(expr->choose->subjects[i])) {
                return true;
            }
        }
        for (uint32_t a = 0; a < expr->choose->arm_count; a++) {
            if (expr_leaves(expr->choose->arms[a].value) ||
                block_leaves(&expr->choose->arms[a].body)) {
                return true;
            }
        }
        return false;
    case KEST_EXPR_IF:
        return expr_leaves(expr->branch->condition) ||
               expr_leaves(expr->branch->then_value) ||
               block_leaves(&expr->branch->then_body) ||
               expr_leaves(expr->branch->otherwise) ||
               expr_leaves(expr->branch->else_value) ||
               block_leaves(&expr->branch->else_body);
    }
    return false;
}

static bool stmt_leaves(const KestStmt *stmt) {
    switch (stmt->kind) {
    case KEST_STMT_BREAK:
        return true;
    case KEST_STMT_CONTINUE:
    case KEST_STMT_WAIT:
        return false;
    case KEST_STMT_LET:
        return expr_leaves(stmt->let.value);
    case KEST_STMT_ASSIGN:
        return expr_leaves(stmt->assign.target) ||
               expr_leaves(stmt->assign.value);
    case KEST_STMT_EXPR:
    case KEST_STMT_DEFER:
        return expr_leaves(stmt->value);
    case KEST_STMT_WHILE:
        return expr_leaves(stmt->loop.condition);
    case KEST_STMT_FOR:
        return expr_leaves(stmt->each->sequence) ||
               expr_leaves(stmt->each->until);
    case KEST_STMT_RETURN:
        return expr_leaves(stmt->result);
    case KEST_STMT_SCRATCH:
    case KEST_STMT_BLOCK:
        return block_leaves(&stmt->block);
    }
    return false;
}

static bool block_leaves(const KestBlock *block) {
    for (uint32_t i = 0; i < block->count; i++) {
        if (stmt_leaves(block->items[i])) {
            return true;
        }
    }
    return false;
}

static bool stmt_returns(const KestStmt *stmt) {
    switch (stmt->kind) {
    case KEST_STMT_RETURN:
        return true;
    case KEST_STMT_SCRATCH:
    case KEST_STMT_BLOCK:
        return always_returns(&stmt->block);
    case KEST_STMT_EXPR:
        return expr_returns(stmt->value);
    case KEST_STMT_WHILE:
        // A loop written `while true` that nothing breaks out of is a loop the
        // program does not come back from, so a body that ends in one ends in
        // a `return` or in nothing at all. `while let` is not one: it ends
        // when what it asks for is `none`. See D1080.
        return stmt->loop.binding.length == 0 && stmt->loop.condition != NULL &&
               stmt->loop.condition->kind == KEST_EXPR_BOOL &&
               stmt->loop.condition->boolean &&
               !block_leaves(&stmt->loop.body);
    // A `for` walks something that can be empty, so it is a loop a program
    // comes back from however it is written. The rest leave the body the way
    // the statement after them does.
    case KEST_STMT_LET:
    case KEST_STMT_ASSIGN:
    case KEST_STMT_FOR:
    case KEST_STMT_BREAK:
    case KEST_STMT_CONTINUE:
    case KEST_STMT_DEFER:
    case KEST_STMT_WAIT:
        return false;
    }
    return false;
}

static bool check_unit(KestProgram *program, KestUnit *unit);

// `resumes c.at`, held to what it says: `c` is something the function takes, a
// struct, and what it gives back; `at` is a field of it that is an enum. What
// the body waits at is a case of that enum. Answers the enum, or NULL with the
// reason said. See D1263.
static const KestType *resumes_from(Checker *checker, const KestDecl *decl,
                                    const KestType *signature) {
    KestSpan param = {0, 0};
    KestSpan field = {0, 0};
    if (!kest_resumes_spans(checker->program->source, decl, &param, &field)) {
        return NULL;
    }
    const char *param_name = span_text(checker, param);
    const char *field_name = span_text(checker, field);
    if (decl->type_param_count > 0) {
        report(checker, param, "K0368",
               "a body that resumes takes no types, because what it waits in "
               "is one struct");
        return NULL;
    }
    const KestType *held = NULL;
    for (uint32_t p = 0;
         p < decl->function.param_count && p < signature->param_count; p++) {
        KestSpan name = decl->function.params[p]->name;
        if (name.length == param.length &&
            memcmp(span_text(checker, name), param_name, param.length) == 0) {
            held = signature->params[p];
        }
    }
    if (held == NULL) {
        report(checker, param, "K0368",
               "`%.*s` is not something `%.*s` takes",
               (int)param.length, param_name, (int)decl->name.length,
               span_text(checker, decl->name));
        return NULL;
    }
    if (held->tag == KEST_T_ERROR) {
        return NULL;
    }
    if (held->tag != KEST_T_STRUCT) {
        report(checker, param, "K0368",
               "a body resumes from a struct it is handed, and `%.*s` is `%s`",
               (int)param.length, param_name, type_name(checker, held));
        return NULL;
    }
    if (!kest_type_equal(signature->result, held)) {
        report(checker, param, "K0368",
               "`%.*s` resumes from `%.*s`, so it gives back a `%s`",
               (int)decl->name.length, span_text(checker, decl->name),
               (int)param.length, param_name, type_name(checker, held));
        kest_diags_suggest(checker->program->diags,
                           "a `wait` gives back what the body resumes from, "
                           "and so does the end of it");
        return NULL;
    }
    for (uint32_t m = 0; m < held->member_count; m++) {
        if (!kest_word_same(held->members[m].name, field_name, field.length)) {
            continue;
        }
        const KestType *cases = held->members[m].type;
        if (cases == NULL || cases->tag != KEST_T_ENUM) {
            report(checker, field, "K0368",
                   "a body resumes from a field that is an enum, and `%.*s` "
                   "is `%s`",
                   (int)field.length, field_name, type_name(checker, cases));
            return NULL;
        }
        return cases;
    }
    report(checker, field, "K0368", "`%s` has no field `%.*s`",
           type_name(checker, held), (int)field.length, field_name);
    return NULL;
}

// One body against one signature. A generic copy is the same thing with its
// type names bound, which is what makes a copy not a special case.
static bool check_function(KestProgram *program, Checker *checker,
                           const KestDecl *decl, KestType *signature) {
    const char *name = kest_span_text(program->source, decl->name);
    checker->local_count = 0;
    checker->depth = 0;
    checker->loop_depth = 0;
    checker->result = signature->result;
    checker->function = decl;

    for (uint32_t p = 0;
         p < decl->function.param_count && p < signature->param_count; p++) {
        declare_local(checker, decl->function.params[p]->name,
                      signature->params[p]);
        if (checker->local_count > 0) {
            checker->locals[checker->local_count - 1].is_parameter = true;
        }
    }

    uint32_t body = checker->local_count;
    checker->resume_cases = NULL;
    checker->waited = NULL;
    checker->waits = 0;
    checker->scratch_depth = 0;
    if (decl->function.resumes > 0) {
        checker->resume_cases = resumes_from(checker, decl, signature);
        checker->resume_mark = body;
        if (checker->resume_cases != NULL) {
            checker->waited = KEST_ARENA_ARRAY(
                program->arena, uint32_t,
                checker->resume_cases->case_count + 1);
            if (checker->waited == NULL) {
                checker->out_of_memory = true;
                return false;
            }
            memset(checker->waited, 0,
                   sizeof(uint32_t) * (checker->resume_cases->case_count + 1));
        }
    }
    check_block(checker, (KestBlock *)&decl->function.body);
    checker->resume_cases = NULL;
    // What the body itself declared, which is the one scope nothing else
    // rewinds: a block inside it is dropped where it ends, and this is where
    // the outermost one does. See D726.
    drop_locals(checker, body);

    // Nothing is said about a result nobody could read. A signature already
    // refused is a signature this has no opinion about, and saying `can end
    // without returning `<unknown>`` above the refusal that made it unknown is
    // the second voice D507 keeps taking out. See D519.
    if (checker->result != NULL && checker->result->tag != KEST_T_VOID &&
        checker->result->tag != KEST_T_ERROR &&
        !always_returns(&decl->function.body)) {
        report(checker, decl->name, "K0316",
               "`%.*s` can end without returning `%s`",
               (int)decl->name.length, name,
               kest_type_name(program->arena, checker->result));
        // The shape somebody writes when they expect the last thing in a body
        // to be what it gives back. A `match` or an `if` whose arms give
        // values is a value, and a value on its own is not a return.
        const KestBlock *written = &decl->function.body;
        const KestStmt *last =
            written->count == 0 ? NULL : written->items[written->count - 1];
        if (last != NULL && last->kind == KEST_STMT_EXPR &&
            last->value != NULL &&
            ((last->value->kind == KEST_EXPR_MATCH &&
              last->value->choose->gives) ||
             (last->value->kind == KEST_EXPR_IF &&
              last->value->branch != NULL && last->value->branch->gives))) {
            kest_diags_suggest(program->diags,
                               "the arms give a value, so it is one: write "
                               "`return` in front of it");
        }
    }
    return !checker->out_of_memory;
}

static bool check_unit(KestProgram *program, KestUnit *unit) {
    Checker checker = {0};
    checker.program = program;

    // A constant's value is an expression like any other and needs a type on
    // it, both to be measured against what was declared and because the
    // compiler writes it into every use and reads that type to choose the
    // instruction.
    for (uint32_t i = 0; i < unit->count; i++) {
        KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_CONST) {
            continue;
        }
        KestType *declared = kest_resolve_type_ref(program, decl->constant.type);
        KestType *value = check_expr(&checker, decl->constant.value, declared);
        if (!kest_type_equal(value, declared)) {
            expected_but(&checker, decl->constant.value->span, declared, value,
                         "this constant");
        }
    }

    for (uint32_t i = 0; i < unit->count; i++) {
        KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FN || decl->function.is_extern) {
            continue;
        }

        // Where it is declared, not what it is called: two functions may share
        // a name and each has to be checked against its own signature.
        KestSymbol *symbol =
            kest_symbol_at(program, program->source, decl->name);
        if (symbol == NULL || symbol->type->tag != KEST_T_FN) {
            continue;
        }
        // A generic's body is checked here too, once, with each type name
        // standing for a value that can do what the declaration says it can
        // and nothing else. Until D1043 this was skipped entirely and a body
        // was first read when something copied it -- so a generic calling a
        // name that is not there, or reading a field of a type parameter,
        // checked clean and shipped. Each copy is still checked where it is
        // made, because that is where the types are known.
        if (symbol->type->type_param_count > 0) {
            if (symbol->type->type_param_stands == NULL) {
                continue;
            }
            kest_bind_types(program, symbol->type->type_param_names,
                            symbol->type->type_param_stands,
                            symbol->type->type_param_count);
            // A generic's own body, checked with its type names standing
            // for themselves: what is asked there is whether the body is
            // right for every type the declaration allows rather than for
            // one, which is what `kest_bind_types` above puts in place. See
            // D1043.
            bool ok = check_function(program, &checker, decl, symbol->type);
            kest_unbind_types(program);
            if (!ok) {
                return false;
            }
            // And a requirement the body never asked for, which refuses types
            // that would have worked and is how a written contract drifts
            // away from the body it is about. The same rule as an import
            // nothing writes through. See D1043.
            for (uint32_t g = 0; g < symbol->type->type_param_count; g++) {
                const KestType *stands = symbol->type->type_param_stands[g];
                uint8_t spare = (uint8_t)(stands->wants & ~stands->took);
                for (uint32_t at = 0; at < KEST_CAPABILITY_COUNT; at++) {
                    if ((spare & kest_capability(at)->bit) == 0) {
                        continue;
                    }
                    kest_diags_add(program->diags, KEST_SEVERITY_WARNING,
                                   "K0513", decl->type_params[g].name,
                                   "nothing in this body %s a `%s`",
                                   kest_capability(at)->word,
                                   stands->name);
                    kest_diags_suggest(program->diags,
                                       "take it out: what a generic says it "
                                       "needs is what it may be handed");
                }
            }
            continue;
        }

        if (!check_function(program, &checker, decl, symbol->type)) {
            return false;
        }
    }
    return true;
}

// `main` is what `kest run` calls: with nothing, and what it answers becomes
// the exit status. That is a shape, and a shape is a declaration, so it is
// held to here rather than when somebody tries to run it — a program that
// compiles and then cannot be run has been told it was fine.
//
// Only the file that was named is held to it. A `main` further in is a
// function like any other, because nothing will call it.
static void check_entry(KestProgram *program, KestUnit *unit) {
    Checker checker = {0};
    checker.program = program;

    // How many of them there are, first: a name may be several functions in
    // this language, and `main` is the one name where that cannot be. What
    // `kest run` calls is a name and not a shape, so two of them is a program
    // with two beginnings and nothing to choose between them.
    const KestDecl *first_main = NULL;
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FN || decl->function.is_extern) {
            continue;
        }
        const char *name = kest_span_text(program->source, decl->name);
        if (!kest_word_same(KEST_MAIN, name, decl->name.length)) {
            continue;
        }
        if (first_main == NULL) {
            first_main = decl;
            continue;
        }
        report(&checker, decl->name, "K0355",
               "`main` is the name `kest run` calls, and this file declares "
               "more than one");
        kest_diags_note(program->diags, program->source, first_main->name,
                        "this is the other one");
        suggest(&checker, "one of them is where the program starts; the rest "
                          "want names of their own");
    }
    // And what is wrong with the one that starts the program, which is the
    // first of them: the rest have been told they are one too many, and
    // saying `kest run` calls this with nothing about a function that is not
    // the entry is a message about the wrong line.
    if (first_main != NULL) {
        const KestDecl *decl = first_main;
        KestSymbol *symbol =
            kest_symbol_at(program, program->source, decl->name);
        if (symbol == NULL || symbol->type->tag != KEST_T_FN) {
            return;
        }

        const KestType *result = symbol->type->result;
        // An error type has already been reported once and stands for
        // whatever was meant, so it is not a second mistake.
        if (result != NULL && result->tag != KEST_T_ERROR &&
            result->tag != KEST_T_VOID &&
            !(result->tag == KEST_T_INT && result->width == 32 &&
              result->is_signed)) {
            report(&checker, decl->name, "K0347",
                   "`main` gives `%s`, and what `main` gives is the exit "
                   "status",
                   kest_type_name(program->arena, result));
            suggest(&checker, "give `i32`, which is a number from 0 to 255, "
                              "or give nothing");
        }
        if (symbol->type->param_count > 0) {
            report(&checker, decl->name, "K0348",
                   "`main` takes %u parameter%s, and `kest run` calls it with "
                   "nothing",
                   symbol->type->param_count,
                   symbol->type->param_count == 1 ? "" : "s");
            suggest(&checker, "declare it `fn main()`");
        }
        if (symbol->type->type_param_count > 0) {
            report(&checker, decl->name, "K0349",
                   "`main` is generic, and nothing calls it with a type to "
                   "make the copy from");
            suggest(&checker, "declare it `fn main()`");
        }
    }
}

// Every copy of a generic function is the same tree, and the checker writes
// the types it worked out onto it, so a tree carries one copy's types at a
// time. The compiler asks for them back before it emits each copy. Nothing is
// reported here: whatever there was to say was said the first time.
bool kest_retype_instance(KestProgram *program, KestInstance *instance) {
    if (instance->type == NULL) {
        return true;
    }
    Checker checker = {0};
    checker.program = program;
    kest_diags_mute(program->diags, true);
    kest_bind_types(program, instance->names, instance->bindings,
                    instance->count);
    bool ok = check_function(program, &checker, instance->decl,
                             instance->type);
    kest_unbind_types(program);
    kest_diags_mute(program->diags, false);
    return ok;
}

// The last piece of a name, which is what a function is called where it is
// written: everything is registered under the module it came from.
static bool is_called(const KestSymbol *symbol, const char *name) {
    const char *dot = strrchr(symbol->name, '.');
    return strcmp(dot == NULL ? symbol->name : dot + 1, name) == 0;
}

bool kest_check_bodies(KestProgram *program, KestUnits *units) {
    // The file that was named, kept because `program->source` is whichever
    // unit was last worked on and the warnings at the end of this are about
    // the file somebody asked about. Everything else was reached from it.
    const KestSource *root = NULL;
    for (uint32_t u = 0; u < units->count; u++) {
        kest_program_in(program, &units->items[u]);
        kest_diags_in(program->diags, program->source);
        if (!check_unit(program, &units->items[u].unit)) {
            return false;
        }
        // The first is the file that was named; the rest were reached from it.
        if (u == 0) {
            root = program->source;
            check_entry(program, &units->items[u].unit);
        }
    }

    // Checking a copy may call another generic, which makes another copy, so
    // this runs until nothing new appears rather than once over a list.
    Checker checker = {0};
    checker.program = program;
    bool more = true;
    while (more) {
        more = false;
        for (uint32_t i = 0; i < program->instance_count; i++) {
            KestInstance *instance = &program->instances[i];
            if (instance->checked || instance->type == NULL) {
                continue;
            }
            instance->checked = true;
            more = true;
            kest_program_in(program, instance->unit);
            kest_diags_in(program->diags, program->source);
            kest_bind_types(program, instance->names, instance->bindings,
                            instance->count);
            uint32_t before = program->diags->count;
            // Whatever this body asks for is asked for by whoever asked for
            // this, however many bodies down that is.
            checker.asking = instance->site;
            checker.asking_source = instance->site_source;
            bool ok = check_function(program, &checker, instance->decl,
                                     instance->type);
            checker.asking.length = 0;
            checker.asking_source = NULL;
            kest_unbind_types(program);
            // Everything this copy's body had to say is about this copy, so
            // each of them is told which copy and where it was asked for. A
            // body says more than one thing, which is why the note goes on
            // each rather than on the last; and it says which by naming what
            // the type names stand for, because two calls on one line are two
            // copies and the line alone does not say which.
            // In the arena and all of it: this was two hundred and fifty-six
            // bytes with room kept back for a tail that counted what did not
            // fit, and there is nothing to count when there is room for
            // everything.
            //
            // And only for a copy that said something. The note is put on
            // what was said between `before` and now, so a copy that said
            // nothing has nothing to put it on -- and building the string
            // anyway spends an arena allocation, and one per binding inside
            // `kest_type_name`, on a sentence nobody will read. Most copies
            // in a program that compiles are quiet ones. See D742.
            size_t room = 1;
            if (instance->site.length == 0 ||
                program->diags->count == before) {
                if (!ok) {
                    return false;
                }
                continue;
            }
            for (uint32_t b = 0; b < instance->count; b++) {
                room += strlen(instance->names[b]) +
                        strlen(kest_type_name_read(program->arena,
                                                   instance->bindings[b])) +
                        strlen("`` is ``, and ");
            }
            char *which = kest_arena_alloc(program->arena, room, 1);
            size_t used = 0;
            if (which == NULL) {
                room = 0;
            }
            for (uint32_t b = 0; which != NULL && b < instance->count; b++) {
                used += (size_t)snprintf(
                    which + used, room - used, "%s`%s` is `%s`",
                    b == 0 ? "" : (b + 1 == instance->count ? " and " : ", "),
                    instance->names[b],
                    kest_type_name_read(program->arena,
                                        instance->bindings[b]));
            }
            for (uint32_t d = before;
                 instance->site.length > 0 && d < program->diags->count; d++) {
                // `where`, because the names are the ones written in the body
                // above and not in the line this note is on: a reader looking
                // at `K` is looking at the frame the message opened with.
                kest_diags_note_at(program->diags, d,
                                   instance->site_source, instance->site,
                                   "this copy was asked for here, where %s",
                                   which);
            }
            if (!ok) {
                return false;
            }
        }
    }

    // An `extern` nothing calls is not asked of any host: what starting holds
    // a host to is what the program can reach, and a declaration nobody
    // reached is a name in a file. Saying so is the point — a host writer
    // reading that file would bind it, and binding it is work with nothing on
    // the other end.
    //
    // This is the same sentence as the one below about a function of the
    // program's own, and it is said in the same place for the same reason: a
    // reader asks `check` about a file, and `check` does not emit code.
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        const KestType *type = symbol->type;
        if (type == NULL || type->tag != KEST_T_FN || !type->is_foreign ||
            type->foreign_name == NULL || symbol->named) {
            continue;
        }
        kest_diags_in(program->diags, symbol->source);
        kest_diags_add(program->diags, KEST_SEVERITY_WARNING, "K0506",
                       symbol->span,
                       "nothing calls `%s`, so no host is asked for it",
                       type->foreign_name);
        kest_diags_suggest(program->diags,
                           "call it, or take the declaration out: a host that "
                           "binds it is doing work with nothing on the other "
                           "end");
    }

    // The two below are about a program: a file with a `main` in it. A
    // library is named by whoever imports it and would light up from end to
    // end.
    bool a_program = false;
    for (uint32_t i = 0; i < program->global_count && !a_program; i++) {
        const KestSymbol *symbol = &program->globals[i];
        a_program = symbol->type != NULL && symbol->type->tag == KEST_T_FN &&
                    is_called(symbol, "main") && symbol->source == root;
    }

    // A constant nothing reads. A host cannot ask for one — what it can ask
    // for by name is a function, which is why there is no warning about a
    // function nobody in the program calls (D224) — so a constant nothing
    // reads is one nothing will ever read.
    for (uint32_t i = 0; a_program && i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        // A function name is a constant too — nothing may write to it — so
        // what tells the two apart is the type, the same way the JSON does.
        if (!symbol->is_const || symbol->type == NULL ||
            symbol->type->tag == KEST_T_FN || symbol->named ||
            symbol->source != root) {
            continue;
        }
        // A `[T; N]` reads it before there is a symbol to mark, so the names
        // those counted with are kept and read here.
        bool counted = false;
        for (uint32_t c = 0; c < program->counted_count && !counted; c++) {
            counted = is_called(symbol, program->counted[c]);
        }
        if (counted) {
            continue;
        }
        kest_diags_in(program->diags, symbol->source);
        kest_diags_add(program->diags, KEST_SEVERITY_WARNING, "K0508",
                       symbol->span, "nothing in this program reads `%s`",
                       symbol->name);
        kest_diags_suggest(program->diags,
                           "take it out: a constant is a name for a value, and "
                           "one nothing reads is a value nobody asked for");
    }

    // And a shape nothing names. A host cannot ask for one either: what a
    // host may lend is a type the program holds in an array, and holding it
    // in one is naming it.
    //
    // A shape that names itself — a list whose next is one of its own — is
    // named by that, so this is quiet about a shape nobody but itself
    // mentions. What it catches is one nobody mentions at all.
    for (uint32_t i = 0; a_program && i < program->type_count; i++) {
        const KestType *type = program->types[i];
        if ((type->tag != KEST_T_STRUCT && type->tag != KEST_T_ENUM &&
             type->tag != KEST_T_FLAGS) ||
            type->named || type->declared_in != root) {
            continue;
        }
        kest_diags_in(program->diags, type->declared_in);
        kest_diags_add(program->diags, KEST_SEVERITY_WARNING, "K0509",
                       type->span, "nothing in this program names `%s`",
                       type->name);
        kest_diags_suggest(program->diags,
                           "take it out, or hold one: a shape nothing names is "
                           "laid out and never reached");
    }

    // And an import nothing reached. A module named here is read, parsed,
    // checked and compiled whether or not a name comes through it, so one
    // nothing writes is a file's worth of work for a file that never mentions
    // it — the only one of these warnings with a number behind it. About the
    // file somebody asked about, because the rest were reached from it and a
    // library module is read for whoever imports it. See D725.
    const KestUnitInfo *named = units->count > 0 ? &units->items[0] : NULL;
    for (uint32_t i = 0; a_program && named != NULL && i < named->import_count;
         i++) {
        if (named->import_reached[i]) {
            continue;
        }
        // Where it is written, which is the line to take out.
        KestSpan where = {0, 0};
        for (uint32_t d = 0; d < named->unit.count; d++) {
            const KestDecl *decl = named->unit.items[d];
            if (decl->kind != KEST_DECL_IMPORT) {
                continue;
            }
            const char *wrote = kest_span_text(&named->source, decl->name);
            size_t length = decl->name.length;
            const char *last = wrote;
            for (size_t at = 0; at < length; at++) {
                if (wrote[at] == '.') {
                    last = wrote + at + 1;
                }
            }
            size_t tail = length - (size_t)(last - wrote);
            if (kest_word_same(named->imports[i], last, tail)) {
                where = decl->name;
            }
        }
        kest_diags_in(program->diags, &named->source);
        kest_diags_add(program->diags, KEST_SEVERITY_WARNING, "K0511", where,
                       "nothing in this file writes `%s`", named->imports[i]);
        kest_diags_suggest(program->diags,
                           "take the import out: a module named here is read "
                           "and compiled whether anything comes through it or "
                           "not");
    }
    return true;
}

