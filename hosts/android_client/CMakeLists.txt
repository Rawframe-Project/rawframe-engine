# The Android client (D551): the client as the shared library Maul Window's
# activity loads and starts. Only on Android, where the window system and
# the device layer are built.
if(NOT ANDROID OR NOT TARGET rawframe_input_window OR NOT TARGET rawframe_render_canvas_gpu)
    return()
endif()

rawframe_host(NAME android_client OUTPUT rawframe_client LIBRARY
    SOURCES src/main.cpp
    DEPS window_host host game_content input_kest network_quic window world_animation world_audio render render_canvas
         render_canvas_gpu world_ui render_scene render_scene_gpu world_kest world_localization world_replication
         world_runtime composition diagnostics result base
)
# The activity calls the library's ANativeActivity_onCreate, which nothing in
# it references: kept, and exported, from Maul Window's static library.
target_link_options(rawframe_host_android_client PRIVATE "-Wl,-u,ANativeActivity_onCreate"
                    "-Wl,--export-dynamic-symbol=ANativeActivity_onCreate")
