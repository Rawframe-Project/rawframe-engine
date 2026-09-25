# A World heard (ADR-0038): `rawframe.sound`'s emitter and listener
# components played through the audio module. Client only: the dedicated
# server carries the components as plain values and links none of this.
rawframe_module(NAME world_audio
    SOURCES src/hearing.cpp src/sound_loader.cpp src/world_audio.cpp
    DEPS game_content assets content audio physics2d physics3d world_replication world_kest kest world composition schema execution diagnostics document result base
)

rawframe_module_tests(NAME world_audio SOURCES tests/world_audio_test.cpp)
