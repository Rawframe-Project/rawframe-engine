# The Host (SPEC-0005 Host, ADR-0017): the reusable process-level owner of the
# clock, diagnostics routing and its sink, the executors, and the root scope,
# and the Host schedule loop. Host executables under hosts/ stay thin on top.
rawframe_module(NAME host SOURCES src/host.cpp src/main.cpp DEPS composition execution diagnostics result base)

rawframe_module_tests(NAME host SOURCES tests/host_test.cpp)
