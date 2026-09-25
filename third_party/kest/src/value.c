#include "value.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

// The shortest spelling that reads back as the same number, so what is
// printed is what is there. A float with nothing after the point still gets
// one, because `3` and `3.0` are not the same value in this language.
int kest_write_real(char *buffer, size_t size, double value, bool narrow) {
    // Not a number has no sign worth printing: which one comes out of a
    // divide is the machine's business and `-nan` says something about the
    // bits rather than about the value. It is also the one answer here that
    // cannot be read back, because there is no way to write it in the
    // language; an infinity is the same and keeps its sign, which does mean
    // something.
    if (value != value) {
        return snprintf(buffer, size, "nan");
    }
    // Every width from one up. The first that reads back is the shortest, and
    // a ladder that steps from six to nine prints nine digits for a number
    // that needed eight. Most numbers a program prints are short, so counting
    // up is where the answer usually is as well.
    int most = narrow ? 9 : 17;

    // Shortest is counted in characters and not in digits, because `%g` moves
    // to an exponent when the digits it is given run out: `123456792` reads
    // back at nine and at eight it is `1.2345679e+08`, which is fewer digits
    // and more to read. Once one is found without an exponent in it nothing
    // wider can be shorter, so that is where this stops.
    int chosen = most;
    size_t shortest = 0;
    for (int digits = 1; digits <= most; digits++) {
        int wrote = snprintf(buffer, size, "%.*g", digits, value);
        double back = strtod(buffer, NULL);
        if (narrow ? (float)back != (float)value : back != value) {
            continue;
        }
        if (shortest == 0 || (size_t)wrote < shortest) {
            shortest = (size_t)wrote;
            chosen = digits;
        }
        if (strchr(buffer, 'e') == NULL) {
            break;
        }
    }
    int written = snprintf(buffer, size, "%.*g", chosen, value);
    if (strpbrk(buffer, ".eni") == NULL) {
        written += snprintf(buffer + written, size - (size_t)written, ".0");
    }
    return written;
}

int kest_write_whole(char *buffer, uint64_t bits, bool is_signed) {
    static const char PAIRS[] = "00010203040506070809"
                                "10111213141516171819"
                                "20212223242526272829"
                                "30313233343536373839"
                                "40414243444546474849"
                                "50515253545556575859"
                                "60616263646566676869"
                                "70717273747576777879"
                                "80818283848586878889"
                                "90919293949596979899";
    bool negative = is_signed && (int64_t)bits < 0;
    // The magnitude of the least `i64` is one more than the greatest, and it
    // is what nought minus it is in unsigned arithmetic.
    uint64_t left = negative ? (uint64_t)0 - bits : bits;
    char digits[KEST_WHOLE_ROOM];
    int at = KEST_WHOLE_ROOM;
    while (left >= 100) {
        unsigned pair = (unsigned)(left % 100) * 2;
        left /= 100;
        digits[--at] = PAIRS[pair + 1];
        digits[--at] = PAIRS[pair];
    }
    if (left >= 10) {
        digits[--at] = PAIRS[left * 2 + 1];
        digits[--at] = PAIRS[left * 2];
    } else {
        digits[--at] = (char)('0' + left);
    }
    if (negative) {
        digits[--at] = '-';
    }
    int written = KEST_WHOLE_ROOM - at;
    memcpy(buffer, digits + at, (size_t)written);
    buffer[written] = '\0';
    return written;
}

// What an array starts at. Everything an arena hands out and never takes back
// is kept, including every size an array grew through, so a floor too low costs
// the sizes under it and a floor too high costs the room over it. The module's
// own lists are few and long and one number in bytes does for them; the arrays
// a chunk holds are three shapes of thing and each has its own, measured.
// See D753.
#define FLOOR_BYTES 64

// What a body holds, measured over the library and the examples: the middle
// body is ninety-two bytes of code in thirty instructions with three
// constants, and nine in ten are under two hundred and thirty bytes in
// eighty-five instructions with nine constants. A floor at the middle, so half
// of them never double and the rest double once. See D753.
#define FLOOR_CODE 128
#define FLOOR_ORIGINS 32
#define FLOOR_CONSTANTS 4

static void *grow_from(KestArena *arena, void *items, uint32_t count,
                       uint32_t *capacity, size_t size, uint32_t floor) {
    uint32_t grown = *capacity == 0 ? (floor == 0 ? 1 : floor)
                                    : *capacity * 2;
    void *moved = kest_arena_alloc(arena, size * grown, 16);
    if (moved == NULL) {
        return NULL;
    }
    // The array starts out NULL, and memcpy is not allowed a null source
    // even for nothing.
    if (count > 0) {
        memcpy(moved, items, size * count);
    }
    *capacity = grown;
    return moved;
}

static void *grow(KestArena *arena, void *items, uint32_t count,
                  uint32_t *capacity, size_t size) {
    return grow_from(arena, items, count, capacity, size,
                     (uint32_t)(FLOOR_BYTES / size));
}

void kest_module_init(KestModule *module, KestArena *arena) {
    module->arena = arena;
    module->out_of_room = false;
    module->carrying_off = false;
    module->functions = NULL;
    module->count = 0;
    module->capacity = 0;
    module->externs = NULL;
    module->extern_count = 0;
    module->extern_capacity = 0;
    module->layouts = NULL;
    module->alias = "";
    module->layout_types = NULL;
    module->layout_count = 0;
    module->layout_capacity = 0;
}

KestChunk *kest_module_add(KestModule *module, const char *name) {
    // One name, one function. What a function is compiled under carries what
    // tells it from the others of its name — what it takes, or what a copy was
    // given — so two of them here is this project having built one of those
    // names wrongly, and the second would quietly be the one that runs.
    for (uint32_t i = 0; i < module->count; i++) {
        if (strcmp(module->functions[i]->name, name) == 0) {
            return NULL;
        }
    }

    // And nowhere to put one is not two of a name. Both answer nothing here,
    // and the caller said the same thing about both: `two functions are
    // compiled under this name, which the checker allowed`, which is this
    // compiler calling itself wrong about a machine that had simply run out.
    // Three rungs of a ladder said it before anybody read one. See D853.
    if (module->count == module->capacity) {
        void *moved = grow(module->arena, module->functions, module->count,
                           &module->capacity, sizeof(KestChunk *));
        if (moved == NULL) {
            module->out_of_room = true;
            return NULL;
        }
        module->functions = moved;
    }

    KestChunk *chunk = KEST_ARENA_NEW(module->arena, KestChunk);
    if (chunk == NULL) {
        module->out_of_room = true;
        return NULL;
    }
    chunk->name = name;
    // And the same name as somebody wrote it, once. A name with nothing after
    // it is its own written form, so most functions share the one string.
    const char *hash = strchr(name, '#');
    chunk->wrote = hash == NULL
                       ? name
                       : kest_arena_strndup(module->arena, name,
                                            (size_t)(hash - name));
    if (chunk->wrote == NULL) {
        module->out_of_room = true;
        return NULL;
    }
    module->functions[module->count++] = chunk;
    return chunk;
}

int32_t kest_module_find(const KestModule *module, const char *name) {
    for (uint32_t i = 0; i < module->count; i++) {
        if (strcmp(module->functions[i]->name, name) == 0) {
            return (int32_t)i;
        }
    }

    // A host calls by name and should not have to know that a function is
    // compiled under what it takes as well. That works while the name means
    // one function, and when it means several the host has to say which.
    int32_t only = -1;
    return kest_module_copies(module, name, &only, 1) == 1 ? only : -1;
}

// Which function a host means by a name. What a file writes is registered
// under the module it wrote it in, and a host writes what the file writes, so
// a bare name is looked for under the module of the file that was named as
// well. -1 when there is no such function.
int32_t kest_module_entry(const KestModule *module, const char *name) {
    int32_t found = kest_module_find(module, name);
    if (found >= 0) {
        return found;
    }
    const char *alias = module->alias;
    size_t prefix = alias == NULL ? 0 : strlen(alias);
    char qualified[256];
    if (prefix == 0 || prefix + strlen(name) + 2 > sizeof(qualified)) {
        return -1;
    }
    memcpy(qualified, alias, prefix);
    qualified[prefix] = '.';
    memcpy(qualified + prefix + 1, name, strlen(name) + 1);
    return kest_module_find(module, qualified);
}

// What making a copy per set of types costs a module, by the rule D612 already
// wrote down: two chunks written the same and declared in one place are one
// generic compiled twice. `bodies` comes back as how many were copied at all
// and the answer as how many chunks those became, so the difference between
// them is what the rule cost over compiling each body once, and `bytes` is
// what that difference is in code.
// What a module still holds when a build is done, by what asked for it. Four
// numbers because a total is a number with nothing to divide it by: the code
// itself and the room it sits in, where every instruction came from, the values
// worked out where they stood, and the layouts a host is told about. Everything
// a build holds that is not one of these is the program's rather than the
// module's -- its source, its types, the names it registered. See D784.
void kest_module_holds(const KestModule *module, uint32_t *code,
                       uint32_t *origins, uint32_t *constants,
                       uint32_t *layouts, uint32_t *chunks) {
    *code = 0;
    *origins = 0;
    *constants = 0;
    // The chunks themselves and the list they are found in, which is what a
    // module is when the four arrays below are taken out of it.
    *chunks = (uint32_t)(module->capacity * sizeof(KestChunk *) +
                         module->count * sizeof(KestChunk) +
                         module->extern_capacity * sizeof(KestExtern));
    *layouts = (uint32_t)(module->layout_capacity * sizeof(KestLayout));
    for (uint32_t i = 0; i < module->layout_count; i++) {
        *layouts += (uint32_t)(module->layouts[i].count * sizeof(KestPiece));
    }
    for (uint32_t i = 0; i < module->count; i++) {
        const KestChunk *chunk = module->functions[i];
        *code += chunk->code_capacity;
        *origins += (uint32_t)(chunk->origin_capacity * sizeof(uint32_t));
        *constants +=
            (uint32_t)(chunk->constant_capacity * sizeof(KestValue));
    }
}

uint32_t kest_module_copied(const KestModule *module, uint32_t *bodies,
                            uint32_t *bytes) {
    uint32_t made = 0;
    *bodies = 0;
    *bytes = 0;
    for (uint32_t i = 0; i < module->count; i++) {
        const KestChunk *one = module->functions[i];
        uint32_t same = 0;
        bool first = true;
        for (uint32_t j = 0; j < module->count; j++) {
            const KestChunk *other = module->functions[j];
            if (one->source != other->source ||
                one->declared.offset != other->declared.offset ||
                !kest_word_same(one->wrote, other->wrote,
                                strlen(other->wrote))) {
                continue;
            }
            if (j < i) {
                first = false;
            }
            same++;
        }
        if (same < 2) {
            continue;
        }
        made++;
        if (first) {
            (*bodies)++;
        } else {
            *bytes += one->code_count;
        }
    }
    return made;
}

uint32_t kest_module_copies(const KestModule *module, const char *name,
                            int32_t *found, uint32_t room) {
    size_t length = strlen(name);
    uint32_t count = 0;
    for (uint32_t i = 0; i < module->count; i++) {
        const char *candidate = module->functions[i]->name;
        if (strncmp(candidate, name, length) != 0 || candidate[length] != '#') {
            continue;
        }
        if (count < room) {
            found[count] = (int32_t)i;
        }
        count++;
    }
    return count;
}

// Whether anything in here is a thing whose value the pieces cannot weigh: a
// handle is a handle of something, a tag is a tag of an enum, and a set of bits
// is a set of named ones. Worked out where the layout is made, because the
// alternative is looking at every piece of every argument of every call. See
// D840.
static bool by_the_type(const KestType *type) {
    if (type == NULL) {
        return false;
    }
    switch (type->tag) {
    case KEST_T_ENUM:
    case KEST_T_FLAGS:
    case KEST_T_TEXT:
    case KEST_T_ARRAY:
    case KEST_T_STORE:
        return true;
    case KEST_T_OPTIONAL:
    case KEST_T_FIXED:
        return by_the_type(type->element);
    case KEST_T_STRUCT:
        for (uint32_t i = 0; i < type->member_count; i++) {
            if (by_the_type(type->members[i].type)) {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

// Whether anything in here is a tagged union, which is what makes the piece
// list not enough to move a value by.
static bool holds_a_tag(const KestType *type) {
    if (type == NULL) {
        return false;
    }
    if (type->tag == KEST_T_ENUM) {
        return true;
    }
    if (type->tag == KEST_T_OPTIONAL) {
        return holds_a_tag(type->element);
    }
    if (type->tag == KEST_T_FIXED) {
        return holds_a_tag(type->element);
    }
    if (type->tag == KEST_T_STRUCT) {
        for (uint32_t i = 0; i < type->member_count; i++) {
            if (holds_a_tag(type->members[i].type)) {
                return true;
            }
        }
    }
    return false;
}

// A field's name under the one it is a field of, which is what a host asking
// about a shape reads a piece by. Made where the piece is, because the walk
// below is the only place that knows both halves. See D946.
static const char *under(KestArena *arena, const char *path, const char *name,
                         bool an_index) {
    if (path == NULL) {
        return an_index ? NULL : name;
    }
    size_t room = strlen(path) + strlen(name) + 2;
    char *joined = KEST_ARENA_ARRAY(arena, char, room);
    if (joined == NULL) {
        return path;
    }
    snprintf(joined, room, an_index ? "%s%s" : "%s.%s", path, name);
    return joined;
}

// One piece per slot, in the order the slots are, each with where it is in
// memory and what the program calls it. A nested struct contributes its own
// pieces at its own offset and under its own name.
static uint16_t describe(KestArena *arena, KestPiece *pieces, uint16_t at,
                         const KestType *type, uint16_t base,
                         const char *path) {
    if (type == NULL) {
        pieces[at].offset = base;
        pieces[at].kind = KEST_L_WORD;
        pieces[at].name = path;
        return at + 1;
    }
    if (type->tag == KEST_T_STRUCT) {
        // One with nothing in it is one slot all the same, and the piece that
        // says so has to be written: a walk that filled none of them left a
        // host reading whatever the block had been zeroed to, which is the
        // first kind there is. See D898.
        if (type->member_count == 0) {
            pieces[at].offset = base;
            pieces[at].kind = KEST_L_NOTHING;
            pieces[at].name = path;
            return at + 1;
        }
        for (uint32_t i = 0; i < type->member_count; i++) {
            at = describe(arena, pieces, at, type->members[i].type,
                          (uint16_t)(base + type->members[i].byte_offset),
                          under(arena, path, type->members[i].name, false));
        }
        return at;
    }
    // That many of the same thing, one after another, which is what a C array
    // inside a struct is.
    if (type->tag == KEST_T_FIXED) {
        for (uint32_t i = 0; i < type->count; i++) {
            char which[16];
            snprintf(which, sizeof(which), "[%u]", i);
            at = describe(arena, pieces, at, type->element,
                          (uint16_t)(base + i * type->element->byte_size),
                          under(arena, path, which, true));
        }
        return at;
    }
    if (type->tag == KEST_T_OPTIONAL) {
        at = describe(arena, pieces, at, type->element, base, path);
        pieces[at].offset = (uint16_t)(base + type->element->byte_size);
        // The byte that says whether it is there is not the field: a host
        // reading names off these is reading what a program wrote, and nobody
        // wrote this one.
        pieces[at].name = NULL;
        // The byte says it is the one that says whether the value is there,
        // rather than saying it is a byte. What is worth saying is what a host
        // cannot work out: a number, this byte and a number is a shape a
        // program can write two ways, and a layout said the same of both.
        // See D714.
        pieces[at].kind = KEST_L_HELD;
        return at + 1;
    }
    // The tag, and then one slot per thing the widest case carries. What each
    // of those is depends on the tag, so they are placeholders and the moving
    // is done by type; the pieces are here so the count is the truth.
    //
    // Where they sit is the widest case's, which is the case that decided how
    // big this is. They used to say the tag's own offset — three pieces of one
    // enum all at nought — and a host reading that would lay its own payload
    // over the tag.
    if (type->tag == KEST_T_ENUM) {
        // The tag says it is a tag, rather than saying the four bytes it is
        // laid out as. What it is worth saying is what a host cannot work out:
        // a layout of a shape with an enum in it is a run of pieces, and the
        // tag of the field is a whole number among whole numbers unless it
        // says so. See D708.
        pieces[at].offset = base;
        pieces[at].kind = KEST_L_TAG;
        pieces[at].name = path;
        at++;

        const KestVariantType *widest = NULL;
        for (uint32_t c = 0; c < type->case_count; c++) {
            if (widest == NULL ||
                type->cases[c].payload_count > widest->payload_count) {
                widest = &type->cases[c];
            }
        }
        for (uint16_t s = 1; s < type->slots; s++) {
            uint32_t which = (uint32_t)s - 1;
            uint16_t where = widest != NULL && which < widest->payload_count
                                 ? widest->byte_offsets[which]
                                 : 4;
            pieces[at].offset = (uint16_t)(base + where);
            pieces[at].kind = KEST_L_PAYLOAD;
            // What a case carries is named by the case rather than by the
            // shape: `kest_case_of` is the door to those.
            pieces[at].name = NULL;
            at++;
        }
        return at;
    }
    pieces[at].offset = base;
    pieces[at].kind = kest_scalar_of(type);
    pieces[at].name = path;
    return at + 1;
}

// The name a program writes for a type is the last piece of the one it is
// registered under, and a host may write either.
static bool named_as(const KestType *type, const char *wanted) {
    const char *written = kest_type_written(type);
    if (written == NULL) {
        return false;
    }
    return strcmp(type->name, wanted) == 0 || strcmp(written, wanted) == 0;
}

const char *kest_module_nearest(const KestModule *module, const char *name) {
    size_t length = strlen(name);
    // The same rule the rest of the language suggests by: at one or two
    // characters everything is one edit from everything.
    if (length < 3) {
        return NULL;
    }
    uint32_t limit = length == 3 ? 1 : (uint32_t)length / 3;
    const KestType *best = NULL;
    uint32_t nearest = limit + 1;
    for (uint32_t i = 0; i < module->layout_count; i++) {
        const KestType *type = module->layout_types[i];
        const char *written = kest_type_written(type);
        if (written == NULL) {
            continue;
        }
        uint32_t distance = kest_word_distance(name, length, written,
                                               strlen(written), limit);
        if (distance < nearest) {
            nearest = distance;
            best = type;
        }
    }
    return best == NULL ? NULL
                        : kest_module_askable(module, kest_type_written(best));
}

const char *kest_module_askable(const KestModule *module, const char *name) {
    const KestLayout *found[8];
    uint32_t count = kest_module_layout_of(module, name, found, 8);
    if (count <= 1) {
        return count == 0 ? NULL : name;
    }
    // Two of a name are told apart by the module in front of one, so that is
    // the only one of them a host can ask for and get.
    for (uint32_t i = 0; i < count && i < 8; i++) {
        const KestType *type = found[i]->type;
        if (type != NULL && type->name != NULL &&
            strchr(type->name, '.') != NULL) {
            return type->name;
        }
    }
    return NULL;
}

uint32_t kest_module_layout_of(const KestModule *module, const char *name,
                               const KestLayout **found, uint32_t room) {
    const KestType *last = NULL;
    uint32_t count = 0;
    for (uint32_t i = 0; i < module->layout_count; i++) {
        const KestType *type = module->layout_types[i];
        if (!named_as(type, name) || type == last) {
            continue;
        }
        last = type;
        if (count < room) {
            found[count] = &module->layouts[i];
        }
        count++;
    }
    return count;
}

// What each case of every enum in a type carries, which the pieces of a layout
// cannot say: which type is in a payload slot depends on the tag, so the layout
// says `KEST_L_PAYLOAD` there and this says what is really in one. Laid out
// where the type is laid out, because a host asking afterwards has nowhere to
// put the answer — and for every enum the type reaches by value rather than for
// the one that is the type, because a tag inside a shape is a tag a host meets.
// See D702 and D709.
static bool lay_out_cases(KestModule *module, const KestType *type) {
    if (type == NULL) {
        return true;
    }
    if (type->tag == KEST_T_STRUCT) {
        for (uint32_t i = 0; i < type->member_count; i++) {
            if (!lay_out_cases(module, type->members[i].type)) {
                return false;
            }
        }
        return true;
    }
    if (type->tag == KEST_T_FIXED || type->tag == KEST_T_OPTIONAL) {
        return lay_out_cases(module, type->element);
    }
    if (type->tag != KEST_T_ENUM) {
        return true;
    }
    for (uint32_t c = 0; c < type->case_count; c++) {
        KestVariantType *variant = &type->cases[c];
        if (variant->payload_count == 0 || variant->carries != NULL) {
            continue;
        }
        uint16_t carried = 0;
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            uint16_t wide = variant->payload[p]->slots;
            carried = (uint16_t)(carried + (wide == 0 ? 1 : wide));
        }
        KestPiece *carries =
            KEST_ARENA_ARRAY(module->arena, KestPiece, carried);
        if (carries == NULL) {
            return false;
        }
        uint16_t at = 0;
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            at = describe(module->arena, carries, at, variant->payload[p],
                          variant->byte_offsets[p], NULL);
        }
        variant->carries = carries;
        variant->carry_count = at;
    }
    return true;
}

// Which enum the tag at a piece belongs to, found by walking the type the way
// its pieces were laid out: the same walk `describe` makes, counting instead of
// writing. A value that is an enum has its tag at piece nought and one inside a
// shape has it wherever the fields in front of it end. See D709.
// And the same walk for a set of named bits, which is the other kind whose
// meaning is its declaration order: a bit's value is one shifted by its place
// in the list, so a bit put in the middle doubles every bit after it. Both are
// walked by the one function, because a walk that knew about one and stepped
// over the other would count the pieces differently depending on what it was
// looking for. See D1076.
static const KestType *named_at(const KestType *type, uint16_t want,
                                uint16_t *at, uint8_t looking);

static const KestType *enum_at(const KestType *type, uint16_t want,
                               uint16_t *at) {
    return named_at(type, want, at, (uint8_t)KEST_T_ENUM);
}

static const KestType *named_at(const KestType *type, uint16_t want,
                                uint16_t *at, uint8_t looking) {
    if (type == NULL) {
        *at = (uint16_t)(*at + 1);
        return NULL;
    }
    if (type->tag == KEST_T_STRUCT) {
        for (uint32_t i = 0; i < type->member_count; i++) {
            const KestType *found =
                named_at(type->members[i].type, want, at, looking);
            if (found != NULL) {
                return found;
            }
        }
        return NULL;
    }
    if (type->tag == KEST_T_FIXED) {
        for (uint32_t i = 0; i < type->count; i++) {
            const KestType *found = named_at(type->element, want, at, looking);
            if (found != NULL) {
                return found;
            }
        }
        return NULL;
    }
    if (type->tag == KEST_T_OPTIONAL) {
        const KestType *found = named_at(type->element, want, at, looking);
        if (found != NULL) {
            return found;
        }
        *at = (uint16_t)(*at + 1);
        return NULL;
    }
    if (type->tag == KEST_T_ENUM) {
        bool here = *at == want;
        *at = (uint16_t)(*at + (type->slots == 0 ? 1 : type->slots));
        return here && looking == (uint8_t)KEST_T_ENUM ? type : NULL;
    }
    if (type->tag == KEST_T_FLAGS) {
        bool here = *at == want;
        *at = (uint16_t)(*at + 1);
        return here && looking == (uint8_t)KEST_T_FLAGS ? type : NULL;
    }
    *at = (uint16_t)(*at + 1);
    return NULL;
}

// How many slots moving a value takes, which is what the type walk the steps
// below replaced answered as it went. See D1159.
static uint16_t walked_slots(const KestType *type) {
    if (type == NULL) {
        return 1;
    }
    switch (type->tag) {
    case KEST_T_STRUCT: {
        uint16_t used = 0;
        for (uint32_t i = 0; i < type->member_count; i++) {
            used = (uint16_t)(used + walked_slots(type->members[i].type));
        }
        return used;
    }
    case KEST_T_FIXED:
        return (uint16_t)(type->count * walked_slots(type->element));
    case KEST_T_OPTIONAL:
        return (uint16_t)(walked_slots(type->element) + 1);
    case KEST_T_ENUM:
        return type->slots;
    default:
        return type->tag == KEST_T_TEXT ? 2 : 1;
    }
}

// How many steps and how many case ranges a value's walk is, cases and all.
static void walk_size(const KestType *type, uint32_t *steps, uint32_t *ranges) {
    if (type == NULL) {
        (*steps)++;
        return;
    }
    switch (type->tag) {
    case KEST_T_STRUCT:
        for (uint32_t i = 0; i < type->member_count; i++) {
            walk_size(type->members[i].type, steps, ranges);
        }
        return;
    case KEST_T_FIXED:
        for (uint32_t i = 0; i < type->count; i++) {
            walk_size(type->element, steps, ranges);
        }
        return;
    case KEST_T_OPTIONAL:
        walk_size(type->element, steps, ranges);
        (*steps)++;
        return;
    case KEST_T_ENUM:
        (*steps)++;
        *ranges += type->case_count;
        for (uint32_t c = 0; c < type->case_count; c++) {
            for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
                walk_size(type->cases[c].payload[p], steps, ranges);
            }
        }
        return;
    default:
        (*steps)++;
        return;
    }
}

// The steps of one value at `slot` and `byte`, written from `n` on, with a
// tag's cases left for the caller to write after them. Answers where the next
// step goes.
static uint32_t flatten(KestMoveStep *steps, uint32_t n, const KestType *type,
                        uint16_t slot, uint32_t byte) {
    if (type == NULL) {
        steps[n] = (KestMoveStep){.kind = KEST_L_WORD, .slot = slot,
                                  .byte = byte};
        return n + 1;
    }
    switch (type->tag) {
    case KEST_T_STRUCT:
        for (uint32_t i = 0; i < type->member_count; i++) {
            n = flatten(steps, n, type->members[i].type, slot,
                        byte + type->members[i].byte_offset);
            slot = (uint16_t)(slot + walked_slots(type->members[i].type));
        }
        return n;
    case KEST_T_FIXED:
        for (uint32_t i = 0; i < type->count; i++) {
            n = flatten(steps, n, type->element, slot,
                        byte + i * type->element->byte_size);
            slot = (uint16_t)(slot + walked_slots(type->element));
        }
        return n;
    case KEST_T_OPTIONAL:
        n = flatten(steps, n, type->element, slot, byte);
        steps[n] = (KestMoveStep){
            .kind = KEST_L_HELD,
            .slot = (uint16_t)(slot + walked_slots(type->element)),
            .byte = byte + type->element->byte_size};
        return n + 1;
    case KEST_T_ENUM:
        steps[n] = (KestMoveStep){.kind = KEST_MOVE_CASES,
                                  .slot = slot,
                                  .byte = byte,
                                  .slots = type->slots,
                                  .size = type->byte_size,
                                  .case_count = type->case_count,
                                  .type = type};
        return n + 1;
    default:
        steps[n] = (KestMoveStep){.kind = kest_scalar_of(type), .slot = slot,
                                  .byte = byte};
        return n + 1;
    }
}

// How far apart two of a kind lie in the bytes, and in the slots: what the
// merging below asks of two steps before it makes them one run.
static uint32_t bytes_apart(uint8_t kind) {
    switch (kind) {
    case KEST_L_I8:
    case KEST_L_U8:
    case KEST_L_BOOL:
    case KEST_L_HELD:
    case KEST_L_FLAGS8:
        return 1;
    case KEST_L_I16:
    case KEST_L_U16:
    case KEST_L_FLAGS16:
        return 2;
    case KEST_L_I32:
    case KEST_L_U32:
    case KEST_L_F32:
    case KEST_L_FLAGS32:
        return 4;
    case KEST_L_TEXT:
        return 16;
    default:
        return 8;
    }
}

// The steps from `first` up to `end` with every step that carries on the run
// before it -- the same kind, the next slot and the next bytes -- folded into
// that run. Answers where the steps end now.
static uint32_t merged(KestMoveStep *steps, uint32_t first, uint32_t end) {
    uint32_t kept = first;
    for (uint32_t i = first; i < end; i++) {
        KestMoveStep step = steps[i];
        step.many = 1;
        if (kept > first && step.kind != KEST_MOVE_CASES) {
            KestMoveStep *run = &steps[kept - 1];
            uint32_t apart = bytes_apart(step.kind);
            uint32_t slots_apart = step.kind == KEST_L_TEXT ? 2u : 1u;
            if (run->kind == step.kind && run->many < UINT16_MAX &&
                step.byte == run->byte + run->many * apart &&
                step.slot == run->slot + run->many * slots_apart) {
                run->many++;
                continue;
            }
        }
        steps[kept++] = step;
    }
    for (uint32_t i = first; i < kept; i++) {
        if (steps[i].many > 1) {
            steps[i].kind |= KEST_MOVE_RUN;
        }
    }
    return kept;
}

// The steps from `first` up to `end` with every tag after every scalar, in
// the order each was in. A step says its own slot and its own bytes, so the
// order moves nothing; what it buys is that the last step of a run is a tag
// wherever there is one, and a walk goes on into that tag's case rather than
// calling itself for it. See D1227.
static void cases_last(KestMoveStep *steps, uint32_t first, uint32_t end) {
    uint32_t kept = first;
    for (uint32_t i = first; i < end; i++) {
        if (steps[i].kind == KEST_MOVE_CASES) {
            continue;
        }
        KestMoveStep step = steps[i];
        for (uint32_t j = i; j > kept; j--) {
            steps[j] = steps[j - 1];
        }
        steps[kept++] = step;
    }
}

// A value's walk. The value's own steps come first; then, for every tag
// among the steps so far in the order they were written, each of its cases as
// a run of steps of its own -- which may hold tags, whose cases are written
// when the walk reaches them. NULL for no memory.
static const KestMoving *walk_of(KestArena *arena, const KestType *type) {
    uint32_t total = 0;
    uint32_t ranges = 0;
    walk_size(type, &total, &ranges);
    KestMoving *walk = KEST_ARENA_NEW(arena, KestMoving);
    KestMoveStep *steps = KEST_ARENA_ARRAY(arena, KestMoveStep, total);
    KestMoveRun *runs =
        KEST_ARENA_ARRAY(arena, KestMoveRun, ranges == 0 ? 1 : ranges);
    if (walk == NULL || steps == NULL || runs == NULL) {
        return NULL;
    }
    uint32_t n = merged(steps, 0, flatten(steps, 0, type, 0, 0));
    cases_last(steps, 0, n);
    walk->count = n;
    uint32_t r = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (steps[i].kind != KEST_MOVE_CASES) {
            continue;
        }
        const KestType *chosen = steps[i].type;
        steps[i].cases = r;
        r += chosen->case_count;
        for (uint32_t c = 0; c < chosen->case_count; c++) {
            const KestVariantType *variant = &chosen->cases[c];
            uint32_t first = n;
            for (uint32_t p = 0; p < variant->payload_count; p++) {
                n = flatten(steps, n, variant->payload[p],
                            (uint16_t)(steps[i].slot + variant->offsets[p]),
                            steps[i].byte + variant->byte_offsets[p]);
            }
            n = merged(steps, first, n);
            cases_last(steps, first, n);
            runs[steps[i].cases + c] = (KestMoveRun){first, n - first};
        }
    }
    walk->steps = steps;
    walk->ranges = runs;
    return walk;
}

int32_t kest_module_layout(KestModule *module, const KestType *type) {
    for (uint32_t i = 0; i < module->layout_count; i++) {
        if (module->layout_types[i] == type) {
            return (int32_t)i;
        }
    }
    if (module->layout_count == module->layout_capacity) {
        uint32_t capacity = module->layout_capacity;
        void *layouts = grow(module->arena, module->layouts,
                             module->layout_count, &capacity, sizeof(KestLayout));
        uint32_t types_capacity = module->layout_capacity;
        void *types =
            grow(module->arena, module->layout_types, module->layout_count,
                 &types_capacity, sizeof(const KestType *));
        if (layouts == NULL || types == NULL) {
            return -1;
        }
        module->layouts = layouts;
        module->layout_types = types;
        module->layout_capacity = capacity;
    }

    uint16_t slots = type == NULL || type->slots == 0 ? 1 : type->slots;
    KestPiece *pieces = KEST_ARENA_ARRAY(module->arena, KestPiece, slots);
    if (pieces == NULL) {
        return -1;
    }
    if (!lay_out_cases(module, type)) {
        return -1;
    }

    KestLayout *layout = &module->layouts[module->layout_count];
    layout->pieces = pieces;
    layout->count = describe(module->arena, pieces, 0, type, 0, NULL);
    layout->slots = slots;
    layout->type = type;
    layout->tagged = holds_a_tag(type);
    layout->by_the_type = by_the_type(type);
    // Every value is moved by its walk, a tag or none: what used to be a loop
    // over the pieces moved one piece a turn of a switch, which was most of
    // what reading a struct out of an array cost. See D1177.
    layout->walk = walk_of(module->arena, type);
    if (layout->walk == NULL) {
        return -1;
    }
    layout->size = type == NULL || type->byte_size == 0 ? 8 : type->byte_size;
    layout->align = type == NULL || type->byte_align == 0 ? 8 : type->byte_align;
    module->layout_types[module->layout_count] = type;
    return (int32_t)module->layout_count++;
}

// What the tag names and what it carries, out of what the layout above laid
// out for each case. A host that has read a tag out of slot nought has this and
// nothing else: the pieces of the case, which are the ones the enum's own
// layout could not name. See D702.
const char *kest_case_of(const KestLayout *layout, uint16_t piece, int32_t tag,
                         const KestPiece **carries, uint16_t *count) {
    if (layout == NULL || piece >= layout->count ||
        layout->pieces[piece].kind != KEST_L_TAG) {
        return NULL;
    }
    uint16_t walked = 0;
    const KestType *type = enum_at(layout->type, piece, &walked);
    if (type == NULL || tag < 0 || (uint32_t)tag >= type->case_count) {
        return NULL;
    }
    const KestVariantType *variant = &type->cases[tag];
    if (carries != NULL) {
        *carries = variant->carries;
    }
    if (count != NULL) {
        *count = variant->carry_count;
    }
    return variant->name;
}

int32_t kest_module_extern(KestModule *module, const char *name, KestSpan span,
                           const KestSource *source, bool promises,
                           bool deterministic) {
    for (uint32_t i = 0; i < module->extern_count; i++) {
        if (strcmp(module->externs[i].name, name) == 0) {
            return (int32_t)i;
        }
    }
    if (module->extern_count == module->extern_capacity) {
        void *moved =
            grow(module->arena, module->externs, module->extern_count,
                 &module->extern_capacity, sizeof(KestExtern));
        if (moved == NULL) {
            return -1;
        }
        module->externs = moved;
    }
    module->externs[module->extern_count].name = name;
    module->externs[module->extern_count].span = span;
    module->externs[module->extern_count].source = source;
    module->externs[module->extern_count].takes = NULL;
    module->externs[module->extern_count].takes_count = 0;
    module->externs[module->extern_count].gives = 0;
    module->externs[module->extern_count].gives_value = false;
    module->externs[module->extern_count].promises = promises;
    module->externs[module->extern_count].deterministic = deterministic;
    return (int32_t)module->extern_count++;
}

void kest_module_extern_shape(KestModule *module, uint32_t at, uint16_t *takes,
                              uint16_t count, uint16_t gives,
                              bool gives_value) {
    if (at >= module->extern_count) {
        return;
    }
    module->externs[at].takes = takes;
    module->externs[at].takes_count = count;
    module->externs[at].gives = gives;
    module->externs[at].gives_value = gives_value;
}

static uint32_t kest_op_width(uint8_t op);

bool kest_chunk_emit(KestModule *module, KestChunk *chunk, uint8_t byte,
                     uint32_t origin) {
    if (chunk->code_count == chunk->code_capacity) {
        uint32_t capacity = chunk->code_capacity;
        void *code = grow_from(module->arena, chunk->code, chunk->code_count,
                               &capacity, sizeof(uint8_t), FLOOR_CODE);
        if (code == NULL) {
            module->out_of_room = true;
            return false;
        }
        chunk->code = code;
        chunk->code_capacity = capacity;
    }
    // An opcode starts an instruction and what follows it does not, so where a
    // thing was written is kept once for the instruction rather than once for
    // every byte of it. Which of the two this byte is comes from the width of
    // the last opcode, asked of the one table that answers that. See D751.
    if (chunk->code_count == chunk->next_instruction) {
        if (chunk->origin_count == chunk->origin_capacity) {
            uint32_t capacity = chunk->origin_capacity;
            void *origins = grow_from(module->arena, chunk->origins,
                                      chunk->origin_count, &capacity,
                                      sizeof(uint32_t), FLOOR_ORIGINS);
            if (origins == NULL) {
                module->out_of_room = true;
                return false;
            }
            chunk->origins = origins;
            chunk->origin_capacity = capacity;
        }
        chunk->origins[chunk->origin_count++] = origin;
        chunk->next_instruction += kest_op_width(byte);
    }
    chunk->code[chunk->code_count++] = byte;
    return true;
}

void kest_chunk_take_back(KestChunk *chunk, uint32_t to) {
    if (chunk == NULL || to > chunk->code_count) {
        return;
    }
    // The instruction being taken back is one byte and started where the code
    // now ends, so what it wrote is one origin and one expectation. Both go
    // back with it: a `code_count` moved on its own leaves the next byte
    // looking like an operand and the origin of every instruction after it
    // off by one. See D804.
    chunk->code_count = to;
    if (chunk->next_instruction > to) {
        chunk->next_instruction = to;
        if (chunk->origin_count > 0) {
            chunk->origin_count--;
        }
    }
}

uint32_t kest_op_wide(uint8_t op) {
    return kest_op_width(op);
}

uint32_t kest_chunk_origin(const KestChunk *chunk, uint32_t offset) {
    // Walked rather than looked up: a table of where every instruction starts
    // would be the thing this is for getting rid of. What reads one is a
    // program that has already failed, and a walk over a body is nothing
    // beside writing a message about it. See D751.
    if (chunk == NULL || chunk->origins == NULL || chunk->code == NULL) {
        return 0;
    }
    uint32_t at = 0;
    uint32_t which = 0;
    while (at < chunk->code_count && which < chunk->origin_count) {
        uint32_t width = kest_op_width(chunk->code[at]);
        if (offset < at + width) {
            return chunk->origins[which];
        }
        at += width;
        which++;
    }
    return chunk->origin_count > 0 ? chunk->origins[chunk->origin_count - 1]
                                   : 0;
}

bool kest_chunk_names(KestModule *module, KestChunk *chunk, const char *name,
                      uint16_t slot, uint16_t slots, uint8_t kind,
                      bool at_address) {
    if (chunk == NULL || name == NULL) {
        return false;
    }
    if (chunk->named_count == chunk->named_capacity) {
        uint16_t grown = chunk->named_capacity == 0
                             ? 8
                             : (uint16_t)(chunk->named_capacity * 2);
        if (grown <= chunk->named_capacity) {
            return false;
        }
        KestNamed *moved = KEST_ARENA_ARRAY(module->arena, KestNamed, grown);
        if (moved == NULL) {
            // A name a debugger cannot show is not worth refusing a compile
            // over. What it costs is that a stopped machine says the slot and
            // not what it was called.
            return false;
        }
        for (uint16_t i = 0; i < chunk->named_count; i++) {
            moved[i] = chunk->named[i];
        }
        chunk->named = moved;
        chunk->named_capacity = grown;
    }
    KestNamed *one = &chunk->named[chunk->named_count++];
    one->name = name;
    one->slot = slot;
    one->slots = slots;
    one->kind = kind;
    one->at_address = at_address;
    return true;
}

const char *kest_chunk_named(const KestChunk *chunk, uint16_t slot,
                             uint16_t *slots, uint8_t *kind,
                             bool *at_address) {
    if (chunk == NULL) {
        return NULL;
    }
    // Backwards, because the last name written for a slot is the one a body
    // gave it most recently and a body that reuses a slot after a scope ends
    // gave it two.
    for (uint16_t i = chunk->named_count; i > 0; i--) {
        const KestNamed *one = &chunk->named[i - 1];
        if (one->slot != slot) {
            continue;
        }
        if (slots != NULL) {
            *slots = one->slots;
        }
        if (kind != NULL) {
            *kind = one->kind;
        }
        if (at_address != NULL) {
            *at_address = one->at_address;
        }
        return one->name;
    }
    return NULL;
}

bool kest_chunk_emit_u16(KestModule *module, KestChunk *chunk, uint16_t value,
                         uint32_t origin) {
    return kest_chunk_emit(module, chunk, (uint8_t)(value & 0xff), origin) &&
           kest_chunk_emit(module, chunk, (uint8_t)(value >> 8), origin);
}

uint32_t kest_chunk_constant_run(KestModule *module, KestChunk *chunk,
                                 const KestValue *values,
                                 const uint8_t *classes, uint32_t count) {
    // Looked up as a run rather than a value at a time: the entries have to
    // be together and in order, so what is compared is the whole run. A table
    // read in ten places is stored once.
    for (uint32_t start = 0; count <= chunk->constant_count &&
                             start + count <= chunk->constant_count;
         start++) {
        bool same = true;
        for (uint32_t i = 0; i < count && same; i++) {
            same = chunk->constant_classes[start + i] == classes[i] &&
                   memcmp(&chunk->constants[start + i], &values[i],
                          sizeof(KestValue)) == 0;
        }
        if (same) {
            return start;
        }
    }

    uint32_t first = chunk->constant_count;
    for (uint32_t i = 0; i < count; i++) {
        if (chunk->constant_count == chunk->constant_capacity) {
            uint32_t capacity = chunk->constant_capacity;
            void *held = grow_from(module->arena, chunk->constants,
                                   chunk->constant_count, &capacity,
                                   sizeof(KestValue), FLOOR_CONSTANTS);
            uint32_t class_capacity = chunk->constant_capacity;
            void *kinds = grow_from(module->arena, chunk->constant_classes,
                                    chunk->constant_count, &class_capacity,
                                    sizeof(uint8_t), FLOOR_CONSTANTS);
            if (held == NULL || kinds == NULL) {
                module->out_of_room = true;
                return 0;
            }
            chunk->constants = held;
            chunk->constant_classes = kinds;
            chunk->constant_capacity = capacity;
        }
        chunk->constant_classes[chunk->constant_count] = classes[i];
        chunk->constants[chunk->constant_count] = values[i];
        chunk->constant_count++;
    }
    return first;
}

typedef enum {
    NONE,
    U16,
    U16_U16,
    U16_U16_U16,
    JUMP,
    BACK,
    WALK,
    FIND,
    FIND_BACK,
    // A constant and then a forward jump, which is five bytes.
    WEIGH,
    // Four and five numbers: an element moved by a run and an index read
    // where they are. See D1167.
    U16_X4,
    U16_X5,
    // An element by a run and an index, a constant, and a forward jump,
    // which is eleven bytes. See D1178.
    WEIGH_ELEMENT,
} Operands;

// The table below is written in the header's names for what an operand is,
// shortened so that a row still reads as one line.
#define IS_NUMBER KEST_OPERAND_NUMBER
#define IS_SLOT KEST_OPERAND_SLOT
#define IS_SLOT_RUN KEST_OPERAND_SLOT_RUN
#define IS_CONSTANT KEST_OPERAND_CONSTANT
#define IS_CONSTANT_RUN KEST_OPERAND_CONSTANT_RUN
#define IS_FUNCTION KEST_OPERAND_FUNCTION
#define IS_EXTERN KEST_OPERAND_EXTERN
#define IS_LAYOUT KEST_OPERAND_LAYOUT
#define IS_FORWARD KEST_OPERAND_FORWARD
#define IS_BACKWARD KEST_OPERAND_BACKWARD

typedef struct {
    const char *name;
    Operands operands;
    KestOperand is[5];
} Instruction;

static const Instruction INSTRUCTIONS[] = {
    {"const", U16, {IS_CONSTANT}},
    {"const.run", U16_U16, {IS_CONSTANT, IS_CONSTANT_RUN}},
    {"const.at", U16_U16_U16, {IS_CONSTANT, IS_NUMBER, IS_NUMBER}},
    {"load", U16, {IS_SLOT}},
    {"store", U16, {IS_SLOT}},
    {"load.n", U16_U16, {IS_SLOT, IS_SLOT_RUN}},
    {"store.n", U16_U16, {IS_SLOT, IS_SLOT_RUN}},
    {"load2", U16_U16, {IS_SLOT, IS_SLOT}},
    {"load.k", U16_U16, {IS_SLOT, IS_CONSTANT}},
    {"field", U16_U16_U16, {IS_NUMBER, IS_NUMBER, IS_NUMBER}},
    {"array", U16_U16, {IS_NUMBER, IS_LAYOUT}},
    {"make.array", U16, {IS_LAYOUT}},
    {"push", U16, {IS_LAYOUT}},
    {"fit", U16, {IS_LAYOUT}},
    {"push.text", U16, {IS_LAYOUT}},
    {"fit.text", U16, {IS_LAYOUT}},
    {"room", U16, {IS_LAYOUT}},
    {"index", U16, {IS_LAYOUT}},
    {"index.ll", U16_U16_U16, {IS_SLOT, IS_SLOT, IS_LAYOUT}},
    {"pop.last", U16, {IS_LAYOUT}},
    {"take", U16, {IS_LAYOUT}},
    {"clear", NONE, {}},
    {"elem.addr", U16, {IS_NUMBER}},
    {"elem.at", U16_U16, {IS_NUMBER, IS_LAYOUT}},
    {"load.slots", U16_U16_U16, {IS_SLOT, IS_NUMBER, IS_NUMBER}},
    {"store.slots", U16_U16_U16, {IS_SLOT, IS_NUMBER, IS_NUMBER}},
    {"offset.addr", U16_U16, {IS_NUMBER, IS_NUMBER}},
    {"load.at", U16_U16, {IS_NUMBER, IS_LAYOUT}},
    {"len", NONE, {}},
    {"text.len", NONE, {}},
    {"text.at", NONE, {}},
    {"text.in", U16_U16, {IS_SLOT, IS_SLOT}},
    {"text.slice", NONE, {}},
    {"text.rest", NONE, {}},
    {"text.matches", NONE, {}},
    {"text.find", NONE, {}},
    {"text.i", NONE, {}},
    {"text.u", NONE, {}},
    {"text.f", NONE, {}},
    {"text.f32", NONE, {}},
    {"text.b", NONE, {}},
    {"text.flags", U16, {IS_LAYOUT}},
    {"text.value", U16, {IS_LAYOUT}},
    {"concat", U16, {IS_NUMBER}},
    {"hash.i", NONE, {}},
    {"hash.f", NONE, {}},
    {"hash.t", NONE, {}},
    {"hash.value", U16, {IS_LAYOUT}},
    {"eq.value", U16, {IS_LAYOUT}},
    {"ne.value", U16, {IS_LAYOUT}},
    {"text.from", NONE, {}},
    {"new.store", U16_U16, {IS_NUMBER, IS_LAYOUT}},
    {"load.elem", U16_U16, {IS_NUMBER, IS_LAYOUT}},
    {"store.elem", U16_U16, {IS_NUMBER, IS_LAYOUT}},
    {"index.to", U16_U16, {IS_LAYOUT, IS_SLOT}},
    {"elem.from", U16_U16_U16, {IS_NUMBER, IS_LAYOUT, IS_SLOT}},
    {"add.i.narrow.to", U16_U16, {IS_NUMBER, IS_SLOT}},
    {"sub.i.narrow.to", U16_U16, {IS_NUMBER, IS_SLOT}},
    {"add.f.to", U16, {IS_SLOT}},
    {"sub.f.to", U16, {IS_SLOT}},
    {"add", U16, {IS_NUMBER}},
    {"get", U16, {IS_NUMBER}},
    {"set", U16, {IS_NUMBER}},
    {"remove", NONE, {}},
    {"count", NONE, {}},
    {"seek.from", FIND, {IS_SLOT, IS_SLOT, IS_FORWARD}},
    {"seek.next", FIND_BACK, {IS_SLOT, IS_SLOT, IS_BACKWARD}},
    {"store.ref", NONE, {}},
    {"true", NONE, {}},
    {"false", NONE, {}},
    {"pop", NONE, {}},
    {"pop.n", U16, {IS_NUMBER}},
    {"rotate", U16, {IS_NUMBER}},
    {"add.i", NONE, {}},
    {"sub.i", NONE, {}},
    {"mul.i", NONE, {}},
    {"div.i", NONE, {}},
    {"mod.i", NONE, {}},
    {"div.u", NONE, {}},
    {"mod.u", NONE, {}},
    {"neg.i", NONE, {}},
    {"and.i", NONE, {}},
    {"or.i", NONE, {}},
    {"xor.i", NONE, {}},
    {"not.i", NONE, {}},
    {"shl", NONE, {}},
    {"shr.i", NONE, {}},
    {"shr.u", NONE, {}},
    {"narrow", U16, {IS_NUMBER}},
    {"add.i.narrow", U16, {IS_NUMBER}},
    {"sub.i.narrow", U16, {IS_NUMBER}},
    {"mul.i.narrow", U16, {IS_NUMBER}},
    {"i2f", NONE, {}},
    {"u2f", NONE, {}},
    {"f2i", U16, {IS_NUMBER}},
    {"to.f32", NONE, {}},
    {"add.f", NONE, {}},
    {"sub.f", NONE, {}},
    {"mul.f", NONE, {}},
    {"div.f", NONE, {}},
    {"mod.f", NONE, {}},
    {"neg.f", NONE, {}},
    {"add.f32", NONE, {}},
    {"sub.f32", NONE, {}},
    {"mul.f32", NONE, {}},
    {"div.f32", NONE, {}},
    {"mod.f32", NONE, {}},
    {"neg.f32", NONE, {}},
    {"lt.i", NONE, {}},
    {"le.i", NONE, {}},
    {"gt.i", NONE, {}},
    {"ge.i", NONE, {}},
    {"lt.u", NONE, {}},
    {"le.u", NONE, {}},
    {"gt.u", NONE, {}},
    {"ge.u", NONE, {}},
    {"lt.f", NONE, {}},
    {"le.f", NONE, {}},
    {"gt.f", NONE, {}},
    {"ge.f", NONE, {}},
    {"eq.i", NONE, {}},
    {"ne.i", NONE, {}},
    {"eq.f", NONE, {}},
    {"ne.f", NONE, {}},
    {"eq.t", NONE, {}},
    {"ne.t", NONE, {}},
    {"lt.t", NONE, {}},
    {"le.t", NONE, {}},
    {"gt.t", NONE, {}},
    {"ge.t", NONE, {}},
    {"not", NONE, {}},
    {"jump", JUMP, {IS_FORWARD}},
    {"jump.false", JUMP, {IS_FORWARD}},
    {"jump.true", JUMP, {IS_FORWARD}},
    {"jump.false.lt.i", JUMP, {IS_FORWARD}},
    {"jump.false.le.i", JUMP, {IS_FORWARD}},
    {"jump.false.gt.i", JUMP, {IS_FORWARD}},
    {"jump.false.ge.i", JUMP, {IS_FORWARD}},
    {"jump.false.eq.i", JUMP, {IS_FORWARD}},
    {"jump.false.ne.i", JUMP, {IS_FORWARD}},
    {"jump.true.lt.i", JUMP, {IS_FORWARD}},
    {"jump.true.le.i", JUMP, {IS_FORWARD}},
    {"jump.true.gt.i", JUMP, {IS_FORWARD}},
    {"jump.true.ge.i", JUMP, {IS_FORWARD}},
    {"jump.true.eq.i", JUMP, {IS_FORWARD}},
    {"jump.true.ne.i", JUMP, {IS_FORWARD}},
    {"jump.false.lt.f", JUMP, {IS_FORWARD}},
    {"jump.false.le.f", JUMP, {IS_FORWARD}},
    {"jump.false.gt.f", JUMP, {IS_FORWARD}},
    {"jump.false.ge.f", JUMP, {IS_FORWARD}},
    {"jump.false.eq.f", JUMP, {IS_FORWARD}},
    {"jump.false.ne.f", JUMP, {IS_FORWARD}},
    {"jump.true.lt.f", JUMP, {IS_FORWARD}},
    {"jump.true.le.f", JUMP, {IS_FORWARD}},
    {"jump.true.gt.f", JUMP, {IS_FORWARD}},
    {"jump.true.ge.f", JUMP, {IS_FORWARD}},
    {"jump.true.eq.f", JUMP, {IS_FORWARD}},
    {"jump.true.ne.f", JUMP, {IS_FORWARD}},
    {"store.k", U16_U16, {IS_SLOT, IS_CONSTANT}},
    {"add.k.self", U16_U16_U16, {IS_NUMBER, IS_SLOT, IS_CONSTANT}},
    {"sub.k.self", U16_U16_U16, {IS_NUMBER, IS_SLOT, IS_CONSTANT}},
    {"jump.false.lt.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.le.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.gt.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.ge.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.eq.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.ne.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.lt.c", WEIGH, {IS_CONSTANT, IS_FORWARD}},
    {"jump.false.le.c", WEIGH, {IS_CONSTANT, IS_FORWARD}},
    {"jump.false.gt.c", WEIGH, {IS_CONSTANT, IS_FORWARD}},
    {"jump.false.ge.c", WEIGH, {IS_CONSTANT, IS_FORWARD}},
    {"jump.false.eq.c", WEIGH, {IS_CONSTANT, IS_FORWARD}},
    {"jump.false.ne.c", WEIGH, {IS_CONSTANT, IS_FORWARD}},
    {"jump.false.lt.f.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.le.f.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.gt.f.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.ge.f.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.eq.f.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.ne.f.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.true.lt.f.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.true.le.f.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.true.gt.f.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.true.ge.f.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.true.eq.f.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"jump.true.ne.f.k", FIND, {IS_SLOT, IS_CONSTANT, IS_FORWARD}},
    {"add.f.ll", U16_U16_U16, {IS_SLOT, IS_SLOT, IS_SLOT}},
    {"sub.f.ll", U16_U16_U16, {IS_SLOT, IS_SLOT, IS_SLOT}},
    {"index.to.ll", U16_X4, {IS_LAYOUT, IS_SLOT, IS_SLOT, IS_SLOT}},
    {"elem.from.ll", U16_X5, {IS_NUMBER, IS_LAYOUT, IS_SLOT, IS_SLOT, IS_SLOT}},
    {"mod.i.c", U16, {IS_CONSTANT}},
    {"mod.i.k", U16_U16, {IS_SLOT, IS_CONSTANT}},
    {"div.i.c", U16, {IS_CONSTANT}},
    {"div.i.k", U16_U16, {IS_SLOT, IS_CONSTANT}},
    {"add.i.narrow.c", U16_U16, {IS_NUMBER, IS_CONSTANT}},
    {"add.i.narrow.k", U16_U16_U16, {IS_NUMBER, IS_SLOT, IS_CONSTANT}},
    {"sub.i.narrow.c", U16_U16, {IS_NUMBER, IS_CONSTANT}},
    {"sub.i.narrow.k", U16_U16_U16, {IS_NUMBER, IS_SLOT, IS_CONSTANT}},
    {"mul.i.narrow.c", U16_U16, {IS_NUMBER, IS_CONSTANT}},
    {"mul.i.narrow.k", U16_U16_U16, {IS_NUMBER, IS_SLOT, IS_CONSTANT}},
    {"float.bits", U16, {IS_NUMBER}},
    {"bits.f32", NONE, {}},
    {"jump.false.lt.e", WEIGH_ELEMENT, {IS_SLOT, IS_SLOT, IS_LAYOUT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.le.e", WEIGH_ELEMENT, {IS_SLOT, IS_SLOT, IS_LAYOUT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.gt.e", WEIGH_ELEMENT, {IS_SLOT, IS_SLOT, IS_LAYOUT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.ge.e", WEIGH_ELEMENT, {IS_SLOT, IS_SLOT, IS_LAYOUT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.eq.e", WEIGH_ELEMENT, {IS_SLOT, IS_SLOT, IS_LAYOUT, IS_CONSTANT, IS_FORWARD}},
    {"jump.false.ne.e", WEIGH_ELEMENT, {IS_SLOT, IS_SLOT, IS_LAYOUT, IS_CONSTANT, IS_FORWARD}},
    {"loop", BACK, {IS_BACKWARD}},
    {"next.less.i", WALK, {IS_SLOT, IS_SLOT, IS_BACKWARD}},
    {"next.less.u", WALK, {IS_SLOT, IS_SLOT, IS_BACKWARD}},
    {"scratch", U16, {IS_SLOT}},
    {"unscratch", U16, {IS_SLOT}},
    {"call", U16_U16, {IS_FUNCTION, IS_NUMBER}},
    {"call.value", U16_U16, {IS_NUMBER, IS_NUMBER}},
    {"call.host", U16_U16_U16, {IS_EXTERN, IS_NUMBER, IS_NUMBER}},
    {"return", U16, {IS_NUMBER}},
    {"stop", NONE, {}},
};

// One name an opcode, and the compiler counts them, the same way the token
// names are counted. What each is called is `check-tables.sh`'s to hold.
_Static_assert(sizeof(INSTRUCTIONS) / sizeof(INSTRUCTIONS[0]) ==
                   KEST_OP_STOP + 1,
               "every instruction has a name and nothing else does");

const char *kest_op_name(uint8_t op) {
    return op <= KEST_OP_STOP ? INSTRUCTIONS[op].name : "?";
}

KestOperand kest_op_operand(uint8_t op, uint32_t k) {
    uint32_t known = (uint32_t)(sizeof(INSTRUCTIONS) / sizeof(INSTRUCTIONS[0]));
    return op < known && k < 5 ? INSTRUCTIONS[op].is[k] : KEST_OPERAND_NUMBER;
}

static uint16_t read_u16(const KestChunk *chunk, uint32_t offset) {
    return (uint16_t)(chunk->code[offset] | (chunk->code[offset + 1] << 8));
}

uint16_t kest_chunk_u16(const KestChunk *chunk, uint32_t offset) {
    return read_u16(chunk, offset);
}

// How many bytes an instruction takes. This is the only place that knows, so
// a walk that prints and a walk that does not cannot come apart: D057's bug
// was a second answer to this question that had a jump seven bytes wide.
// How many bytes an instruction takes. Everything in this file that walks a
// chunk asks this and nothing works it out for itself, because two answers is
// how a walk goes out of step with the code. Nothing outside walks one; the
// day something does, this stops being static rather than being copied.
static uint32_t kest_op_width(uint8_t op) {
    switch (INSTRUCTIONS[op].operands) {
    case NONE:
        return 1;
    case U16:
    case JUMP:
    case BACK:
        // A jump carries how far as one number, printed as a place to make it
        // readable. It is the same two bytes.
        return 3;
    case U16_U16:
    case WEIGH:
        return 5;
    case U16_U16_U16:
    case WALK:
    case FIND:
    case FIND_BACK:
        return 7;
    case U16_X4:
        return 9;
    case U16_X5:
    case WEIGH_ELEMENT:
        return 11;
    }
    return 1;
}

// The deepest run of frames a call can make, and the slots those frames take
// together, written into `depth` and `slots` at this function's own place. A
// program that can reach itself has no answer and neither has one that calls
// through a value, because what a value points at is not known until it runs.
// What one call adds to what a body needs, gathered as the walk goes. Kept
// apart from the walk's own numbers because a call is counted in two places:
// where the code calls, and where a body was carried rather than called.
typedef struct {
    uint32_t deepest;
    uint32_t widest;
    bool reaches_host;
    uint32_t host_deepest;
    uint32_t host_widest;
    uint32_t host_started;
} Sums;

static bool measure_chunk(const KestModule *module, uint32_t which,
                          uint8_t *state, uint32_t *depth, uint32_t *slots,
                          uint32_t *host_depth, uint32_t *host_slots,
                          uint32_t *host_from, KestNoLeast *reasons,
                          KestReason *why);

static bool count_the_call(const KestModule *module, uint32_t which,
                           uint16_t callee, uint8_t *state, uint32_t *depth,
                           uint32_t *slots, uint32_t *host_depth,
                           uint32_t *host_slots, uint32_t *host_from,
                           KestNoLeast *reasons, KestReason *why, Sums *sums,
                           bool framed) {
    if (callee >= module->count ||
        !measure_chunk(module, callee, state, depth, slots, host_depth,
                       host_slots, host_from, reasons, why)) {
        state[which] = 0;
        // A function that calls one with no answer has none either, and for
        // the same reason: what a reader asks about a function is whether its
        // own stack can be worked out, and it cannot if anything it reaches
        // has no bottom. See D601.
        if (reasons != NULL && callee < module->count) {
            // And where it came from, which is what a reader opens: a
            // function three calls above a `call.value` is told it calls
            // through a value, and the one that does is the one to look at.
            // See D602.
            reasons[which].reach = reasons[callee].reach != 0
                                       ? reasons[callee].reach
                                       : (uint8_t)why->reach;
            reasons[which].from =
                reasons[callee].reach != 0 ? reasons[callee].from : callee;
        }
        return false;
    }
    // A body carried rather than called runs in its caller's frame, on both
    // engines: what it adds is its slots and not a frame. See D1156.
    uint32_t deeper = framed ? depth[callee] : depth[callee] - 1;
    if (deeper > sums->deepest) {
        sums->deepest = deeper;
    }
    // What a call adds is what the callee needs less the arguments it was
    // handed: the machine puts the callee's frame at `top` less the slots the
    // call carries, so the caller's arguments and the callee's parameters are
    // the same slots counted once. Added whole, a chain of calls was charged
    // its arguments twice at every step -- which is room a host is told to
    // find and no program reaches. See D813.
    uint32_t callee_adds =
        slots[callee] - module->functions[callee]->param_slots;
    if (callee_adds > sums->widest) {
        sums->widest = callee_adds;
    }
    if (host_depth[callee] > 0) {
        sums->reaches_host = true;
    }
    if (host_depth[callee] > sums->host_deepest) {
        sums->host_deepest = host_depth[callee];
    }
    uint32_t host_adds =
        host_slots[callee] == 0
            ? 0
            : host_slots[callee] - module->functions[callee]->param_slots;
    if (host_adds > sums->host_widest) {
        sums->host_widest = host_adds;
        sums->host_started = host_from[callee];
    }
    return true;
}

static bool measure_chunk(const KestModule *module, uint32_t which,
                          uint8_t *state, uint32_t *depth, uint32_t *slots,
                          uint32_t *host_depth, uint32_t *host_slots,
                          uint32_t *host_from, KestNoLeast *reasons,
                          KestReason *why) {
    if (state[which] == 2) {
        return true;
    }
    if (state[which] == 1) {
        // The one that comes back round, which is the one to name: it is where
        // the run of calls closes.
        why->reach = KEST_REACH_ITSELF;
        why->where = module->functions[which]->name;
        if (reasons != NULL) {
            reasons[which].reach = KEST_REACH_ITSELF;
            reasons[which].from = which;
        }
        return false;
    }
    state[which] = 1;

    const KestChunk *chunk = module->functions[which];
    uint32_t deepest = 0;
    uint32_t widest = 0;
    // The same two numbers again, over the runs of calls that end at a host
    // function rather than at a `return`. A host function is where a host may
    // call back in, and what it starts on top of is what is in use there.
    uint32_t host_deepest = 0;
    uint32_t host_widest = 0;
    bool reaches_host = false;
    // Which function the deepest call into the host is in, which is this one
    // until a callee turns out to reach one from further in. A host reads the
    // number to size a machine and the name to know what to shorten. See D605.
    uint32_t host_started = which;
    Sums sums = {0, 0, false, 0, 0, which};
    for (uint32_t at = 0; at < chunk->code_count;) {
        uint8_t op = chunk->code[at];
        if (op == KEST_OP_CALL_VALUE) {
            // Which function this enters is not known here, but which
            // functions it could enter is: a function becomes a value in one
            // place, and every one that ever does is written down. So the
            // call costs the worst of those, the same way an ordinary call
            // costs the one it names — and a program that turns none of its
            // functions into a value has no candidates at all, which leaves a
            // host that handed one in and no answer to give. A host that
            // hands in something wider than these meets the machine asking
            // for room at the call and being told no, which is what every
            // other under-asking meets. See D814.
            bool any = false;
            for (uint32_t maybe = 0; maybe < module->count; maybe++) {
                if (module->functions[maybe] == NULL ||
                    !module->functions[maybe]->as_value) {
                    continue;
                }
                any = true;
                if (!measure_chunk(module, maybe, state, depth, slots,
                                   host_depth, host_slots, host_from, reasons,
                                   why)) {
                    state[which] = 0;
                    if (reasons != NULL && maybe < module->count) {
                        reasons[which].reach = reasons[maybe].reach != 0
                                                   ? reasons[maybe].reach
                                                   : (uint8_t)why->reach;
                        reasons[which].from = reasons[maybe].reach != 0
                                                  ? reasons[maybe].from
                                                  : maybe;
                    }
                    return false;
                }
                if (depth[maybe] > deepest) {
                    deepest = depth[maybe];
                }
                uint32_t through =
                    slots[maybe] - module->functions[maybe]->param_slots;
                if (through > widest) {
                    widest = through;
                }
                if (host_depth[maybe] > 0) {
                    reaches_host = true;
                }
                if (host_depth[maybe] > host_deepest) {
                    host_deepest = host_depth[maybe];
                }
                uint32_t host_through =
                    host_slots[maybe] == 0
                        ? 0
                        : host_slots[maybe] -
                              module->functions[maybe]->param_slots;
                if (host_through > host_widest) {
                    host_widest = host_through;
                    host_started = host_from[maybe];
                }
            }
            if (!any) {
                why->reach = KEST_REACH_VALUE;
                why->where = chunk->name;
                state[which] = 0;
                if (reasons != NULL) {
                    reasons[which].reach = KEST_REACH_VALUE;
                    reasons[which].from = which;
                }
                return false;
            }
            at += kest_op_width(op);
            continue;
        }
        if (op == KEST_OP_CALL_HOST) {
            reaches_host = true;
        }
        if (op == KEST_OP_CALL &&
            !count_the_call(module, which, read_u16(chunk, at + 1), state,
                            depth, slots, host_depth, host_slots, host_from,
                            reasons, why, &sums, true)) {
            return false;
        }
        at += kest_op_width(op);
    }
    // And the bodies carried here rather than called: the other backend
    // still calls them, above this body's own slots, and neither engine gives
    // one a frame. See D1156.
    for (uint16_t i = 0; i < chunk->carried_count; i++) {
        if (!count_the_call(module, which, chunk->carried[i], state, depth,
                            slots, host_depth, host_slots, host_from, reasons,
                            why, &sums, false)) {
            return false;
        }
    }
    deepest = deepest > sums.deepest ? deepest : sums.deepest;
    widest = widest > sums.widest ? widest : sums.widest;
    reaches_host = reaches_host || sums.reaches_host;
    if (sums.host_deepest > host_deepest) {
        host_deepest = sums.host_deepest;
    }
    if (sums.host_widest > host_widest) {
        host_widest = sums.host_widest;
        host_started = sums.host_started;
    }

    state[which] = 2;
    uint32_t own = chunk->slot_count + chunk->stack_needed;
    depth[which] = deepest + 1;
    slots[which] = widest + own;
    // A chunk that reaches no host function is nought rather than its own
    // width: what this measures is where a call into the host happens, and one
    // that never happens is not a place.
    host_depth[which] = reaches_host ? host_deepest + 1 : 0;
    host_slots[which] = reaches_host ? host_widest + own : 0;
    host_from[which] = host_started;
    return true;
}

// Which functions lie on a run of calls that comes back round. A back edge is
// a call to a function the walk is already inside, and everything from that
// one up to where the walk is now goes round with it. A call through a value
// is an edge to every function this program ever names as one, the same set
// D814 measures against, so a loop that closes through a function value is one
// of these too.
static void cycle_walk(const KestModule *module, uint32_t which, uint8_t *state,
                       uint8_t *on_cycle, uint32_t *chain, uint32_t *where,
                       uint32_t depth) {
    state[which] = 1;
    where[which] = depth;
    chain[depth] = which;
    const KestChunk *chunk = module->functions[which];
    for (uint32_t at = 0; chunk != NULL && at < chunk->code_count;) {
        uint8_t op = chunk->code[at];
        uint32_t first = module->count;
        uint32_t last = module->count;
        if (op == KEST_OP_CALL) {
            first = read_u16(chunk, at + 1);
            last = first + 1;
        } else if (op == KEST_OP_CALL_VALUE) {
            first = 0;
            last = module->count;
        }
        for (uint32_t callee = first; callee < last; callee++) {
            if (callee >= module->count || module->functions[callee] == NULL ||
                (op == KEST_OP_CALL_VALUE &&
                 !module->functions[callee]->as_value)) {
                continue;
            }
            if (state[callee] == 1) {
                for (uint32_t back = where[callee]; back <= depth; back++) {
                    on_cycle[chain[back]] = 1;
                }
            } else if (state[callee] == 0) {
                cycle_walk(module, callee, state, on_cycle, chain, where,
                           depth + 1);
            }
        }
        at += kest_op_width(op);
    }
    state[which] = 2;
}

// Whether this body reaches a host function without going through a call:
// the first step of the closure below.
static bool calls_a_host(const KestChunk *chunk) {
    for (uint32_t at = 0; chunk != NULL && at < chunk->code_count;) {
        if (chunk->code[at] == KEST_OP_CALL_HOST) {
            return true;
        }
        at += kest_op_width(chunk->code[at]);
    }
    return false;
}

void kest_module_cycles(const KestModule *module, KestArena *arena,
                        int32_t only, bool to_host, uint32_t *widest,
                        uint32_t *widest_in_a_turn, uint32_t *all_the_rest) {
    *widest = 0;
    *widest_in_a_turn = 0;
    *all_the_rest = 0;
    if (module == NULL || module->count == 0 ||
        (only >= 0 && (uint32_t)only >= module->count)) {
        return;
    }
    KestMark before = kest_arena_mark(arena);
    uint8_t *state = KEST_ARENA_ARRAY(arena, uint8_t, module->count);
    uint8_t *on_cycle = KEST_ARENA_ARRAY(arena, uint8_t, module->count);
    uint32_t *chain = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    uint32_t *where = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    if (state == NULL || on_cycle == NULL || chain == NULL || where == NULL) {
        kest_arena_rewind(arena, before);
        return;
    }
    for (uint32_t i = 0; i < module->count; i++) {
        state[i] = 0;
        on_cycle[i] = 0;
    }
    if (only >= 0) {
        cycle_walk(module, (uint32_t)only, state, on_cycle, chain, where, 0);
    } else {
        for (uint32_t i = 0; i < module->count; i++) {
            if (state[i] == 0) {
                cycle_walk(module, i, state, on_cycle, chain, where, 0);
            }
        }
    }
    // And which of them reach a host function, when that is what was asked.
    // A body that never reaches one cannot be on a chain of frames that ends
    // at a host call, above it or below it, so it is not part of where the
    // machine can be when one happens. Walked to a standstill rather than in
    // order, because the graph has loops in it by the time this is asked and
    // an order is what a loop has not got. See D818.
    uint8_t *to_a_host = on_cycle;
    if (to_host) {
        to_a_host = KEST_ARENA_ARRAY(arena, uint8_t, module->count);
        if (to_a_host == NULL) {
            kest_arena_rewind(arena, before);
            return;
        }
        for (uint32_t i = 0; i < module->count; i++) {
            to_a_host[i] = (uint8_t)(state[i] != 0 &&
                                     calls_a_host(module->functions[i]));
        }
        for (bool moved = true; moved;) {
            moved = false;
            for (uint32_t i = 0; i < module->count; i++) {
                const KestChunk *one = module->functions[i];
                if (state[i] == 0 || to_a_host[i] || one == NULL) {
                    continue;
                }
                for (uint32_t at = 0; at < one->code_count;
                     at += kest_op_width(one->code[at])) {
                    uint8_t op = one->code[at];
                    uint32_t first = module->count;
                    uint32_t last = module->count;
                    if (op == KEST_OP_CALL) {
                        first = read_u16(one, at + 1);
                        last = first + 1;
                    } else if (op == KEST_OP_CALL_VALUE) {
                        first = 0;
                        last = module->count;
                    }
                    for (uint32_t callee = first;
                         callee < last && !to_a_host[i]; callee++) {
                        if (callee >= module->count ||
                            module->functions[callee] == NULL ||
                            (op == KEST_OP_CALL_VALUE &&
                             !module->functions[callee]->as_value)) {
                            continue;
                        }
                        if (to_a_host[callee]) {
                            to_a_host[i] = 1;
                            moved = true;
                        }
                    }
                }
            }
        }
    }
    // Only what the walk got to. A function nothing here reaches cannot stand
    // in a chain of frames under the one asked about, so it is not part of
    // what that one can want. See D817.
    for (uint32_t i = 0; i < module->count; i++) {
        const KestChunk *one = module->functions[i];
        uint32_t own =
            one == NULL ? 0 : (uint32_t)one->slot_count + one->stack_needed;
        if (state[i] == 0 || (to_host && !to_a_host[i])) {
            continue;
        }
        if (own > *widest) {
            *widest = own;
        }
        if (on_cycle[i]) {
            if (own > *widest_in_a_turn) {
                *widest_in_a_turn = own;
            }
        } else {
            *all_the_rest += own;
        }
    }
    kest_arena_rewind(arena, before);
}

// Whether an instruction reaches the heap. This is the list the machine
// itself keeps, read off the cases that call the allocator, and it is the one
// thing that makes a `no.alloc` promise a property of what runs rather than
// of what was read.
bool kest_op_allocates(uint8_t op) {
    // Every instruction is named, and none of them falls into a `default`: an
    // instruction added to the language that reaches the heap would otherwise
    // be one this proof does not know about, and a promise kept by not
    // looking. The walk over the tree is the first proof of a `no.alloc`
    // promise and this is the second; the second is what says the first was
    // wrong, so it cannot be the one that is quietly out of date.
    switch ((KestOp)op) {
    case KEST_OP_ARRAY:
    case KEST_OP_MAKE_ARRAY:
    case KEST_OP_PUSH:
    case KEST_OP_PUSH_TEXT:
    case KEST_OP_ROOM:
    case KEST_OP_ADD:
    case KEST_OP_NEW_STORE:
    case KEST_OP_TEXT_I:
    case KEST_OP_TEXT_U:
    case KEST_OP_TEXT_F:
    case KEST_OP_TEXT_F32:
    case KEST_OP_TEXT_B:
    case KEST_OP_TEXT_FLAGS:
    case KEST_OP_TEXT_VALUE:
    case KEST_OP_CONCAT:
    case KEST_OP_TEXT_FROM:
        return true;
    case KEST_OP_CONST:
    case KEST_OP_CONST_RUN:
    case KEST_OP_CONST_AT:
    case KEST_OP_LOAD:
    case KEST_OP_STORE:
    case KEST_OP_LOADN:
    case KEST_OP_STOREN:
    case KEST_OP_LOAD2:
    case KEST_OP_LOADK:
    case KEST_OP_FIELD:
    case KEST_OP_INDEX:
    case KEST_OP_INDEX_LL:
    case KEST_OP_POP_LAST:
    case KEST_OP_TAKE:
    case KEST_OP_CLEAR:
    case KEST_OP_FIT:
    case KEST_OP_FIT_TEXT:
    case KEST_OP_ELEM_ADDR:
    case KEST_OP_ELEM_AT:
    // Neither of these reaches the heap: one reads a place and one writes it,
    // and the block is the one the array already has.
    case KEST_OP_LOAD_ELEM:
    case KEST_OP_STORE_ELEM:
    case KEST_OP_INDEX_TO:
    case KEST_OP_ELEM_FROM:
    case KEST_OP_INDEX_TO_LL:
    case KEST_OP_ELEM_FROM_LL:
    case KEST_OP_MOD_I_C:
    case KEST_OP_MOD_I_K:
    case KEST_OP_DIV_I_C:
    case KEST_OP_DIV_I_K:
    case KEST_OP_ADD_I_NARROW_C:
    case KEST_OP_ADD_I_NARROW_K:
    case KEST_OP_SUB_I_NARROW_C:
    case KEST_OP_SUB_I_NARROW_K:
    case KEST_OP_MUL_I_NARROW_C:
    case KEST_OP_MUL_I_NARROW_K:
    case KEST_OP_FLOAT_BITS:
    case KEST_OP_BITS_F32:
    case KEST_OP_ADD_I_NARROW_TO:
    case KEST_OP_SUB_I_NARROW_TO:
    case KEST_OP_ADD_F_TO:
    case KEST_OP_SUB_F_TO:
    case KEST_OP_ADD_F_LL:
    case KEST_OP_SUB_F_LL:
    case KEST_OP_LOAD_SLOTS:
    case KEST_OP_STORE_SLOTS:
    case KEST_OP_OFFSET_ADDR:
    case KEST_OP_LOAD_AT:
    case KEST_OP_LEN:
    case KEST_OP_TEXT_LEN:
    case KEST_OP_TEXT_AT:
    case KEST_OP_TEXT_IN:
    case KEST_OP_SCRATCH:
    case KEST_OP_UNSCRATCH:
    case KEST_OP_TEXT_SLICE:
    case KEST_OP_TEXT_REST:
    case KEST_OP_TEXT_MATCHES:
    case KEST_OP_TEXT_FIND:
    case KEST_OP_HASH_I:
    case KEST_OP_HASH_F:
    case KEST_OP_HASH_T:
    case KEST_OP_HASH_VALUE:
    case KEST_OP_EQ_VALUE:
    case KEST_OP_NE_VALUE:
    case KEST_OP_GET:
    case KEST_OP_SET:
    case KEST_OP_REMOVE:
    case KEST_OP_COUNT:
    case KEST_OP_SEEK_FROM:
    case KEST_OP_SEEK_NEXT:
    case KEST_OP_STORE_REF:
    case KEST_OP_TRUE:
    case KEST_OP_FALSE:
    case KEST_OP_POP:
    case KEST_OP_POPN:
    case KEST_OP_ROTATE:
    case KEST_OP_ADD_I:
    case KEST_OP_SUB_I:
    case KEST_OP_MUL_I:
    case KEST_OP_DIV_I:
    case KEST_OP_MOD_I:
    case KEST_OP_DIV_U:
    case KEST_OP_MOD_U:
    case KEST_OP_NEG_I:
    case KEST_OP_AND_I:
    case KEST_OP_OR_I:
    case KEST_OP_XOR_I:
    case KEST_OP_NOT_I:
    case KEST_OP_SHL:
    case KEST_OP_SHR_I:
    case KEST_OP_SHR_U:
    case KEST_OP_NARROW:
    case KEST_OP_ADD_I_NARROW:
    case KEST_OP_SUB_I_NARROW:
    case KEST_OP_MUL_I_NARROW:
    case KEST_OP_I2F:
    case KEST_OP_U2F:
    case KEST_OP_F2I:
    case KEST_OP_TO_F32:
    case KEST_OP_ADD_F:
    case KEST_OP_SUB_F:
    case KEST_OP_MUL_F:
    case KEST_OP_DIV_F:
    case KEST_OP_MOD_F:
    case KEST_OP_NEG_F:
    case KEST_OP_ADD_F32:
    case KEST_OP_SUB_F32:
    case KEST_OP_MUL_F32:
    case KEST_OP_DIV_F32:
    case KEST_OP_MOD_F32:
    case KEST_OP_NEG_F32:
    case KEST_OP_LT_I:
    case KEST_OP_LE_I:
    case KEST_OP_GT_I:
    case KEST_OP_GE_I:
    case KEST_OP_LT_U:
    case KEST_OP_LE_U:
    case KEST_OP_GT_U:
    case KEST_OP_GE_U:
    case KEST_OP_LT_F:
    case KEST_OP_LE_F:
    case KEST_OP_GT_F:
    case KEST_OP_GE_F:
    case KEST_OP_EQ_I:
    case KEST_OP_NE_I:
    case KEST_OP_EQ_F:
    case KEST_OP_NE_F:
    case KEST_OP_EQ_T:
    case KEST_OP_NE_T:
    case KEST_OP_LT_T:
    case KEST_OP_LE_T:
    case KEST_OP_GT_T:
    case KEST_OP_GE_T:
    case KEST_OP_NOT:
    case KEST_OP_JUMP:
    case KEST_OP_JUMP_FALSE:
    case KEST_OP_JUMP_TRUE:
    case KEST_OP_JUMP_FALSE_LT_I:
    case KEST_OP_JUMP_FALSE_LE_I:
    case KEST_OP_JUMP_FALSE_GT_I:
    case KEST_OP_JUMP_FALSE_GE_I:
    case KEST_OP_JUMP_FALSE_EQ_I:
    case KEST_OP_JUMP_FALSE_NE_I:
    case KEST_OP_JUMP_TRUE_LT_I:
    case KEST_OP_JUMP_TRUE_LE_I:
    case KEST_OP_JUMP_TRUE_GT_I:
    case KEST_OP_JUMP_TRUE_GE_I:
    case KEST_OP_JUMP_TRUE_EQ_I:
    case KEST_OP_JUMP_TRUE_NE_I:
    case KEST_OP_JUMP_FALSE_LT_F:
    case KEST_OP_JUMP_FALSE_LE_F:
    case KEST_OP_JUMP_FALSE_GT_F:
    case KEST_OP_JUMP_FALSE_GE_F:
    case KEST_OP_JUMP_FALSE_EQ_F:
    case KEST_OP_JUMP_FALSE_NE_F:
    case KEST_OP_JUMP_TRUE_LT_F:
    case KEST_OP_JUMP_TRUE_LE_F:
    case KEST_OP_JUMP_TRUE_GT_F:
    case KEST_OP_JUMP_TRUE_GE_F:
    case KEST_OP_JUMP_TRUE_EQ_F:
    case KEST_OP_JUMP_TRUE_NE_F:
    case KEST_OP_STORE_K:
    case KEST_OP_ADD_K_SELF:
    case KEST_OP_SUB_K_SELF:
    case KEST_OP_JUMP_FALSE_LT_K:
    case KEST_OP_JUMP_FALSE_LE_K:
    case KEST_OP_JUMP_FALSE_GT_K:
    case KEST_OP_JUMP_FALSE_GE_K:
    case KEST_OP_JUMP_FALSE_EQ_K:
    case KEST_OP_JUMP_FALSE_NE_K:
    case KEST_OP_JUMP_FALSE_LT_C:
    case KEST_OP_JUMP_FALSE_LE_C:
    case KEST_OP_JUMP_FALSE_GT_C:
    case KEST_OP_JUMP_FALSE_GE_C:
    case KEST_OP_JUMP_FALSE_EQ_C:
    case KEST_OP_JUMP_FALSE_LT_FK:
    case KEST_OP_JUMP_FALSE_LE_FK:
    case KEST_OP_JUMP_FALSE_GT_FK:
    case KEST_OP_JUMP_FALSE_GE_FK:
    case KEST_OP_JUMP_FALSE_EQ_FK:
    case KEST_OP_JUMP_FALSE_NE_FK:
    case KEST_OP_JUMP_TRUE_LT_FK:
    case KEST_OP_JUMP_TRUE_LE_FK:
    case KEST_OP_JUMP_TRUE_GT_FK:
    case KEST_OP_JUMP_TRUE_GE_FK:
    case KEST_OP_JUMP_TRUE_EQ_FK:
    case KEST_OP_JUMP_TRUE_NE_FK:
    case KEST_OP_JUMP_FALSE_NE_C:
    case KEST_OP_JUMP_FALSE_LT_E:
    case KEST_OP_JUMP_FALSE_LE_E:
    case KEST_OP_JUMP_FALSE_GT_E:
    case KEST_OP_JUMP_FALSE_GE_E:
    case KEST_OP_JUMP_FALSE_EQ_E:
    case KEST_OP_JUMP_FALSE_NE_E:
    case KEST_OP_LOOP:
    case KEST_OP_NEXT_LESS_I:
    case KEST_OP_NEXT_LESS_U:
    case KEST_OP_CALL:
    case KEST_OP_CALL_VALUE:
    case KEST_OP_CALL_HOST:
    case KEST_OP_RETURN:
    // Nothing compiles to this, so nothing this proof reads ever holds one.
    // It is here because the switch has no `default` and that is the point of
    // the switch: an instruction added to the language has to be decided
    // about here rather than let through. See D991.
    case KEST_OP_STOP:
        return false;
    }
    return false;
}

const char *kest_op_stack(const KestModule *module, const KestChunk *chunk,
                          uint32_t at, uint32_t *takes, uint32_t *gives) {
    uint8_t op = chunk->code[at];
    uint32_t u[5] = {0, 0, 0, 0, 0};
    // A layout or a function this names is one there is before anything is
    // read out of it: the verifier has asked by the time it gets here, and
    // the machine that checks itself asks this before the handler's own
    // guard has had its turn.
    for (uint32_t k = 0; k < (kest_op_width(op) - 1) / 2 && k < 5; k++) {
        u[k] = read_u16(chunk, at + 1 + 2 * k);
        if ((INSTRUCTIONS[op].is[k] == IS_LAYOUT && u[k] >= module->layout_count) ||
            (INSTRUCTIONS[op].is[k] == IS_FUNCTION && u[k] >= module->count)) {
            return "names what the program has not got";
        }
    }
    // What a layout operand holds, in slots: the machine reads it as the
    // layout's width in one place and as the type's in another, and the two
    // are the same number except for a type of no slots, which the layout
    // counts as one.
#define LAID(k) ((uint32_t)module->layouts[u[k]].slots)
#define TYPED(k)                                                               \
    (module->layout_types[u[k]] == NULL ? UINT32_MAX                           \
                                        : (uint32_t)module->layout_types[u[k]]->slots)
    uint32_t t = 0;
    uint32_t g = 0;
    switch ((KestOp)op) {
    case KEST_OP_CONST:
    case KEST_OP_LOAD:
    case KEST_OP_TRUE:
    case KEST_OP_FALSE:
    case KEST_OP_TEXT_IN:
    case KEST_OP_MOD_I_K:
    case KEST_OP_DIV_I_K:
    case KEST_OP_ADD_I_NARROW_K:
    case KEST_OP_SUB_I_NARROW_K:
    case KEST_OP_MUL_I_NARROW_K:
        g = 1;
        break;
    case KEST_OP_LOAD2:
    case KEST_OP_LOADK:
        g = 2;
        break;
    case KEST_OP_CONST_RUN:
    case KEST_OP_LOADN:
        g = u[1];
        break;
    case KEST_OP_STOREN:
        t = u[1];
        break;
    case KEST_OP_CONST_AT:
    case KEST_OP_LOAD_SLOTS:
        t = 1;
        g = u[1];
        break;
    case KEST_OP_STORE_SLOTS:
        t = u[1] + 1;
        break;
    case KEST_OP_STORE:
    case KEST_OP_POP:
    case KEST_OP_CLEAR:
    case KEST_OP_JUMP_FALSE:
    case KEST_OP_JUMP_TRUE:
    case KEST_OP_JUMP_FALSE_LT_C:
    case KEST_OP_JUMP_FALSE_LE_C:
    case KEST_OP_JUMP_FALSE_GT_C:
    case KEST_OP_JUMP_FALSE_GE_C:
    case KEST_OP_JUMP_FALSE_EQ_C:
    case KEST_OP_JUMP_FALSE_NE_C:
        t = 1;
        break;
    case KEST_OP_POPN:
        t = u[0];
        break;
    case KEST_OP_ROTATE:
        t = u[0];
        g = u[0];
        break;
    case KEST_OP_FIELD:
        t = u[2];
        g = u[1];
        break;
    case KEST_OP_ARRAY:
        t = u[0] * LAID(1);
        g = 1;
        break;
    case KEST_OP_MAKE_ARRAY:
    case KEST_OP_FIT:
        t = 1 + LAID(0);
        g = 1;
        break;
    case KEST_OP_PUSH:
        t = 1 + LAID(0);
        break;
    case KEST_OP_PUSH_TEXT:
        t = 3;
        break;
    case KEST_OP_FIT_TEXT:
        t = 3;
        g = 1;
        break;
    case KEST_OP_ROOM:
    case KEST_OP_INDEX_TO:
    case KEST_OP_ELEM_FROM:
    case KEST_OP_ADD_I_NARROW_TO:
    case KEST_OP_SUB_I_NARROW_TO:
    case KEST_OP_ADD_F_TO:
    case KEST_OP_SUB_F_TO:
    case KEST_OP_JUMP_FALSE_LT_I:
    case KEST_OP_JUMP_FALSE_LE_I:
    case KEST_OP_JUMP_FALSE_GT_I:
    case KEST_OP_JUMP_FALSE_GE_I:
    case KEST_OP_JUMP_FALSE_EQ_I:
    case KEST_OP_JUMP_FALSE_NE_I:
    case KEST_OP_JUMP_TRUE_LT_I:
    case KEST_OP_JUMP_TRUE_LE_I:
    case KEST_OP_JUMP_TRUE_GT_I:
    case KEST_OP_JUMP_TRUE_GE_I:
    case KEST_OP_JUMP_TRUE_EQ_I:
    case KEST_OP_JUMP_TRUE_NE_I:
    case KEST_OP_JUMP_FALSE_LT_F:
    case KEST_OP_JUMP_FALSE_LE_F:
    case KEST_OP_JUMP_FALSE_GT_F:
    case KEST_OP_JUMP_FALSE_GE_F:
    case KEST_OP_JUMP_FALSE_EQ_F:
    case KEST_OP_JUMP_FALSE_NE_F:
    case KEST_OP_JUMP_TRUE_LT_F:
    case KEST_OP_JUMP_TRUE_LE_F:
    case KEST_OP_JUMP_TRUE_GT_F:
    case KEST_OP_JUMP_TRUE_GE_F:
    case KEST_OP_JUMP_TRUE_EQ_F:
    case KEST_OP_JUMP_TRUE_NE_F:
        t = 2;
        break;
    case KEST_OP_INDEX:
    case KEST_OP_TAKE:
        t = 2;
        g = LAID(0);
        break;
    case KEST_OP_ELEM_AT:
        t = 2;
        g = LAID(1);
        break;
    case KEST_OP_INDEX_LL:
        g = LAID(2);
        break;
    case KEST_OP_POP_LAST:
        t = 1;
        g = LAID(0) + 1;
        break;
    case KEST_OP_ELEM_ADDR:
    case KEST_OP_OFFSET_ADDR:
    case KEST_OP_REMOVE:
    case KEST_OP_STORE_REF:
    case KEST_OP_ADD_I:
    case KEST_OP_SUB_I:
    case KEST_OP_MUL_I:
    case KEST_OP_DIV_I:
    case KEST_OP_MOD_I:
    case KEST_OP_DIV_U:
    case KEST_OP_MOD_U:
    case KEST_OP_AND_I:
    case KEST_OP_OR_I:
    case KEST_OP_XOR_I:
    case KEST_OP_SHL:
    case KEST_OP_SHR_I:
    case KEST_OP_SHR_U:
    case KEST_OP_ADD_I_NARROW:
    case KEST_OP_SUB_I_NARROW:
    case KEST_OP_MUL_I_NARROW:
    case KEST_OP_ADD_F:
    case KEST_OP_SUB_F:
    case KEST_OP_MUL_F:
    case KEST_OP_DIV_F:
    case KEST_OP_MOD_F:
    case KEST_OP_ADD_F32:
    case KEST_OP_SUB_F32:
    case KEST_OP_MUL_F32:
    case KEST_OP_DIV_F32:
    case KEST_OP_MOD_F32:
    case KEST_OP_LT_I:
    case KEST_OP_LE_I:
    case KEST_OP_GT_I:
    case KEST_OP_GE_I:
    case KEST_OP_LT_U:
    case KEST_OP_LE_U:
    case KEST_OP_GT_U:
    case KEST_OP_GE_U:
    case KEST_OP_LT_F:
    case KEST_OP_LE_F:
    case KEST_OP_GT_F:
    case KEST_OP_GE_F:
    case KEST_OP_EQ_I:
    case KEST_OP_NE_I:
    case KEST_OP_EQ_F:
    case KEST_OP_NE_F:
    case KEST_OP_HASH_T:
    case KEST_OP_TEXT_LEN:
        t = 2;
        g = 1;
        break;
    case KEST_OP_LOAD_AT:
        t = 1;
        g = LAID(1);
        break;
    case KEST_OP_LOAD_ELEM:
        t = 2;
        g = 2 + LAID(1);
        break;
    case KEST_OP_STORE_ELEM:
        t = 2 + LAID(1);
        break;
    case KEST_OP_LEN:
    case KEST_OP_COUNT:
    case KEST_OP_HASH_I:
    case KEST_OP_HASH_F:
    case KEST_OP_NEW_STORE:
    case KEST_OP_NEG_I:
    case KEST_OP_NOT_I:
    case KEST_OP_NARROW:
    case KEST_OP_I2F:
    case KEST_OP_U2F:
    case KEST_OP_F2I:
    case KEST_OP_TO_F32:
    case KEST_OP_NEG_F:
    case KEST_OP_NEG_F32:
    case KEST_OP_NOT:
    case KEST_OP_MOD_I_C:
    case KEST_OP_DIV_I_C:
    case KEST_OP_ADD_I_NARROW_C:
    case KEST_OP_SUB_I_NARROW_C:
    case KEST_OP_MUL_I_NARROW_C:
    case KEST_OP_FLOAT_BITS:
    case KEST_OP_BITS_F32:
        t = 1;
        g = 1;
        break;
    case KEST_OP_TEXT_AT:
        t = 3;
        g = 1;
        break;
    case KEST_OP_TEXT_SLICE:
        t = 4;
        g = 2;
        break;
    case KEST_OP_TEXT_REST:
        t = 3;
        g = 2;
        break;
    case KEST_OP_TEXT_MATCHES:
        t = 5;
        g = 1;
        break;
    case KEST_OP_TEXT_FIND:
        t = 5;
        g = 2;
        break;
    case KEST_OP_TEXT_I:
    case KEST_OP_TEXT_U:
    case KEST_OP_TEXT_F:
    case KEST_OP_TEXT_F32:
    case KEST_OP_TEXT_B:
    case KEST_OP_TEXT_FROM:
        t = 1;
        g = 2;
        break;
    case KEST_OP_TEXT_FLAGS:
    case KEST_OP_TEXT_VALUE:
        t = TYPED(0);
        g = 2;
        break;
    case KEST_OP_CONCAT:
        t = 2 * u[0];
        g = 2;
        break;
    case KEST_OP_HASH_VALUE:
        t = TYPED(0);
        g = 1;
        break;
    case KEST_OP_EQ_VALUE:
    case KEST_OP_NE_VALUE:
        t = TYPED(0) == UINT32_MAX ? UINT32_MAX : 2 * TYPED(0);
        g = 1;
        break;
    case KEST_OP_EQ_T:
    case KEST_OP_NE_T:
    case KEST_OP_LT_T:
    case KEST_OP_LE_T:
    case KEST_OP_GT_T:
    case KEST_OP_GE_T:
        t = 4;
        g = 1;
        break;
    case KEST_OP_ADD:
        t = u[0] + 1;
        g = 1;
        break;
    case KEST_OP_GET:
        t = 2;
        g = u[0] + 1;
        break;
    case KEST_OP_SET:
        t = u[0] + 2;
        g = 1;
        break;
    case KEST_OP_SEEK_FROM:
    case KEST_OP_SEEK_NEXT:
    case KEST_OP_JUMP:
    case KEST_OP_STORE_K:
    case KEST_OP_ADD_K_SELF:
    case KEST_OP_SUB_K_SELF:
    case KEST_OP_JUMP_FALSE_LT_K:
    case KEST_OP_JUMP_FALSE_LE_K:
    case KEST_OP_JUMP_FALSE_GT_K:
    case KEST_OP_JUMP_FALSE_GE_K:
    case KEST_OP_JUMP_FALSE_EQ_K:
    case KEST_OP_JUMP_FALSE_NE_K:
    case KEST_OP_JUMP_FALSE_LT_FK:
    case KEST_OP_JUMP_FALSE_LE_FK:
    case KEST_OP_JUMP_FALSE_GT_FK:
    case KEST_OP_JUMP_FALSE_GE_FK:
    case KEST_OP_JUMP_FALSE_EQ_FK:
    case KEST_OP_JUMP_FALSE_NE_FK:
    case KEST_OP_JUMP_TRUE_LT_FK:
    case KEST_OP_JUMP_TRUE_LE_FK:
    case KEST_OP_JUMP_TRUE_GT_FK:
    case KEST_OP_JUMP_TRUE_GE_FK:
    case KEST_OP_JUMP_TRUE_EQ_FK:
    case KEST_OP_JUMP_TRUE_NE_FK:
    case KEST_OP_ADD_F_LL:
    case KEST_OP_SUB_F_LL:
    case KEST_OP_INDEX_TO_LL:
    case KEST_OP_ELEM_FROM_LL:
    case KEST_OP_JUMP_FALSE_LT_E:
    case KEST_OP_JUMP_FALSE_LE_E:
    case KEST_OP_JUMP_FALSE_GT_E:
    case KEST_OP_JUMP_FALSE_GE_E:
    case KEST_OP_JUMP_FALSE_EQ_E:
    case KEST_OP_JUMP_FALSE_NE_E:
    case KEST_OP_LOOP:
    case KEST_OP_NEXT_LESS_I:
    case KEST_OP_NEXT_LESS_U:
    case KEST_OP_SCRATCH:
    case KEST_OP_UNSCRATCH:
    case KEST_OP_STOP:
        break;
    case KEST_OP_CALL: {
        // What a call leaves is what the body it enters gives back, which is
        // held to its declaration at every `return` it reaches; what it takes
        // is held here, because the machine starts the callee's frame that
        // many slots down and a frame that starts anywhere else reads the
        // caller's values as its own arguments.
        const KestChunk *callee = module->functions[u[0]];
        if (u[1] != callee->param_slots) {
            return "hands a function other than what it takes";
        }
        t = u[1];
        g = callee->result_slots;
        break;
    }
    case KEST_OP_CALL_VALUE:
        // The function is a number only a run can know, so the machine holds
        // the one it finds to taking and giving what this says.
        t = u[0] + 1;
        g = u[1];
        break;
    case KEST_OP_CALL_HOST:
        t = u[1];
        g = u[2];
        break;
    case KEST_OP_RETURN:
        t = u[0];
        break;
    }
#undef LAID
#undef TYPED
    if (t == UINT32_MAX) {
        return "reads a value of a layout with no type";
    }
    *takes = t;
    *gives = g;
    return NULL;
}

bool kest_module_needs(const KestModule *module, KestArena *arena,
                       int32_t only, uint32_t *stack_slots,
                       uint32_t *call_depth, uint32_t *from_host_slots,
                       uint32_t *from_host_frames, KestNoLeast *reasons,
                       KestReason *why) {
    why->reach = KEST_REACH_KNOWN;
    why->where = NULL;
    if (from_host_slots != NULL) {
        *from_host_slots = 0;
    }
    if (from_host_frames != NULL) {
        *from_host_frames = 0;
    }
    if (module->count == 0) {
        *stack_slots = 0;
        *call_depth = 0;
        return true;
    }
    uint8_t *state = kest_arena_alloc(arena, module->count, 1);
    uint32_t *depth = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    uint32_t *slots = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    uint32_t *host_depth = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    uint32_t *host_slots = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    uint32_t *host_from = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    if (state == NULL || depth == NULL || slots == NULL ||
        host_depth == NULL || host_slots == NULL || host_from == NULL) {
        // Not that there is no answer: a host that frees something and asks
        // again may be told one, and a host told nothing was asked would not
        // know to. See D566.
        why->reach = KEST_REACH_NO_ROOM;
        return false;
    }
    memset(state, 0, module->count);

    // A host may call anything the program defines, so the answer is the worst
    // of them — unless a host says which one it calls, and then the answer is
    // that one and what it reaches. A host that knows is not made to pay for
    // what it will never call.
    uint32_t worst_depth = 0;
    uint32_t worst_slots = 0;
    uint32_t worst_host_depth = 0;
    uint32_t worst_host_slots = 0;
    uint32_t started_at = 0;
    uint32_t from = only < 0 ? 0 : (uint32_t)only;
    uint32_t until = only < 0 ? module->count : from + 1;
    if (from >= module->count) {
        why->reach = KEST_REACH_NO_NAME;
        return false;
    }
    // The first function with no answer is the one the program is told about,
    // and it is not the only one there is: a caller of it has no answer
    // either, and a walk that stopped at the first would leave every other one
    // looking as if it had been worked out. So when somebody wants them all,
    // the walk carries on and the first reason is kept. See D601.
    bool answered = true;
    KestReason first = {KEST_REACH_KNOWN, NULL};
    for (uint32_t i = from; i < until; i++) {
        if (!measure_chunk(module, i, state, depth, slots, host_depth,
                           host_slots, host_from, reasons, why)) {
            if (answered) {
                first = *why;
                answered = false;
            }
            if (reasons == NULL) {
                return false;
            }
            continue;
        }
        if (depth[i] > worst_depth) {
            worst_depth = depth[i];
        }
        if (slots[i] > worst_slots) {
            worst_slots = slots[i];
        }
        if (host_depth[i] > worst_host_depth) {
            worst_host_depth = host_depth[i];
        }
        if (host_slots[i] > worst_host_slots) {
            worst_host_slots = host_slots[i];
            started_at = host_from[i];
        }
    }
    // And what each of them needs on its own, which the walk above worked out
    // for every function it reached: a host that calls one function is sized
    // for that one rather than for the worst there is, and it does not have to
    // ask about it by name to be told. See D603.
    if (reasons != NULL) {
        for (uint32_t i = 0; i < module->count; i++) {
            if (state[i] == 2) {
                reasons[i].slots = slots[i];
                reasons[i].frames = depth[i];
            }
        }
    }
    if (!answered) {
        *why = first;
        return false;
    }
    *stack_slots = worst_slots;
    *call_depth = worst_depth;
    if (from_host_slots != NULL) {
        *from_host_slots = worst_host_slots;
        // And the function it is in, in the field that says which function an
        // answer is about. A host reads the number to size a machine; the name
        // is what it would have to shorten to make the number smaller, and
        // nothing said it. See D605.
        if (worst_host_slots > 0 && started_at < module->count) {
            why->where = module->functions[started_at]->name;
        }
    }
    if (from_host_frames != NULL) {
        *from_host_frames = worst_host_depth;
    }
    return true;
}

// Prints one instruction and says where the next one starts. What it prints is
// its own business; how far it moves is `kest_op_width` and nothing else.
static uint32_t disassemble_one(const KestModule *module,
                                const KestChunk *chunk, uint32_t offset,
                                FILE *out) {
    uint8_t op = chunk->code[offset];
    const Instruction *instruction = &INSTRUCTIONS[op];
    // Wide enough for the longest name there is, so a number after one never
    // runs into it.
    // A name as wide as the column still has a space after it.
    fprintf(out, "  %04u  %-16s%s", offset, instruction->name,
            strlen(instruction->name) >= 16 ? " " : "");

    switch (instruction->operands) {
    case NONE:
        fputc('\n', out);
        break;
    case U16: {
        uint16_t operand = read_u16(chunk, offset + 1);
        if (op == KEST_OP_CONST) {
            KestValue value = chunk->constants[operand];
            switch (chunk->constant_classes[operand]) {
            case KEST_CONST_FLOAT:
                fprintf(out, "%u  ; %g\n", operand, value.real);
                break;
            case KEST_CONST_TEXT:
                fprintf(out, "%u  ; \"%s\"\n", operand, value.text);
                break;
            default:
                fprintf(out, "%u  ; %lld\n", operand,
                        (long long)value.integer);
            }
        } else {
            fprintf(out, "%u\n", operand);
        }
        break;
    }
    case U16_U16:
        // The name of what a call reaches, beside the number that reaches it.
        // Every other instruction that names something says what it named —
        // a constant prints its value — and this one printed an index into a
        // list a reader would have to count. What calls what is the one thing
        // the emitted code already knows and nobody could read. See D599.
        if (op == KEST_OP_CALL && module != NULL &&
            read_u16(chunk, offset + 1) < module->count) {
            fprintf(out, "%u  %u  ; %s\n", read_u16(chunk, offset + 1),
                    read_u16(chunk, offset + 3),
                    module->functions[read_u16(chunk, offset + 1)]->name);
            break;
        }
        fprintf(out, "%u  %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3));
        break;
    case U16_U16_U16:
        fprintf(out, "+%u  %u of %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3), read_u16(chunk, offset + 5));
        break;
    case JUMP:
        fprintf(out, "%u  -> %u\n", read_u16(chunk, offset + 1),
                offset + 3 + read_u16(chunk, offset + 1));
        break;
    case BACK:
        fprintf(out, "%u  -> %u\n", read_u16(chunk, offset + 1),
                offset + 3 - read_u16(chunk, offset + 1));
        break;
    case WALK:
        fprintf(out, "%u  < %u  -> %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3),
                offset + 7 - read_u16(chunk, offset + 5));
        break;
    case FIND:
        fprintf(out, "%u  %u  -> %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3),
                offset + 7 + read_u16(chunk, offset + 5));
        break;
    case FIND_BACK:
        fprintf(out, "%u  %u  -> %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3),
                offset + 7 - read_u16(chunk, offset + 5));
        break;
    case WEIGH:
        fprintf(out, "%u  -> %u\n", read_u16(chunk, offset + 1),
                offset + 5 + read_u16(chunk, offset + 3));
        break;
    case U16_X4:
        fprintf(out, "%u  %u  %u[%u]\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3), read_u16(chunk, offset + 5),
                read_u16(chunk, offset + 7));
        break;
    case U16_X5:
        fprintf(out, "+%u  %u  %u  %u[%u]\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3), read_u16(chunk, offset + 5),
                read_u16(chunk, offset + 7), read_u16(chunk, offset + 9));
        break;
    case WEIGH_ELEMENT:
        fprintf(out, "%u  %u  %u  %u  -> %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3), read_u16(chunk, offset + 5),
                read_u16(chunk, offset + 7),
                offset + 11 + read_u16(chunk, offset + 9));
        break;
    }
    return offset + kest_op_width(op);
}

static const char *const SCALARS[] = {"i8",  "i16", "i32",     "i64",
                                     "u8",  "u16", "u32",     "u64",
                                     "f32", "f64", "word",    "text",
                                     "payload", "tag", "held", "bool",
                                     "nothing", "flags8", "flags16",
                                     "flags32", "flags64", "fn", "ref"};

// A reason built where it is kept, because it names the type the word did not
// fit in (D193).
static const char *reason_of(KestArena *arena, const char *format, ...) {
    va_list args;
    va_list again;
    va_start(args, format);
    va_copy(again, args);
    int room = vsnprintf(NULL, 0, format, args);
    va_end(args);
    char *out = room < 0 ? NULL : kest_arena_alloc(arena, (size_t)room + 1, 1);
    if (out != NULL) {
        vsnprintf(out, (size_t)room + 1, format, again);
    }
    va_end(again);
    return out == NULL ? "does not fit" : out;
}

// The ends of a whole number of that width and sign. `u64` is cut short at
// what a signed read can carry, which is the widest thing the shell can hand
// over anyway.
static void ends_of(const KestType *type, long long *low, long long *high) {
    if (type->is_signed) {
        *high = type->width >= 64 ? LLONG_MAX
                                  : ((long long)1 << (type->width - 1)) - 1;
        *low = -*high - 1;
        return;
    }
    *low = 0;
    *high = type->width >= 63 ? LLONG_MAX
                              : ((long long)1 << type->width) - 1;
}

bool kest_value_read(KestArena *arena, const char *text, const KestType *type,
                     KestValue *into, const char **why) {
    char *end = NULL;
    switch (type->tag) {
    case KEST_T_INT: {
        errno = 0;
        long long value = strtoll(text, &end, 0);
        if (end == text || *end != '\0') {
            *why = "is not a number";
            return false;
        }
        long long low = 0;
        long long high = 0;
        ends_of(type, &low, &high);
        // A number the machine cannot carry, and one it can carry and the
        // type cannot hold. Both were taken before, and what the program got
        // was a number nobody typed.
        if (errno == ERANGE || value < low || value > high) {
            *why = reason_of(arena, "does not fit in `%s`",
                             kest_type_name(arena, type));
            return false;
        }
        into->integer = value;
        return true;
    }
    case KEST_T_FLOAT: {
        errno = 0;
        double value = strtod(text, &end);
        if (end == text || *end != '\0') {
            *why = "is not a number";
            return false;
        }
        if (errno == ERANGE && value != 0.0) {
            *why = reason_of(arena, "does not fit in `%s`",
                             kest_type_name(arena, type));
            return false;
        }
        if (type->width == 32 && value > (double)FLT_MAX) {
            *why = reason_of(arena, "does not fit in `%s`",
                             kest_type_name(arena, type));
            return false;
        }
        into->real = type->width == 32 ? (double)(float)value : value;
        return true;
    }
    case KEST_T_BOOL:
        if (strcmp(text, "true") == 0 || strcmp(text, "false") == 0) {
            into->integer = strcmp(text, "true") == 0;
            return true;
        }
        *why = "is not `true` or `false`";
        return false;
    case KEST_T_TEXT:
        into->text = text;
        return true;
    default:
        *why = "cannot be written as a word";
        return false;
    }
}

const char *kest_scalar_name(uint8_t kind) {
    // One list, so a message about what a slot holds and a walk that prints a
    // layout cannot come to call the same thing two names.
    return kind < sizeof(SCALARS) / sizeof(SCALARS[0]) ? SCALARS[kind]
                                                       : "something else";
}

_Static_assert(sizeof(SCALARS) / sizeof(SCALARS[0]) == KEST_L_REF + 1,
               "every scalar a layout holds has a name and nothing else does");

// What a reason there is no least is called, which the JSON and the words a
// listing prints are the same list of: a reason added to `KestReach` is caught
// here rather than printed as whatever the last one fell through to.
const char *kest_reach_name(KestReach reach) {
    switch (reach) {
    case KEST_REACH_KNOWN:
        return "worked out";
    case KEST_REACH_ITSELF:
        return "reaches itself";
    case KEST_REACH_VALUE:
        return "calls through a value";
    case KEST_REACH_NO_NAME:
        return "no function of that name";
    case KEST_REACH_NO_ROOM:
        return "no room to work it out";
    case KEST_REACH_UNASKED:
        return "not worked out";
    }
    // Not reached while those are the reasons there are, and the switch above
    // is what holds them to being all of them.
    return "not worked out";
}

void kest_module_needs_json(const KestModule *module, int32_t only,
                            FILE *out) {
    uint32_t stack = 0;
    uint32_t deep = 0;
    KestReason why = {KEST_REACH_UNASKED, NULL};
    if (kest_module_needs(module, module->arena, only, &stack, &deep, NULL,
                          NULL, NULL, &why)) {
        fprintf(out, "\"slots\":%u,\"frames\":%u", stack, deep);
        return;
    }
    fputs("\"slots\":null,\"frames\":null,\"why\":", out);
    kest_json_text(kest_reach_name(why.reach), out);
    fputs(",\"where\":", out);
    if (why.where == NULL) {
        fputs("null", out);
    } else {
        kest_json_text(why.where, out);
    }
}

// The fold is `kest_mark_bytes`, which is the one arithmetic every mark here is
// made of; these are what a module is folded in terms of. See D663.
static void fold(uint64_t *mark, const void *bytes, size_t length) {
    *mark = kest_mark_bytes(*mark, bytes, length);
}

static void fold_text(uint64_t *mark, const char *text) {
    fold(mark, text == NULL ? "" : text, text == NULL ? 1 : strlen(text) + 1);
}

static void fold_number(uint64_t *mark, uint64_t value, unsigned bytes) {
    *mark = kest_mark_number(*mark, value, bytes);
}

uint64_t kest_layout_mark(const KestLayout *layout) {
    if (layout == NULL) {
        return 0;
    }
    uint64_t mark = KEST_MARK_START;
    fold_number(&mark, layout->size, 2);
    fold_number(&mark, layout->align, 2);
    // How many slots it is, which is not how many pieces it has: a piece of
    // text is one piece and two slots, and a host filling a frame is told the
    // second. See D964.
    fold_number(&mark, layout->slots, 2);
    fold_number(&mark, layout->tagged, 1);
    for (uint16_t p = 0; p < layout->count; p++) {
        fold_number(&mark, layout->pieces[p].offset, 2);
        fold_number(&mark, layout->pieces[p].kind, 1);
        // The name is part of the shape for the question this answers: a field
        // somebody renamed is a field a save format has to be told about, and
        // a number that stayed the same would be the host told nothing.
        fold_text(&mark, layout->pieces[p].name);
        // And which cases a tag can name, in the order they are numbered. A
        // tag is a number and the number is the case's place, so a case put in
        // the middle of an enum renumbers every case after it: a world saved
        // before it reads back with each of those meaning the one below. The
        // size does not move, the pieces do not move and nothing else here
        // moved either, so until this was folded the mark said nothing had
        // changed. See D1072.
        if (layout->pieces[p].kind == KEST_L_TAG) {
            for (int32_t tag = 0;; tag++) {
                const char *named = kest_case_of(layout, p, tag, NULL, NULL);
                if (named == NULL) {
                    fold_number(&mark, (uint64_t)tag, 2);
                    break;
                }
                fold_text(&mark, named);
            }
            continue;
        }
        // And which bits a set of named ones has, in the order they are
        // declared. A bit is one shifted by its place in the list, so a bit
        // put in the middle doubles every bit after it -- and the width, the
        // size, the alignment and every piece stay exactly where they were,
        // which is the same shape of silence a case put in the middle of an
        // enum was until D1072. See D1076.
        if (layout->pieces[p].kind < KEST_L_FLAGS8 ||
            layout->pieces[p].kind > KEST_L_FLAGS64) {
            continue;
        }
        uint16_t walked = 0;
        const KestType *set =
            named_at(layout->type, p, &walked, (uint8_t)KEST_T_FLAGS);
        if (set == NULL) {
            continue;
        }
        for (uint32_t bit = 0; bit < set->case_count; bit++) {
            fold_text(&mark, set->cases[bit].name);
        }
        fold_number(&mark, set->case_count, 2);
    }
    return mark;
}

// How many bytes a text constant is. Text is two constants since D964 -- the
// bytes and how many -- so the count is the one after it, and what a mark is
// taken over is that many bytes rather than as far as the first nought, which
// is a byte text may hold since D971. Two pieces of text that are the same up
// to a nought and differ after it are two programs, and a mark that could not
// tell them apart is a reload that says nothing moved.
static size_t text_constant_length(const KestChunk *chunk, uint32_t which,
                                   const char *bytes) {
    if (bytes == NULL) {
        return 0;
    }
    if (which + 1 < chunk->constant_count) {
        return (size_t)chunk->constants[which + 1].integer;
    }
    return strlen(bytes);
}

// The bytes of a text constant, folded over as many as there are. A constant
// with no bytes at all is folded as the empty one, so that a chunk which has
// one and a chunk which does not are two chunks.
static void fold_text_bytes(uint64_t *mark, const char *bytes, size_t length) {
    fold(mark, bytes == NULL ? "" : bytes, bytes == NULL ? 1 : length);
}

static uint64_t body_mark(const KestChunk *chunk) {
    uint64_t mark = KEST_MARK_START;
    fold(&mark, chunk->code, chunk->code_count);
    for (uint32_t which = 0; which < chunk->constant_count; which++) {
        fold_number(&mark, chunk->constant_classes[which], 1);
        if (chunk->constant_classes[which] == KEST_CONST_TEXT) {
            fold_text_bytes(&mark, chunk->constants[which].text,
                            text_constant_length(
                                chunk, which, chunk->constants[which].text));
        } else {
            fold_number(&mark, (uint64_t)chunk->constants[which].integer, 8);
        }
    }
    return mark;
}

uint64_t kest_module_mark(const KestModule *module) {
    if (module == NULL) {
        return 0;
    }
    // What the machine will run, and nothing about where it was written. The
    // instructions, the constants, the shapes crossing the boundary, the names
    // a host looks up and the promises it is held to — but not the spans, not
    // the origins and not the file, because a program with a comment added is
    // the same program to run and a host caching what it compiled would
    // otherwise throw it away for a reformat. What that costs a reader is that
    // two programs with one mark may say different places when they fail, and
    // the reference says so. See D659.
    uint64_t mark = KEST_MARK_START;
    fold_text(&mark, module->alias);
    for (uint32_t at = 0; at < module->count; at++) {
        const KestChunk *chunk = module->functions[at];
        fold_text(&mark, chunk->name);
        fold(&mark, chunk->code, chunk->code_count);
        for (uint32_t which = 0; which < chunk->constant_count; which++) {
            fold_number(&mark, chunk->constant_classes[which], 1);
            if (chunk->constant_classes[which] == KEST_CONST_TEXT) {
                fold_text_bytes(&mark, chunk->constants[which].text,
                            text_constant_length(
                                chunk, which, chunk->constants[which].text));
            } else {
                fold_number(&mark,
                            (uint64_t)chunk->constants[which].integer, 8);
            }
        }
        for (uint16_t which = 0; which < chunk->takes_count; which++) {
            fold_number(&mark, chunk->takes[which], 2);
        }
        fold_number(&mark, chunk->gives, 2);
        fold_number(&mark, chunk->param_slots, 2);
        fold_number(&mark, chunk->result_slots, 2);
        fold_number(&mark, chunk->slot_count, 2);
        fold_number(&mark, chunk->stack_needed, 2);
        fold_number(&mark, chunk->returns_value, 1);
        fold_number(&mark, chunk->no_alloc, 1);
        fold_number(&mark, chunk->no_host, 1);
        fold_number(&mark, chunk->deterministic, 1);
    }
    for (uint32_t at = 0; at < module->extern_count; at++) {
        const KestExtern *host = &module->externs[at];
        fold_text(&mark, host->name);
        for (uint16_t which = 0; which < host->takes_count; which++) {
            fold_number(&mark, host->takes[which], 2);
        }
        fold_number(&mark, host->gives, 2);
        fold_number(&mark, host->gives_value, 1);
        fold_number(&mark, host->promises, 1);
        fold_number(&mark, host->deterministic, 1);
    }
    for (uint32_t at = 0; at < module->layout_count; at++) {
        const KestLayout *shape = &module->layouts[at];
        fold_number(&mark, shape->size, 2);
        fold_number(&mark, shape->align, 2);
        // How wide it is in slots, which is not how many pieces it has: a
        // piece of text is one piece and two slots. A host filling a frame is
        // told the second, so a build where that moved is a build a host has
        // to be told about. See D964.
        fold_number(&mark, shape->slots, 2);
        fold_number(&mark, shape->tagged, 1);
        for (uint16_t piece = 0; piece < shape->count; piece++) {
            fold_number(&mark, shape->pieces[piece].offset, 2);
            fold_number(&mark, shape->pieces[piece].kind, 1);
        }
    }
    return mark;
}

void kest_module_disassemble_json(const KestModule *module,
                                  const char *const *entries, FILE *out) {
    fputs("\"layouts\":[", out);
    for (uint32_t i = 0; i < module->layout_count; i++) {
        const KestLayout *layout = &module->layouts[i];
        // `tagged` is what the C side of this carries and the JSON did not:
        // a host reading pieces has to know whether they are pieces it may
        // walk or a payload it has to switch on.
        fprintf(out, "%s{\"bytes\":%u,\"align\":%u,\"tagged\":%s,\"of\":",
                i == 0 ? "" : ",", layout->size, layout->align,
                layout->tagged ? "true" : "false");
        // Which type this is the layout of. Without it two layouts that differ
        // only in what they are of read as one — every `[T]` is one word
        // whatever `T` is — and a reader counting what a module holds would
        // say half of them were written twice. They are not: the machine reads
        // `type` to pack a value across the boundary, to name an enum's cases
        // and to ask whether a value holds its own memory, so `[i32]` and
        // `[text]` are one shape and two layouts. See D779.
        if (layout->type != NULL) {
            kest_json_text(kest_type_name(module->arena, layout->type), out);
        } else {
            fputs("null", out);
        }
        fputs(",\"pieces\":[", out);
        for (uint16_t p = 0; p < layout->count; p++) {
            fprintf(out, "%s{\"byte\":%u,\"is\":\"%s\",\"name\":",
                    p == 0 ? "" : ",", layout->pieces[p].offset,
                    SCALARS[layout->pieces[p].kind]);
            // Null rather than left out where a piece is nobody's field, so a
            // tool reading these reads the same shape for every piece. See
            // D946.
            if (layout->pieces[p].name == NULL) {
                fputs("null", out);
            } else {
                kest_json_text(layout->pieces[p].name, out);
            }
            fputc('}', out);
        }
        fprintf(out, "],\"mark\":\"%016llx\"}",
                (unsigned long long)kest_layout_mark(layout));
    }

    fputs("],\"hosts\":[", out);
    for (uint32_t i = 0; i < module->extern_count; i++) {
        fputs(i == 0 ? "" : ",", out);
        kest_json_text(module->externs[i].name, out);
    }

    // The same two numbers the text form prints, and null where there are
    // none to give: a run of calls that comes back round has no deepest frame
    // and a call through a value reaches what is not known until it runs, so
    // `why` says which of the two it was.
    fputs("],\"needs\":{", out);
    kest_module_needs_json(module, -1, out);

    // And one for each name the caller asked about that the program has,
    // whether or not it differs from the whole. The text form leaves out the
    // ones that are the same because a reader would be reading them twice; a
    // tool looks one up by name and wants it there.
    fputs(",\"entries\":[", out);
    bool first_entry = true;
    for (uint32_t e = 0; entries != NULL && entries[e] != NULL; e++) {
        int32_t at = kest_module_entry(module, entries[e]);
        if (at < 0) {
            continue;
        }
        fputs(first_entry ? "{\"name\":" : ",{\"name\":", out);
        first_entry = false;
        kest_json_text(entries[e], out);
        fputc(',', out);
        kest_module_needs_json(module, at, out);
        fputc('}', out);
    }
    fputs("]}", out);

    // The same walk the words make, for the same reason: which function the
    // answer stopped at is a thing about that function and is said beside it.
    uint32_t reached = 0;
    uint32_t deep = 0;
    KestReason why = {KEST_REACH_UNASKED, NULL};
    // One byte a function, which is what saying it about every one of them
    // costs: the walk visits them all anyway, and a reason it does not write
    // down is a function that looks as if it had been worked out. See D601.
    KestNoLeast *reasons =
        module->count == 0
            ? NULL
            : KEST_ARENA_ARRAY(module->arena, KestNoLeast, module->count);
    if (reasons != NULL) {
        memset(reasons, 0, sizeof(KestNoLeast) * module->count);
    }
    (void)kest_module_needs(module, module->arena, -1, &reached, &deep, NULL,
                            NULL, reasons, &why);

    fputs(",\"functions\":[", out);
    for (uint32_t i = 0; i < module->count; i++) {
        const KestChunk *chunk = module->functions[i];
        fputs(i == 0 ? "" : ",", out);
        fputs("{\"name\":", out);
        kest_json_text(chunk->name, out);
        // And what it was written as, which is the name a declaration has and
        // this one has what it was compiled with on the end of. A tool reading
        // a listing beside `check` joins the two on it; cutting the name at
        // the `#` is the same rule written a second time, in whoever is
        // reading it. The words form says it as the front of the name, which
        // is where a person reads it. See D611.
        fputs(",\"wrote\":", out);
        kest_json_text(chunk->wrote, out);
        // And where the declaration it was compiled from is written, under the
        // names `check` lists a declaration's place under: two chunks written
        // the same and declared in one place are one generic compiled twice,
        // and two chunks written the same and declared in two are two
        // functions of a name. Nothing said which before. See D612.
        if (chunk->source != NULL) {
            uint32_t line = 0;
            uint32_t column = 0;
            kest_source_locate(chunk->source, chunk->declared.offset, &line,
                               &column);
            fputs(",\"file\":", out);
            kest_json_text(chunk->source->path, out);
            fprintf(out, ",\"line\":%u,\"column\":%u", line, column);
        }
        // What the code itself takes, which is the one thing about a compiled
        // function a reader could only get by adding up the instructions
        // below and knowing how wide each of them is. See D744. Beside it,
        // the room it is in: a chunk's arrays double from a floor as a body is
        // written, so the room is between what the body took and twice it, and
        // beside every byte of code is four bytes saying where it came from.
        // A reader with both can see what a module holds of a function against
        // what it wrote. See D750.
        fprintf(out,
                ",\"bytes\":%u,\"room\":%u,\"constants\":%u"
                ",\"parameterSlots\":%u,\"slots\":%u,\"deep\":%u"
                ",\"folded\":%u,\"foldedSlots\":%u"
                ",\"noAlloc\":%s,\"noHost\":%s,\"deterministic\":%s"
                ",\"body\":\"%016llx\",\"why\":",
                chunk->code_count, chunk->code_capacity,
                chunk->constant_count, chunk->param_slots, chunk->slot_count, chunk->stack_needed,
                chunk->folded, chunk->folded_slots,
                chunk->no_alloc ? "true" : "false",
                chunk->no_host ? "true" : "false",
                chunk->deterministic ? "true" : "false",
                (unsigned long long)body_mark(chunk));
        if (reasons != NULL && reasons[i].reach != 0) {
            kest_json_text(kest_reach_name((KestReach)reasons[i].reach), out);
            fputs(",\"where\":", out);
            kest_json_text(module->functions[reasons[i].from]->name, out);
            fputs(",\"least\":null", out);
        } else if (reasons != NULL) {
            fprintf(out, "null,\"where\":null,\"least\":{\"slots\":%u,"
                         "\"frames\":%u}",
                    reasons[i].slots, reasons[i].frames);
        } else {
            fputs("null,\"where\":null,\"least\":null", out);
        }
        fputs(",\"code\":[", out);
        uint32_t offset = 0;
        bool first = true;
        while (offset < chunk->code_count) {
            uint8_t op = chunk->code[offset];
            fputs(first ? "" : ",", out);
            first = false;
            fputs("{\"at\":", out);
            fprintf(out, "%u,\"op\":", offset);
            kest_json_text(INSTRUCTIONS[op].name, out);
            fputs(",\"operands\":[", out);
            // How many numbers follow is the width and nothing else, so this
            // cannot come apart from what a walk of the code steps by.
            uint32_t count = (kest_op_width(op) - 1) / 2;
            for (uint32_t k = 0; k < count; k++) {
                fprintf(out, "%s%u", k == 0 ? "" : ",",
                        read_u16(chunk, offset + 1 + k * 2));
            }
            fputs("]}", out);
            offset += kest_op_width(op);
        }
        fputs("]}", out);
    }
    fputc(']', out);
}

void kest_module_disassemble(const KestModule *module,
                             const char *const *entries, FILE *out) {
    // A file of nothing but generic functions has no bodies: a copy exists
    // where one is called, and nothing here called any. A file that declares
    // nothing has none either, and the sentence has to be true of both. It is
    // said whatever else there is to show — a file may lay a shape out and
    // still have nothing with a body in it, which is what `std.sort` became
    // the day its two functions took types.
    if (module->count == 0) {
        fputs("nothing to run: nothing here has a body, and a function that "
              "takes types only gets one where it is called\n",
              out);
    }
    for (uint32_t i = 0; i < module->layout_count; i++) {
        const KestLayout *layout = &module->layouts[i];
        fprintf(out, "layout %u  %u byte%s aligned %u%s:", i, layout->size,
                layout->size == 1 ? "" : "s", layout->align,
                layout->tagged ? ", tagged" : "");
        for (uint16_t p = 0; p < layout->count; p++) {
            fprintf(out, " +%u %s", layout->pieces[p].offset,
                    SCALARS[layout->pieces[p].kind]);
            // The name where there is one, so the words say what the object
            // says: a reader of a listing and a tool reading the JSON beside
            // it are reading one thing.
            if (layout->pieces[p].name != NULL) {
                fprintf(out, " %s", layout->pieces[p].name);
            }
        }
        fputc('\n', out);
    }
    for (uint32_t i = 0; i < module->extern_count; i++) {
        fprintf(out, "host %s\n", module->externs[i].name);
    }

    // What a host has to give the machine before any of this runs. It is the
    // worst of every function, because a host may call anything the program
    // defines, and it is the one number here that is not about the code below
    // it.
    uint32_t stack = 0;
    uint32_t deep = 0;
    KestReason why = {KEST_REACH_UNASKED, NULL};
    // The same byte a function the JSON writer keeps, for the same reason.
    KestNoLeast *reasons =
        module->count == 0
            ? NULL
            : KEST_ARENA_ARRAY(module->arena, KestNoLeast, module->count);
    if (reasons != NULL) {
        memset(reasons, 0, sizeof(KestNoLeast) * module->count);
    }
    if (kest_module_needs(module, module->arena, -1, &stack, &deep, NULL, NULL,
                          reasons, &why)) {
        fprintf(out, "needs %u slot%s and %u frame%s\n", stack,
                stack == 1 ? "" : "s", deep, deep == 1 ? "" : "s");
        // And what an entry point costs on its own, when it is less. A host
        // that calls one of these and nothing else can ask for that instead,
        // and the difference is what the rest of the program costs it. Which
        // names those are is the caller's: this is a library and the names a
        // command line calls are not its business.
        for (uint32_t e = 0; entries != NULL && entries[e] != NULL; e++) {
            int32_t at = kest_module_entry(module, entries[e]);
            uint32_t alone_slots = 0;
            uint32_t alone_deep = 0;
            KestReason alone = {KEST_REACH_UNASKED, NULL};
            if (at >= 0 &&
                kest_module_needs(module, module->arena, at, &alone_slots,
                                  &alone_deep, NULL, NULL, NULL, &alone) &&
                (alone_slots != stack || alone_deep != deep)) {
                fprintf(out, "     %u and %u for `%s` on its own\n",
                        alone_slots, alone_deep, entries[e]);
            }
        }
    } else if (why.reach == KEST_REACH_ITSELF ||
               why.reach == KEST_REACH_VALUE) {
        // The two a host answers by picking a number. The others are not about
        // the program in front of the reader — a build that did not compile
        // has said so already — so nothing is printed for them here.
        fprintf(out, "needs a number a host picks: `%s` %s\n",
                why.where == NULL ? "something here" : why.where,
                kest_reach_name(why.reach));
    }

    for (uint32_t i = 0; i < module->count; i++) {
        const KestChunk *chunk = module->functions[i];
        // What it carries, and not what its declaration says: the machine
        // reads this and nothing else when it checks the one call the second
        // proof cannot see through, and until now nothing anywhere could see
        // it. A generic instance carries what the generic promised, which is
        // a thing worth being able to look at rather than to trust.
        // And which function the walk stopped at, said where a reader is
        // looking when they ask why there is no number. The line above says
        // the program has none and names the function; this is that function,
        // and a reader who came here from the disassembly rather than from the
        // top of it would otherwise have to go back. See D600.
        const char *stopped_at = "";
        const char *came_from = "";
        if (reasons != NULL && reasons[i].reach != 0) {
            stopped_at = kest_reach_name((KestReach)reasons[i].reach);
            // Where it came from, when that is somebody else: the function
            // that is the reason says so by being it.
            if (reasons[i].from != i && reasons[i].from < module->count) {
                came_from = module->functions[reasons[i].from]->name;
            }
        }
        fprintf(out,
                "fn %s  %u parameter slot%s, %u slot%s, %u deep%s%s%s%s%s\n",
                chunk->name, chunk->param_slots,
                chunk->param_slots == 1 ? "" : "s", chunk->slot_count,
                chunk->slot_count == 1 ? "" : "s", chunk->stack_needed,
                chunk->no_alloc ? ", promises `no.alloc`" : "",
                chunk->no_host ? ", promises `no.host`" : "",
                chunk->deterministic ? ", promises `deterministic`" : "",
                stopped_at[0] == '\0' ? "" : ", ", stopped_at);
        if (came_from[0] != '\0') {
            fprintf(out, "     in %s\n", came_from);
        }
        // Where it was declared, for the chunks a reader cannot place: one
        // written name that is two chunks is either a generic compiled twice
        // or two functions of a name, and which it is is whether they were
        // written in one place. Said only there, because for a name that is
        // one chunk the listing is already about the one declaration there is.
        // Under the header and indented, like everything else said about the
        // function above it: a line beginning `fn` is a function, and a count
        // of them is a thing this tree takes. See D612.
        bool shared = false;
        for (uint32_t other = 0; other < module->count && !shared; other++) {
            shared = other != i &&
                     strcmp(module->functions[other]->wrote, chunk->wrote) == 0;
        }
        if (shared && chunk->source != NULL) {
            uint32_t line = 0;
            uint32_t column = 0;
            kest_source_locate(chunk->source, chunk->declared.offset, &line,
                               &column);
            fprintf(out, "     declared at %s:%u:%u\n", chunk->source->path,
                    line, column);
        }
        // And what a machine to call this one takes, which is not the two
        // numbers above it: those are this function's own frame, and this is
        // everything it reaches.
        if (reasons != NULL && reasons[i].reach == 0) {
            fprintf(out, "     %u slot%s and %u frame%s to call it\n",
                    reasons[i].slots, reasons[i].slots == 1 ? "" : "s",
                    reasons[i].frames, reasons[i].frames == 1 ? "" : "s");
        }
        uint32_t offset = 0;
        while (offset < chunk->code_count) {
            offset = disassemble_one(module, chunk, offset, out);
        }
    }
}
