#include "contract.h"

#include "check.h"

#include <string.h>

#define NO_SITE ((KestSpan){0, 0})

// How many further places one body can name. The promise's own note takes one
// of what a diagnostic has room for, so the rest is this.
#define MORE_SITES (KEST_MOST_PLACES - 1)

typedef struct {
    const KestDecl *decl;
    // What it is matched by, which includes what it takes, and what it is
    // called in a message, which does not.
    const char *name;
    const char *display;
    // Where this body allocates, if it does directly. Zero length when it
    // does not.
    KestSpan site;
    // What the site is: a call through a value nothing promises about, rather
    // than an allocation. The shape is what the value is written as, which is
    // what a promise would have to be written into.
    const char *shape;
    // What it is about the site that reaches the heap, in the words the reader
    // needs: `this allocates` says which line and not what on it.
    const char *why;
    // And every other place in this body that reaches it. A promise broken in
    // four places is four lines to change, and a reader told the first of them
    // compiles four times to hear the rest. One note each, and the promise
    // takes the last, which is what bounds this. See D509.
    KestSpan sites[MORE_SITES];
    const char *whys[MORE_SITES];
    uint32_t site_count;
    uint32_t more;
    // Indices of the functions this one calls, and where each call is.
    uint32_t *callees;
    KestSpan *calls;
    uint32_t call_count;
    uint32_t call_capacity;
    uint32_t unit;
    bool allocates;
    bool promises;
    bool is_extern;
    bool visiting;
} Function;

typedef struct {
    KestProgram *program;
    Function *functions;
    uint32_t count;
    bool out_of_memory;
    // Which promise this walk is about. The graph is the same graph either
    // way — the calls a body makes are the calls a body makes — and what
    // differs is what counts as reaching: a builtin that grows, or a function
    // the host provides. Walked once for each rather than once for both,
    // because a body that breaks one and keeps the other has one thing wrong
    // with it and a reader wants that one. See D853.
    // Which promise this walk is about: nought for `no.alloc`, one for
    // `no.host`, two for `deterministic`. The graph is the same graph for all
    // three -- the calls a body makes are the calls a body makes -- and what
    // differs is what counts as reaching. See D942.
    int about;
    // Whether what is being read is a generic declaration rather than a copy
    // of one. A declaration has no types until a copy gives it some, so what a
    // call in it reaches cannot be said -- but what its own body reaches can:
    // `array()` makes something that can grow whatever `T` is. So a template
    // is read for what it does and not for what it calls, which leaves a
    // promise on one that nothing instantiates proved rather than assumed.
    // See D939.
    bool a_template;
    // Where each function is, by the name the checker gave it. The walk that
    // found one was the graph's own size for every call in it: a project of
    // nine thousand functions spent two fifths of a clean check inside it,
    // and three times over, because a promise is proved once for each of the
    // three. Slots hold one more than the place they name. See D1087.
    uint32_t *by_name;
    uint32_t by_name_slots;
} Graph;


// One place a body is written down as reaching the heap, so the first and the
// rest are kept by the same rule and nothing has to remember which it is.
static void reaches(Graph *graph, Function *function, KestSpan span,
                    const char *why) {
    // Nothing a body writes reaches the host. The only way out of a program is
    // a call to something the host provides, and that is a call like any
    // other: found where calls are found and followed where calls are
    // followed.
    // Only `no.alloc` is about what a body does to the heap. The other two are
    // about what it calls, which is followed where calls are followed.
    if (graph->about != 0) {
        return;
    }
    function->allocates = true;
    if (function->site.length == 0) {
        function->site = span;
        function->why = why;
        return;
    }
    if (span.offset == function->site.offset) {
        return;
    }
    for (uint32_t i = 0; i < function->site_count; i++) {
        if (function->sites[i].offset == span.offset) {
            return;
        }
    }
    if (function->site_count == MORE_SITES) {
        function->more++;
        return;
    }
    function->sites[function->site_count] = span;
    function->whys[function->site_count++] = why;
}

static const char *span_text(Graph *graph, KestSpan span) {
    return kest_span_text(graph->program->source, span);
}

// The shape a parameter was written as, for a call through a value in a generic
// declaration. A template has no types until a copy gives it some, so what a
// call in one reaches cannot be read off the tree -- but a parameter's shape is
// written down whatever `T` turns out to be, and a promise is part of a shape.
// Without this a promise on a generic that nothing instantiates was proved
// against nothing, and the refusal arrived at the first use, in the file of
// whoever called it rather than the file that made the promise. See D943.
static const KestTypeRef *written_shape(Graph *graph, Function *function,
                                        const KestExpr *callee) {
    if (callee->kind != KEST_EXPR_NAME || function->decl == NULL) {
        return NULL;
    }
    const char *text = span_text(graph, callee->span);
    for (uint32_t i = 0; i < function->decl->function.param_count; i++) {
        const KestField *param = function->decl->function.params[i];
        if (param->type == NULL || param->type->kind != KEST_TYPE_FN ||
            param->name.length != callee->span.length) {
            continue;
        }
        if (memcmp(span_text(graph, param->name), text,
                   callee->span.length) == 0) {
            return param->type;
        }
    }
    return NULL;
}

// And the words it was written in, which are the words to suggest the promise
// be added to. Taken from the source rather than made from a type, because
// there is no type here to make one from.
static const char *shape_written(Graph *graph, KestSpan span) {
    char *copy = KEST_ARENA_ARRAY(graph->program->arena, char, span.length + 1);
    if (copy == NULL) {
        graph->out_of_memory = true;
        return NULL;
    }
    memcpy(copy, span_text(graph, span), span.length);
    copy[span.length] = '\0';
    return copy;
}

// Every function put in once, after the nodes are made and their names are
// known. The first one under a name is the one a lookup answers with, which is
// what the walk this replaced did. Answers false only for no room, and a graph
// with no table walks itself the way it always did. See D1087.
static bool graph_index(Graph *graph, KestArena *arena) {
    uint32_t slots = 64;
    while (slots < (graph->count + 1) * 2) {
        slots *= 2;
    }
    graph->by_name = KEST_ARENA_ARRAY(arena, uint32_t, slots);
    if (graph->by_name == NULL) {
        graph->by_name_slots = 0;
        return false;
    }
    graph->by_name_slots = slots;
    uint32_t mask = slots - 1;
    for (uint32_t i = 0; i < graph->count; i++) {
        const char *name = graph->functions[i].name;
        if (name == NULL) {
            continue;
        }
        uint32_t slot = kest_name_hash(name, strlen(name)) & mask;
        while (graph->by_name[slot] != 0) {
            slot = (slot + 1u) & mask;
        }
        graph->by_name[slot] = i + 1u;
    }
    return true;
}

static int32_t find_exact(Graph *graph, const char *text, size_t length) {
    if (graph->by_name_slots == 0) {
        for (uint32_t i = 0; i < graph->count; i++) {
            if (kest_word_same(graph->functions[i].name, text, length)) {
                return (int32_t)i;
            }
        }
        return -1;
    }
    uint32_t mask = graph->by_name_slots - 1;
    uint32_t slot = kest_name_hash(text, length) & mask;
    while (graph->by_name[slot] != 0) {
        uint32_t at = graph->by_name[slot] - 1u;
        if (kest_word_same(graph->functions[at].name, text, length)) {
            return (int32_t)at;
        }
        slot = (slot + 1u) & mask;
    }
    return -1;
}

// Which function a call reaches, which the checker settled and left on the
// callee, because two functions may share a name.
static int32_t find_called(Graph *graph, const KestExpr *callee) {
    if (callee->type == NULL || callee->type->tag != KEST_T_FN ||
        callee->type->symbol == NULL) {
        return -1;
    }
    return find_exact(graph, callee->type->symbol,
                      strlen(callee->type->symbol));
}

static void record_call(Graph *graph, Function *caller, uint32_t callee,
                        KestSpan span) {
    // What a template calls is settled per copy, and each copy is a node of
    // its own. See D939.
    if (graph->a_template) {
        return;
    }
    if (caller->call_count == caller->call_capacity) {
        uint32_t grown =
            caller->call_capacity == 0 ? 8 : caller->call_capacity * 2;
        uint32_t *callees =
            KEST_ARENA_ARRAY(graph->program->arena, uint32_t, grown);
        KestSpan *calls =
            KEST_ARENA_ARRAY(graph->program->arena, KestSpan, grown);
        if (callees == NULL || calls == NULL) {
            graph->out_of_memory = true;
            return;
        }
        if (caller->call_count > 0) {
            memcpy(callees, caller->callees,
                   sizeof(uint32_t) * caller->call_count);
            memcpy(calls, caller->calls, sizeof(KestSpan) * caller->call_count);
        }
        caller->callees = callees;
        caller->calls = calls;
        caller->call_capacity = grown;
    }
    caller->calls[caller->call_count] = span;
    caller->callees[caller->call_count++] = callee;
}

static void walk_block(Graph *graph, Function *function,
                       const KestBlock *block);

static void walk_expr(Graph *graph, Function *function, const KestExpr *expr) {
    if (expr == NULL) {
        return;
    }

    switch (expr->kind) {
    // What a block does is done in the body it is written in, whichever
    // function it is handed to, so it is walked as part of that body.
    // See D1257.
    case KEST_EXPR_BLOCK:
        if (expr->lambda->value != NULL) {
            walk_expr(graph, function, expr->lambda->value);
        } else {
            walk_block(graph, function, &expr->lambda->body);
        }
        break;
    case KEST_EXPR_ARRAY:
        // A run of a written length is laid out where it stands (D064): slots
        // in the frame, or bytes inside the struct it is written into. Nothing
        // reaches the heap, and the compiler emits no instruction that could.
        // What allocates is the kind that can grow.
        if (expr->type == NULL || expr->type->tag != KEST_T_FIXED) {
            reaches(graph, function, expr->span,
                    "a run that can grow is one on the heap");
        }
        for (uint32_t i = 0; i < expr->array.count; i++) {
            walk_expr(graph, function, expr->array.items[i]);
        }
        break;

    case KEST_EXPR_TEXT:
        // Text with a hole in it is built, and building it reaches the heap.
        // A string with nothing in it is a constant and does not.
        reaches(graph, function, expr->span,
                "text with a hole in it is built, and what is built is on the "
                "heap");
        for (uint32_t i = 0; i < expr->text.count; i++) {
            walk_expr(graph, function, expr->text.parts[i].value);
        }
        break;
    case KEST_EXPR_CALL: {
        const KestExpr *callee = expr->call.callee;
        // A store can grow, so putting something into one reaches the heap.
        // Reading through a reference, writing through one and removing what
        // it named do not, which is what makes a frame step able to walk an
        // object graph inside a promise.
        if (callee->kind == KEST_EXPR_NAME && find_called(graph, callee) < 0) {
            const char *text = span_text(graph, callee->span);
            // Every name the language answers to on its own, each with what it
            // does to the heap or nothing where it does none. The list is
            // every builtin rather than only the ones that allocate, because
            // one this proof has never heard of is one it says nothing about:
            // the promise would then be broken with nothing to name the line,
            // and what would catch it is the proof that reads the emitted
            // code, which says this project got it wrong for what is the
            // program's own mistake. `check-tables.sh` holds these names to
            // the ones the checker knows, so a builtin added to the language
            // is one somebody has to have an opinion about here.
            static const struct {
                const char *name;
                const char *why;
            } REACHES[] = {
                {"add", "`add` grows what it is given"},
                {"array", "`array()` makes something that can grow"},
                {"bits", NULL},
                {"clear", NULL},
                {"find", NULL},
                {"float", NULL},
                // It writes where there is room and answers false where there
                // is not, so it never reaches the heap. See D940.
                {"fit", NULL},
                {"get", NULL},
                {"hash", NULL},
                {"len", NULL},
                {"matches", NULL},
                {"pop", NULL},
                {"push", "`push` grows what it is given"},
                {"remove", NULL},
                // A piece of a piece of text is a place inside it and how
                // many bytes of it, so there is nothing to copy. It used to
                // cost one: a piece of text was a pointer that had to end in
                // a nought, so a cut out of the middle was a copy. See D964.
                {"rest", NULL},
                {"room", "`room` makes room in what it is given"},
                {"set", NULL},
                {"slice", NULL},
                {"store", "`store()` makes something that can grow"},
            };
            for (uint32_t i = 0; i < sizeof(REACHES) / sizeof(REACHES[0]); i++) {
                if (!kest_word_same(REACHES[i].name, text,
                                    callee->span.length)) {
                    continue;
                }
                if (REACHES[i].why == NULL) {
                    break;
                }
                reaches(graph, function, expr->span, REACHES[i].why);
                break;
            }
            // Text from bytes copies them, which is the whole point of it: the
            // pieces are gathered free and paid for once. It is a conversion
            // and not a builtin, which is why it is asked about here rather
            // than in the table the builtins are held to.
            if (kest_word_same("text", text, callee->span.length)) {
                reaches(graph, function, expr->span,
                        "`text` copies the bytes it is given");
            }
        }
        // Through a value there is no body to follow, so what it promises is
        // what is known about it. A function type with no symbol is a value
        // rather than a declaration, and its promise is part of its type,
        // which is what keeps this provable at all.
        // A block is not one of those: what it does is written where it is,
        // and judged there, as part of the body it was written in. See D1257.
        if (callee->type != NULL && callee->type->tag == KEST_T_FN &&
            callee->type->symbol == NULL && !callee->type->is_foreign &&
            !callee->type->block &&
            !(graph->about == 2   ? callee->type->deterministic
              : graph->about == 1 ? callee->type->no_host
                                  : callee->type->no_alloc)) {
            if (function->site.length == 0) {
                function->site = expr->span;
                function->shape =
                    kest_type_name(graph->program->arena, callee->type);
            }
            function->allocates = true;
        }
        if (graph->a_template &&
            (callee->type == NULL || callee->type->tag != KEST_T_FN)) {
            const KestTypeRef *written =
                written_shape(graph, function, callee);
            if (written != NULL &&
                !(graph->about == 2   ? written->deterministic
                  : graph->about == 1 ? written->no_host
                                      : written->no_alloc)) {
                if (function->site.length == 0) {
                    function->site = expr->span;
                    function->shape = shape_written(graph, written->span);
                }
                function->allocates = true;
            }
        }
        // Building a struct is not a call and does not reach anything. A
        // dotted callee is an extern named for its host type.
        int32_t index = find_called(graph, callee);
        if (index >= 0) {
            record_call(graph, function, (uint32_t)index, expr->span);
        }
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            walk_expr(graph, function, expr->call.args[i]);
        }
        break;
    }
    case KEST_EXPR_UNARY:
        walk_expr(graph, function, expr->unary.operand);
        break;
    case KEST_EXPR_BINARY:
        walk_expr(graph, function, expr->binary.left);
        walk_expr(graph, function, expr->binary.right);
        break;
    case KEST_EXPR_FIELD:
        walk_expr(graph, function, expr->field.object);
        break;
    case KEST_EXPR_INDEX:
        walk_expr(graph, function, expr->index.object);
        walk_expr(graph, function, expr->index.index);
        break;
    case KEST_EXPR_MATCH:
        for (uint32_t i = 0; i < expr->choose->subject_count; i++) {
            walk_expr(graph, function, expr->choose->subjects[i]);
        }
        for (uint32_t a = 0; a < expr->choose->arm_count; a++) {
            walk_expr(graph, function, expr->choose->arms[a].value);
            walk_block(graph, function, &expr->choose->arms[a].body);
        }
        break;
    case KEST_EXPR_IF:
        walk_expr(graph, function, expr->branch->condition);
        walk_expr(graph, function, expr->branch->then_value);
        walk_block(graph, function, &expr->branch->then_body);
        walk_expr(graph, function, expr->branch->otherwise);
        walk_expr(graph, function, expr->branch->else_value);
        walk_block(graph, function, &expr->branch->else_body);
        break;
    default:
        break;
    }
}

static void walk_stmt(Graph *graph, Function *function, const KestStmt *stmt) {
    switch (stmt->kind) {
    case KEST_STMT_LET:
        walk_expr(graph, function, stmt->let.value);
        break;
    case KEST_STMT_ASSIGN:
        walk_expr(graph, function, stmt->assign.target);
        walk_expr(graph, function, stmt->assign.value);
        break;
    case KEST_STMT_EXPR:
    case KEST_STMT_DEFER:
        // What is deferred still runs, so it counts against the promise.
        walk_expr(graph, function, stmt->value);
        break;
    case KEST_STMT_WHILE:
        walk_expr(graph, function, stmt->loop.condition);
        walk_block(graph, function, &stmt->loop.body);
        break;
    case KEST_STMT_FOR:
        walk_expr(graph, function, stmt->each->sequence);
        walk_expr(graph, function, stmt->each->until);
        walk_block(graph, function, &stmt->each->body);
        break;
    case KEST_STMT_RETURN:
        walk_expr(graph, function, stmt->result);
        break;
    case KEST_STMT_SCRATCH:
        // A block of working memory is a block that has some. A promise to
        // reach no heap has none to put back, so writing one inside such a
        // function is the promise being kept by a body that has nothing to do
        // with it — refused where it is written rather than allowed and
        // pointless. See D966.
        reaches(graph, function, stmt->span,
                "a `scratch { }` block is working memory, and a promise to "
                "reach no heap has none");
        walk_block(graph, function, &stmt->block);
        break;
    case KEST_STMT_BLOCK:
        walk_block(graph, function, &stmt->block);
        break;
    default:
        break;
    }
}

static void walk_block(Graph *graph, Function *function,
                       const KestBlock *block) {
    for (uint32_t i = 0; i < block->count; i++) {
        walk_stmt(graph, function, block->items[i]);
    }
}

// Follows the calls down to a body that allocates, collecting the names it
// went through. Reporting where the promise was made leaves the reader to
// walk the graph; reporting where the allocation is does not.
//
// As deep as the graph is. This was sixteen, and a promise broken further down
// than that was reported at the line that made it, saying `this allocates` of
// a body that allocates nothing. A path cannot be longer than the number of
// functions, because a function on it is not walked into twice.
typedef struct {
    const char **names;
    // Where each call is, and in which file, so the path is a place per hop
    // rather than a sentence.
    KestSpan *calls;
    uint32_t *units;
    uint32_t room;
    uint32_t count;
    KestSpan site;
    const char *why;
    // Set when the site is a call through a value rather than an allocation:
    // the shape the value is written as. What is wrong with it is not that it
    // allocates but that nothing says it does not.
    const char *shape;
    // Which file the site is in. A span alone does not say, and the body that
    // breaks a promise is often not in the file that made it.
    uint32_t unit;
    // Set when the path ends at a foreign function rather than at a body,
    // because then there is a declaration to point at rather than a line.
    bool ends_in_extern;
} Path;

static bool trace(Graph *graph, uint32_t index, Path *path) {
    Function *function = &graph->functions[index];
    if (function->visiting || path->count == path->room) {
        return false;
    }

    if (function->site.length > 0) {
        path->site = function->site;
        path->unit = function->unit;
        path->shape = function->shape;
        path->why = function->why;
        return true;
    }
    if (function->is_extern) {
        return false;
    }

    function->visiting = true;
    for (uint32_t i = 0; i < function->call_count; i++) {
        Function *callee = &graph->functions[function->callees[i]];
        if (!callee->allocates) {
            continue;
        }

        path->calls[path->count] = function->calls[i];
        path->units[path->count] = function->unit;
        path->names[path->count++] = callee->display;
        if (callee->is_extern) {
            path->site = function->calls[i];
            path->unit = function->unit;
            path->ends_in_extern = true;
            function->visiting = false;
            return true;
        }
        if (trace(graph, function->callees[i], path)) {
            function->visiting = false;
            return true;
        }
        path->count--;
    }
    function->visiting = false;
    return false;
}

static bool prove_promise(KestProgram *program, const KestUnits *units,
                          int about) {
    Graph graph = {0};
    graph.program = program;
    graph.about = about;

    for (uint32_t u = 0; u < units->count; u++) {
        for (uint32_t i = 0; i < units->items[u].unit.count; i++) {
            if (units->items[u].unit.items[i]->kind == KEST_DECL_FN) {
                graph.count++;
            }
        }
    }
    // And one for every copy of a generic, because a copy is what runs and a
    // declaration is not. The body is one tree that is typed again for each
    // set of types, so a declaration walked once is whichever copy happened to
    // be typed into it last -- and a call carries the copy's own name, so the
    // walk below looked for a node that was not there and read the call as
    // reaching nothing at all. That is how `check` accepted what `emit`
    // refused, and blamed the compiler for a mistake in the program. See D933.
    graph.count += program->instance_count;
    if (graph.count == 0) {
        return true;
    }
    graph.functions = KEST_ARENA_ARRAY(program->arena, Function, graph.count);
    if (graph.functions == NULL) {
        return false;
    }

    uint32_t next = 0;
    for (uint32_t u = 0; u < units->count; u++) {
      kest_program_in(program, &units->items[u]);
      const KestUnit *unit = &units->items[u].unit;
      for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FN) {
            continue;
        }
        Function *function = &graph.functions[next++];
        function->unit = u;
        function->decl = decl;
        // Named by the symbol the checker gave it, which includes what it
        // takes, because two functions may share a name. An extern is
        // declared under its receiver too, so that is where it is found.
        KestSpan where = decl->name;
        if (decl->function.receiver.length > 0) {
            where.offset = decl->function.receiver.offset;
            where.length = decl->name.offset + decl->name.length -
                           decl->function.receiver.offset;
        }
        KestSymbol *symbol = kest_symbol_at(program, program->source, where);
        if (symbol == NULL || symbol->type->symbol == NULL) {
            next--;
            graph.count--;
            continue;
        }
        function->name = symbol->type->symbol;
        function->display = symbol->name;
        function->promises = about == 2   ? decl->function.deterministic
                             : about == 1 ? decl->function.no_host
                                          : decl->function.no_alloc;
        function->is_extern = decl->function.is_extern;
        // A foreign body is not here to be read, so its promise is the only
        // thing there is to go on — and about the host there is nothing to go
        // on either way: a function the host provides is the host, whatever it
        // says about the heap.
        function->allocates =
            function->is_extern && (about == 1 || !function->promises);
        function->site = NO_SITE;
      }
    }

    // The copies. Each is the same declaration under another set of types, and
    // it is named by the symbol the call sites carry.
    uint32_t first_copy = next;
    for (uint32_t i = 0; i < program->instance_count; i++) {
        KestInstance *instance = &program->instances[i];
        if (instance->type == NULL || instance->symbol == NULL) {
            graph.count--;
            continue;
        }
        Function *function = &graph.functions[next++];
        memset(function, 0, sizeof *function);
        function->decl = instance->decl;
        function->name = instance->symbol;
        function->display = instance->symbol;
        function->promises =
            about == 2   ? instance->decl->function.deterministic
            : about == 1 ? instance->decl->function.no_host
                         : instance->decl->function.no_alloc;
        function->is_extern = instance->decl->function.is_extern;
        function->allocates =
            function->is_extern && (about == 1 || !function->promises);
        function->site = NO_SITE;
        function->unit = 0;
        for (uint32_t u = 0; u < units->count; u++) {
            if (&units->items[u] == instance->unit) {
                function->unit = u;
                break;
            }
        }
    }

    // Every node is made and named by here, which is where the table can be
    // built: a name is what a call is looked up by, and nothing is added
    // after. See D1087.
    if (!graph_index(&graph, program->arena)) {
        return false;
    }

    for (uint32_t i = 0; i < graph.count; i++) {
        Function *function = &graph.functions[i];
        // A generic declaration is read for what its own body reaches and not
        // for what it calls: the first is true whatever the types are, and the
        // second is settled per copy. Without it a promise on a generic that
        // nothing instantiates was proved against nothing at all.
        graph.a_template = i < first_copy &&
                           function->decl->type_param_count > 0;
        kest_program_in(program, &units->items[function->unit]);
        // The tree is typed for this copy before it is read, which is the
        // whole of what makes the answer that copy's own.
        if (i >= first_copy) {
            kest_retype_instance(program, &program->instances[i - first_copy]);
        }
        if (!function->is_extern) {
            walk_block(&graph, function, &function->decl->function.body);
        }
        if (graph.out_of_memory) {
            return false;
        }
    }

    // Allocation spreads up the call graph until nothing changes, which is
    // what makes a promise about a whole call tree rather than one body.
    bool moved = true;
    while (moved) {
        moved = false;
        for (uint32_t i = 0; i < graph.count; i++) {
            Function *function = &graph.functions[i];
            if (function->allocates) {
                continue;
            }
            for (uint32_t c = 0; c < function->call_count; c++) {
                if (graph.functions[function->callees[c]].allocates) {
                    function->allocates = true;
                    moved = true;
                    break;
                }
            }
        }
    }

    // What the walk found, written back where a reader can be told it. A body
    // that reaches nothing and promises nothing is a promise somebody could
    // make, and `kest check --cost` is where that is said. A generic is
    // written once per copy of it and the copies are OR-ed, because a copy is
    // what runs. See D976.
    for (uint32_t i = 0; i < graph.count; i++) {
        Function *function = &graph.functions[i];
        if (function->decl == NULL || !function->allocates) {
            continue;
        }
        KestDecl *written = (KestDecl *)function->decl;
        if (about == 0) {
            written->function.reaches_heap = true;
        } else if (about == 1) {
            written->function.reaches_host = true;
        } else {
            written->function.not_deterministic = true;
        }
    }

    for (uint32_t i = 0; i < graph.count; i++) {
        Function *function = &graph.functions[i];
        if (!function->promises || !function->allocates ||
            function->is_extern) {
            continue;
        }

        Path path = {0};
        path.room = graph.count;
        path.names =
            KEST_ARENA_ARRAY(program->arena, const char *, graph.count);
        path.calls = KEST_ARENA_ARRAY(program->arena, KestSpan, graph.count);
        path.units = KEST_ARENA_ARRAY(program->arena, uint32_t, graph.count);
        if (path.names == NULL || path.calls == NULL || path.units == NULL) {
            return false;
        }
        path.unit = function->unit;
        if (!trace(&graph, i, &path)) {
            path.site = function->decl->name;
        }
        const char *promise = about == 2   ? "deterministic"
                              : about == 1 ? "no.host"
                                           : "no.alloc";
        kest_program_in(program, &units->items[path.unit]);
        kest_diags_in(program->diags, program->source);

        if (path.shape != NULL) {
            // Not that it allocates: that nothing says it does not. The fix
            // is in the shape the value is written as, which is where a
            // promise about a body nobody can see has to live.
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0402",
                           path.site,
                           "nothing promises about what this calls, and `%s` "
                           "promises `%s`",
                           function->display, promise);
            kest_diags_suggest(program->diags,
                               "write the promise into the shape: `%s %s`",
                               path.shape, promise);
        } else if (about == 2) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0401",
                           path.site,
                           "this reaches outside the simulation profile, and "
                           "`%s` promises `deterministic`",
                           function->display);
        } else if (about == 1) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0401",
                           path.site,
                           "this calls the host, and `%s` promises `no.host`",
                           function->display);
        } else {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0401",
                           path.site,
                           "this allocates, and `%s` promises `no.alloc`",
                           function->display);
            if (path.why != NULL) {
                kest_diags_suggest(program->diags, "%s", path.why);
            }
        }

        if (path.ends_in_extern) {
            kest_diags_suggest(program->diags,
                               about != 0 ? "`%s` is the host's"
                                          : "`%s` is declared to allocate",
                               path.names[path.count - 1]);
        }

        // Every other place this body breaks it, before the promise, so the
        // allocations read as one list. A path through calls stays a chain
        // because there the chain is the story; a body is a list. See D509.
        if (path.shape == NULL && path.count == 0 && path.why != NULL &&
            path.site.offset == function->site.offset) {
            for (uint32_t n = 0; n < function->site_count; n++) {
                uint32_t left = function->site_count - n - 1 + function->more;
                if (n + 1 == MORE_SITES && left > 0) {
                    kest_diags_note(program->diags,
                                    &units->items[function->unit].source,
                                    function->sites[n],
                                    "and here, and %u more place%s", left,
                                    left == 1 ? "" : "s");
                    break;
                }
                // Saying the same reason under every one of them is noise;
                // saying a different one is the whole point of saying any.
                if (strcmp(function->whys[n], path.why) == 0) {
                    kest_diags_note(program->diags,
                                    &units->items[function->unit].source,
                                    function->sites[n], "and here");
                } else {
                    kest_diags_note(program->diags,
                                    &units->items[function->unit].source,
                                    function->sites[n], "and here: %s",
                                    function->whys[n]);
                }
            }
        }

        // The promise first, then the calls under it in the order they are
        // made, so the chain reads forwards from the thing that was promised
        // to the thing that breaks it.
        kest_diags_note(program->diags, &units->items[function->unit].source,
                        function->decl->name, "`%s` promises it here",
                        function->display);
        uint32_t hops = path.ends_in_extern ? path.count - 1 : path.count;
        // One note is the promise, so the rest of the room is the path. A
        // chain longer than that is shown from the promise down, and the last
        // note there is room for counts what is under it: a path that stops
        // without saying so reads as a path that ended.
        uint32_t room = KEST_MOST_PLACES - 1;
        for (uint32_t n = 0; n < hops && n < room; n++) {
            uint32_t left = hops - n - 1;
            if (n + 1 == room && left > 0) {
                kest_diags_note(program->diags,
                                &units->items[path.units[n]].source,
                                path.calls[n],
                                "which calls `%s`, and %u call%s under that",
                                path.names[n], left, left == 1 ? "" : "s");
                break;
            }
            kest_diags_note(program->diags, &units->items[path.units[n]].source,
                            path.calls[n], "which calls `%s`",
                            path.names[n]);
        }
    }
    return true;
}

bool kest_check_contracts(KestProgram *program, const KestUnits *units) {
    // Once for each promise. Two walks over one graph rather than one walk
    // answering two questions: a body that breaks one and keeps the other has
    // one thing wrong with it, and a reader wants that one said on its own.
    // Both are run whatever the first says, because a program with two
    // promises broken has two things to fix and finding out about the second
    // one build later is what a compiler that reports everything is for.
    bool held = prove_promise(program, units, 0);
    bool crossing = prove_promise(program, units, 1);
    return prove_promise(program, units, 2) && crossing && held;
}
