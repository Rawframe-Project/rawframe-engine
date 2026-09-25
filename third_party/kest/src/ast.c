#include "ast.h"

#include <stdio.h>

bool kest_resumes_spans(const KestSource *source, const KestDecl *decl,
                        KestSpan *param, KestSpan *field) {
    if (decl->kind != KEST_DECL_FN || decl->function.resumes == 0 ||
        source == NULL) {
        return false;
    }
    // Read by the lexer rather than by looking at the bytes, because between
    // the two names there may be anything a line may hold: a line ended after
    // the dot, a comment. Only a function that waits is read this way.
    KestArena *arena = kest_arena_new();
    if (arena == NULL) {
        return false;
    }
    KestDiags quiet;
    kest_diags_init(&quiet, arena);
    uint32_t count = 0;
    KestToken *tokens =
        kest_lex_again(arena, source, &quiet, decl->function.resumes - 1,
                       (uint32_t)source->length, &count);
    KestSpan names[2] = {{0, 0}, {0, 0}};
    uint32_t found = 0;
    bool dotted = false;
    for (uint32_t i = 0; tokens != NULL && i < count && found < 2; i++) {
        if (tokens[i].kind == KEST_TOK_NEWLINE) {
            continue;
        }
        if (tokens[i].kind == KEST_TOK_IDENT && (found == 0 || dotted)) {
            names[found++] = tokens[i].span;
            continue;
        }
        if (tokens[i].kind == KEST_TOK_DOT && found == 1 && !dotted) {
            dotted = true;
            continue;
        }
        break;
    }
    kest_arena_free(arena);
    *param = names[0];
    *field = names[1];
    return found == 2;
}

// What a generic may ask of a type it is given. Two, and each is a thing this
// language itself provides for a value laid out flat: two of them are equal or
// they are not, and some of them have an order. There is no third for `hash`,
// because `hash` stands for what compares and applies exactly where `==` does
// -- a word for it would be a second spelling of the first. Nothing here is a
// thing a program declares for a type of its own. See D1043.
static const KestCapabilityName CAPABILITIES[KEST_CAPABILITY_COUNT] = {
    {"compares", KEST_WANTS_COMPARES},
    {"orders", KEST_WANTS_ORDERS},
};

const KestCapabilityName *kest_capability(uint32_t at) {
    return at < KEST_CAPABILITY_COUNT ? &CAPABILITIES[at] : &CAPABILITIES[0];
}

void kest_capability_list(char *out, size_t room) {
    size_t written = 0;
    for (uint32_t at = 0; at < KEST_CAPABILITY_COUNT && written + 1 < room;
         at++) {
        const char *between = at == 0                        ? ""
                              : at + 1 == KEST_CAPABILITY_COUNT ? " and "
                                                                : ", ";
        int said = snprintf(out + written, room - written, "%s`%s`", between,
                            CAPABILITIES[at].word);
        if (said < 0) {
            break;
        }
        written += (size_t)said;
    }
}

static void indent(FILE *out, int depth) {
    fprintf(out, "%*s", depth * 2, "");
}

static void print_span(const KestSource *source, KestSpan span, FILE *out) {
    fprintf(out, "%.*s", (int)span.length, kest_span_text(source, span));
}

static void print_op(KestTokenKind kind, FILE *out) {
    char bare[KEST_TOKEN_NAME_ROOM];
    fputs(kest_token_bare(kind, bare, sizeof(bare)), out);
}

static void print_type(const KestTypeRef *type, const KestSource *source,
                       FILE *out) {
    if (type == NULL) {
        fputs("?", out);
        return;
    }
    switch (type->kind) {
    case KEST_TYPE_NAMED:
        print_span(source, type->name, out);
        break;
    case KEST_TYPE_GENERIC:
        print_span(source, type->name, out);
        fputc('<', out);
        for (uint32_t i = 0; i < type->arg_count; i++) {
            if (i > 0) {
                fputs(", ", out);
            }
            print_type(type->args[i], source, out);
        }
        fputc('>', out);
        break;
    case KEST_TYPE_ARRAY:
        fputc('[', out);
        print_type(type->element, source, out);
        if (type->count.length > 0) {
            fputs("; ", out);
            print_span(source, type->count, out);
        }
        fputc(']', out);
        break;
    case KEST_TYPE_FN:
        fputs(type->block ? "block(" : "fn(", out);
        for (uint32_t i = 0; i < type->arg_count; i++) {
            fputs(i == 0 ? "" : ", ", out);
            print_type(type->args[i], source, out);
        }
        fputc(')', out);
        if (type->element != NULL) {
            fputs(" -> ", out);
            print_type(type->element, source, out);
        }
        if (type->no_alloc) {
            fputs(" no.alloc", out);
        }
        if (type->no_host) {
            fputs(" no.host", out);
        }
        break;
    case KEST_TYPE_OPTIONAL:
        print_type(type->element, source, out);
        fputc('?', out);
        break;
    }
}

static void print_block(const KestBlock *block, const KestSource *source,
                        int depth, FILE *out);

static void print_expr(const KestExpr *expr, const KestSource *source,
                       int depth, FILE *out) {
    if (expr == NULL) {
        fputs("<error>", out);
        return;
    }
    switch (expr->kind) {
    case KEST_EXPR_INT:
    case KEST_EXPR_FLOAT:
    case KEST_EXPR_NAME:
    case KEST_EXPR_STRING:
    case KEST_EXPR_BYTE:
        print_span(source, expr->span, out);
        break;
    case KEST_EXPR_BOOL:
        fputs(expr->boolean ? "true" : "false", out);
        break;
    case KEST_EXPR_NONE:
        fputs("none", out);
        break;
    case KEST_EXPR_TEXT:
        fputs("(text", out);
        for (uint32_t i = 0; i < expr->text.count; i++) {
            fputc(' ', out);
            if (expr->text.parts[i].value != NULL) {
                print_expr(expr->text.parts[i].value, source, depth, out);
            } else {
                fputc('"', out);
                print_span(source, expr->text.parts[i].text, out);
                fputc('"', out);
            }
        }
        fputc(')', out);
        break;
    case KEST_EXPR_UNARY:
        fputc('(', out);
        print_op(expr->unary.op, out);
        fputc(' ', out);
        print_expr(expr->unary.operand, source, depth, out);
        fputc(')', out);
        break;
    case KEST_EXPR_BINARY:
        fputc('(', out);
        print_op(expr->binary.op, out);
        fputc(' ', out);
        print_expr(expr->binary.left, source, depth, out);
        fputc(' ', out);
        print_expr(expr->binary.right, source, depth, out);
        fputc(')', out);
        break;
    case KEST_EXPR_CALL:
        fputs("(call ", out);
        print_expr(expr->call.callee, source, depth, out);
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            fputc(' ', out);
            print_expr(expr->call.args[i], source, depth, out);
        }
        fputc(')', out);
        break;
    case KEST_EXPR_FIELD:
        fputs("(. ", out);
        print_expr(expr->field.object, source, depth, out);
        fputc(' ', out);
        print_span(source, expr->field.name, out);
        fputc(')', out);
        break;
    case KEST_EXPR_ARRAY:
        fputs("(array", out);
        for (uint32_t i = 0; i < expr->array.count; i++) {
            fputc(' ', out);
            print_expr(expr->array.items[i], source, depth, out);
        }
        fputc(')', out);
        break;
    case KEST_EXPR_BLOCK: {
        const KestLambda *lambda = expr->lambda;
        fputs("(handed |", out);
        for (uint32_t i = 0; i < lambda->param_count; i++) {
            fputs(i == 0 ? "" : ", ", out);
            print_span(source, lambda->params[i], out);
        }
        fputs("|", out);
        if (lambda->value != NULL) {
            fputc(' ', out);
            print_expr(lambda->value, source, depth, out);
        } else {
            fputc('\n', out);
            print_block(&lambda->body, source, depth + 1, out);
            indent(out, depth);
        }
        fputc(')', out);
        break;
    }
    case KEST_EXPR_IF: {
        const KestBranch *branch = expr->branch;
        fputs("(if ", out);
        if (branch->binding.length > 0) {
            fputs("let ", out);
            print_span(source, branch->binding, out);
            fputc(' ', out);
        }
        print_expr(branch->condition, source, depth, out);
        if (branch->then_value != NULL) {
            fputs(" -> ", out);
            print_expr(branch->then_value, source, depth, out);
        } else {
            // What is in it and not how much of it. This said how many
            // statements were in an arm, so two programs that differ in what
            // an `if` does had one tree — and what says a formatted file means
            // the same is this tree. See D447.
            fputc('\n', out);
            print_block(&branch->then_body, source, depth + 1, out);
            indent(out, depth);
        }
        if (branch->otherwise != NULL) {
            fputs(" else ", out);
            print_expr(branch->otherwise, source, depth, out);
        } else if (branch->has_else) {
            fputs(" else", out);
            if (branch->else_value != NULL) {
                fputs(" -> ", out);
                print_expr(branch->else_value, source, depth, out);
            } else {
                fputc('\n', out);
                print_block(&branch->else_body, source, depth + 1, out);
                indent(out, depth);
            }
        }
        fputc(')', out);
        break;
    }
    case KEST_EXPR_MATCH:
        fputs("(match", out);
        for (uint32_t i = 0; i < expr->choose->subject_count; i++) {
            fputc(' ', out);
            print_expr(expr->choose->subjects[i], source, depth, out);
        }
        for (uint32_t i = 0; i < expr->choose->arm_count; i++) {
            const KestArm *arm = &expr->choose->arms[i];
            fputs(" (", out);
            for (uint32_t p = 0; p < arm->part_count; p++) {
                const KestArmPart *part = &arm->parts[p];
                fputs(p == 0 ? "" : " ", out);
                if (part->name.length == 0) {
                    fputs("else", out);
                } else {
                    print_span(source, part->name, out);
                }
                for (uint32_t b = 0; b < part->binding_count; b++) {
                    fputc(' ', out);
                    print_span(source, part->bindings[b], out);
                }
            }
            if (arm->value != NULL) {
                fputs(" -> ", out);
                print_expr(arm->value, source, depth, out);
            } else {
                // The same about an arm that is a block.
                fputc('\n', out);
                print_block(&arm->body, source, depth + 1, out);
                indent(out, depth);
            }
            fputc(')', out);
        }
        fputc(')', out);
        break;
    case KEST_EXPR_INDEX:
        fputs("(index ", out);
        print_expr(expr->index.object, source, depth, out);
        fputc(' ', out);
        print_expr(expr->index.index, source, depth, out);
        fputc(')', out);
        break;
    }
}

static void print_block(const KestBlock *block, const KestSource *source,
                        int depth, FILE *out);

static void print_stmt(const KestStmt *stmt, const KestSource *source,
                       int depth, FILE *out) {
    indent(out, depth);
    switch (stmt->kind) {
    case KEST_STMT_LET:
        fputs("(let ", out);
        print_span(source, stmt->let.name, out);
        fputs(" : ", out);
        print_type(stmt->let.type, source, out);
        fputc(' ', out);
        print_expr(stmt->let.value, source, depth, out);
        fputs(")\n", out);
        break;
    case KEST_STMT_ASSIGN:
        fputc('(', out);
        print_op(stmt->assign.op, out);
        fputc(' ', out);
        print_expr(stmt->assign.target, source, depth, out);
        fputc(' ', out);
        print_expr(stmt->assign.value, source, depth, out);
        fputs(")\n", out);
        break;
    case KEST_STMT_EXPR:
        print_expr(stmt->value, source, depth, out);
        fputc('\n', out);
        break;
    case KEST_STMT_DEFER:
        fputs("(defer ", out);
        print_expr(stmt->value, source, depth, out);
        fputs(")\n", out);
        break;
    case KEST_STMT_WHILE:
        fputs("(while ", out);
        if (stmt->loop.binding.length > 0) {
            fputs("let ", out);
            print_span(source, stmt->loop.binding, out);
            fputc(' ', out);
        }
        print_expr(stmt->loop.condition, source, depth, out);
        fputc('\n', out);
        print_block(&stmt->loop.body, source, depth + 1, out);
        indent(out, depth);
        fputs(")\n", out);
        break;
    case KEST_STMT_FOR:
        fputs("(for ", out);
        if (stmt->each->index.length > 0) {
            print_span(source, stmt->each->index, out);
            fputs(", ", out);
        }
        print_span(source, stmt->each->name, out);
        fputs(" in ", out);
        print_expr(stmt->each->sequence, source, depth, out);
        if (stmt->each->until != NULL) {
            fputs(" .. ", out);
            print_expr(stmt->each->until, source, depth, out);
        }
        fputc('\n', out);
        print_block(&stmt->each->body, source, depth + 1, out);
        indent(out, depth);
        fputs(")\n", out);
        break;
    case KEST_STMT_RETURN:
        fputs("(return", out);
        if (stmt->result != NULL) {
            fputc(' ', out);
            print_expr(stmt->result, source, depth, out);
        }
        fputs(")\n", out);
        break;
    case KEST_STMT_BREAK:
        fputs("(break)\n", out);
        break;
    case KEST_STMT_WAIT:
        fputs("(wait ", out);
        print_span(source, stmt->wait.name, out);
        fputs(")\n", out);
        break;
    case KEST_STMT_CONTINUE:
        fputs("(continue)\n", out);
        break;
    case KEST_STMT_SCRATCH:
        fputs("(scratch\n", out);
        print_block(&stmt->block, source, depth + 1, out);
        indent(out, depth);
        fputs(")\n", out);
        break;
    case KEST_STMT_BLOCK:
        fputs("(block\n", out);
        print_block(&stmt->block, source, depth + 1, out);
        indent(out, depth);
        fputs(")\n", out);
        break;
    }
}

static void print_block(const KestBlock *block, const KestSource *source,
                        int depth, FILE *out) {
    for (uint32_t i = 0; i < block->count; i++) {
        print_stmt(block->items[i], source, depth, out);
    }
}

static void print_type_params(const KestDecl *decl, const KestSource *source,
                              FILE *out) {
    for (uint32_t i = 0; i < decl->type_param_count; i++) {
        fputs(i == 0 ? " <" : " ", out);
        print_span(source, decl->type_params[i].name, out);
        if (i + 1 == decl->type_param_count) {
            fputc('>', out);
        }
    }
}

static void print_decl(const KestDecl *decl, const KestSource *source,
                       FILE *out) {
    switch (decl->kind) {
    case KEST_DECL_MODULE:
        fputs("(module ", out);
        print_span(source, decl->name, out);
        fputs(")\n", out);
        break;
    case KEST_DECL_IMPORT:
        fputs("(import ", out);
        print_span(source, decl->name, out);
        fputs(")\n", out);
        break;
    case KEST_DECL_CONST:
        fputs("(const ", out);
        print_span(source, decl->name, out);
        fputs(" : ", out);
        print_type(decl->constant.type, source, out);
        fputc(' ', out);
        print_expr(decl->constant.value, source, 0, out);
        fputs(")\n", out);
        break;
    case KEST_DECL_STRUCT:
        fputs("(struct ", out);
        print_span(source, decl->name, out);
        print_type_params(decl, source, out);
        fputc('\n', out);
        for (uint32_t i = 0; i < decl->record.field_count; i++) {
            indent(out, 1);
            fputs("(field ", out);
            print_span(source, decl->record.fields[i]->name, out);
            fputc(' ', out);
            print_type(decl->record.fields[i]->type, source, out);
            fputs(")\n", out);
        }
        fputs(")\n", out);
        break;
    case KEST_DECL_FLAGS:
        fputs("(flags ", out);
        print_span(source, decl->name, out);
        fputc(' ', out);
        print_type(decl->choice.width, source, out);
        for (uint32_t i = 0; i < decl->choice.case_count; i++) {
            fputc(' ', out);
            print_span(source, decl->choice.cases[i]->name, out);
        }
        fputs(")\n", out);
        break;
    case KEST_DECL_ENUM:
        fputs("(enum ", out);
        print_span(source, decl->name, out);
        fputc('\n', out);
        for (uint32_t i = 0; i < decl->choice.case_count; i++) {
            indent(out, 1);
            fputs("(case ", out);
            print_span(source, decl->choice.cases[i]->name, out);
            for (uint32_t p = 0; p < decl->choice.cases[i]->payload_count; p++) {
                fputc(' ', out);
                print_type(decl->choice.cases[i]->payload[p], source, out);
            }
            fputs(")\n", out);
        }
        fputs(")\n", out);
        break;
    case KEST_DECL_FN:
        fputs(decl->function.is_extern ? "(extern fn " : "(fn ", out);
        if (decl->function.receiver.length > 0) {
            print_span(source, decl->function.receiver, out);
            fputc('.', out);
        }
        print_span(source, decl->name, out);
        print_type_params(decl, source, out);
        KestSpan resumed;
        KestSpan resumed_field;
        if (kest_resumes_spans(source, decl, &resumed, &resumed_field)) {
            fputs(" resumes ", out);
            print_span(source, resumed, out);
            fputc('.', out);
            print_span(source, resumed_field, out);
        }
        if (decl->function.no_alloc) {
            fputs(" no.alloc", out);
        }
        if (decl->function.no_host) {
            fputs(" no.host", out);
        }
        fputc('\n', out);
        for (uint32_t i = 0; i < decl->function.param_count; i++) {
            indent(out, 1);
            fputs("(param ", out);
            print_span(source, decl->function.params[i]->name, out);
            fputc(' ', out);
            print_type(decl->function.params[i]->type, source, out);
            fputs(")\n", out);
        }
        if (decl->function.result != NULL) {
            indent(out, 1);
            fputs("(result ", out);
            print_type(decl->function.result, source, out);
            fputs(")\n", out);
        }
        if (!decl->function.is_extern) {
            print_block(&decl->function.body, source, 1, out);
        }
        fputs(")\n", out);
        break;
    }
}

void kest_ast_dump(const KestUnit *unit, const KestSource *source, FILE *out) {
    // A file that declares nothing has a tree with nothing in it, and printing
    // nothing is what a command that did not run looks like.
    if (unit->count == 0) {
        fputs("// this file declares nothing\n", out);
        return;
    }
    for (uint32_t i = 0; i < unit->count; i++) {
        print_decl(unit->items[i], source, out);
    }
}
