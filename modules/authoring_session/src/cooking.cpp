#include "cooking.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring/session.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string_view>
#include <thread>
#include <utility>

namespace rawframe::authoring_session {

namespace {

using document::Value;

result::Error failure(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return result::fail(kind, authoring::kAuthoringDomain, code(error), why).error();
}

/// The counts a line of the tool's begins with after `prefix`: `<prefix>X,
/// reused Y`.
bool counted(std::string_view line, std::string_view prefix, unsigned long long& cooked, unsigned long long& reused) {
    return line.starts_with(prefix) &&
           std::sscanf(std::string{line.substr(prefix.size())}.c_str(), "%llu, reused %llu", &cooked, &reused) == 2;
}

/// A progress record from `cooking <n>/<count> <source>`, or none.
std::optional<std::string> progressOf(const Value& id, std::string_view line) {
    constexpr std::string_view kCooking = "cooking ";
    unsigned long long step = 0;
    unsigned long long steps = 0;
    int consumed = 0;
    if (!line.starts_with(kCooking) ||
        std::sscanf(std::string{line.substr(kCooking.size())}.c_str(), "%llu/%llu %n", &step, &steps, &consumed) != 2 ||
        consumed == 0) {
        return std::nullopt;
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.progress"));
    made.add("id", id);
    made.add("step", Value::integer(static_cast<std::int64_t>(step)));
    made.add("steps", Value::integer(static_cast<std::int64_t>(steps)));
    made.add("source", Value::string(std::string{line.substr(kCooking.size() + static_cast<std::size_t>(consumed))}));
    return document::writeCompact(made) + "\n";
}

} // namespace

Cooking::Cooking(std::filesystem::path program) : program_(std::move(program)) {
}

Cooking::~Cooking() {
    if (child_.has_value()) {
        child_->kill();
    }
}

result::Status Cooking::start(const document::Value& id,
                              const std::filesystem::path& sources,
                              const std::filesystem::path& output,
                              const std::optional<std::filesystem::path>& cache) {
    if (program_.empty()) {
        return std::unexpected{failure(authoring::AuthoringError::UnsupportedOperation,
                                       result::ErrorClass::FailedPrecondition,
                                       "this session has no cook tool")};
    }
    if (child_.has_value()) {
        return std::unexpected{failure(authoring::AuthoringError::LimitExceeded,
                                       result::ErrorClass::ResourceExhausted,
                                       "a session cooks one game at a time")
                                   .withContext("operations", std::to_string(kMaximumSessionOperations))};
    }
    // Never into the sources, which the tool refuses too: checked before
    // anything is made there.
    std::error_code error;
    const std::filesystem::path kSources = std::filesystem::weakly_canonical(sources, error);
    const std::filesystem::path kOutput = std::filesystem::weakly_canonical(output, error);
    const auto [kEnd, kAt] = std::ranges::mismatch(kSources, kOutput);
    if (error || kEnd == kSources.end()) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "a game is cooked outside its own directory")
                                   .withContext("output", output.string())};
    }
    // What the tool says goes beside what it makes: the last cook's account
    // of itself, next to its receipt.
    std::filesystem::create_directories(output, error);
    if (error || !std::filesystem::is_directory(output, error)) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "the cook's output cannot be made")
                                   .withContext("output", output.string())};
    }
    std::vector<std::string> arguments = {sources.string(), output.string()};
    if (cache.has_value()) {
        arguments.push_back(cache->string());
    }
    log_ = output / "cook.log";
    auto child = process::Child::start({.program = program_, .arguments = std::move(arguments), .output = log_});
    if (!child.has_value()) {
        return std::unexpected{failure(authoring::AuthoringError::Internal,
                                       result::ErrorClass::Unavailable,
                                       "the cook tool cannot be started")
                                   .withContext("program", program_.string())};
    }
    child_ = std::move(*child);
    id_ = id;
    read_ = 0;
    partial_.clear();
    stopping_.clear();
    return {};
}

bool Cooking::cancel(const document::Value& id) {
    if (!child_.has_value() || document::writeCompact(id) != document::writeCompact(id_)) {
        return false;
    }
    if (stopping_.empty()) {
        stopping_ = "requested";
        static_cast<void>(child_->requestStop());
    }
    return true;
}

std::vector<std::string> Cooking::poll(bool& failed) {
    failed = false;
    std::vector<std::string> said;
    if (!child_.has_value()) {
        return said;
    }
    // Whether it has ended asked first, so what is read after is all it
    // said.
    const std::optional<int> kExit = child_->exited();
    std::string text;
    if (std::FILE* file = std::fopen(log_.string().c_str(), "rb")) {
        if (std::fseek(file, static_cast<long>(read_), SEEK_SET) == 0) {
            char buffer[4096];
            std::size_t got = 0;
            while ((got = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
                text.append(buffer, got);
            }
        }
        std::fclose(file);
    }
    read_ += text.size();
    partial_ += text;
    std::vector<std::string> lines;
    for (std::size_t at = partial_.find('\n'); at != std::string::npos; at = partial_.find('\n')) {
        lines.push_back(partial_.substr(0, at));
        partial_.erase(0, at + 1);
    }
    std::string firstFailure;
    std::size_t failures = 0;
    unsigned long long cooked = 0;
    unsigned long long reused = 0;
    bool ended = false;
    for (const std::string& kLine : lines) {
        if (std::optional<std::string> progress = progressOf(id_, kLine)) {
            said.push_back(std::move(*progress));
        } else if (constexpr std::string_view kTool = "rawframe-cook: "; kLine.starts_with(kTool)) {
            firstFailure = failures++ == 0 ? kLine.substr(kTool.size()) : firstFailure;
        } else if (counted(kLine, "cooked ", cooked, reused)) {
            ended = true;
        }
    }
    if (!kExit.has_value()) {
        return said;
    }
    // Its last words read; it has ended.
    child_.reset();
    if (!stopping_.empty() && *kExit != 0) {
        said.push_back(authoring::writeCancelled(id_, stopping_));
        return said;
    }
    if (*kExit == 0 && ended) {
        Value made = Value::object();
        made.add("kind", Value::string("authoring.cooked"));
        made.add("cooked", Value::integer(static_cast<std::int64_t>(cooked)));
        made.add("reused", Value::integer(static_cast<std::int64_t>(reused)));
        said.push_back(authoring::writeReply(id_, std::move(made)));
        return said;
    }
    failed = true;
    // 1: sources that did not cook; 2: a request the tool refused.
    const bool kRefused = *kExit == 1 || *kExit == 2;
    result::Error error =
        kRefused ? failure(authoring::AuthoringError::ValidationFailed,
                           result::ErrorClass::InvalidArgument,
                           *kExit == 1 ? "the game's sources did not cook" : "the cook tool refused the request")
                 : failure(authoring::AuthoringError::Internal,
                           result::ErrorClass::Internal,
                           "the cook tool ended without its account");
    error = std::move(error).withContext("exit", std::to_string(*kExit));
    if (failures > 0) {
        error = std::move(error).withContext("failures", std::to_string(failures)).withContext("first", firstFailure);
    }
    said.push_back(authoring::writeRefusal(id_, error));
    return said;
}

std::vector<std::string> Cooking::end() {
    if (!child_.has_value()) {
        return {};
    }
    stopping_ = "ended";
    static_cast<void>(child_->requestStop());
    // A source cooks in well under this; past it, it is killed.
    constexpr auto kWaited = std::chrono::seconds{10};
    const auto kUntil = std::chrono::steady_clock::now() + kWaited;
    while (child_->exited() == std::nullopt && std::chrono::steady_clock::now() < kUntil) {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    child_->kill();
    bool failed = false;
    return poll(failed);
}

} // namespace rawframe::authoring_session
