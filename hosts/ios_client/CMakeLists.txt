# The iOS client (D586): the client as an application UIKit runs, in the
# simulator. Only on iOS, where the window system and the device layer are
# built.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" OR NOT TARGET rawframe_input_window OR NOT TARGET rawframe_render_canvas_gpu)
    return()
endif()

rawframe_host(NAME ios_client OUTPUT rawframe-client
    SOURCES src/main.cpp
    DEPS window_host host process game_content input_kest network_quic window world_animation world_audio render render_canvas
         render_canvas_gpu world_ui render_scene render_scene_gpu world_kest world_localization world_replication
         world_runtime composition diagnostics result base
)
# An application bundle whose Info.plist gives Maul Window's scenes their
# manifest (ios/Info.plist.in).
set_target_properties(rawframe_host_ios_client PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER dev.rawframe.client
    MACOSX_BUNDLE_BUNDLE_NAME Rawframe
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
