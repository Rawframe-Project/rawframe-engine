#include "rawframe/document/record.h"

#include "rawframe/document/errors.h"

#include <algorithm>
#include <utility>

namespace rawframe::document {

namespace {

std::unexpected<result::Error> refuse(DocumentError error, std::string_view path, std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kDocumentDomain, code(error), why)
            .error()
            .withContext("path", path)};
}

std::string_view kindName(Value::Kind kind) noexcept {
    switch (kind) {
    case Value::Kind::Null:
        return "null";
    case Value::Kind::Bool:
        return "a truth";
    case Value::Kind::Number:
        return "a number";
    case Value::Kind::String:
        return "a string";
    case Value::Kind::Array:
        return "an array";
    case Value::Kind::Object:
        return "an object";
    }
    return "a value";
}

} // namespace

std::unexpected<result::Error> invalid(std::string_view path, std::string_view why) {
    return refuse(DocumentError::Invalid, path, why);
}

std::unexpected<result::Error> notCanonical(std::string_view path, std::string_view why) {
    return refuse(DocumentError::NotCanonical, path, why);
}

result::Result<Record> Record::of(const Value& value, std::span<const std::string_view> fields, std::string path) {
    if (value.kind() != Value::Kind::Object) {
        return invalid(path, "a record is an object");
    }
    std::size_t last = 0;
    for (const std::string& name : value.names()) {
        const auto kFound = std::ranges::find(fields, name);
        if (kFound == fields.end()) {
            return invalid(path + "." + name, "no such field in this record");
        }
        const auto kIndex = static_cast<std::size_t>(kFound - fields.begin()) + 1;
        if (kIndex < last) {
            return notCanonical(path + "." + name, "fields are written in their declared order");
        }
        last = kIndex;
    }
    return Record{value, std::move(path)};
}

std::string Record::pathOf(std::string_view field) const {
    return path_ + "." + std::string{field};
}

result::Result<const Value*> Record::optional(std::string_view field, Value::Kind kind) const {
    const Value* found = value_->find(field);
    if (found != nullptr && found->kind() != kind) {
        return invalid(pathOf(field), std::string{"the field is "} + std::string{kindName(kind)});
    }
    return found;
}

result::Result<const Value*> Record::required(std::string_view field, Value::Kind kind) const {
    RAWFRAME_TRY_ASSIGN(const Value* found, optional(field, kind));
    if (found == nullptr) {
        return invalid(pathOf(field), "the field is required");
    }
    return found;
}

result::Result<std::string_view> Record::text(std::string_view field) const {
    RAWFRAME_TRY_ASSIGN(const Value* found, required(field, Value::Kind::String));
    return std::string_view{*found->text()};
}

result::Result<std::optional<std::string_view>> Record::optionalText(std::string_view field) const {
    RAWFRAME_TRY_ASSIGN(const Value* found, optional(field, Value::Kind::String));
    return found == nullptr ? std::nullopt : std::optional{std::string_view{*found->text()}};
}

result::Result<std::int64_t> Record::integer(std::string_view field) const {
    RAWFRAME_TRY_ASSIGN(const Value* found, required(field, Value::Kind::Number));
    const auto kNumber = found->integer();
    if (!kNumber) {
        return invalid(pathOf(field), "the field is an integer");
    }
    return *kNumber;
}

result::Result<std::int64_t> Record::integer(std::string_view field, std::int64_t fallback) const {
    if (value_->find(field) == nullptr) {
        return fallback;
    }
    RAWFRAME_TRY_ASSIGN(const std::int64_t kNumber, integer(field));
    if (kNumber == fallback) {
        return notCanonical(pathOf(field), "a field at its default is omitted");
    }
    return kNumber;
}

result::Result<double> Record::real(std::string_view field, double fallback) const {
    RAWFRAME_TRY_ASSIGN(const Value* found, optional(field, Value::Kind::Number));
    if (found == nullptr) {
        return fallback;
    }
    const double kNumber = *found->real();
    if (kNumber == fallback) {
        return notCanonical(pathOf(field), "a field at its default is omitted");
    }
    return kNumber;
}

result::Result<bool> Record::truth(std::string_view field, bool fallback) const {
    RAWFRAME_TRY_ASSIGN(const Value* found, optional(field, Value::Kind::Bool));
    if (found == nullptr) {
        return fallback;
    }
    if (*found->truth() == fallback) {
        return notCanonical(pathOf(field), "a field at its default is omitted");
    }
    return *found->truth();
}

} // namespace rawframe::document
