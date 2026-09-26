# Curated game saves (ADR-0057): a declared projection of a World's
# persistent entities captured into canonical, digested bytes, read as
# hostile input, and applied through one command buffer.
rawframe_module(NAME world_save
    SOURCES src/save.cpp src/store.cpp
    DEPS world schema execution diagnostics result base
)

# The directory store is where there are files (RAWFRAME_FILE_SYSTEM); the
# web build has none.
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    rawframe_module_tests(NAME world_save SOURCES tests/save_test.cpp)
else()
    rawframe_module_tests(NAME world_save SOURCES tests/save_test.cpp tests/store_test.cpp)
endif()
rawframe_module_fuzz(NAME world_save TARGET save SOURCES tests/fuzz_save.cpp)
