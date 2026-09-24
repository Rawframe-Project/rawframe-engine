# Composition (SPEC-0005, ADR-0068, SPEC-0049 as amended by D16): participant
# declarations, registrars, plan validation, and the participant lifecycle.
rawframe_module(NAME composition
    SOURCES
        src/composition.cpp
        src/plan.cpp
    DEPS execution diagnostics result base
)

rawframe_module_tests(NAME composition SOURCES
    tests/lifecycle_test.cpp
    tests/plan_test.cpp
)
