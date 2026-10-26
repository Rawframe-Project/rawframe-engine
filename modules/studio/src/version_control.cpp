#include "version_control.h"

#include "records.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace rawframe::studio {

void VersionControl::ask(std::string purpose, std::vector<std::string> arguments) {
    if (!trusted_) {
        return;
    }
    waiting_.emplace_back(std::move(purpose), std::move(arguments));
}

std::optional<VersionControl::Refusal> VersionControl::start(std::string_view path,
                                                             const std::filesystem::path& directory,
                                                             const std::filesystem::path& output) {
    if (child_.has_value() || waiting_.empty()) {
        return std::nullopt;
    }
    // Asked before trust and trusted since, nothing waits; checked again
    // all the same, as this is the one place git starts.
    if (!trusted_) {
        waiting_.clear();
        return std::nullopt;
    }
    const auto kProgram = programOnPath("git", path);
    if (!kProgram.has_value()) {
        waiting_.clear();
        return Refusal{.kind = Refusal::Kind::NoGit, .why = "no git is on the path"};
    }
    if (isBatch(*kProgram)) {
        waiting_.clear();
        return Refusal{.kind = Refusal::Kind::Batch,
                       .why = "git on the path is a batch file; put git's own program on the path"};
    }
    auto [purpose, arguments] = std::move(waiting_.front());
    waiting_.pop_front();
    auto started =
        process::Child::start({.program = *kProgram, .arguments = words(directory, arguments), .output = output});
    if (!started.has_value()) {
        waiting_.clear();
        return Refusal{.kind = Refusal::Kind::Failed,
                       .why = "git did not start: " + std::string{started.error().description()}};
    }
    child_ = std::move(*started);
    runningFor_ = std::move(purpose);
    output_ = output;
    ++started_;
    return std::nullopt;
}

std::optional<VersionControl::Ended> VersionControl::poll() {
    if (!child_.has_value()) {
        return std::nullopt;
    }
    const std::optional<int> kCode = child_->exited();
    if (!kCode.has_value()) {
        return std::nullopt;
    }
    child_.reset();
    Ended ended{.purpose = std::exchange(runningFor_, {}), .code = *kCode};
    {
        std::ifstream read{output_, std::ios::binary};
        ended.output.assign(std::istreambuf_iterator<char>{read}, std::istreambuf_iterator<char>{});
    }
    std::error_code error;
    std::filesystem::remove(output_, error);
    return ended;
}

void VersionControl::stop() noexcept {
    child_.reset();
    waiting_.clear();
    runningFor_.clear();
}

std::vector<std::string> VersionControl::words(const std::filesystem::path& directory,
                                               const std::vector<std::string>& arguments) {
    std::vector<std::string> made = {"-c", "core.fsmonitor=false", "-c", "diff.external=", "-C", directory.string()};
    made.insert(made.end(), arguments.begin(), arguments.end());
    if (!arguments.empty() && arguments.front() == "diff") {
        // Right after the command, before any path the author chose.
        made.insert(made.begin() + 7, {"--no-ext-diff", "--no-textconv"});
    }
    return made;
}

} // namespace rawframe::studio
