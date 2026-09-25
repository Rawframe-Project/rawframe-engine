#include "verify.h"

#include <stdio.h>
#include <string.h>

// Which chunk first reaches the heap, following calls, or -1. `where` is left
// at the instruction that does it.
// Where a promise is broken in what was emitted, and which chunk it is in. The
// same walk for both promises, because the thing that differs is one
// instruction: what reaches the heap is a list of them, and what reaches the
// host is `KEST_OP_CALL_HOST` and nothing else. See D853.
static int32_t breaks_in(const KestModule *module, uint32_t which,
                         uint8_t *state, uint32_t *where, int about) {
    if (state[which] != 0) {
        return -1;
    }
    state[which] = 1;

    const KestChunk *chunk = module->functions[which];
    for (uint32_t at = 0; at < chunk->code_count;) {
        uint8_t op = chunk->code[at];
        if (about == 0 && kest_op_allocates(op)) {
            *where = at;
            return (int32_t)which;
        }
        if (about == 1 && op == KEST_OP_CALL_HOST) {
            *where = at;
            return (int32_t)which;
        }
        // And a crossing out of the simulation profile, which is a crossing to
        // a door that does not promise it is inside one. `no.host` refuses
        // every crossing; this refuses the ones nobody has vouched for.
        // See D942.
        if (about == 2 && op == KEST_OP_CALL_HOST) {
            uint16_t door = kest_chunk_u16(chunk, at + 1);
            if (door >= module->extern_count ||
                !module->externs[door].deterministic) {
                *where = at;
                return (int32_t)which;
            }
        }
        if (op == KEST_OP_CALL) {
            uint16_t callee = kest_chunk_u16(chunk, at + 1);
            if (callee < module->count) {
                int32_t found =
                    breaks_in(module, callee, state, where, about);
                if (found >= 0) {
                    return found;
                }
            }
        }
        at += kest_op_wide(op);
    }
    return -1;
}

static bool starts_at(const uint8_t *starts, uint32_t where) {
    return ((unsigned)starts[where / 8] >> (where % 8)) & 1u;
}

// Whether every number every instruction of a body carries is one the body or
// the module has, and every jump lands where an instruction starts: what the
// machine reads without asking, proved before it runs. Answers the code and
// says what was wrong into `said`, or answers NULL. A body whose instructions
// cannot be told apart has already been refused by the walk above this, which
// is what makes the table of where each one starts worth building. See D1237.
static const char *names_only_what_is_there(const KestModule *module,
                                            const KestChunk *chunk,
                                            uint8_t *starts, char *said,
                                            size_t room) {
    memset(starts, 0, ((size_t)chunk->code_count + 8) / 8);
    for (uint32_t at = 0; at < chunk->code_count; at += kest_op_wide(chunk->code[at])) {
        starts[at / 8] = (uint8_t)(starts[at / 8] | (1u << (at % 8)));
    }
    for (uint32_t at = 0; at < chunk->code_count;) {
        uint8_t op = chunk->code[at];
        uint32_t wide = kest_op_wide(op);
        const char *name = kest_op_name(op);
        uint32_t previous = 0;
        for (uint32_t k = 0; k < (wide - 1) / 2; k++) {
            uint32_t value = kest_chunk_u16(chunk, at + 1 + 2 * k);
            const char *wrong = NULL;
            uint32_t has = 0;
            switch (kest_op_operand(op, k)) {
            case KEST_OPERAND_NUMBER:
                break;
            case KEST_OPERAND_SLOT:
                has = chunk->slot_count;
                wrong = value < has ? NULL : "slot";
                break;
            case KEST_OPERAND_SLOT_RUN:
                has = chunk->slot_count;
                wrong = previous + value <= has ? NULL : "run of slots";
                break;
            case KEST_OPERAND_CONSTANT:
                has = chunk->constant_count;
                wrong = value < has ? NULL : "constant";
                break;
            case KEST_OPERAND_CONSTANT_RUN:
                has = chunk->constant_count;
                wrong = previous + value <= has ? NULL : "run of constants";
                break;
            case KEST_OPERAND_FUNCTION:
                has = module->count;
                wrong = value < has ? NULL : "function";
                break;
            case KEST_OPERAND_EXTERN:
                has = module->extern_count;
                wrong = value < has ? NULL : "door of the host";
                break;
            case KEST_OPERAND_LAYOUT:
                has = module->layout_count;
                wrong = value < has ? NULL : "layout";
                break;
            case KEST_OPERAND_FORWARD: {
                uint32_t lands = at + wide + value;
                if (lands >= chunk->code_count || !starts_at(starts, lands)) {
                    snprintf(said, room,
                             "`%s` at %u jumps to %u, where no instruction "
                             "starts",
                             name, at, lands);
                    return "K0409";
                }
                break;
            }
            case KEST_OPERAND_BACKWARD:
                if (value > at + wide || !starts_at(starts, at + wide - value)) {
                    snprintf(said, room,
                             "`%s` at %u jumps back %u, to where no "
                             "instruction starts",
                             name, at, value);
                    return "K0409";
                }
                break;
            }
            if (wrong != NULL) {
                snprintf(said, room,
                         "`%s` at %u names %s %u of the %u there are",
                         name, at, wrong,
                         kest_op_operand(op, k) == KEST_OPERAND_SLOT_RUN ||
                                 kest_op_operand(op, k) == KEST_OPERAND_CONSTANT_RUN
                             ? previous + value
                             : value,
                         has);
                return "K0408";
            }
            previous = value;
        }
        // Three read a run with a stride between its pieces, and what they
        // read last is the first plus the stride times the count.
        if (op == KEST_OP_CONST_AT || op == KEST_OP_LOAD_SLOTS ||
            op == KEST_OP_STORE_SLOTS) {
            uint32_t first = kest_chunk_u16(chunk, at + 1);
            uint32_t reach = first + kest_chunk_u16(chunk, at + 3) * kest_chunk_u16(chunk, at + 5);
            uint32_t has = op == KEST_OP_CONST_AT ? chunk->constant_count
                                                  : chunk->slot_count;
            if (reach > has) {
                snprintf(said, room,
                         "`%s` at %u reads to %u of the %u there are",
                         name, at, reach, has);
                return "K0408";
            }
        }
        at += wide;
    }
    return NULL;
}

// Where the operand stack stands at every instruction a body can reach, by
// walking every path from the first: an instruction is reached at one depth
// or it is refused, never takes more than is there, never leaves more than the
// body was given room for, and a `return` hands back exactly what the
// declaration says with nothing under it. The machine moves `top` without
// asking any of that, so this is what makes an operand an operand. What cannot
// be reached is not walked, because nothing runs it. See D1239.
static const char *stack_on_every_path(const KestModule *module,
                                       const KestChunk *chunk,
                                       uint16_t *depth, uint32_t *work,
                                       char *said, size_t room) {
    const uint16_t unknown = UINT16_MAX;
    for (uint32_t at = 0; at < chunk->code_count; at++) {
        depth[at] = unknown;
    }
    uint32_t waiting = 0;
    if (chunk->code_count > 0) {
        depth[0] = 0;
        work[waiting++] = 0;
    }
    while (waiting > 0) {
        uint32_t at = work[--waiting];
        while (true) {
            uint8_t op = chunk->code[at];
            uint32_t wide = kest_op_wide(op);
            const char *name = kest_op_name(op);
            uint32_t here = depth[at];
            uint32_t takes = 0;
            uint32_t gives = 0;
            const char *why = kest_op_stack(module, chunk, at, &takes, &gives);
            if (why != NULL) {
                snprintf(said, room, "`%s` at %u %s", name, at,
                         why);
                return "K0410";
            }
            if (takes > here) {
                snprintf(said, room,
                         "`%s` at %u takes %u slot(s) and %u are there",
                         name, at, takes, here);
                return "K0410";
            }
            uint32_t after = here - takes + gives;
            if (after > chunk->stack_needed) {
                snprintf(said, room,
                         "`%s` at %u leaves %u slot(s) where the body was "
                         "given room for %u",
                         name, at, after, chunk->stack_needed);
                return "K0410";
            }
            if (op == KEST_OP_RETURN) {
                if (here != takes || takes != chunk->result_slots) {
                    snprintf(said, room,
                             "`return` at %u gives back %u slot(s) of %u "
                             "where the declaration gives %u",
                             at, takes, here, chunk->result_slots);
                    return "K0410";
                }
                break;
            }
            if (op == KEST_OP_STOP) {
                break;
            }
            // Where it may go besides the next instruction, and whether the
            // next is one of the places at all.
            uint32_t lands = UINT32_MAX;
            bool falls = op != KEST_OP_JUMP && op != KEST_OP_LOOP;
            for (uint32_t k = 0; k < (wide - 1) / 2; k++) {
                uint32_t value = kest_chunk_u16(chunk, at + 1 + 2 * k);
                if (kest_op_operand(op, k) == KEST_OPERAND_FORWARD) {
                    lands = at + wide + value;
                } else if (kest_op_operand(op, k) == KEST_OPERAND_BACKWARD) {
                    lands = at + wide - value;
                }
            }
            if (lands != UINT32_MAX) {
                if (depth[lands] == unknown) {
                    depth[lands] = (uint16_t)after;
                    work[waiting++] = lands;
                } else if (depth[lands] != after) {
                    snprintf(said, room,
                             "`%s` at %u arrives at %u with %u slot(s) where "
                             "another way arrives with %u",
                             name, at, lands, after,
                             depth[lands]);
                    return "K0410";
                }
            }
            if (!falls) {
                break;
            }
            uint32_t next = at + wide;
            if (next >= chunk->code_count) {
                snprintf(said, room, "`%s` at %u runs off the end",
                         name, at);
                return "K0410";
            }
            if (depth[next] != unknown) {
                if (depth[next] != after) {
                    snprintf(said, room,
                             "`%s` at %u arrives at %u with %u slot(s) where "
                             "another way arrives with %u",
                             name, at, next, after, depth[next]);
                    return "K0410";
                }
                break;
            }
            depth[next] = (uint16_t)after;
            at = next;
        }
    }
    return NULL;
}

bool kest_module_prove(const KestModule *module, KestArena *arena,
                       KestDiags *diags) {
    if (module->count == 0) {
        return true;
    }
    uint8_t *state = kest_arena_alloc(arena, module->count, 1);
    if (state == NULL) {
        return false;
    }

    // What the walks below keep about one body -- where each instruction
    // starts, the depth of the stack at each, and the places still to walk
    // from -- sized for the largest body and in an arena of the verifier's
    // own, given back at the end. It is not the build's, because what proving
    // takes out of a build is what a build given exactly what it costs does
    // not have: `examples/embed.c` builds a program inside what it cost a
    // moment before, and was refused 4,100 bytes short. See D1237.
    uint32_t largest = 1;
    for (uint32_t i = 0; i < module->count; i++) {
        if (module->functions[i]->code_count > largest) {
            largest = module->functions[i]->code_count;
        }
    }
    KestArena *scratch = kest_arena_new();
    uint8_t *starts = NULL;
    uint16_t *depth = NULL;
    uint32_t *work = NULL;
    if (scratch != NULL) {
        starts = kest_arena_alloc(scratch, ((size_t)largest + 8) / 8, 1);
        depth = KEST_ARENA_ARRAY(scratch, uint16_t, largest);
        work = KEST_ARENA_ARRAY(scratch, uint32_t, largest);
        if (depth == NULL || work == NULL) {
            starts = NULL;
        }
    }

    bool held = true;

    // Every chunk has to be walkable, which means that stepping by what each
    // instruction says it takes lands exactly on the end. A width that is
    // wrong for one instruction puts everything after it out of step, and a
    // walk that reads the middle of an instruction as an instruction is how
    // half the calls in a program went unseen once (D057).
    uint32_t known = (uint32_t)KEST_OP_STOP + 1;
    for (uint32_t i = 0; i < module->count; i++) {
        const KestChunk *chunk = module->functions[i];
        uint32_t at = 0;
        uint8_t last = KEST_OP_RETURN;
        const char *wrong = NULL;
        // A `return` may give back less than the function says, because the
        // one written past the end of a body gives nothing and is there for a
        // body that falls off it. More is what a host would read out of its
        // frame past the end, so it is the direction that is held.
        int32_t gives = -1;
        while (at < chunk->code_count) {
            last = chunk->code[at];
            if (last >= known) {
                wrong = "lands on something that is not an instruction";
                break;
            }
            if (last == KEST_OP_RETURN && gives < 0) {
                uint16_t count = kest_chunk_u16(chunk, at + 1);
                if (count > chunk->result_slots) {
                    gives = count;
                }
            }
            at += kest_op_wide(last);
        }
        if (wrong == NULL && at != chunk->code_count) {
            wrong = "steps past the end";
        }
        // Every chunk ends in a return, so a walk that ends anywhere else
        // stepped through the middle of something. Landing on the end by luck
        // is possible; landing on the end having last seen a return is not.
        if (wrong == NULL && last != KEST_OP_RETURN) {
            wrong = "ends on something that is not a return";
        }
        if (wrong != NULL) {
            KestSpan nowhere = {0, 0};
            kest_diags_in(diags, chunk->source);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0406", nowhere,
                           "`%s` cannot be walked: %u bytes of code and a walk "
                           "that %s",
                           chunk->name, chunk->code_count, wrong);
            kest_diags_fault(diags,
                             "an instruction is a different width from what "
                             "it says");
            held = false;
        }
        // Every number an instruction carries names something that is there,
        // and every jump lands on an instruction. See D1237.
        if (wrong == NULL) {
            char said[200];
            const char *code = NULL;
            if (starts == NULL) {
                snprintf(said, sizeof said,
                         "could not be checked for want of memory");
                code = "K0408";
            }
            if (code == NULL) {
                code = names_only_what_is_there(module, chunk, starts, said,
                                                sizeof said);
            }
            if (code == NULL) {
                code = stack_on_every_path(module, chunk, depth, work, said,
                                           sizeof said);
            }
            if (code != NULL) {
                KestSpan nowhere = {0, 0};
                kest_diags_in(diags, chunk->source);
                kest_diags_add(diags, KEST_SEVERITY_ERROR, code, nowhere,
                               "`%s`: %s", chunk->name, said);
                kest_diags_fault(diags,
                                 "what the compiler wrote into an instruction "
                                 "and what the program holds disagree");
                held = false;
            }
        }
        // How wide a frame has to be is answered from the declaration before
        // anything runs, so a `return` wider than that would be read back into
        // a host's frame past the end of it.
        if (wrong == NULL && gives >= 0) {
            KestSpan nowhere = {0, 0};
            kest_diags_in(diags, chunk->source);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0407", nowhere,
                           "`%s` has a `return` giving %d slots back where its "
                           "declaration gives %u",
                           chunk->name, gives, chunk->result_slots);
            kest_diags_fault(diags,
                             "what a call reads back is the declaration's "
                             "width");
            held = false;
        }
    }

    for (uint32_t i = 0; i < module->count; i++) {
        for (int about = 0; about < 3; about++) {
            if (!(about == 2   ? module->functions[i]->deterministic
                  : about == 1 ? module->functions[i]->no_host
                               : module->functions[i]->no_alloc)) {
                continue;
            }
            memset(state, 0, module->count);
            uint32_t where = 0;
            int32_t at = breaks_in(module, i, state, &where, about);
            if (at < 0) {
                continue;
            }
            // Reaching here means the walk over the tree missed something, so
            // it is reported against the instruction rather than against a
            // promise: the promise was checked and this is the code that was
            // emitted for it.
            const KestChunk *guilty = module->functions[at];
            KestSpan span = {kest_chunk_origin(guilty, where), 1};
            const char *written = module->functions[i]->wrote;

            kest_diags_in(diags, guilty->source);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0405", span,
                           about == 2
                               ? "this reaches outside the simulation "
                                 "profile, and `%s` promises `deterministic`"
                           : about == 1
                               ? "this calls the host, and `%s` promises "
                                 "`no.host`"
                               : "this reaches the heap, and `%s` promises "
                                 "`no.alloc`",
                           written);
            kest_diags_fault(diags,
                             "the promise was allowed and the code says "
                             "otherwise");
            held = false;
        }
    }
    kest_arena_free(scratch);
    return held;
}

