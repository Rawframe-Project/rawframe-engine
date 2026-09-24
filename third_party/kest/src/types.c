#include "types.h"

// How many a `[T; N]` holds. Written once: the test and the sentence that says
// the number were two literals, and nothing but a reader held them together.
#define MAX_ELEMENTS UINT16_MAX

#include <stdlib.h>
#include <string.h>

static void *grow(KestArena *arena, void *items, uint32_t count,
                  uint32_t *capacity, size_t size) {
    uint32_t grown = *capacity == 0 ? 8 : *capacity * 2;
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

double kest_left_over(double left, double right) {
    // A nought to divide by, either side not a number, or an infinity being
    // divided: C answers all of those with what is not a number, and this is
    // where a float differs from a whole number, which stops instead.
    // `volatile`, because the division is the point: a compiler that folds it
    // says "potential divide by 0" about the one place here that means to, and
    // a warning about what was written on purpose is a warning nobody reads.
    // There is no `NAN` to reach for -- the library is libc and this file does
    // not include `<math.h>` for one constant. See D970.
    volatile double nothing = 0.0;
    if (left != left || right != right || right == 0.0 ||
        left - left != 0.0) {
        return nothing / nothing;
    }
    double divisor = right < 0.0 ? -right : right;
    // An infinity to divide by leaves the whole of what was divided.
    if (divisor - divisor != 0.0) {
        return left;
    }
    double rest = left < 0.0 ? -left : left;
    if (rest < divisor) {
        return left;
    }
    // Double the divisor until one more would pass what is left, then take it
    // away and halve back down. Every step is exact in binary: doubling and
    // halving only move the exponent, and every subtraction here is of two
    // numbers close enough together that nothing is rounded off. Which is why
    // this is written out rather than asking C for `fmod`: the engine is held
    // to libc and nothing beyond it, and this is arithmetic. See D776.
    double scaled = divisor;
    while (scaled <= rest * 0.5) {
        scaled += scaled;
    }
    while (scaled >= divisor) {
        if (rest >= scaled) {
            rest -= scaled;
        }
        scaled *= 0.5;
    }
    // What is left over carries the sign of what was divided, the way a whole
    // number already does.
    return left < 0.0 ? -rest : rest;
}

// What the checker still holds when a build is done, beside what the module
// does. The types it made, the names it registered and the table it finds them
// in, the composed types it keeps so that two of one are one, and the copies of
// generic shapes. Everything left over after these and the module's four is
// text: names kept out of spans, and the source they were cut from. See D784.
void kest_program_holds(const KestProgram *program, uint32_t *types,
                        uint32_t *globals, uint32_t *found_by,
                        uint32_t *composed) {
    *types = (uint32_t)(program->types_made * sizeof(KestType));
    *globals = (uint32_t)(program->global_capacity * sizeof(KestSymbol));
    *found_by = (uint32_t)(program->by_name_slots * sizeof(uint32_t));
    *composed = (uint32_t)(program->composed_capacity * sizeof(KestType *));
}

static const char *span_string(KestProgram *program, KestSpan span) {
    const char *kept = kest_arena_strndup(
        program->arena, kest_span_text(program->source, span), span.length);
    if (kept == NULL) {
        // Every reader of this keeps what comes back as a name and measures it
        // later, so nothing is what none of them can be given. An empty name
        // is a name no file can write, and saying the arena is empty stops the
        // build before one is compared with anything. See D510.
        kest_diags_starve(program->diags);
        return "";
    }
    return kept;
}

// The name a declaration lives under: `world.Npc` for a struct `Npc` in module
// `game.world`, and `Npc` in a file that declares no module.
static const char *qualified(KestProgram *program, KestSpan span) {
    if (program->module[0] == '\0') {
        return span_string(program, span);
    }
    size_t room = strlen(program->module) + span.length + 2;
    char *name = kest_arena_alloc(program->arena, room, 1);
    if (name == NULL) {
        return NULL;
    }
    snprintf(name, room, "%s.%.*s", program->module, (int)span.length,
             kest_span_text(program->source, span));
    return name;
}

void kest_program_in(KestProgram *program, const KestUnitInfo *unit) {
    program->unit = unit;
    program->source = &unit->source;
    program->alias = unit->alias;
    program->module = unit->module;
}

// Tries the current file's own module first, then the name as written, which
// is already qualified when it names something imported.
// A name under this file's own module: `player.hurt` for `hurt`. Asked of
// every name in every file, so the room for it is the stack until a name is
// longer than that — which used to mean the name was not looked up at all, and
// a struct with a long name was unknown in the file that declared it.
static const char *under_alias(KestProgram *program, const char *name,
                               size_t length, char *stack, size_t room) {
    size_t needed = strlen(program->module) + length + 2;
    char *out = stack;
    if (needed > room) {
        out = kest_arena_alloc(program->arena, needed, 1);
        room = needed;
        if (out == NULL) {
            return NULL;
        }
    }
    snprintf(out, room, "%s.%.*s", program->module, (int)length, name);
    return out;
}

// A name written with somebody else's module in front of it, under the whole
// of what that module calls itself: `math.twice` in a file that imported
// `a.math` is `a.math.twice`. NULL where the first part is not an alias this
// file may write, which is every name that is not qualified at all.
//
// This is where two modules under one last part stop being one module. What a
// file writes is the last part, and which module that means is the file's own
// question -- so `render.math` and `physics.math` are two names here and one
// name to each of the files that import them. See D1039.
static const char *under_import(KestProgram *program, const char *name,
                                size_t length, char *stack, size_t room) {
    if (program->unit == NULL) {
        return NULL;
    }
    size_t head = 0;
    while (head < length && name[head] != '.') {
        head++;
    }
    if (head == length) {
        return NULL;
    }
    const char *whole = NULL;
    if (kest_word_same(program->alias, name, head)) {
        whole = program->module;
    }
    for (uint32_t i = 0; whole == NULL && i < program->unit->import_count;
         i++) {
        if (kest_word_same(program->unit->imports[i], name, head)) {
            whole = program->unit->import_paths[i];
        }
    }
    if (whole == NULL || whole[0] == '\0') {
        return NULL;
    }
    size_t rest = length - head - 1;
    size_t needed = strlen(whole) + rest + 2;
    char *out = stack;
    if (needed > room) {
        out = kest_arena_alloc(program->arena, needed, 1);
        room = needed;
        if (out == NULL) {
            return NULL;
        }
    }
    snprintf(out, room, "%s.%.*s", whole, (int)rest, name + head + 1);
    return out;
}

KestType *kest_lookup_type(KestProgram *program, const char *name,
                           size_t length) {
    // Looking one up is naming it. Registering one does not come through
    // here, so what this marks is a name somebody wrote.
    if (program->module[0] != '\0') {
        char stack[256];
        const char *joined =
            under_alias(program, name, length, stack, sizeof(stack));
        if (joined != NULL) {
            KestType *type = kest_find_type(program, joined, strlen(joined));
            if (type != NULL) {
                type->named = true;
                return type;
            }
        }
    }
    {
        char stack[256];
        const char *whole =
            under_import(program, name, length, stack, sizeof(stack));
        if (whole != NULL) {
            KestType *type = kest_find_type(program, whole, strlen(whole));
            if (type != NULL) {
                type->named = true;
                return type;
            }
        }
    }
    KestType *found = kest_find_type(program, name, length);
    if (found != NULL) {
        found->named = true;
    }
    return found;
}

KestSymbol *kest_lookup_global(KestProgram *program, const char *name,
                               size_t length) {
    if (program->module[0] != '\0') {
        char stack[256];
        const char *joined =
            under_alias(program, name, length, stack, sizeof(stack));
        if (joined != NULL) {
            KestSymbol *symbol =
                kest_find_global(program, joined, strlen(joined));
            if (symbol != NULL) {
                return symbol;
            }
        }
    }
    {
        char stack[256];
        const char *whole =
            under_import(program, name, length, stack, sizeof(stack));
        if (whole != NULL) {
            KestSymbol *symbol =
                kest_find_global(program, whole, strlen(whole));
            if (symbol != NULL) {
                return symbol;
            }
        }
    }
    return kest_find_global(program, name, length);
}

static KestType *new_type(KestProgram *program, KestTypeTag tag) {
    KestType *type = KEST_ARENA_NEW(program->arena, KestType);
    if (type != NULL) {
        type->tag = tag;
        program->types_made++;
    }
    return type;
}

static uint32_t name_hash(const char *name, size_t length);

// Where a type's name says it goes. Nothing is ever taken out, so a run of
// full slots is a run of names that landed on the same one and it ends at the
// first empty slot -- and what was put there first is what a lookup answers
// with, which is what the walk this replaced did. See D1086.
static void type_index_put(KestProgram *program, uint32_t at) {
    const char *name = program->types[at]->name;
    if (name == NULL || program->types_by_name_slots == 0) {
        return;
    }
    uint32_t mask = program->types_by_name_slots - 1;
    uint32_t slot = name_hash(name, strlen(name)) & mask;
    while (program->types_by_name[slot] != 0) {
        slot = (slot + 1u) & mask;
    }
    program->types_by_name[slot] = at + 1u;
}

// Room for one more, which is a table twice as big when it is half full.
static bool type_index_room(KestProgram *program) {
    if (program->types_by_name_slots >= (program->type_count + 1) * 2) {
        return true;
    }
    uint32_t slots = program->types_by_name_slots == 0
                         ? 64
                         : program->types_by_name_slots * 2;
    uint32_t *made = KEST_ARENA_ARRAY(program->arena, uint32_t, slots);
    if (made == NULL) {
        return false;
    }
    program->types_by_name = made;
    program->types_by_name_slots = slots;
    for (uint32_t i = 0; i < program->type_count; i++) {
        type_index_put(program, i);
    }
    return true;
}

// The name is given here rather than written on afterwards, because a type in
// the list under no name is a type the index cannot find. See D1086.
static bool register_type(KestProgram *program, KestType *type,
                          const char *name) {
    if (program->type_count == program->type_capacity) {
        void *moved = grow(program->arena, program->types, program->type_count,
                           &program->type_capacity, sizeof(KestType *));
        if (moved == NULL) {
            return false;
        }
        program->types = moved;
    }
    type->name = name;
    program->types[program->type_count++] = type;
    if (!type_index_room(program)) {
        return false;
    }
    type_index_put(program, program->type_count - 1);
    return true;
}

void kest_import_reached_by(KestProgram *program, const char *alias,
                            size_t length) {
    if (program->unit == NULL || program->unit->import_reached == NULL) {
        return;
    }
    // The same walk `kest_needs_import` makes, at the other end of the same
    // question: that one asks whether a name is out of reach and this marks
    // which import put it in reach. Kept apart because the suggestion machine
    // asks the first of every name it offers, and a name offered is not a name
    // written. See D725.
    for (uint32_t i = 0; i < program->unit->import_count; i++) {
        const char *imported = program->unit->imports[i];
        if (kest_word_same(imported, alias, length)) {
            program->unit->import_reached[i] = true;
            return;
        }
    }
}

void kest_import_reached(KestProgram *program, const char *name,
                         size_t length) {
    const char *dot = memchr(name, '.', length);
    if (dot == NULL) {
        return;
    }
    kest_import_reached_by(program, name, (size_t)(dot - name));
}

bool kest_file_reaches(KestProgram *program, const char *alias,
                       size_t length) {
    if (program->unit == NULL) {
        return false;
    }
    // Its own module first. A file writing the name it calls itself in front
    // of one of its own names has written nothing that needs bringing into
    // reach, and is the one shape where asking only about imports answers no
    // to a file that may write it. See D736.
    if (kest_word_same(program->alias, alias, length)) {
        return true;
    }
    for (uint32_t i = 0; i < program->unit->import_count; i++) {
        const char *imported = program->unit->imports[i];
        if (kest_word_same(imported, alias, length)) {
            return true;
        }
    }
    return false;
}

// A module is not a thing in the program: it is what the names under it have
// in common. So a name is a module this file can reach when something is
// declared under it and the file imported it.
// Which module a file means by the word it writes in front of a name: `io` is
// `std.io` in a file that imported it, and its own module where the word is
// the file's own. NULL where the word is not one this file may write, which is
// what makes a name out of reach out of reach. See D1039.
const char *kest_module_for(KestProgram *program, const char *alias,
                            size_t length) {
    if (program->module[0] != '\0' &&
        kest_word_same(program->alias, alias, length)) {
        return program->module;
    }
    if (program->unit == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < program->unit->import_count; i++) {
        if (kest_word_same(program->unit->imports[i], alias, length)) {
            return program->unit->import_paths[i];
        }
    }
    return NULL;
}

// Whether a registered name lives under a module a file wrote the word for.
// The word is expanded first, because a name lives under the whole of its
// module and what a file writes is the last part of it. See D1039.
static bool under_module_of(KestProgram *program, const char *whole,
                                 const char *name, size_t length) {
    const char *module = kest_module_for(program, name, length);
    if (module == NULL || module[0] == '\0') {
        return kest_under_module(whole, name, length);
    }
    return kest_under_module(whole, module, strlen(module));
}

bool kest_under_module(const char *whole, const char *name, size_t length) {
    return strlen(whole) > length + 1 && whole[length] == '.' &&
           memcmp(whole, name, length) == 0;
}

// Where a module was read from, which is the file the first thing under it was
// declared in. A program built against one library and read with another gets
// a message about a name that is not there and no word about which `io` it
// looked in; the file is the answer to that.
const KestSymbol *kest_first_under(KestProgram *program, const char *name,
                                   size_t length) {
    const KestSymbol *any = NULL;
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        if (!under_module_of(program, symbol->name, name, length) ||
            kest_needs_import(program, symbol->name,
                              strlen(symbol->name))) {
            continue;
        }
        // A name under the module and nothing further: `io.print` rather than
        // `io.Io.write`, which is a crossing the module declares and not the
        // name anybody reaches for first. One with another dot in it is kept
        // in case there is no plainer one, because pointing at the file is
        // the whole of what the other caller wants. See D738.
        if (strchr(symbol->name + length + 1, '.') == NULL) {
            return symbol;
        }
        if (any == NULL) {
            any = symbol;
        }
    }
    return any;
}

bool kest_module_named(KestProgram *program, const char *name,
                       size_t length) {
    for (uint32_t i = 0; i < program->global_count; i++) {
        const char *whole = program->globals[i].name;
        if (under_module_of(program, whole, name, length) &&
            !kest_needs_import(program, whole, strlen(whole))) {
            return true;
        }
    }
    // A module may declare nothing but types, and a type is not a global.
    for (uint32_t i = 0; i < program->type_count; i++) {
        const char *whole = program->types[i]->name;
        if (whole != NULL && under_module_of(program, whole, name, length) &&
            !kest_needs_import(program, whole, strlen(whole))) {
            return true;
        }
    }
    return false;
}

bool kest_needs_import(KestProgram *program, const char *name, size_t length) {
    const char *dot = memchr(name, '.', length);
    if (dot == NULL || program->unit == NULL) {
        return false;
    }

    // The question is where the name was declared, not how it is spelled: a
    // host receiver has a dot in it and crosses nothing.
    const KestSource *declared_in = NULL;
    KestType *type = kest_lookup_type(program, name, length);
    if (type != NULL) {
        declared_in = type->declared_in;
    } else {
        KestSymbol *symbol = kest_lookup_global(program, name, length);
        if (symbol != NULL) {
            declared_in = symbol->source;
        }
    }
    if (declared_in == NULL || declared_in == program->source) {
        return false;
    }

    // In reach where the module it lives under is this file's own or one this
    // file imported. Asked of the whole module rather than of the word before
    // the first dot, because a name lives under the whole of its module and
    // `std.io.print` begins with `std`, which is nobody's module. A name a
    // file wrote itself -- `io.print` -- is matched by its last part, which is
    // what `kest_file_reaches` answers. See D1039.
    for (uint32_t i = 0; program->unit != NULL &&
                         i <= program->unit->import_count;
         i++) {
        const char *module = i == 0 ? program->module
                                    : program->unit->import_paths[i - 1];
        if (module == NULL || module[0] == '\0') {
            continue;
        }
        size_t reach = strlen(module);
        if (length > reach + 1 && name[reach] == '.' &&
            memcmp(name, module, reach) == 0) {
            return false;
        }
    }
    return !kest_file_reaches(program, name, (size_t)(dot - name));
}

// Whether this program holds a name written this way under a module this file
// has not imported: `text.chars` where `std.text.chars` is registered and
// nothing here asked for `std.text`. What a file writes is a module's last
// part, so that is what is matched, and the rest of the written name after it.
//
// It is the question `kest_needs_import` cannot answer once the lookups have
// failed: they expand through this file's imports and a module this file did
// not import is exactly the one they cannot see. Without it a reader writing
// `text.chars` with no import was told `text` is a type. See D1039.
// The files of this program in a table, by the alias their names are written
// through. Built once, on the first question that needs it. See D1087.
static bool alias_index(KestProgram *program) {
    if (program->files_by_alias_slots > 0 || program->files == NULL ||
        program->files->count == 0) {
        return program->files_by_alias_slots > 0;
    }
    uint32_t slots = 64;
    while (slots < (program->files->count + 1) * 2) {
        slots *= 2;
    }
    uint32_t *made = KEST_ARENA_ARRAY(program->arena, uint32_t, slots);
    if (made == NULL) {
        return false;
    }
    program->files_by_alias = made;
    program->files_by_alias_slots = slots;
    uint32_t mask = slots - 1;
    for (uint32_t i = 0; i < program->files->count; i++) {
        const char *alias = program->files->items[i].alias;
        if (alias == NULL || alias[0] == '\0') {
            continue;
        }
        uint32_t slot = name_hash(alias, strlen(alias)) & mask;
        while (program->files_by_alias[slot] != 0) {
            slot = (slot + 1u) & mask;
        }
        program->files_by_alias[slot] = i + 1u;
    }
    return true;
}

bool kest_out_of_reach(KestProgram *program, const char *name, size_t length) {
    const char *dot = memchr(name, '.', length);
    if (dot == NULL || program->files == NULL) {
        return false;
    }
    size_t head = (size_t)(dot - name);
    size_t rest = length - head - 1;
    // Which files could answer this: the ones whose alias is the head of the
    // name. Through the table where there is one, and every file where there
    // is not -- two files may share an alias, so what answers is a run of
    // slots rather than one. See D1087.
    bool tabled = alias_index(program);
    uint32_t mask = tabled ? program->files_by_alias_slots - 1 : 0;
    uint32_t slot = tabled ? (name_hash(name, head) & mask) : 0;
    uint32_t next = 0;
    while (true) {
        uint32_t which;
        if (tabled) {
            if (program->files_by_alias[slot] == 0) {
                return false;
            }
            which = program->files_by_alias[slot] - 1u;
            slot = (slot + 1u) & mask;
        } else {
            if (next >= program->files->count) {
                return false;
            }
            which = next++;
        }
        const char *module = program->files->items[which].module;
        const char *alias = program->files->items[which].alias;
        if (module == NULL || module[0] == '\0' ||
            !kest_word_same(alias, name, head)) {
            continue;
        }
        size_t reach = strlen(module);
        size_t room = reach + rest + 2;
        char *whole = kest_arena_alloc(program->arena, room, 1);
        if (whole == NULL) {
            return false;
        }
        snprintf(whole, room, "%s.%.*s", module, (int)rest, dot + 1);
        if ((kest_find_global(program, whole, reach + rest + 1) != NULL ||
             kest_find_type(program, whole, reach + rest + 1) != NULL) &&
            kest_needs_import(program, whole, reach + rest + 1)) {
            return true;
        }
    }
}

KestType *kest_find_type(KestProgram *program, const char *name,
                         size_t length) {
    // A composed type has no name of its own and is not in here; `kest_type_name`
    // builds one on demand and nothing looks it up by that.
    if (program->types_by_name_slots == 0) {
        return NULL;
    }
    uint32_t mask = program->types_by_name_slots - 1;
    uint32_t slot = name_hash(name, length) & mask;
    while (program->types_by_name[slot] != 0) {
        KestType *one = program->types[program->types_by_name[slot] - 1u];
        if (kest_word_same(one->name, name, length)) {
            return one;
        }
        slot = (slot + 1u) & mask;
    }
    return NULL;
}

static bool add_primitive(KestProgram *program, const char *name,
                          KestTypeTag tag, uint8_t width, bool is_signed) {
    KestType *type = new_type(program, tag);
    if (type == NULL) {
        return false;
    }
    // A piece of text is two: what it is made of and how many bytes that is.
    // Its length is part of it rather than something to go and count, which is
    // what makes `len` a read and a cut free. See D964.
    type->slots = tag == KEST_T_VOID ? 0 : (tag == KEST_T_TEXT ? 2 : 1);
    // A handle is a machine word. A piece of text is two of them where it is
    // laid out in memory, for the same reason it is two slots: what it is made
    // of, and how many bytes that is. A number is what it says it is.
    type->byte_size = tag == KEST_T_VOID   ? 0
                      : tag == KEST_T_TEXT ? 16
                      : width == 0 || width == 1
                          ? (tag == KEST_T_BOOL ? 1 : 8)
                          : (uint16_t)(width / 8);
    type->byte_align = type->byte_size == 0 ? 1
                       : tag == KEST_T_TEXT ? 8
                                            : type->byte_size;
    type->width = width;
    type->is_signed = is_signed;
    return register_type(program, type, name);
}

static bool add_primitives(KestProgram *program) {
    return add_primitive(program, "void", KEST_T_VOID, 0, false) &&
           add_primitive(program, "bool", KEST_T_BOOL, 1, false) &&
           add_primitive(program, "text", KEST_T_TEXT, 0, false) &&
           add_primitive(program, "i8", KEST_T_INT, 8, true) &&
           add_primitive(program, "i16", KEST_T_INT, 16, true) &&
           add_primitive(program, "i32", KEST_T_INT, 32, true) &&
           add_primitive(program, "i64", KEST_T_INT, 64, true) &&
           add_primitive(program, "u8", KEST_T_INT, 8, false) &&
           add_primitive(program, "u16", KEST_T_INT, 16, false) &&
           add_primitive(program, "u32", KEST_T_INT, 32, false) &&
           add_primitive(program, "u64", KEST_T_INT, 64, false) &&
           add_primitive(program, "f32", KEST_T_FLOAT, 32, false) &&
           add_primitive(program, "f64", KEST_T_FLOAT, 64, false);
}

// Levenshtein distance, capped: anything past `limit` is not a suggestion
// worth making, so the walk stops rather than finishing the matrix.
// What a constant of this name is written as. The symbol table first, because
// that is where a name from another file is; then the file being read, because
// a type is resolved before this file's constants are declared and a count may
// name one.
static const KestDecl *constant_in_file(KestProgram *program, const char *name,
                                        uint32_t length) {
    const KestUnit *unit = program->unit == NULL ? NULL : &program->unit->unit;
    for (uint32_t i = 0; unit != NULL && i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind == KEST_DECL_CONST &&
            decl->name.length == length &&
            memcmp(kest_span_text(program->source, decl->name), name,
                   length) == 0) {
            return decl;
        }
    }
    return NULL;
}

// The same, in the file another module is. A count may name a constant from one
// — `[i32; box.CELLS]` — and the name is read before there is a symbol for it,
// so the file is found by what it calls itself and its declarations are read
// there. The source is that file's, because a declaration's spans are into it.
// See D681.
static const KestDecl *constant_in_module(KestProgram *program,
                                          const char *module,
                                          uint32_t module_length,
                                          const char *name, uint32_t length,
                                          const KestSource **read_in) {
    const KestUnits *files = program->files;
    for (uint32_t f = 0; files != NULL && f < files->count; f++) {
        const KestUnitInfo *info = &files->items[f];
        if (info->alias == NULL ||
            !kest_word_same(info->alias, module, module_length)) {
            continue;
        }
        for (uint32_t i = 0; i < info->unit.count; i++) {
            const KestDecl *decl = info->unit.items[i];
            if (decl->kind == KEST_DECL_CONST &&
                decl->name.length == length &&
                memcmp(kest_span_text(&info->source, decl->name), name,
                       length) == 0) {
                *read_in = &info->source;
                return decl;
            }
        }
    }
    return NULL;
}

// Counting how many of something there are is reading the constant that says
// how many, and it happens before the constants are a list of symbols.
static void remember_count(KestProgram *program, const char *name,
                           uint32_t length) {
    char *kept = kest_arena_strndup(program->arena, name, length);
    if (kept == NULL) {
        return;
    }
    if (program->counted_count == program->counted_capacity) {
        void *moved = grow(program->arena, program->counted,
                           program->counted_count, &program->counted_capacity,
                           sizeof(const char *));
        if (moved == NULL) {
            return;
        }
        program->counted = moved;
    }
    program->counted[program->counted_count++] = kept;
}

// Said here because the fold of one value asks for the fold of a whole one: a
// hash is over a value laid out flat, and the flat one is worked out below.
static uint32_t fold_slots(KestProgram *program, const KestExpr *expr,
                           KestValue *out, uint32_t room, uint32_t depth,
                           const char **why);

static const KestExpr *constant_written(KestProgram *program, const char *name,
                                        uint32_t length) {
    KestSymbol *symbol = kest_lookup_global(program, name, length);
    if (symbol != NULL && symbol->is_const) {
        // Counting how many of something there are is reading it, the same as
        // adding it to a number is: `[i32; CELLS]` names `CELLS`.
        symbol->named = true;
        return symbol->value;
    }
    const KestDecl *decl = constant_in_file(program, name, length);
    return decl == NULL ? NULL : decl->constant.value;
}

// `f32` rounds where `f64` does not, which is part of what the type means.
bool kest_is_narrow(const KestType *type) {
    return type != NULL && type->tag == KEST_T_FLOAT && type->width == 32;
}

bool kest_is_unsigned(const KestType *type) {
    return type != NULL && type->tag == KEST_T_INT && !type->is_signed;
}

bool kest_is_float(const KestType *type) {
    return type != NULL && type->tag == KEST_T_FLOAT;
}

bool kest_is_a_run(const KestType *type) {
    return type != NULL &&
           (type->tag == KEST_T_ENUM || type->tag == KEST_T_STRUCT ||
            type->tag == KEST_T_FIXED);
}

// What a constant is, worked out where it is written rather than where it is
// used: one value, so it is the same value everywhere it appears and it costs
// one instruction to push. The checker has already said the expression makes
// sense and what its type is; this only has to do the arithmetic.
//
// False when it is not something that can be worked out here, and then the
// caller says so at the place that asked.
static bool fold(KestProgram *program, const KestExpr *expr, KestValue *out,
                 uint32_t depth, const char **why) {
    if (expr == NULL) {
        return false;
    }
    if (depth > 32) {
        *why = "a constant made out of itself has no value to work out";
        return false;
    }
    const KestType *type = expr->type;
    bool real = type != NULL && type->tag == KEST_T_FLOAT;
    bool unsigned_ = type != NULL && kest_is_unsigned(type);

    switch (expr->kind) {
    case KEST_EXPR_INT: {
        bool overflow = false;
        out->integer = (int64_t)kest_token_integer(
            kest_span_text(program->source, expr->span), expr->span.length,
            &overflow);
        return true;
    }
    case KEST_EXPR_BYTE: {
        KestSpan content = {expr->span.offset + 1, expr->span.length - 2};
        out->integer =
            (unsigned char)kest_literal_text(program->arena,
                                             program->source, content,
                                             NULL)[0];
        return true;
    }
    case KEST_EXPR_FLOAT:
        out->real = kest_literal_real(program->source, expr->span);
        if (kest_is_narrow(type)) {
            out->real = (float)out->real;
        }
        return true;
    case KEST_EXPR_STRING: {
        KestSpan content = {expr->span.offset + 1, expr->span.length - 2};
        out->text = kest_literal_text(program->arena, program->source, content,
                                      NULL);
        return true;
    }
    case KEST_EXPR_BOOL:
        out->integer = expr->boolean;
        return true;
    case KEST_EXPR_TEXT:
        // Filling a hole is what the machine does, and a constant is worked
        // out before there is one.
        *why = "a constant is written without holes in it";
        program->fold_never = true;
        return false;
    case KEST_EXPR_NAME: {
        // A name the body declared is the body's before it is a constant's,
        // and worth something only where the body holds its value. See D1231.
        if (program->held_name != NULL) {
            const KestValue *value = NULL;
            uint32_t slots = 0;
            if (program->held_name(program->held_context,
                                   kest_span_text(program->source, expr->span),
                                   expr->span.length, &value, &slots)) {
                if (value != NULL && slots == 1) {
                    *out = value[0];
                    return true;
                }
                *why = "a name the body keeps is not worked out where it is "
                       "written";
                return false;
            }
        }
        // A constant made of itself has no value to work out, which the depth
        // catches; this is only for a name that is not a constant at all.
        const KestExpr *written = constant_written(
            program, kest_span_text(program->source, expr->span),
            expr->span.length);
        return written != NULL && fold(program, written, out, depth + 1, why);
    }
    // `i32(x)` inside a constant: a conversion is a call whose callee is a
    // type, and what it does to a number is what the machine does to one with
    // an instruction. Refused where it was written until now, which made a
    // narrowing a thing a program may write everywhere except where it is
    // worked out — and `const N: i32 = 130` was taken and wrapped, which is the
    // same narrowing with nobody asking for it. See D669.
    case KEST_EXPR_CALL: {
        const KestType *to = expr->call.callee != NULL
                                 ? expr->call.callee->type
                                 : NULL;
        if (to == NULL && expr->call.callee != NULL &&
            expr->call.callee->kind == KEST_EXPR_NAME) {
            // A builtin whose answer cannot be anything else: how many are in
            // a run of them, which the type says, and the number standing for
            // a value, which this language promises does not move. Both are
            // worked out here rather than run, so a program may have a table
            // of them before it starts. See D670.
            const char *called =
                kest_span_text(program->source, expr->call.callee->span);
            uint32_t length = expr->call.callee->span.length;
            if (kest_word_same("len", called, length)) {
                const KestType *of = expr->call.arg_count == 1
                                         ? expr->call.args[0]->type
                                         : NULL;
                if (of == NULL || of->tag != KEST_T_FIXED) {
                    *why = "`len` is worked out where it is written for a run "
                           "of a size the type says, and asked while running "
                           "for one that grows";
                    program->fold_never = true;
                    return false;
                }
                out->integer = of->count;
                return true;
            }
            if (kest_word_same("hash", called, length) &&
                expr->call.arg_count == 1) {
                // Over the value laid out flat, which is what the machine
                // hashes and what this works out: one walk, asked of slots
                // either way. A case of an enum is its tag and what it
                // carries, so it is folded like anything else and hashed like
                // anything else. See D671.
                const KestType *what = expr->call.args[0]->type;
                uint32_t wide = what == NULL ? 0 : what->slots;
                if (wide == 0) {
                    *why = "a constant hashes a value, and this is not one";
                    return false;
                }
                KestValue *held =
                    KEST_ARENA_ARRAY(program->arena, KestValue, wide);
                if (held == NULL) {
                    return false;
                }
                if (fold_slots(program, expr->call.args[0], held, wide,
                               depth + 1, why) != wide) {
                    return false;
                }
                out->integer = (int64_t)kest_hash_value(what, held);
                return true;
            }
            // A float as its bits and the other way, which round nothing and
            // are the same bits on every machine, so a constant may be written
            // as the bits it is: a coefficient taken from somebody else's
            // table is those bits and not whatever a decimal of it parses to.
            // Each does what the machine's instruction does, a `u32` and an
            // `f32` through a `float` the way `bits.f32` goes. See D1235.
            if ((kest_word_same("float", called, length) ||
                 kest_word_same("bits", called, length)) &&
                expr->call.arg_count == 1) {
                const KestType *what = expr->call.args[0]->type;
                KestValue held = {0};
                if (what == NULL ||
                    !fold(program, expr->call.args[0], &held, depth + 1,
                          why)) {
                    return false;
                }
                bool narrow = what->slots == 1 &&
                              (kest_scalar_of(what) == KEST_L_U32 ||
                               kest_scalar_of(what) == KEST_L_F32);
                if (!narrow) {
                    *out = held;
                    return true;
                }
                if (kest_word_same("float", called, length)) {
                    uint32_t word = (uint32_t)held.integer;
                    float single;
                    memcpy(&single, &word, sizeof single);
                    out->real = single;
                } else {
                    float single = (float)held.real;
                    uint32_t word;
                    memcpy(&word, &single, sizeof word);
                    out->integer = word;
                }
                return true;
            }
        }
        if (to == NULL || expr->call.arg_count != 1 ||
            (to->tag != KEST_T_INT && to->tag != KEST_T_FLOAT)) {
            *why = "a constant is worked out before there is a machine, and a "
                   "call is where a program starts";
            program->fold_never = true;
            return false;
        }
        KestValue held = {0};
        if (!fold(program, expr->call.args[0], &held, depth + 1, why)) {
            return false;
        }
        const KestType *from = expr->call.args[0]->type;
        bool from_real = from != NULL && from->tag == KEST_T_FLOAT;
        if (to->tag == KEST_T_INT) {
            out->integer = from_real
                               ? kest_real_to_int(kest_scalar_of(to),
                                                  held.real)
                               : kest_narrow_to(kest_scalar_of(to),
                                                held.integer);
            return true;
        }
        double as_real = from_real ? held.real
                         : (from != NULL && from->tag == KEST_T_INT &&
                            !from->is_signed)
                             ? (double)(uint64_t)held.integer
                             : (double)held.integer;
        // A slot holds a double either way, so widening is nothing and
        // narrowing is a rounding.
        out->real = to->width == 32 ? (double)(float)as_real : as_real;
        return true;
    }
    // `box.CELLS` inside a constant: a constant another module declared, which
    // is one name with a dot in it. What it is written as is in that module's
    // file, so it is worked out against that file rather than against this
    // one — the spans are into it, and read here they name bytes nobody
    // wrote. See D665.
    case KEST_EXPR_FIELD: {
        KestSymbol *elsewhere = kest_lookup_global(
            program, kest_span_text(program->source, expr->span),
            expr->span.length);
        if (elsewhere == NULL || !elsewhere->is_const) {
            *why = "a constant is a name for a value, and this is a field of "
                   "something";
            program->fold_never = true;
            return false;
        }
        elsewhere->named = true;
        const KestSource *reading = program->source;
        if (elsewhere->source != NULL) {
            program->source = elsewhere->source;
        }
        bool worked = fold(program, elsewhere->value, out, depth + 1, why);
        program->source = reading;
        return worked;
    }
    case KEST_EXPR_UNARY: {
        KestValue held = {0};
        if (!fold(program, expr->unary.operand, &held, depth + 1, why)) {
            return false;
        }
        switch (expr->unary.op) {
        case KEST_TOK_MINUS:
            if (real) {
                out->real = -held.real;
            } else {
                // Unsigned, so that the smallest number negates to itself
                // the way this language says it does rather than being
                // undefined the way C says it is. See D667.
                out->integer = (int64_t)(0 - (uint64_t)held.integer);
            }
            return true;
        case KEST_TOK_BANG:
            out->integer = !held.integer;
            return true;
        case KEST_TOK_TILDE:
            out->integer = ~held.integer;
            return true;
        default:
            return false;
        }
    }
    case KEST_EXPR_BINARY: {
        KestValue left = {0};
        KestValue right = {0};
        
        if (!fold(program, expr->binary.left, &left, depth + 1, why) ||
            !fold(program, expr->binary.right, &right, depth + 1, why)) {
            return false;
        }
        // Which arithmetic this is comes from the type the checker settled on
        // for the whole thing, not from the pieces: a comparison of two
        // numbers gives a truth.
        const KestType *side = expr->binary.left->type;
        bool numbers = side != NULL && side->tag == KEST_T_FLOAT;
        if (numbers) {
            double a = left.real;
            double b = right.real;
            switch (expr->binary.op) {
            case KEST_TOK_PLUS:
                out->real = a + b;
                break;
            case KEST_TOK_MINUS:
                out->real = a - b;
                break;
            case KEST_TOK_STAR:
                out->real = a * b;
                break;
            // Nought on the right is where a float parts company with a
            // whole number: the machine answers an infinity with a sign, or
            // what is not a number for nought over nought, and does not stop.
            // A constant is worked out by a second arithmetic and has to be
            // the same one — a program that says a float divide has an answer
            // and then refuses the one written down says it in two voices.
            // The whole numbers below keep their refusal, because there the
            // machine does stop. See D805.
            case KEST_TOK_SLASH:
                out->real = a / b;
                break;
            case KEST_TOK_PERCENT:
                out->real = kest_left_over(a, b);
                break;
            case KEST_TOK_LT:
                out->integer = a < b;
                break;
            case KEST_TOK_LTEQ:
                out->integer = a <= b;
                break;
            case KEST_TOK_GT:
                out->integer = a > b;
                break;
            case KEST_TOK_GTEQ:
                out->integer = a >= b;
                break;
            case KEST_TOK_EQEQ:
                out->integer = a == b;
                break;
            case KEST_TOK_BANGEQ:
                out->integer = a != b;
                break;
            default:
                return false;
            }
            if (real && kest_is_narrow(type)) {
                out->real = (float)out->real;
            }
            return true;
        }
        if (side != NULL && side->tag == KEST_T_TEXT) {
            // Text compares and does not add: there is no `+` on text.
            int order = strcmp(left.text, right.text);
            switch (expr->binary.op) {
            case KEST_TOK_EQEQ:
                out->integer = order == 0;
                return true;
            case KEST_TOK_BANGEQ:
                out->integer = order != 0;
                return true;
            case KEST_TOK_LT:
                out->integer = order < 0;
                return true;
            case KEST_TOK_LTEQ:
                out->integer = order <= 0;
                return true;
            case KEST_TOK_GT:
                out->integer = order > 0;
                return true;
            case KEST_TOK_GTEQ:
                out->integer = order >= 0;
                return true;
            default:
                return false;
            }
        }
        int64_t a = left.integer;
        int64_t b = right.integer;
        switch (expr->binary.op) {
        case KEST_TOK_PLUS:
            out->integer = (int64_t)((uint64_t)a + (uint64_t)b);
            break;
        case KEST_TOK_MINUS:
            out->integer = (int64_t)((uint64_t)a - (uint64_t)b);
            break;
        case KEST_TOK_STAR:
            out->integer = (int64_t)((uint64_t)a * (uint64_t)b);
            break;
        case KEST_TOK_SLASH:
            if (b == 0) {
                *why = "this divides by nought";
                return false;
            }
            // The one pair whose quotient does not fit, which the reference
            // says wraps to itself with nought left over and which C leaves
            // undefined — a trap on the machine this compiles on, so a
            // constant written this way took the compiler down rather than
            // being worked out. The machine has held this since it had a
            // divide; this is the same guard on the other arithmetic. See
            // D806.
            if (!unsigned_ && a == INT64_MIN && b == -1) {
                out->integer = INT64_MIN;
                break;
            }
            out->integer = unsigned_ ? (int64_t)((uint64_t)a / (uint64_t)b)
                                     : a / b;
            break;
        case KEST_TOK_PERCENT:
            if (b == 0) {
                *why = "this divides by nought";
                return false;
            }
            if (!unsigned_ && a == INT64_MIN && b == -1) {
                out->integer = 0;
                break;
            }
            out->integer = unsigned_ ? (int64_t)((uint64_t)a % (uint64_t)b)
                                     : a % b;
            break;
        case KEST_TOK_AMP:
            out->integer = a & b;
            break;
        case KEST_TOK_PIPE:
            out->integer = a | b;
            break;
        case KEST_TOK_CARET:
            out->integer = a ^ b;
            break;
        // A count of nothing is not a count, which the machine says while
        // running and a constant has nothing to work out; a count past the
        // width has an answer, and it is the one the machine gives — nothing
        // is left of a number shifted further than it is wide, except the
        // sign a signed shift keeps shifting in. Refused for the first and
        // answered for the second, which is where these were one refusal.
        // See D806.
        case KEST_TOK_LTLT:
            if (b < 0) {
                *why = "a shift of a count below nought is not a shift";
                return false;
            }
            out->integer = b >= 64 ? 0 : (int64_t)((uint64_t)a << b);
            break;
        case KEST_TOK_GTGT:
            if (b < 0) {
                *why = "a shift of a count below nought is not a shift";
                return false;
            }
            if (b >= 64) {
                out->integer = unsigned_ ? 0 : (a < 0 ? -1 : 0);
                break;
            }
            out->integer = unsigned_ ? (int64_t)((uint64_t)a >> b) : a >> b;
            break;
        case KEST_TOK_LT:
            out->integer = unsigned_ ? (uint64_t)a < (uint64_t)b : a < b;
            break;
        case KEST_TOK_LTEQ:
            out->integer = unsigned_ ? (uint64_t)a <= (uint64_t)b : a <= b;
            break;
        case KEST_TOK_GT:
            out->integer = unsigned_ ? (uint64_t)a > (uint64_t)b : a > b;
            break;
        case KEST_TOK_GTEQ:
            out->integer = unsigned_ ? (uint64_t)a >= (uint64_t)b : a >= b;
            break;
        case KEST_TOK_EQEQ:
            out->integer = a == b;
            break;
        case KEST_TOK_BANGEQ:
            out->integer = a != b;
            break;
        case KEST_TOK_AMPAMP:
            out->integer = a && b;
            break;
        case KEST_TOK_PIPEPIPE:
            out->integer = a || b;
            break;
        default:
            return false;
        }
        // A narrower type wraps at its width, the same as it does while
        // running, so a constant and the arithmetic that made it agree.
        if (type != NULL && type->tag == KEST_T_INT && type->width < 64) {
            uint64_t held = (uint64_t)out->integer;
            uint64_t mask = (~(uint64_t)0) >> (64 - type->width);
            held &= mask;
            if (!unsigned_ && (held & (mask ^ (mask >> 1))) != 0) {
                held |= ~mask;
            }
            out->integer = (int64_t)held;
        }
        return true;
    }
    // A choice is where this stops on purpose rather than for want of a case.
    // Working one out would mean binding what a case carries to a name and
    // folding an arm under it, which is an environment, which is a second
    // machine — and this compiler has spent a week making two of a thing into
    // one. A constant that picks between two values is two constants and a
    // program that picks. See D672.
    case KEST_EXPR_MATCH:
    case KEST_EXPR_IF:
        *why = "a choice is made while running: a constant that picks between "
               "two values is two constants and a program that picks";
        program->fold_never = true;
        return false;
    default:
        return false;
    }
}

// A value laid out flat: one slot for a scalar, and a slot per scalar for a
// struct built where it is written. The arithmetic is all scalar, so this is
// only about how many of them there are.
// What a call gives back and what it builds are two different things. A shape
// is built by naming it, and the arguments are its fields; a function in a
// module that answers the same shape is a call, and its arguments are whatever
// it takes. Read the other way round, `random.next(random.from(5))` folded to
// the seed it was handed -- a constant of a type the program never worked out.
// What tells them apart is that a function's name is of a function type and a
// shape's name is of the shape. See D887.
static bool builds_rather_than_calls(const KestExpr *expr) {
    const KestExpr *callee = expr->call.callee;
    return callee != NULL &&
           (callee->type == NULL || callee->type->tag != KEST_T_FN);
}

// How many bytes a piece of text worked out where it was written is. Measuring
// it would stop at a nought, and a nought is a byte text may hold since D971,
// so this reads it from the source the value came from instead: a written
// piece of text says how long it is, and a name that stands for one is asked
// about whatever it stands for. Anything else has no written form to read and
// is measured, which is right for every value that cannot hold a nought.
static size_t folded_text_length(KestProgram *program, const KestExpr *expr,
                                 const char *bytes, uint32_t depth) {
    if (expr != NULL && depth <= 32) {
        switch (expr->kind) {
        case KEST_EXPR_STRING: {
            KestSpan content = {expr->span.offset + 1, expr->span.length - 2};
            size_t length = 0;
            kest_literal_text(program->arena, program->source, content,
                              &length);
            return length;
        }
        case KEST_EXPR_NAME: {
            const KestExpr *written = constant_written(
                program, kest_span_text(program->source, expr->span),
                expr->span.length);
            if (written != NULL) {
                return folded_text_length(program, written, bytes, depth + 1);
            }
            break;
        }
        default:
            break;
        }
    }
    return strlen(bytes);
}

static uint32_t fold_slots(KestProgram *program, const KestExpr *expr,
                           KestValue *out, uint32_t room, uint32_t depth,
                           const char **why) {
    if (expr == NULL || room == 0 || depth > 32) {
        return 0;
    }
    const KestType *type = expr->type;

    if (expr->kind == KEST_EXPR_NAME && type != NULL &&
        (type->tag == KEST_T_STRUCT || type->tag == KEST_T_FIXED)) {
        const KestValue *value = NULL;
        uint32_t slots = 0;
        if (program->held_name != NULL &&
            program->held_name(program->held_context,
                               kest_span_text(program->source, expr->span),
                               expr->span.length, &value, &slots)) {
            return 0;
        }
        const KestExpr *written = constant_written(
            program, kest_span_text(program->source, expr->span),
            expr->span.length);
        if (written != NULL) {
            return fold_slots(program, written, out, room, depth + 1, why);
        }
    }

    // That many of something, written where it stands: the same idea as a
    // struct laid out flat, and the same fold. See below for why a call is
    // asked what it calls first.
    if (type != NULL && type->tag == KEST_T_FIXED &&
        expr->kind == KEST_EXPR_ARRAY) {
        uint32_t used = 0;
        for (uint32_t i = 0; i < expr->array.count; i++) {
            uint32_t wrote = fold_slots(program, expr->array.items[i],
                                        out + used, room - used, depth + 1,
                                        why);
            if (wrote == 0) {
                return 0;
            }
            used += wrote;
        }
        return used == type->slots ? used : 0;
    }

    // A case of an enum, written with what it carries or without: the tag is
    // slot nought and each piece sits where the case says it does. Every other
    // slot is nought, because a value with a hole in it is bytes nobody wrote
    // and two of them built the same way would not compare alike. See D671.
    if (type != NULL && type->tag == KEST_T_ENUM &&
        (expr->kind == KEST_EXPR_FIELD || expr->kind == KEST_EXPR_CALL) &&
        builds_rather_than_calls(expr)) {
        const KestExpr *named = expr->kind == KEST_EXPR_CALL
                                    ? expr->call.callee
                                    : expr;
        if (named == NULL || named->kind != KEST_EXPR_FIELD ||
            room < type->slots) {
            return 0;
        }
        const char *word =
            kest_span_text(program->source, named->field.name);
        uint32_t written = named->field.name.length;
        const KestVariantType *which = NULL;
        uint32_t at = 0;
        for (; at < type->case_count; at++) {
            if (kest_word_same(type->cases[at].name, word, written)) {
                which = &type->cases[at];
                break;
            }
        }
        if (which == NULL) {
            return 0;
        }
        uint32_t carried =
            expr->kind == KEST_EXPR_CALL ? expr->call.arg_count : 0;
        if (carried != which->payload_count) {
            return 0;
        }
        for (uint32_t slot = 0; slot < type->slots; slot++) {
            out[slot].integer = 0;
        }
        out[0].integer = at;
        for (uint32_t piece = 0; piece < carried; piece++) {
            uint32_t wide = which->payload[piece]->slots;
            if (which->offsets[piece] + wide > type->slots ||
                fold_slots(program, expr->call.args[piece],
                           out + which->offsets[piece], wide, depth + 1,
                           why) != wide) {
                return 0;
            }
        }
        return type->slots;
    }

    if (type != NULL && type->tag == KEST_T_STRUCT &&
        expr->kind == KEST_EXPR_CALL && builds_rather_than_calls(expr)) {
        // One with no fields is one slot of nought, which is what the machine
        // pushes for it and what its width says it is. Worked out as no slots
        // at all, it was a constant that could not be written down of a value
        // a program may make anywhere else. See D807.
        if (type->member_count == 0 && expr->call.arg_count == 0) {
            out[0].integer = 0;
            return 1;
        }
        uint32_t used = 0;
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            uint32_t wrote = fold_slots(program, expr->call.args[i], out + used,
                                        room - used, depth + 1, why);
            if (wrote == 0) {
                return 0;
            }
            used += wrote;
        }
        // Every field or none: a struct that was not filled where it was
        // written is not a value yet.
        return used == type->slots ? used : 0;
    }

    // An element of a constant run, or a field of a constant struct, is a
    // constant: worked out here rather than copied into slots and read back.
    if (expr->kind == KEST_EXPR_INDEX || expr->kind == KEST_EXPR_FIELD) {
        const KestExpr *object = expr->kind == KEST_EXPR_INDEX
                                     ? expr->index.object
                                     : expr->field.object;
        const KestType *held = object == NULL ? NULL : object->type;
        uint32_t wide = held == NULL ? 0 : held->slots;
        if (wide == 0 || (held->tag != KEST_T_FIXED &&
                          held->tag != KEST_T_STRUCT)) {
            return 0;
        }
        KestValue *inside =
            KEST_ARENA_ARRAY(program->arena, KestValue, wide);
        if (inside == NULL ||
            fold_slots(program, object, inside, wide, depth + 1, why) != wide) {
            return 0;
        }
        uint32_t from = 0;
        uint32_t many = 0;
        if (expr->kind == KEST_EXPR_INDEX) {
            KestValue where = {0};
            if (held->tag != KEST_T_FIXED ||
                !fold(program, expr->index.index, &where, depth + 1, why) ||
                where.integer < 0 || where.integer >= (int64_t)held->count) {
                return 0;
            }
            many = held->element->slots;
            from = (uint32_t)where.integer * many;
        } else {
            const KestMember *member = NULL;
            for (uint32_t i = 0; i < held->member_count; i++) {
                if (held->members[i].name != NULL &&
                    kest_word_same(held->members[i].name,
                                   kest_span_text(program->source,
                                                  expr->field.name),
                                   expr->field.name.length)) {
                    member = &held->members[i];
                    break;
                }
            }
            if (member == NULL || member->type == NULL) {
                return 0;
            }
            from = member->offset;
            many = member->type->slots;
        }
        if (many == 0 || many > room || from + many > wide) {
            return 0;
        }
        memcpy(out, inside + from, sizeof(KestValue) * many);
        return many;
    }

    KestValue one = {0};
    if (!fold(program, expr, &one, depth, why)) {
        return 0;
    }
    out[0] = one;
    // A piece of text is two: what it is made of and how many bytes that is.
    // The fold above works out the first and this is the second, which is the
    // one place a value worked out where it was written has a width that is
    // not one. See D964.
    if (type != NULL && type->tag == KEST_T_TEXT) {
        if (room < 2) {
            return 0;
        }
        out[1].integer =
            one.text == NULL
                ? 0
                : (int64_t)folded_text_length(program, expr, one.text, 0);
        return 2;
    }
    return 1;
}

// The number standing for a value, over the same parts that decide whether
// two of them are equal. Anything else would let two equal values differ.
uint64_t kest_hash_value(const KestType *type, const KestValue *slots) {
    switch (type->tag) {
    case KEST_T_FLOAT:
        return kest_mix(slots[0].real == 0.0 ? 0 : (uint64_t)slots[0].integer);
    // The same whole number `==` compares, mixed the way every other whole
    // number here is: `hash` applies to exactly what `==` applies to, so a
    // reference that compares is a reference that hashes. See D923.
    // By the place alone. What is above the place is the number the process
    // handed out, which is one count shared by every machine of a process: a
    // hash of the whole word answers differently on the second machine of a
    // run, and `deterministic` says the same program answers the same thing.
    // Two references to one place that are not the same reference hash alike
    // and compare unequal, which is what a hash is allowed to do and what
    // every table here already handles. See D1054.
    case KEST_T_REF:
        return kest_mix((uint64_t)slots[0].integer &
                        ((1ull << KEST_REF_PLACE_BITS) - 1ull));
    case KEST_T_TEXT:
        // Through the one fold this compiler has, which is what a file is
        // marked with and what a program's `hash` over text answers. See D663.
        return kest_mark_bytes(KEST_MARK_START, slots[0].text,
                               (size_t)slots[1].integer);
    case KEST_T_ENUM: {
        uint64_t bits = kest_mix((uint64_t)slots[0].integer);
        uint32_t which = (uint32_t)slots[0].integer;
        if (which >= type->case_count) {
            return bits;
        }
        const KestVariantType *variant = &type->cases[which];
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            bits = bits * 31 ^
                   kest_hash_value(variant->payload[p], slots + variant->offsets[p]);
        }
        return bits;
    }
    // And a struct, which is a value laid out flat: what it is is its fields,
    // so what it hashes to is what they hash to, folded the way a case's
    // payload is. See D874.
    case KEST_T_STRUCT: {
        uint64_t bits = kest_mix((uint64_t)type->member_count);
        for (uint32_t m = 0; m < type->member_count; m++) {
            const KestMember *member = &type->members[m];
            bits = bits * 31 ^
                   kest_hash_value(member->type, slots + member->offset);
        }
        return bits;
    }
    case KEST_T_FIXED: {
        uint16_t stride = type->element->slots == 0 ? 1 : type->element->slots;
        uint64_t bits = kest_mix((uint64_t)type->count);
        for (uint32_t i = 0; i < type->count; i++) {
            bits = bits * 31 ^
                   kest_hash_value(type->element, slots + (size_t)i * stride);
        }
        return bits;
    }
    // One slot with a number in it, which is what these three are: a whole
    // number, a truth and a set of bits are the bits in slot nought and
    // nothing else.
    case KEST_T_INT:
    case KEST_T_BOOL:
    case KEST_T_FLAGS:
        return kest_mix((uint64_t)slots[0].integer);
    // Every other tag written out rather than left to a `default`, so that a
    // tag added to the language cannot land here by not being mentioned. What
    // decides which reach this is `has_equality`, which lists the same tags,
    // and the compiler holds the two lists to being one another. See D542.
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_OPTIONAL:
    case KEST_T_ARRAY:
    case KEST_T_STORE:
    case KEST_T_FN:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        break;
    }
    // Nothing reaches this: `hash` is refused for every tag above by the
    // checker, which asks `has_equality` first. It is here because C wants a
    // value, and nought is the one a reader of a fault would rather see than
    // whatever was in slot nought.
    return 0;
}

uint64_t kest_mix(uint64_t bits) {
    bits ^= bits >> 33;
    bits *= 0xff51afd7ed558ccdULL;
    bits ^= bits >> 33;
    bits *= 0xc4ceb9fe1a85ec53ULL;
    bits ^= bits >> 33;
    return bits;
}

int64_t kest_real_to_int(uint16_t scalar, double value) {
    // C leaves a value outside the range undefined. This does not: it stops at
    // the end, which is the answer every reader expects and the only one that
    // is the same on every machine.
    double low;
    double high;
    switch (scalar) {
    case KEST_L_I8:
        low = -128.0;
        high = 127.0;
        break;
    case KEST_L_I16:
        low = -32768.0;
        high = 32767.0;
        break;
    case KEST_L_I32:
        low = -2147483648.0;
        high = 2147483647.0;
        break;
    case KEST_L_U8:
        low = 0.0;
        high = 255.0;
        break;
    case KEST_L_U16:
        low = 0.0;
        high = 65535.0;
        break;
    case KEST_L_U32:
        low = 0.0;
        high = 4294967295.0;
        break;
    case KEST_L_U64:
        low = 0.0;
        high = 18446744073709551615.0;
        break;
    default:
        low = -9223372036854775808.0;
        high = 9223372036854775807.0;
        break;
    }
    if (value != value) {
        return 0;
    }
    if (value <= low) {
        return scalar == KEST_L_I64 ? INT64_MIN : (int64_t)low;
    }
    if (value >= high) {
        return scalar == KEST_L_U64    ? (int64_t)UINT64_MAX
               : scalar == KEST_L_I64  ? INT64_MAX
                                       : (int64_t)high;
    }
    // A `u64` is converted through one. Everything else here fits an `int64_t`
    // by the time it reaches this line, and a `u64` does not: the whole top
    // half of its range is above `INT64_MAX`, so `(int64_t)value` was undefined
    // where it mattered and saturated at two to the sixty-third in practice.
    // Ten to the nineteenth came back as nine and a quarter. See D930.
    if (scalar == KEST_L_U64) {
        return (int64_t)(uint64_t)value;
    }
    return (int64_t)value;
}

uint32_t kest_fold_const(KestProgram *program, const KestExpr *expr,
                         KestValue *out, uint32_t room, const char **why,
                         bool *never) {
    *why = NULL;
    program->fold_never = false;
    uint32_t filled = fold_slots(program, expr, out, room, 0, why);
    // Counted when it worked, because an asking that came to nothing is a
    // question rather than a piece of work: what a reader wants to know is
    // how many values this compiler worked out, and a constant is one of
    // those however many times it is read. See D675.
    if (filled == room) {
        program->folds++;
    } else {
        program->asked_for_nothing++;
    }
    if (never != NULL) {
        *never = program->fold_never;
    }
    return filled;
}

bool kest_type_has_text(const KestType *type, const KestType **without) {
    if (type == NULL) {
        return false;
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
                if (!kest_type_has_text(type->cases[c].payload[p], without)) {
                    return false;
                }
            }
        }
        return true;
    // A value laid out flat is written the way a program writes one, which is
    // the same rule that gave it `==` in D874: a struct is its name and its
    // fields, `[T; N]` is its elements in brackets, and each is written when
    // everything in it is. See D876.
    case KEST_T_STRUCT:
        for (uint32_t m = 0; m < type->member_count; m++) {
            if (!kest_type_has_text(type->members[m].type, without)) {
                return false;
            }
        }
        return true;
    case KEST_T_FIXED:
        return kest_type_has_text(type->element, without);
    // `none`, or what it holds written the way it is written on its own.
    // Both are what a program writes, which is the whole of the rule.
    case KEST_T_OPTIONAL:
        return kest_type_has_text(type->element, without);
    // Written out rather than left to a `default`, so that a tag added to the
    // language does not quietly land on the wrong side of this. The writer in
    // the machine lists the same tags for the same reason, and the two lists
    // are what has to agree.
    case KEST_T_VOID:
    case KEST_T_ARRAY:
    case KEST_T_REF:
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

bool kest_type_holds_own(const KestType *type, const KestType **what) {
    if (type == NULL) {
        return false;
    }
    switch (type->tag) {
    // Every one of these is a machine word standing for something the machine
    // keeps: the bytes of a piece of text, the header of an array or a store,
    // the place a reference names, the function a value stands for.
    case KEST_T_TEXT:
    case KEST_T_ARRAY:
    case KEST_T_STORE:
    case KEST_T_REF:
    case KEST_T_FN:
        *what = type;
        return true;
    case KEST_T_STRUCT:
        for (uint32_t i = 0; i < type->member_count; i++) {
            if (kest_type_holds_own(type->members[i].type, what)) {
                return true;
            }
        }
        return false;
    case KEST_T_ENUM:
        for (uint32_t c = 0; c < type->case_count; c++) {
            for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
                if (kest_type_holds_own(type->cases[c].payload[p], what)) {
                    return true;
                }
            }
        }
        return false;
    case KEST_T_FIXED:
    case KEST_T_OPTIONAL:
        return kest_type_holds_own(type->element, what);
    // Written out rather than left to a `default`, so that a tag added to the
    // language does not quietly land on the side that can be lent.
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_BOOL:
    case KEST_T_INT:
    case KEST_T_FLOAT:
    case KEST_T_FLAGS:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        return false;
    }
    return false;
}

const char *kest_type_written(const KestType *type) {
    if (type == NULL || type->name == NULL) {
        return NULL;
    }
    const char *dot = strrchr(type->name, '.');
    return dot == NULL ? type->name : dot + 1;
}


// The closest declared type name, or NULL when nothing is close enough to be
// worth putting in front of a reader. A wrong suggestion costs more than none.
// The way a file writes a registered name: the last part of its module and
// then the name, `vec.Vec2` for `std.vec.Vec2`. It is what a suggestion has to
// answer with and what a written name has to be compared against, because a
// name lives under the whole of its module and nobody types the whole of it.
// The name itself where it is under no module. See D1039.
const char *kest_written_as(KestProgram *program, const char *whole) {
    const char *dot = strrchr(whole, '.');
    if (dot == NULL) {
        return whole;
    }
    const char *from = whole;
    for (const char *at = whole; at < dot; at++) {
        if (*at == '.') {
            from = at + 1;
        }
    }
    if (from == whole) {
        return whole;
    }
    size_t room = strlen(from) + 1;
    char *written = kest_arena_alloc(program->arena, room, 1);
    if (written == NULL) {
        return whole;
    }
    memcpy(written, from, room);
    return written;
}

static const char *nearest_type(KestProgram *program, const char *name,
                                     size_t length) {
    // Every one or two character name is one edit from every other, so a
    // suggestion at that length carries no information.
    if (length < 3) {
        return NULL;
    }
    uint32_t limit = length == 3 ? 1 : (uint32_t)length / 3;
    const char *best = NULL;
    uint32_t best_distance = limit + 1;

    bool written_plain = memchr(name, '.', length) == NULL;
    for (uint32_t i = 0; i < program->type_count; i++) {
        const char *candidate = program->types[i]->name;
        // A copy of a generic is named for what it was made with, and nobody
        // wrote that name: `Pair<i32, text>` is not what somebody meant to
        // type. The shape it came from is in this list under its own name.
        if (candidate == NULL || strchr(candidate, '<') != NULL) {
            continue;
        }
        if (kest_needs_import(program, candidate, strlen(candidate))) {
            continue;
        }
        // A declared type is held under its module and written without it, so
        // what is compared is the part that was written the same way (D198).
        const char *dot = strrchr(candidate, '.');
        const char *tail = dot == NULL ? candidate : dot + 1;
        const char *written = kest_written_as(program, candidate);
        const char *against = written_plain ? tail : written;
        uint32_t distance =
            kest_word_distance(name, length, against, strlen(against), limit);
        if (distance >= best_distance) {
            continue;
        }
        best_distance = distance;
        // Reachable by the last piece alone means this file declared it, and
        // that is how it is written back.
        best = kest_lookup_type(program, tail, strlen(tail)) != NULL ? tail
                                                                    : written;
    }
    return best;
}

// The name to write where this type is wanted: its own, without the module in
// front of it when the module is the file's own, and with its own names for
// the types it takes. A suggestion showing one type for a shape that takes two
// is a suggestion that does not compile.
// The type names a shape is waiting for, said as names: `` `T` `` for one and
// `` `A` and `B` `` for two. NULL for a shape that takes none. Static, because
// the one sentence that says them is in this file and nothing else has a
// reason to build the list. See D757 and D758.
static const char *type_names(KestArena *arena, const KestType *type) {
    if (type->type_param_count == 0) {
        return NULL;
    }
    // In the arena and as long as it is. What this used to build was a form --
    // `Box<T>` -- and a form is code: `T` is a placeholder where the
    // declaration wrote it, so a program that also declares a `struct T` makes
    // that form compile and mean a box of something else. A suggestion that
    // compiles and is wrong is worse than one that does not. So the names are
    // said as names, and where they were written is what the note carries.
    // See D757.
    size_t room = 1;
    for (uint32_t i = 0; i < type->type_param_count; i++) {
        const char *held = type->type_param_names == NULL
                               ? NULL
                               : type->type_param_names[i];
        room += strlen(held == NULL ? "T" : held) + strlen("`` and ");
    }
    char *out = kest_arena_alloc(arena, room, 1);
    if (out == NULL) {
        return NULL;
    }
    size_t used = 0;
    for (uint32_t i = 0; i < type->type_param_count; i++) {
        const char *held = type->type_param_names == NULL
                               ? NULL
                               : type->type_param_names[i];
        // A list a person reads: nothing before the first, ` and ` before the
        // last, a comma between the rest.
        const char *before = i == 0                                ? ""
                             : i + 1 == type->type_param_count     ? " and "
                                                                   : ", ";
        used += (size_t)snprintf(out + used, room - used, "%s`%s`", before,
                                 held == NULL ? "T" : held);
    }
    return out;
}

KestType *kest_resolve_type_ref(KestProgram *program,
                                const KestTypeRef *ref);

// What a composed type is made of, as a number: the kind, what it holds and
// how many. The element is compared by address rather than by shape, which is
// what the walk this replaced did -- a composed type is interned here, so two
// of the same shape are one address. See D1088.
static uint32_t shape_hash(KestTypeTag tag, const KestType *element,
                           uint32_t count) {
    uint64_t mixed = (uint64_t)tag * 1099511628211u;
    mixed ^= (uint64_t)(uintptr_t)element * 2654435761u;
    mixed ^= (uint64_t)count * 40503u;
    return (uint32_t)(mixed ^ (mixed >> 32));
}

static uint32_t composed_slot(const KestProgram *program, KestTypeTag tag,
                              const KestType *element, uint32_t count) {
    uint32_t mask = program->composed_by_shape_slots - 1;
    uint32_t slot = shape_hash(tag, element, count) & mask;
    while (program->composed_by_shape[slot] != 0) {
        const KestType *already =
            program->composed[program->composed_by_shape[slot] - 1u];
        if (already->tag == tag && already->element == element &&
            already->count == count) {
            return slot;
        }
        slot = (slot + 1u) & mask;
    }
    return slot;
}

static bool composed_room(KestProgram *program) {
    if (program->composed_by_shape_slots >= (program->composed_count + 1) * 2) {
        return true;
    }
    uint32_t slots = program->composed_by_shape_slots == 0
                         ? 64
                         : program->composed_by_shape_slots * 2;
    uint32_t *made = KEST_ARENA_ARRAY(program->arena, uint32_t, slots);
    if (made == NULL) {
        return false;
    }
    program->composed_by_shape = made;
    program->composed_by_shape_slots = slots;
    for (uint32_t i = 0; i < program->composed_count; i++) {
        const KestType *one = program->composed[i];
        uint32_t slot =
            composed_slot(program, one->tag, one->element, one->count);
        program->composed_by_shape[slot] = i + 1u;
    }
    return true;
}

static KestType *compose(KestProgram *program, KestTypeTag tag,
                         KestType *element, uint32_t count) {
    // What it is made of is all it is, so one already made of the same thing
    // is the same type. Asked by what went in rather than by what came out:
    // `kest_type_equal` is assignability and would answer yes for a
    // `fn() no.alloc` where a `fn()` was wanted, which is the right answer to
    // a different question. See D780.
    if (!composed_room(program)) {
        return NULL;
    }
    uint32_t slot = composed_slot(program, tag, element, count);
    if (program->composed_by_shape[slot] != 0) {
        return program->composed[program->composed_by_shape[slot] - 1u];
    }
    if (program->composed_count == program->composed_capacity) {
        void *moved = grow(program->arena, program->composed,
                           program->composed_count,
                           &program->composed_capacity, sizeof(KestType *));
        if (moved == NULL) {
            return NULL;
        }
        program->composed = moved;
    }
    KestType *type = new_type(program, tag);
    if (type == NULL) {
        return NULL;
    }
    program->composed[program->composed_count++] = type;
    type->element = element;
    type->count = count;
    // Put in where the lookup above looked, which is where it will be looked
    // for again.
    program->composed_by_shape[composed_slot(program, tag, element, count)] =
        program->composed_count;
    // A reference and an array are one handle. An optional carries a tag
    // beside whatever it holds, which is what lets a lookup that finds
    // nothing cost no allocation.
    // A reference is one slot: an index with the generation it was handed out
    // at packed above it, so a stale one is recognised rather than followed.
    type->slots = tag == KEST_T_OPTIONAL && element != NULL
                      ? (uint16_t)(element->slots + 1)
                      : 1;
    if (tag == KEST_T_OPTIONAL && element != NULL) {
        // What it holds, then a byte saying whether it does, laid out the way
        // a C struct of the two would be.
        type->byte_align = element->byte_align;
        uint16_t used = (uint16_t)(element->byte_size + 1);
        uint16_t align = type->byte_align == 0 ? 1 : type->byte_align;
        type->byte_size = (uint16_t)((used + align - 1) / align * align);
    } else {
        type->byte_size = 8;
        type->byte_align = 8;
    }
    return type;
}

// Which of the two kinds of name a global is. `is_const` does not say: a
// function is declared with it set, because what it marks is a name that
// cannot be written to rather than a name for a value. What a function has
// that nothing else does is a signature. See D740.
static bool is_a_function(const KestSymbol *symbol) {
    return symbol->type != NULL && symbol->type->tag == KEST_T_FN;
}

static KestType *error_type(KestProgram *program) {
    return new_type(program, KEST_T_ERROR);
}

KestType *kest_bound_type(KestProgram *program, const char *name,
                          size_t length) {
    for (uint32_t i = 0; i < program->bound_count; i++) {
        if (kest_word_same(program->bound_names[i], name, length)) {
            return program->bound_types[i];
        }
    }
    return NULL;
}

// One sentence for every way a type can be written with the wrong number of
// type names after it. It was three, and two of them named the shape
// differently -- `Box` as the reader wrote it and `c.Pair` qualified -- so a
// reader meeting both in one file was told about two things. It is one thing
// that happened: what the shape takes, and what was written. The shape is
// named as the reader wrote it, because a diagnostic about what somebody wrote
// calls it what they called it, and where it came from is what the note is
// for. See D756.
static void wrong_type_count(KestProgram *program, KestSpan where,
                             const char *name, size_t length, uint32_t takes,
                             uint32_t written, const KestType *shape,
                             const char *written_as) {
    char wanted[32];
    char given[32];
    if (takes == 0) {
        snprintf(wanted, sizeof(wanted), "no types");
    } else {
        snprintf(wanted, sizeof(wanted), "%u type%s", takes,
                 takes == 1 ? "" : "s");
    }
    if (written == 0) {
        snprintf(given, sizeof(given), "none are");
    } else {
        snprintf(given, sizeof(given), "%u %s", written,
                 written == 1 ? "is" : "are");
    }
    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0302", where,
                   "`%.*s` takes %s, and %s written here", (int)length, name,
                   wanted, given);
    if (takes == 0) {
        kest_diags_suggest(program->diags, "write it without them: `%.*s`",
                           (int)length, name);
    } else if (written_as != NULL) {
        kest_diags_suggest(program->diags, "write a type for each of them: %s",
                           written_as);
    }
    if (shape != NULL && shape->declared_in != NULL) {
        kest_diags_note(program->diags, shape->declared_in, shape->span,
                        "declared here");
    }
}

static KestType *resolve_named(KestProgram *program, const KestTypeRef *ref) {
    const char *name = kest_span_text(program->source, ref->name);
    size_t length = ref->name.length;

    // A type name a generic function brought into scope stands for whatever
    // this instance was given, or for itself while the signature is declared.
    KestType *bound = kest_bound_type(program, name, length);
    if (bound != NULL) {
        return bound;
    }

    KestType *type = kest_lookup_type(program, name, length);
    if (type != NULL && type->type_param_count > 0) {
        wrong_type_count(program, ref->name, name, length,
                         type->type_param_count, 0, type,
                         type_names(program->arena, type));
        return error_type(program);
    }
    // The absence of a value is registered under a name so the compiler can
    // look it up, and that name is reachable to anybody who writes it. `fn f()
    // -> void` compiled, which is a second spelling of `fn f()` — and this
    // language refuses second spellings, the way it refuses `if (x < 3)`. See
    // D519.
    if (type != NULL && type->tag == KEST_T_VOID) {
        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0357", ref->name,
                       "`void` is not a type this language writes");
        kest_diags_suggest(program->diags,
                           "a function that gives nothing back is written "
                           "with no `->`");
        return error_type(program);
    }
    if (type != NULL) {
        kest_import_reached(program, name, length);
        if (kest_needs_import(program, name, length)) {
            const char *dot = memchr(name, '.', length);
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0325",
                           ref->name, "this file does not import `%.*s`",
                           (int)(dot - name), name);
            kest_diags_suggest(program->diags,
                               "a name is only reachable from a module this "
                               "file asked for");
            if (type->declared_in != NULL) {
                kest_diags_note(program->diags, type->declared_in, type->span,
                                "declared here");
            }
        }
        return type;
    }

    // A name the program has, written where a type goes: the other half of
    // the refusal the name walk makes for a type written where a value goes.
    // Saying `unknown` about a name a reader has declared sends them looking
    // for a spelling mistake in a word they spelt right. See D740.
    KestSymbol *held = kest_lookup_global(program, name, length);
    if (held != NULL) {
        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0360", ref->name,
                       "`%.*s` is %s, and this wants a type", (int)length, name,
                       is_a_function(held) ? "a function" : "a constant");
        // Written down is named, which is what keeps a reader from being told
        // to take out the constant they have just written. See D735.
        held->named = true;
        kest_import_reached(program, name, length);
        if (!is_a_function(held)) {
            kest_diags_suggest(program->diags,
                               "a constant counts a run rather than naming "
                               "one: `[i32; %.*s]`",
                               (int)length, name);
        }
        if (held->source != NULL) {
            kest_diags_note(program->diags, held->source, held->span,
                            "declared here");
        }
        return error_type(program);
    }

    // A module written where a type goes. `io` is the half of `io.Colour`
    // that says where to look, and the program has it -- so it is not an
    // unknown type and not a spelling to guess at. The same refusal the name
    // walk makes, about the same word, asked of the one place that knows what
    // a module is. See D739.
    if (kest_module_named(program, name, length)) {
        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0359", ref->name,
                       "`%.*s` is a module, and this wants a type", (int)length,
                       name);
        // Naming it is writing to it. See D735.
        kest_import_reached_by(program, name, length);
        // A type under it first, which is what was asked for -- and what a
        // module of shapes has nothing but. Signatures resolve before bodies,
        // so the functions under a module may not be registered yet where a
        // type is asked for, which is the other reason to look here. See D739.
        const char *example = NULL;
        for (uint32_t i = 0; i < program->type_count; i++) {
            const char *whole = program->types[i]->name;
            if (whole != NULL &&
                under_module_of(program, whole, name, length) &&
                !kest_needs_import(program, whole, strlen(whole))) {
                example = whole;
                break;
            }
        }
        if (example == NULL) {
            const KestSymbol *under = kest_first_under(program, name, length);
            example = under == NULL ? NULL : under->name;
        }
        if (example != NULL) {
            kest_diags_suggest(program->diags,
                               "a module is a place to look and not a type: "
                               "`%s` is one of the names under it",
                               example);
        }
        return error_type(program);
    }

    // The module in front of the name was written to, whatever answers under
    // it: a file whose one use of an import is the name it got wrong would
    // otherwise be told to take the import out as well. See D735.
    kest_import_reached(program, name, length);
    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0301", ref->name,
                   "unknown type `%.*s`", (int)length, name);
    // And one this program has under a module this file has not asked for,
    // which is the same certainty a name of that kind is: not a spelling to
    // try, but the shape the reader has already written, in the file beside
    // this one. Said before any spelling is guessed at. See D733.
    for (uint32_t i = 0; i < program->type_count; i++) {
        const KestType *other = program->types[i];
        const char *whole = other->name;
        if (whole == NULL) {
            continue;
        }
        // Where the module ends is wherever this name begins, because a name
        // lives under the whole of its module. See D1039.
        size_t reach = strlen(whole);
        const char *dot = reach > length + 1 &&
                                  whole[reach - length - 1] == '.' &&
                                  kest_word_same(whole + reach - length, name,
                                                 length)
                              ? whole + reach - length - 1
                              : NULL;
        if (dot == NULL || !kest_needs_import(program, whole, strlen(whole))) {
            continue;
        }
        kest_diags_suggest(program->diags,
                           "`%s` is in this program, and this file does not "
                           "import `%.*s`",
                           whole, (int)(dot - whole), whole);
        if (other->declared_in != NULL) {
            kest_diags_note(program->diags, other->declared_in, other->span,
                            "declared here");
        }
        return error_type(program);
    }
    // And a module the library has. Here the reader wrote the module in front
    // of the type — `vec.Vec2` is the whole name — so what is asked about is
    // the part before the first dot, and what was missing is the line at the
    // top of the file. See D734.
    const char *dot = memchr(name, '.', length);
    if (dot != NULL && program->files != NULL &&
        !kest_file_reaches(program, name, (size_t)(dot - name)) &&
        kest_library_has(program->files->library, name,
                         (size_t)(dot - name))) {
        kest_diags_suggest(program->diags,
                           "`std.%.*s` is in the library, and this file does "
                           "not import it",
                           (int)(dot - name), name);
        return error_type(program);
    }
    const char *nearest = nearest_type(program, name, length);
    if (nearest != NULL) {
        kest_diags_suggest(program->diags, "did you mean `%s`?", nearest);
    }
    return error_type(program);
}

KestType *kest_array_of(KestProgram *program, KestType *element) {
    return compose(program, KEST_T_ARRAY, element, 0);
}

KestType *kest_optional_of(KestProgram *program, KestType *element) {
    return compose(program, KEST_T_OPTIONAL, element, 0);
}

KestType *kest_ref_of(KestProgram *program, KestType *element) {
    return compose(program, KEST_T_REF, element, 0);
}

// That many of something, laid out where it stands. Unlike an array it is a
// value: copying one copies all of it, and a struct holding one holds the
// whole thing rather than a handle to it.
KestType *kest_fixed_of(KestProgram *program, KestType *element,
                        uint32_t count) {
    // Composed like an array or an optional, and like them not registered:
    // it has no name to be found under and two of them are one type by what
    // they hold rather than by being the same one.
    // What it is made of is the element and how many, so two of them are one
    // type, the same way an array of one thing is. Asked through the same
    // lookup, which is why that one takes a count: everything else composed
    // here has none, and nought is what they all agree on. See D781.
    KestType *type = compose(program, KEST_T_FIXED, element, count);
    if (type == NULL) {
        return error_type(program);
    }
    if (element != NULL) {
        type->slots = (uint16_t)(element->slots * count);
        type->byte_size = (uint16_t)(element->byte_size * count);
        type->byte_align = element->byte_align;
    }
    return type;
}

// A function as a value. One slot holding which function it is, and what it
// promises is part of what it is: a value that promises `no.alloc` may go
// where one that does not is wanted, and not the other way round, which is
// what keeps a cost contract provable through an indirect call.
// A function as a value. What it promises is part of what it is.
static KestType *fn_of(KestProgram *program, KestType **params,
                            uint32_t count,
                     KestType *result, bool no_alloc, bool no_host,
                     bool deterministic) {
    KestType *type = new_type(program, KEST_T_FN);
    if (type == NULL) {
        return NULL;
    }
    type->params = KEST_ARENA_ARRAY(program->arena, KestType *,
                                    count == 0 ? 1 : count);
    if (type->params == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < count; i++) {
        type->params[i] = params[i];
    }
    type->param_count = count;
    type->result = result;
    type->no_alloc = no_alloc;
    type->no_host = no_host;
    type->deterministic = deterministic;
    type->slots = 1;
    type->byte_size = 8;
    type->byte_align = 8;
    return type;
}

static bool measure(KestProgram *program, KestType *type);
static bool sized_within(KestProgram *program, uint32_t bytes, uint32_t slots,
                         const KestSource *where, KestSpan span,
                         const char *what);

// One copy of a generic struct per set of types. The copy is a struct like any
// other by the time anything else sees it: fields resolved, laid out, and
// measured, so nothing downstream knows it came from a shape.
KestType *kest_struct_of(KestProgram *program, KestType *shape, KestType **args,
                         uint32_t count) {
    // The name is what a copy is found by as well as what it is called, so a
    // name cut short is two copies being one type: `Box<...One>` and
    // `Box<...Two>` agreeing for two hundred and fifty-six bytes were one
    // struct, and the second was refused for holding what it holds. Sized from
    // the shape and the types, in the arena.
    size_t room = strlen(shape->name) + 3;
    for (uint32_t i = 0; i < count; i++) {
        room += strlen(kest_type_name(program->arena, args[i])) + 2;
    }
    char *written = kest_arena_alloc(program->arena, room, 1);
    if (written == NULL) {
        return error_type(program);
    }
    size_t used = (size_t)snprintf(written, room, "%s<", shape->name);
    for (uint32_t i = 0; i < count; i++) {
        used += (size_t)snprintf(written + used, room - used, "%s%s",
                                 i == 0 ? "" : ", ",
                                 kest_type_name(program->arena, args[i]));
    }
    snprintf(written + used, room - used, ">");

    KestType *made = kest_find_type(program, written, strlen(written));
    if (made != NULL) {
        // A copy is found by its name, and its name is built from the types it
        // was given, so the one found holds those types. If it does not, this
        // project built the name wrongly and two copies are one struct — which
        // is what a name built in a buffer did, and what a program was then
        // refused for.
        // By their names, which is what the key was built from: two of one
        // name are one type by the rule this is checking, and the same type
        // asked for twice is two objects and one name.
        bool same = made->type_arg_count == count;
        for (uint32_t i = 0; same && i < count; i++) {
            same = strcmp(kest_type_name(program->arena, made->type_args[i]),
                          kest_type_name(program->arena, args[i])) == 0;
        }
        if (!same) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0354",
                           shape->span,
                           "two copies of `%s` are one type, which the naming "
                           "of them allowed",
                           shape->name);
            kest_diags_fault(program->diags,
                             "a copy is found by a name built from what it "
                             "was given");
            return error_type(program);
        }
        return made;
    }

    const char *name = written;
    made = new_type(program, shape->tag);
    if (name == NULL || made == NULL || !register_type(program, made, name)) {
        return error_type(program);
    }
    made->span = shape->span;
    made->declared_in = shape->declared_in;
    // A copy exists because something asked for it, and asking for it is
    // naming it: `Box<i32>` written on a `let`, in a signature, or built by
    // naming the shape is what makes this copy at all. Without this, every
    // program that declares a generic struct of its own and uses it was
    // warned that nothing names the copy it had just made — which no file in
    // this tree could show, because the one generic struct here is the
    // library's and the warning is about the file that was named.
    made->named = true;
    // Which shape this is a copy of, so a value built by naming the shape can
    // be recognised as this one.
    made->decl = shape->decl;
    made->unit = shape->unit;
    made->shape = shape;
    made->type_args = KEST_ARENA_ARRAY(program->arena, KestType *,
                                       count == 0 ? 1 : count);
    if (made->type_args == NULL) {
        return error_type(program);
    }
    for (uint32_t i = 0; i < count; i++) {
        made->type_args[i] = args[i];
    }
    made->type_arg_count = count;

    const KestDecl *decl = shape->decl;
    const KestUnitInfo *was_unit = program->unit;
    const KestSource *was_source = program->source;
    const char *was_alias = program->alias;
    const char *was_module = program->module;
    kest_program_in(program, (KestUnitInfo *)shape->unit);

    const char **names =
        KEST_ARENA_ARRAY(program->arena, const char *, count == 0 ? 1 : count);
    KestType **bound =
        KEST_ARENA_ARRAY(program->arena, KestType *, count == 0 ? 1 : count);
    if (names == NULL || bound == NULL) {
        return error_type(program);
    }
    for (uint32_t i = 0; i < count; i++) {
        names[i] = shape->type_param_names[i];
        bound[i] = args[i];
    }
    // A copy may name the shape again with other types, so what was bound
    // before this one has to come back after it.
    uint32_t was_count = program->bound_count;
    const char **was_names = KEST_ARENA_ARRAY(program->arena, const char *,
                                              was_count == 0 ? 1 : was_count);
    KestType **was_types = KEST_ARENA_ARRAY(program->arena, KestType *,
                                            was_count == 0 ? 1 : was_count);
    if (was_names == NULL || was_types == NULL) {
        return error_type(program);
    }
    for (uint32_t i = 0; i < was_count; i++) {
        was_names[i] = program->bound_names[i];
        was_types[i] = program->bound_types[i];
    }
    kest_bind_types(program, names, bound, count);

    if (shape->tag == KEST_T_ENUM) {
        // What each case carries, with the names it was written with standing
        // for what this copy was asked for. A case's name and how many things
        // it carries are the shape's; what they are is this copy's. The walk
        // that refuses two cases of one name ran when the shape was resolved,
        // so it is not run again here. See D1048.
        uint32_t how_many = decl->choice.case_count;
        KestVariantType *cases = KEST_ARENA_ARRAY(program->arena,
                                                  KestVariantType,
                                                  how_many == 0 ? 1 : how_many);
        if (cases == NULL) {
            return error_type(program);
        }
        for (uint32_t c = 0; c < how_many && c < shape->case_count; c++) {
            const KestVariant *written_case = decl->choice.cases[c];
            uint32_t held = shape->cases[c].payload_count;
            cases[c].name = shape->cases[c].name;
            cases[c].span = shape->cases[c].span;
            cases[c].payload_count = held;
            cases[c].payload = KEST_ARENA_ARRAY(program->arena, KestType *,
                                                held == 0 ? 1 : held);
            cases[c].offsets = KEST_ARENA_ARRAY(program->arena, uint16_t,
                                                held == 0 ? 1 : held);
            cases[c].byte_offsets = KEST_ARENA_ARRAY(program->arena, uint16_t,
                                                     held == 0 ? 1 : held);
            if (cases[c].payload == NULL || cases[c].offsets == NULL ||
                cases[c].byte_offsets == NULL) {
                return error_type(program);
            }
            for (uint32_t p = 0; p < held; p++) {
                cases[c].payload[p] =
                    kest_resolve_type_ref(program, written_case->payload[p]);
            }
        }
        made->cases = cases;
        made->case_count = shape->case_count;
    } else {
        uint32_t fields = decl->record.field_count;
        KestMember *members = KEST_ARENA_ARRAY(program->arena, KestMember,
                                               fields == 0 ? 1 : fields);
        if (members == NULL) {
            return error_type(program);
        }
        for (uint32_t f = 0; f < fields; f++) {
            members[f].name = span_string(program,
                                          decl->record.fields[f]->name);
            members[f].span = decl->record.fields[f]->name;
            members[f].own = decl->record.fields[f]->own.length != 0;
            members[f].type =
                kest_resolve_type_ref(program, decl->record.fields[f]->type);
        }
        made->members = members;
        made->member_count = fields;
    }

    kest_bind_types(program, was_names, was_types, was_count);
    program->unit = was_unit;
    program->source = was_source;
    program->alias = was_alias;
    program->module = was_module;

    measure(program, made);
    return made;
}

KestType *kest_resolve_type_ref(KestProgram *program,
                                const KestTypeRef *ref) {
    if (ref == NULL) {
        return error_type(program);
    }

    switch (ref->kind) {
    case KEST_TYPE_FN: {
        KestType *params[16];
        uint32_t count = ref->arg_count < 16 ? ref->arg_count : 16;
        for (uint32_t i = 0; i < count; i++) {
            params[i] = kest_resolve_type_ref(program, ref->args[i]);
        }
        KestType *result = ref->element == NULL
                               ? kest_lookup_type(program, "void", 4)
                               : kest_resolve_type_ref(program, ref->element);
        return fn_of(program, params, count, result, ref->no_alloc,
                     ref->no_host, ref->deterministic);
    }

    case KEST_TYPE_NAMED:
        return resolve_named(program, ref);

    case KEST_TYPE_GENERIC: {
        const char *name = kest_span_text(program->source, ref->name);
        bool is_ref = kest_word_same("ref", name, ref->name.length);
        bool is_store = kest_word_same("store", name, ref->name.length);
        if (!is_ref && !is_store) {
            // `Pair<i32, text>`: a copy of a shape, made the first time it is
            // written and found again after that.
            KestType *shape = kest_lookup_type(program, name, ref->name.length);
            if (shape != NULL && shape->type_param_count > 0) {
                uint32_t count = ref->arg_count;
                KestType **args = KEST_ARENA_ARRAY(program->arena, KestType *,
                                                   count == 0 ? 1 : count);
                if (args == NULL) {
                    return error_type(program);
                }
                for (uint32_t i = 0; i < count; i++) {
                    args[i] = kest_resolve_type_ref(program, ref->args[i]);
                }
                if (ref->arg_count != shape->type_param_count) {
                    wrong_type_count(program, ref->span, name,
                                     ref->name.length,
                                     shape->type_param_count, ref->arg_count,
                                     shape,
                                     type_names(program->arena, shape));
                    return error_type(program);
                }
                return kest_struct_of(program, shape, args, count);
            }
            // A shape that takes none, written with some. It is not an
            // unknown generic type -- it is a type, and what is wrong is the
            // angle brackets -- and the walk below would offer the name back
            // as itself, which is D737's mistake in the other walk.
            // See D755.
            if (shape != NULL && shape->tag != KEST_T_ERROR) {
                wrong_type_count(program, ref->span, name, ref->name.length, 0,
                                 ref->arg_count, shape, NULL);
                return error_type(program);
            }
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0302",
                           ref->name, "unknown generic type `%.*s`",
                           (int)ref->name.length, name);
            // `ref` and `store` are the two the language has; the rest are
            // declared, so the nearest declared name is the likelier answer.
            const char *nearest =
                nearest_type(program, name, ref->name.length);
            if (nearest != NULL) {
                kest_diags_suggest(program->diags, "did you mean `%s`?",
                                   nearest);
            } else {
                kest_diags_suggest(program->diags,
                                   "`ref<T>` and `store<T>` are built in, and "
                                   "a shape of your own is written "
                                   "`struct %.*s<T> { }`",
                                   (int)ref->name.length, name);
            }
            return error_type(program);
        }
        if (ref->arg_count != 1) {
            wrong_type_count(program, ref->span, name, ref->name.length, 1,
                             ref->arg_count, NULL,
                             "`T`");
            return error_type(program);
        }
        return compose(program, is_ref ? KEST_T_REF : KEST_T_STORE,
                       kest_resolve_type_ref(program, ref->args[0]), 0);
    }

    case KEST_TYPE_ARRAY: {
        KestType *element = kest_resolve_type_ref(program, ref->element);
        if (ref->count.length == 0) {
            return compose(program, KEST_T_ARRAY, element, 0);
        }
        // A number, or the name of a constant that is one. D064 asked for a
        // literal because a name could be a size that changes; a constant is
        // worked out where it is written and cannot, and a program with the
        // same number in five places is the thing that changes wrongly.
        const char *digits = kest_span_text(program->source, ref->count);
        uint64_t how_many = 0;
        if (digits[0] >= '0' && digits[0] <= '9') {
            for (uint32_t i = 0; i < ref->count.length; i++) {
                how_many = how_many * 10 + (uint64_t)(digits[i] - '0');
            }
        } else {
            // Looked up in the file being read rather than in the symbol
            // table, because a type is resolved before the constants are
            // declared: a struct's fields are what a constant of that struct
            // is measured from, so constants cannot come first.
            // A count names a constant in this file: the token is one name
            // and a name from another file has a dot in it. Its declared type
            // is what says it is a number, because nothing has been checked
            // yet when a type is being resolved.
            // A name with a dot in it is a constant another module declared,
            // found in the file that module is: there are no symbols yet, and
            // the declarations of every file are here. See D681.
            const KestSource *read_in = program->source;
            const char *dot = memchr(digits, '.', ref->count.length);
            const KestDecl *declared =
                dot == NULL
                    ? constant_in_file(program, digits, ref->count.length)
                    : constant_in_module(
                          program, digits, (uint32_t)(dot - digits), dot + 1,
                          ref->count.length - (uint32_t)(dot - digits) - 1,
                          &read_in);
            if (declared != NULL) {
                remember_count(program, digits, ref->count.length);
            }
            const KestSource *was_reading = program->source;
            program->source = read_in;
            const KestType *counted =
                declared == NULL
                    ? NULL
                    : kest_resolve_type_ref(program, declared->constant.type);
            KestValue value = {0};
            const char *why = NULL;
            if (declared == NULL || counted == NULL ||
                counted->tag != KEST_T_INT) {
                program->source = was_reading;
                // Which of the three it is, because they are three different
                // things to do about it: the module is not one this program
                // read, the module is there and has no such constant, or the
                // name is a constant and not a number. One refusal for all
                // three sent a reader to look for whichever they thought of
                // first. See D683.
                if (dot != NULL && declared == NULL) {
                    uint32_t named = (uint32_t)(dot - digits);
                    bool anywhere = false;
                    for (uint32_t f = 0;
                         program->files != NULL && f < program->files->count &&
                         !anywhere;
                         f++) {
                        const char *called = program->files->items[f].alias;
                        anywhere = called != NULL &&
                                   kest_word_same(called, digits, named);
                    }
                    if (!anywhere) {
                        kest_diags_add(program->diags, KEST_SEVERITY_ERROR,
                                       "K0326", ref->count,
                                       "this program reads no module called "
                                       "`%.*s`",
                                       (int)named, digits);
                        kest_diags_suggest(program->diags,
                                           "a count names a constant in this "
                                           "file or in a module the program "
                                           "reads");
                        return error_type(program);
                    }
                    kest_diags_add(program->diags, KEST_SEVERITY_ERROR,
                                   "K0326", ref->count,
                                   "`%.*s` has no constant called `%.*s`",
                                   (int)named, digits,
                                   (int)(ref->count.length - named - 1),
                                   dot + 1);
                    kest_diags_suggest(program->diags,
                                       "`const N: i32 = 16` there, and "
                                       "`[T; %.*s.N]` here",
                                       (int)named, digits);
                    return error_type(program);
                }
                if (declared != NULL) {
                    kest_diags_add(program->diags, KEST_SEVERITY_ERROR,
                                   "K0326", ref->count,
                                   "`%.*s` is a constant and not a number",
                                   (int)ref->count.length, digits);
                    kest_diags_suggest(program->diags,
                                       "a count is how many there are, so it "
                                       "is a whole number");
                    return error_type(program);
                }
                kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0326",
                               ref->count,
                               "a count is a number or a constant that is one");
                kest_diags_suggest(program->diags,
                                   "`const N: i32 = 16` and then `[T; N]`");
                return error_type(program);
            }
            uint32_t worked = declared == NULL
                                  ? 0
                                  : kest_fold_const(program,
                                                    declared->constant.value,
                                                    &value, 1, &why, NULL);
            program->source = was_reading;
            if (worked != 1) {
                kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0326",
                               ref->count,
                               "this count is not worked out where it is "
                               "written");
                kest_diags_suggest(program->diags, "%s",
                                   why != NULL ? why
                                               : "a constant is a number, a "
                                                 "truth or a piece of text, "
                                                 "and arithmetic on those");
                return error_type(program);
            }
            how_many = value.integer < 0 ? 0 : (uint64_t)value.integer;
        }
        if (how_many == 0 || how_many > MAX_ELEMENTS) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0326",
                           ref->count,
                           "an array of that many has no size: %llu",
                           (unsigned long long)how_many);
            kest_diags_suggest(program->diags,
                               "between one and %u, and `[T]` for one that "
                               "grows",
                               MAX_ELEMENTS);
            return error_type(program);
        }
        // What it holds may not be measured yet — a struct is measured after
        // the fields that name it are resolved — and then this is sized again
        // where it is held. Where it is already known, it is known here.
        KestType *run = kest_fixed_of(program, element, (uint32_t)how_many);
        if (element != NULL && element->byte_size != 0 &&
            !sized_within(program,
                          (uint32_t)(element->byte_size * how_many),
                          (uint32_t)(element->slots * how_many),
                          program->source,
                          ref->count, kest_type_name(program->arena, run))) {
            return error_type(program);
        }
        return run;
    }

    case KEST_TYPE_OPTIONAL:
        return compose(program, KEST_T_OPTIONAL,
                       kest_resolve_type_ref(program, ref->element), 0);
    }
    return error_type(program);
}

const char *kest_type_name(KestArena *arena, const KestType *type) {
    if (type == NULL) {
        return "?";
    }
    // What a message calls the absence of a value. `void` is the name it is
    // registered under, because that is what the compiler looks it up by, and
    // it is not a word this language has: a reader told `found `void`` is told
    // about a type they cannot write and cannot look up. See D519.
    if (type->tag == KEST_T_VOID) {
        return "nothing";
    }
    if (type->name != NULL) {
        return type->name;
    }

    if (type->tag == KEST_T_FN) {
        // Sized from what it is made of, both promises included. This was two
        // hundred and fifty-six bytes and gave back what fitted, which is a
        // name that is not the type's — and it is the name a copy of a generic
        // is compiled under as well as the one a message says. Room for one
        // promise where there are two gave back `no.alloc n`, which is the
        // same mistake one promise later. See D853.
        const char *result =
            type->result == NULL || type->result->tag == KEST_T_VOID
                ? NULL
                : kest_type_name(arena, type->result);
        size_t room = strlen("fn()") + strlen(" no.alloc") +
                      strlen(" no.host") + strlen(" deterministic") + 1;
        for (uint32_t i = 0; i < type->param_count; i++) {
            room += strlen(kest_type_name(arena, type->params[i])) + 2;
        }
        room += result == NULL ? 0 : strlen(result) + 4;
        char *written = kest_arena_alloc(arena, room, 1);
        if (written == NULL) {
            return "?";
        }

        size_t used = (size_t)snprintf(written, room, "fn(");
        for (uint32_t i = 0; i < type->param_count; i++) {
            used += (size_t)snprintf(written + used, room - used, "%s%s",
                                     i == 0 ? "" : ", ",
                                     kest_type_name(arena, type->params[i]));
        }
        used += (size_t)snprintf(written + used, room - used, ")");
        if (result != NULL) {
            used += (size_t)snprintf(written + used, room - used, " -> %s",
                                     result);
        }
        // All three, in the order the library writes them. The third was
        // added to the language by D942 and to this by nobody: what stood here
        // was `room += 14`, which grew a number after the memory it described
        // had been handed out and wrote no word at all. So a shape that
        // promised `deterministic` was named as one that did not — in a
        // message telling a reader to write the promise into the shape, in
        // what `check` prints, and in the name a copy of a generic is compiled
        // under. See D1064.
        if (type->no_alloc) {
            used += (size_t)snprintf(written + used, room - used, " no.alloc");
        }
        if (type->no_host) {
            used += (size_t)snprintf(written + used, room - used, " no.host");
        }
        if (type->deterministic) {
            snprintf(written + used, room - used, " deterministic");
        }
        return written;
    }

    if (type->tag == KEST_T_ERROR) {
        return "<unknown>";
    }

    const char *inner = kest_type_name(arena, type->element);
    // What is written round it, with room for a count written out in full.
    size_t room = strlen(inner) + 32;
    char *buffer = kest_arena_alloc(arena, room, 1);
    if (buffer == NULL) {
        return "?";
    }
    switch (type->tag) {
    case KEST_T_ARRAY:
        snprintf(buffer, room, "[%s]", inner);
        break;
    case KEST_T_FIXED:
        snprintf(buffer, room, "[%s; %u]", inner, type->count);
        break;
    case KEST_T_REF:
        snprintf(buffer, room, "ref<%s>", inner);
        break;
    case KEST_T_STORE:
        snprintf(buffer, room, "store<%s>", inner);
        break;
    case KEST_T_OPTIONAL:
        snprintf(buffer, room, "%s?", inner);
        break;
    // Everything left has a name of its own and answered above: a primitive
    // and a declared type are registered under one, a function type is written
    // out where it is made, and an error is `<unknown>`. Nothing reaches these
    // and they are written out rather than left to a `default`, so that a
    // composed tag added to the language cannot come out as `?` — a name that
    // is not the type's is a copy of a generic compiled under somebody else's.
    // See D546.
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_BOOL:
    case KEST_T_INT:
    case KEST_T_FLOAT:
    case KEST_T_TEXT:
    case KEST_T_ENUM:
    case KEST_T_FLAGS:
    case KEST_T_STRUCT:
    case KEST_T_FN:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        return "?";
    }
    return buffer;
}

// A name to a slot, one byte at a time. Every name here is a name somebody
// wrote, so what this has to be is spread over short words that differ in a
// letter or two — not the fastest one there is.
uint32_t kest_name_hash(const char *name, size_t length) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; i++) {
        hash ^= (unsigned char)name[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t name_hash(const char *name, size_t length) {
    return kest_name_hash(name, length);
}

// Whether the index still says what the list says. It is a shortcut, and a
// shortcut that stops being true is a name the program has and cannot find, or
// a name it does not have and finds anyway: the first is caught by every
// program that uses one, and the second is caught by nothing, because what is
// in a table and not in the list it indexes is a place nobody looks at.
//
// So the sanitised build says so, the way the arena says its own. The places
// are added up rather than ticked off, which is what the arena does with what
// it handed out: a table with a place in it twice and another missing does not
// add up to the numbers from one to as many as there are.
#if KEST_CHECKED
static void index_agrees(const KestProgram *program, const char *after) {
    uint32_t filled = 0;
    uint64_t places = 0;
    for (uint32_t slot = 0; slot < program->by_name_slots; slot++) {
        uint32_t at = program->by_name[slot];
        if (at == 0) {
            continue;
        }
        if (at > program->global_count) {
            fprintf(stderr,
                    "kest: after %s the index names place %u of %u names\n",
                    after, at - 1, program->global_count);
            abort();
        }
        filled++;
        places += at;
    }
    if (filled != program->global_count) {
        fprintf(stderr,
                "kest: after %s the index holds %u of the %u names there "
                "are\n",
                after, filled, program->global_count);
        abort();
    }
    uint64_t all = (uint64_t)program->global_count *
                   ((uint64_t)program->global_count + 1) / 2;
    if (places != all) {
        fprintf(stderr,
                "kest: after %s the index holds one name twice and another "
                "not at all\n",
                after);
        abort();
    }
    // And in the order they were declared. Everything under one name is on one
    // run of slots, and which of them a lookup answers with is which of them
    // was put there first: the first `abs` is the one found, and a second
    // declaration of a name is told which line the first one is on. Appending
    // keeps that for nothing. A rebuild puts every name in again, which is
    // where it can be lost, and losing it is a message pointing at the wrong
    // line rather than a program that behaves differently.
    // And the two indexes beside it, for the same reason: what is in a table
    // and not in the list it indexes is a place nobody looks at. Every
    // declaration is found where it was written, and every named type is
    // found by its name -- the first one declared under it, which is what the
    // walk they replaced answered with. See D1086.
    for (uint32_t at = 0; at < program->global_count; at++) {
        const KestSymbol *one = &program->globals[at];
        const KestSymbol *found =
            kest_symbol_at((KestProgram *)program, one->source, one->span);
        if (found == NULL) {
            fprintf(stderr,
                    "kest: after %s the declaration of `%s` is not where it "
                    "was written\n",
                    after, one->name);
            abort();
        }
    }
    for (uint32_t at = 0; at < program->type_count; at++) {
        const char *named = program->types[at]->name;
        if (named == NULL) {
            continue;
        }
        const KestType *first =
            kest_find_type((KestProgram *)program, named, strlen(named));
        if (first == NULL) {
            fprintf(stderr, "kest: after %s the type `%s` is not in the index\n",
                    after, named);
            abort();
        }
    }
    for (uint32_t at = 0; at < program->global_count; at++) {
        const char *name = program->globals[at].name;
        const KestSymbol *first =
            kest_find_global((KestProgram *)program, name, strlen(name));
        if (first == NULL || first > &program->globals[at]) {
            fprintf(stderr,
                    "kest: after %s `%s` is found where it was declared "
                    "second\n",
                    after, name);
            abort();
        }
    }
}
#else
#define index_agrees(program, after) ((void)(program), (void)(after))
#endif

// Puts the global at `at` where its name says. Nothing is ever taken out, so a
// run of full slots is a run of names that landed on the same one, and it ends
// at the first empty slot: everything under a name is on that run, in the
// order it was declared.
static void index_put(KestProgram *program, uint32_t at) {
    const char *name = program->globals[at].name;
    uint32_t mask = program->by_name_slots - 1;
    uint32_t slot = name_hash(name, strlen(name)) & mask;
    while (program->by_name[slot] != 0) {
        slot = (slot + 1) & mask;
    }
    program->by_name[slot] = at + 1;
}

// The same for where a declaration is written. A place is a file and an
// offset in it, and the two together are one name for one declaration. See
// D1086.
static uint32_t place_hash(const KestSource *source, uint32_t offset) {
    uint64_t mixed = (uint64_t)(uintptr_t)source * 1099511628211u;
    mixed ^= (uint64_t)offset * 2654435761u;
    return (uint32_t)(mixed ^ (mixed >> 32));
}

static void place_put(KestProgram *program, uint32_t at) {
    if (program->by_place_slots == 0) {
        return;
    }
    const KestSymbol *one = &program->globals[at];
    uint32_t mask = program->by_place_slots - 1;
    uint32_t slot = place_hash(one->source, one->span.offset) & mask;
    while (program->by_place[slot] != 0) {
        slot = (slot + 1u) & mask;
    }
    program->by_place[slot] = at + 1u;
}

static bool place_room(KestProgram *program) {
    if (program->by_place_slots >= (program->global_count + 1) * 2) {
        return true;
    }
    uint32_t slots =
        program->by_place_slots == 0 ? 64 : program->by_place_slots * 2;
    uint32_t *made = KEST_ARENA_ARRAY(program->arena, uint32_t, slots);
    if (made == NULL) {
        return false;
    }
    program->by_place = made;
    program->by_place_slots = slots;
    for (uint32_t i = 0; i < program->global_count; i++) {
        place_put(program, i);
    }
    return true;
}

// Room for one more, which is a table twice as big when it is half full: a
// table that fills up is the walk this replaced, one probe at a time. What it
// hands out arrives as nought, so an empty slot is what every slot is until
// something is put in it.
static bool index_room(KestProgram *program) {
    if (program->by_name_slots >= (program->global_count + 1) * 2) {
        return true;
    }
    uint32_t slots =
        program->by_name_slots == 0 ? 64 : program->by_name_slots * 2;
    uint32_t *made = KEST_ARENA_ARRAY(program->arena, uint32_t, slots);
    if (made == NULL) {
        return false;
    }
    program->by_name = made;
    program->by_name_slots = slots;
    for (uint32_t i = 0; i < program->global_count; i++) {
        index_put(program, i);
    }
    index_agrees(program, "a bigger index");
    return true;
}

static uint32_t under_one_name(KestProgram *program, const char *name,
                               size_t length, KestSymbol **found,
                               uint32_t room);

// Every function under one name, where the name may be written the way a file
// writes it. The three forms are the ones `kest_lookup_global` tries and for
// the same reason: a name lives under the whole of its module and a file
// writes the last part. See D1039.
uint32_t kest_overloads(KestProgram *program, const char *name, size_t length,
                        KestSymbol **found, uint32_t room) {
    if (program->module[0] != '\0') {
        char stack[256];
        const char *joined =
            under_alias(program, name, length, stack, sizeof(stack));
        if (joined != NULL) {
            uint32_t count =
                under_one_name(program, joined, strlen(joined), found, room);
            if (count > 0) {
                return count;
            }
        }
    }
    {
        char stack[256];
        const char *whole =
            under_import(program, name, length, stack, sizeof(stack));
        if (whole != NULL) {
            uint32_t count =
                under_one_name(program, whole, strlen(whole), found, room);
            if (count > 0) {
                return count;
            }
        }
    }
    return under_one_name(program, name, length, found, room);
}

static uint32_t under_one_name(KestProgram *program, const char *name,
                               size_t length, KestSymbol **found,
                               uint32_t room) {
    uint32_t count = 0;
    if (program->by_name_slots == 0) {
        return 0;
    }
    // The same walk as above and it does not stop at the first: two functions
    // of one name are two entries on one run of slots, in the order they were
    // declared, and the run ends where the empty slot is.
    uint32_t mask = program->by_name_slots - 1;
    uint32_t slot = name_hash(name, length) & mask;
    while (program->by_name[slot] != 0 && count < room) {
        KestSymbol *one = &program->globals[program->by_name[slot] - 1];
        if (kest_word_same(one->name, name, length) &&
            one->type->tag == KEST_T_FN) {
            found[count++] = one;
        }
        slot = (slot + 1) & mask;
    }
    return count;
}

void kest_program_used(KestProgram *program, const KestSource *source,
                       KestSpan span, const KestSource *declared_in,
                       KestSpan declared, const KestType *type,
                       bool is_local) {
    if (program == NULL || source == NULL || !program->index_names) {
        return;
    }
    if (program->use_count == program->use_capacity) {
        uint32_t grown =
            program->use_capacity == 0 ? 64 : program->use_capacity * 2;
        KestUse *moved = KEST_ARENA_ARRAY(program->arena, KestUse, grown);
        if (moved == NULL) {
            // An index for an editor is not worth refusing a compile over:
            // what it costs is that the editor knows less about this file,
            // and what refusing would cost is the file not compiling at all.
            return;
        }
        if (program->use_count > 0) {
            memcpy(moved, program->uses,
                   sizeof(KestUse) * program->use_count);
        }
        program->uses = moved;
        program->use_capacity = grown;
    }
    KestUse *one = &program->uses[program->use_count++];
    one->source = source;
    one->span = span;
    one->declared_in = declared_in;
    one->declared = declared;
    one->type = type;
    one->is_local = is_local;
}

const KestUse *kest_program_uses(const KestProgram *program, uint32_t *count) {
    if (count != NULL) {
        *count = program == NULL ? 0 : program->use_count;
    }
    return program == NULL ? NULL : program->uses;
}

KestSymbol *kest_symbol_at(KestProgram *program, const KestSource *source,
                           KestSpan span) {
    if (program->by_place_slots == 0) {
        return NULL;
    }
    uint32_t mask = program->by_place_slots - 1;
    uint32_t slot = place_hash(source, span.offset) & mask;
    while (program->by_place[slot] != 0) {
        KestSymbol *one = &program->globals[program->by_place[slot] - 1u];
        if (one->source == source && one->span.offset == span.offset) {
            return one;
        }
        slot = (slot + 1u) & mask;
    }
    return NULL;
}

KestSymbol *kest_find_global(KestProgram *program, const char *name,
                             size_t length) {
    if (program->by_name_slots == 0) {
        return NULL;
    }
    // The first one declared under a name is the one this answers with, which
    // is what the walk it replaced did: everything under one name lands on one
    // slot, and what is put there first is what is passed first on the way
    // out.
    uint32_t mask = program->by_name_slots - 1;
    uint32_t slot = name_hash(name, length) & mask;
    while (program->by_name[slot] != 0) {
        KestSymbol *one = &program->globals[program->by_name[slot] - 1];
        if (kest_word_same(one->name, name, length)) {
            return one;
        }
        slot = (slot + 1) & mask;
    }
    return NULL;
}

// What a function is compiled under: its name and what it takes. Two
// functions sharing a name are two functions and need two of these.
static const char *symbol_of(KestProgram *program, const char *name,
                             const KestType *type) {
    // As long as it is, in the arena. This was five hundred and twelve bytes
    // of the stack, and what it did when they ran out was give back the name
    // without what it takes — so two functions of one name, told apart by
    // exactly that, were compiled under one symbol.
    size_t room = strlen(name) + 1;
    for (uint32_t i = 0; i < type->param_count; i++) {
        room += strlen(kest_type_name(program->arena, type->params[i])) + 1;
    }
    char *out = kest_arena_alloc(program->arena, room, 1);
    if (out == NULL) {
        return name;
    }

    size_t used = (size_t)snprintf(out, room, "%s", name);
    for (uint32_t i = 0; i < type->param_count; i++) {
        used += (size_t)snprintf(out + used, room - used, "%c%s",
                                 i == 0 ? '#' : ',',
                                 kest_type_name(program->arena,
                                                type->params[i]));
    }
    return out;
}

// Whether these two take exactly the same things, which is the only way two
// functions of one name are the same function.
static bool same_parameters(const KestType *a, const KestType *b) {
    if (a->param_count != b->param_count) {
        return false;
    }
    for (uint32_t i = 0; i < a->param_count; i++) {
        if (!kest_type_equal(a->params[i], b->params[i])) {
            return false;
        }
    }
    return true;
}

static bool add_global_value(KestProgram *program, const char *name,
                             KestType *type, KestSpan span, bool is_const,
                             const KestExpr *value, const KestDecl *decl);

static bool add_global(KestProgram *program, const char *name, KestType *type,
                       KestSpan span, bool is_const, const KestDecl *decl) {
    return add_global_value(program, name, type, span, is_const, NULL, decl);
}

static bool add_global_value(KestProgram *program, const char *name,
                             KestType *type, KestSpan span, bool is_const,
                             const KestExpr *value, const KestDecl *decl) {
    KestSymbol *existing = kest_find_global(program, name, strlen(name));
    // Two functions may share a name when they take different things. Two of
    // anything else may not, and neither may two that take the same things.
    if (existing != NULL && type->tag == KEST_T_FN &&
        existing->type->tag == KEST_T_FN && !type->is_foreign &&
        !existing->type->is_foreign) {
        uint32_t count = 0;
        KestSymbol *all[32];
        count = kest_overloads(program, name, strlen(name), all, 32);
        existing = NULL;
        for (uint32_t i = 0; i < count; i++) {
            if (same_parameters(all[i]->type, type)) {
                existing = all[i];
                break;
            }
        }
    }
    if (existing != NULL) {
        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0304", span,
                       "`%s` is already declared", name);
        kest_diags_note(program->diags, existing->source, existing->span,
                        "the first one");
        return true;
    }

    if (program->global_count == program->global_capacity) {
        void *moved =
            grow(program->arena, program->globals, program->global_count,
                 &program->global_capacity, sizeof(KestSymbol));
        if (moved == NULL) {
            return false;
        }
        program->globals = moved;
    }
    // Before the list grows rather than after, because what this rebuilds is
    // read out of the list as it is: a place is put in it below, and it is put
    // in the index there too.
    if (!index_room(program) || !place_room(program)) {
        return false;
    }

    KestSymbol *symbol = &program->globals[program->global_count++];
    symbol->name = name;
    symbol->type = type;
    symbol->span = span;
    symbol->source = program->source;
    symbol->is_const = is_const;
    symbol->value = value;
    symbol->decl = decl;
    index_put(program, program->global_count - 1);
    place_put(program, program->global_count - 1);
    index_agrees(program, "a declaration");
    return true;
}

// Structs are registered before any field is resolved, so two of them may name
// each other and a field may name the type it belongs to.
static bool declare_structs(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_STRUCT) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        if (name == NULL) {
            return false;
        }
        KestType *existing = kest_find_type(program, name, strlen(name));
        if (existing != NULL) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0304",
                           decl->name, "`%s` is already declared", name);
            kest_diags_note(program->diags, existing->declared_in,
                            existing->span, "the first one");
            continue;
        }
        KestType *type = new_type(program, KEST_T_STRUCT);
        if (type == NULL || !register_type(program, type, name)) {
            return false;
        }
        type->span = decl->name;
        type->declared_in = program->source;
        // Which file declared it, which is where the module a field marked
        // `own` belongs to comes from. It used to be set for generic shapes
        // alone, because a copy is made from where the shape was written and
        // nothing else asked. See D1041.
        type->unit = program->unit;

        // A generic struct is not a type but the shape of one. `Pair<i32>` is
        // a type; `Pair` on its own has no size and is never measured.
        if (decl->type_param_count > 0) {
            type->type_param_count = decl->type_param_count;
            type->type_param_names = KEST_ARENA_ARRAY(
                program->arena, const char *, decl->type_param_count);
            if (type->type_param_names == NULL) {
                return false;
            }
            for (uint32_t g = 0; g < decl->type_param_count; g++) {
                type->type_param_names[g] =
                    span_string(program, decl->type_params[g].name);
                if (type->type_param_names[g] == NULL) {
                    return false;
                }
            }
            type->decl = decl;
        }
    }
    return true;
}

static bool resolve_struct_fields(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_STRUCT) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        // A name nothing could be made for is a host with nothing left, and
        // looking a type up by a name that is not there reads nothing at all.
        if (name == NULL) {
            return false;
        }
        KestType *type = kest_find_type(program, name, strlen(name));
        if (type == NULL || type->members != NULL) {
            continue;
        }
        // A shape's fields are resolved with its names standing for
        // themselves, so a use can put what it was given beside them and see
        // what each one has to be. It is never measured; a copy is.
        uint32_t generics = type->type_param_count;
        const char **names = KEST_ARENA_ARRAY(program->arena, const char *,
                                              generics);
        KestType **stands =
            KEST_ARENA_ARRAY(program->arena, KestType *, generics);
        if (names == NULL || stands == NULL) {
            return false;
        }
        for (uint32_t g = 0; g < generics; g++) {
            names[g] = type->type_param_names[g];
            stands[g] = new_type(program, KEST_T_PARAM);
            if (stands[g] == NULL) {
                return false;
            }
            stands[g]->name = names[g];
            stands[g]->slots = 1;
            stands[g]->byte_size = 8;
            stands[g]->byte_align = 8;
        }
        kest_bind_types(program, names, stands, generics);

        uint32_t count = decl->record.field_count;
        KestMember *members = KEST_ARENA_ARRAY(program->arena, KestMember,
                                               count == 0 ? 1 : count);
        if (members == NULL) {
            return false;
        }

        uint32_t used = 0;
        for (uint32_t f = 0; f < count; f++) {
            const KestField *field = decl->record.fields[f];
            const char *field_name = span_string(program, field->name);
            if (field_name == NULL) {
                return false;
            }

            bool duplicate = false;
            for (uint32_t seen = 0; seen < used; seen++) {
                if (strcmp(members[seen].name, field_name) == 0) {
                    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0303",
                                   field->name,
                                   "field `%s` is declared twice in `%s`",
                                   field_name, name);
                    kest_diags_note(program->diags, NULL, members[seen].span,
                                    "the first one");
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            members[used].name = field_name;
            members[used].type = kest_resolve_type_ref(program, field->type);
            members[used].span = field->name;
            members[used].own = field->own.length != 0;
            used++;
        }

        type->members = members;
        type->member_count = used;
        kest_unbind_types(program);
    }
    return true;
}

// A value's size is what it holds, so a thing that holds itself has none.
// Structs and enums are measured together because either may hold the other,
// and both are broken by a `ref`, which is one word whatever it points at.
static const char *span_string(KestProgram *program, KestSpan span);
static bool register_type(KestProgram *program, KestType *type,
                          const char *name);
static KestType *new_type(KestProgram *program, KestTypeTag tag);
static KestType *error_type(KestProgram *program);

// A value is at most 65535 bytes and as many slots, because that is what a
// layout says one is and a value is a frame's worth rather than a heap's. The
// sentence is here and the place it is said about is the caller's, which is
// either where the count was written or the struct that came out too big.
static bool sized_within(KestProgram *program, uint32_t bytes, uint32_t slots,
                         const KestSource *where, KestSpan span,
                         const char *what) {
    if (bytes <= UINT16_MAX && slots <= UINT16_MAX) {
        return true;
    }
    kest_diags_in(program->diags, where);
    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0327", span,
                   "`%s` is %u bytes, and a value is at most %u", what, bytes,
                   UINT16_MAX);
    kest_diags_suggest(program->diags,
                       "`[T]` holds that many on the heap and is a handle");
    return false;
}

static bool measure_held(KestProgram *program, KestType *type,
                         const KestType *whole) {
    if (type == NULL) {
        return true;
    }
    // That many of something is sized from what it holds, and it is composed
    // while fields are resolved — before the structs among them are measured.
    // So it is sized here, where what it holds has just been.
    if (type->tag == KEST_T_FIXED) {
        if (!measure_held(program, type->element, whole)) {
            return false;
        }
        const KestType *element = type->element;
        uint32_t one = element == NULL || element->slots == 0 ? 1
                                                              : element->slots;
        uint32_t bytes = element == NULL || element->byte_size == 0
                             ? 8
                             : element->byte_size;
        type->byte_align = element == NULL || element->byte_align == 0
                               ? 8
                               : element->byte_align;
        if (!sized_within(program, bytes * type->count, one * type->count,
                          whole == NULL ? NULL : whole->declared_in,
                          whole == NULL ? type->span : whole->span,
                          kest_type_name(program->arena, type))) {
            type->slots = 1;
            type->byte_size = 8;
            type->byte_align = 8;
            return true;
        }
        type->slots = (uint16_t)(one * type->count);
        type->byte_size = (uint16_t)(bytes * type->count);
        return true;
    }
    if (type->tag != KEST_T_STRUCT && type->tag != KEST_T_ENUM) {
        return true;
    }
    return measure(program, type);
}

static bool refuse_cycle(KestProgram *program, KestType *type) {
    kest_diags_in(program->diags, type->declared_in);
    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0319", type->span,
                   "`%s` contains itself, so it has no size", type->name);
    kest_diags_suggest(program->diags,
                       "hold it through `ref<%s>`, which is a handle",
                       type->name);
    // One word, so the rest of the file is still checkable against a type
    // that has a size even though it is the wrong one.
    type->slots = 1;
    type->byte_size = 8;
    type->byte_align = 8;
    type->sizing = false;
    return false;
}

static bool measure_struct(KestProgram *program, KestType *type) {
    uint16_t offset = 0;
    uint16_t bytes = 0;
    uint16_t align = 1;

    for (uint32_t i = 0; i < type->member_count; i++) {
        KestType *member = type->members[i].type;
        if (!measure_held(program, member, type)) {
            return refuse_cycle(program, type);
        }
        type->members[i].offset = offset;
        offset = (uint16_t)(offset + (member == NULL ? 1 : member->slots));

        // The bytes are laid out the way a C compiler would, so an array of
        // these can be the array the host already has.
        uint16_t member_size = member == NULL ? 8 : member->byte_size;
        uint16_t member_align = member == NULL || member->byte_align == 0
                                    ? 8
                                    : member->byte_align;
        bytes = (uint16_t)((bytes + member_align - 1) / member_align *
                           member_align);
        type->members[i].byte_offset = bytes;
        bytes += member_size;
        if (member_align > align) {
            align = member_align;
        }
    }

    type->slots = offset == 0 ? 1 : offset;
    type->byte_align = align;
    type->byte_size =
        (uint16_t)(bytes == 0 ? 1 : (bytes + align - 1) / align * align);
    return true;
}

// An enum is a tag and whichever case's payload is widest, which is what a
// tagged union is and why every case can be read for its tag alone.
static bool measure_enum(KestProgram *program, KestType *type) {
    uint16_t payload_slots = 0;
    uint16_t payload_bytes = 0;
    uint16_t align = 4;

    for (uint32_t c = 0; c < type->case_count; c++) {
        KestVariantType *variant = &type->cases[c];
        uint16_t slots = 0;
        uint16_t bytes = 0;
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            KestType *held = variant->payload[p];
            if (!measure_held(program, held, type)) {
                return refuse_cycle(program, type);
            }
            uint16_t held_align = held == NULL || held->byte_align == 0
                                      ? 8
                                      : held->byte_align;
            if (held_align > align) {
                align = held_align;
            }
            variant->offsets[p] = slots;
            slots = (uint16_t)(slots + (held == NULL ? 1 : held->slots));
            bytes = (uint16_t)((bytes + held_align - 1) / held_align *
                               held_align);
            variant->byte_offsets[p] = bytes;
            bytes = (uint16_t)(bytes + (held == NULL ? 8 : held->byte_size));
        }
        if (slots > payload_slots) {
            payload_slots = slots;
        }
        if (bytes > payload_bytes) {
            payload_bytes = bytes;
        }
    }

    // The tag is a four byte integer, so the payload starts wherever its own
    // alignment puts it after that.
    uint16_t start = (uint16_t)((4 + align - 1) / align * align);
    for (uint32_t c = 0; c < type->case_count; c++) {
        for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
            type->cases[c].offsets[p] =
                (uint16_t)(type->cases[c].offsets[p] + 1);
            type->cases[c].byte_offsets[p] =
                (uint16_t)(type->cases[c].byte_offsets[p] + start);
        }
    }

    type->slots = (uint16_t)(payload_slots + 1);
    type->byte_align = align;
    uint16_t total = (uint16_t)(start + payload_bytes);
    type->byte_size = (uint16_t)((total + align - 1) / align * align);
    return true;
}

static bool measure(KestProgram *program, KestType *type) {
    if (type->slots > 0) {
        return true;
    }
    if (type->sizing) {
        return false;
    }
    type->sizing = true;
    bool ok = type->tag == KEST_T_ENUM ? measure_enum(program, type)
                                       : measure_struct(program, type);
    type->sizing = false;
    return ok;
}

static bool measure_all(KestProgram *program) {
    for (uint32_t i = 0; i < program->type_count; i++) {
        KestType *type = program->types[i];
        if (type->type_param_count > 0) {
            continue;
        }
        if (type->tag == KEST_T_STRUCT || type->tag == KEST_T_ENUM) {
            const KestSource *was = program->source;
            measure(program, type);
            kest_diags_in(program->diags, was);
        }
    }
    return true;
}

// One walk for the two shapes that are a name and a list of cases. An enum and
// a set of flags are declared the same way and differ in the tag each carries,
// and it was written out twice until a check read the two bodies as one shape.
// See D770.
static bool declare_cased(KestProgram *program, const KestUnit *unit,
                          KestDeclKind wanted, KestTypeTag tag) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != wanted) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        if (name == NULL) {
            return false;
        }
        KestType *existing = kest_find_type(program, name, strlen(name));
        if (existing != NULL) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0304",
                           decl->name, "`%s` is already declared", name);
            kest_diags_note(program->diags, existing->declared_in,
                            existing->span, "the first one");
            continue;
        }
        KestType *type = new_type(program, tag);
        if (type == NULL || !register_type(program, type, name)) {
            return false;
        }
        type->span = decl->name;
        type->declared_in = program->source;
        type->unit = program->unit;

        // An enum that takes types is not a type but the shape of one, the
        // same as a struct that does: `Answer<i32>` is a type and `Answer` on
        // its own has no size and is never measured. A `flags` set takes none
        // -- it is a width and a list of bits -- and the parser never reads
        // any for one. See D1048.
        if (decl->type_param_count > 0) {
            type->type_param_count = decl->type_param_count;
            type->type_param_names = KEST_ARENA_ARRAY(
                program->arena, const char *, decl->type_param_count);
            if (type->type_param_names == NULL) {
                return false;
            }
            for (uint32_t g = 0; g < decl->type_param_count; g++) {
                type->type_param_names[g] =
                    span_string(program, decl->type_params[g].name);
                if (type->type_param_names[g] == NULL) {
                    return false;
                }
            }
            type->decl = decl;
        }
    }
    return true;
}

static bool resolve_flags_cases(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FLAGS) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        // A name nothing could be made for is a host with nothing left, and
        // looking a type up by a name that is not there reads nothing at all.
        if (name == NULL) {
            return false;
        }
        KestType *type = kest_find_type(program, name, strlen(name));
        if (type == NULL || type->cases != NULL) {
            continue;
        }

        KestType *over = kest_resolve_type_ref(program, decl->choice.width);
        if (over == NULL || over->tag == KEST_T_ERROR) {
            continue;
        }
        if (over->tag != KEST_T_INT || over->is_signed) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0337",
                           decl->choice.width->span,
                           "flags sit over an unsigned integer, found `%s`",
                           kest_type_name(program->arena, over));
            kest_diags_suggest(program->diags,
                               "`u8`, `u16`, `u32` or `u64` says how many "
                               "bits the host sees");
            continue;
        }
        // One slot however wide it is declared, because every value of every
        // width fits in one. The byte layout is the declared width, which is
        // what a host reading the same memory sees (D016).
        type->width = over->width;
        type->is_signed = false;
        type->slots = 1;
        type->byte_size = (uint16_t)(over->width / 8);
        type->byte_align = type->byte_size;

        uint32_t count = decl->choice.case_count;
        KestVariantType *cases = KEST_ARENA_ARRAY(program->arena,
                                                  KestVariantType,
                                                  count == 0 ? 1 : count);
        if (cases == NULL) {
            return false;
        }

        uint32_t used = 0;
        for (uint32_t c = 0; c < count; c++) {
            const KestVariant *written = decl->choice.cases[c];
            const char *case_name = span_string(program, written->name);
            if (case_name == NULL) {
                return false;
            }
            bool duplicate = false;
            for (uint32_t seen = 0; seen < used; seen++) {
                if (strcmp(cases[seen].name, case_name) == 0) {
                    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0303",
                                   written->name,
                                   "flag `%s` is declared twice in `%s`",
                                   case_name, name);
                    kest_diags_note(program->diags, NULL, cases[seen].span,
                                    "the first one");
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
            if (used >= over->width) {
                kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0338",
                               written->name,
                               "`%s` is flag %u, and a `%s` holds %u",
                               case_name, used + 1,
                               kest_type_name(program->arena, over),
                               over->width);
                kest_diags_suggest(program->diags,
                                   "widen the type the flags sit over");
                continue;
            }
            cases[used].name = case_name;
            cases[used].span = written->name;
            cases[used].payload_count = 0;
            cases[used].payload =
                KEST_ARENA_ARRAY(program->arena, KestType *, 1);
            cases[used].offsets = KEST_ARENA_ARRAY(program->arena, uint16_t, 1);
            cases[used].byte_offsets =
                KEST_ARENA_ARRAY(program->arena, uint16_t, 1);
            if (cases[used].payload == NULL || cases[used].offsets == NULL ||
                cases[used].byte_offsets == NULL) {
                return false;
            }
            used++;
        }
        type->cases = cases;
        type->case_count = used;
    }
    return true;
}

static bool resolve_enum_cases(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_ENUM) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        // A name nothing could be made for is a host with nothing left, and
        // looking a type up by a name that is not there reads nothing at all.
        if (name == NULL) {
            return false;
        }
        KestType *type = kest_find_type(program, name, strlen(name));
        if (type == NULL || type->cases != NULL) {
            continue;
        }

        // A shape's cases are resolved with its names standing for
        // themselves, the same way a struct's fields are: what a case carries
        // is a `T` until a copy says what `T` is. It is never measured; a copy
        // is. See D1048.
        uint32_t generics = type->type_param_count;
        const char **stand_names = KEST_ARENA_ARRAY(program->arena,
                                                    const char *, generics);
        KestType **stands =
            KEST_ARENA_ARRAY(program->arena, KestType *, generics);
        if (stand_names == NULL || stands == NULL) {
            return false;
        }
        for (uint32_t g = 0; g < generics; g++) {
            stand_names[g] = type->type_param_names[g];
            stands[g] = new_type(program, KEST_T_PARAM);
            if (stands[g] == NULL) {
                return false;
            }
            stands[g]->name = stand_names[g];
            stands[g]->slots = 1;
            stands[g]->byte_size = 8;
            stands[g]->byte_align = 8;
        }
        kest_bind_types(program, stand_names, stands, generics);

        uint32_t count = decl->choice.case_count;
        KestVariantType *cases = KEST_ARENA_ARRAY(program->arena,
                                                  KestVariantType,
                                                  count == 0 ? 1 : count);
        if (cases == NULL) {
            return false;
        }

        uint32_t used = 0;
        for (uint32_t c = 0; c < count; c++) {
            const KestVariant *written = decl->choice.cases[c];
            const char *case_name = span_string(program, written->name);
            if (case_name == NULL) {
                return false;
            }
            bool duplicate = false;
            for (uint32_t seen = 0; seen < used; seen++) {
                if (strcmp(cases[seen].name, case_name) == 0) {
                    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0303",
                                   written->name,
                                   "case `%s` is declared twice in `%s`",
                                   case_name, name);
                    kest_diags_note(program->diags, NULL, cases[seen].span,
                                    "the first one");
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            uint32_t held = written->payload_count;
            cases[used].name = case_name;
            cases[used].span = written->name;
            cases[used].payload_count = held;
            cases[used].payload =
                KEST_ARENA_ARRAY(program->arena, KestType *, held == 0 ? 1 : held);
            cases[used].offsets =
                KEST_ARENA_ARRAY(program->arena, uint16_t, held == 0 ? 1 : held);
            cases[used].byte_offsets =
                KEST_ARENA_ARRAY(program->arena, uint16_t, held == 0 ? 1 : held);
            if (cases[used].payload == NULL || cases[used].offsets == NULL ||
                cases[used].byte_offsets == NULL) {
                return false;
            }
            for (uint32_t p = 0; p < held; p++) {
                cases[used].payload[p] =
                    kest_resolve_type_ref(program, written->payload[p]);
            }
            used++;
        }
        type->cases = cases;
        type->case_count = used;
        kest_unbind_types(program);
    }
    return true;
}

// The same type with every type name replaced by what it stands for. A type
// that mentions none is itself, so nothing is rebuilt for the common case.
bool kest_mentions_name(const KestType *type);

static bool mentions_param(const KestType *type) {
    if (type == NULL) {
        return false;
    }
    if (type->tag == KEST_T_PARAM) {
        return true;
    }
    if (mentions_param(type->element) || mentions_param(type->result)) {
        return true;
    }
    for (uint32_t i = 0; i < type->param_count; i++) {
        if (mentions_param(type->params[i])) {
            return true;
        }
    }
    // A copy of a generic struct is asked about what it was made with, not
    // about its fields: a struct that holds a reference to itself would have
    // no end.
    for (uint32_t i = 0; i < type->type_arg_count; i++) {
        if (mentions_param(type->type_args[i])) {
            return true;
        }
    }
    return false;
}

bool kest_mentions_name(const KestType *type) {
    return mentions_param(type);
}

KestType *kest_substitute(KestProgram *program, KestType *type,
                          const char **names, KestType **bindings,
                          uint32_t count) {
    if (type == NULL || !mentions_param(type)) {
        return type;
    }
    if (type->tag == KEST_T_PARAM) {
        for (uint32_t i = 0; i < count; i++) {
            if (strcmp(names[i], type->name) == 0) {
                return bindings[i];
            }
        }
        return type;
    }
    switch (type->tag) {
    case KEST_T_ARRAY:
        return kest_array_of(
            program,
            kest_substitute(program, type->element, names, bindings, count));
    case KEST_T_OPTIONAL:
        return kest_optional_of(
            program,
            kest_substitute(program, type->element, names, bindings, count));
    // And the same run, made again with what its element turned out to be.
    // How many there are is the type's and not the copy's. See D546.
    case KEST_T_FIXED:
        return kest_fixed_of(
            program,
            kest_substitute(program, type->element, names, bindings, count),
            type->count);
    case KEST_T_REF:
        return kest_ref_of(
            program,
            kest_substitute(program, type->element, names, bindings, count));
    case KEST_T_STORE:
        return compose(
            program, KEST_T_STORE,
            kest_substitute(program, type->element, names, bindings, count), 0);
    case KEST_T_FN: {
        KestType *params[16];
        uint32_t used = type->param_count < 16 ? type->param_count : 16;
        for (uint32_t i = 0; i < used; i++) {
            params[i] = kest_substitute(program, type->params[i], names,
                                        bindings, count);
        }
        return fn_of(
            program, params, used,
            kest_substitute(program, type->result, names, bindings, count),
            type->no_alloc, type->no_host,
            type->deterministic);
    }
    // A copy of a shape, made again with what its types turned out to be. An
    // enum that takes types is a shape like a struct that does, so the two go
    // through the same door; one that takes none has no `shape` and is itself.
    // See D546 and D1048.
    case KEST_T_STRUCT:
    case KEST_T_ENUM: {
        if (type->shape == NULL) {
            return type;
        }
        uint32_t used = type->type_arg_count;
        KestType **args = KEST_ARENA_ARRAY(program->arena, KestType *,
                                           used == 0 ? 1 : used);
        if (args == NULL) {
            return type;
        }
        for (uint32_t i = 0; i < used; i++) {
            args[i] = kest_substitute(program, type->type_args[i], names,
                                      bindings, count);
        }
        return kest_struct_of(program, type->shape, args, used);
    }
    // Everything left stands for itself: a primitive, a set of bits and a type
    // name already bound are what they were, and a type this is asked about
    // with nothing to put in it is too. Written out for the reason the three
    // beside it are — a composed tag added to the language would come back
    // unsubstituted, which is a copy of a generic made with the name still in
    // it. See D546.
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_BOOL:
    case KEST_T_INT:
    case KEST_T_FLOAT:
    case KEST_T_TEXT:
    case KEST_T_FLAGS:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        return type;
    }
    return type;
}

// Works out what a type name has to stand for by putting the declared type
// beside the one that was passed. Anything that does not mention a name is
// checked later, against the instance, where a mismatch reports properly.
bool kest_unify(const KestType *declared, const KestType *given,
                const char **names, KestType **bindings, uint32_t count) {
    if (declared == NULL || given == NULL) {
        return true;
    }
    if (declared->tag == KEST_T_PARAM) {
        for (uint32_t i = 0; i < count; i++) {
            if (strcmp(names[i], declared->name) != 0) {
                continue;
            }
            if (bindings[i] == NULL) {
                bindings[i] = (KestType *)given;
                return true;
            }
            return kest_type_equal(given, bindings[i]);
        }
        return true;
    }
    if (declared->tag != given->tag) {
        return true;
    }
    // A copy of a shape against a copy of the same shape: what each was made
    // with, one at a time. An enum that takes types is a shape like a struct
    // that does. See D1048.
    if (declared->tag == KEST_T_STRUCT || declared->tag == KEST_T_ENUM) {
        if (declared->shape == NULL || declared->shape != given->shape) {
            return true;
        }
        for (uint32_t i = 0; i < declared->type_arg_count &&
                             i < given->type_arg_count;
             i++) {
            if (!kest_unify(declared->type_args[i], given->type_args[i], names,
                            bindings, count)) {
                return false;
            }
        }
        return true;
    }
    switch (declared->tag) {
    // A run of a written length is one of these: `[T; 3]` says what `T` is as
    // plainly as `[T]` does, and leaving it out of this list left a generic
    // over one uncallable — `what T is here cannot be told from what was
    // passed`, about an argument it was written in. See D546.
    case KEST_T_ARRAY:
    case KEST_T_FIXED:
    case KEST_T_OPTIONAL:
    case KEST_T_REF:
    case KEST_T_STORE:
        return kest_unify(declared->element, given->element, names, bindings,
                          count);
    case KEST_T_FN: {
        if (declared->param_count != given->param_count) {
            return true;
        }
        for (uint32_t i = 0; i < declared->param_count; i++) {
            if (!kest_unify(declared->params[i], given->params[i], names,
                            bindings, count)) {
                return false;
            }
        }
        return kest_unify(declared->result, given->result, names, bindings,
                          count);
    }
    // Everything left mentions no name, so there is nothing here to work out:
    // what a primitive or a declared type says about `T` is nothing, and a
    // mismatch between the declaration and what was passed is reported against
    // the instance rather than here. Written out for the same reason. See
    // D546.
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_BOOL:
    case KEST_T_INT:
    case KEST_T_FLOAT:
    case KEST_T_TEXT:
    case KEST_T_ENUM:
    case KEST_T_FLAGS:
    case KEST_T_STRUCT:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        return true;
    }
    return true;
}

// Binds the names a generic declaration brought into scope. Anything resolved
// while they are bound sees them; nothing else does.
// What a type name stands for where it is written. A copy of a generic struct
// is found by its name, and `Box<K, V>` asked for by two generics in one
// module is one copy -- so the `K` inside it is whichever of them asked first,
// and what that one said about `K` is not what this one says. The name is what
// is meant; which name is bound here is the question. See D1043.
static KestType *stands_here(const KestProgram *program,
                             const KestType *type) {
    if (type == NULL || type->tag != KEST_T_PARAM || type->name == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < program->bound_count; i++) {
        if (program->bound_types[i] != NULL &&
            program->bound_types[i]->tag == KEST_T_PARAM &&
            strcmp(program->bound_names[i], type->name) == 0) {
            return program->bound_types[i];
        }
    }
    return (KestType *)type;
}

bool kest_wants(KestProgram *program, const KestType *type,
                KestCapability bit) {
    KestType *stands = stands_here(program, type);
    if (stands == NULL || (stands->wants & bit) == 0) {
        return false;
    }
    stands->took |= (uint8_t)bit;
    return true;
}

void kest_bind_types(KestProgram *program, const char **names,
                     KestType **types, uint32_t count) {
    if (count > program->bound_capacity) {
        const char **grown_names =
            KEST_ARENA_ARRAY(program->arena, const char *, count);
        KestType **grown_types =
            KEST_ARENA_ARRAY(program->arena, KestType *, count);
        if (grown_names == NULL || grown_types == NULL) {
            program->bound_count = 0;
            return;
        }
        program->bound_names = grown_names;
        program->bound_types = grown_types;
        program->bound_capacity = count;
    }
    program->bound_count = count;
    for (uint32_t i = 0; i < count; i++) {
        program->bound_names[i] = names[i];
        program->bound_types[i] = types[i];
    }
}

void kest_unbind_types(KestProgram *program) {
    program->bound_count = 0;
}

// What a copy is of and what it was given, as a number. A type is read for its
// kind, its width and its name rather than for its address, because two types
// that are the same are not always one object -- so two sets that are equal
// land on one slot and are compared there the way they always were. See D1088.
static uint32_t binding_hash(const KestType *type) {
    if (type == NULL) {
        return 2166136261u;
    }
    uint32_t hash = 2166136261u ^ (uint32_t)type->tag;
    hash = hash * 16777619u + type->width;
    hash = hash * 16777619u + (type->is_signed ? 1u : 0u);
    hash = hash * 16777619u + type->count;
    if (type->name != NULL) {
        hash = hash * 16777619u + kest_name_hash(type->name, strlen(type->name));
    }
    // One step into what it holds, which is what tells `[i32]` from `[text]`.
    // Not all the way down: a hash is a bucket and the comparison below is
    // what decides.
    if (type->element != NULL) {
        hash = hash * 16777619u + (uint32_t)type->element->tag;
        hash = hash * 16777619u + type->element->width;
        if (type->element->name != NULL) {
            hash = hash * 16777619u +
                   kest_name_hash(type->element->name,
                                  strlen(type->element->name));
        }
    }
    return hash;
}

static uint32_t use_hash(const KestDecl *decl, KestType **bindings,
                         uint32_t count) {
    uint64_t mixed = (uint64_t)(uintptr_t)decl * 1099511628211u;
    mixed ^= (uint64_t)count * 2654435761u;
    for (uint32_t i = 0; i < count; i++) {
        mixed = mixed * 31u + binding_hash(bindings[i]);
    }
    return (uint32_t)(mixed ^ (mixed >> 32));
}

static bool same_use(const KestInstance *held, const KestDecl *decl,
                     KestType **bindings, uint32_t count) {
    if (held->decl != decl || held->count != count) {
        return false;
    }
    for (uint32_t b = 0; b < count; b++) {
        if (!kest_type_equal(held->bindings[b], bindings[b]) ||
            !kest_type_equal(bindings[b], held->bindings[b])) {
            return false;
        }
    }
    return true;
}

static uint32_t use_slot(const KestProgram *program, const KestDecl *decl,
                         KestType **bindings, uint32_t count) {
    uint32_t mask = program->instances_by_use_slots - 1;
    uint32_t slot = use_hash(decl, bindings, count) & mask;
    while (program->instances_by_use[slot] != 0) {
        const KestInstance *held =
            &program->instances[program->instances_by_use[slot] - 1u];
        if (same_use(held, decl, bindings, count)) {
            return slot;
        }
        slot = (slot + 1u) & mask;
    }
    return slot;
}

static bool use_room(KestProgram *program) {
    if (program->instances_by_use_slots >= (program->instance_count + 1) * 2) {
        return true;
    }
    uint32_t slots = program->instances_by_use_slots == 0
                         ? 64
                         : program->instances_by_use_slots * 2;
    uint32_t *made = KEST_ARENA_ARRAY(program->arena, uint32_t, slots);
    if (made == NULL) {
        return false;
    }
    program->instances_by_use = made;
    program->instances_by_use_slots = slots;
    for (uint32_t i = 0; i < program->instance_count; i++) {
        const KestInstance *one = &program->instances[i];
        uint32_t slot =
            use_slot(program, one->decl, one->bindings, one->count);
        program->instances_by_use[slot] = i + 1u;
    }
    return true;
}

// One copy per set of types. Asking twice for the same set gives the one that
// is already there, so a call in a loop compiles one body.
KestInstance *kest_instance_of(KestProgram *program, const KestDecl *decl,
                               const KestUnitInfo *unit, const char **names,
                               KestType **bindings, uint32_t count) {
    if (!use_room(program)) {
        return NULL;
    }
    uint32_t slot = use_slot(program, decl, bindings, count);
    if (program->instances_by_use[slot] != 0) {
        return &program->instances[program->instances_by_use[slot] - 1u];
    }
    if (program->instance_count == program->instance_capacity) {
        uint32_t capacity = program->instance_capacity == 0
                                ? 8
                                : program->instance_capacity * 2;
        KestInstance *grown =
            KEST_ARENA_ARRAY(program->arena, KestInstance, capacity);
        if (grown == NULL) {
            return NULL;
        }
        if (program->instance_count > 0) {
            memcpy(grown, program->instances,
                   (size_t)program->instance_count * sizeof(KestInstance));
        }
        program->instances = grown;
        program->instance_capacity = capacity;
    }
    KestInstance *made = &program->instances[program->instance_count++];
    memset(made, 0, sizeof *made);
    made->decl = decl;
    made->unit = unit;
    made->count = count;
    made->names = KEST_ARENA_ARRAY(program->arena, const char *,
                                   count == 0 ? 1 : count);
    made->bindings = KEST_ARENA_ARRAY(program->arena, KestType *,
                                      count == 0 ? 1 : count);
    if (made->names == NULL || made->bindings == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < count; i++) {
        made->names[i] = names[i];
        made->bindings[i] = bindings[i];
    }
    // Put in where the lookup above looked, now that the copy holds the types
    // it was given: the slot is found again rather than kept, because the
    // table may have been made bigger in between.
    program->instances_by_use[use_slot(program, decl, made->bindings, count)] =
        program->instance_count;
    return made;
}

static bool declare_functions(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FN) {
            continue;
        }

        KestType *type = new_type(program, KEST_T_FN);
        if (type == NULL) {
            return false;
        }

        // The signature of a generic function mentions names that stand for
        // themselves until a call says what they are.
        uint32_t generics = decl->type_param_count;
        const char **names =
            KEST_ARENA_ARRAY(program->arena, const char *,
                             generics == 0 ? 1 : generics);
        KestType **stands = KEST_ARENA_ARRAY(program->arena, KestType *,
                                             generics == 0 ? 1 : generics);
        if (names == NULL || stands == NULL) {
            return false;
        }
        for (uint32_t g = 0; g < generics; g++) {
            names[g] = span_string(program, decl->type_params[g].name);
            stands[g] = new_type(program, KEST_T_PARAM);
            if (names[g] == NULL || stands[g] == NULL) {
                return false;
            }
            stands[g]->name = names[g];
            stands[g]->slots = 1;
            stands[g]->byte_size = 8;
            stands[g]->byte_align = 8;
            stands[g]->wants = decl->type_params[g].wants;
        }
        kest_bind_types(program, names, stands, generics);
        type->type_param_count = generics;
        if (generics > 0) {
            type->type_param_names =
                KEST_ARENA_ARRAY(program->arena, const char *, generics);
            uint8_t *wants = KEST_ARENA_ARRAY(program->arena, uint8_t, generics);
            if (type->type_param_names == NULL || wants == NULL) {
                return false;
            }
            for (uint32_t g = 0; g < generics; g++) {
                type->type_param_names[g] = names[g];
                wants[g] = stands[g]->wants;
            }
            type->type_param_wants = wants;
            type->type_param_stands = stands;
            type->decl = decl;
            type->unit = program->unit;
        }

        uint32_t count = decl->function.param_count;
        type->params =
            KEST_ARENA_ARRAY(program->arena, KestType *, count == 0 ? 1 : count);
        if (type->params == NULL) {
            return false;
        }
        type->param_count = count;

        for (uint32_t p = 0; p < count; p++) {
            const KestField *param = decl->function.params[p];
            type->params[p] = kest_resolve_type_ref(program, param->type);

            const char *param_name = span_string(program, param->name);
            for (uint32_t seen = 0; seen < p; seen++) {
                const KestField *earlier = decl->function.params[seen];
                // Through the one door that measures the name it was given
                // rather than trusting the span beside it. A name the arena
                // had no room for comes back empty, and comparing that many
                // bytes of an empty name reads past the end of it -- which a
                // run that refused the allocation under this found, and which
                // was there before anything asked. See D510.
                if (kest_word_same(param_name,
                                   kest_span_text(program->source,
                                                  earlier->name),
                                   earlier->name.length)) {
                    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0305",
                                   param->name,
                                   "parameter `%s` is declared twice",
                                   param_name);
                    kest_diags_note(program->diags, NULL, earlier->name,
                                    "the first one");
                    break;
                }
            }
        }

        type->result = decl->function.result == NULL
                           ? kest_find_type(program, "void", 4)
                           : kest_resolve_type_ref(program, decl->function.result);
        type->no_alloc = decl->function.no_alloc;
        type->no_host = decl->function.no_host;
        type->deterministic = decl->function.deterministic;
        type->is_foreign = decl->function.is_extern;

        // An extern function with a receiver is named for the host type it
        // belongs to, so `Clock.now` and `Timer.now` can both exist.
        KestSpan span = decl->name;
        const char *name;
        KestSpan bare = decl->name;
        if (decl->function.receiver.length > 0) {
            bare.offset = decl->function.receiver.offset;
            bare.length = decl->name.offset + decl->name.length -
                          decl->function.receiver.offset;
        }
        type->foreign_name = span_string(program, bare);
        if (decl->function.receiver.length > 0) {
            KestSpan whole = {decl->function.receiver.offset,
                              decl->name.offset + decl->name.length -
                                  decl->function.receiver.offset};
            span = whole;
            name = qualified(program, whole);
        } else {
            name = qualified(program, decl->name);
        }
        if (name == NULL) {
            return false;
        }
        type->symbol = symbol_of(program, name, type);
        kest_unbind_types(program);
        if (!add_global(program, name, type, span, true, decl)) {
            return false;
        }
    }
    return true;
}

static bool declare_constants(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_CONST) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        KestType *type = kest_resolve_type_ref(program, decl->constant.type);
        // What it is written as, kept so that working it out is possible
        // wherever it is used and wherever a count asks for it.
        if (name == NULL || !add_global_value(program, name, type, decl->name,
                                              true, decl->constant.value,
                                              NULL)) {
            return false;
        }
    }
    return true;
}

const char *kest_nearest_global(KestProgram *program, const char *name,
                                size_t length) {
    if (length < 3) {
        return NULL;
    }
    uint32_t limit = length == 3 ? 1 : (uint32_t)length / 3;
    const char *best = NULL;
    uint32_t best_distance = limit + 1;

    for (uint32_t i = 0; i < program->global_count; i++) {
        const char *candidate = program->globals[i].name;
        uint32_t distance =
            kest_word_distance(name, length, candidate, strlen(candidate), limit);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }
    return best;
}

const char *kest_nearest_member(const KestType *type, const char *name,
                                size_t length) {
    if (length < 3) {
        return NULL;
    }
    uint32_t limit = length == 3 ? 1 : (uint32_t)length / 3;
    const char *best = NULL;
    uint32_t best_distance = limit + 1;

    for (uint32_t i = 0; i < type->member_count; i++) {
        const char *candidate = type->members[i].name;
        uint32_t distance =
            kest_word_distance(name, length, candidate, strlen(candidate), limit);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }
    return best;
}

bool kest_type_equal(const KestType *a, const KestType *b) {
    if (a == NULL || b == NULL) {
        return true;
    }
    if (a == b) {
        return true;
    }
    if (a->tag == KEST_T_ERROR || b->tag == KEST_T_ERROR) {
        return true;
    }
    if (a->tag != b->tag) {
        return false;
    }
    switch (a->tag) {
    case KEST_T_INT:
        return a->width == b->width && a->is_signed == b->is_signed;
    case KEST_T_FLOAT:
        return a->width == b->width;
    case KEST_T_ARRAY:
    case KEST_T_REF:
    case KEST_T_STORE:
    case KEST_T_OPTIONAL:
        return kest_type_equal(a->element, b->element);
    case KEST_T_FIXED:
        return a->count == b->count && kest_type_equal(a->element, b->element);
    case KEST_T_FN: {
        // Called as (given, wanted): a value that promises more fits where
        // less is asked for.
        if (a->param_count != b->param_count ||
            !kest_type_equal(a->result, b->result)) {
            return false;
        }
        for (uint32_t i = 0; i < a->param_count; i++) {
            if (!kest_type_equal(a->params[i], b->params[i])) {
                return false;
            }
        }
        // A value promising more may go where one promising less is wanted,
        // and each promise is asked about on its own: one that promises the
        // heap and not the host is neither above nor below one that promises
        // the other way, and neither goes where the other is wanted. See D853.
        return (a->no_alloc || !b->no_alloc) && (a->no_host || !b->no_host) &&
               (a->deterministic || !b->deterministic);
    }
    // Everything left is a type there is one object of: a primitive is
    // registered once, a struct, an enum and a set of bits are the declaration
    // they were written at, and a name standing for a type is the one the
    // instance bound. Two of any of them that did not match by pointer above
    // are two types. Written out rather than left to a `default` so that a tag
    // added to the language has to be thought about here, where the wrong
    // answer is a program refused for nothing. See D546.
    case KEST_T_ERROR:
    // A name standing for itself, which is what a type parameter is while the
    // generic it belongs to is being checked. Two of them are one type when
    // they are the same name: a shape copied with a parameter as its argument
    // is cached under the name it was made with, so `Table<K, V>` asked for
    // twice in one module is one copy and the `K` in it is the `K` of
    // whichever asked first. Without this a body was told that `K` is not `K`.
    // See D1043.
    case KEST_T_PARAM:
        return a->name != NULL && b->name != NULL &&
               strcmp(a->name, b->name) == 0;
    case KEST_T_VOID:
    case KEST_T_BOOL:
    case KEST_T_TEXT:
    case KEST_T_ENUM:
    case KEST_T_FLAGS:
    case KEST_T_STRUCT:
    case KEST_T_MODULE:
        return false;
    }
    return false;
}

// Where a file says what it calls itself, for pointing at the line. Whether it
// is the library's is not worked out here: the loader decided that when it
// decided where to read the file from.
// Whether any one file imports both of these, which is the only way a `math.`
// written in a file could mean either of them. A program holding two modules
// under one last part is fine; a file reaching two of them is not, and the
// file is where the fix goes. See D1039.
static const KestUnitInfo *one_file_reaches_both(const KestUnits *units,
                                                const KestUnitInfo *first,
                                                const KestUnitInfo *second) {
    for (uint32_t u = 0; u < units->count; u++) {
        const KestUnitInfo *file = &units->items[u];
        bool saw_first = strcmp(file->module, first->module) == 0;
        bool saw_second = strcmp(file->module, second->module) == 0;
        for (uint32_t k = 0; k < file->import_count; k++) {
            if (strcmp(file->import_paths[k], first->module) == 0) {
                saw_first = true;
            }
            if (strcmp(file->import_paths[k], second->module) == 0) {
                saw_second = true;
            }
        }
        if (saw_first && saw_second) {
            return file;
        }
    }
    return NULL;
}

// Where a file wrote the import that named this module, so a refusal about a
// file can point at the line the reader has to change rather than at a module
// declaration somewhere else. The module's own line where the file is the
// module itself.
static KestSpan where_imported(const KestUnitInfo *file,
                               const KestUnitInfo *named) {
    for (uint32_t i = 0; i < file->unit.count; i++) {
        const KestDecl *decl = file->unit.items[i];
        if (decl->kind != KEST_DECL_IMPORT && decl->kind != KEST_DECL_MODULE) {
            continue;
        }
        const char *written = kest_span_text(&file->source, decl->name);
        if (decl->name.length == strlen(named->module) &&
            strncmp(written, named->module, decl->name.length) == 0) {
            return decl->name;
        }
    }
    KestSpan nowhere = {0, 0};
    return nowhere;
}

static KestSpan module_span(const KestUnitInfo *unit) {
    KestSpan span = {0, 1};
    for (uint32_t d = 0; d < unit->unit.count; d++) {
        if (unit->unit.items[d]->kind == KEST_DECL_MODULE) {
            span = unit->unit.items[d]->name;
        }
    }
    return span;
}

bool kest_check(KestArena *arena, KestDiags *diags, const KestUnits *units,
                bool index_names, KestProgram **out) {
    KestProgram *program = KEST_ARENA_NEW(arena, KestProgram);
    if (program == NULL) {
        return false;
    }
    program->arena = arena;
    program->diags = diags;
    program->index_names = index_names;
    program->alias = "";
    program->module = "";
    program->files = units;
    *out = program;

    // Nothing is declared for a program before it says what it imports.
    // Saying something is the host's to do and `std.io` is where it is asked
    // for; see D024.
    if (!add_primitives(program)) {
        return false;
    }

    // A name lives under the whole of its module, so `a.math.min` and
    // `b.math.min` are two entries and a program may hold both. What cannot
    // happen is one *file* writing `math.` and meaning either of them, which
    // is what the walk below looks for: two modules a file imports whose last
    // parts are the same. Under D185 this was refused for the whole program,
    // because the table was keyed by the last part alone. See D1039.
    for (uint32_t i = 0; i < units->count; i++) {
        if (units->items[i].alias[0] == '\0') {
            continue;
        }
        for (uint32_t j = 0; j < i; j++) {
            if (strcmp(units->items[i].alias, units->items[j].alias) != 0) {
                continue;
            }
            // Two modules under one last part are two modules, and a program
            // may hold both: a name lives under the whole of what its module
            // calls itself, so `render.math` and `physics.math` are two names
            // here. What cannot happen is one *file* writing `math.` and
            // meaning either, so this is a refusal about a file rather than
            // about the program. See D1039.
            const KestUnitInfo *reaching = one_file_reaches_both(
                units, &units->items[i], &units->items[j]);
            if (reaching == NULL) {
                continue;
            }
            // The one that can be changed is the one to point at. A `std`
            // module is the library's and is not the reader's to rename, so
            // when one of the two is that, the other one is where the message
            // goes and the library is the note.
            const KestUnitInfo *first = &units->items[i];
            const KestUnitInfo *second = &units->items[j];
            if (first->from_library) {
                const KestUnitInfo *held = first;
                first = second;
                second = held;
            }
            // The refusal is about the file that reaches both, because that
            // is the only place `math.` could mean either -- and it is the
            // file whose lines a reader can change. Two modules under one
            // last part are fine everywhere else. See D1039.
            KestSpan at_first = where_imported(reaching, first);
            KestSpan at_second = where_imported(reaching, second);
            kest_diags_in(diags, &reaching->source);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0328",
                           at_first.length > 0 ? at_first
                                               : module_span(first),
                           "this file reaches two modules called `%s`",
                           first->alias);
            kest_diags_suggest(diags,
                               second->from_library
                                   ? "the other one is the library's and is "
                                     "not yours to rename, so this is the one "
                                     "to call something else"
                                   : "a name written `%s.` here would be "
                                     "either of them, so one of the two has "
                                     "to be called something else",
                               first->alias);
            kest_diags_note(diags, &reaching->source,
                            at_second.length > 0 ? at_second
                                                 : module_span(second),
                            "the other one");
        }
    }

    // Every struct in every file is registered before any field is resolved,
    // so a type may name one declared in a file that has not been read yet as
    // well as one below it.
    for (uint32_t i = 0; i < units->count; i++) {
        kest_program_in(program, &units->items[i]);
        kest_diags_in(diags, program->source);
        if (!declare_structs(program, &units->items[i].unit) ||
            !declare_cased(program, &units->items[i].unit, KEST_DECL_ENUM,
                           KEST_T_ENUM) ||
            !declare_cased(program, &units->items[i].unit, KEST_DECL_FLAGS,
                           KEST_T_FLAGS)) {
            return false;
        }
    }
    for (uint32_t i = 0; i < units->count; i++) {
        kest_program_in(program, &units->items[i]);
        kest_diags_in(diags, program->source);
        if (!resolve_struct_fields(program, &units->items[i].unit) ||
            !resolve_enum_cases(program, &units->items[i].unit) ||
            !resolve_flags_cases(program, &units->items[i].unit)) {
            return false;
        }
    }
    measure_all(program);
    for (uint32_t i = 0; i < units->count; i++) {
        kest_program_in(program, &units->items[i]);
        kest_diags_in(diags, program->source);
        if (!declare_constants(program, &units->items[i].unit) ||
            !declare_functions(program, &units->items[i].unit)) {
            return false;
        }
    }
    return true;
}

// The module a name lives in, which is what is in front of the first dot.
// `io.Io.write` is `io`'s, the same as `io.print`: what a host calls it is the
// rest of the name and not another module.
// Which module a registered name is under. A name lives under the whole of
// what its module calls itself, and what comes after may hold dots of its own:
// `std.io.Io.write` is a capability's function in `std.io`. So the answer is
// the longest module this program holds that the name begins with, asked of
// the program rather than guessed from a dot. Everything before the last dot
// where nothing matches, which is what a name from no module of this program
// can be given. See D1039.
static size_t module_of_in(const KestUnits *files, const char *name) {
    size_t best = 0;
    for (uint32_t i = 0; files != NULL && i < files->count; i++) {
        const char *module = files->items[i].module;
        if (module == NULL || module[0] == '\0') {
            continue;
        }
        size_t length = strlen(module);
        if (length > best && strncmp(name, module, length) == 0 &&
            name[length] == '.') {
            best = length;
        }
    }
    if (best > 0) {
        return best;
    }
    const char *dot = strrchr(name, '.');
    return dot == NULL ? strlen(name) : (size_t)(dot - name);
}

static const KestUnits *dumping_files;

// The modules a program is made of, in a table, so that splitting a name where
// its module ends is a lookup rather than a walk of every file. A listing of a
// thousand modules spent a fifth of a `check` inside that walk, and the walk
// that gathered what each module holds spent as much again. Both are keyed on
// the same table here. See D1086.
typedef struct {
    // One more than the place in `names`, so nought is an empty slot.
    uint32_t *slots;
    uint32_t slot_count;
    const char **names;
    size_t *lengths;
    // What each module's line in a listing is, filled while the listing is
    // walked. -1 until it has one.
    int32_t *held_at;
    uint32_t count;
} Modules;

static const Modules *dumping_modules;

static uint32_t module_slot(const Modules *modules, const char *name,
                            size_t length) {
    uint32_t mask = modules->slot_count - 1;
    uint32_t slot = name_hash(name, length) & mask;
    while (modules->slots[slot] != 0) {
        uint32_t at = modules->slots[slot] - 1u;
        if (modules->lengths[at] == length &&
            memcmp(modules->names[at], name, length) == 0) {
            return slot;
        }
        slot = (slot + 1u) & mask;
    }
    return slot;
}

// Every module named by a file, put in once. Answers false only for no room,
// and a listing with no table walks the files the way it always did.
static bool modules_prepare(KestArena *arena, const KestUnits *files,
                            Modules *modules) {
    modules->slots = NULL;
    modules->slot_count = 0;
    modules->names = NULL;
    modules->lengths = NULL;
    modules->held_at = NULL;
    modules->count = 0;
    uint32_t many = files == NULL ? 0 : files->count;
    if (many == 0) {
        return true;
    }
    uint32_t slots = 64;
    while (slots < (many + 1) * 2) {
        slots *= 2;
    }
    modules->slots = KEST_ARENA_ARRAY(arena, uint32_t, slots);
    modules->names = KEST_ARENA_ARRAY(arena, const char *, many);
    modules->lengths = KEST_ARENA_ARRAY(arena, size_t, many);
    modules->held_at = KEST_ARENA_ARRAY(arena, int32_t, many);
    if (modules->slots == NULL || modules->names == NULL ||
        modules->lengths == NULL || modules->held_at == NULL) {
        modules->slots = NULL;
        modules->slot_count = 0;
        return false;
    }
    modules->slot_count = slots;
    for (uint32_t i = 0; i < many; i++) {
        const char *module = files->items[i].module;
        if (module == NULL || module[0] == '\0') {
            continue;
        }
        size_t length = strlen(module);
        uint32_t slot = module_slot(modules, module, length);
        if (modules->slots[slot] != 0) {
            continue;
        }
        modules->names[modules->count] = module;
        modules->lengths[modules->count] = length;
        modules->held_at[modules->count] = -1;
        modules->slots[slot] = ++modules->count;
    }
    return true;
}

// Where a name's module ends: the longest dotted prefix of it that is a module
// this program has. A name holds few dots, so this is a lookup or two rather
// than a walk of every file.
static size_t module_of_prepared(const Modules *modules, const char *name) {
    if (modules == NULL || modules->slot_count == 0) {
        return 0;
    }
    const char *dot = name + strlen(name);
    while (dot > name) {
        dot--;
        if (*dot != '.') {
            continue;
        }
        size_t length = (size_t)(dot - name);
        uint32_t slot = module_slot(modules, name, length);
        if (modules->slots[slot] != 0) {
            return length;
        }
    }
    return 0;
}

static size_t module_of(const char *name) {
    if (dumping_modules != NULL && dumping_modules->slot_count > 0) {
        size_t length = module_of_prepared(dumping_modules, name);
        if (length > 0) {
            return length;
        }
        const char *dot = strrchr(name, '.');
        return dot == NULL ? strlen(name) : (size_t)(dot - name);
    }
    return module_of_in(dumping_files, name);
}

static bool same_module(const char *name, const char *module, size_t length) {
    // A file that names no module puts its names under nothing, so the ones
    // with no dot in them are its own. Everything else is under something.
    if (length == 0) {
        return strchr(name, '.') == NULL;
    }
    return module_of(name) == length && memcmp(name, module, length) == 0;
}

// What a module holds, for the ones a file imported rather than wrote. A
// program that imports one line of `std.math` held thirty lines of it in this
// listing, and what a reader came for was the file they are looking at.
typedef struct {
    const char *name;
    size_t length;
    uint32_t types;
    uint32_t functions;
    uint32_t foreign;
} Held;

static Held *held_of(Held *held, uint32_t *count, const char *name) {
    size_t length = module_of(name);
    // Which line of the listing this module already has, asked of the table
    // the modules are in rather than by walking the lines written so far.
    // That walk was the listing's own length for every name in it.
    if (dumping_modules != NULL && dumping_modules->slot_count > 0 &&
        length > 0) {
        uint32_t slot = module_slot(dumping_modules, name, length);
        if (dumping_modules->slots[slot] != 0) {
            uint32_t which = dumping_modules->slots[slot] - 1u;
            int32_t at = dumping_modules->held_at[which];
            if (at >= 0) {
                return &held[at];
            }
            dumping_modules->held_at[which] = (int32_t)*count;
        }
    } else {
        for (uint32_t i = 0; i < *count; i++) {
            if (held[i].length == length &&
                memcmp(held[i].name, name, length) == 0) {
                return &held[i];
            }
        }
    }
    Held *one = &held[(*count)++];
    one->name = name;
    one->length = length;
    one->types = 0;
    one->functions = 0;
    one->foreign = 0;
    return one;
}

bool kest_program_dump(const KestProgram *program, KestArena *arena,
                       const char *root, FILE *out) {
    // Which modules this program holds, so a name can be split where its
    // module ends rather than at a dot. Set for the length of the walk below
    // and put back, because what `module_of` answers is about one program.
    const KestUnits *was_dumping = dumping_files;
    const Modules *were_modules = dumping_modules;
    dumping_files = program->files;
    Modules modules;
    dumping_modules = modules_prepare(arena, program->files, &modules)
                          ? &modules
                          : NULL;
    size_t root_length = root == NULL ? 0 : strlen(root);
    uint32_t elsewhere = 0;
    uint32_t said = 0;
    Held *held = KEST_ARENA_ARRAY(arena, Held,
                                  program->type_count + program->global_count +
                                      1);
    // What this says about the file it was asked about depends on holding
    // every module it is not about, so an arena with no room for that list is
    // one this cannot answer from. It used to answer anyway, and what it wrote
    // then was every type of every module the program imports, listed as
    // though the file had declared them: a run that came back nought having
    // said something else. See D845.
    if (held == NULL) {
        return false;
    }

    for (uint32_t i = 0; i < program->type_count; i++) {
        const KestType *type = program->types[i];
        // A shape is not a type and has no layout, and neither has a copy
        // made with a name that is still standing for itself.
        if (type->type_param_count > 0 || mentions_param(type)) {
            continue;
        }
        if (type->name != NULL &&
            !same_module(type->name, root, root_length)) {
            if (type->tag == KEST_T_STRUCT || type->tag == KEST_T_ENUM ||
                type->tag == KEST_T_FLAGS) {
                held_of(held, &elsewhere, type->name)->types++;
            }
            continue;
        }
        if (type->tag == KEST_T_FLAGS) {
            said++;
            fprintf(out, "flags %s  1 slot, %u byte%s over u%u\n", type->name,
                    type->byte_size, type->byte_size == 1 ? "" : "s",
                    type->width);
            for (uint32_t c = 0; c < type->case_count; c++) {
                fprintf(out, "  bit %u  %s\n", c, type->cases[c].name);
            }
            continue;
        }
        if (type->tag == KEST_T_ENUM) {
            said++;
            fprintf(out, "enum %s  %u slot%s, %u byte%s aligned %u\n",
                    type->name, type->slots, type->slots == 1 ? "" : "s",
                    type->byte_size, type->byte_size == 1 ? "" : "s",
                    type->byte_align);
            for (uint32_t c = 0; c < type->case_count; c++) {
                fprintf(out, "  %u %s", c, type->cases[c].name);
                for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
                    fprintf(out, " slot +%u byte +%u %s",
                            type->cases[c].offsets[p],
                            type->cases[c].byte_offsets[p],
                            kest_type_name(arena, type->cases[c].payload[p]));
                }
                fputc('\n', out);
            }
            continue;
        }
        if (type->tag != KEST_T_STRUCT) {
            continue;
        }
        said++;
        fprintf(out, "struct %s  %u slot%s, %u byte%s aligned %u\n",
                type->name, type->slots, type->slots == 1 ? "" : "s",
                type->byte_size, type->byte_size == 1 ? "" : "s",
                type->byte_align);
        for (uint32_t m = 0; m < type->member_count; m++) {
            fprintf(out, "  slot +%u  byte +%-3u %s: %s\n",
                    type->members[m].offset, type->members[m].byte_offset,
                    type->members[m].name,
                    kest_type_name(arena, type->members[m].type));
        }
    }

    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        const KestType *type = symbol->type;
        if (!same_module(symbol->name, root, root_length)) {
            Held *one = held_of(held, &elsewhere, symbol->name);
            if (type->tag == KEST_T_FN) {
                one->functions++;
                if (type->is_foreign) {
                    one->foreign++;
                }
            } else {
                one->types++;
            }
            continue;
        }
        if (type->tag != KEST_T_FN) {
            said++;
            fprintf(out, "const %s: %s\n", symbol->name,
                    kest_type_name(arena, type));
            continue;
        }
        // Written the way the file writes it, so that what a host has to
        // provide is the line that says `extern` and not a line a reader has
        // to know something to tell apart. `--json` says the same thing with
        // a field, because a tool cannot read a word at the front.
        said++;
        fprintf(out, "%sfn %s", type->is_foreign ? "extern " : "",
                symbol->name);
        // The names it takes and what each has to be able to do, written the
        // way the file writes them. A generic's requirements are part of what
        // it is -- `set` wants a key that compares and hashes -- so a reader
        // handed the signature is handed them. See D1043.
        for (uint32_t g = 0; g < type->type_param_count; g++) {
            fputs(g == 0 ? "<" : ", ", out);
            fputs(type->type_param_names[g], out);
            uint8_t wants = type->type_param_wants == NULL
                                ? 0
                                : type->type_param_wants[g];
            if (wants == 0) {
                continue;
            }
            fputs(":", out);
            for (uint32_t at = 0; at < KEST_CAPABILITY_COUNT; at++) {
                if ((wants & kest_capability(at)->bit) != 0) {
                    fprintf(out, " %s", kest_capability(at)->word);
                }
            }
        }
        if (type->type_param_count > 0) {
            fputs(">", out);
        }
        fputs("(", out);
        for (uint32_t p = 0; p < type->param_count; p++) {
            fprintf(out, "%s%s", p > 0 ? ", " : "",
                    kest_type_name(arena, type->params[p]));
        }
        // Written the way the file writes it: a function that gives nothing
        // back has no arrow, so this has none either. See D519.
        bool gives = type->result != NULL && type->result->tag != KEST_T_VOID;
        fprintf(out, ")%s%s%s%s%s\n", gives ? " -> " : "",
                gives ? kest_type_name(arena, type->result) : "",
                type->no_alloc ? " no.alloc" : "",
                type->no_host ? " no.host" : "",
                type->deterministic ? " deterministic" : "");
    }

    // A line each for what was imported. The whole of them is what `--json`
    // is for, and `kest check` on the file itself is the other way to read
    // one.
    for (uint32_t i = 0; i < elsewhere; i++) {
        const Held *one = &held[i];
        fprintf(out, "%.*s ", (int)one->length, one->name);
        const char *between = " ";
        if (one->types > 0) {
            fprintf(out, "%s%u type%s", between, one->types,
                    one->types == 1 ? "" : "s");
            between = ", ";
        }
        if (one->functions > 0) {
            fprintf(out, "%s%u function%s", between, one->functions,
                    one->functions == 1 ? "" : "s");
            between = ", ";
        }
        if (one->foreign > 0) {
            fprintf(out, "%s%u the host provides", between, one->foreign);
        }
        fputc('\n', out);
    }

    // A file may hold nothing, and what a command says about it has to be
    // something: silence is what a command that did not run looks like.
    if (said == 0 && elsewhere == 0) {
        fputs("this file declares nothing\n", out);
    }
    dumping_files = was_dumping;
    dumping_modules = were_modules;
    return true;
}


// Where something was declared, which is what a tool wants in order to go
// there.
static void write_where(const KestSource *source, KestSpan span, FILE *out) {
    if (source == NULL) {
        return;
    }
    uint32_t line = 0;
    uint32_t column = 0;
    kest_source_locate(source, span.offset, &line, &column);
    fputs(",\"file\":", out);
    kest_json_text(source->path, out);
    fprintf(out, ",\"line\":%u,\"column\":%u", line, column);
}

// Whether a byte can be part of a name, for reading one out of a type written
// as words.
static bool a_name_byte(char byte) {
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || byte == '_';
}

// A type written with a generic's own type parameters put as where they stand
// rather than as what somebody called them. `fn pick<T>(a: T, b: T) -> T` and
// the same declaration with `U` written in place of `T` are one function said
// twice: the name is the declaration's own and nothing outside it can see it,
// so a fingerprint that tells the two apart is one that moves when nothing a
// caller can act on has. See D945.
static const char *where_it_stands(KestArena *arena, const char *written,
                                   const KestType *of) {
    if (of->type_param_count == 0 || of->type_param_names == NULL) {
        return written;
    }
    // `#` and the number are never longer than the shortest name a program can
    // write plus two, so three times is room enough for a type written
    // entirely out of them.
    size_t room = strlen(written) * 3 + 8;
    char *out = KEST_ARENA_ARRAY(arena, char, room);
    if (out == NULL) {
        return written;
    }
    size_t at = 0;
    size_t put = 0;
    while (written[at] != '\0' && put + 8 < room) {
        // Only where a name begins: `Pair` is not two names because it ends
        // in one, and a field of a module is not one either.
        if (at > 0 && (a_name_byte(written[at - 1]) || written[at - 1] == '.')) {
            out[put++] = written[at++];
            continue;
        }
        uint32_t which = of->type_param_count;
        size_t length = 0;
        for (uint32_t i = 0; i < of->type_param_count; i++) {
            size_t wide = strlen(of->type_param_names[i]);
            if (strncmp(written + at, of->type_param_names[i], wide) == 0 &&
                !a_name_byte(written[at + wide])) {
                which = i;
                length = wide;
                break;
            }
        }
        if (which == of->type_param_count) {
            out[put++] = written[at++];
            continue;
        }
        int said = snprintf(out + put, room - put, "#%u", which);
        if (said < 0) {
            return written;
        }
        put += (size_t)said;
        at += length;
    }
    out[put] = '\0';
    return out;
}

void kest_program_costs(const KestProgram *program, FILE *out) {
    uint32_t said = 0;
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        if (symbol->type->tag != KEST_T_FN) {
            continue;
        }
        bool foreign = symbol->type->is_foreign || symbol->decl == NULL ||
                       symbol->decl->kind != KEST_DECL_FN ||
                       symbol->decl->function.is_extern;
        if (said == 0) {
            // What each column is, because a table of `yes` and `no` with no
            // heading is a table nobody can read, and because the last one is
            // the only one that is advice rather than a fact.
            fprintf(out, "%-40s %-6s %-6s %-6s %s\n", "what the compiler "
                                                       "proved",
                    "heap", "host", "varies", "could promise");
        }
        said++;
        if (foreign) {
            // Judged by what it declares, because there is no body here to
            // walk. Nothing is proved about one and it says so rather than
            // saying no.
            fprintf(out, "%-40s %-6s %-6s %-6s %s\n", symbol->name, "?", "?",
                    "?", "the host says");
            continue;
        }
        bool heap = symbol->decl->function.reaches_heap;
        bool host = symbol->decl->function.reaches_host;
        bool varies = symbol->decl->function.not_deterministic;
        char could[64];
        size_t used = 0;
        if (!heap && !symbol->type->no_alloc) {
            used += (size_t)snprintf(could + used, sizeof(could) - used,
                                     "%sno.alloc", used == 0 ? "" : " ");
        }
        if (!host && !symbol->type->no_host) {
            used += (size_t)snprintf(could + used, sizeof(could) - used,
                                     "%sno.host", used == 0 ? "" : " ");
        }
        if (!varies && !symbol->type->deterministic) {
            snprintf(could + used, sizeof(could) - used, "%sdeterministic",
                     used == 0 ? "" : " ");
        } else if (used == 0) {
            snprintf(could, sizeof(could), "%s", "");
        }
        fprintf(out, "%-40s %-6s %-6s %-6s %s\n", symbol->name,
                heap ? "yes" : "no", host ? "yes" : "no",
                varies ? "yes" : "no", could);
    }
    if (said == 0) {
        fprintf(out, "this program declares no function\n");
    }
}

// Which module a declaration is in, written beside its name. A name lives
// under the whole of its module and what comes after may hold dots of its own,
// so a tool splitting the name at a dot would invent modules -- `std.io.Io` is
// a capability inside `std.io`. It is written rather than derivable. See
// D1039.
static void write_module(const KestProgram *program, const char *name,
                         FILE *out) {
    // The same table the printed listing uses where there is one, and the walk
    // of every file where there is not. See D1086.
    size_t length = 0;
    if (dumping_modules != NULL && dumping_modules->slot_count > 0) {
        length = module_of_prepared(dumping_modules, name);
        if (length == 0) {
            const char *dot = strrchr(name, '.');
            length = dot == NULL ? strlen(name) : (size_t)(dot - name);
        }
    } else {
        length = module_of_in(program->files, name);
    }
    fputs(",\"module\":", out);
    if (length == 0 || length == strlen(name)) {
        fputs("null", out);
        return;
    }
    char held[256];
    if (length + 1 > sizeof(held)) {
        fputs("null", out);
        return;
    }
    memcpy(held, name, length);
    held[length] = '\0';
    kest_json_text(held, out);
}

void kest_program_dump_json(const KestProgram *program, KestArena *arena,
                            FILE *out) {
    const Modules *were_modules = dumping_modules;
    Modules modules;
    dumping_modules = modules_prepare(arena, program->files, &modules)
                          ? &modules
                          : NULL;
    fputs("\"types\":[", out);
    bool first = true;
    for (uint32_t i = 0; i < program->type_count; i++) {
        const KestType *type = program->types[i];
        if (type->tag != KEST_T_STRUCT && type->tag != KEST_T_ENUM &&
            type->tag != KEST_T_FLAGS) {
            continue;
        }
        // The same two the printed form leaves out, for the same reason: a
        // shape is not a type and has no layout, and neither has a copy made
        // with a name that is still standing for itself. Saying `0 bytes` of
        // either is answering a question nobody asked with a number nobody
        // can use.
        if (type->type_param_count > 0 || mentions_param(type)) {
            continue;
        }
        fputs(first ? "" : ",", out);
        first = false;
        fputs("{\"name\":", out);
        kest_json_text(type->name, out);
        write_module(program, type->name, out);
        fprintf(out, ",\"kind\":\"%s\"",
                type->tag == KEST_T_ENUM
                    ? "enum"
                    : (type->tag == KEST_T_FLAGS ? "flags" : "struct"));
        fprintf(out, ",\"slots\":%u,\"bytes\":%u,\"align\":%u,\"named\":%s",
                type->slots, type->byte_size, type->byte_align,
                type->named ? "true" : "false");
        write_where(type->declared_in, type->span, out);
        // A set of bits is a type with a layout like any other, and what a
        // reader wants of it is which bit each name stands for. It says the
        // width it is kept in, because that is what a host lays beside its
        // own and what `u8(state)` gives back.
        if (type->tag == KEST_T_FLAGS) {
            fprintf(out, ",\"over\":\"u%u\",\"bits\":[", type->width);
            for (uint32_t c = 0; c < type->case_count; c++) {
                fputs(c == 0 ? "" : ",", out);
                fputs("{\"name\":", out);
                kest_json_text(type->cases[c].name, out);
                fprintf(out, ",\"bit\":%u,\"named\":%s}", c,
                        type->cases[c].named ? "true" : "false");
            }
            fputs("]}", out);
            continue;
        }
        if (type->tag == KEST_T_ENUM) {
            // What a case carries and where each piece of it sits, which is
            // what a host laying one out beside its own needs.
            fputs(",\"cases\":[", out);
            for (uint32_t c = 0; c < type->case_count; c++) {
                fputs(c == 0 ? "" : ",", out);
                fputs("{\"name\":", out);
                kest_json_text(type->cases[c].name, out);
                fprintf(out, ",\"tag\":%u,\"named\":%s,\"carries\":[", c,
                        type->cases[c].named ? "true" : "false");
                for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
                    fputs(p == 0 ? "" : ",", out);
                    fputs("{\"type\":", out);
                    kest_json_text(
                        kest_type_name(arena, type->cases[c].payload[p]), out);
                    fprintf(out, ",\"slot\":%u,\"byte\":%u}",
                            type->cases[c].offsets[p],
                            type->cases[c].byte_offsets[p]);
                }
                fputs("]}", out);
            }
            fputs("]}", out);
            continue;
        }
        fputs(",\"fields\":[", out);
        for (uint32_t m = 0; m < type->member_count; m++) {
            fputs(m == 0 ? "" : ",", out);
            fputs("{\"name\":", out);
            kest_json_text(type->members[m].name, out);
            fputs(",\"type\":", out);
            kest_json_text(kest_type_name(arena, type->members[m].type), out);
            fprintf(out, ",\"slot\":%u,\"byte\":%u,\"own\":%s}",
                    type->members[m].offset, type->members[m].byte_offset,
                    type->members[m].own ? "true" : "false");
        }
        fputs("]}", out);
    }

    fputs("],\"functions\":[", out);
    first = true;
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        if (symbol->type->tag != KEST_T_FN) {
            continue;
        }
        fputs(first ? "" : ",", out);
        first = false;
        fputs("{\"name\":", out);
        kest_json_text(symbol->name, out);
        write_module(program, symbol->name, out);
        // The type names it takes, each with what it has to be able to do.
        // A generic's requirements are part of what it is, so a tool reading
        // this is handed them rather than left to find out at a call. Empty
        // for a function that takes no types. See D1043.
        fputs(",\"typeParameters\":[", out);
        for (uint32_t g = 0; g < symbol->type->type_param_count; g++) {
            fputs(g == 0 ? "{\"name\":" : ",{\"name\":", out);
            kest_json_text(symbol->type->type_param_names[g], out);
            fputs(",\"wants\":[", out);
            uint8_t wants = symbol->type->type_param_wants == NULL
                                ? 0
                                : symbol->type->type_param_wants[g];
            bool said = false;
            for (uint32_t at = 0; at < KEST_CAPABILITY_COUNT; at++) {
                if ((wants & kest_capability(at)->bit) == 0) {
                    continue;
                }
                fputs(said ? "," : "", out);
                said = true;
                kest_json_text(kest_capability(at)->word, out);
            }
            fputs("]}", out);
        }
        fputs("],\"parameters\":[", out);
        for (uint32_t p = 0; p < symbol->type->param_count; p++) {
            fputs(p == 0 ? "" : ",", out);
            kest_json_text(kest_type_name(arena, symbol->type->params[p]), out);
        }
        // What it gives back, under a name of its own: `result` is what a
        // call answered and is null when there is none, and this is the type
        // a function gives back, which is `nothing` when it gives nothing.
        // One name for one thing, and these were two things. See D590.
        fputs("],\"gives\":", out);
        kest_json_text(kest_type_name(arena, symbol->type->result), out);
        fprintf(out, ",\"noAlloc\":%s,\"noHost\":%s,"
                     "\"deterministic\":%s,\"foreign\":%s,"
                     "\"named\":%s",
                symbol->type->no_alloc ? "true" : "false",
                symbol->type->no_host ? "true" : "false",
                symbol->type->deterministic ? "true" : "false",
                symbol->type->is_foreign ? "true" : "false",
                symbol->named ? "true" : "false");
        // What the promises' proof found, beside what the declaration says.
        // The two are different questions: one is what a caller was told and
        // the other is what the compiler walked the call graph and saw. A body
        // that reaches nothing and says nothing is a promise somebody could
        // make, and `could` says so. A foreign body is judged by what it
        // declares, because there is no body here to walk, so nothing is
        // proved about one and `could` is false. See D976.
        {
            bool foreign = symbol->type->is_foreign || symbol->decl == NULL ||
                           symbol->decl->kind != KEST_DECL_FN ||
                           symbol->decl->function.is_extern;
            bool heap = foreign || symbol->decl->function.reaches_heap;
            bool host = foreign || symbol->decl->function.reaches_host;
            bool varies = foreign || symbol->decl->function.not_deterministic;
            fprintf(out,
                    ",\"proved\":{\"walked\":%s,\"reachesHeap\":%s,"
                    "\"reachesHost\":%s,\"notDeterministic\":%s,"
                    "\"couldPromiseNoAlloc\":%s,\"couldPromiseNoHost\":%s,"
                    "\"couldPromiseDeterministic\":%s}",
                    foreign ? "false" : "true", heap ? "true" : "false",
                    host ? "true" : "false", varies ? "true" : "false",
                    !foreign && !heap && !symbol->type->no_alloc ? "true"
                                                                 : "false",
                    !foreign && !host && !symbol->type->no_host ? "true"
                                                                : "false",
                    !foreign && !varies && !symbol->type->deterministic
                        ? "true"
                        : "false");
        }
        // And a number standing for this declaration and what it promises a
        // caller, folded from what is already printed beside it rather than
        // from where it is written. It is a signature, which is one of four
        // things that get called identity and is the only one of them this
        // number is: not where the declaration is, not what its body compiled
        // to -- `emit` says that per chunk -- and not what a value made while
        // it runs is. See D924 and D945. A tool that watches a file -- a reloader, a save format, a
        // debugger, something reading a diff -- needs to know that the thing
        // it saw yesterday is the thing it is looking at today, and a line
        // number is not that: a blank line above it moves every one of them.
        //
        // What it is made of is the module and name, which is the same
        // qualified name a program writes; the types it takes and gives, which
        // is what tells two functions of one name apart; and the promises,
        // which are part of what a caller may do with it. So it survives
        // formatting, a comment, a renamed local and a declaration moved past
        // it, and it changes when the signature or a promise changes -- which
        // is when a caller has to be told. See D924.
        uint64_t stands_for = kest_mark_bytes(KEST_MARK_START, symbol->name,
                                              strlen(symbol->name));
        stands_for = kest_mark_bytes(stands_for, "(", 1);
        for (uint32_t p = 0; p < symbol->type->param_count; p++) {
            const char *one = where_it_stands(
                arena, kest_type_name(arena, symbol->type->params[p]),
                symbol->type);
            stands_for = kest_mark_bytes(stands_for, one, strlen(one));
            stands_for = kest_mark_bytes(stands_for, ",", 1);
        }
        const char *gives = where_it_stands(
            arena, kest_type_name(arena, symbol->type->result), symbol->type);
        stands_for = kest_mark_bytes(stands_for, ")->", 3);
        stands_for = kest_mark_bytes(stands_for, gives, strlen(gives));
        char promised[4] = {symbol->type->no_alloc ? 'a' : '-',
                            symbol->type->no_host ? 'h' : '-',
                            symbol->type->deterministic ? 'd' : '-',
                            symbol->type->is_foreign ? 'f' : '-'};
        stands_for = kest_mark_bytes(stands_for, promised, sizeof(promised));
        fprintf(out, ",\"signature\":\"%016llx\"",
                (unsigned long long)stands_for);
        write_where(symbol->source, symbol->span, out);
        fputc('}', out);
    }

    fputs("],\"constants\":[", out);
    first = true;
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        if (symbol->type->tag == KEST_T_FN) {
            continue;
        }
        fputs(first ? "" : ",", out);
        first = false;
        fputs("{\"name\":", out);
        kest_json_text(symbol->name, out);
        write_module(program, symbol->name, out);
        fputs(",\"type\":", out);
        kest_json_text(kest_type_name(arena, symbol->type), out);
        fprintf(out, ",\"named\":%s", symbol->named ? "true" : "false");
        write_where(symbol->source, symbol->span, out);
        fputc('}', out);
    }
    fputc(']', out);
    // And how many types checking this program made, against the ones above
    // that have names. A program makes one for every signature, every optional
    // and every run of something, and those are most of them: the list is what
    // a reader asks about and this is what the stage cost. See D644.
    // And what one weighs on the machine that answered, for the same reason a
    // node and a token say it: a tool holding what a build keeps against what
    // it is made of wants both numbers from the same run. See D754.
    fprintf(out, ",\"typesMade\":%u,\"typeBytes\":%zu", program->types_made,
            sizeof(KestType));
    dumping_modules = were_modules;
}
