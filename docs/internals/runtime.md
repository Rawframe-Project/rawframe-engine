# Runtime

The foundation every program stands on: how failures are reported, how work runs, how a program is composed and how it starts and stops. The modules are `base`, `result`, `diagnostics`, `execution`, `composition` and `host`, in that order of dependency.

## Contents

- [Failures](#failures)
- [Assertions and the fatal path](#assertions-and-the-fatal-path)
- [Diagnostics](#diagnostics)
- [Work and time](#work-and-time)
- [Composition](#composition)
- [The Host](#the-host)

## Failures

The engine is built without exceptions and without RTTI. A function that can fail returns `result::Result<T>`, an alias of `std::expected<T, Error>`, or `result::Status` when it has no value. Every such function is `[[nodiscard]]`, and engine code never calls `.value()` on one: it throws on failure, so `tools/check_repo.py` refuses it. Code tests `has_value()` and dereferences, or propagates with `RAWFRAME_TRY` and `RAWFRAME_TRY_ASSIGN`, and builds a failure with `return fail(...)`.

An `Error` carries:

- a **class** from a closed vocabulary (`invalid_argument`, `failed_precondition`, `out_of_range`, `not_found`, `already_exists`, `conflict`, `unauthenticated`, `permission_denied`, `unsupported`, `resource_exhausted`, `unavailable`, `data_loss`, `internal`), which says how a caller should react;
- a **domain**, a stable 128-bit value its owning module declares once, and a **code** within it, which together say exactly what failed;
- bounded context fields and a cause chain.

Every bound truncates and says so rather than failing. Cancellation is not an error: it is a task's outcome. Nothing durable is keyed by an error's text, and an error never holds a secret, a full local path or personal data.

## Assertions and the fatal path

`rawframe/base/assert.h` has three macros. `RAWFRAME_ASSERT` states an internal invariant and is compiled in the debug and development configurations only, so its condition must have no effect the program needs. `RAWFRAME_CHECK` and `RAWFRAME_PANIC` are contract checks that stay in every build. The assertion level is a build input set from the configuration; a translation unit built outside the build system fails rather than guess it.

Input from outside the process (files, network data, scripts, arguments) is never handled by an assertion: it is validated and refused with an error in every build. A broken assertion or check takes the fatal path, which writes a bounded crash record to standard error and ends the process as it would have ended; it never allocates on the way.

## Diagnostics

An owner reports through an `Emitter` handed to it at construction; a default-constructed one is disabled, and there is no global emitter to fall back to. A record is a log record, a span or a metric, each with an identity of a domain and a code built only from constants, so text from untrusted input can never become an identity. Fields carry a clearance; a `Secret` field is removed before any sink writes.

The NDJSON sink serializes, truncates and redacts on the emitting thread into a buffer allocated once, and never performs I/O there; `drain` hands finished bytes to a writer. A full buffer drops the newest record and counts it. A rate limiter passes each identity's log records at most a set number of times per window and carries the count of what it held back; spans and metrics are never limited, since gaps in them would mislead.

## Work and time

`execution` owns threads. A Host has at most one executor of each kind: CPU work, sized from the processors the process may actually use (its affinity and, on Linux, its cgroup quota, never the machine's count alone), and blocking I/O. Work is submitted by an admitted owner under a quota; there is no default quota, and `critical` priority is for trusted owners only. Priority never decides gameplay order.

- A **job scope** runs work that finishes before the scope does, so jobs may borrow anything that outlives it. Submission cannot fail: when the queue or quota is full, the submitting thread runs the job itself. Waiting helps run queued work rather than blocking a worker.
- **Cancellation** is a tree that follows ownership: a scope is created from its parent, cancelled when it is, and destroyed before it. Cancellation is cooperative and flows parent to child only.
- **Time** is monotonic, in nanoseconds, and never converted from wall time. The Host owns the clock; tests own a `ManualClock` that moves only when told to.

On the web, which has no threads, executors are drained by the Host, locks are empty, and every wait runs the work it waits on; a wait still unsatisfied after that is a deadlock and takes the fatal path.

Every capacity and limit of this layer lives in `rawframe/execution/bounds.h`, and no other source spells one as a literal; the check enforces it.

## Composition

A program is a composition of **participants**. Each declares:

- the capabilities it provides and requires, and those it may lend;
- the roles (`DedicatedServer`, `Client`, `Tool`, `Test`) and platforms it is eligible for;
- its lifetime scope (`Host`, `Runtime` or `World`; a participant depends only on ones that live at least as long);
- the executors it needs and its quota on each;
- its stop budget, which must be positive, and nested inside its parent's;
- the Host phases it runs in.

Modules register their participants through a registrar that the host program calls explicitly; nothing registers itself through a static object. The plan is built before anything is constructed: it picks one provider per capability, refuses a missing or doubled one, checks every budget's nesting, and reports every problem at once. A participant runs only in the phases it declared, and only through what its context hands it.

## The Host

The Host owns the process's authorities (the clock, the executors, the root cancellation scope, the configuration and the files it may read) and lends them to the composition. Its lifecycle moves one way: `starting`, `preparing`, `ready`, `active`, `draining`, `stopping`, `stopped`, with `failed` from any state before draining. Admission of new connections is open only while `active`. Health (`healthy`, `degraded`, `unhealthy`) is separate: a healthy Host may be draining, and an unhealthy one is drained and stopped.

A stop request (SIGINT, SIGTERM, or the console's control events on Windows) starts a drain: the Host waits for the connections participants still serve, up to `host.drain_ms`, then quiesces and stops participants in reverse start order, dependents before their dependencies, each within its budget.

Each iteration runs the phases in order. A World that falls behind catches up at most `world.maximum_ticks_per_iteration` ticks an iteration; behind by `world.degraded_ms` it is degraded and closes admission, and behind by `world.debt_limit_ms`, or degraded for `world.overload_ms`, it is overloaded. A World paused, or held at a debugger's breakpoint, owes no ticks for the time it stood still.

`host::hostMain` is every program's entry: it reads `--config`, whose relative paths are under the file's own directory, bridges the stop signals, writes diagnostics to standard output, and returns the Host's exit code.
