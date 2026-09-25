#ifndef KEST_H
#define KEST_H

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

// What this is, in the one place it is said. Everything else that names a
// version reads it from here: the Makefile cuts it out of this line for the
// name of a release archive, and `tools/check-docs.sh` holds every document
// that prints it to what a run of `kest --version` says. A number written
// twice is two numbers the day one of them moves. See D998.
#define KEST_VERSION_MAJOR 0
#define KEST_VERSION_MINOR 0
#define KEST_VERSION_PATCH 1
#define KEST_VERSION_STRING "0.0.1"

// What shape the JSON every command writes is in, which is a different thing
// from the version above: a compiler that has moved on in ways no tool can see
// writes the same objects, and a field that changes meaning or goes away is a
// tool reading the wrong thing whatever the compiler calls itself. Every object
// a command writes begins with it, so a tool reads one number before it reads
// anything it has to understand.
//
// It goes up when a field changes what it means, is taken away, or is added
// where a reader was told the list was everything. See D947.
#define KEST_JSON_SCHEMA 4

// What shape the doors below are in, which is a third thing again. A host is
// compiled against this header and linked against a library built from some
// other copy of the tree, and nothing in C notices: a struct that gained a
// field, a function whose arguments changed, an enum with a case inserted in
// the middle are all a host reading memory that means something else. So the
// number is here for the host's compiler and `kest_abi_version` is there for
// the library, and a host that compares them is a host that finds out before
// it crosses rather than after.
//
// It goes up when anything a host can see changes: a function's arguments or
// what it answers, a struct's fields or their order, an enum's cases or their
// numbers, or what any of them mean. It does not go up for a new function or
// a new enum case added at the end, which a host built against the older
// number does not know about and cannot be hurt by. See D974.
#define KEST_ABI_VERSION 4

// What deterministic code is held to, named and numbered. `deterministic` is a
// promise about a profile rather than about arithmetic in the abstract: which
// operations are in it, how each rounds, what is refused. A host that keeps a
// replay or ships a save reads this and writes it down beside them, because a
// run under one profile and a run under another are two runs. See D974.
#define KEST_PROFILE_NAME "kest-det"
#define KEST_PROFILE_VERSION 3

// Which edition of the language a program is written in, which is a fifth
// thing and the one a program chooses: a change that would stop a program
// compiling, or make it mean something else, is made under a new edition, and
// a project's `edition` line says which one it was written against. A project
// that says none was written against the first, so what a manifest meant the
// day it was written is what it means for good. This is the only edition
// there is. See D1252.
#define KEST_EDITION 2026
#define KEST_EDITION_STRING "2026"

// Every function declared here is called by one of the two hosts written
// against it, so there is somewhere to look for each: `src/main.c` is a
// command line — it compiles, runs, calls one function, ticks a program and
// reports — and `examples/embed.c` is an engine, which keeps a world between
// frames, lends its own memory, binds what a program asks of it and watches
// what a frame costs. `tools/check-dead.sh` holds that to being true rather
// than leaving it a claim.

// Returns the version this library was built as, for a host that links against
// a Kest it did not compile itself.
const char *kest_version(void);

// What shape the doors are in, as the library was built. A host compares it
// with `KEST_ABI_VERSION`, which is what its own compiler read, and the two
// differing means the header and the library are two versions of this project:
// everything below is a promise the library did not make. See D974.
uint32_t kest_abi_version(void);

// What deterministic code in this library is held to: the profile's name and
// its number, which together are what a replay or a save is written under. A
// host that keeps either writes this down beside it. See D974.
const char *kest_profile(uint32_t *version);

// And whether that library checks itself: built under a sanitiser, with the
// readings that only that build does. A host asks this rather than asking its
// own compiler, because the two are not the same question — a host compiled one
// way may be linked against a library compiled the other — and because the
// compilers do not spell the question the same, so a host that asks for one
// spelling gets the wrong answer under the other. This library asked once, in
// the one place that knows.
//
// What a host does with it is decide whether to run what only a checked build
// catches, and whether to keep away from what a checked build catches first.
// See D828.
bool kest_checked(void);

typedef struct KestBuild KestBuild;

// How what the boundary says is written. Prose for a person, and the same set
// as JSON for whatever reads it after: an editor, a build, a model repairing
// what it wrote. Nothing is in one form and not the other.
typedef enum {
    KEST_FORM_TEXT,
    KEST_FORM_JSON,
} KestForm;

// A file handed to a build rather than read by it: where the build would have
// found it, and what it holds. A release binary carries its program this way,
// so it runs where there is no source at all. See D1172.
typedef struct {
    const char *path;
    const char *text;
    size_t length;
} KestFile;

// A runtime value carries no tag. The language is statically typed, so an
// instruction knows what it is operating on and a host function knows what it
// was declared to take.
// Every member of this is the whole of a slot, and a `bool` crosses as
// `integer`, nought or one. A member narrower than a slot would be one a host
// could read and could not write: writing a byte of a union leaves the other
// seven holding whatever was in them, and what the machine reads is the whole
// slot, so `false` written that way arrives as true. See D557.
typedef union {
    int64_t integer;
    double real;
    const char *text;
    void *object;
} KestValue;

// The bytes of a piece of text a slot holds, and how many there are. It is the
// door to read one through: `text` above is what this language keeps text as
// today — a run of bytes ending in a nought, on the machine's heap — and a
// host that reads the member is a host written against that, where one that
// asks is a host written against text. The two are the same bytes now and the
// second is the one that survives this language carrying a length beside them.
//
// `length` may be NULL for a host that only wants the bytes, and what it
// answers costs nothing: how many bytes there are is the second slot of the
// two a piece of text is, so this reads it rather than measuring. Nothing and
// nought for a slot with no address in it. See D964.
//
// What it hands back is not always a C string. Text the machine made ends in a
// nought and text cut out of the middle of some does not, because a cut is a
// place inside what it was cut from and nothing is copied. A host that wants
// one hands the bytes and the length to whatever it is calling, or copies
// them. See D964.
//
// Asked of a slot the program says is text: a slot carries no tag, so eight
// bytes that are a number are not an address. `kest_frame_layout` is what says
// which slots of a frame are text. See D955.
const char *kest_text_bytes(const KestValue *value, uint32_t *length);

// What one scalar inside a value is, where memory is shared.
typedef enum {
    KEST_L_I8,
    KEST_L_I16,
    KEST_L_I32,
    KEST_L_I64,
    KEST_L_U8,
    KEST_L_U16,
    KEST_L_U32,
    KEST_L_U64,
    KEST_L_F32,
    KEST_L_F64,
    // A handle: an array, a store, a function value — a machine word as it is,
    // read and written through `object`.
    KEST_L_WORD,
    // And a piece of text, which is a machine word too and is not read like
    // one. It said `KEST_L_WORD` until D896, which is the width and not what
    // it is: a host handed a frame saw the same kind for `text`, for `[i32]`
    // and for `store<T>`, and the answer that came with it — read it through
    // `text` or `object`, whichever the type is — sent a reader to the
    // declaration for the one thing a layout is for. Reading a store's handle
    // through `text` is a `strlen` over the machine's own memory, and nothing
    // between the two says so. The fifth kind this one was hiding, after the
    // tag, the byte an optional keeps, a truth and a reference.
    KEST_L_TEXT,
    // What a case of an enum carries, which the tag beside it says. A host
    // reading a layout switches on the tag and knows what is there; what it
    // must not do is read it as the word a handle is, which is what this said
    // before it had a name of its own.
    KEST_L_PAYLOAD,
    // And the tag itself: four bytes, read and written as a whole number, and
    // the one piece of a value with a tag in it whose meaning does not depend
    // on another. It said `KEST_L_I32` until D708, which is what it is and not
    // what it means — so a layout could not say where the tags in it were, and
    // a host walking one could not tell the tag of a field from a number
    // beside it. Every other piece says what it is; this one says it too.
    KEST_L_TAG,
    // And the byte an optional keeps after its value, saying whether the value
    // is there. One byte, read and written as a whole number, and the same
    // reason as the tag beside it: it said `KEST_L_U8` until D714, which is
    // what it is and not what it means, so `struct { at: i32?, n: i32 }` and a
    // number, a `bool` and a number were one run of pieces — same kinds, same
    // offsets, same size — and a host could lend either under the other's name.
    KEST_L_HELD,
    // And a truth, which is the same byte again and the last of the three this
    // kind had hidden. It said `KEST_L_U8` until D839, which is what it is and
    // not what it means: a `u8` holds 0 to 255 and a truth holds one of two,
    // so a host writing 7 wrote a value the program reads as true where it
    // asks `if`, and as neither where it asks `== true`. A host that knows
    // which it is writing writes 0 or 1, and one that does not is told. See
    // D839.
    KEST_L_BOOL,
    // The one slot a shape with nothing in it takes. A struct with no fields
    // is one slot of nought (D807) and nothing wrote the piece that says so —
    // the array was as many pieces as slots and the walk filled none of them,
    // so what a host read was the first kind there is, out of a block the
    // arena had zeroed. A layout said `i8`, which is a byte a host may write
    // anything into, about the one value that type has. What a host writes
    // there is not weighed, and that is the one thing about this kind that is
    // not a mistake: the shape has no fields, so nothing in the program ever
    // reads the slot. See D898.
    KEST_L_NOTHING,
    // And a set of named bits, at each of the four widths one may sit over.
    // These said `KEST_L_U8` and the rest until D897, which is the width and
    // not what it is: a `u8` holds nought to 255 and a set of two names holds
    // four values, so `struct { m: Marks, n: i32 }` and a byte beside a number
    // were one run of pieces — same kinds, same offsets, same size — and a
    // host could lend either under the other's name. Four kinds rather than
    // one, because a flag set is the width it was declared over and a host
    // laying out its own memory needs that as much as it needs the meaning.
    KEST_L_FLAGS8,
    KEST_L_FLAGS16,
    KEST_L_FLAGS32,
    KEST_L_FLAGS64,
    // A function value, which is not a machine word either: it is which
    // function of the program this is, a number the machine reads through
    // `integer` and calls through. It said `KEST_L_WORD` until D897 — so a
    // layout told a host to read it through `object`, which is a pointer made
    // out of an index, and the one the program handed over first is the null
    // one. The same mistake `KEST_L_REF` was, in the last place it was left.
    KEST_L_FN,
    // A place in a store, which is not a machine word at all: a reference is
    // the slot it names and the number of times that slot has been handed out,
    // packed into one whole number. It said `KEST_L_WORD` until D715, and the
    // answer that came with that — read it through `text` or `object` — was a
    // pointer made out of a number nobody meant as one. A host reads and writes
    // this through `integer`, and what it is for is telling it apart from the
    // handles it used to be one kind with.
    KEST_L_REF,
} KestScalar;

// And which member of a `KestValue` a slot of one of those kinds is written
// and read through, which is the other reading of the same enum. The kinds are
// the type's own widths — what a piece of it is where memory is shared — and a
// slot is eight bytes whatever that width is, so a host that writes the width a
// kind names writes one byte into eight and the machine reads the other seven.
// See D557: that is the mistake this says out loud instead of leaving to a
// reader.
typedef enum {
    // `KEST_L_F32` and `KEST_L_F64`: `real`, a `double` in the slot either
    // way, which is what a layout of an `f32` array is not.
    KEST_S_REAL,
    // `KEST_L_WORD`: `object`, which is what a handle is. `KEST_L_REF` and
    // `KEST_L_FN` used to be ones of these and are not words: each is a
    // number, and each answers `KEST_S_INTEGER` like every other number. `KEST_L_TEXT` used to be one
    // too, and answers `KEST_S_TEXT`: every kind names one member now, which
    // is what makes this answer worth asking for. See D896.
    KEST_S_WORD,
    // `KEST_L_TEXT`: `text`, which a host reads as the bytes it is and hands
    // over with `kest_text`.
    KEST_S_TEXT,
    // `KEST_L_PAYLOAD`: what the case carries, which the tag beside it says. A
    // host reads the tag first and asks this about the type that came with it.
    KEST_S_TAGGED,
    // Every other kind, however narrow: `integer`, and a `bool` is nought or
    // one.
    KEST_S_INTEGER,
} KestSlot;

// Total for every kind a layout is made of, so a host walking one has an
// answer for each piece rather than for the ones it thought of.
KestSlot kest_slot_of(uint8_t kind);

typedef struct {
    uint16_t offset;
    uint8_t kind;
    // What the program calls this piece, as a path from the value a host is
    // asking about: `x` for a field, `where.x` for a field of a field,
    // `cells[2]` for one of a run laid out where it stands. NULL where a piece
    // is not a named thing -- a scalar asked about on its own, a slot a case
    // carries, the byte that says whether an optional is there.
    //
    // A host doing schema work -- a save format, a reloader, something
    // matching a program against bytes it already has -- was reading names out
    // of `check --json` and bytes out of here, which is two doors for one
    // question and two things to keep in step. See D946.
    const char *name;
} KestPiece;

// How a value is laid out in memory, as against how it sits on the stack. One
// piece per slot, in slot order, so unpacking an element is a walk of this.
//
// A tagged union has no one piece per slot: which type a payload slot holds
// depends on the tag. A value that holds one anywhere says so, and is moved by
// reading the tag first rather than by walking the pieces.
typedef struct {
    const KestPiece *pieces;
    uint16_t count;
    // How many slots the value is, which is not how many pieces it has: a
    // piece of text is one piece and two slots. See D964.
    uint16_t slots;
    uint16_t size;
    uint16_t align;
    const void *type;
    bool tagged;
    // Whether what a host writes into this has to be read by its type rather
    // than by its pieces. A piece says how wide a slot is and what it is read
    // through, and for most values that is the whole of what can be wrong —
    // but a handle is a handle of something, a tag is a tag of an enum, and a
    // set of bits is a set of named ones, and none of those is a thing a
    // width can say. The machine reads this rather than looking at the pieces
    // every call. See D840.
    bool by_the_type;
    // The machine's own: a value written out once as the runs of reads and
    // writes moving one takes, so moving one is a walk of that rather than of
    // its type or of its pieces. Nothing a host reads. See D1159 and D1177.
    const void *walk;
} KestLayout;

// A number that moves when this shape does: its size, what it is aligned to,
// whether anything in it is a tag, for every piece where it sits, what is
// there and what the program calls it, for a tag which cases it can name and in
// what order, and for a set of named bits which bits it has and in what order.
// Those two were not in it until D1072 and D1076, and both are a number that
// is a place in a list: a case put in the middle of an enum renumbers every
// case after it, a bit put in the middle of a set doubles every bit after it,
// and in both the size, the alignment and every piece stay exactly where they
// were.
//
// It is what a host doing schema work keeps beside the bytes it saved. After a
// reload, the same number means the same shape and nothing to migrate; a
// different one means a field moved, changed width or changed name, and the
// host either maps the old onto the new or refuses. Folded here rather than
// left to a host, because two hosts folding their own way would have two
// numbers for one shape. Nought for no layout.
//
// What it does not say is which of those changed. A host that needs that walks
// the pieces, which is what they are for. See D948.
uint64_t kest_layout_mark(const KestLayout *layout);

// Which case the tag at a piece of this value names, and what that case
// carries: the name as the program wrote it, and one piece a slot over the
// slots after the tag, at the byte each of them sits at inside the value the
// tag belongs to — which begins where the tag does, so a host that wants those
// bytes inside the whole value adds the tag piece's own offset. It is the
// question `KEST_L_PAYLOAD` leaves open: a payload slot's kind is the tag's to
// say, so the layout cannot say it and this can, once a host has a tag to ask
// about.
//
// `piece` is the piece the tag is, which `KEST_L_TAG` says. A value that is an
// enum has its tag at piece nought; one inside a shape has it wherever the
// fields in front of it end, and either is asked about the same way.
//
// A host filling a frame with an enum writes the tag into that slot and then
// has to know which member of a `KestValue` each slot after it is;
// `kest_slot_of` over these kinds says so, the same as for anything else. A
// host that keeps its own numbers for the cases holds them against this by
// walking the tags up from nought and reading the names.
//
// NULL when the piece is not a tag and when the number is no case of the enum
// whose tag it is. A case that carries nothing answers its name with nought
// pieces. `carries` and `count` may both be NULL for a host that only wants
// the name.
const char *kest_case_of(const KestLayout *layout, uint16_t piece, int32_t tag,
                         const KestPiece **carries, uint16_t *count);

// The name of the one function this language knows about. The checker holds a
// function of this name in the file that was named to the shape a host can
// call — nothing in, a number or nothing back — so a host that wants to call
// the entry point writes this rather than a string of its own.
#define KEST_MAIN "main"

// As much as usual, which a host can say by name rather than by picking a
// number of its own. It is what a machine takes when the program has no answer
// — one that reaches itself, or that calls through a value — and it is what it
// took for everything before D575.
#define KEST_STACK_SLOTS 65536
#define KEST_CALL_DEPTH 1024

// What the machine is allowed. Zero for either of the first two is what the
// program asked for: the worst any function needs, plus the worst call back
// into the program from inside a host function, because a machine does not
// know which function a host will call. A program with no deepest call has no
// number to give, and then zero is the two numbers above. For the heap, zero
// is whatever the host itself can spare.
//
// A host that has no opinion is the one this is for. `kest_needs` is half of
// that question asked before there is a machine: it answers the worst any
// function needs and not the way back in, so a host that asks it and writes
// the answer here gets a smaller machine than one that writes nothing. Adding
// `kest_needs_from` with no name is the whole of it, and the two added is
// exactly what zero here means. A host that wants more than the program asked
// for says so here — a machine that runs out says what it would have needed,
// so a host that finds out here is told what to write.
//
// The heap is the one of the three that grows while a program runs, so it is
// the one a host watching a frame budget puts a number on: crossing it is a
// message at the instruction that asked, in the same shape as anything else
// that fails while running, rather than a machine that has taken the memory
// the host wanted for something else.
//
// `fuel` is the fourth and the only one of them that bounds *time* rather than
// memory: how many instructions the machine may run before it stops. Nought is
// no ceiling, which is what a machine has always had. A host that runs code it
// did not write needs this one — `while true {}` is a program, the compiler is
// right to accept it, and without a budget it is a frame that never ends.
typedef struct {
    uint32_t stack_slots;
    uint32_t call_depth;
    size_t heap_bytes;
    uint64_t fuel;
} KestLimits;

// No ceiling on how long a program runs, which is what `KestLimits.fuel` means
// when it is nought and what every machine had before there was a fuel field.
#define KEST_FUEL_UNLIMITED 0
// Why there is a least, or why there is not. A run of calls that comes back
// round has no deepest frame, and a call through a function value reaches
// something that is not known until it runs; those are two different things to
// be told, because one is a shape a host can change and the other is a number
// a host has to pick.
typedef enum {
    KEST_REACH_KNOWN,
    KEST_REACH_ITSELF,
    // A call through a value, where the program turns none of its own
    // functions into one. Which function such a call enters is not known
    // here, but which ones it could enter is: a function becomes a value in
    // one place, and the answer counts the worst of the ones that ever do. A
    // program with none of them can only have been handed one by a host, and
    // then there is nothing to count. A host that hands in something wider
    // than the program's own is told so at the call, in the same words as any
    // other machine asked for more than it was given. See D814.
    KEST_REACH_VALUE,
    // The program defines no function of that name, which is the same news
    // `kest_entry` gives with -1: a host asking about one it cannot call.
    KEST_REACH_NO_NAME,
    // There was no room to work it out. The answer is not that there is no
    // answer — a host that frees something and asks again may be told one.
    KEST_REACH_NO_ROOM,
    // Nothing was asked: a host that handed over nothing to answer about, or
    // a reason nobody has written into yet. A build that did not compile is
    // not one of these and never was — `kest_build` answers NULL for one, so
    // a host holding a build is holding one that compiled. See D566.
    KEST_REACH_UNASKED,
} KestReach;

// What working the least out found, and the function it found it in. The name
// is the program's own: one that takes something carries what it takes, which
// is how one copy of a generic is told from another.
typedef struct {
    KestReach reach;
    const char *where;
} KestReason;

// The least this program can be given, worked out from what it calls. It is
// enough for every function the host could call, not the least for one of
// them, because a host does not want a different answer per call site.
//
// False when there is no answer, and `why` says which of the reasons above it
// was and where. A host that gets false picks a number and finds out, which is
// what every host did before this; `why` may be NULL for a host that only
// wants to know whether to.
//
// It answers for one call in. A host whose bound function calls back in adds
// room for what that starts, because how many times it will is the host's to
// know and not the program's.
//
// The heap it answers is nought, and so do the two below: what a program
// allocates is what it is given to work on, and a loop over four events and a
// loop over four thousand are the same program. Nought is written rather than
// left alone, because a field an answer does not touch is one a caller cannot
// tell from one it did — a host's own cap goes on after asking, and what a host
// that has not measured one has is a machine that says what it reached for when
// it runs out. See D724.
bool kest_needs(KestBuild *build, KestLimits *least, KestReason *why);

// The least for one function and what it reaches, for a host that knows which
// ones it calls. `name` is what the file wrote, with or without the module in
// front of it, which is the name `kest_entry` takes.
//
// False when there is no such function, as well as for the two reasons above.
// A host that calls several asks about each and takes the largest, because
// which of them it will call and in what order is the host's to know.
bool kest_needs_of(KestBuild *build, const char *name, KestLimits *least,
                   KestReason *why);

// The most this program can want, for a host with no names and a ceiling on
// frames. It is `kest_needs` where there is a least and a bound where there is
// not — the same shape as the two below, and `why` says which it gave.
//
// It is what a machine given nothing is sized by, said before there is one: a
// host that wants to know what saying nothing will cost asks this, and gets the
// number the machine would have picked.
//
// False for the reasons `kest_needs` is false.
bool kest_bound(KestBuild *build, uint32_t frames, KestLimits *most,
                KestReason *why);

// The most `name` and what it reaches can want, for a host that has a name and
// a ceiling on frames rather than a program with a least. `frames` is how many
// a host will allow; nought means as many as usual.
//
// A name whose stack can be worked out answers what `kest_needs_of` answers
// and the frames are not looked at: there is a least, and a bound above it is
// a number nobody needs. A name that reaches a run of calls that comes back
// round has none, and what this gives instead is the smaller of two readings
// of a chain of frames — the widest body it reaches, once a frame, and the
// bodies that go round once a frame with the bodies that do not paid for once.
// Both are true of any chain, so the smaller is.
//
// It is a bound and not a least: a machine made from it is big enough and may
// be bigger than anything the program reaches. A host that wants to know which
// it was asks `kest_needs_of` first — this answers true either way, and `why`
// says which, `KEST_REACH_KNOWN` for a least and `KEST_REACH_ITSELF` or
// `KEST_REACH_VALUE` for a bound.
//
// False for the same reasons `kest_needs_of` is false, and for the same name.
bool kest_bound_of(KestBuild *build, const char *name, uint32_t frames,
                   KestLimits *most, KestReason *why);

// Where the machine already is when it calls into the host: the frames and
// slots a machine is holding where `name` reaches a host function: the widest
// the function itself ever gets, plus the worst of the same over everything it
// reaches on the way to one. It is the function's widest rather than the width
// at the call, so a long expression anywhere in the body is in it whether the
// host is called before that expression or after — which makes the number
// large enough always, and makes shortening the line the call is on the wrong
// thing to shorten. A host function that calls back in with `kest_call` starts
// from there and not from nothing, so what a re-entrant host needs is this plus
// what the entry it calls needs on its own — `kest_needs_of` for that one,
// added to this.
//
// Both are nought when nothing `name` reaches calls into the host, and then
// there is nowhere to call back in from. The heap is not part of it: it does
// not nest.
//
// `name` may be NULL, and then the answer is over every function the program
// defines, which is what a host that calls more than one has to be given.
//
// On true `why->where` names the function the deepest call into the host is
// in, and is NULL when nothing reaches one. That is the function a host would
// have to shorten to make the number smaller, and asking about it by name
// answers what it reaches the host at on its own, which is less than this.
// Shortening it means making the function narrower wherever it is widest,
// which is not always where the call is.
//
// False for the same reasons `kest_needs_of` is false, and for the same name.
bool kest_needs_from(KestBuild *build, const char *name, KestLimits *inside,
                     KestReason *why);

// And where a host may be called back in from, for a name with no least. It is
// `kest_needs_from` where there is one and a bound where there is not, the same
// way `kest_bound_of` is `kest_needs_of` — `why` says which, and a host that
// asks this of a program that can answer the other gets the other.
//
// The bound counts only the bodies that reach a host function: a body that
// never reaches one cannot stand in a chain of frames that ends at a host call,
// above it or below it. `name` may be NULL for every function the program
// defines, the same as `kest_needs_from`.
//
// What a re-entrant host wants is this plus `kest_bound_of` for the entry it
// calls, which is the same sum as for a program with a least and made of the
// same two doors.
bool kest_bound_from(KestBuild *build, const char *name, uint32_t frames,
                     KestLimits *inside, KestReason *why);

// The machine, while it is running. A host function is handed one so that it
// can give the program a view of memory the host owns.
typedef struct KestRuntime KestRuntime;

// A function the host provides. Its arguments are the slots at `frame`, laid
// out the way the declaration says, and it writes its result over them. A
// value of more than one slot occupies that many, so a `Vec3` argument is
// three and a returned one replaces the first three.
// `context` is whatever was given when the function was bound, which is how a
// host reaches its own state from inside one.
typedef void (*KestNative)(KestValue *frame, KestRuntime *runtime,
                           void *context);

// Hands the program a piece of text. The bytes are copied into the machine's
// heap, which is where the program's own text lives, so nothing is promised
// about the host's copy afterwards: a host that handed a pointer of its own
// would be undertaking to keep it as long as the program holds it, and a
// program holds a piece of text for as long as it likes.
//
// Text ends at its first zero byte, so a zero inside `length` is a mistake
// rather than a cut: it is `K0611` and what is written is empty. So is what is
// written for no address to copy from, and for a heap with no room to copy
// into, and this answers false for each of the three; which of them it was is
// in the report. An empty piece of text is what a host asking for one gets,
// and that answers true.
//
// It writes two slots, because a piece of text is two: what it is made of, and
// how many bytes that is. `into` is where the first goes. See D964.
bool kest_text(KestRuntime *runtime, const char *bytes, uint32_t length,
               KestValue *into);

// Who owns what, which is the whole of what this library promises about
// threads. A build is read-only once it has been built, with two exceptions
// written down below: the program, the layouts, the text a diagnostic points
// at, all of it is written once and read by every machine after. The field of
// it a machine writes as it starts is the count of how many are standing on
// it, and that is an atomic, so machines may be started and freed from any
// thread.
//
// The two exceptions are both a host's to keep to. A start that *fails* writes
// why into the build's report, so two of those at once are two threads writing
// one report (D1071). And a debugger writes the program itself: a breakpoint
// is an instruction written over, in the build, so every machine of that build
// runs into it -- debug a build no other machine is standing on (D1077, and
// `kest_code_of` says it again where a host writes the byte).
//
// A machine is one thread's while it runs. Everything it changes is its own --
// its heap, its stack, its stamps, its world, its report -- so two machines of
// one build may run at once on two threads and neither can see what the other
// is doing. What may be done to a machine from another thread is ask it to
// stop: `kest_cancel` is one store of one word and is written to be done from
// a signal handler or from another thread while the machine runs. Everything
// else here is the owning thread's.
//
// What this library does not do is lock anything. Two threads calling into one
// machine is two threads writing one stack, and nothing here will tell you.
// See D952.

// A machine that did not start is a machine with nothing in it. `kest_start`
// answers NULL and writes why into the build's report, which is where a host
// finds out; a host that carries on regardless gets, at every door below, the
// answer a machine gives when it has nothing — no entry, no layout, nought
// bytes, an empty piece of text, and false. There is nowhere to say more than
// that, because a report belongs to a machine and there is none. See D894.

// Hands the program an array over memory the host owns. Nothing is copied and
// nothing is freed: the caller keeps the block and must outlive the program's
// use of it.
//
// `element` is what the program calls the type, and `size` is what this host
// thinks one is. The stride comes from the program, so it cannot be wrong;
// `size` is here to be disagreed with. A host that has a different idea of the
// shape is told, and gets a value whose `object` is NULL, rather than reading
// the block as something it is not.
//
// What it may hold is numbers. A shape holding text, an array, a store, a
// reference or a function value is refused with `K0647`: those are pointers
// into the machine's own memory, and one sitting in the host's block is one
// the machine did not put there, cannot vouch for, and cannot take back when
// the lend ends. Hand those over a frame instead, where `kest_text` makes the
// text the machine's.
KestValue kest_borrow(KestRuntime *runtime, void *data, uint32_t length,
                      const char *element, size_t size);

// How many there are in an array the machine made or the host lent, and how
// many it has room for. A host driving a world reads back a run of things and
// has to know where it stops: the engine host in `examples/engine.c` walked one
// by asking the program, because there was nowhere to ask the machine. Both
// answer nought for a value that is not an array of this machine's, which is
// the same answer an empty one gives -- a host that needs to tell the two apart
// asks `kest_still_holds` first.
//
// `room` may be NULL for a host that only wants the length.
uint32_t kest_array_length(const KestRuntime *runtime, KestValue array,
                           uint32_t *room);

// What a run of elements is, in memory. This is the one piece of the runtime's
// own memory anything outside it reads directly, and it is here for one
// reader: the C this project's other backend writes (D1093). A read of one
// element was a call to `kest_elem_at`, and a call is a wall the host's
// compiler cannot see past -- it cannot hoist the length out of a loop, it
// cannot keep the block in a register, and it cannot tell that nothing in the
// loop moved the array. With the shape here the fast path is four lines of C
// and the refusals are still the machine's: anything the inline test does not
// like goes through `kest_elem_at`, which says what the machine says.
//
// A host may read it too, and a host that does is a host holding itself to
// `KEST_ABI_VERSION`. Every field of this is what that number is about: this
// struct changing is every generated file in the world reading memory that
// means something else, which is exactly what D974 put the number there for.
//
// `of` is the build's own idea of what one element is, and is nothing a host
// can read. It is here because the shape has to be the whole shape -- a
// struct written out to the field before the last one is a struct whose size
// is wrong. See D1112.
typedef struct {
    uint32_t what;
    uint32_t length;
    uint32_t capacity;
    uint16_t stride;
    // Lent by the host, which means the block is not the machine's to move
    // and the run is not the machine's to grow.
    bool borrowed;
    unsigned char *bytes;
    const void *of;
} KestRun;

// What the first word of one says, for the test that says a handle is a run
// of elements rather than a world of them or a lend the host has taken back.
#define KEST_RUN_IS 0x4b415252u

// What a call keeps while it is running, and where the machine keeps them.
// This is the second piece of the runtime's own memory anything outside it
// reads directly, and it is here for the same one reader the first was: the C
// this project's other backend writes (D1093, D1112).
//
// A call from a body the host's compiler compiled was a call to
// `kest_native_room`, which asked three things and wrote four fields. The
// asking is three comparisons and the writing is four stores; what a call
// cost on top of that is the call -- a wall the host's compiler cannot hoist
// a loop-invariant across, cannot keep a counter in a register over, and
// cannot inline through. Taking it away took 28% of the instructions and 35%
// of the cycles off `bench/rules.kest`. See D1122.
//
// Every field of both of these is what `KEST_ABI_VERSION` is about, for the
// reason the run of elements is: one of them moving is every generated file
// in the world reading memory that means something else. `chunk` is the
// build's own idea of a function and is nothing a host can read; `ip` may be
// NULL for a call that was made from compiled code, which has no
// instructions to point at.
typedef struct {
    const void *chunk;
    const unsigned char *ip;
    KestValue *base;
    // Where in the source this call was made, which a compiled body knows
    // and an instruction does not: nought means "work it out from `ip`".
    uint32_t said_at;
} KestCall;

typedef struct {
    // The calls that are standing, and how many of them there are.
    KestCall *calls;
    uint32_t *many;
    // How many there may be, and how far the slots go.
    uint32_t most;
    const KestValue *limit;
    // How far up the slots are live, for the collector: a call raises it and
    // nothing lowers it, because what is above it is slots nothing wrote.
    KestValue **reached;
    // What the program's functions are, in the order a generated file names
    // them. `calls[n].chunk` comes from here.
    const void *const *chunks;
} KestLedger;

// What reads one of these is `kest_ledger`, which is not a door a host is
// given: a host that wanted one would be asking how the machine keeps its
// calls, and what a host is given about that is `kest_needs_from` and what a
// refusal says. The shape is here because it is what the version number is
// about; the door is in the library's own header beside the rest of what a
// generated file calls.

// And the end of a lend, which is the host saying the block is not its to lend
// any more. Nothing is freed: the block was the host's throughout. What
// changes is what the program holds — every use of it afterwards is a message
// at the instruction that used it rather than a read of memory the host has
// moved on from.
//
// A host lending what it owns for a frame calls this at the end of the frame.
// Without it the header says nothing about how long the block was good for,
// and the program's copy of the handle outlives whatever the host did next.
//
// True when the lend was ended. False when the value is not a lend this
// machine gave out, and when it is an array the program made rather than one
// the host lent, which is not the host's to end; both say why into
// `kest_report`.
bool kest_lend_ends(KestRuntime *runtime, KestValue lent);

// A handle to a lend that has ended is dead the moment it ends, and stays dead
// only until the next lend. What a lend costs is a header, and the header a
// lend gives back is the header the next one gets — so a handle kept past the
// end of its lend names whatever was lent after it, and the machine cannot
// tell: a lend handle is a pointer, and unlike a reference into a store it
// carries no stamp to say which lend it is a handle to. Ending a lend is where
// a host drops the handle, not a thing it does before using one more time.
// See D352.

// Whether what a host kept is still the machine's to read. A host function is
// handed the program's values and may keep one past the call: a piece of text,
// an array, a store. They last as long as the heap they are on, which is as
// long as nothing throws it away — and a host that threw it away is the only
// one who knows, which is one thing too many to have to remember.
//
// True while the machine still has the memory it handed out. False after
// `kest_heap_reset`, and false for anything this machine never gave the host.
//
// It says nothing about what is written there: a lend the host itself ended is
// still the machine's memory, and the host that ended it knows it did. What
// this answers is the one thing a host cannot see for itself.
//
// And it answers about the memory rather than about what was in it. A piece of
// text kept across `kest_heap_reset` is gone, and stays gone only until the
// machine makes something: what it makes goes where that was, so the pointer
// is live again and reads whatever is written there now. It is the same shape
// as a handle to a lend that has ended, for the same reason — a pointer
// carries no stamp — so a host drops what it kept when it throws the heap
// away, rather than asking afterwards. See D353.
bool kest_still_holds(const KestRuntime *runtime, KestValue kept);

// And the thing a host has to say before keeping one. What a program makes
// stands on memory the machine gives back when nothing can reach it, and what
// it walks to decide that is its own: the slots of the program that is
// running, and the worlds and runs those name. A handle in the host's own
// memory is not in that walk — the machine cannot read the host's variables —
// so a host that keeps one across a call that allocates is holding a pointer
// to memory the machine has given away.
//
// There are two ways to be right about this, and a host picks one:
//
//   - hand it back in. A handle in the frame of the call is one the walk
//     reads, so a host that passes its world into every call it makes about
//     that world needs nothing else. This is what `examples/engine.c` does.
//   - say so here. The machine then keeps it whatever the program can reach,
//     until `kest_lets_go` or `kest_heap_reset`.
//
// Answers false when there is no room to remember it, and true for anything
// else — including a value that is not the machine's at all, which keeps
// nothing and costs nothing.
//
// This is not reference counting and a host does not have to balance it: one
// value said twice is kept once, and a heap thrown away forgets every one of
// them. See D996.
bool kest_keeps(KestRuntime *runtime, KestValue kept);

// And the other end of it: the machine stops keeping this for the host's sake,
// and it lasts as long as the program can reach it and no longer. Answers
// whether the machine was keeping it.
bool kest_lets_go(KestRuntime *runtime, KestValue kept);

// And which of the two places it is in, which is what the answer above is a
// yes to both of. A host keeping a value between frames is choosing between
// two lifetimes with one pointer in its hand: what the program made while
// running goes when the heap does, and what the program was written with is
// in the build and outlasts every reset. Nothing about the pointer says which,
// and asking afterwards is asking about memory that may already be somebody
// else's — so it is asked before it is kept.
typedef enum {
    // Not this machine's at all: a pointer of the host's own, or one from a
    // heap that has been thrown away.
    KEST_KEPT_NOWHERE,
    // On the heap the program runs on, which `kest_heap_reset` empties.
    KEST_KEPT_HEAP,
    // A lend: a header of the machine's, on that same heap, in front of a
    // block that is the host's own. The two halves do not last the same
    // length of time, and this is the answer that says so — the block is
    // there for as long as the host has it, and the handle in front of it
    // goes with the heap like anything else on one. A lend the host has
    // ended is no longer one of these: the header is the machine's memory
    // and nothing is in front of any more.
    KEST_KEPT_LENT,
    // In the build the machine was started from: text the file was written
    // with, there for as long as the build is.
    KEST_KEPT_PROGRAM,
} KestKept;

// Total, and the one answer `kest_still_holds` is read out of, so the two
// cannot come to disagree about what the machine has.
KestKept kest_kept_where(const KestRuntime *runtime, KestValue kept);


// Calls a function the program defines, by the name it lives under. `frame`
// holds the arguments laid out the way the declaration says and receives the
// result over them, which is the same convention a host function is called
// with, in the other direction.
//
// `entry` is what `kest_entry` gave for the name. `slots` is how many
// `KestValue`s `frame` holds; the program says how many it needs, so a frame
// that is too narrow is a message rather than a read past the end of the
// host's array.
//
// D007 measured the outward crossing as the wider of the two, so the shape to
// reach for is one call carrying a batch rather than one call per item.
// Returns false when the program failed while running, which is reported into
// the diagnostics the runtime was made with.
// `frame` has to be wide enough for whichever is larger, what is passed or
// what comes back, because they are the same slots.
//
// No frame is a frame of no slots, and is what to pass for a function that
// takes nothing and gives nothing back. Anything else is refused rather than
// run on whatever the stack was left holding.
//
// A bound function may call this, which is a run of the machine standing under
// a C frame of the host's own. It ends like any other run: the frames it made
// go with it whether it returned or was refused, and the call it was made from
// carries on and answers what it would have answered. What it cannot do is
// stop -- a breakpoint in a run made this way is `K0708`, because there is
// nothing for a resume to carry on into once the bound function has returned.
// See D1032 and D1079.
bool kest_call(KestRuntime *runtime, int32_t entry, KestValue *frame,
               uint32_t slots);

// Where a function lives in this program, or -1 when there is none of that
// name. Finding a name is a search over everything the program defines, so it
// is done once and a frame calls by what it found. This is also how a host
// asks whether the program defines something, which is why a name nothing
// knows is -1 and nothing else: asking is allowed.
//
// Two names are here and still cannot be handed over, and those say why into
// `kest_report`: a name that is several functions, because two may share one
// when they take different things and because a generic is compiled once for
// each set of types it is used with; and a function the host itself provides,
// which crosses the other way. Both would otherwise send a host looking for a
// typo.
//
// The first names them, and those names are the program's own: `add#i32,i32`
// is what to ask for, and `kest_frame_layout` says what the one that came back
// takes.
//
// The name is the one the file writes. A file that says `module game.world`
// registers its `spawn` as `world.spawn`, and this finds it either way.
//
// A name nothing knows is -1 and nothing else. That is the one answer at this
// boundary that means no and says why nowhere, and it is the one where a host
// asked a question rather than made a mistake: whether a program defines
// something is what this is for. `kest_host_bind` is the other, and for a
// different reason — a host has no report to write into.
//
// What comes back is where a function lives *in this program*, so it belongs
// to the build this machine was started from and to no other. A host that
// reloads asks again for every name it calls: the same name in a program
// compiled again is very likely a different number, and a number from the old
// one is a call into whatever is at that place now, made with a frame the
// program never agreed to. Nothing in the machine can see that a number came
// from somewhere else -- it is a number. What a host can hold it to is the
// shape: `kest_frame_takes` and `kest_frame_layout` say what the function that
// came back wants, and `examples/engine.c` asks both of every door it calls
// across each of its reloads. See D985.
int32_t kest_entry(KestRuntime *runtime, const char *name);

// The one at `at` of the functions of that name, or -1 past the last. A name
// that is one function is that function at nought and nothing after it.
//
// This is what to walk when `kest_entry` says a name is several functions: a
// host asks each of them what it takes, with `kest_frame_layout`, and calls
// the one it meant. It does not have to know how the compiler spells a name
// that carries what it takes — and for a copy of a generic it could not guess
// it, because the spelling holds the types the copy was compiled for as well
// as the ones it was written with. The refusal `kest_entry` gives spells them
// out, for a host that would rather keep the name than walk again.
//
// -1 is past the last and nothing else: however many a name is, the walk
// reaches all of them.
int32_t kest_entry_of(KestRuntime *runtime, const char *name, uint32_t at);

// The name the function at `entry` is compiled under, or NULL for an index
// that is no function — which is what ends a walk from zero of everything a
// program defines. This is the other direction of `kest_entry`: that one turns
// a name a host wrote into an index, and this says what the program calls the
// thing at one.
//
// A host that knows the names it wants asks for them and keeps what it was
// given. One embedding a program it did not write — a mod, a level, a rule set
// — has no list to ask from, and learning the names one refused lookup at a
// time is a walk of every name the program has for each name it guesses. This
// is the list itself, and the spelling is the one `kest_entry` takes back,
// including what a copy of a generic is compiled under. See D609.
//
// It says nothing at the end: a walk ending is not news, the same as the walk
// of what a program asks the host for. Asking what a frame holds is not a walk
// and does say so.
const char *kest_entry_name(KestRuntime *runtime, int32_t entry);

// And the same function as somebody wrote it: the name without what tells one
// copy of a generic from another, so the two copies of `pick` above are both
// `pick` here and the walk says which functions of the list are one function.
// NULL for an index that is no function, the same as the name above.
//
// This is the spelling every message uses, so a host that reads a refusal and
// a host that reads the list are looking at the same word. It is not always a
// spelling `kest_entry` can take back: a name that is several functions is
// refused, which is the refusal that names the copies. What goes back in is
// the name above. See D610.
const char *kest_entry_wrote(KestRuntime *runtime, int32_t entry);

// The four questions below all answer an index that is no function the way
// they answer a real one that takes nothing, gives nothing, or has nothing
// past its last argument: with nought or with NULL. `K0634` is what says
// which, so a host that got -1 from `kest_entry` and asked anyway is told,
// rather than told about a function that takes and gives nothing.
//
// How many arguments this takes, and where the one at `which` starts in the
// frame, in slots. A value is one slot a scalar, so a `Vec2` is two and the
// second one of them starts at two; asking beats counting the fields of the
// first, which is what a host would otherwise be doing with a number the
// program already knows.
//
// `kest_frame_at` answers how wide the arguments are together when `which` is
// past the last one, which is where a result written over them would start.
// The promises a function can make, which are the three this language has. Two
// of them are spelled with a dot in a program — `no.alloc`, `no.host` — so the
// namespace can hold more without taking more keywords, and they are named here
// for the same reason: a host asks about one of them by saying which, and a
// fourth one added to the language is a case added here rather than a door
// added beside the one below. See D857 and D942.
typedef enum {
    // Nothing this function does reaches the heap.
    KEST_PROMISE_NO_ALLOC,
    // Nothing it does calls back out into the host.
    KEST_PROMISE_NO_HOST,
    // Every machine keeping the simulation profile answers the same for it,
    // which is what a host replaying inputs or checking two peers against
    // each other needs said about a body before it runs one.
    KEST_PROMISE_DETERMINISTIC,
} KestPromise;

// Whether the one at `entry` made the promise asked about, which the compiler
// proved against the code it emitted. False past the last function, false for
// one that made no promise, and false for a `which` this language has not.
//
// They are the things about a function a host can act on before calling it: a
// frame step that may reach the heap is one an engine puts somewhere other than
// a frame, or refuses to install at all, and one that may call back in is one a
// host driving a frame from inside its own lock cannot install at all. What a
// program costs in other ways — how many of its values were worked out where
// they stand, how much reading it cost — is in what `--json` prints, because a
// host cannot do anything about those and a tool reading them can. See D680 and
// D857.
bool kest_entry_promises(KestRuntime *runtime, int32_t entry,
                         KestPromise which);

uint32_t kest_frame_takes(KestRuntime *runtime, int32_t entry);
uint32_t kest_frame_at(KestRuntime *runtime, int32_t entry, uint32_t which);

// What the argument at `which` is, or NULL past the last one. It is the same
// layout `kest_build_layout` gives for a type by name, so a host checks an
// argument the way it checks something it lends: the bytes, and where each
// piece of it sits. Writing the right number of slots with the wrong things
// in them is the mistake this is for.
const KestLayout *kest_frame_layout(KestRuntime *runtime, int32_t entry,
                                    uint32_t which);

// And what comes back over them, or NULL when the function gives nothing. The
// same layout again, so a host reads a result knowing what it is rather than
// knowing how wide it is.
const KestLayout *kest_frame_gives(KestRuntime *runtime, int32_t entry);

// A number written into a slot is weighed against the width the program keeps
// it at: a slot is sixty-four bits and an `i32` is thirty-two, and a host that
// writes more is refused with `K0636` at the call — and `K0652` where it is a
// host function answering one, which is the same weighing at the other end. A
// slot holds a double and an `f32` holds less, so an `f32` is weighed the same
// way: what a host writes there is what `f32(x)` would have made of it. Not a
// number is one at either width and goes through. Every width in this language
// wraps at its own end, and a frame is the one place a value it cannot make
// could get in. What a host writes it narrows the way the program would.
//
// What this host is about to write, said back to the program before it writes
// it: one kind a slot, in the order the arguments are laid out, which is the
// order `kest_frame_layout` gives them in. A frame of the right width with the
// wrong things in it is the mistake this is for — `kest_call` can see how wide
// a frame is and not what a host meant to put in it.
//
// `count` has to be every slot the arguments take: saying what some of them
// hold is not checking the rest, and a host that stops short is told so rather
// than told nothing.
//
// The kinds are the ones a layout is made of, and a slot holds what a piece of
// that type holds: a struct of three `f32` is three slots of `KEST_L_F32`,
// each read as a `double` in the slot, which is what the layout said and this
// says again where a host can be wrong about it.
//
// True when they agree. False when they do not, and when there is nothing at
// `entry`; both say why into `kest_report`.
bool kest_frame_fills(KestRuntime *runtime, int32_t entry,
                      const uint8_t *kinds, uint32_t count);

// And what this host is about to read back out of one, which is the same
// mistake in the other direction: a slot holding a float read as a number of
// the host's own is a number nobody wrote. One kind a slot again, over what
// the function gives back, which is nothing at all for one that gives nothing.
//
// `count` has to be every slot it gives back, the same as above and for the
// other reason: a host reading more slots than came back reads the slot above
// the answer, which is the machine's and not the answer's. The width and what
// is in it are two questions, and this asks both.
//
// True when they agree, and false in the same three ways.
bool kest_frame_reads(KestRuntime *runtime, int32_t entry,
                      const uint8_t *kinds, uint32_t count);

// The arguments handed over as words, written the way a program writes them:
// `12`, `1.5`, `true`, and a piece of text as itself. The machine reads each
// one as the type the declaration says and lays them out in `frame`, so a host
// that hands over words has no slots to be wrong about — the other half of
// `kest_gave_text`, which says what a frame holds without a host reading one.
//
// `words` is one a parameter and not one a slot, and `count` has to be how
// many the function takes. `slots` is how wide `frame` is, which has to be at
// least what the function takes and what it gives back, the same as
// `kest_call`.
//
// What cannot be written as a word is refused rather than guessed at: a struct,
// an array, a store, a reference, a handle. A host with one of those lays out
// the slots itself and says what it wrote with `kest_frame_fills`.
//
// True when the frame holds them. False when a word is not what the function
// takes, when there is the wrong number of them, and when there is nothing at
// `entry`; each says why into `kest_report`.
bool kest_takes_text(KestRuntime *runtime, int32_t entry, KestValue *frame,
                     uint32_t slots, const char *const *words, uint32_t count);

// What came back, written the way the language writes a value in a hole: `12`,
// `true`, `Door.Shut`, `State.Moving | State.Armed`. Text on its own is what it
// holds and not the source that spells it.
//
// The number of bytes it needs, not counting the end, whatever `room` was —
// the same answer `snprintf` gives, so a host that got a number too big for
// its buffer asks again with one that fits. `out` holds as much as it can with
// an end on it.
//
// This says what is in the frame rather than putting something there: a host
// calls with `kest_call` and then asks. A frame nothing has been called with
// is `K0632` and minus one, rather than a read of whatever the frame was made
// out of.
//
// Nought less than nothing — minus one — for a function that gives nothing,
// and for one that gives something the language has no text of its own for: a
// struct, a run, a store, a reference. A host that wants those written walks
// them with `kest_frame_gives` and writes what it finds, because what a
// program means by them is the host's to decide. Which of the two it was is
// `K0646` into `kest_report`, naming the type where there is one: the number
// says no and cannot say why, and an index that is no function is a third
// thing that also answers minus one.
int64_t kest_gave_text(KestRuntime *runtime, int32_t entry,
                       const KestValue *frame, char *out, size_t room);

// How wide a frame has to be to call this: enough for what it takes and for
// what it gives back, whichever is more.
//
// It is not the number `kest_frame_fills` and `kest_frame_reads` judge a host
// by: they ask about one side each, and this is the wider of the two. A host
// told it said the wrong number of slots is sent to the door that gave the
// number it was judged by, which is `kest_frame_layout` for what a function
// takes and `kest_frame_gives` for what comes back.
//
// Zero for an index that is no function, and zero for a function that takes
// nothing and gives nothing, because that is what it needs. The number cannot
// tell those apart and so the first of them says so into `kest_report`: ask
// this about what `kest_entry` answered, and check that first.
uint32_t kest_frame_slots(KestRuntime *runtime, int32_t entry);

// Writes what the program has said since the last time this was asked: what
// failed while running, and what a lend disagreed about. A host that gets
// `false` from `kest_call`, or a lend whose `object` is NULL, calls this to
// find out why. Nothing is written twice, and nothing from before this machine
// started is written at all: what failed to compile went to `kest_build`.
//
// A machine that has said nothing since it was last asked writes nothing, in
// either form, because that is what it has to say. `KEST_FORM_JSON` writes one
// object per call for the run of diagnostics that call is about, so a host
// asking after every call gets one line each.
void kest_report(KestRuntime *runtime, FILE *out, KestForm form);

// The most places one diagnostic shows: the declaration it is about, the other
// declaration of that name, the calls between a promise and the body that broke
// it. A diagnostic with more says how many it left out rather than stopping
// where a reader would take it for the end — `leftOut` in JSON — and a run of
// calls shown under a refusal is this many frames, because a frame is a place.
#define KEST_MOST_PLACES 8

// The most a machine keeps of what nobody has asked for. A run of diagnostics
// is made at this size, so a machine nobody asks never grows the list it was
// given; what it did not keep is counted, and the report says how many there
// were. A host that reads what it is told never meets this. See D618.
#define KEST_MOST_UNREAD 16

// How many bytes the running program is holding. It goes down as well as up:
// what a program makes and then replaces is given back, so a world that
// rewrites a name every round holds what the world holds rather than what it
// has ever written. `kest_heap_reset` puts it back to nothing. See D996.
//
// This is what a ceiling is held against and what a host watching a frame
// budget reads. It is not what a call cost: a call that made a megabyte of
// text and let it go answers the same as one that made nothing, and the two
// numbers below are what tell them apart.
size_t kest_heap_used(const KestRuntime *runtime);

// How many bytes the running program has ever been handed. This only goes up,
// until `kest_heap_reset` puts it back to nothing.
//
// Asked on either side of a call, the difference is what that call cost, which
// is the number a host with a frame budget wants: a total is a number without
// a scale, and a frame is what a host has to fit into. A call into a function
// that promises `no.alloc` answers nought, which is that promise read from
// outside rather than taken on faith.
size_t kest_heap_taken(const KestRuntime *runtime);

// And the most it was ever holding at once, which is the number that says a
// program has settled: a world driven ten times as long that stops at the same
// figure is a world whose memory is bounded by what it holds rather than by
// how long it has been running. See D996.
size_t kest_heap_most(const KestRuntime *runtime);

// What a run did, counted rather than timed. A machine counts what it did; a
// clock belongs to the host, which is the one thing that can say how long a
// thing took on the machine it is running on. See D979.
typedef struct {
    // What the run spent, in the unit a budget is spent in: the same steps
    // `kest_fuel_set` bounds, so a profile and a frame budget are in the same
    // currency and a host can read one against the other. It is not one per
    // instruction -- work an instruction does is charged for, which is what
    // makes a budget bound time rather than code size.
    //
    // There is no count per instruction here. Taking one is a test at the top
    // of the dispatch loop, and that measured a third of the machine: a
    // profiler that makes a program a third slower is measuring a different
    // program. The build that checks itself counts them under `KEST_DEEP`,
    // where a third is nothing beside what a sanitiser costs. See D979.
    uint64_t steps;
    // Times it crossed into something the host provides.
    uint64_t crossings;
    // Bodies entered, added up, which is every call the program made.
    uint64_t calls;
    // What the budget was and what is left of it. A machine with no budget
    // says nought for both.
    uint64_t fuel_given;
    uint64_t fuel_left;
    // What the program's heap holds now, which goes down as well as up: a
    // world that replaces a name every round holds what the world holds.
    size_t heap;
    // And the most it ever held at once, which is the number that says a
    // world has settled -- a run driven ten times as long that stops at the
    // same figure is bounded by what it holds rather than by how long it has
    // been running. See D996.
    size_t most;
} KestCounted;

// What the memory under a program did, which is counted always rather than
// only when somebody asked: every one of these is at an allocation, a walk, a
// lend or a copy, and a program runs millions of instructions between any two
// of them. `KestCounted` above is what the program did; this is what the heap
// under it did about that. See D1007.
//
// It is a struct of its own and a door of its own rather than more fields on
// `KestCounted`, because the fields of a struct a host has compiled against
// are what `KEST_ABI_VERSION` is about and 1.x does not move that: what a
// minor version may add is a door.
typedef struct {
    // Places handed out, bytes asked for, and what the places they were cut
    // from are worth. The second against the third is what the ladder of
    // widths costs -- a run of a hundred and thirty bytes is in a place of a
    // hundred and ninety-two -- and it is the one number nothing else says.
    uint64_t allocations;
    uint64_t asked;
    uint64_t given;
    // Times something grew where it stood rather than moving and being
    // copied, which is what a run filled one element at a time wants.
    uint64_t grown;
    // Walks that swept, and the bytes they gave back.
    uint64_t sweeps;
    uint64_t reclaimed;
    // Plots asked of the host and handed back to it, which is the memory the
    // host sees rather than the memory the program holds.
    uint64_t plots_made;
    uint64_t plots_freed;
    // Blocks of working memory opened, which is every `scratch { }` entered.
    uint64_t blocks;
    // What the walks took and what the longest one took, in whatever unit the
    // clock `kest_clock` was given counts in -- and nought for a machine that
    // was given none, which is every machine until a host says otherwise.
    // This library is ISO C and there is no monotonic clock in it, so the
    // clock is the host's the way every other outside thing is. See D935.
    uint64_t walked;
    uint64_t worst_walk;
    // And the two halves of that, because they are bounded by different
    // things: marking is bounded by what the program can still reach and
    // sweeping by how much memory there is to look through.
    uint64_t marking;
    uint64_t sweeping;
    // Slots read loosely, added up over every walk: what a walk of the roots
    // costs, which is the part of a collection a program controls by how deep
    // it is standing.
    uint64_t roots;
    // Lends begun and elements in them.
    uint64_t lends;
    uint64_t lent_elements;
    // Bytes copied because something outgrew the place it was in. A program
    // that says how many there will be copies none of them.
    uint64_t copied;
} KestTelemetry;

// Answers false for no machine and no room to write into, and true otherwise.
// There is nothing to turn on: these are counted whether or not anybody asks.
bool kest_telemetry(const KestRuntime *runtime, KestTelemetry *into);

// The byte a host writes over an instruction to stop the machine there, which
// is the instruction nothing compiles to. A host writing a debugger has to
// know it and cannot work it out: the public header does not hand out the
// instruction set and should not, because what a breakpoint is is the
// machine's business. So the machine says which byte it is rather than the
// host writing the number down -- which it did, twice, and the second time the
// number had moved and the machine ran whatever that byte now means.
//
// Writing it anywhere but the first byte of an instruction is writing into the
// middle of one, and nothing can check that for a host. See D991 and D1012.
uint8_t kest_break_byte(void);

// Walks now, at a moment the host chose, and gives back everything nothing can
// reach. It is what a frame-budgeted host reaches for: a machine decides for
// itself when a walk is worth doing, which is when it has been handed as much
// as it is holding, and that moment is wherever the program happened to be.
// A host that calls this between frames moves the walk to between frames.
//
// Between calls only. Inside one, the host's own C locals are not something
// this machine can read, so a walk started there could give away what the host
// is holding and nothing else could say so -- `kest_keeps` is the door for a
// host that wants to hold one across a call. Refused while a program is
// running, with a diagnostic, the way throwing the heap away is.
//
// And refused for a machine a debugger stopped, which is between calls to
// look at and in the middle of one in fact: its frames are standing on the
// heap, and a walk there once reached nothing at all and gave that memory
// back. See D1078.
//
// Answers whether it walked and gave anything back. A machine with a
// `scratch { }` block open does not, because a block gives its own memory back
// and a walk in the middle of one would be a walk over memory that is about to
// go anyway. Neither does one with no room left to remember where a walk had
// got to: a walk that could not finish has not seen everything, and giving
// memory back after one of those would give away what the program can still
// reach. Both answer false and leave the heap as they found it.
//
// It does not change what a program answers. A program cannot tell that this
// happened, which is what keeps `deterministic` true.
bool kest_collect(KestRuntime *runtime);

// When the machine decides a walk is worth doing on its own: after it has been
// handed this many times what it was holding when it last swept. One by
// default, which is the shortest pause there is to have and what a host with a
// frame to fit into wants.
//
// A host that is not budgeting a frame wants a bigger number, and what it buys
// is measured: over `bench/agents.kest`, three takes the collector from 15.7 %
// of a call to 6.8 % of one, for a pause half again as long and twice the
// memory held at most. A walk cannot be made shorter than what it has to mark
// -- 0.68 milliseconds for every megabyte still reachable, measured over
// worlds from five thousand to forty thousand entities -- so this is the one
// number there is to turn. Nought and one mean the same thing. See D1045.
//
// It does not change what a program answers, for the same reason `kest_collect`
// does not.
void kest_collect_after(KestRuntime *runtime, uint32_t times);

// What one collection cost, handed to a host the moment it finishes.
//
// It is the only way to get a distribution rather than a total. A machine that
// kept every pause would be holding a list that grows with how long the
// program ran, and which percentiles are wanted, over what window, and whether
// to keep the samples at all are the host's questions rather than this
// library's. `KestCounted` says what every walk came to added up and what the
// longest was; this says what each one was.
//
// Nothing in a program can reach it, and nothing about what a program answers
// changes when it is set -- which is what keeps `deterministic` true, the same
// as for `kest_clock`. See D1027.
typedef struct {
    // What the whole of it took and the two halves, in whatever unit the clock
    // `kest_clock` was given counts in. Nought for a machine given no clock,
    // and a host that wants a distribution gives one.
    uint64_t took;
    uint64_t marking;
    uint64_t sweeping;
    // Slots read loosely to find the roots, which is what a program controls
    // by how deep it is standing when a walk happens.
    uint64_t roots;
    // Bytes given back by this walk, and bytes in places still handed out
    // after it.
    uint64_t reclaimed;
    uint64_t live;
    // The plots the heap is holding, what the host gave for them, and what of
    // that is in places nothing is using. A plot goes back to the host only
    // when every place in it is free, so the last of these is the memory this
    // heap is holding and cannot hand back.
    uint64_t plots;
    uint64_t plot_bytes;
    uint64_t free_bytes;
} KestPause;

// Told what each collection cost, at the end of each one that swept. NULL
// turns it off, which is what every machine starts with. A walk that could not
// finish says nothing, because it took nothing.
//
// It is called while the machine is stopped, which is what a pause is: a host
// that does work in here is lengthening the pause it is being told about.
// Keeping the numbers is what it is for.
void kest_collected(KestRuntime *runtime,
                    void (*told)(const KestPause *pause, void *context),
                    void *context);

// The clock a machine times its own walks with. It is the host's because this
// library is ISO C, which has no monotonic clock, and because a duration is
// the one thing about a run that belongs to the machine it ran on rather than
// to the program. What it counts in is the host's to decide and the host's to
// read back: the machine adds them up and does not interpret them.
//
// Nothing in a program can reach it, and nothing about what a program answers
// changes when it is set -- which is what keeps `deterministic` true.
void kest_clock(KestRuntime *runtime, uint64_t (*now)(void *), void *context);

// Starts or stops counting. A machine that is not counting pays one test of a
// pointer that is nothing, and a machine that is asked twice keeps what it had
// rather than starting again. Returns false when there was no room to count in,
// which is a question the caller asked rather than anything about the program.
bool kest_count(KestRuntime *runtime, bool on);

// What has been counted so far, or false for a machine nobody asked.
bool kest_counted(const KestRuntime *runtime, KestCounted *into);

// How many times one function of this program was entered, by the number
// `kest_entry` answers with. Nought for a machine nobody asked.
uint64_t kest_counted_entry(const KestRuntime *runtime, int32_t entry);

// And what the allocation that was refused was asking for, or nought when
// nothing has been refused — which a heap thrown away with `kest_heap_reset`
// is: the refusal belonged to the heap that is gone, and a host raising a
// ceiling by it would be raising this one by a number about another. A ceiling stops a program at the allocation that
// would have crossed it, so what it has used stops short of what it was
// allowed — by this much, which is the difference between a frame that missed
// by eight bytes and one that missed by a megabyte. A host raising a ceiling
// reads it to know what it is raising it by.
size_t kest_heap_wanted(const KestRuntime *runtime);

// Which of the two said no. `kest_heap_wanted` is one number for two things
// that happened, and a host does something different about each: a ceiling it
// set is a number it can raise, and a machine with nothing left is not. The
// machine knows which; this is it saying so.
typedef enum {
    // Until something has been refused, which is most of the time.
    KEST_REFUSED_NOTHING,
    // The ceiling this host gave in `KestLimits`, which is a promise this
    // machine kept.
    KEST_REFUSED_CEILING,
    // The machine underneath, which had nothing left to give. Raising the
    // ceiling changes nothing about this one.
    KEST_REFUSED_MACHINE,
} KestRefusal;

KestRefusal kest_heap_refused_by(const KestRuntime *runtime);

// What this machine is actually running with, which is what the host asked for
// where it asked and the built-in number where it did not. Those numbers are
// otherwise not knowable: a host that passed nothing has no way to write down
// what it got, and `kest_heap_used` is a number without a scale until the
// ceiling beside it is readable.
//
// `heap_bytes` answers zero when there is no ceiling, because that is what no
// ceiling is. The other two are always a number, because a machine always has
// a stack and a depth.
void kest_allowed(const KestRuntime *runtime, KestLimits *limits);

// Where a debugger stopped this machine, as an offset into the code of the
// body it stopped in, or -1 for a machine that is not stopped. A machine stops
// when it runs the one instruction nothing compiles to, which is what a
// debugger writes over the first byte of an instruction it wants to stop at.
//
// A stopped machine is not finished and is not broken: its frames, its stack
// and its heap are where they were, what a `scratch { }` opened is still open,
// and nothing has been said into the report. `kest_call` answers false for one,
// so a host asks this to tell a stop from a refusal. See D991.
//
// A machine only stops in a call a host made from outside: a breakpoint in a
// run a bound function called back in is `K0708`, and that run ends as a
// refusal. See D1079.
//
// And it is in the middle of a call, which is the half of that a host has to
// act on: the frames are standing on the heap, so the five doors that would
// take that heap away or move the ceiling over it -- `kest_collect`,
// `kest_heap_reset`, `kest_scratch_mark`, `kest_scratch_rewind` and
// `kest_heap_allow` -- refuse here the way they refuse inside a call. Freeing
// the machine is the one way out that is not `kest_resume`. See D1078.
int64_t kest_stopped(const KestRuntime *runtime);

// Which body it stopped in, by the number `kest_entry` answers with, or -1.
int32_t kest_stopped_in(const KestRuntime *runtime);

// Carries on from where it stopped. The instruction the breakpoint was written
// over is run first, so a debugger puts the byte back before calling this.
// Answers what `kest_call` would have: false for a machine that stopped again,
// which `kest_stopped` tells from a refusal.
//
// A resume is the rest of the call that stopped, so it answers the same way:
// `frame` and `room` are where what comes back is written, the same frame the
// call was made with. NULL for a host that does not want it.
bool kest_resume(KestRuntime *runtime, KestValue *frame, uint32_t room);

// The bytes a body was compiled to, and how many, so a debugger can write a
// breakpoint over one and put it back. Nothing else should write here: this is
// the program the machine is running.
//
// And it is the *build's* program, not this machine's. Every machine started
// from one build reads the same bytes, so a breakpoint written for one is an
// instruction every one of them runs into. That is what makes a breakpoint
// cost a running machine nothing, and it is the one thing that writes a build
// after it was built: debug a build no other machine is standing on. See
// D1077.
//
// A body of a handful of instructions that can stop nothing may also have been
// written into its callers rather than called (D1156). Those copies say where
// they came from through `kest_came_from`, the way every instruction does, so
// a breakpoint put on a line is put in every body with an instruction from
// that line; one written at the start of a body stops the calls that were
// left calls. `kest debug` compiles every call as a call.
uint8_t *kest_code_of(KestRuntime *runtime, int32_t entry, uint32_t *count);

// Where in the source the instruction at this offset came from, or -1. One per
// instruction, which is what makes a breakpoint at a line a breakpoint at an
// instruction and what makes a stack trace say where.
int64_t kest_came_from(const KestRuntime *runtime, int32_t entry,
                       uint32_t at);

// How many frames deep the machine is, and what each is: `kest_frame_in` is
// which body, by the number `kest_entry` answers with, and `kest_frame_ip` is
// how far into its code it is. Nought frames for a machine that is not
// running and not stopped.
uint32_t kest_frames_deep(const KestRuntime *runtime);
int32_t kest_frame_in(const KestRuntime *runtime, uint32_t deep);
int64_t kest_frame_ip(const KestRuntime *runtime, uint32_t deep);

// One slot of one frame, for reading a local out of a stopped machine. False
// past the last frame and past the slots that frame has.
bool kest_frame_slot(const KestRuntime *runtime, uint32_t deep, uint16_t slot,
                     KestValue *into);

// What the body called that slot, or NULL for one it did not name. `kind` is
// what a layout piece is, so a host reads the slot as what it holds. A body
// that reused a slot after a scope ended gave it more than one name and the
// last is the one answered.
const char *kest_frame_name(const KestRuntime *runtime, uint32_t deep,
                            uint16_t slot, uint16_t *slots, uint8_t *kind);

// Whether that slot holds *where* the value is rather than the value. A `for`
// binds its element by address when the body never writes the name it bound,
// which is a copy a turn saved and nothing a program can tell (D866) -- but a
// host reading slots is reading the machine's own working, and a pointer read
// as a number is a number nobody can do anything with. True for one of those,
// false for a slot that holds what it is called and for one with no name.
//
// What to do with one: the address is where the element is, laid out the way
// `kest_build_layout` says that type is, so a host reads it through
// `kest_frame_slot` and then reads the memory as the layout. `kest debug`
// writes `at 0x...` for one rather than printing the address as though it
// were the value, which is what it did. See D1081.
bool kest_frame_at_address(const KestRuntime *runtime, uint32_t deep,
                           uint16_t slot);

// How many slots a frame has, which is how far a host walks looking for names.
uint16_t kest_frame_wide(const KestRuntime *runtime, uint32_t deep);

// Give this machine a budget, or take its budget away with
// `KEST_FUEL_UNLIMITED`. One unit is one step, and a step is a jump that goes
// back or a call: those are the two things a program does to go on doing
// something, and a budget on them bounds every program that would not stop.
// On top of that, an instruction that does as much work as the program asked
// for — joining text, filling a run of something, making room for one, reading
// a piece of text to its end — costs a unit for every sixty-four bytes or
// elements of it, because a budget that counted a megabyte copy as one step is
// one a program can spend a second inside without spending a unit of.
//
// It is a count and not a duration, so two machines given the same budget stop
// in the same place — what a step costs is the machine's and how many there
// are is the program's. See D921 and D950.
//
// This is the door a host replenishes through. A machine that stopped for want
// of fuel is not broken and is not finished: its stack, its heap and everything
// the program built are where they were, so a host that gives it more and calls
// again carries on from the next tick rather than from the start. What it may
// not do is carry on from the middle of the call that stopped — the call
// returned, and the work that call had not done is not done.
//
// Called between calls. A host may call it from inside one of its own bound
// functions; what it may not do is expect the call it is inside to see the new
// budget, because that call is already spending the old one.
void kest_fuel_set(KestRuntime *runtime, uint64_t instructions);

// What a door of the host's cost the program, said by the host that knows.
// A bound function may do as much work as it likes and the machine cannot see
// any of it: what crossed out was a call and what comes back is a value. A host
// that reads a file, walks a scene or asks another system something charges for
// it here, in the same units the machine spends — a unit is a step, and work is
// a unit for every sixty-four bytes or elements of it, which is the rate the
// machine charges its own instructions at.
//
// Called from inside a bound function, where the program's budget is whole:
// the slice the machine was spending is given back before a door is entered, so
// what is spent here is spent against what the program has left. A host that
// spends more than that does not stop the call it is inside — that call is the
// host's and the machine is not running — but the machine stops at the next
// step the program takes, which is the same refusal a budget spent any other
// way gives.
//
// Nothing for a machine with no budget, which is a host charging for work
// nobody is counting. See D951.
void kest_fuel_spend(KestRuntime *runtime, uint64_t work);

// What is left of it, and `KEST_FUEL_UNLIMITED`'s own answer — every bit set —
// for a machine with no budget. A host that watches a frame reads this after a
// tick to learn what the tick cost, which is the same number the build that
// checks itself counts and a release build does not.
uint64_t kest_fuel_left(const KestRuntime *runtime);

// Ask this machine to stop at the next instruction. It is the other half of the
// same mechanism and costs the same nothing: a host that wants a running
// program to stop — an editor cancelling, a process shutting down — sets this
// and the machine stops the way it stops for fuel, with everything it built
// intact and a refusal that says which of the two it was.
//
// It is one store of one word, so a host may call it from a signal handler or
// from another thread while the machine runs. What it may not do is free the
// machine from there: stopping is a message and freeing is a change.
//
// A machine stays cancelled until a host gives it fuel again. `kest_fuel_set`
// is what takes it back.
// A bound function says the crossing failed. It is the one thing a host could
// not do: `KestNative` gives nothing back, so a door that could not do what it
// was asked either wrote a value that means nothing or reached for a global of
// its own, and the program carried on with the first. Called from inside a
// bound function, and what happens then is that the call refuses at the
// instruction that made it, in the same shape as anything else that fails while
// running, with the host's own words under it.
//
// `why` is the host's and is copied. Nothing is taken from it but the words: a
// code is the machine's to allocate, and what a host says is a sentence about
// what it could not do.
//
// Anything the door wrote into the frame is not read. A host that fails and
// writes an answer is a host that failed.
void kest_native_failed(KestRuntime *runtime, const char *why);

void kest_cancel(KestRuntime *runtime);

// Whether somebody asked it to stop and it has not been given fuel since.
bool kest_cancelled(const KestRuntime *runtime);

// How deep a host may nest what it marks. A scratch is a frame's worth of
// working memory and a host that wants nine of them at once is a host doing
// something this is not for.
#define KEST_SCRATCH_DEEP 8

// Marks where the heap is, and answers a number to put it back by. Nought when
// it could not: while the program is running, with anything lent, and past the
// depth above, each of which says why into `kest_report`.
//
// What it is for is a frame's working memory. A host that calls a query which
// makes text, an array, a store — anything on the heap — and reads the answer
// out, marks before the call and puts the heap back after it, and then the
// query costs the same every frame rather than every frame costing the last
// one. It is `kest_heap_reset` with a place to go back to rather than the
// beginning.
//
// What a host must not do is keep anything the program made after the mark. A
// piece of text, an array, a store or a reference into one that came out of a
// call after the mark is gone when the heap goes back, and the machine cannot
// tell: a handle is a pointer. What is safe to read out is what a host copies
// out — a number, or `kest_gave_text` into a buffer of the host's own — and
// what is safe to keep is what was made before the mark.
//
// The program's own state is the same rule said again: anything it pushed into
// a store or an array that was made before the mark is on the heap after it,
// so a host that marks, calls a frame step that adds to its world, and puts the
// heap back has taken the world's memory out from under it. Mark round a query
// and not round a step that keeps something.
//
// A program says the same thing in `scratch { }`, where the compiler proves
// nothing made inside the block outlives it and a host proves nothing here:
// what a program writes is checked and what a host writes is written down. See
// D957, D966 and D972.
//
// Nought for no machine, for a machine whose program is running, and for one a
// debugger stopped -- a stop is the middle of a call, and a mark taken there is
// a place the frames under it are standing above. See D1078.
uint32_t kest_scratch_mark(KestRuntime *runtime);

// And back to it: everything the program made since is gone and the heap is
// where it was. True when it was put back. False, with why in `kest_report`,
// for a mark this machine did not hand out, one that has already been used,
// one from before a reset, for a rewind while the program is running or while
// a debugger has the machine stopped in the middle of a call, and for one with
// anything lent.
//
// A mark under one that is still open takes the ones above it with it, which is
// what makes nesting mean anything.
bool kest_scratch_rewind(KestRuntime *runtime, uint32_t mark);

// Throws the heap away and starts it again. Nothing in the machine survives a
// call, so between calls there is nothing of the program's left to point at
// it; what this invalidates is every handle the *host* is still holding. An
// array, a store or a piece of text that came out of `kest_call` is gone
// after this, and passing one back in is reading freed memory.
//
// Between calls, and not inside one. A bound function that asks for this from
// inside the call it was called from is asking for what the program is
// standing on, and is refused: `kest_report` says so. So is a host asking it
// of a machine a debugger stopped, which is the middle of a call with nothing
// of the host's on the machine's stack to show it. See D1078.
//
// That is the only false. It was once a new heap and a free of the old one,
// which the host could be out of memory for; it is the same heap emptied now,
// so it asks the host for nothing and there is nothing else it can fail at.
// See D322.
bool kest_heap_reset(KestRuntime *runtime);

// How much heap this machine may have from here on, said after there is one.
// A host dividing a number it was given cannot divide it before the machine
// exists: what a machine costs is the arithmetic a machine is made with, and a
// host that works that out for itself keeps a second copy of a sum that is
// only ever right until somebody changes one of them. So it makes a machine
// with a heap of nothing, asks `kest_runtime_cost` what that took, and says
// here how much of what is left the program may have. Nought is no ceiling,
// which is what a host that never says this has.
//
// Answers false for no machine and while the program is running, because what
// the program is holding is on the heap and a ceiling moved under it is a
// promise changed after it was made. Between calls is where it belongs, which
// is where `kest_heap_reset` belongs for the same reason. See D850.
//
// And false for a machine a debugger stopped, which is the middle of a call by
// the same reading: nothing is taken away by this door, but the call the
// machine is in the middle of would carry on into a wall it never had. See
// D1078.
bool kest_heap_allow(KestRuntime *runtime, size_t bytes);

// What the host provides, bound by the name the program declares:
// `extern fn Clock.now() -> i64` is bound as "Clock.now".
typedef struct KestHost KestHost;

KestHost *kest_host_new(void);
void kest_host_free(KestHost *host);

// Returns false when the host is out of memory, and when the name is already
// bound: a name is bound once. A machine takes what the host held when it
// started and keeps it, so binding after `kest_start` would change the table
// and not the machine, and answering that it had worked would be a lie half
// the time.
bool kest_host_bind(KestHost *host, const char *name, KestNative function,
                    void *context);

// The function bound to a name, or NULL. A program that declares something
// the host does not provide is refused before it runs, by name.
KestNative kest_host_find(const KestHost *host, const char *name,
                          void **context);

// Says a door already bound may be called by code nobody trusts: a machine
// started with `kest_start_untrusted` is refused a program that asks for any
// door not said so, by name. What a door does with what it is handed is the
// host's to get right -- this is the host saying it has. False for a name not
// bound. See D1246.
bool kest_host_open(KestHost *host, const char *name);

// Whether a door is open to code nobody trusts.
bool kest_host_opened(const KestHost *host, const char *name);

// A compiled program, and everything it was compiled from. One of these is
// what a host has instead of the stages there are.
// Compiles a file and everything it imports. `library` is where `std` lives --
// a directory, with a separator after it or without, the way `KEST_LIB` is
// read -- or NULL for `lib/` beside the program. Returns NULL when it did not
// compile. See D1145.
//
// What it could not compile goes to `errors` in the form asked for, or nowhere
// when that is NULL. A program that compiled and had something said about it —
// a shape nothing names, a declaration nothing calls — has that waiting in
// `kest_build_report`, because a build that answered is one a host may want to
// say nothing about. Asking costs nothing and says nothing twice. See D631.
//
// `room` is the most this may ask the machine for while it reads, checks and
// compiles, in bytes, and nought is as much as there is. It is the same
// question `KestLimits.heap_bytes` asks about a machine and it is asked here
// because compiling is where the answer is unbounded: what a program allocates
// is a number the program chose, and what compiling one costs is a number
// nobody chose — a file this compiler cannot make sense of can ask for
// everything there is, and one did. A build refused by this says what it had
// taken, what it was allowed and what the allocation that crossed it wanted;
// a build refused by the machine says so instead, because one of the two is
// raised by picking a bigger number and the other is not. See D843 and D844.
KestBuild *kest_build(const char *path, const char *library, FILE *errors,
                      KestForm form, size_t room);

// The same with a ceiling on the work compiling does as well as on the bytes
// it asks for. `room` bounds how much a file can make this hold and `work`
// bounds how long it can make it take: a unit is a word read, a piece of the
// tree made or checked, an instruction laid down or walked over while it is
// proved, so the count is the same on every machine and for every run of the
// same files, and a ceiling that let one through lets it through again. Nought
// is as much as it needs. A build that reaches the ceiling stops where it is
// and is refused with K0666, which says how much it was given; the count one
// that got through took is in `kest_build_work`. See D1248.
KestBuild *kest_build_within(const char *path, const char *library,
                             FILE *errors, KestForm form, size_t room,
                             uint64_t work);

// How many units of work compiling this took, whether it was given a ceiling
// or not. The count is what `kest_build_within` is told, so a host that wants
// a ceiling with room in it can measure what its own programs take and give
// them a multiple.
uint64_t kest_build_work(const KestBuild *build);

// The same from files handed over rather than read: the first is the program
// and the rest are what it imports, the library's among them, each where a
// build would have found it -- a library's under `library`, the way
// `kest_build` is told where one is. Nothing is read from a disk or looked
// for on one, so what is built is what was handed and nothing else, whatever
// is beside the program where it is run. A file it asks for and was not
// handed is refused the way one that is not on a disk is. What a release
// binary starts with, carrying the program inside it. See D1172.
KestBuild *kest_build_from(const KestFile *files, uint32_t count,
                           const char *library, FILE *errors, KestForm form,
                           size_t room);
// Frees the build and everything on it. Answers whether there is no build now:
// true when it freed one and true when there was none, false when a machine is
// still standing on it. The program the machines run is on here, and so is
// every piece of text their diagnostics point at, so this is refused while any
// of them is up rather than left to be found out about afterwards: free the
// machines with `kest_runtime_free`, then the build. `kest_build_report` says
// which it was. See D324.
bool kest_build_free(KestBuild *build);
// Freeing what is not there is not a refusal, the same way it is not for a
// machine — and the same limit holds: a build that has been freed is not a
// build that is not there, and its pointer belongs at no door here.

// What the build has said and nobody has been told yet, in the form asked for.
// A build that compiled says nothing here, and then says something when a
// machine fails to start on it: what the program asks the host for and the
// host has not got is settled before anything runs, and there is no machine to
// ask about it afterwards. A host that gets NULL from `kest_start` calls this.
//
// Nothing is written twice: what was written when it was said is not written
// again.
void kest_build_report(KestBuild *build, FILE *out, KestForm form);

// What this build cost: how many bytes reading, checking and compiling the
// program took. Nought for no build.
//
// It is the compiler's own work rather than the program's, which is what
// `kest_heap_used` is about — the two never move together. A host that
// compiles at startup pays this once and never thinks about it again; one that
// reloads a file whenever it changes pays it every time, and this is the
// number that says what that costs. A rebuild costs what the first build cost:
// nothing is carried over from one to the next, so a host reading this after a
// reload is reading the same number it read the first time. See D573.
size_t kest_build_cost(const KestBuild *build);

// And what it is still holding, which is what a host that keeps a build around
// is paying for now rather than what it paid to make one. The two differ by
// what a stage left behind for nobody: the tokens a file is read into are dead
// the moment its tree is made, and the trees are dead once every copy has been
// compiled, so both are given back where the stage that reads them ends. A
// build that was checked and not compiled still holds its trees, because the
// compiler is one of the two stages that read them. Nought for no build.
// See D747 and D748.
size_t kest_build_held(const KestBuild *build);

// Which files that cost was paid for, by position, or NULL past the last of
// them. A host walks from zero until NULL to learn every one.
//
// The list is the file the host named and everything that file imports, which
// is not a list a host can work out for itself: an import names a path relative
// to the file that wrote it, and what a program is made of is settled by the
// loader rather than by whoever started it. A host that reloads a program when
// something changes watches these; a host that watched only what it named would
// keep running a program whose library moved under it. See D657.
const char *kest_build_read(const KestBuild *build, uint32_t at);

// How many bytes the one at `at` is, and nought past the last of them. That is
// the file as it was read rather than as it is now, which is the number the
// cost above was paid over.
size_t kest_build_read_bytes(const KestBuild *build, uint32_t at);

// A number that moves when the bytes of the one at `at` move, and nought past
// the last of them. Two files that are the same bytes have the same number and
// two that differ anywhere do not, which is what a size cannot say: two edits
// that keep the length look the same by size.
//
// It is FNV-1a over the file, which is what the language hashes text with. A
// host may keep it, write it down, and compare it with one from another
// machine: it is the bytes and nothing about this run. See D658.
uint64_t kest_build_read_mark(const KestBuild *build, uint32_t at);

// And one number for the program: every file's mark folded in the order they
// were read. Nought for no build and for a build that read nothing.
//
// What it answers is whether this is the same program, which is a question a
// host asks after a reload and when it looks for what it compiled last time.
// Folded here rather than left to a host, because two hosts folding their own
// way would have two numbers for one program.
uint64_t kest_build_mark(const KestBuild *build);

// And a number for what was made of them: what the machine will run, rather
// than what was read to get there. Nought for a build that did not compile.
//
// It moves when an instruction, a constant, a name, a promise or a shape that
// crosses the boundary moves, and it does not move when only where they were
// written does — so a program with a comment added, or one run through the
// formatter, has the mark it had. That is what a host caching what it compiled
// asks, and `kest_build_mark` is what a host watching files asks; they are two
// questions and two numbers.
//
// What it costs is that two programs with one mark may say different places
// when they fail: where a chunk came from is not in it. See D659.
uint64_t kest_build_code_mark(const KestBuild *build);

// And all of them added up, which is what a cost is divided by: a program of
// four lines that imports the library costs what the library costs, so a host
// dividing by the file it named would call it fifteen times dearer a byte than
// it is. Nought for no build. See D656.
size_t kest_build_source(const KestBuild *build);

// And what this machine is made of: the stack, the frames, the table of what
// the host provides, and the machine itself. Nought for no machine.
//
// Not what the program has allocated, which is `kest_heap_used` — the two never
// move together. This is what a host pays to start one and gets back when it
// frees one, and it is a machine's own: starting a machine takes nothing from
// the build it was started on, so a host that starts one, frees it and starts
// another pays for one machine rather than for all of them. See D574.
size_t kest_runtime_cost(const KestRuntime *runtime);

// A name the program asks the host for, by position, or NULL past the last of
// them. A host walks from zero until NULL to learn every one.
//
// `kest_start` refuses a program whose externs are not all bound, and says
// which by name. This is the same list before the refusal, for a host that
// embeds a program it did not write and would otherwise learn the names one
// failed start at a time. It is asked of the build, because that is what a
// host has before there is a machine.
const char *kest_build_extern(const KestBuild *build, uint32_t at);

// And the same list said as capabilities rather than as names: the receiver of
// each extern, once each, in the order they were first asked for, or NULL past
// the last. `extern fn Io.write(...)` asks for `Io`, and a host walks these to
// decide what a program may do before it binds a single function.
//
// A capability here is the receiver and nothing else. There is no second
// mechanism: what a program can do is what the host bound, so denying one is
// not binding it, and this is the list to read before deciding. An extern with
// no receiver -- a bare `extern fn name()` -- is under the empty capability,
// which is a host binding one function and meaning it. See D981.
const char *kest_build_capability(const KestBuild *build, uint32_t at);

// The three below answer a place past the last one the way they answer a real
// extern that takes nothing or gives nothing back. `K0648` says which, and
// goes to `kest_build_report`: the walk above ends at NULL and asking past
// where it ended is not the same news.

// What the program expects the one at `at` to take and to give back: how many
// arguments, what each of them is, and what comes back over them. The same
// layouts `kest_frame_layout` gives for a function the host calls, because a
// crossing is the same shape whichever way it goes.
//
// A host binds a C function to a name and nothing else checks that the two
// agree about what crosses: a function bound to a name that takes two things
// and written to take three reads whatever is beside them. This is the check,
// and it is asked of the build, because a host binds before there is a
// machine. `kest_extern_gives` is NULL for one that gives nothing.
uint32_t kest_extern_takes(const KestBuild *build, uint32_t at);
const KestLayout *kest_extern_layout(const KestBuild *build, uint32_t at,
                                     uint32_t which);
const KestLayout *kest_extern_gives(const KestBuild *build, uint32_t at);

// What the program lays a type out as where memory is shared, by the name a
// host would lend it under. Answers how many types of that name the program
// has, and fills `layout` when that is one: nought is a name it does not hold
// in an array, which is exactly the set that cannot be lent, and more than one
// is a name that needs the module written in front of it, `world.Event`.
//
// This is what `kest_borrow` will compare a host's own `sizeof` against, asked
// before the lend rather than found out at one. A host lending in a loop
// checks once. The layout is the build's and lasts until `kest_build_free`.
uint32_t kest_build_layout(const KestBuild *build, const char *name,
                           const KestLayout **layout);


// What has to outlive what, which is the whole of it: the build outlives the
// machine, and nothing else has to outlive anything. Starting reads what the
// host bound and keeps its own copy, so a host may be freed as soon as a
// machine has started — `examples/embed.c` does that, rather than saying so.
// The layouts a build lent are the build's and go with it.
//
// A machine for a compiled program. The build has to outlive it, and
// `limits` may be NULL. Free it with `kest_runtime_free`.
//
// A build makes as many machines as a host wants. Each has its own stack,
// heap and diagnostics, and what they share is the compiled program. What one
// says is not what another reports.
//
// Two things write a build after it is built and both are exceptions a host
// has to know: a start that fails writes the build's report, and a debugger
// writes the program itself — a breakpoint is an instruction written over, and
// it is written over for every machine of that build at once. So fail-to-start
// from one thread, and debug a build no other machine is standing on. See
// `kest_code_of`, D1071 and D1077.
KestRuntime *kest_start(KestBuild *build, const KestHost *host,
                        const KestLimits *limits);

// A machine for a program nobody trusts, which is `kest_start` with three
// things more. Every door the program asks for has to be one the host opened
// with `kest_host_open`. `limits` has to say how long it may run and how much
// heap it may have -- nought for either is refused rather than read as no
// ceiling. And it runs only what the verifier proved: a body the other backend
// wrote as C and a host linked in is not entered, and the machine runs the
// instructions it was proved from instead. Refused by name in the build's
// report, like a door that is not bound. What this machine is to promise, and
// how far each promise is kept, is SECURITY.md. See D1246.
KestRuntime *kest_start_untrusted(KestBuild *build, const KestHost *host,
                                  const KestLimits *limits);

// After the call it was made for returns. A bound function that frees the
// machine from inside one is refused and told, because the frames and the
// stack are what the program is standing on; the heap then waits for
// `kest_build_free`.
//
// Answers whether there is no machine now: true when it freed one and true
// when there was none, false when it was refused. *None* is NULL. A machine
// that has been freed is not none — it is memory the host handed back, and the
// pointer is not one to bring to this door or any other. The answer above for
// a machine that did not start is one this boundary can give because there is
// nothing to read; for one that has gone there is something to read and it is
// not the machine's any more. The sanitised build says so at the first door
// tried; a shipping one says nothing at all. See D895. A host that reads the
// answer knows what it is still holding without reading the report, and the
// refusal lasts exactly as long as the call it was asked in — return from the
// bound function and free it there. Nothing takes it away by force, so a host
// that asks in a loop and never returns keeps the machine and everything on
// it. See D323.
bool kest_runtime_free(KestRuntime *runtime);

// A machine a host keeps across frames, with the world it is keeping, and the
// reload a game does under that world. Everything below is made of the doors
// above and nothing else, and a host that wants to do one of these things its
// own way does it with those. What these are for is that every host that keeps
// a world wrote the same few hundred lines to do it -- a frame with the world
// in front, every handle in it said to the machine, the doors found again
// after a reload and held to taking what they took, the files watched -- and
// two hosts writing them are two sets of the same mistakes. See D1151.
typedef struct KestHeld KestHeld;

// Builds `path` against `library` (NULL as `kest_build` takes it) and starts a
// machine over it with `host`, which has to outlive what this answers: a
// reload starts another machine with it. What would not build goes to
// `errors`. NULL when it did not build or start, and then there is nothing to
// free.
KestHeld *kest_held_new(const char *path, const char *library,
                        const KestHost *host, FILE *errors);

// Frees the machine and the build. True when there is nothing held now.
bool kest_held_free(KestHeld *held);

// The machine being held, for everything the doors above do with one: a lend,
// a report, fuel. It is another machine after a reload.
KestRuntime *kest_held_runtime(KestHeld *held);

// Calls `entry` with `count` values and keeps what it gives back as the world:
// every slot of it, and every handle in it said to the machine, because this
// host holds them between calls where no walk of the program's can see them.
bool kest_held_begin(KestHeld *held, const char *entry, const KestValue *args,
                     uint32_t count);

// Calls `entry` with the world in front of `count` more values, and writes
// what it answered into `answer` where that is not NULL. A door is found by
// name the first time it is called and remembered, and a reload holds every
// door remembered to still being there and taking what it took.
bool kest_held_call(KestHeld *held, const char *entry, const KestValue *args,
                    uint32_t count, KestValue *answer);

// A run of the world's, read where it lies rather than copied out by the
// program: the piece of the world the program calls `piece` -- `ground.tiles`
// for a field of a field -- as its bytes, how many elements, and how wide one
// is. NULL when the world has no such piece or it is not a run. Good until
// the next call into the machine, which may move it.
const void *kest_held_run(KestHeld *held, const char *piece, uint32_t *count,
                          uint16_t *stride);

// Whether any file the program was built from is not the bytes it was built
// from: every one is read again and held to its mark. A host asks this as
// often as it wants a reload to be noticed, and not every frame.
bool kest_held_changed(KestHeld *held);

// The program built again from `path` (NULL for the one it was) and put under
// the world the running one holds, or the running one kept exactly as it was.
// The protocol is the program's two functions: `save`, which the running
// machine is asked with the world and answers with bytes, and `restore`, which
// a machine of the new build is handed those bytes and answers with a world.
// Nothing replaces the running machine until the new one has made its world
// and every door this host has called is there and takes what it took. What
// happened is written into `said` either way, the stage it stopped at when it
// did, and what would not build goes to the errors this was made with.
bool kest_held_reload(KestHeld *held, const char *path, char *said,
                      size_t room);


#endif
