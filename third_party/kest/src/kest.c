#include "kest.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    KestNative function;
    void *context;
} Binding;

struct KestHost {
    Binding *items;
    uint32_t count;
    uint32_t capacity;
};

const char *kest_text_bytes(const KestValue *value, uint32_t *length) {
    if (value == NULL || value->text == NULL) {
        if (length != NULL) {
            *length = 0;
        }
        return NULL;
    }
    if (length != NULL) {
        *length = (uint32_t)value[1].integer;
    }
    return value->text;
}

const char *kest_version(void) {
    return KEST_VERSION_STRING;
}

// Read out of this library rather than out of whatever header the caller
// compiled against, which is the whole of what it is for.
uint32_t kest_abi_version(void) {
    return KEST_ABI_VERSION;
}

const char *kest_profile(uint32_t *version) {
    if (version != NULL) {
        *version = KEST_PROFILE_VERSION;
    }
    return KEST_PROFILE_NAME;
}

KestHost *kest_host_new(void) {
    return calloc(1, sizeof(KestHost));
}

void kest_host_free(KestHost *host) {
    if (host == NULL) {
        return;
    }
    for (uint32_t i = 0; i < host->count; i++) {
        free((void *)host->items[i].name);
    }
    free(host->items);
    free(host);
}

bool kest_host_bind(KestHost *host, const char *name, KestNative function,
                    void *context) {
    // Nothing to bind into. `kest_host_new` answers nothing when there is no
    // memory for a host, and a host that did not look would otherwise find out
    // by writing through it: what a boundary owes a caller is a refusal rather
    // than the caller's own mistake made worse.
    if (host == NULL || name == NULL || function == NULL) {
        return false;
    }
    // A name is bound once. Binding it again silently replaced what was there
    // and told the caller it had worked, which is only true until a machine
    // has started: a machine takes what the host held when it started and
    // keeps it. Either answer surprises somebody, so neither is given.
    for (uint32_t i = 0; i < host->count; i++) {
        if (strcmp(host->items[i].name, name) == 0) {
            return false;
        }
    }

    if (host->count == host->capacity) {
        uint32_t grown = host->capacity == 0 ? 8 : host->capacity * 2;
        Binding *moved = realloc(host->items, sizeof(Binding) * grown);
        if (moved == NULL) {
            return false;
        }
        host->items = moved;
        host->capacity = grown;
    }

    char *owned = malloc(strlen(name) + 1);
    if (owned == NULL) {
        return false;
    }
    memcpy(owned, name, strlen(name) + 1);
    host->items[host->count].name = owned;
    host->items[host->count].function = function;
    host->items[host->count].context = context;
    host->count++;
    return true;
}

KestNative kest_host_find(const KestHost *host, const char *name,
                          void **context) {
    // A list of no doors holds no door of that name, which is what a host that
    // could not be made has. See D895.
    if (host == NULL || name == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < host->count; i++) {
        if (strcmp(host->items[i].name, name) == 0) {
            if (context != NULL) {
                *context = host->items[i].context;
            }
            return host->items[i].function;
        }
    }
    return NULL;
}

// Which member of a value a slot of this kind is. No `default`: a kind added
// to the layouts is a kind nothing here has an answer for, and the build says
// so rather than a host writing whatever this fell through to.
KestSlot kest_slot_of(uint8_t kind) {
    switch ((KestScalar)kind) {
    case KEST_L_F32:
    case KEST_L_F64:
        return KEST_S_REAL;
    case KEST_L_WORD:
        return KEST_S_WORD;
    case KEST_L_TEXT:
        return KEST_S_TEXT;
    case KEST_L_PAYLOAD:
        return KEST_S_TAGGED;
    case KEST_L_FN:
    case KEST_L_NOTHING:
    case KEST_L_FLAGS8:
    case KEST_L_FLAGS16:
    case KEST_L_FLAGS32:
    case KEST_L_FLAGS64:
    case KEST_L_I8:
    case KEST_L_I16:
    case KEST_L_I32:
    case KEST_L_I64:
    case KEST_L_U8:
    case KEST_L_BOOL:
    case KEST_L_U16:
    case KEST_L_U32:
    case KEST_L_U64:
    // A tag is four bytes read as a whole number, which is what a host writes
    // and reads it through. What it is for is saying where it is.
    case KEST_L_TAG:
    // And the byte after an optional's value is one byte read the same way,
    // for the same reason: what it is for is saying which byte it is.
    case KEST_L_HELD:
    // A reference is a number and reads like one. It is its own kind because
    // of what it is not: a machine word, which is what it said it was while a
    // host was being told to read it through a pointer. See D715.
    case KEST_L_REF:
        return KEST_S_INTEGER;
    }
    // A kind that is not one of them is a host's own number, and a slot is an
    // integer when nothing says otherwise: the switch above is what says a new
    // kind was never decided about, and this is what a byte from somewhere
    // else gets.
    return KEST_S_INTEGER;
}
