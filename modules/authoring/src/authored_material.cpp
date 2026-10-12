#include "rawframe/authoring/authored_material.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/material/instance.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <variant>

namespace rawframe::authoring {

namespace {

using document::Value;

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, AuthoringError error, std::string_view why) {
    return result::fail(errorClass, kAuthoringDomain, code(error), why);
}

constexpr std::array<std::string_view, 4> kDocumentFields = {"formatVersion", "kind", "base", "overrides"};

/// The instance's objects' members in order: the root's, and its overrides
/// by name (D721).
std::span<const std::string_view> orderOf(std::string_view holder) noexcept {
    if (holder.empty()) {
        return kDocumentFields;
    }
    return {};
}

result::Status reads(std::string_view text) {
    auto read = material::readInstance(text);
    if (!read.has_value()) {
        return std::unexpected<result::Error>{std::move(read).error().mappedTo(
            result::ErrorClass::InvalidArgument,
            kAuthoringDomain,
            code(AuthoringError::ValidationFailed),
            "the material instance does not read: " + std::string{read.error().description()})};
    }
    return {};
}

/// A Kest name's shape, as an interface input's and a switch's are.
bool named(std::string_view name) {
    return !name.empty() && name.size() <= 64 && name[0] >= 'a' && name[0] <= 'z' &&
           std::ranges::all_of(name, [](char letter) {
               return (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') || letter == '_';
           });
}

} // namespace

result::Result<std::unique_ptr<AuthoredMaterial>>
AuthoredMaterial::open(base::Bits128 document, std::string_view text, const HistoryLimits& limits) {
    RAWFRAME_TRY(reads(text));
    RAWFRAME_TRY_ASSIGN(JsonDocument opened, JsonDocument::open(text, orderOf, reads, limits));
    return std::unique_ptr<AuthoredMaterial>{new AuthoredMaterial(document, std::move(opened))};
}

std::string AuthoredMaterial::base() const {
    const Value* kBase = document_.root().find("base");
    return kBase != nullptr && kBase->text() != nullptr ? *kBase->text() : std::string{};
}

result::Result<Committed> AuthoredMaterial::change(std::uint64_t generation, std::span<const Operation> operations) {
    RAWFRAME_TRY_ASSIGN(JsonDocument::Staged staged, document_.stage(generation));
    if (operations.size() > kMaximumBatchOperations) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      AuthoringError::LimitExceeded,
                      "a batch has more operations than its limit");
    }
    for (std::size_t at = 0; at < operations.size(); ++at) {
        const auto* kSet = std::get_if<SetMaterialOverride>(&operations[at]);
        if (kSet == nullptr || !named(kSet->input)) {
            return std::unexpected<result::Error>{
                refuse(result::ErrorClass::InvalidArgument,
                       AuthoringError::ValidationFailed,
                       kSet == nullptr ? "the operation does not change a material instance"
                                       : "an override names an input or a switch as a Kest name is")
                    .error()
                    .withContext("index", std::to_string(at))};
        }
        JsonPath path = {JsonStep{.member = "overrides", .key = {}, .id = {}},
                         JsonStep{.member = kSet->input, .key = {}, .id = {}}};
        std::optional<Value> before = slotAt(staged.root, path);
        auto applied = document_.apply(
            staged, JsonDelta{.path = std::move(path), .place = 0, .before = std::move(before), .after = kSet->value});
        if (!applied.has_value()) {
            return std::unexpected<result::Error>{std::move(applied).error().withContext("index", std::to_string(at))};
        }
    }
    return document_.keep(std::move(staged));
}

std::string AuthoredMaterial::summaryAt(std::size_t entry) const {
    const std::vector<JsonDelta>& journal = document_.journalAt(entry);
    if (journal.empty()) {
        return "no change";
    }
    const JsonDelta& kFirst = journal.front();
    std::string made = (kFirst.after.has_value() ? "set " : "unset ") + kFirst.path.back().member;
    if (journal.size() > 1) {
        made += " and " + std::to_string(journal.size() - 1) + " more";
    }
    return made;
}

result::Result<Value> answer(const AuthoredMaterial& material, const Query& query) {
    if (!std::holds_alternative<ReadMaterialInstance>(query)) {
        return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                     AuthoringError::ValidationFailed,
                                                     "the query does not read a material instance")
                                                  .error()
                                                  .withContext("operation", declarationOf(query).name)};
    }
    Value made = Value::object();
    made.add("instance", material.documentValue());
    return made;
}

} // namespace rawframe::authoring
