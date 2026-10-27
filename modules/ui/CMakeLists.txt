# The engine's UI over Maul UI's retained core,
# private to this module: a tree of nodes the engine keys, laid out by
# Scale+Offset sizing and flexbox, styled by the classes of a game's
# ui.styles document, its node components started and nested as its
# ui.document says. Client and tools only: the dedicated server
# links none of it.
rawframe_module(NAME ui SOURCES src/access.cpp src/text_edit.cpp src/tree.cpp src/tree_edit.cpp src/tree_style.cpp src/tree_range.cpp src/styles.cpp src/document.cpp src/node_look.cpp DEPS texture composition document result base)
target_link_libraries(rawframe_ui PRIVATE maul-ui)
# AT-SPI's adapter where Maul UI builds it, Linux's.
target_compile_definitions(rawframe_ui PRIVATE RAWFRAME_UI_ATSPI=$<BOOL:${MAUL_UI_ATSPI}>
                                               RAWFRAME_UI_ANDROID=$<BOOL:${MAUL_UI_ANDROID_ACCESSIBILITY}>
                                               RAWFRAME_UI_UIA=$<BOOL:${MAUL_UI_UIA}>
                                               RAWFRAME_UI_APPKIT=$<BOOL:${MAUL_UI_NSACCESSIBILITY}>
                                               RAWFRAME_UI_UIKIT=$<BOOL:${MAUL_UI_UIACCESSIBILITY}>
                                               RAWFRAME_UI_ARIA=$<BOOL:${MAUL_UI_ARIA}>)

rawframe_module_tests(NAME ui SOURCES tests/access_test.cpp tests/edit_test.cpp tests/text_test.cpp tests/tree_test.cpp tests/scroll_test.cpp tests/range_test.cpp tests/style_test.cpp tests/styles_test.cpp tests/document_test.cpp tests/node_look_test.cpp)
if(TARGET rawframe_ui_tests)
    target_compile_definitions(rawframe_ui_tests PRIVATE RAWFRAME_UI_FONTS="${RAWFRAME_TEST_FILES}/modules/ui/tests/fonts/")
endif()
rawframe_module_fuzz(NAME ui TARGET styles RUNS 20000 SEEDS tests/seeds/styles SOURCES tests/fuzz_styles.cpp)
rawframe_module_fuzz(NAME ui TARGET document RUNS 20000 SEEDS tests/seeds/document SOURCES tests/fuzz_document.cpp)
