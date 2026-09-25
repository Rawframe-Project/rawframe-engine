#pragma once

// Binding records, shared by action sets and override documents: an action
// set's binding carries its slot and device class, an override entry's
// binding takes them from the entry.

#include "rawframe/document/json.h"
#include "rawframe/input/actions.h"
#include "rawframe/result/result.h"

#include <string>

namespace rawframe::input {

/// A binding of `action` read from `value`; with `key`, its slot and device
/// class are the key's and the record has neither field.
[[nodiscard]] result::Result<Binding>
readBinding(const document::Value& value, const Action& action, const std::string& path, const Binding* key);

/// The record `readBinding` reads back to the same binding, defaults
/// omitted; with `keyed`, without its slot and device class.
[[nodiscard]] document::Value bindingDocument(const Binding& binding, bool keyed);

} // namespace rawframe::input
