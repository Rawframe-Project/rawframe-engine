# Scenario tests: whole slices of the engine working together, which no single
# module's tests may reach because of the module graph.
add_executable(rawframe_scenario_tests replication_test.cpp)
target_link_libraries(rawframe_scenario_tests PRIVATE
    rawframe_policy rawframe::world_replication rawframe::network_loopback rawframe::test)
add_test(NAME scenarios COMMAND rawframe_scenario_tests)
