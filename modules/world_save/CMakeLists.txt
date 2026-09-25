# Curated game saves (ADR-0057): a declared projection of a World's
# persistent entities captured into canonical, digested bytes, read as
# hostile input, and applied through one command buffer.
rawframe_module(NAME world_save
    SOURCES src/save.cpp src/store.cpp
    DEPS world schema execution diagnostics result base
)

rawframe_module_tests(NAME world_save SOURCES tests/save_test.cpp tests/store_test.cpp)
