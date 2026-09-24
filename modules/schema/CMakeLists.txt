# Schema (ADR-0011, SPEC-0006): stable component identity, component
# descriptors, and the frozen registry a World binds to. The Kest-based schema
# source format arrives with the Kest integration.
rawframe_module(NAME schema SOURCES src/registry.cpp DEPS result base)

rawframe_module_tests(NAME schema SOURCES tests/registry_test.cpp)
