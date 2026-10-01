# The engine's UI (ADR-0034, SPEC-0030, D374) over Maul UI's retained core,
# private to this module: a tree of nodes the engine keys, laid out by
# Scale+Offset sizing and flexbox. Client and tools only: the dedicated
# server links none of it.
rawframe_module(NAME ui SOURCES src/tree.cpp DEPS result base)
target_link_libraries(rawframe_ui PRIVATE maul-ui)

rawframe_module_tests(NAME ui SOURCES tests/text_test.cpp tests/tree_test.cpp)
if(TARGET rawframe_ui_tests)
    target_compile_definitions(rawframe_ui_tests PRIVATE RAWFRAME_UI_FONTS="${CMAKE_CURRENT_SOURCE_DIR}/tests/fonts/")
endif()
