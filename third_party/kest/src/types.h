#ifndef KEST_TYPES_H
#define KEST_TYPES_H

#include "kest.h"
#include "loader.h"

// What a reference is made of, in the one place two layers both read it: the
// low bits are the place in the store and everything above them is the number
// the process handed out. The runtime builds one; the type layer hashes one,
// and hashes the place alone -- the handed-out number is one count shared by
// every machine of a process, so a program that could see it would answer
// differently on the second machine of a run and `deterministic` would be a
// word. See D1033 and D1054.
#define KEST_REF_PLACE_BITS 24

typedef enum {
    // Stands in where a type could not be resolved. It compares equal to
    // everything, so one bad annotation reports once instead of at every use.
    KEST_T_ERROR,
    KEST_T_VOID,
    KEST_T_BOOL,
    KEST_T_INT,
    KEST_T_FLOAT,
    KEST_T_TEXT,
    KEST_T_STRUCT,
    KEST_T_ENUM,
    // A set of named bits over an integer of a written width. Not an enum: a
    // value is any combination of the cases, so nothing exhausts it and a
    // `match` does not apply. See D033.
    KEST_T_FLAGS,
    KEST_T_ARRAY,
    // `[f32; 16]`: that many, laid out where it stands, and a value like a
    // struct rather than a handle like an array. See D064.
    KEST_T_FIXED,
    KEST_T_REF,
    // A slot map that hands out references and can delete what it holds. Not
    // a collector, not a count, not a region: see D014.
    KEST_T_STORE,
    KEST_T_OPTIONAL,
    KEST_T_FN,
    // An imported name. Its members are not resolved yet, so reading one
    // yields an error type without a diagnostic; see the worklog.
    KEST_T_MODULE,
    // A name standing for one type per instance of a generic function. It
    // never reaches the compiler: a call binds it and a copy is compiled with
    // it substituted. See D040.
    KEST_T_PARAM,
} KestTypeTag;

typedef struct {
    const char *name;
    KestType *type;
    KestSpan span;
    // Where this member starts inside its struct, in slots. A struct is a
    // value laid out flat, so a nested struct's members are part of the same
    // run and a field is reached by adding offsets rather than by chasing a
    // pointer.
    uint16_t offset;
    // And where it starts in bytes, which is a different number: a slot is
    // eight bytes whatever it holds, and a `f32` is four. The two layouts are
    // for two places, and D016 says which is which.
    uint16_t byte_offset;
    // Written `own`, so only the module that declared the shape may name it.
    // Nothing of it reaches the machine: a member is laid out where it was
    // laid out and a host reads what it read. See D1041.
    bool own;
} KestMember;

// One case of an enum: what it carries, by position, and where each piece
// sits. The tag is slot zero and byte zero, so every case's payload starts
// after it and the tag can be read without knowing which case it is.
typedef struct {
    const char *name;
    KestType **payload;
    uint16_t *offsets;
    uint16_t *byte_offsets;
    uint32_t payload_count;
    // What this one carries, laid out the way anything else is: one piece a
    // slot, at the byte it sits at inside the value. The enum's own layout
    // cannot say it — which type a payload slot holds depends on the tag — so
    // it is said here, per case, and `kest_case_of` is the door onto it. Made
    // where the enum's layout is made, because a host asking has nowhere to put
    // one. See D702.
    KestPiece *carries;
    uint16_t carry_count;
    KestSpan span;
    // Whether anything in the program wrote this one's name: built it, tested
    // for it, or answered it in a `match`. A set of bits and an enum are the
    // one place where a name inside a shape can go unwritten while the shape
    // itself is held everywhere.
    bool named;
} KestVariantType;

struct KestType {
    KestTypeTag tag;
    // Set for primitives and structs. Composed types are named on demand by
    // kest_type_name, so nothing has to be built for types nobody reports.
    const char *name;
    // ENUM.
    KestVariantType *cases;
    // STRUCT.
    KestMember *members;
    // Which file declared it. A primitive has none.
    const KestSource *declared_in;
    // ARRAY, REF, OPTIONAL and FIXED.
    KestType *element;
    // FN.
    KestType **params;
    KestType *result;
    // The name this one function is compiled under, which includes what it
    // takes, because two functions may share a name if they take different
    // things. Set for every function, generic or not.
    const char *symbol;
    // A generic function, whose parameters mention type names. It has no body
    // to compile until a call says what they stand for, so where it was
    // written is kept: a call makes the copy from there.
    const char **type_param_names;
    // And what each of them has to be able to do, one mask a parameter in the
    // same order, out of what the declaration wrote. See D1043.
    const uint8_t *type_param_wants;
    // And the type each name stands for while the generic's own body is
    // checked, which is a type that can do what the parameter says and
    // nothing else. The signature was resolved against these, so the body has
    // to be checked against the same ones. See D1043.
    KestType **type_param_stands;
    const KestDecl *decl;
    const KestUnitInfo *unit;
    // A copy of a generic struct: which shape it came from and what it was
    // made with, so a copy inside a generic body can be made again with the
    // names that body was given.
    KestType *shape;
    KestType **type_args;
    // Declared rather than defined here, so the host must provide it and
    // nothing about it can be inferred. The name the host binds is the one
    // written, without the module: which file declared it is Kest's business
    // and not the host's.
    const char *foreign_name;
    // The words are all above and the numbers are all here, which is what
    // keeps a type from being half padding: a `bool` between two pointers is
    // seven bytes of nothing, and this shape had seven of those. See D644.
    KestSpan span;
    uint32_t case_count;
    uint32_t member_count;
    // FIXED only: how many.
    uint32_t count;
    uint32_t param_count;
    uint32_t type_param_count;
    uint32_t type_arg_count;
    // How many slots a value of this type occupies. One for everything that
    // fits in a machine word, and the sum of its members for a struct.
    uint16_t slots;
    // What this type is where memory is shared: the size and alignment a C
    // compiler would give it, so an array of them can be the same bytes the
    // host already has.
    uint16_t byte_size;
    uint16_t byte_align;
    // INT and FLOAT.
    uint8_t width;
    // PARAM only: what the generic this one stands in for says it can do,
    // which is what the body is checked against. See D1043.
    uint8_t wants;
    // And what the body actually asked of it, so a generic saying it needs
    // something it never uses is told: a requirement nothing uses refuses
    // types that would have worked, and it is the way a written contract
    // drifts away from the body it is about. See D1043.
    uint8_t took;
    bool is_signed;
    // Set while the size is being worked out, so a struct that contains
    // itself is caught rather than followed forever.
    bool sizing;
    // Whether anything in the program wrote its name: a field, a parameter,
    // a binding, a value built out of it. A type nothing names is compiled,
    // laid out, and never reachable — see the warning `check` gives for it.
    bool named;
    bool no_alloc;
    // And that it calls nothing the host provides, which is the other promise
    // and is proved the same way. See D853.
    bool no_host;
    bool deterministic;
    bool is_foreign;
    // STRUCT only: one of the language's own `vec2`, `vec3` and `vec4`, which
    // have operators a struct a program declares has not. See D1250.
    bool vector;
};

typedef struct KestInstance KestInstance;

typedef struct {
    const char *name;
    KestType *type;
    KestSpan span;
    // Which file declared it, so what is said about it can be shown there.
    const KestSource *source;
    // What was written, for the things the type does not carry: a parameter
    // has a name where it is declared and only a type after that. NULL for a
    // constant, which is a name for a value and has no parameters.
    const KestDecl *decl;
    bool is_const;
    // Whether anything in the program named it: a call, or the name handed
    // around as a value. What this is for is a library, where a function
    // nothing names is one nothing has ever run — a reader can only count
    // mentions, and a mention in a comment is not one.
    bool named;
    // What a constant is written as, for working it out. A constant is a name
    // for a value and the value is in the tree; nothing else needs this.
    const KestExpr *value;
    // And what it came to, worked out once where it is declared rather than
    // again at every use: a constant read five times was folded five times,
    // and one read no times was never worked out at all — so a program could
    // carry one that divides by nought and nothing said so. NULL until the
    // compiler has been over it. See D674.
    KestValue *folded;
    uint32_t folded_slots;
    // Whether working it out was refused, so that the uses under it say
    // nothing: what a refusal says about a declaration is said once.
    bool would_not_fold;
} KestSymbol;

// Everything one file declares, after names have been resolved to types.
// One place a name was written, and what it turned out to name. The checker
// resolves every name once and threw the answer away; an editor asking "what
// is this" and "where else is it" has to have the same answer the compiler
// had, and working it out again in a second reader is the second semantic
// pipeline this project does not have. So the answers are kept. See D977.
typedef struct {
    // The file the name is written in, and where in it.
    const KestSource *source;
    KestSpan span;
    // Where what it names is declared, which is what "go to definition" is.
    // `declared_in` is NULL for a name whose declaration has no file -- a
    // builtin, or a type the language provides.
    const KestSource *declared_in;
    KestSpan declared;
    // What it is, written the way a message writes one. Kept as the type
    // rather than as text, because text of a type costs an allocation and
    // most of these are never asked about.
    const KestType *type;
    // Whether what it names is a name in one body. A local and a global with
    // one spelling are two things, and renaming one must not touch the other.
    bool is_local;
} KestUse;

typedef struct {
    KestArena *arena;
    // How many times this compiler has worked a value out where it was
    // written. A constant is folded once, at its declaration, and every use of
    // it reads what came of that — so this is one per constant and not one per
    // use, which is the difference a reader can see rather than infer. See
    // D675.
    uint32_t folds;
    // And how many times it was asked and there was nothing to work out: a
    // field of a local, a name that is not a constant. The compiler asks the
    // folder of anything that might be one, because that is how it finds out —
    // ninety-one askings to nineteen answers in one example — so what a fold
    // that comes to nothing costs is what this number is worth reading for.
    // See D676.
    uint32_t asked_for_nothing;
    // Whether the last fold stopped because the language does not work that
    // kind of thing out, as against because what was written cannot be worked
    // out. Kept here because a fold is a walk and the answer is about the walk
    // rather than about any one step of it. See D673.
    bool fold_never;
    // While a walk written out once a turn is being compiled, what a name the
    // body declared is worth: true for a name the body has, with the value
    // where it holds one and NULL where it does not. NULL otherwise, and then
    // every name the folder reads is a constant's. See D1231.
    bool (*held_name)(void *context, const char *name, uint32_t length,
                      const KestValue **value, uint32_t *slots);
    void *held_context;
    // The file being worked on, and the name its declarations live under.
    // Every name is registered under the whole of what the file calls itself,
    // `a.math.twice`, because two modules may share a last part and cannot
    // share the whole; inside its own module the prefix may be left off, and
    // `alias` is the last part a reader writes in front of a name from
    // somewhere else. See D1039.
    const KestSource *source;
    const char *alias;
    const char *module;
    const KestUnitInfo *unit;
    KestDiags *diags;

    // Primitives and structs, in declaration order.
    KestType **types;
    uint32_t type_count;
    uint32_t type_capacity;
    // And where each of them is, by name, for the reason the globals below
    // have one. A program holds every file's types rather than one file's, so
    // the walk that found a type by name was the program's own size for every
    // type any body names: a thousand modules of three functions each spent
    // 45 per cent of a clean check inside it. Slots hold one more than the
    // place they name, so nought is an empty slot. See D1086.
    uint32_t *types_by_name;
    uint32_t types_by_name_slots;

    // And the ones nothing declared: `[text]`, `Item?`, `ref<Npc>`. A composed
    // type is what it is made of and nothing else, so two written in two
    // places are one type rather than two that answer the same — which is
    // what the layout table found out the long way round, by holding a
    // hundred and fifty-seven layouts of a type it had already laid out. Kept
    // apart from `types` because those have names and are found by them, and
    // these have none. See D780.
    KestType **composed;
    uint32_t composed_count;
    uint32_t composed_capacity;
    // And where each of them is, by what it is made of. One already made of
    // the same thing is the same type, and the walk that found it was the
    // list of every composed type in the program for every `[Thing]` any body
    // writes: at a million lines that was a twelfth of a clean check. Slots
    // hold one more than the place they name. See D1088.
    uint32_t *composed_by_shape;
    uint32_t composed_by_shape_slots;

    KestSymbol *globals;
    uint32_t global_count;
    uint32_t global_capacity;
    // Where each of them is, by name. Unlike the types above, there are enough
    // of these for the difference to show: the list is walked by name for
    // every declaration and every use, so what it cost was the program's own
    // size squared. Slots hold one more than the place they name, so nought is
    // an empty slot. See D327.
    uint32_t *by_name;
    uint32_t by_name_slots;
    // And where each of them was written, which is how everything that walks
    // declarations finds the symbol the checker made for one. That walk was
    // the list again for every declaration -- the contract prover alone spent
    // a twentieth of a clean check in it at a thousand modules. Keyed on the
    // file and the offset, because a span is unique in a file and a file is
    // unique in a program. Slots hold one more than the place they name. See
    // D1086.
    uint32_t *by_place;
    uint32_t by_place_slots;
    // And the files of this program by the alias a name is written through.
    // Asking whether `alias.name` is a name some module has and this file did
    // not import walked every file, for every dotted name in every body: a
    // project of eighteen hundred modules spent a quarter of a clean check
    // there. Two files may share an alias, so a run of slots holds all of
    // them and a lookup walks the run. Slots hold one more than the file they
    // name. See D1087.
    uint32_t *files_by_alias;
    uint32_t files_by_alias_slots;

    // What the type names in scope stand for right now. Only a generic
    // signature or a generic body is resolved with any of these set. As many
    // as a declaration wrote: this was a run of eight, and the ninth was
    // dropped without a word, so a shape's own type name was unknown between
    // its own angle brackets.
    const char **bound_names;
    KestType **bound_types;
    uint32_t bound_count;
    uint32_t bound_capacity;

    // One per set of types a generic function is called with. The checker
    // fills this and the compiler walks it, so a copy exists exactly where it
    // is used and nowhere else.
    KestInstance *instances;
    uint32_t instance_count;
    uint32_t instance_capacity;
    // And where each copy is, by the declaration it is of and the types it
    // was given. The walk that found one compared every copy in the program
    // against the types in hand, and comparing two types is a walk of its
    // own: at a million lines, with one copy of a generic per module, that
    // was a seventh of a clean check. What a slot holds is one more than the
    // copy it names, and what lands on one slot is compared the way it always
    // was. See D1088.
    uint32_t *instances_by_use;
    uint32_t instances_by_use_slots;
    // Where every name was written and what it named. Filled by the checker,
    // read by whatever asks about a place in a file. See D977.
    KestUse *uses;
    uint32_t use_count;
    uint32_t use_capacity;
    // Whether to keep them at all. A compile run out of a build does not want
    // an index of every name in the program and should not pay for one: the
    // library's is a third again of what a finished build holds. An editor
    // asks for it and everybody else does not. See D977.
    bool index_names;
    // Constants that a `[T; N]` counted with. A type is resolved before the
    // constants are declared — a struct's fields are what a constant of that
    // struct is measured from — so there is no symbol to mark when a count
    // reads one, and the name is kept until there is.
    // Every file this program is made of. A count is resolved before the
    // constants are symbols, so a `[T; box.CELLS]` is answered by finding the
    // file `box` is and reading what it declares — which is what this is for
    // and the only thing it is used for. See D681.
    const KestUnits *files;
    const char **counted;
    uint32_t counted_count;
    uint32_t counted_capacity;
    // Every type this program made, counted where they are made. What is in
    // `types` is the ones with names; a program makes one for every signature,
    // every optional, every run of something, and those are most of them. A
    // tree was countable because the parser makes nodes in three places, and
    // this is the same for the stage above it. See D644.
    uint32_t types_made;
} KestProgram;

// A generic function with its type names bound. The symbol is what the copy
// is compiled under, which is the name with what it was given written into it.
struct KestInstance {
    const KestDecl *decl;
    const KestUnitInfo *unit;
    const char *symbol;
    KestType *type;
    const char **names;
    KestType **bindings;
    uint32_t count;
    bool checked;
    // The call that made this copy, and the file it is in. A mistake in a
    // generic body is a mistake in one of its copies, and which one is the
    // call that asked for it.
    KestSpan site;
    const KestSource *site_source;
};

// Resolves declarations, their field types and their signatures, reporting
// what it cannot resolve. Returns false only when the host is out of memory.
// What a division leaves over, for a float: the same answer C's `fmod` gives,
// worked out out of arithmetic because the engine is held to libc and nothing
// beyond it. The folder here and the machine ask the one function, which is
// what makes the folded answer and the run one the same by construction rather
// than by two people writing the same twenty lines. See D668, D776.
// What the checker still holds when a build is done. See D784.
void kest_program_holds(const KestProgram *program, uint32_t *types,
                        uint32_t *globals, uint32_t *found_by,
                        uint32_t *composed);

double kest_left_over(double left, double right);

// `index_names` keeps the answers the checker works out for every name, which
// is what an editor reads and what nothing else does.
bool kest_check(KestArena *arena, KestDiags *diags, const KestUnits *units,
                bool index_names,
                KestProgram **out);

// Points the program at one file, so what follows resolves names the way that
// file writes them.
void kest_program_in(KestProgram *program, const KestUnitInfo *unit);

// Writes down that a name was written here and what it named. Quietly does
// nothing when there is no room, because an index for an editor is not worth
// refusing a compile over.
void kest_program_used(KestProgram *program, const KestSource *source,
                       KestSpan span, const KestSource *declared_in,
                       KestSpan declared, const KestType *type, bool is_local);

// Every name written in this program, in the order they were read. What an
// editor asks of it: which use is under this offset, which declaration it
// points at, and which other uses point at the same one.
const KestUse *kest_program_uses(const KestProgram *program, uint32_t *count);

// Lookup as a file writes it: its own names bare, everything else prefixed
// with the module it came from.
KestType *kest_lookup_type(KestProgram *program, const char *name,
                           size_t length);
KestSymbol *kest_lookup_global(KestProgram *program, const char *name,
                               size_t length);

// Every function declared under this name, in declaration order. A name with
// one meaning has one; a name with several has several, and which is meant is
// settled by what is passed.
uint32_t kest_overloads(KestProgram *program, const char *name, size_t length,
                        KestSymbol **found, uint32_t room);

// The one declared at this place. Two functions may share a name, so where a
// declaration is is the only thing that names exactly one of them.
KestSymbol *kest_symbol_at(KestProgram *program, const KestSource *source,
                           KestSpan span);

// Whether this name was reached across a module boundary the file did not ask
// to cross. A name found in the file's own module crosses nothing, and so
// does a host receiver, which is a name with a dot in it and not a module.
bool kest_needs_import(KestProgram *program, const char *name, size_t length);
// Whether this program holds a name written this way under a module this file
// has not imported. See D1039.
bool kest_out_of_reach(KestProgram *program, const char *name, size_t length);

// What a name hashes to, for the tables that find one. Here rather than in
// each of them because one hash written twice is two the day either moves:
// the program's own indexes and the contract graph's both find a function by
// the name the checker gave it. See D1087.
uint32_t kest_name_hash(const char *name, size_t length);
// The way a file writes a registered name: the last part of its module and
// then the name. See D1039.
const char *kest_written_as(KestProgram *program, const char *whole);

// And which import a name was reached through, marked where the reach is
// decided rather than where it is offered: the suggestion machine asks whether
// every name in the program needs an import, and a name offered is not a name
// written. See D725.
void kest_import_reached(KestProgram *program, const char *name, size_t length);

// The same mark, for a walk that has the alias on its own rather than inside a
// dotted name: `io.pr1nt` reaches `io` and finds nothing under it, which is a
// file that writes the import and would otherwise be told to take it out.
// See D735.
void kest_import_reached_by(KestProgram *program, const char *alias,
                            size_t length);

// Whether this file may write the alias in front of a name. Two things put one
// in reach and they are asked about together, because a walk that asks about
// only one of them disagrees with the others: an import the file wrote, and the
// module the file itself is. What it answers is about the file rather than
// about the program -- a module may be read because another file asked for it,
// and this one still cannot write its name. `kest_needs_import` is the same
// question turned round, and asks this. See D736.
bool kest_file_reaches(KestProgram *program, const char *alias, size_t length);

// A module is not a thing in the program: it is what the names under it have in
// common. These three ask about that, and they live here rather than beside one
// walk because both walks ask them -- a module written where a value goes and a
// module written where a type goes are one mistake said twice. See D739.
//
// Whether a registered name is one of the names under this module.
bool kest_under_module(const char *whole, const char *name, size_t length);
// Which module a file means by a word it writes in front of a name, or NULL
// for a word this file may not write.
const char *kest_module_for(KestProgram *program, const char *alias,
                            size_t length);

// Whether this file can reach a module of this name, which is so when
// something reachable is declared under it.
bool kest_module_named(KestProgram *program, const char *name, size_t length);

// What a generic's own type name stands for while a signature or a copy's body
// is being resolved, and NULL for a name that is not one of them. Asked where a
// type is written and where a value is: a name that stands for a type is not an
// unknown name wherever it is written. See D741.
KestType *kest_bound_type(KestProgram *program, const char *name,
                          size_t length);

// One of the names under it, preferring a name with nothing further after the
// module, and NULL when the file reaches no such module. What it is for is
// being said out loud and being pointed at: the file it was declared in is the
// answer to which `io` a program was read with.
const KestSymbol *kest_first_under(KestProgram *program, const char *name,
                                   size_t length);

// One copy of a generic struct per set of types, made the first time that set
// is written and found again after that.
KestType *kest_struct_of(KestProgram *program, KestType *shape,
                         KestType **args, uint32_t count);

// Whether a type mentions a name that is standing for itself.
bool kest_mentions_name(const KestType *type);

// The same type with every type name replaced by what it stands for.
KestType *kest_substitute(KestProgram *program, KestType *type,
                          const char **names, KestType **bindings,
                          uint32_t count);

// Works out what each type name has to stand for by putting a declared type
// beside the one that was passed. False when two uses disagree.
bool kest_unify(const KestType *declared, const KestType *given,
                const char **names, KestType **bindings, uint32_t count);

// Binds the type names a generic declaration or instance brought into scope.
// Anything resolved while they are bound sees them and nothing else does.
// Whether a type name may be asked to do this, and a note that the body did
// ask. What is read is what is bound where the question is asked rather than
// what is on the type: two generics in one module share a copy of a shape, and
// the name inside it belongs to whichever of them asked for it first. One door
// rather than a question and a mark beside it, because a question asked
// without the mark is a requirement that reads as one nothing uses.
// See D1043.
bool kest_wants(KestProgram *program, const KestType *type,
                KestCapability bit);
void kest_bind_types(KestProgram *program, const char **names,
                     KestType **types, uint32_t count);
void kest_unbind_types(KestProgram *program);

// The copy of a generic function for one set of types, made if it is the
// first time that set was asked for.
KestInstance *kest_instance_of(KestProgram *program, const KestDecl *decl,
                               const KestUnitInfo *unit, const char **names,
                               KestType **bindings, uint32_t count);


KestType *kest_resolve_type_ref(KestProgram *program, const KestTypeRef *ref);

// Makes the type of an array holding this element, for a literal whose type
// nobody wrote down.
KestType *kest_array_of(KestProgram *program, KestType *element);
KestType *kest_optional_of(KestProgram *program, KestType *element);
KestType *kest_ref_of(KestProgram *program, KestType *element);
// That many of something, laid out where it stands rather than behind a
// handle. Copying one copies all of it.
KestType *kest_fixed_of(KestProgram *program, KestType *element,
                        uint32_t count);

KestType *kest_find_type(KestProgram *program, const char *name,
                         size_t length);
KestSymbol *kest_find_global(KestProgram *program, const char *name,
                             size_t length);

// The name a program writes for a type, which is the last piece of the one it
// is registered under: a type declared in `examples.flags` is `State` there.
// One place, because a lend matches on it and a message prints it.
const char *kest_type_written(const KestType *type);


// The closest declared name, or NULL when nothing is close enough to be worth
// putting in front of a reader. A wrong suggestion costs more than none.
const char *kest_nearest_global(KestProgram *program, const char *name,
                                size_t length);
const char *kest_nearest_member(const KestType *type, const char *name,
                                size_t length);

// Error types compare equal to everything, so one bad annotation reports once
// rather than at every use of what it annotated.
// Whether a type is the narrower float, which is what decides rounding: `f32`
// and `f64` are different instructions because rounding to the narrower one is
// part of what the type means. Asked where a constant is worked out and where
// one is compiled, and it was two functions with one body -- the day either
// moved, a constant folded and a value compiled would have rounded differently
// and the same program would have answered two ways. See D768.
bool kest_is_narrow(const KestType *type);

// One of the language's `vec2`, `vec3` and `vec4`, which are structs with
// operators. See D1250.
bool kest_is_vector(const KestType *type);

// And whether it is a whole number with no sign, which decides which way a
// comparison, a shift and a widening go. Two bodies for that as well.
bool kest_is_unsigned(const KestType *type);

// Whether it is a float at all, which is what says arithmetic on it is the
// machine's floating instruction rather than its whole-number one.
bool kest_is_float(const KestType *type);

// And whether one value of it is a run of slots rather than one, which is what
// says a comparison of two of them walks memory and a hash of one does. Here
// rather than in a backend because both backends ask it, and the same question
// answered in two files is two answers the day either moves.
bool kest_is_a_run(const KestType *type);

bool kest_type_equal(const KestType *a, const KestType *b);

// What a constant is worth, worked out from what it is written as: a number, a
// truth or a piece of text, and arithmetic on those and on other constants.
// False when it is not one of those, and then `why` says which of the two ways
// it was not when there is one to name.
//
// One of these, because the compiler pushes the value and a `[T; N]` counts
// with it, and two would be two answers about one constant.
// How many slots it wrote, or nought when it is not one of those. A struct is
// a value laid out flat, so a constant that is one fills a slot per scalar in
// it and the caller says how much room it has.
// What a number becomes when it is read as a scalar of another kind. Two
// things a program can write and this compiler does in two places — the machine
// runs one and the folder works out the other — so they are one thing here
// rather than one each. D668 holds the two to each other; this is what makes
// there be nothing to hold. See D669.
//
// `kest_narrow_to` cuts an integer to a width, sign extended or zero extended,
// which is how every integer is kept in a slot. `kest_real_to_int` stops at the
// end of the width rather than leaving what C leaves undefined outside it, and
// answers nought for a number that is not one.
// What one value of this type is where memory is shared, which is also the
// width its arithmetic is cut to.
// Which of the kinds a layout can hold this type is read through. Written
// here rather than in the file beside the rest of the type questions, and for
// the reason D868 moved `kest_narrow_to`: the machine runs one of these for
// every scalar of every element it packs or unpacks, and a call across a file
// for a switch was four per cent of `bench/rules.kest`. See D1028.
static inline uint8_t kest_scalar_of(const KestType *type) {
    switch (type->tag) {
    case KEST_T_BOOL:
        return KEST_L_BOOL;
    case KEST_T_FLOAT:
        return type->width == 32 ? KEST_L_F32 : KEST_L_F64;
    case KEST_T_INT:
        switch (type->width) {
        case 8:
            return type->is_signed ? KEST_L_I8 : KEST_L_U8;
        case 16:
            return type->is_signed ? KEST_L_I16 : KEST_L_U16;
        case 32:
            return type->is_signed ? KEST_L_I32 : KEST_L_U32;
        default:
            return type->is_signed ? KEST_L_I64 : KEST_L_U64;
        }
    // A place in a store, which is a number rather than a machine word: the
    // slot it names and how many times that slot has been handed out, packed
    // into one. A host reads it through `integer`, and until it said so it was
    // one kind with the handles it is handed beside. See D715.
    case KEST_T_REF:
        return KEST_L_REF;
    // Bytes rather than a handle, which is the difference a host reading a
    // frame cannot make out of a width. See D896.
    case KEST_T_TEXT:
        return KEST_L_TEXT;
    // Which function of the program this is, which is a number: the machine
    // calls through it by reading `integer`, and a layout that said `word`
    // was telling a host to read that number as a pointer. See D897.
    case KEST_T_FN:
        return KEST_L_FN;
    // A set of named bits, at the width it was declared over: what it holds is
    // the bits it has names for, and a byte is a byte. See D897.
    case KEST_T_FLAGS:
        switch (type->width) {
        case 8:
            return KEST_L_FLAGS8;
        case 16:
            return KEST_L_FLAGS16;
        case 32:
            return KEST_L_FLAGS32;
        default:
            return KEST_L_FLAGS64;
        }
    default:
        return KEST_L_WORD;
    }
}


// And the one round of a mixer a number is hashed with, which is the machine's
// and the folder's alike: a constant that hashes a number is worked out where it
// is written and has to answer what the machine would have. See D670.
// The number standing for a value, over the same parts that decide whether two
// of them are equal. The machine asks it of what is on its stack and the folder
// asks it of what it worked out, and they are one walk rather than two. See
// D671.
uint64_t kest_hash_value(const KestType *type, const KestValue *slots);

uint64_t kest_mix(uint64_t bits);

// Written here rather than beside the rest of them, and written once. The
// machine runs one of these for every `+`, `-` and `*` on a number narrower
// than a slot, and a call across a file for a switch of six cases cost more
// than the dispatch that reached it: a loop hop went from sixteen nanoseconds
// to twelve when this stopped being a call. See D868.
static inline int64_t kest_narrow_to(uint16_t scalar, int64_t value) {
    switch (scalar) {
    case KEST_L_I8:
        return (int8_t)value;
    case KEST_L_I16:
        return (int16_t)value;
    case KEST_L_I32:
        return (int32_t)value;
    case KEST_L_BOOL:
    case KEST_L_U8:
        return (uint8_t)value;
    case KEST_L_U16:
        return (uint16_t)value;
    case KEST_L_U32:
        return (uint32_t)value;
    default:
        return value;
    }
}

int64_t kest_real_to_int(uint16_t scalar, double value);

// What an expression is worth, worked out where it is written, in as many slots
// as the value takes. Nought when it is not worked out here, and then `why`
// says what stopped it and `never` says which of the two kinds of stop it was:
// what was written cannot be worked out — made of itself, divided by nought —
// or the language does not work this kind of thing out at all, which a choice
// and a call into a program are. A reader is told either way; a tool sorting
// refusals needs them under two codes, because one is a mistake to fix and the
// other is a rule to write around. `never` may be NULL. See D673.
uint32_t kest_fold_const(KestProgram *program, const KestExpr *expr,
                         KestValue *out, uint32_t room, const char **why,
                         bool *never);

// Whether a value of this type can be written as text, which is what a hole in
// a string holds and what the command line prints when it calls something.
// Only what has one obvious spelling: a struct has several and the author
// knows which one they meant. `without` names the type that has none, for the
// message. One rule, because the compiler refusing one and the command line
// printing one would be two answers.
bool kest_type_has_text(const KestType *type, const KestType **without);

// Whether anything in this type is a pointer into the machine's own memory:
// a piece of text, a handle to an array or a store, a reference, a function
// value. Those are what a lend cannot carry — the bytes are the host's, and a
// pointer in them is one the machine can neither vouch for nor take back.
// `what` names the one that is, for the message.
bool kest_type_holds_own(const KestType *type, const KestType **what);

// The spelling used in diagnostics: `i32`, `[Player]`, `ref<Npc>?`.
const char *kest_type_name(KestArena *arena, const KestType *type);

// The same, cut where a reader stops reading, for a message that names a type
// it cannot promise is short. See D1248.
const char *kest_type_name_read(KestArena *arena, const KestType *type);

// What a program writes instead of a name the library had and has not, said
// as a sentence, or NULL for one it never had. `alias` is the word the file
// wrote in front of it. See D1252.
const char *kest_retired(KestProgram *program, const char *alias,
                         size_t alias_length, const char *name,
                         size_t length);

// Prints what was resolved, for seeing what the checker built.
// What the program holds, for a person. The file that was named is written out
// in full and what it imported is a line each, because a reader came for the
// one in front of them; `--json` holds all of it either way.
bool kest_program_dump(const KestProgram *program, KestArena *arena,
                       const char *root, FILE *out);

// The same, as JSON: what a tool asks when it wants to know what is in a
// program rather than what is wrong with one.
// What the promises' proof found about every body in this program, written for
// a person: one line each, saying what it reaches and what it could promise and
// does not. There are no durations in it -- a count of instructions is not a
// time, and one printed as though it were is a number nobody measured. See
// D976.
void kest_program_costs(const KestProgram *program, FILE *out);

void kest_program_dump_json(const KestProgram *program, KestArena *arena,
                            FILE *out);

#endif
