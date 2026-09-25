#ifndef KEST_AST_H
#define KEST_AST_H

#include "lexer.h"

// Literals and names keep only their span. The text is source[span], and the
// value is produced later, so the tree stays small and every node can report
// where it came from.

typedef enum {
    KEST_TYPE_NAMED,    // f32, Player
    KEST_TYPE_GENERIC,  // ref<Npc>
    KEST_TYPE_ARRAY,    // [T]
    KEST_TYPE_OPTIONAL, // T?
    KEST_TYPE_FN,       // fn(i32, i32) -> bool no.alloc
} KestTypeKind;

typedef struct KestTypeRef KestTypeRef;

struct KestTypeRef {
    KestTypeKind kind;
    KestSpan span;
    // NAMED and GENERIC only.
    KestSpan name;
    KestTypeRef **args;
    uint32_t arg_count;
    // ARRAY, OPTIONAL and FN only. NULL for a function that gives nothing.
    KestTypeRef *element;
    // `[f32; 16]`. Zero is `[f32]`, which is a handle to something that can
    // grow; a count makes it that many, laid out where it stands.
    KestSpan count;
    // FN only: written `block(...)`, which a function takes and a value never
    // is. See D1257.
    bool block;
    // FN only. What the value promises, which is part of what it is. Two of
    // them, and a value promising more may go where one promising less is
    // wanted. See D853.
    bool no_alloc;
    bool no_host;
    // The third promise: the same answer on every machine that keeps the
    // simulation profile. Not a `no`, because it says what a body does. See
    // D942.
    bool deterministic;
};

typedef enum {
    KEST_EXPR_INT,
    KEST_EXPR_FLOAT,
    KEST_EXPR_STRING,
    KEST_EXPR_BYTE,
    KEST_EXPR_BOOL,
    KEST_EXPR_NAME,
    KEST_EXPR_UNARY,
    KEST_EXPR_BINARY,
    KEST_EXPR_CALL,
    KEST_EXPR_FIELD,
    KEST_EXPR_INDEX,
    KEST_EXPR_ARRAY,
    KEST_EXPR_NONE,
    KEST_EXPR_TEXT,
    KEST_EXPR_MATCH,
    KEST_EXPR_IF,
    // `|x| x * 2` and `|x| { total += x }`: a body handed to a function that
    // takes a `block`, which runs where it was written. See D1257.
    KEST_EXPR_BLOCK,
} KestExprKind;

typedef struct KestExpr KestExpr;
typedef struct KestArm KestArm;
typedef struct KestBranch KestBranch;
typedef struct KestLambda KestLambda;

// What a `match` is, whichever it is used as.
typedef struct {
    // One or more. Two enums answered together is one `match` rather than one
    // inside another, so an arm answers a case for each of them.
    KestExpr **subjects;
    uint32_t subject_count;
    KestArm *arms;
    uint32_t arm_count;
    // Set by the checker when every case is answered.
    bool total;
    // Set when the arms give values, which is when every one of them does.
    bool gives;
} KestChoose;

// One piece of an interpolated string: either a run of characters or the
// expression written in a hole, never both.
typedef struct {
    KestSpan text;
    KestExpr *value;
} KestTextPart;

// Resolved by the checker. The compiler reads it to choose between an integer
// and a floating point instruction, rather than working the type out again.
typedef struct KestType KestType;

struct KestExpr {
    KestExprKind kind;
    // Set by the checker where a plain value stands in a place that wants an
    // optional. The compiler then writes the tag beside it. Nothing else in
    // the language converts on its own.
    //
    // Beside the kind rather than after the type, because a `bool` after two
    // words is seven bytes of padding on every node a program has. See D642.
    bool wrapped;
    KestSpan span;
    KestType *type;
    union {
        bool boolean;
        struct {
            KestTokenKind op;
            KestExpr *operand;
        } unary;
        struct {
            KestTokenKind op;
            KestExpr *left;
            KestExpr *right;
        } binary;
        struct {
            KestExpr *callee;
            KestExpr **args;
            uint32_t arg_count;
            // Written `x.f(a)` and read as `f(x, a)`: the checker moves `x` to
            // the front of what is passed and names `f` by its own name, and
            // this says so for everything after it, which would otherwise
            // take a local called `f` for the function. See D1256.
            bool method;
        } call;
        struct {
            KestExpr *object;
            KestSpan name;
        } field;
        struct {
            KestExpr *object;
            KestExpr *index;
        } index;
        struct {
            KestExpr **items;
            uint32_t count;
        } array;
        struct {
            KestTextPart *parts;
            uint32_t count;
        } text;
        // Held through a pointer the way a branch beside it is. Written out
        // it is the widest thing this union can be, and eight `match`
        // expressions in the whole of this tree were making every name and
        // every `+` eight bytes wider. See D782.
        KestChoose *choose;
        // Out of line because it holds blocks, which are named below this.
        KestBranch *branch;
        // And for the same reason.
        KestLambda *lambda;
    };
};

typedef struct KestStmt KestStmt;

typedef struct {
    KestStmt **items;
    uint32_t count;
} KestBlock;

// What an `if` is, whichever it is used as. An arm gives a value when it is
// written `-> expression` and does something when it is a block, and both arms
// are the same kind, which is D027's rule and not a second one.
struct KestBranch {
    // `if let x = maybe`. Zero length for a plain `if`.
    KestSpan binding;
    KestExpr *condition;
    KestExpr *then_value;
    KestBlock then_body;
    KestExpr *else_value;
    KestBlock else_body;
    // An `else if`, which is another `if`.
    KestExpr *otherwise;
    bool has_else;
    bool gives;
};

// A block written where it is handed over: the names it gives what it is
// called with, and either the value it gives or the statements it runs. It
// runs in the frame it was written in, reading and writing that frame's
// names, and goes nowhere else, which is what lets it be no more than code.
// See D1257.
struct KestLambda {
    KestSpan *params;
    uint32_t param_count;
    // `|x| x * 2`. NULL for a body in braces, which gives nothing.
    KestExpr *value;
    KestBlock body;
};


// One arm of a match: the case it is for, the names it gives what that case
// carries, and what to do. A zero-length name is the `else` arm.
//
// An arm either gives a value, written `-> expression`, or does something,
// written as a block. Every arm of one match is the same kind, which is what
// makes a match either a value or a statement and never quietly both.
// One position of an arm: the case answered there, and the names given to
// whatever that case carries. A zero length name is `else`, which answers any
// case in that position.
typedef struct KestArmPart {
    KestSpan name;
    KestSpan *bindings;
    uint32_t binding_count;
} KestArmPart;

struct KestArm {
    // One per subject, or one `else` standing for all of them.
    KestArmPart *parts;
    uint32_t part_count;
    KestSpan span;
    KestExpr *value;
    KestBlock body;
};

typedef enum {
    KEST_STMT_LET,
    KEST_STMT_ASSIGN,
    KEST_STMT_EXPR,
    KEST_STMT_WHILE,
    KEST_STMT_FOR,
    KEST_STMT_RETURN,
    KEST_STMT_BREAK,
    KEST_STMT_CONTINUE,
    KEST_STMT_BLOCK,
    // A block whose working memory goes back where it was when it ends: what
    // the program made inside it is gone, and nothing made inside it may be
    // kept. See D966.
    KEST_STMT_SCRATCH,
    KEST_STMT_DEFER,
    // `wait Walking` in a body that resumes: the field it resumes from is set
    // to that case and the parameter is given back, and the next call carries
    // on from the line after it. See D1263.
    KEST_STMT_WAIT,
} KestStmtKind;

// What a `for` walks. `index` is zero length when the position was not asked
// for, which is most of the time; `until` non-NULL makes `sequence` the first
// number of a range rather than the thing being walked. Named so a statement
// can hold one through a pointer rather than inside itself. See D782.
typedef struct {
    KestSpan index;
    KestSpan name;
    KestExpr *sequence;
    KestExpr *until;
    KestBlock body;
    // Whether the body assigns to that name, and to that position, which the
    // checker knows and the compiler cannot see without walking the body
    // again. A walk keeps its count where nothing can name it and hands the
    // name a copy, so that writing the name cannot make the count go wrong;
    // where nothing writes it, the copy is two instructions a turn spent
    // defending against nothing and the name is the count itself. Assigning
    // to a field of the name is not assigning to the name. See D866.
    bool name_written;
    bool index_written;
} KestEach;

struct KestStmt {
    KestStmtKind kind;
    // Set by the checker on the last statement of an arm written as a block
    // where a value was meant, so the one message about the arms is not said
    // again once for every arm. See D516.
    //
    // Beside the kind for the reason the same bit is beside an expression's:
    // a `bool` after a span is padding on every statement a program has. See
    // D642.
    bool passed_over;
    KestSpan span;
    union {
        struct {
            KestSpan name;
            // NULL when the type is left to inference, which is the usual case
            // inside a body; see D005.
            KestTypeRef *type;
            KestExpr *value;
            // Whether anything in the body assigns to that name or to
            // anything inside it, which the checker knows because it is what
            // resolved it. Written by the
            // parser and cleared by the checker, so a name nobody has looked
            // at is a name that was written: a value the compiler puts in the
            // chunk because nothing changes it is the one thing that must not
            // be got wrong by a walk that did not happen. See D887.
            bool name_written;
        } let;
        struct {
            // KEST_TOK_EQ, or one of the compound assignment operators.
            KestTokenKind op;
            KestExpr *target;
            KestExpr *value;
        } assign;
        struct {
            // `while let one = next()`. Zero length for a plain `while`, and
            // then the condition is a `bool` rather than an optional.
            KestSpan binding;
            KestExpr *condition;
            KestBlock body;
        } loop;
        // Held through a pointer, the way a `match` is. Written out it is the
        // widest thing this union can be by sixteen bytes, and a `for` is one
        // statement in twenty-five: the other twenty-four were carrying the
        // room for a position, a name, a sequence, an until and a body, and
        // using none of it. See D782.
        KestEach *each;
        // NULL for a bare `return`.
        KestExpr *result;
        KestExpr *value;
        KestBlock block;
        struct {
            // The case, as written.
            KestSpan name;
            // Which case of the enum it is, and which of the body's waits,
            // counted from one in the order they are written. Worked out by
            // the checker; the compiler lands the next call on it.
            uint32_t tag;
            uint32_t ordinal;
        } wait;
    };
};

// A name and a type: a struct field, or a function parameter.
typedef struct {
    KestSpan name;
    KestTypeRef *type;
    // The `own` in front of a struct field, and nothing wide for a field
    // written without one or for a parameter, which cannot take it. The span
    // rather than a flag because the formatter leads a field from where it
    // begins, and where a marked one begins is the word. See D1041.
    KestSpan own;
} KestField;

// One case of an enum: its name and what it carries, by position.
typedef struct {
    KestSpan name;
    KestTypeRef **payload;
    uint32_t payload_count;
} KestVariant;

typedef enum {
    KEST_DECL_MODULE,
    KEST_DECL_IMPORT,
    KEST_DECL_CONST,
    KEST_DECL_STRUCT,
    KEST_DECL_ENUM,
    KEST_DECL_FLAGS,
    KEST_DECL_FN,
} KestDeclKind;

// What a generic may ask of a type it is given. Written as a word after the
// parameter, proved against the body where the generic is declared, and
// required of the type at every call that makes a copy. A struct is a value
// laid out flat, so what a struct can do is what its fields can do, and none
// of these is a thing a program declares for a type of its own: they are the
// three the language itself provides. See D1043.
typedef enum {
    KEST_WANTS_COMPARES = 1u << 0,
    KEST_WANTS_ORDERS = 1u << 1,
} KestCapability;

// The word each is written as, in one place: the parser reads them here, the
// formatter writes them back from here, and what a message lists is made from
// here. Three lists of one thing is three lists the day one of them changes.
typedef struct {
    const char *word;
    KestCapability bit;
} KestCapabilityName;

// A type name a declaration takes, beside what it has to be able to do. The
// two together rather than two arrays, because a declaration is eight bytes
// wider for every pointer in it and a tree is half of what compiling costs.
typedef struct {
    KestSpan name;
    uint8_t wants;
} KestTypeParam;

#define KEST_CAPABILITY_COUNT 2u
// One of them by its place in the list, which is how everything that walks
// them reads them: a door rather than an array, so the list is this file's and
// nothing else can be holding a pointer into it.
const KestCapabilityName *kest_capability(uint32_t at);
// Them written out for a message: "`compares`, `orders` and `hashes`".
void kest_capability_list(char *out, size_t room);

typedef struct {
    KestDeclKind kind;
    // Beside the kind, because two four byte numbers together are eight bytes
    // and apart they are sixteen. See D642.
    uint32_t type_param_count;
    KestSpan span;
    // The declared name. For a module or an import it covers the whole dotted
    // path.
    KestSpan name;
    // `fn sort<T>(...)` and `struct Pair<A, B>`. A copy is made per set of
    // types it is used with, so a name here stands for one type per copy.
    // Each carries what it has to be able to do, written after a colon:
    // `fn set<K: compares, V>`. A generic's body is checked against those
    // where it is written, and a copy is refused where the type it was asked
    // for has not got them. See D1043.
    KestTypeParam *type_params;
    union {
        struct {
            KestTypeRef *type;
            KestExpr *value;
        } constant;
        struct {
            KestField **fields;
            uint32_t field_count;
        } record;
        struct {
            KestVariant **cases;
            uint32_t case_count;
            // `flags State: u8`. The width is written rather than counted,
            // because it is what a host sees and a ninth flag must not change
            // it quietly.
            KestTypeRef *width;
        } choice;
        struct {
            // `Clock` in `extern fn Clock.now()`. Zero length when absent.
            KestSpan receiver;
            KestField **params;
            uint32_t param_count;
            // `resumes c.at`: where the `c` is, one past its offset so that
            // nought is a function that does not wait. An offset rather than
            // two spans because it sits in the four bytes after the count,
            // which were padding, and a node widened is every node of every
            // program widened; `kest_resumes_spans` reads the two names back
            // out of the source. See D1263.
            uint32_t resumes;
            // NULL when the function returns nothing.
            KestTypeRef *result;
            bool is_extern;
            bool no_alloc;
            bool no_host;
            bool deterministic;
            // What the promise's proof found about this body, whether or not
            // it promises anything. The proof walks the call graph three
            // times -- once for the heap, once for the host, once for what is
            // not deterministic -- and each walk writes what it found here, so
            // a body that could keep a promise and does not say so is a thing
            // a reader can be told rather than a thing they have to try. For a
            // generic these are true if any copy of it is, because a copy is
            // what runs. See D976.
            bool reaches_heap;
            bool reaches_host;
            bool not_deterministic;
            KestBlock body;
        } function;
    };
} KestDecl;

typedef struct {
    KestDecl **items;
    uint32_t count;
    // How many nodes are under those declarations: every expression, every
    // statement and every declaration the parser made for this file. A tree is
    // the biggest thing reading a file makes — more than the tokens it came
    // from — and what it is made of was a thing nobody could ask. Counted where
    // the nodes are made, because a walk to count them would be a second walk
    // of the one thing this file already walks. See D641.
    uint32_t nodes;
} KestUnit;

// Prints the tree as indented s-expressions, for seeing what the parser built.
void kest_ast_dump(const KestUnit *unit, const KestSource *source, FILE *out);

// The two names of a `resumes c.at` as spans of the source: the parameter and
// the field. False for a function that does not wait. See D1263.
bool kest_resumes_spans(const KestSource *source, const KestDecl *decl,
                        KestSpan *param, KestSpan *field);

#endif
