# Execution (SPEC-0005, SPEC-0048): monotonic time, cancellation, task
# outcomes, the executors, scoped jobs, and async operations.
rawframe_module(NAME execution
    SOURCES
        src/budget.cpp
        src/cancellation.cpp
        src/executor.cpp
        src/job_scope.cpp
        src/operation.cpp
        src/parallelism.cpp
        src/time.cpp
    DEPS diagnostics result base
)

rawframe_module_tests(NAME execution SOURCES
    tests/cancellation_test.cpp
    tests/executor_test.cpp
    tests/job_scope_test.cpp
    tests/operation_test.cpp
)

# The executors are the only place the engine starts threads.
find_package(Threads REQUIRED)
target_link_libraries(rawframe_execution PUBLIC Threads::Threads)
