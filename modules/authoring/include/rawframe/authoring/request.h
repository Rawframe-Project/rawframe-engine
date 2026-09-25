#pragma once

// Authoring on the wire (SPEC-0040): a request document naming operations,
// the discovery document publishing every operation whole, and the one
// error record every failure is written as. All three are strict JSON under
// the authored-document profile, so a tool or an agent can build a valid
// call from discovery alone.
//
//   {
//     "formatVersion": 1,
//     "kind": "authoring.request",
//     "batch": "atomic",
//     "operations": [
//       {"operation": "scene.create_entity", "entity": "<uuid>", "name": "door"},
//       {"operation": "scene.set_field", "entity": "<uuid>",
//        "component": "<uuid>", "field": "x", "value": {"real": 1.5}}
//     ]
//   }
//
// `batch` is `atomic` (SPEC-0040's AtomicBatch), `continue_per_item`, or
// `halt_remaining` (its IndependentBatch). An optional `expects` names the
// document's generation the request was computed against. Each operation
// holds exactly the inputs its declaration lists: entities and components
// as UUID text, a place as an integer, a name or field as text, a value as
// null (the default) or one of `{"signed": "<integer text>"}`,
// `{"unsigned": "<integer text>"}`, `{"real": <number>}`, and
// `{"truth": <bool>}`, and a target as UUID text or null. Integers travel as
// text so none is rounded.

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring/operations.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring {

enum class Batch : std::uint8_t {
    Atomic,
    ContinuePerItem,
    HaltRemaining,
};

struct Request {
    Batch batch = Batch::Atomic;
    std::optional<std::string> expects;
    std::vector<Operation> operations;
};

/// Reads a request; refuses (`ValidationFailed`, with the operation's index
/// as `index`) anything out of the form above, and (`UnsupportedOperation`)
/// an operation this surface generation does not have.
[[nodiscard]] result::Result<Request> readRequest(std::string_view text);

/// Every operation, its inputs and their types, its history class and
/// targets, and every error code, under the surface generation.
[[nodiscard]] std::string writeDiscovery();

/// SPEC-0040's error code of an authoring error, `validation_failed` and
/// the rest; `internal` for any other domain's.
[[nodiscard]] std::string_view codeName(const result::Error& error) noexcept;

/// The one error record: `code`, `class`, `message`, and `details`, the
/// error's context as text fields.
[[nodiscard]] document::Value errorRecord(const result::Error& error);

} // namespace rawframe::authoring
