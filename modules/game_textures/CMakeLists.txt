# A game's textures (ADR-0025, D306): the textures its `texture` lines
# declare, read decoded by identity from cooked content and held. The
# canvas's sprites and the scene's materials read them; neither owns them.
rawframe_module(NAME game_textures
    SOURCES src/game_textures.cpp src/asked.cpp
    DEPS world_kest assets content texture execution result base
)

rawframe_module_tests(NAME game_textures SOURCES tests/game_textures_test.cpp)
