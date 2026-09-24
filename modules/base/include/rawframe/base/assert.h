#pragma once

#include "rawframe/base/fatal.h"

#include <source_location>
#include <string_view>

// The assertion level is a build input, set by cmake/rawframe.cmake from the
// configuration (ADR-0006):
//
//   1  contract_only  shipping: RAWFRAME_CHECK and RAWFRAME_PANIC only
//   2  full           debug and development: RAWFRAME_ASSERT as well
//
// There is deliberately no default. A default would let a translation unit built
// outside the build system pick an assertion meaning silently.
#ifndef RAWFRAME_ASSERTIONS
#error "RAWFRAME_ASSERTIONS is undefined; build through cmake/rawframe.cmake, which sets it from the configuration."
#endif

#if RAWFRAME_ASSERTIONS != 1 && RAWFRAME_ASSERTIONS != 2
#error "RAWFRAME_ASSERTIONS must be 1 (contract_only) or 2 (full); no other assertion level is implemented."
#endif

/// Reports a developer invariant. Side-effect free by contract, per SPEC-0004:
/// the condition is not evaluated in `build.shipping`, so an expression whose
/// effect the program needs must not appear here.
///
/// The second argument must be a string literal and is required. The source
/// location already carries file and line; what it cannot carry is what the
/// author expected to be true.
///
/// The parameters are spelled `conditionExpression` and `messageLiteral` rather
/// than `condition` and `message` because `FatalRecord` has members of those two
/// names. A macro parameter is substituted everywhere its identifier appears,
/// designators included, so `.condition = ...` would expand to `. 1 == 1 = ...`
/// and the record would stop being constructible. The names differ on purpose.
// The `do { ... } while (false)` wrapper is what makes these single statements,
// and the reason an assertion can be the sole body
// of an `if` without changing what the `else` binds to. The analyzer's blanket
// objection to do-while does not distinguish the idiom from a loop.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while)
#if RAWFRAME_ASSERTIONS == 2
#define RAWFRAME_ASSERT(conditionExpression, messageLiteral)                                                           \
    do {                                                                                                               \
        if (!(conditionExpression)) {                                                                                  \
            ::rawframe::base::raiseFatal(::rawframe::base::FatalRecord{                                                \
                .reason = ::rawframe::base::FatalReason::AssertionFailed,                                              \
                .location = ::std::source_location::current(),                                                         \
                .condition = ::std::string_view{#conditionExpression},                                                 \
                .message = ::std::string_view{"" messageLiteral},                                                      \
                .threadIdentity = 0,                                                                                   \
            });                                                                                                        \
        }                                                                                                              \
    } while (false)
#else
// Shipping removes the evaluation and keeps the type check. SPEC-0004 permits
// removing the assertion completely; removing the text as well means a
// condition naming a renamed variable compiles here and fails in debug, so the
// defect is found by whichever configuration someone happened to build. The
// condition therefore appears in an unevaluated operand: `sizeof` type-checks
// its operand without evaluating it, and the same juxtaposition trick keeps the
// string-literal contract enforced in every configuration.
#define RAWFRAME_ASSERT(conditionExpression, messageLiteral)                                                           \
    do {                                                                                                               \
        static_cast<void>(sizeof(static_cast<bool>(conditionExpression)));                                             \
        static_cast<void>(sizeof("" messageLiteral));                                                                  \
    } while (false)
#endif

/// Reports an always-on unrecoverable condition. Evaluated exactly once in every
/// configuration. Not for untrusted or environmental input, which is a Result.
#define RAWFRAME_CHECK(conditionExpression, messageLiteral)                                                            \
    do {                                                                                                               \
        if (!(conditionExpression)) {                                                                                  \
            ::rawframe::base::raiseFatal(::rawframe::base::FatalRecord{                                                \
                .reason = ::rawframe::base::FatalReason::CheckFailed,                                                  \
                .location = ::std::source_location::current(),                                                         \
                .condition = ::std::string_view{#conditionExpression},                                                 \
                .message = ::std::string_view{"" messageLiteral},                                                      \
                .threadIdentity = 0,                                                                                   \
            });                                                                                                        \
        }                                                                                                              \
    } while (false)

/// Terminates unconditionally. Non-returning in every configuration.
///
/// A function would satisfy the analyzer here and would break the family: the
/// message must be a string literal, which the juxtaposition below enforces and a
/// parameter cannot, and the source location must be the call site's.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define RAWFRAME_PANIC(messageLiteral)                                                                                 \
    ::rawframe::base::raiseFatal(::rawframe::base::FatalRecord{                                                        \
        .reason = ::rawframe::base::FatalReason::Panic,                                                                \
        .location = ::std::source_location::current(),                                                                 \
        .condition = ::std::string_view{},                                                                             \
        .message = ::std::string_view{"" messageLiteral},                                                              \
        .threadIdentity = 0,                                                                                           \
    })

// NOLINTEND(cppcoreguidelines-avoid-do-while)
