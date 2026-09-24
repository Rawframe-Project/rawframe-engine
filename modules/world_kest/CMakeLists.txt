# World systems written in Kest: a Kest function run once per matching
# archetype over lent columns, with written columns journaled and committed
# only when every call succeeds. Separate from rawframe.world and rawframe.kest
# because neither should know the other.
rawframe_module(NAME world_kest
    SOURCES src/kest_systems.cpp
    DEPS world_runtime kest world composition schema execution diagnostics result base
)

rawframe_module_tests(NAME world_kest SOURCES tests/kest_systems_test.cpp)
