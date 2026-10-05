// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// XCB, opened at run time (mwin-0006) into a table each context loads for
// itself: libxcb, and where they are there libxcb-randr for monitors,
// libxkbcommon-x11 and libxcb-xkb for the keyboard, libxcb-cursor for
// cursor themes, and libxcb-xinput for raw motion. The X server's replies come from the C library's
// malloc and go back through mwinReleaseSystemMemory.

#ifndef MAUL_WINDOW_SRC_X11_API_H
#define MAUL_WINDOW_SRC_X11_API_H

#include "maul-window/base.h"

#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xinput.h>
#include <xcb/xkb.h>
#include <xkbcommon/xkbcommon-x11.h>

typedef struct mwinX11Api
{
    void* library;
    typeof(xcb_connect)* connect;
    typeof(xcb_disconnect)* disconnect;
    typeof(xcb_connection_has_error)* connectionHasError;
    typeof(xcb_get_setup)* getSetup;
    typeof(xcb_setup_roots_iterator)* setupRootsIterator;
    typeof(xcb_screen_next)* screenNext;
    typeof(xcb_generate_id)* generateId;
    typeof(xcb_flush)* flush;
    typeof(xcb_poll_for_event)* pollForEvent;
    typeof(xcb_get_extension_data)* getExtensionData;
    typeof(xcb_request_check)* requestCheck;
    typeof(xcb_create_window_checked)* createWindowChecked;
    typeof(xcb_destroy_window)* destroyWindow;
    typeof(xcb_map_window)* mapWindow;
    typeof(xcb_unmap_window)* unmapWindow;
    typeof(xcb_configure_window)* configureWindow;
    typeof(xcb_change_property)* changeProperty;
    typeof(xcb_delete_property)* deleteProperty;
    typeof(xcb_get_maximum_request_length)* maximumRequestLength;
    typeof(xcb_intern_atom)* internAtom;
    typeof(xcb_intern_atom_reply)* internAtomReply;
    typeof(xcb_get_property)* getProperty;
    typeof(xcb_get_property_reply)* getPropertyReply;
    typeof(xcb_get_property_value)* getPropertyValue;
    typeof(xcb_get_property_value_length)* getPropertyValueLength;
    typeof(xcb_get_atom_name)* getAtomName;
    typeof(xcb_get_atom_name_reply)* getAtomNameReply;
    typeof(xcb_get_atom_name_name)* getAtomNameName;
    typeof(xcb_get_atom_name_name_length)* getAtomNameNameLength;
    typeof(xcb_send_event)* sendEvent;
    typeof(xcb_set_input_focus)* setInputFocus;
    typeof(xcb_translate_coordinates)* translateCoordinates;
    typeof(xcb_translate_coordinates_reply)* translateCoordinatesReply;
    typeof(xcb_change_window_attributes)* changeWindowAttributes;
    typeof(xcb_create_pixmap)* createPixmap;
    typeof(xcb_free_pixmap)* freePixmap;
    typeof(xcb_create_cursor)* createCursor;
    typeof(xcb_free_cursor)* freeCursor;
    typeof(xcb_grab_pointer)* grabPointer;
    typeof(xcb_grab_pointer_reply)* grabPointerReply;
    typeof(xcb_ungrab_pointer)* ungrabPointer;
    typeof(xcb_warp_pointer)* warpPointer;
    typeof(xcb_set_selection_owner)* setSelectionOwner;
    typeof(xcb_get_selection_owner)* getSelectionOwner;
    typeof(xcb_get_selection_owner_reply)* getSelectionOwnerReply;
    typeof(xcb_convert_selection)* convertSelection;
    // libxcb-randr, NULL where it is missing.
    void* randrLibrary;
    xcb_extension_t* randrId;
    typeof(xcb_randr_query_version)* randrQueryVersion;
    typeof(xcb_randr_query_version_reply)* randrQueryVersionReply;
    typeof(xcb_randr_select_input)* randrSelectInput;
    typeof(xcb_randr_get_monitors)* randrGetMonitors;
    typeof(xcb_randr_get_monitors_reply)* randrGetMonitorsReply;
    typeof(xcb_randr_get_monitors_monitors_iterator)* randrMonitorsIterator;
    typeof(xcb_randr_monitor_info_next)* randrMonitorInfoNext;
    // libxkbcommon-x11 and libxcb-xkb, NULL where either is missing.
    void* xkbX11Library;
    void* xcbXkbLibrary;
    typeof(xkb_x11_setup_xkb_extension)* xkbSetupExtension;
    typeof(xkb_x11_get_core_keyboard_device_id)* xkbCoreDevice;
    typeof(xkb_x11_keymap_new_from_device)* xkbKeymapFromDevice;
    typeof(xkb_x11_state_new_from_device)* xkbStateFromDevice;
    typeof(xcb_xkb_select_events_aux)* xkbSelectEvents;
    typeof(xcb_xkb_per_client_flags)* xkbPerClientFlags;
    typeof(xcb_xkb_per_client_flags_reply)* xkbPerClientFlagsReply;
    // libxcb-cursor, NULL where it is missing.
    void* cursorLibrary;
    typeof(xcb_cursor_context_new)* cursorContextNew;
    typeof(xcb_cursor_load_cursor)* cursorLoad;
    typeof(xcb_cursor_context_free)* cursorContextFree;
    // libxcb-xinput, NULL where it is missing.
    void* xinputLibrary;
    xcb_extension_t* xinputId;
    typeof(xcb_input_xi_query_version)* xiQueryVersion;
    typeof(xcb_input_xi_query_version_reply)* xiQueryVersionReply;
    typeof(xcb_input_xi_select_events)* xiSelectEvents;
    typeof(xcb_input_raw_button_press_valuator_mask)* rawValuatorMask;
    typeof(xcb_input_raw_button_press_valuator_mask_length)* rawValuatorMaskLength;
    typeof(xcb_input_raw_button_press_axisvalues_raw)* rawAxisValues;
    typeof(xcb_input_raw_button_press_axisvalues_raw_length)* rawAxisValuesLength;
    typeof(xcb_input_button_press_valuator_mask)* valuatorMask;
    typeof(xcb_input_button_press_valuator_mask_length)* valuatorMaskLength;
    typeof(xcb_input_button_press_axisvalues)* axisValues;
    typeof(xcb_input_button_press_axisvalues_length)* axisValuesLength;
    typeof(xcb_input_xi_query_device)* xiQueryDevice;
    typeof(xcb_input_xi_query_device_reply)* xiQueryDeviceReply;
    typeof(xcb_input_xi_query_device_infos_iterator)* deviceInfos;
    typeof(xcb_input_xi_device_info_next)* deviceInfoNext;
    typeof(xcb_input_xi_device_info_classes_iterator)* deviceClasses;
    typeof(xcb_input_device_class_next)* deviceClassNext;
    typeof(xcb_input_xi_ungrab_device)* xiUngrabDevice;
} mwinX11Api;

// Opens libxcb and fills the table: mwin_errorUnsupported when it or a
// function is missing. The other libraries are opened too when they
// are there.
mwinResult mwinLoadX11(mwinX11Api* api);

// Closes what mwinLoadX11 opened.
void mwinUnloadX11(mwinX11Api* api);

#endif // MAUL_WINDOW_SRC_X11_API_H
