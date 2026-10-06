// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Routed input (record mui-0007): keys, text and navigation go to a
// player's focus, pointer records to their node, each along the target's
// ancestors, top down and then back up, through one function of the
// host's that says whether it handled the event. What the UI leaves
// unhandled is the game's.

#ifndef MAUL_UI_EVENT_H
#define MAUL_UI_EVENT_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A physical key: its USB HID keyboard usage (W3C KeyboardEvent.code),
    // the same on every layout. The keys the library itself reads are
    // named here; every usage is valid.
    typedef uint16_t muiKeyCode;

    enum
    {
        mui_codeEnter = 40,
        mui_codeEscape = 41,
        mui_codeBackspace = 42,
        mui_codeTab = 43,
        mui_codeSpace = 44,
        mui_codeInsert = 73,
        mui_codeHome = 74,
        mui_codePageUp = 75,
        mui_codeDelete = 76,
        mui_codeEnd = 77,
        mui_codePageDown = 78,
        mui_codeArrowRight = 79,
        mui_codeArrowLeft = 80,
        mui_codeArrowDown = 81,
        mui_codeArrowUp = 82,
    };

    // What a key means under the current layout (W3C KeyboardEvent.key):
    // the Unicode code point it types without modifiers, or, for a key
    // that types nothing, MUI_KEY_NAMED with its code in the low bits; 0
    // when the layout gives it no meaning.
    typedef uint32_t muiKey;

#define MUI_KEY_NAMED 0x40000000u

    // The modifier keys held, and the lock keys on.
    typedef uint16_t muiModifiers;

    enum
    {
        mui_modShift = 1,
        mui_modControl = 2,
        mui_modAlt = 4,
        // The Windows, Command or Super key.
        mui_modMeta = 8,
        mui_modCapsLock = 16,
        mui_modNumLock = 32,
    };

    // A key going down (again, when repeated) or up.
    typedef struct muiKeyEvent
    {
        uint64_t timeNs;
        muiKey key;
        muiKeyCode code;
        muiModifiers modifiers;
        bool down;
        // The platform repeats a held key.
        bool repeat;
        // The player whose keyboard it is.
        uint8_t player;
    } muiKeyEvent;

    // Text typed or committed by an input method, as UTF-8 the host
    // passes on as it got it.
    typedef struct muiTextEvent
    {
        uint64_t timeNs;
        const char* text;
        uint32_t length;
        uint8_t player;
    } muiTextEvent;

    // What a gamepad, or the host's own bindings, ask of the UI.
    typedef uint8_t muiNavigation;

    enum
    {
        mui_navigateUp = 0,
        mui_navigateDown = 1,
        mui_navigateLeft = 2,
        mui_navigateRight = 3,
        // Sequentially, as Tab and Shift+Tab.
        mui_navigateNext = 4,
        mui_navigatePrevious = 5,
        // A gamepad's confirm and back buttons.
        mui_navigateActivate = 6,
        mui_navigateCancel = 7,
    };

    typedef struct muiNavigationEvent
    {
        uint64_t timeNs;
        muiNavigation action;
        uint8_t player;
    } muiNavigationEvent;

    // A wheel or touchpad scroll at a point: turns in detents, fractional
    // for smooth wheels and touchpads, positive y away from the user and
    // positive x to the right, as Maul Window gives them.
    typedef struct muiWheelEvent
    {
        uint64_t timeNs;
        // The point, in the space the root's rectangle is in.
        float x;
        float y;
        float deltaX;
        float deltaY;
        muiModifiers modifiers;
        uint8_t player;
    } muiWheelEvent;

    // What an event is; its fields below say which carry it.
    typedef uint8_t muiEventKind;

    enum
    {
        // key, code, modifiers, repeat.
        mui_eventKeyDown = 1,
        mui_eventKeyUp = 2,
        // text, length.
        mui_eventText = 3,
        // navigation.
        mui_eventNavigation = 4,
        // pointer.
        mui_eventPointer = 5,
        // wheel.
        mui_eventWheel = 6,
    };

    // A routed event, the same for every node on the route.
    typedef struct muiEvent
    {
        muiEventKind kind;
        uint8_t player;
        muiNavigation navigation;
        bool repeat;
        muiModifiers modifiers;
        muiKeyCode code;
        muiKey key;
        uint32_t length;
        uint64_t timeNs;
        // The node the event is for: the end of the tunnel, the start of
        // the bubble.
        muiNodeId target;
        const char* text;
        const muiPointerRecord* pointer;
        const muiWheelEvent* wheel;
    } muiEvent;

    // Which way an event travels when a node hears it.
    typedef uint8_t muiPhase;

    enum
    {
        // From the top of the target's tree down to the target.
        mui_phaseTunnel = 0,
        // From the target up to the top of its tree.
        mui_phaseBubble = 1,
    };

    /// The host's function for routed events: whether it handled the
    /// event at the node, which ends the route. It may edit the tree,
    /// focus and style; it may not feed input or dispatch (the calls that
    /// do are refused while it runs). The route was fixed before the first
    /// call, so nodes it destroys are passed over and nodes it adds hear
    /// nothing of this event.
    typedef bool (*muiEventFunction)(void* user, muiNodeId nodeId, muiPhase phase,
                                     const muiEvent* event);

    /// Sets the function routed events go to; NULL routes nothing, so
    /// every input is unhandled but for the library's defaults.
    ///
    /// @param context   The context.
    /// @param function  The function, or NULL.
    /// @param user      Passed to the function.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context or a
    ///         call from a measure, paint or event function.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiSetEventFunction(muiContext* context,
                                                        muiEventFunction function, void* user);

    /// Routes a key to the player's focus under the root (a focus a modal
    /// layer covers: the top modal layer under the root; none: the root).
    /// Unhandled, a key down does what the library does by default: Tab,
    /// with Shift or not and no other modifier, moves the focus as
    /// muiFocus_Move does. Escape cancels every drag going
    /// (maul-ui/pointer.h). On a focused range (maul-ui/range.h), the
    /// unmodified keys of ARIA's slider pattern change its value: arrows
    /// along its axis a step, Page Up and Down a page, Home and End the
    /// ends. Else an arrow without modifiers, when the focus is
    /// in a scroll container along its axis (maul-ui/scroll.h), moves
    /// the focus to the candidate directional navigation finds inside it
    /// if that lies within half a scrollport of the visible part (or a
    /// link leads out), else steps the container a line while it can
    /// move, as Android's ScrollView does; otherwise it moves the focus
    /// as muiFocus_MoveToward does. Page Up and Down, Home and End
    /// without modifiers, and Space with Shift or not and no other
    /// modifier, step the vertical scroll container holding the focus a
    /// page or to an end. What moves is handled. A key down makes the
    /// player's next focus by code shown.
    ///
    /// @param context     The context.
    /// @param rootId      The root of the subtree the player's input is for.
    /// @param event       The key: a player below MUI_MAX_PLAYERS.
    /// @param handledOut  Receives whether the UI handled it; what it did
    ///                    not is the game's.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an event outside the above, or a call from a
    ///         measure, paint or event function; `mui_errorStale` for a
    ///         root that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiKeyInput(muiContext* context, muiNodeId rootId,
                                                const muiKeyEvent* event, bool* handledOut);

    /// Routes text to the player's focus under the root, as muiKeyInput
    /// routes a key; text has no default.
    ///
    /// @param context     The context.
    /// @param rootId      The root.
    /// @param event       The text: non-NULL when its length is not 0, a
    ///                    player below MUI_MAX_PLAYERS.
    /// @param handledOut  Receives whether the UI handled it.
    /// @return As muiKeyInput's.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextInput(muiContext* context, muiNodeId rootId,
                                                 const muiTextEvent* event, bool* handledOut);

    /// Routes a navigation action to the player's focus under the root, as
    /// muiKeyInput routes a key. Unhandled, the four directions do what
    /// arrows do, a focused range's included, next and previous move the focus as muiFocus_Move
    /// does; a move is handled. Activate and cancel have no
    /// default. It makes the player's next focus by code shown.
    ///
    /// @param context     The context.
    /// @param rootId      The root.
    /// @param event       The action: a known one, a player below
    ///                    MUI_MAX_PLAYERS.
    /// @param handledOut  Receives whether the UI handled it.
    /// @return As muiKeyInput's.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNavigationInput(muiContext* context, muiNodeId rootId,
                                                       const muiNavigationEvent* event,
                                                       bool* handledOut);

    /// Routes a wheel turn to the node under its point, as muiHitTest
    /// finds it; a point over nothing is routed nowhere and not handled.
    /// Unhandled, it scrolls by the scroll rule's step a detent
    /// (maul-ui/scroll.h): the scroll container it scrolled last, while
    /// turns keep coming within the rule's latch time and the point stays
    /// over that container; otherwise the nearest scroll container from
    /// the node up that can move that way, not past the root of the
    /// node's layer. Shift turns a vertical-only turn horizontal, as on
    /// Windows. A turn a scroll container takes is handled, even at its
    /// end while latched.
    ///
    /// @param context     The context.
    /// @param rootId      The root.
    /// @param event       The turn: a finite point and deltas, a player
    ///                    below MUI_MAX_PLAYERS.
    /// @param handledOut  Receives whether the UI handled it.
    /// @return As muiKeyInput's.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiWheelInput(muiContext* context, muiNodeId rootId,
                                                  const muiWheelEvent* event, bool* handledOut);

    /// Routes a pointer record (muiNextPointerRecord) to its node; a record
    /// with no node, or one gone, is routed nowhere and not handled.
    /// Unhandled, a press on a range or inside it, and a drag of one or of
    /// a node inside it, change its value (maul-ui/range.h), and are
    /// handled. The host hands what is not handled and passes through to
    /// the game.
    ///
    /// @param context     The context.
    /// @param record      The record.
    /// @param handledOut  Receives whether the UI handled it.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         call from a measure, paint or event function.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiDispatchPointerRecord(muiContext* context,
                                                             const muiPointerRecord* record,
                                                             bool* handledOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_EVENT_H
