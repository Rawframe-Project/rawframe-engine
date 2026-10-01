# A view's geometry (ADR-0052's picking and projection, D366): the eye's
# axes from its aim, and the four verbs between view points and the World,
# perspective and orthographic, from declared camera state alone. The
# scene and the canvas draw by the same geometry. And the local players'
# views as the presentation last derived them, lent by a client host
# (D367). Client and tools only.
rawframe_module(NAME view
    SOURCES src/view.cpp src/players.cpp
    DEPS composition execution diagnostics result base
)

rawframe_module_tests(NAME view SOURCES tests/view_test.cpp tests/players_test.cpp)
