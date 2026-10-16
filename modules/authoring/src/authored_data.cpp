#include "rawframe/authoring/authored_data.h"

#include "rawframe/authoring/errors.h"

#include <array>
#include <charconv>
#include <utility>

namespace rawframe::authoring {

namespace {

using document::Value;

constexpr std::size_t kMostSteps = 32;

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, AuthoringError error, std::string_view why) {
    return result::fail(errorClass, kAuthoringDomain, code(error), why);
}

/// Every object of a data document keeps its members as they stand.
std::span<const std::string_view> asHeld(std::span<const JsonStep> /*at*/) noexcept {
    static constexpr std::array<std::string_view, 1> kHeld = {kMembersAsHeld};
    return kHeld;
}

bool named(std::string_view text) noexcept {
    if (text.empty() || text.size() > 128) {
        return false;
    }
    for (const char kCharacter : text) {
        const bool kWord = (kCharacter >= 'a' && kCharacter <= 'z') || (kCharacter >= 'A' && kCharacter <= 'Z') ||
                           (kCharacter >= '0' && kCharacter <= '9') || kCharacter == '_' || kCharacter == '-';
        if (!kWord) {
            return false;
        }
    }
    return true;
}

std::string pathText(const JsonPath& path) {
    std::string made;
    for (const JsonStep& kStep : path) {
        made += (made.empty() ? "" : ".") + kStep.member;
        if (!kStep.key.empty()) {
            made += "[" + kStep.id + "]";
        } else if (kStep.place.has_value()) {
            made += "[" + std::to_string(*kStep.place) + "]";
        }
    }
    return made;
}

} // namespace

result::Result<JsonPath> dataPathOf(std::string_view text) {
    const auto kBad = [] {
        return refuse(result::ErrorClass::InvalidArgument,
                      AuthoringError::ValidationFailed,
                      "a path is members apart by dots, an item as member[key=id] or member[place]");
    };
    JsonPath path;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t kEnd = std::min(text.find('.', at), text.size());
        const std::string_view kPart = text.substr(at, kEnd - at);
        JsonStep step;
        if (const std::size_t kOpen = kPart.find('['); kOpen != std::string_view::npos) {
            if (!kPart.ends_with(']')) {
                return kBad();
            }
            step.member = kPart.substr(0, kOpen);
            const std::string_view kInside = kPart.substr(kOpen + 1, kPart.size() - kOpen - 2);
            const std::size_t kEquals = kInside.find('=');
            if (kEquals != std::string_view::npos) {
                step.key = kInside.substr(0, kEquals);
                step.id = kInside.substr(kEquals + 1);
                if (!named(step.key) || !named(step.id)) {
                    return kBad();
                }
            } else {
                // An item by its place from nought, as `keys[2]` (D827).
                std::size_t place = 0;
                const auto [kStop, kError] = std::from_chars(kInside.data(), kInside.data() + kInside.size(), place);
                if (kInside.empty() || kInside.size() > 9 || kError != std::errc{} ||
                    kStop != kInside.data() + kInside.size()) {
                    return kBad();
                }
                step.place = place;
            }
        } else {
            step.member = kPart;
        }
        if (!named(step.member) || path.size() == kMostSteps) {
            return kBad();
        }
        path.push_back(std::move(step));
        at = kEnd + 1;
    }
    return path;
}

result::Result<std::unique_ptr<AuthoredData>>
AuthoredData::open(base::Bits128 document, std::string_view text, DocumentReader reads, const HistoryLimits& limits) {
    RAWFRAME_TRY_ASSIGN(JsonDocument opened, JsonDocument::open(text, asHeld, reads, limits));
    return std::unique_ptr<AuthoredData>{new AuthoredData(document, std::move(opened), reads)};
}

result::Result<Committed> AuthoredData::change(std::uint64_t generation, std::span<const Operation> operations) {
    RAWFRAME_TRY_ASSIGN(JsonDocument::Staged staged, document_.stage(generation));
    if (operations.size() > kMaximumBatchOperations) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      AuthoringError::LimitExceeded,
                      "a batch has more operations than its limit");
    }
    for (std::size_t at = 0; at < operations.size(); ++at) {
        const auto kIndexed = [at](result::Error error) {
            return std::unexpected<result::Error>{std::move(error).withContext("index", std::to_string(at))};
        };
        const auto* set = std::get_if<SetDataValue>(&operations[at]);
        if (set == nullptr) {
            return kIndexed(refuse(result::ErrorClass::InvalidArgument,
                                   AuthoringError::ValidationFailed,
                                   "a data document's only operation is data.set")
                                .error());
        }
        auto path = dataPathOf(set->path);
        if (!path.has_value()) {
            return kIndexed(std::move(path).error());
        }
        // An item the array does not hold yet comes last.
        std::optional<std::size_t> place = placeAt(staged.root, *path);
        if (!place.has_value() && !path->back().key.empty()) {
            const JsonPath kHolder{path->begin(), path->end() - 1};
            const auto kHolding = slotAt(staged.root, kHolder);
            const Value* kArray = kHolding.has_value() ? kHolding->find(path->back().member) : nullptr;
            place = kArray != nullptr ? kArray->items().size() : 0;
        }
        auto applied = document_.apply(
            staged,
            JsonDelta{
                .path = *path, .place = place.value_or(0), .before = slotAt(staged.root, *path), .after = set->value});
        if (!applied.has_value()) {
            return kIndexed(std::move(applied).error());
        }
    }
    return document_.keep(std::move(staged));
}

std::string AuthoredData::summaryAt(std::size_t entry) const {
    const std::vector<JsonDelta>& journal = document_.journalAt(entry);
    if (journal.empty()) {
        return "no change";
    }
    const JsonDelta& kFirst = journal.front();
    std::string made = (kFirst.after.has_value() ? "set " : "remove ") + pathText(kFirst.path);
    if (journal.size() > 1) {
        made += " and " + std::to_string(journal.size() - 1) + " more";
    }
    return made;
}

result::Result<Value> answer(const AuthoredData& data, const Query& query) {
    if (!std::holds_alternative<ReadData>(query)) {
        return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                     AuthoringError::ValidationFailed,
                                                     "the query does not read a data document")
                                                  .error()
                                                  .withContext("operation", declarationOf(query).name)};
    }
    Value made = Value::object();
    made.add("data", data.documentValue());
    return made;
}

} // namespace rawframe::authoring
