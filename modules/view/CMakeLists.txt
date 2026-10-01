# A view's geometry (ADR-0052's picking and projection, D366): the eye's
# axes from its aim, and the four verbs between view points and the World,
# perspective and orthographic, from declared camera state alone. The
# scene and the canvas draw by the same geometry. Client and tools only.
rawframe_module(NAME view
    SOURCES src/view.cpp
    DEPS base
)

rawframe_module_tests(NAME view SOURCES tests/view_test.cpp)
