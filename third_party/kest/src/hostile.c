#include "hostile.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "types.h"

// What a program nobody trusts could hand a door, written as Kest. A door is a
// function of the host's, and what reaches it is whatever the program put in
// the frame: every number there is at every width, every float including the
// ones that are not numbers, text of no length and of a great deal and with a
// nought in it, a case of every enum, an optional holding nothing. The verifier
// holds a program to handing each door what the door declares; what the door
// does with it is the host's to get right, and this is the program that asks.
// See D1254.

typedef struct {
    KestArena *arena;
    char *bytes;
    size_t used;
    size_t room;
    bool failed;
} Written;

static void put(Written *out, const char *format, ...) {
    if (out->failed) {
        return;
    }
    va_list args;
    va_start(args, format);
    va_list again;
    va_copy(again, args);
    int length = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (length < 0) {
        out->failed = true;
        va_end(again);
        return;
    }
    if (out->used + (size_t)length + 1 > out->room) {
        size_t room = out->room == 0 ? 4096 : out->room;
        while (out->used + (size_t)length + 1 > room) {
            room *= 2;
        }
        char *grown = kest_arena_alloc(out->arena, room, 1);
        if (grown == NULL) {
            out->failed = true;
            va_end(again);
            return;
        }
        if (out->used > 0) {
            memcpy(grown, out->bytes, out->used);
        }
        out->bytes = grown;
        out->room = room;
    }
    vsnprintf(out->bytes + out->used, out->room - out->used, format, again);
    va_end(again);
    out->used += (size_t)length;
}

static uint64_t next(uint64_t *state) {
    uint64_t value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

// The name a file writes for one of its own types: what comes after its
// module, because a name lives under the whole of its module and the file is
// inside that one. NULL for a type another module declared, which a program
// can only build through that module's own functions.
static const char *written_name(const KestType *type, const char *module) {
    size_t length = strlen(module);
    if (type->name == NULL || strncmp(type->name, module, length) != 0 ||
        type->name[length] != '.') {
        return NULL;
    }
    return type->name + length + 1;
}

// One whole number at the width, from its ends and its middle.
static void a_whole(Written *out, const KestType *type, uint64_t *state) {
    unsigned width = type->width;
    uint64_t top = width == 64 ? UINT64_MAX : ((uint64_t)1 << width) - 1;
    switch (next(state) % 6) {
    case 0:
        put(out, "0");
        return;
    case 1:
        put(out, "1");
        return;
    case 2:
        if (type->is_signed) {
            put(out, "-1");
        } else {
            put(out, "%" PRIu64, top);
        }
        return;
    case 3:
        if (type->is_signed) {
            put(out, "-%" PRIu64, top / 2 + 1);
        } else {
            put(out, "0");
        }
        return;
    case 4:
        put(out, "%" PRIu64, type->is_signed ? top / 2 : top);
        return;
    default:
        put(out, "%" PRIu64,
            next(state) % (type->is_signed ? top / 2 + 1 : top));
        return;
    }
}

static void a_float(Written *out, const KestType *type, uint64_t *state) {
    static const char *const ENDS[] = {
        "0.0", "-0.0", "(0.0 / 0.0)", "(1.0 / 0.0)", "(-1.0 / 0.0)", "1.0",
        "-1.0", "0.000001",
    };
    uint64_t which = next(state) % 10;
    if (which < 8) {
        put(out, "%s", ENDS[which]);
        return;
    }
    // The largest and the smallest the width holds, written so they are that
    // width's and not rounded into infinity on the way.
    bool narrow = type->width == 32;
    put(out, "%s", which == 8 ? (narrow ? "3.4e38" : "1.7e308")
                              : (narrow ? "1.0e-45" : "5.0e-324"));
}

static void a_text(Written *out, uint64_t *state) {
    switch (next(state) % 6) {
    case 0:
        put(out, "\"\"");
        return;
    case 1:
        put(out, "\"a\"");
        return;
    case 2:
        put(out, "\"\\u{0}\"");
        return;
    case 3:
        put(out, "\"\\u{10ffff}\\u{0}\\u{7f}\"");
        return;
    case 4:
        put(out, "\"\\\"\\{\\}\\\\\"");
        return;
    default: {
        // Longer than anything a door would be written expecting, and not so
        // long a line of source is the thing measured.
        put(out, "\"");
        for (int i = 0; i < 4096; i++) {
            put(out, "x");
        }
        put(out, "\"");
        return;
    }
    }
}

static bool a_value(Written *out, const KestType *type, const char *module,
                    uint64_t *state, int depth);

// What each field of a shape is, in order, with commas between.
static bool pieces(Written *out, KestType *const *types, uint32_t count,
                   const char *module, uint64_t *state, int depth) {
    for (uint32_t i = 0; i < count; i++) {
        if (i > 0) {
            put(out, ", ");
        }
        if (!a_value(out, types[i], module, state, depth + 1)) {
            return false;
        }
    }
    return true;
}

static bool a_value(Written *out, const KestType *type, const char *module,
                    uint64_t *state, int depth) {
    if (type == NULL || depth > 4) {
        return false;
    }
    switch (type->tag) {
    case KEST_T_INT:
        a_whole(out, type, state);
        return true;
    case KEST_T_FLOAT:
        a_float(out, type, state);
        return true;
    case KEST_T_BOOL:
        put(out, next(state) % 2 == 0 ? "true" : "false");
        return true;
    case KEST_T_TEXT:
        a_text(out, state);
        return true;
    case KEST_T_STRUCT: {
        const char *name =
            type->vector ? type->name : written_name(type, module);
        if (name == NULL) {
            return false;
        }
        put(out, "%s(", name);
        for (uint32_t i = 0; i < type->member_count; i++) {
            if (i > 0) {
                put(out, ", ");
            }
            if (!a_value(out, type->members[i].type, module, state,
                         depth + 1)) {
                return false;
            }
        }
        put(out, ")");
        return true;
    }
    case KEST_T_ENUM: {
        const char *name = written_name(type, module);
        if (name == NULL || type->case_count == 0) {
            return false;
        }
        const KestVariantType *one =
            &type->cases[next(state) % type->case_count];
        put(out, "%s.%s", name, one->name);
        if (one->payload_count > 0) {
            put(out, "(");
            if (!pieces(out, one->payload, one->payload_count, module, state,
                        depth)) {
                return false;
            }
            put(out, ")");
        }
        return true;
    }
    case KEST_T_FLAGS: {
        const char *name = written_name(type, module);
        if (name == NULL) {
            return false;
        }
        put(out, "%s()", name);
        return true;
    }
    case KEST_T_OPTIONAL:
        if (next(state) % 3 == 0) {
            put(out, "none");
            return true;
        }
        return a_value(out, type->element, module, state, depth + 1);
    case KEST_T_ARRAY: {
        uint32_t many = (uint32_t)(next(state) % 3) + 1;
        put(out, "[");
        for (uint32_t i = 0; i < many; i++) {
            if (i > 0) {
                put(out, ", ");
            }
            if (!a_value(out, type->element, module, state, depth + 1)) {
                return false;
            }
        }
        put(out, "]");
        return true;
    }
    case KEST_T_FIXED: {
        if (type->count == 0 || type->count > 16) {
            return false;
        }
        put(out, "[");
        for (uint32_t i = 0; i < type->count; i++) {
            if (i > 0) {
                put(out, ", ");
            }
            if (!a_value(out, type->element, module, state, depth + 1)) {
                return false;
            }
        }
        put(out, "]");
        return true;
    }
    // What a program cannot make out of nothing: a reference names something
    // a store already holds, a store and a function are values a program was
    // handed or declared, and the rest are not values at all. A door taking
    // one is said about rather than called.
    case KEST_T_REF:
    case KEST_T_STORE:
    case KEST_T_FN:
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        return false;
    }
    return false;
}

const char *kest_hostile_calls(KestProgram *program, KestArena *arena,
                               const char *path, uint64_t seed,
                               uint32_t rounds, uint32_t *called,
                               uint32_t *passed) {
    Written out = {arena, NULL, 0, 0, false};
    uint64_t state = seed * 2654435761u + 1;
    *called = 0;
    *passed = 0;
    // One function a call, because a door that refuses what it was handed
    // refuses the call it was made from, and every call after it has to be
    // made all the same. `hostile` says how many there are.
    put(&out,
        "\n// Every door this file declares, called with what a program "
        "nobody\n// trusts could hand it: `kest hostile` wrote this from seed "
        "%" PRIu64 ".\n// `hostile` says how many `hostileN` there are. See "
        "D1254.\n",
        seed);
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        const KestType *door = symbol->type;
        if (door == NULL || door->tag != KEST_T_FN || !door->is_foreign ||
            symbol->source == NULL ||
            strcmp(symbol->source->path, path) != 0) {
            continue;
        }
        // The file's module is what is in front of the door's own name, which
        // is how the file's types are named from inside it.
        size_t whole = strlen(symbol->name);
        size_t own = strlen(door->foreign_name);
        if (whole <= own + 1) {
            continue;
        }
        char *module = kest_arena_strndup(arena, symbol->name, whole - own - 1);
        if (module == NULL) {
            return NULL;
        }
        // A door that takes nothing is asked once: asking it again asks the
        // same thing.
        uint32_t asked = door->param_count == 0 ? 1 : rounds;
        for (uint32_t round = 0; round < asked; round++) {
            Written call = {arena, NULL, 0, 0, false};
            put(&call, "\nfn hostile%u() -> i32 {\n    %s(", *called,
                door->foreign_name);
            bool made = pieces(&call, door->params, door->param_count, module,
                               &state, 0);
            put(&call, ")\n    return 0\n}\n");
            if (!made || call.failed) {
                if (round == 0) {
                    put(&out,
                        "\n// `%s` takes what a program cannot make out of "
                        "nothing, so it is not called\n",
                        door->foreign_name);
                    (*passed)++;
                }
                break;
            }
            put(&out, "%.*s", (int)call.used, call.bytes);
            (*called)++;
        }
    }
    put(&out, "\nfn hostile() -> i32 {\n    return %u\n}\n", *called);
    return out.failed ? NULL : out.bytes;
}
