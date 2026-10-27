add_library(rawframe_test STATIC src/harness.cpp src/allocations.cpp src/files.cpp)
# Scratch directories where there are files to write.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    target_sources(rawframe_test PRIVATE src/scratch.cpp)
endif()
add_library(rawframe::test ALIAS rawframe_test)
target_include_directories(rawframe_test PUBLIC include)
target_link_libraries(rawframe_test PUBLIC rawframe_policy)

# The reaper the check runs ctest under ends what a test leaves running,
# an orphan's own children too, and names each (Linux only).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME harness_reap COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/reap.sh ${PROJECT_SOURCE_DIR}/tools/reap.py)
    set_tests_properties(harness_reap PROPERTIES PASS_REGULAR_EXPRESSION "the reaper ended an orphan and its child")
endif()
