# The engine's side of Kest (ADR-0084): compiled programs, the doors the engine
# binds, and machines with finite limits. Kest's C API is linked privately, so
# no header above this module can include it.
rawframe_module(NAME kest
    SOURCES src/program.cpp src/doors.cpp src/machine.cpp
    DEPS execution result base
)
target_link_libraries(rawframe_kest PRIVATE kest::kest)

rawframe_module_tests(NAME kest SOURCES tests/kest_test.cpp)
if(TARGET rawframe_kest_tests)
    target_compile_definitions(rawframe_kest_tests PRIVATE RAWFRAME_KEST_LIBRARY="${RAWFRAME_KEST_LIBRARY}")
endif()
