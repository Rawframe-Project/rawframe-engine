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

# Natively the executors have workers, and their tests race them; the web
# build has none, and its own test runs everything on the one thread.
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    rawframe_module_tests(NAME execution SOURCES tests/cancellation_test.cpp tests/single_thread_test.cpp)
else()
    rawframe_module_tests(NAME execution SOURCES
        tests/cancellation_test.cpp
        tests/executor_test.cpp
        tests/job_scope_test.cpp
        tests/operation_test.cpp
    )
endif()

# The executors are the only place the engine starts threads, where the
# target has them; the web build has none (rawframe/base/threads.h).
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    find_package(Threads REQUIRED)
    target_link_libraries(rawframe_execution PUBLIC Threads::Threads)
endif()
