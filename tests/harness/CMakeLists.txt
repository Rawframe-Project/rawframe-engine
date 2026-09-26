add_library(rawframe_test STATIC src/harness.cpp src/allocations.cpp src/files.cpp)
# Scratch directories where there are files to write.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    target_sources(rawframe_test PRIVATE src/scratch.cpp)
endif()
add_library(rawframe::test ALIAS rawframe_test)
target_include_directories(rawframe_test PUBLIC include)
target_link_libraries(rawframe_test PUBLIC rawframe_policy)
