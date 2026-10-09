// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ARIA adapter's internals (record mui-0008): the adapter, the
// record of the elements the page holds, and the page's calls.

#ifndef MAUL_UI_SRC_ARIA_H
#define MAUL_UI_SRC_ARIA_H

#include "id_map.h"

#include "maul-ui/access_aria.h"

#include <stddef.h>
#include <stdint.h>

// No parent: an element placed in the adapter's container.
#define ARIA_NO_SLOT UINT32_MAX

// Room for an element's id: a prefix of the adapter's and the node's id.
#define ARIA_ID_SIZE 40

// An element the page holds for a shown node: the node, its shown
// parent's element (ARIA_NO_SLOT for the container), the box it was
// last placed at, relative to its parent's, and the walk that last saw
// it shown.
typedef struct muiAriaElement
{
    uint64_t id;
    uint32_t parent;
    uint32_t seen;
    float box[4];
} muiAriaElement;

// Where a walk found a shown node: its parent's place in the walk and
// its index among the parent's shown children.
typedef struct muiAriaPlace
{
    uint32_t parent;
    uint32_t index;
} muiAriaPlace;

struct muiAriaAdapter
{
    muiAllocator allocator;
    size_t blockSize;
    muiAccessTree* tree;
    // The page's handle of the adapter's elements, -1 for none.
    int page;
    float scale;
    bool enabled;
    // Whether the update being applied may have changed the shown tree.
    bool reshaped;
    muiAriaActionFunction action;
    void* user;
    uint32_t nodes;
    // Room for any node's shown children, and the walk of the shown tree.
    uint64_t* scratch;
    uint64_t* walk;
    muiAriaPlace* places;
    // The elements: their records, free places, and records by node id.
    muiAriaElement* elements;
    uint32_t* freeElements;
    uint32_t freeCount;
    muiIdMap elementById;
    uint32_t pass;
    // Nodes whose box changed in the update being applied.
    uint64_t* moved;
    uint32_t movedCount;
    // Whether the focus moved in the update being applied.
    bool focusMoved;
};

// The element showing a node, ARIA_NO_SLOT for none.
uint32_t muiAriaSlotOf(const muiAriaAdapter* adapter, uint64_t id);

// Writes every attribute of a node's element, or those that differ
// between an old record and the node's new one (old NULL for all).
void muiAriaWriteAttributes(const muiAriaAdapter* adapter, uint32_t slot, const muiAccessNode* old,
                            const muiAccessNode* node);

// Whether the text a node names itself by differs between two records.
bool muiAriaNameChanged(const muiAccessNode* old, const muiAccessNode* node);

// Writes an element's id: the adapter's page, then the node's id.
void muiAriaIdOf(int page, uint64_t id, char out[ARIA_ID_SIZE]);

// Whether a node's element is an input of type range: a range the host
// sets.
bool muiAriaIsRange(const muiAccessNode* node);

// What a client did to an element, as the page tells it.
typedef enum muiAriaEvent
{
    mui_ariaClicked = 0,
    mui_ariaFocused = 1,
    mui_ariaRangeSet = 2,
} muiAriaEvent;

// Asks the host for the action a client's event means.
void muiAriaPerform(muiAriaAdapter* adapter, muiAriaEvent event, uint32_t slot, double value);

// The enabling button was pressed, a client acted on an element: called
// from the page.
void muiAriaEnableFromPage(muiAriaAdapter* adapter);
void muiAriaEventFromPage(muiAriaAdapter* adapter, int kind, uint32_t slot, double value);

// The page's calls (aria_page.c). Strings are UTF-8, NUL-terminated.
int muiAriaPageOpen(const char* host, bool deferred, const char* label, muiAriaAdapter* adapter);
void muiAriaPageClose(int page);
bool muiAriaPageDropButton(int page);
void muiAriaPageFocus(int page, uint32_t slot, bool always);
void muiAriaPageAnnounce(int page, const char* text, bool assertive);
void muiAriaPageMake(int page, uint32_t slot, bool range, const char* id);
void muiAriaPageRemove(int page, uint32_t slot);
void muiAriaPagePlace(int page, uint32_t slot, uint32_t parent, uint32_t index);
void muiAriaPageBox(int page, uint32_t slot, float x, float y, float width, float height);
void muiAriaPageAttribute(int page, uint32_t slot, const char* name, const char* value);
void muiAriaPageText(int page, uint32_t slot, const char* text);

#endif // MAUL_UI_SRC_ARIA_H
