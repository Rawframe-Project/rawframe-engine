// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility (record mui-0008): the platform-neutral accessibility
// tree. Every node of a root's tree is a node of it, with a role, text,
// flags and actions; most come from what the library already holds
// (rectangles, scrolling, focus, states, value ranges, virtual lists),
// the rest from the host. The library builds updates in the shape
// AccessKit uses: the nodes that changed, each sent whole, the focus
// with every update, the root with the first. The host hands them to
// the adapters, which keep their own copies, and applies on its thread
// the requests the adapters queued. Nothing is built until the host
// enables a root, as when Maul Window reports that an assistive
// technology asked for one.

#ifndef MAUL_UI_ACCESS_H
#define MAUL_UI_ACCESS_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // What a node is to assistive technology: AccessKit's roles less the
    // browser's document internals. Closed, and only ever appended to.
    typedef uint8_t muiRole;

    enum
    {
        // A node that only lays out others; adapters leave it out and
        // show its children in its place.
        mui_roleGeneric = 0,
        // Text that names or tells, its text in the value.
        mui_roleLabel = 1,
        mui_roleImage = 2,
        mui_roleLink = 3,
        mui_roleButton = 4,
        mui_roleDefaultButton = 5,
        mui_roleCheckBox = 6,
        mui_roleRadioButton = 7,
        mui_roleRadioGroup = 8,
        mui_roleSwitch = 9,
        mui_roleTextInput = 10,
        mui_roleMultilineTextInput = 11,
        mui_roleSearchInput = 12,
        mui_rolePasswordInput = 13,
        mui_roleNumberInput = 14,
        mui_roleEmailInput = 15,
        mui_rolePhoneNumberInput = 16,
        mui_roleUrlInput = 17,
        mui_roleDateInput = 18,
        mui_roleTimeInput = 19,
        mui_roleDateTimeInput = 20,
        mui_roleComboBox = 21,
        mui_roleEditableComboBox = 22,
        mui_roleListBox = 23,
        mui_roleListBoxOption = 24,
        mui_roleList = 25,
        mui_roleListItem = 26,
        mui_roleTree = 27,
        mui_roleTreeItem = 28,
        mui_roleTreeGrid = 29,
        mui_roleTable = 30,
        mui_roleRow = 31,
        mui_roleCell = 32,
        mui_roleRowHeader = 33,
        mui_roleColumnHeader = 34,
        mui_roleRowGroup = 35,
        mui_roleGrid = 36,
        mui_roleGridCell = 37,
        mui_roleMenu = 38,
        mui_roleMenuBar = 39,
        mui_roleMenuItem = 40,
        mui_roleMenuItemCheckBox = 41,
        mui_roleMenuItemRadio = 42,
        mui_roleTab = 43,
        mui_roleTabList = 44,
        mui_roleTabPanel = 45,
        mui_roleToolbar = 46,
        mui_roleTooltip = 47,
        mui_roleDialog = 48,
        mui_roleAlertDialog = 49,
        mui_roleAlert = 50,
        mui_roleStatus = 51,
        mui_roleLog = 52,
        mui_roleTimer = 53,
        mui_roleProgressIndicator = 54,
        mui_roleMeter = 55,
        mui_roleSlider = 56,
        mui_roleSpinButton = 57,
        mui_roleScrollBar = 58,
        mui_roleScrollView = 59,
        // A separator, which may be dragged.
        mui_roleSplitter = 60,
        mui_roleGroup = 61,
        mui_rolePane = 62,
        mui_roleWindow = 63,
        mui_roleTitleBar = 64,
        mui_roleHeading = 65,
        mui_roleParagraph = 66,
        mui_roleRegion = 67,
        mui_roleNavigation = 68,
        mui_roleMain = 69,
        mui_roleBanner = 70,
        mui_roleComplementary = 71,
        mui_roleContentInfo = 72,
        mui_roleSearch = 73,
        mui_roleForm = 74,
        mui_roleArticle = 75,
        mui_roleDocument = 76,
        mui_roleApplication = 77,
        mui_roleFigure = 78,
        mui_roleCaption = 79,
        mui_roleNote = 80,
        mui_roleDetails = 81,
        mui_roleDisclosureTriangle = 82,
        mui_roleCanvas = 83,
        mui_roleVideo = 84,
        mui_roleAudio = 85,
        mui_roleColorWell = 86,
        mui_roleTerminal = 87,
        mui_roleFeed = 88,
        mui_roleMarquee = 89,
    };

    // The highest role.
    enum
    {
        MUI_ROLE_LAST = mui_roleMarquee
    };

    // A node's texts. Each is UTF-8.
    typedef uint8_t muiAccessTextKind;

    enum
    {
        // Its name, when no other node names it.
        mui_accessLabel = 0,
        mui_accessDescription = 1,
        // Its value as text: a text input's text, a label's text, a
        // range's value as the user reads it.
        mui_accessValue = 2,
        mui_accessPlaceholder = 3,
        mui_accessKeyboardShortcut = 4,
        // Read in place of its role's name.
        mui_accessRoleDescription = 5,
        // Read in place of its states.
        mui_accessStateDescription = 6,
    };

    enum
    {
        MUI_ACCESS_TEXTS = 7
    };

    // A node's flags. The host sets the first ten; the library adds the
    // rest when it builds a node.
    typedef uint32_t muiAccessFlags;

    enum
    {
        // Left out with its subtree, as aria-hidden.
        mui_accessHidden = 1u << 0,
        mui_accessReadOnly = 1u << 1,
        mui_accessRequired = 1u << 2,
        mui_accessMultiselectable = 1u << 3,
        mui_accessBusy = 1u << 4,
        // It can be checked: the checked state (maul-ui/style.h) is then
        // reported, as mui_accessChecked, or mixed when this says so.
        mui_accessCheckable = 1u << 5,
        mui_accessMixed = 1u << 6,
        // It can be selected: the selected state is then reported.
        mui_accessSelectable = 1u << 7,
        // It can be expanded, and is: expand and collapse are offered.
        mui_accessExpandable = 1u << 8,
        mui_accessExpanded = 1u << 9,
        // It takes a click (maul-ui/event.h's mui_navigateActivate);
        // buttons, links, checkboxes, radio buttons, switches, menu
        // items, tabs, options, tree items and disclosure triangles do
        // without it.
        mui_accessClickable = 1u << 10,

        // The library's: the checked and selected states on nodes that
        // can have them.
        mui_accessChecked = 1u << 16,
        mui_accessSelected = 1u << 17,
        // It is disabled, or exiting (maul-ui/exit.h), which is hidden.
        mui_accessDisabled = 1u << 18,
        // It roots a modal layer (maul-ui/interaction.h).
        mui_accessModal = 1u << 19,
        // It clips its content, which may lie outside it: a scroll
        // container.
        mui_accessClipsChildren = 1u << 20,
        // It scrolls: the scroll values hold.
        mui_accessScrolls = 1u << 21,
        // It is a range (maul-ui/range.h): the numeric values hold.
        mui_accessNumeric = 1u << 22,
        // It can take the keyboard's focus.
        mui_accessFocusable = 1u << 23,
    };

    enum
    {
        // The flags the host sets.
        MUI_ACCESS_HOST_FLAGS = 0x7FFu
    };

    // What assistive technology asks a node to do.
    typedef uint8_t muiAccessAction;

    enum
    {
        // Routed to the node as a navigation event, mui_navigateActivate.
        mui_actionClick = 0,
        // The keyboard's focus (player slot 0) to it, or away from it.
        mui_actionFocus = 1,
        mui_actionBlur = 2,
        // Posted as mui_notificationAccessAction, count the action.
        mui_actionExpand = 3,
        mui_actionCollapse = 4,
        // A range's value a step on, back, or to a value.
        mui_actionIncrement = 5,
        mui_actionDecrement = 6,
        mui_actionSetValue = 7,
        // Its scroll containers scrolled to show it.
        mui_actionScrollIntoView = 8,
        // A scroll container a page each way, or to an offset.
        mui_actionScrollUp = 9,
        mui_actionScrollDown = 10,
        mui_actionScrollLeft = 11,
        mui_actionScrollRight = 12,
        mui_actionSetScrollOffset = 13,
    };

    // How one node names others.
    typedef uint8_t muiAccessRelation;

    enum
    {
        mui_relationLabelledBy = 0,
        mui_relationDescribedBy = 1,
        mui_relationControls = 2,
        mui_relationDetails = 3,
        mui_relationFlowTo = 4,
        // One node each; adapters read the first given.
        mui_relationActiveDescendant = 5,
        mui_relationErrorMessage = 6,
        mui_relationPopupFor = 7,
    };

    enum
    {
        MUI_ACCESS_RELATIONS = 8
    };

    // One node a relation names.
    typedef struct muiAccessLink
    {
        uint64_t target;
        muiAccessRelation kind;
    } muiAccessLink;

    // Whether and how a node's changes are announced, as aria-live.
    typedef uint8_t muiAccessLive;

    enum
    {
        mui_liveOff = 0,
        mui_livePolite = 1,
        mui_liveAssertive = 2,
    };

    // What a node opens, as aria-haspopup.
    typedef uint8_t muiAccessPopup;

    enum
    {
        mui_popupNone = 0,
        mui_popupMenu = 1,
        mui_popupListBox = 2,
        mui_popupTree = 3,
        mui_popupGrid = 4,
        mui_popupDialog = 5,
    };

    // Which way a node's items or values run; none says nothing.
    typedef uint8_t muiAccessOrientation;

    enum
    {
        mui_orientationNone = 0,
        mui_orientationHorizontal = 1,
        mui_orientationVertical = 2,
    };

    // How a column header's column is sorted, as aria-sort.
    typedef uint8_t muiAccessSort;

    enum
    {
        mui_sortNone = 0,
        mui_sortAscending = 1,
        mui_sortDescending = 2,
        mui_sortOther = 3,
    };

    // Whether a node's value is invalid, as aria-invalid.
    typedef uint8_t muiAccessInvalid;

    enum
    {
        mui_invalidNone = 0,
        mui_invalidTrue = 1,
        mui_invalidGrammar = 2,
        mui_invalidSpelling = 3,
    };

    // Which current thing a node is among related ones, as aria-current.
    typedef uint8_t muiAccessCurrent;

    enum
    {
        mui_currentNone = 0,
        mui_currentTrue = 1,
        mui_currentPage = 2,
        mui_currentStep = 3,
        mui_currentLocation = 4,
        mui_currentDate = 5,
        mui_currentTime = 6,
    };

    // A node's typed values. Counts, indices and positions start at 1; 0
    // gives none.
    typedef struct muiAccessValues
    {
        // A heading's or a tree item's depth.
        uint32_t level;
        // Its position among its siblings, and their count: the library
        // gives a virtual list's items theirs when the host gives none.
        uint32_t setPosition;
        uint32_t setSize;
        // A table's or a grid's rows and columns, a row's or a cell's
        // place in them, and a cell's spans.
        uint32_t rowCount;
        uint32_t columnCount;
        uint32_t rowIndex;
        uint32_t columnIndex;
        uint32_t rowSpan;
        uint32_t columnSpan;
        muiAccessLive live;
        muiAccessPopup popup;
        // The library gives a range its axis when the host gives none.
        muiAccessOrientation orientation;
        muiAccessSort sort;
        muiAccessInvalid invalid;
        muiAccessCurrent current;
    } muiAccessValues;

    // A node as an update sends it: everything it is, whole.
    typedef struct muiAccessNode
    {
        // Its node's id, generation in the high half and index in the
        // low (muiAccessIdOf).
        uint64_t id;
        muiRole role;
        muiAccessFlags flags;
        // A bit per muiAccessAction it takes.
        uint32_t actions;
        // Its border box in its own space, and the transform from that
        // space to its parent's: its place less the parent's scroll.
        muiRect bounds;
        muiDrawTransform transform;
        // With mui_accessNumeric: the value, its limits and step.
        float value;
        float minimum;
        float maximum;
        float step;
        // With mui_accessScrolls: the offset and its largest, the
        // smallest being 0.
        float scrollX;
        float scrollY;
        float scrollXMax;
        float scrollYMax;
        // Its typed values, the host's and the library's.
        muiAccessValues values;
        // Its texts by muiAccessTextKind with their lengths, NULL for
        // none: the host's end in a NUL, the text function's need not.
        const char* text[MUI_ACCESS_TEXTS];
        uint32_t textLength[MUI_ACCESS_TEXTS];
        // The nodes it names, in order of kind, then as the host gave
        // them; NULL for none.
        const muiAccessLink* links;
        uint32_t linkCount;
        // Its children, in order: ids from the update's children.
        uint32_t firstChild;
        uint32_t childCount;
    } muiAccessNode;

    // An update: the nodes that changed since the last for the root, or
    // every node in the first. A node leaves the tree when its parent no
    // longer lists it. Valid until the next call that edits the context.
    typedef struct muiAccessUpdate
    {
        const muiAccessNode* const* nodes;
        uint32_t nodeCount;
        const uint64_t* children;
        // The root's id in the first update after enabling, else 0.
        uint64_t root;
        // The node with the keyboard's focus, or the root.
        uint64_t focus;
    } muiAccessUpdate;

    // A request an adapter queued for the host to apply.
    typedef struct muiAccessRequest
    {
        muiAccessAction action;
        uint64_t target;
        // For mui_actionSetValue.
        float value;
        // For mui_actionSetScrollOffset.
        float x;
        float y;
    } muiAccessRequest;

    /// The host's function for what host content reads as: a node's text,
    /// such as a text block's, valid until the host edits it. It runs
    /// inside muiBuildAccessUpdate, as the measure function runs inside
    /// layout, and may not change the context. The text must be
    /// well-formed UTF-8 below 2^31 bytes; other text is left out.
    typedef bool (*muiAccessTextFunction)(void* user, muiNodeId nodeId, uint64_t hostKey,
                                          const char** textOut, size_t* lengthOut);

    /// The accessibility id of a node.
    ///
    /// @param nodeId  The node.
    /// @return Its id.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API uint64_t muiAccessIdOf(muiNodeId nodeId);

    /// The node of an accessibility id.
    ///
    /// @param id  The id.
    /// @return Its node.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiNodeId muiNodeIdOfAccess(uint64_t id);

    /// Sets the function host content's text comes from: a node whose
    /// content is the host's (maul-ui/layout.h's mui_contentHost) and
    /// whose value text the host did not set reads as what it returns,
    /// and as a label when the host gave it no role. NULL reads nothing.
    ///
    /// @param context   The context.
    /// @param function  The function, or NULL.
    /// @param user      Passed to it.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context or a
    ///         call from a measure or paint function.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiSetAccessTextFunction(muiContext* context,
                                                             muiAccessTextFunction function,
                                                             void* user);

    /// Builds updates for a root's tree from now on, the next one whole.
    /// Enabling an enabled root makes its next update whole again, as
    /// when an adapter starts over. The first root enabled allocates a
    /// copy of each node as last sent and the update's buffers.
    ///
    /// @param context  The context.
    /// @param rootId   A node without a parent.
    /// @return `mui_success`; `mui_errorCapacity` when
    ///         `limits.accessRoots` roots are enabled or memory runs out;
    ///         `mui_errorInvalid` for a NULL context, the null id, a node
    ///         with a parent, or a call from a measure or paint
    ///         function; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiAccess_Enable(muiContext* context, muiNodeId rootId);

    /// Stops building updates for a root; the last root disabled frees
    /// what enabling allocated.
    ///
    /// @param context  The context.
    /// @param rootId   The root.
    /// @return `mui_success`; `mui_empty` for a root not enabled;
    ///         `mui_errorInvalid` for a NULL context, the null id or a call
    ///         from a measure or paint function.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiAccess_Disable(muiContext* context, muiNodeId rootId);

    /// Builds the update for an enabled root after its layout: the nodes
    /// whose role, texts, flags, actions, bounds, transform, values or
    /// children changed since the last update, each whole.
    ///
    /// @param context    The context.
    /// @param rootId     The root.
    /// @param updateOut  Receives the update.
    /// @return `mui_success`; `mui_empty` for a root not enabled;
    ///         `mui_errorInvalid` for a NULL argument, the null id, or a
    ///         call from a measure or paint function;
    ///         `mui_errorStale` for a root that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiBuildAccessUpdate(muiContext* context, muiNodeId rootId,
                                                         muiAccessUpdate* updateOut);

    /// Applies a request to a node that takes its action.
    ///
    /// @param context     The context.
    /// @param request     The request.
    /// @param handledOut  Receives whether it did anything: a click a
    ///                    widget handled, a focus or value or offset that
    ///                    moved, a request posted. May be NULL.
    /// @return `mui_success`; `mui_empty` for a node that does not take
    ///         the action; `mui_errorInvalid` for a NULL context or
    ///         request, an unknown action, a value that is not finite, or
    ///         a call from a measure, paint or event function;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiPerformAccessAction(muiContext* context,
                                                           const muiAccessRequest* request,
                                                           bool* handledOut);

    /// Sets a node's role.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param role     The role, at most MUI_ROLE_LAST.
    /// @return `mui_success`; `mui_errorCapacity` when
    ///         `limits.accessNodes` nodes have accessibility data;
    ///         `mui_errorInvalid` for a NULL context, the null id, an
    ///         unknown role, or a call from a measure or paint
    ///         function; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetAccessRole(muiContext* context, muiNodeId nodeId,
                                                          muiRole role);

    /// Reads a node's role: mui_roleGeneric for one the host gave none.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param roleOut  Receives the role.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or
    ///         the null id; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetAccessRole(const muiContext* context,
                                                          muiNodeId nodeId, muiRole* roleOut);

    /// Sets one of a node's texts; a length of 0 clears it. The text is
    /// copied.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param kind     Which text.
    /// @param text     UTF-8, non-NULL when length is not 0, without NUL.
    /// @param length   Its length in bytes, below 2^31.
    /// @return `mui_success`; `mui_errorCapacity` when
    ///         `limits.accessNodes` nodes have accessibility data or memory
    ///         runs out; `mui_errorInvalid` for a NULL context, the null
    ///         id, an unknown kind, text that is not UTF-8 or holds a NUL,
    ///         or a call from a measure or paint function;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetAccessText(muiContext* context, muiNodeId nodeId,
                                                          muiAccessTextKind kind, const char* text,
                                                          size_t length);

    /// Reads one of a node's texts.
    ///
    /// @param context    The context.
    /// @param nodeId     The node.
    /// @param kind       Which text.
    /// @param textOut    Receives it, NUL-terminated, valid until it is
    ///                   set again or the node is destroyed.
    /// @param lengthOut  Receives its length.
    /// @return `mui_success`; `mui_empty` for none; `mui_errorInvalid` for
    ///         a NULL argument, the null id or an unknown kind;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetAccessText(const muiContext* context,
                                                          muiNodeId nodeId, muiAccessTextKind kind,
                                                          const char** textOut, size_t* lengthOut);

    /// Sets a node's flags, those the host sets.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param flags    Within MUI_ACCESS_HOST_FLAGS.
    /// @return `mui_success`; `mui_errorCapacity` when
    ///         `limits.accessNodes` nodes have accessibility data;
    ///         `mui_errorInvalid` for a NULL context, the null id, a flag
    ///         outside those, or a call from a measure or paint
    ///         function; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetAccessFlags(muiContext* context, muiNodeId nodeId,
                                                           muiAccessFlags flags);

    /// Reads the flags the host set on a node.
    ///
    /// @param context   The context.
    /// @param nodeId    The node.
    /// @param flagsOut  Receives them, 0 for none.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or
    ///         the null id; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetAccessFlags(const muiContext* context,
                                                           muiNodeId nodeId,
                                                           muiAccessFlags* flagsOut);

    /// Names other nodes from a node by one relation, replacing those it
    /// named by it; a count of 0 clears it. The ids are copied; a node
    /// named that is destroyed later stays named, and adapters pass over
    /// ids they do not hold.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param kind     The relation.
    /// @param targets  The nodes, non-NULL when count is not 0.
    /// @param count    How many, below 2^16.
    /// @return `mui_success`; `mui_errorCapacity` when
    ///         `limits.accessNodes` nodes have accessibility data or memory
    ///         runs out; `mui_errorInvalid` for a NULL context, the null id
    ///         as the node or a target, an unknown kind, or a call from a
    ///         measure or paint function; `mui_errorStale` for a node or a
    ///         target that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetAccessRelation(muiContext* context, muiNodeId nodeId,
                                                              muiAccessRelation kind,
                                                              const muiNodeId* targets,
                                                              uint32_t count);

    /// Reads the nodes a node names by one relation.
    ///
    /// @param context     The context.
    /// @param nodeId      The node.
    /// @param kind        The relation.
    /// @param targetsOut  Receives the nodes, up to capacity; may be NULL
    ///                    when capacity is 0.
    /// @param capacity    Room in targetsOut.
    /// @param countOut    Receives how many it names, whatever the room.
    /// @return `mui_success`; `mui_errorCapacity` when they do not fit,
    ///         those that fit written; `mui_errorInvalid` for a NULL
    ///         argument, the null id or an unknown kind; `mui_errorStale`
    ///         for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult
    muiNode_GetAccessRelation(const muiContext* context, muiNodeId nodeId, muiAccessRelation kind,
                              muiNodeId* targetsOut, uint32_t capacity, uint32_t* countOut);

    /// The default values: none of them.
    ///
    /// @return The values.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiAccessValues muiDefaultAccessValues(void);

    /// Sets a node's typed values.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param values   The values, each enum within its own.
    /// @return `mui_success`; `mui_errorCapacity` when
    ///         `limits.accessNodes` nodes have accessibility data;
    ///         `mui_errorInvalid` for a NULL argument, the null id, an enum
    ///         outside its values, or a call from a measure or paint
    ///         function; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetAccessValues(muiContext* context, muiNodeId nodeId,
                                                            const muiAccessValues* values);

    /// Reads the typed values the host set on a node.
    ///
    /// @param context    The context.
    /// @param nodeId     The node.
    /// @param valuesOut  Receives them; the defaults for none.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or
    ///         the null id; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetAccessValues(const muiContext* context,
                                                            muiNodeId nodeId,
                                                            muiAccessValues* valuesOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_ACCESS_H
