// The records of a session's operations on the game's files (D502, D503):
// cooking it, stopping a cook, and importing a file, and what the session
// says of them.

#include "records.h"

namespace rawframe::studio {

Value importRecord(std::int64_t id, std::string_view source, std::string_view as) {
    Value record = Value::object();
    record.add("kind", Value::string("authoring.import"));
    record.add("id", Value::integer(id));
    record.add("source", Value::string(std::string{source}));
    record.add("as", Value::string(std::string{as}));
    return record;
}

std::string importedIn(std::string_view reply) {
    const auto kRead = document::parse(reply);
    const Value* kAnswer = kRead.has_value() ? kRead->find("answer") : nullptr;
    const Value* kKind = kAnswer != nullptr ? kAnswer->find("asset") : nullptr;
    const Value* kPath = kAnswer != nullptr ? kAnswer->find("path") : nullptr;
    if (kKind == nullptr || kKind->text() == nullptr || kPath == nullptr || kPath->text() == nullptr) {
        return {};
    }
    return *kKind->text() + " " + *kPath->text();
}

Value cookRecord(std::int64_t id, const std::filesystem::path& output, const std::filesystem::path& cache) {
    Value record = Value::object();
    record.add("kind", Value::string("authoring.cook"));
    record.add("id", Value::integer(id));
    record.add("output", Value::string(output.string()));
    record.add("cache", Value::string(cache.string()));
    return record;
}

Value cancelRecord(std::int64_t id, std::int64_t operation) {
    Value record = Value::object();
    record.add("kind", Value::string("authoring.cancel"));
    record.add("id", Value::integer(id));
    record.add("operation", Value::integer(operation));
    return record;
}

Heard heardOf(std::string_view line) {
    Heard heard;
    const auto kRead = document::parse(line);
    const Value* kKind = kRead.has_value() ? kRead->find("kind") : nullptr;
    const Value* kId = kKind != nullptr ? kRead->find("id") : nullptr;
    if (kKind == nullptr || kKind->text() == nullptr || kId == nullptr || !kId->integer().has_value()) {
        return heard;
    }
    heard.id = *kId->integer();
    const auto kNumber = [](const Value* in, std::string_view name) {
        const Value* kAt = in != nullptr ? in->find(name) : nullptr;
        return kAt != nullptr ? kAt->integer().value_or(0) : 0;
    };
    if (*kKind->text() == "authoring.progress") {
        const Value* kSource = kRead->find("source");
        heard.kind = Heard::Kind::Progress;
        heard.step = kNumber(&*kRead, "step");
        heard.steps = kNumber(&*kRead, "steps");
        heard.text = kSource != nullptr && kSource->text() != nullptr ? *kSource->text() : "";
        return heard;
    }
    if (*kKind->text() != "authoring.reply") {
        return heard;
    }
    if (const Value* kAnswer = kRead->find("answer"); kAnswer != nullptr) {
        const Value* kAnswered = kAnswer->find("kind");
        if (kAnswered != nullptr && kAnswered->text() != nullptr && *kAnswered->text() == "authoring.cooked") {
            heard.kind = Heard::Kind::Cooked;
            heard.cooked = kNumber(kAnswer, "cooked");
            heard.reused = kNumber(kAnswer, "reused");
        }
    } else if (kRead->find("cancelled") != nullptr) {
        heard.kind = Heard::Kind::Cancelled;
    } else if (const Value* kError = kRead->find("error"); kError != nullptr) {
        heard.kind = Heard::Kind::Failed;
        heard.text = answeredOf(line).message;
        // The first source that did not cook, as the tool said it.
        const Value* kDetails = kError->find("details");
        const Value* kFirst = kDetails != nullptr ? kDetails->find("first") : nullptr;
        if (kFirst != nullptr && kFirst->text() != nullptr) {
            heard.text += ": " + *kFirst->text();
        }
    }
    return heard;
}

} // namespace rawframe::studio
