# Scenario tests: whole slices of the engine working together, which no single
# module's tests may reach because of the module graph. One program a file,
# so the files run beside each other rather than one after another.
foreach(scenario compensation game_subassets install kest_bots replication replication_commands)
    add_executable(rawframe_scenario_${scenario}_tests ${scenario}_test.cpp)
    target_link_libraries(rawframe_scenario_${scenario}_tests PRIVATE
        rawframe_policy rawframe::world_kest rawframe::physics2d rawframe::world_replication rawframe::network_loopback
        rawframe::build rawframe::install rawframe::cook rawframe::game_content rawframe::test)
    # The install scenario reads SPEC-0021's golden Build and pins the plans
    # made of it.
    target_compile_definitions(rawframe_scenario_${scenario}_tests PRIVATE
        RAWFRAME_BUILD_GOLDEN="${PROJECT_SOURCE_DIR}/modules/build/tests/golden/"
        RAWFRAME_PLAN_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/golden/"
        RAWFRAME_SAMPLE_GAMES="${PROJECT_SOURCE_DIR}/games/")
    add_test(NAME scenario_${scenario} COMMAND rawframe_scenario_${scenario}_tests)
endforeach()
