// Child processes (D395): a child's exit code, its output and error in one
// file, its arguments passed as they are (spaces and quotes too), a stop
// request it answers as a Host does, a kill, a kill when its owner goes,
// and a program that cannot be started.

#include "rawframe/process/child.h"
#include "rawframe/process/errors.h"
#include "rawframe/test/files.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

using namespace rawframe;
using process::Child;
using process::ChildSettings;

namespace {

/// Its exit code within five seconds, or nothing.
std::optional<int> waitFor(Child& child) {
    for (int tick = 0; tick < 500; ++tick) {
        if (const auto kCode = child.exited()) {
            return kCode;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return std::nullopt;
}

/// Whether `path` holds `text` within five seconds.
bool waitForText(const std::filesystem::path& path, const std::string& text) {
    for (int tick = 0; tick < 500; ++tick) {
        if (std::filesystem::exists(path) && test::readFile(path.string()) == text) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

std::filesystem::path scratch(const char* name) {
    std::filesystem::path directory = test::scratchDirectory("process");
    std::filesystem::create_directories(directory);
    return directory / name;
}

} // namespace

RAWFRAME_TEST(AChildEndsWithItsCodeAndSaysWhatItWasGiven) {
    auto ended = Child::start({.program = RAWFRAME_PROCESS_CHILD, .arguments = {"exit", "7"}});
    RAWFRAME_EXPECT(ended.has_value() && ended->id() != 0);
    if (ended.has_value()) {
        RAWFRAME_EXPECT(waitFor(*ended) == 7);
        // Once known, the answer stays.
        RAWFRAME_EXPECT(ended->exited() == 7);
    }
    // Its arguments as they were given, its output and error in one file.
    const std::filesystem::path kSaid = scratch("said.txt");
    auto said = Child::start({.program = RAWFRAME_PROCESS_CHILD,
                              .arguments = {"say", "two words", "a \"quote\"", "back\\slash\\", ""},
                              .output = kSaid});
    RAWFRAME_EXPECT(said.has_value());
    if (said.has_value()) {
        RAWFRAME_EXPECT(waitFor(*said) == 0);
        RAWFRAME_EXPECT(test::readFile(kSaid.string()) == "two words|a \"quote\"|back\\slash\\||said\n");
    }
}

RAWFRAME_TEST(AChildStopsWhenAskedAndIsKilledOtherwise) {
    const std::filesystem::path kHeard = scratch("heard.txt");
    auto waiting = Child::start({.program = RAWFRAME_PROCESS_CHILD, .arguments = {"wait"}, .output = kHeard});
    RAWFRAME_EXPECT(waiting.has_value());
    if (waiting.has_value()) {
        // Asked once it listens, it stops as a Host stops; asked again, it
        // has ended.
        RAWFRAME_EXPECT(waitForText(kHeard, "waiting\n"));
        RAWFRAME_EXPECT(!waiting->exited().has_value());
        RAWFRAME_EXPECT(waiting->requestStop().has_value());
        RAWFRAME_EXPECT(waitFor(*waiting) == 3);
        const auto kAgain = waiting->requestStop();
        RAWFRAME_EXPECT(!kAgain.has_value() &&
                        kAgain.error().code() == process::code(process::ProcessError::StopFailed));
    }
    // A kill ends it at once, without its own say.
    auto killed = Child::start({.program = RAWFRAME_PROCESS_CHILD, .arguments = {"wait"}});
    RAWFRAME_EXPECT(killed.has_value());
    if (killed.has_value()) {
        killed->kill();
        RAWFRAME_EXPECT(killed->exited().has_value() && killed->exited() != 3 && killed->exited() != 4);
    }
    // One still running when its owner goes is killed with it: the owner
    // does not wait out its ten seconds.
    const auto kBefore = std::chrono::steady_clock::now();
    {
        auto left = Child::start({.program = RAWFRAME_PROCESS_CHILD, .arguments = {"wait"}});
        RAWFRAME_EXPECT(left.has_value());
        // Moved, it is still the one child.
        std::optional<Child> moved;
        if (left.has_value()) {
            moved.emplace(std::move(*left));
        }
    }
    RAWFRAME_EXPECT(std::chrono::steady_clock::now() - kBefore < std::chrono::seconds{5});
}

RAWFRAME_TEST(AProgramThatCannotRunIsRefusedOrEndsAsAShellWouldSay) {
    auto missing = Child::start({.program = scratch("no-such-program"), .arguments = {}});
    if (missing.has_value()) {
        RAWFRAME_EXPECT(waitFor(*missing) == 127);
    } else {
        RAWFRAME_EXPECT(missing.error().code() == process::code(process::ProcessError::StartFailed));
    }
}
