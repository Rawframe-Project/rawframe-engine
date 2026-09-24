# Failure values (ADR-0009, SPEC-0004, SPEC-0050): Error, Result, Status, the
# propagation macros, and bounded formatting.
rawframe_module(NAME result SOURCES src/error.cpp src/format.cpp DEPS base)

rawframe_module_tests(NAME result SOURCES
    tests/error_test.cpp
    tests/result_test.cpp
    tests/format_test.cpp
)
