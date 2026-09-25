# The web client (ADR-0084, D169): a WebAssembly reactor over rawframe.host
# whose exports a page's JavaScript calls. Built only for the web.
rawframe_host(NAME web_client OUTPUT rawframe-web-client
    SOURCES src/main.cpp
    DEPS host game_content input_kest network_web physics2d physics3d world_animation world_replication world_kest world_runtime
         composition execution diagnostics result base
)
# A reactor: no `main`; the page calls `_initialize`, then the exports.
target_link_options(rawframe_host_web_client PRIVATE -mexec-model=reactor)

if(RAWFRAME_BUILD_TESTS)
    # Node plays the page: it holds a test game's files, starts the client,
    # drives it frame by frame, and stops it; no file system is lent.
    add_test(NAME web_client_game
             COMMAND node --no-warnings ${CMAKE_CURRENT_SOURCE_DIR}/tests/page.mjs
                     $<TARGET_FILE:rawframe_host_web_client> ${PROJECT_SOURCE_DIR}/modules/world_kest/tests/game)
    set_tests_properties(web_client_game PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"code\":\"game_loaded\".*\"code\":\"stopped\".*page: [0-9]+ frames, exit 0")
    # Two clients on one page and one relay: one serves the arena sample,
    # the other's bots join it over the web transport and are admitted.
    add_test(NAME web_client_replicated
             COMMAND node --no-warnings ${CMAKE_CURRENT_SOURCE_DIR}/tests/replicated.mjs
                     $<TARGET_FILE:rawframe_host_web_client> ${PROJECT_SOURCE_DIR}/games/arena)
    set_tests_properties(web_client_replicated PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bots\":2,\"admitted\":2.*page: both exit 0")
endif()
