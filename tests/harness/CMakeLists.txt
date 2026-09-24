add_library(rawframe_test STATIC src/harness.cpp src/allocations.cpp)
add_library(rawframe::test ALIAS rawframe_test)
target_include_directories(rawframe_test PUBLIC include)
target_link_libraries(rawframe_test PUBLIC rawframe_policy)
