# A client's text (ADR-0050, SPEC-0033): the game's string tables and
# translations read from its content into one catalog. Client only: a
# dedicated server links none of this (tools/check_repo.py holds it so).
rawframe_module(NAME world_localization
    SOURCES src/world_localization.cpp
    DEPS localization world_kest game_content content composition execution diagnostics result base
)

rawframe_module_tests(NAME world_localization SOURCES tests/world_localization_test.cpp)
