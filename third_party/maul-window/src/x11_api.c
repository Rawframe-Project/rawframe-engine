// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opening XCB.

#include "x11_api.h"

#include <dlfcn.h>
#include <string.h>

// A symbol of a library by name. A function comes back as data from
// dlsym and is copied, not cast, into its pointer.
static bool Find(void* library, const char* name, void* target, size_t size)
{
    void* symbol = dlsym(library, name);
    if (symbol == nullptr)
    {
        return false;
    }
    memcpy(target, (const void*)&symbol, size);
    return true;
}

#define FIND(library, field, name) Find(api->library, #name, (void*)&api->field, sizeof(api->field))

static bool FindCore(mwinX11Api* api)
{
    return FIND(library, connect, xcb_connect) && FIND(library, disconnect, xcb_disconnect) &&
           FIND(library, connectionHasError, xcb_connection_has_error) &&
           FIND(library, getSetup, xcb_get_setup) &&
           FIND(library, setupRootsIterator, xcb_setup_roots_iterator) &&
           FIND(library, screenNext, xcb_screen_next) &&
           FIND(library, generateId, xcb_generate_id) && FIND(library, flush, xcb_flush) &&
           FIND(library, pollForEvent, xcb_poll_for_event) &&
           FIND(library, getExtensionData, xcb_get_extension_data) &&
           FIND(library, requestCheck, xcb_request_check) &&
           FIND(library, createWindowChecked, xcb_create_window_checked) &&
           FIND(library, destroyWindow, xcb_destroy_window) &&
           FIND(library, mapWindow, xcb_map_window) &&
           FIND(library, unmapWindow, xcb_unmap_window) &&
           FIND(library, configureWindow, xcb_configure_window) &&
           FIND(library, changeProperty, xcb_change_property) &&
           FIND(library, deleteProperty, xcb_delete_property) &&
           FIND(library, maximumRequestLength, xcb_get_maximum_request_length) &&
           FIND(library, internAtom, xcb_intern_atom) &&
           FIND(library, internAtomReply, xcb_intern_atom_reply) &&
           FIND(library, getProperty, xcb_get_property) &&
           FIND(library, getPropertyReply, xcb_get_property_reply) &&
           FIND(library, getPropertyValue, xcb_get_property_value) &&
           FIND(library, getPropertyValueLength, xcb_get_property_value_length) &&
           FIND(library, getAtomName, xcb_get_atom_name) &&
           FIND(library, getAtomNameReply, xcb_get_atom_name_reply) &&
           FIND(library, getAtomNameName, xcb_get_atom_name_name) &&
           FIND(library, getAtomNameNameLength, xcb_get_atom_name_name_length) &&
           FIND(library, sendEvent, xcb_send_event) &&
           FIND(library, setInputFocus, xcb_set_input_focus) &&
           FIND(library, translateCoordinates, xcb_translate_coordinates) &&
           FIND(library, translateCoordinatesReply, xcb_translate_coordinates_reply) &&
           FIND(library, changeWindowAttributes, xcb_change_window_attributes) &&
           FIND(library, createPixmap, xcb_create_pixmap) &&
           FIND(library, freePixmap, xcb_free_pixmap) &&
           FIND(library, createCursor, xcb_create_cursor) &&
           FIND(library, freeCursor, xcb_free_cursor) &&
           FIND(library, grabPointer, xcb_grab_pointer) &&
           FIND(library, grabPointerReply, xcb_grab_pointer_reply) &&
           FIND(library, ungrabPointer, xcb_ungrab_pointer) &&
           FIND(library, warpPointer, xcb_warp_pointer) &&
           FIND(library, setSelectionOwner, xcb_set_selection_owner) &&
           FIND(library, getSelectionOwner, xcb_get_selection_owner) &&
           FIND(library, getSelectionOwnerReply, xcb_get_selection_owner_reply) &&
           FIND(library, convertSelection, xcb_convert_selection);
}

static bool FindRandr(mwinX11Api* api)
{
    // The extension's id is data, not a function.
    api->randrId = dlsym(api->randrLibrary, "xcb_randr_id");
    return api->randrId != nullptr &&
           FIND(randrLibrary, randrQueryVersion, xcb_randr_query_version) &&
           FIND(randrLibrary, randrQueryVersionReply, xcb_randr_query_version_reply) &&
           FIND(randrLibrary, randrSelectInput, xcb_randr_select_input) &&
           FIND(randrLibrary, randrGetMonitors, xcb_randr_get_monitors) &&
           FIND(randrLibrary, randrGetMonitorsReply, xcb_randr_get_monitors_reply) &&
           FIND(randrLibrary, randrMonitorsIterator, xcb_randr_get_monitors_monitors_iterator) &&
           FIND(randrLibrary, randrMonitorInfoNext, xcb_randr_monitor_info_next);
}

static bool FindKeyboard(mwinX11Api* api)
{
    return FIND(xkbX11Library, xkbSetupExtension, xkb_x11_setup_xkb_extension) &&
           FIND(xkbX11Library, xkbCoreDevice, xkb_x11_get_core_keyboard_device_id) &&
           FIND(xkbX11Library, xkbKeymapFromDevice, xkb_x11_keymap_new_from_device) &&
           FIND(xkbX11Library, xkbStateFromDevice, xkb_x11_state_new_from_device) &&
           FIND(xcbXkbLibrary, xkbSelectEvents, xcb_xkb_select_events_aux) &&
           FIND(xcbXkbLibrary, xkbPerClientFlags, xcb_xkb_per_client_flags) &&
           FIND(xcbXkbLibrary, xkbPerClientFlagsReply, xcb_xkb_per_client_flags_reply);
}

static bool FindCursors(mwinX11Api* api)
{
    return FIND(cursorLibrary, cursorContextNew, xcb_cursor_context_new) &&
           FIND(cursorLibrary, cursorLoad, xcb_cursor_load_cursor) &&
           FIND(cursorLibrary, cursorContextFree, xcb_cursor_context_free);
}

static bool FindXinput(mwinX11Api* api)
{
    // The extension's id is data, not a function.
    api->xinputId = dlsym(api->xinputLibrary, "xcb_input_id");
    return api->xinputId != nullptr &&
           FIND(xinputLibrary, xiQueryVersion, xcb_input_xi_query_version) &&
           FIND(xinputLibrary, xiQueryVersionReply, xcb_input_xi_query_version_reply) &&
           FIND(xinputLibrary, xiSelectEvents, xcb_input_xi_select_events) &&
           FIND(xinputLibrary, rawValuatorMask, xcb_input_raw_button_press_valuator_mask) &&
           FIND(xinputLibrary, rawValuatorMaskLength,
                xcb_input_raw_button_press_valuator_mask_length) &&
           FIND(xinputLibrary, rawAxisValues, xcb_input_raw_button_press_axisvalues_raw) &&
           FIND(xinputLibrary, rawAxisValuesLength,
                xcb_input_raw_button_press_axisvalues_raw_length) &&
           FIND(xinputLibrary, valuatorMask, xcb_input_button_press_valuator_mask) &&
           FIND(xinputLibrary, valuatorMaskLength, xcb_input_button_press_valuator_mask_length) &&
           FIND(xinputLibrary, axisValues, xcb_input_button_press_axisvalues) &&
           FIND(xinputLibrary, axisValuesLength, xcb_input_button_press_axisvalues_length) &&
           FIND(xinputLibrary, xiQueryDevice, xcb_input_xi_query_device) &&
           FIND(xinputLibrary, xiQueryDeviceReply, xcb_input_xi_query_device_reply) &&
           FIND(xinputLibrary, deviceInfos, xcb_input_xi_query_device_infos_iterator) &&
           FIND(xinputLibrary, deviceInfoNext, xcb_input_xi_device_info_next) &&
           FIND(xinputLibrary, deviceClasses, xcb_input_xi_device_info_classes_iterator) &&
           FIND(xinputLibrary, deviceClassNext, xcb_input_device_class_next) &&
           FIND(xinputLibrary, xiUngrabDevice, xcb_input_xi_ungrab_device);
}

// Opens an optional library, or leaves it NULL when it or one of its
// functions is missing.
static void OpenOptional(mwinX11Api* api, void** library, const char* name,
                         bool (*find)(mwinX11Api* api))
{
    *library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (*library != nullptr && !find(api))
    {
        dlclose(*library);
        *library = nullptr;
    }
}

mwinResult mwinLoadX11(mwinX11Api* api)
{
    memset(api, 0, sizeof(*api));
    api->library = dlopen("libxcb.so.1", RTLD_NOW | RTLD_LOCAL);
    if (api->library == nullptr || !FindCore(api))
    {
        mwinUnloadX11(api);
        return mwin_errorUnsupported;
    }
    // Each links libxcb itself, and shares the one libxcb.so.1 loaded.
    OpenOptional(api, &api->randrLibrary, "libxcb-randr.so.0", FindRandr);
    OpenOptional(api, &api->cursorLibrary, "libxcb-cursor.so.0", FindCursors);
    OpenOptional(api, &api->xinputLibrary, "libxcb-xinput.so.0", FindXinput);
    api->xcbXkbLibrary = dlopen("libxcb-xkb.so.1", RTLD_NOW | RTLD_LOCAL);
    if (api->xcbXkbLibrary != nullptr)
    {
        OpenOptional(api, &api->xkbX11Library, "libxkbcommon-x11.so.0", FindKeyboard);
    }
    if (api->xkbX11Library == nullptr && api->xcbXkbLibrary != nullptr)
    {
        dlclose(api->xcbXkbLibrary);
        api->xcbXkbLibrary = nullptr;
    }
    return mwin_success;
}

void mwinUnloadX11(mwinX11Api* api)
{
    void* optional[] = {api->randrLibrary, api->cursorLibrary, api->xkbX11Library,
                        api->xcbXkbLibrary, api->xinputLibrary};
    for (size_t i = 0; i < sizeof(optional) / sizeof(optional[0]); i++)
    {
        if (optional[i] != nullptr)
        {
            dlclose(optional[i]);
        }
    }
    if (api->library != nullptr)
    {
        dlclose(api->library);
    }
    memset(api, 0, sizeof(*api));
}
