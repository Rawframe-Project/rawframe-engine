#include "../src/records.h"
#include "../src/version_control.h"
#include "rawframe/process/child.h"
#include "rawframe/test/test.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

using rawframe::process::Child;
using rawframe::studio::mintedIdentity;
using rawframe::studio::programOnPath;
using rawframe::studio::VersionControl;

namespace {

// Git is run as a program only on the POSIX desktops: Windows' git runs
// hooks in a shell of its own, which the scripts below are not written for,
// and a phone or its simulator has no git to start.
#if !defined(_WIN32) && !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
std::string pathVariable() {
    const char* const kPath = std::getenv("PATH");
    return kPath != nullptr ? kPath : "";
}

/// Git run to its end in `directory`, for the test's own setup.
bool git(const std::filesystem::path& directory, std::vector<std::string> arguments) {
    const auto kProgram = programOnPath("git", pathVariable());
    if (!kProgram.has_value()) {
        return false;
    }
    arguments.insert(arguments.begin(), {"-C", directory.string()});
    auto child = Child::start({.program = *kProgram, .arguments = arguments});
    for (int tick = 0; child.has_value() && tick < 1000; ++tick) {
        if (const auto kCode = child->exited()) {
            return *kCode == 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

/// Each command `control` holds started and run to its end, within ten
/// seconds; how many ended with code 0.
int runAll(VersionControl& control, const std::filesystem::path& directory, const std::filesystem::path& output) {
    int succeeded = 0;
    for (int tick = 0; tick < 1000 && (control.running() || control.waiting()); ++tick) {
        static_cast<void>(control.start(pathVariable(), directory, output));
        if (const auto kEnded = control.poll()) {
            succeeded += kEnded->code == 0 ? 1 : 0;
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return succeeded;
}

/// A script that leaves `marker` and does what git asked of it as little
/// as it can.
std::filesystem::path
markerScript(const std::filesystem::path& directory, const std::string& name, const std::filesystem::path& marker) {
    const std::filesystem::path kScript = directory / name;
    std::ofstream{kScript} << "#!/bin/sh\ntouch '" << marker.string() << "'\n";
    std::filesystem::permissions(kScript, std::filesystem::perms::owner_all);
    return kScript;
}
#endif

} // namespace

RAWFRAME_TEST(EveryGitCommandKeepsTheRepositorysProgramsOff) {
    // Each command Studio asks carries the pins first; a diff also leaves
    // external diffs and text conversions off, ahead of any path.
    const std::vector<std::vector<std::string>> kAsked = {{"branch", "--show-current"},
                                                          {"rev-parse", "--show-prefix"},
                                                          {"status", "--porcelain=v1", "--", "."},
                                                          {"diff", "HEAD", "--", "level.scene"},
                                                          {"add", "--all", "--", "."},
                                                          {"commit", "--quiet", "-m", "m", "--", "."}};
    for (const std::vector<std::string>& kArguments : kAsked) {
        const std::vector<std::string> kWords = VersionControl::words("/game", kArguments);
        RAWFRAME_EXPECT(
            (std::vector<std::string>{kWords.begin(), kWords.begin() + 6} ==
             std::vector<std::string>{"-c", "core.fsmonitor=false", "-c", "diff.external=", "-C", "/game"}));
        RAWFRAME_EXPECT(kWords[6] == kArguments.front());
    }
    const std::vector<std::string> kDiff = VersionControl::words("/game", {"diff", "HEAD", "--", "a"});
    RAWFRAME_EXPECT((std::vector<std::string>{kDiff.begin() + 6, kDiff.end()} ==
                     std::vector<std::string>{"diff", "--no-ext-diff", "--no-textconv", "HEAD", "--", "a"}));
}

#if !defined(_WIN32) && !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
RAWFRAME_TEST(GitRunsOnlyInATrustedRepositoryAndNeverItsReadersPrograms) {
    // A repository whose settings name a program for every way git may run
    // one: a commit hook, a file monitor, an external diff, a diff driver's
    // command, and a text conversion, each leaving a marker. Shell scripts
    // as git for Windows would run them are left to the POSIX systems.
    RAWFRAME_EXPECT(programOnPath("git", pathVariable()).has_value());
    std::error_code error;
    const std::filesystem::path kRoot =
        std::filesystem::temp_directory_path() / ("studio-version-control-" + mintedIdentity());
    const std::filesystem::path kGame = kRoot / "game";
    const std::filesystem::path kMarks = kRoot / "marks";
    const std::filesystem::path kOutput = kRoot / "git.out";
    std::filesystem::create_directories(kGame);
    std::filesystem::create_directories(kMarks);
    RAWFRAME_EXPECT(git(kGame, {"init", "--quiet"}));
    RAWFRAME_EXPECT(git(kGame, {"config", "user.name", "Author"}));
    RAWFRAME_EXPECT(git(kGame, {"config", "user.email", "author@example.com"}));
    RAWFRAME_EXPECT(
        git(kGame, {"config", "core.fsmonitor", markerScript(kRoot, "monitor", kMarks / "monitor").string()}));
    RAWFRAME_EXPECT(
        git(kGame, {"config", "diff.external", markerScript(kRoot, "external", kMarks / "external").string()}));
    RAWFRAME_EXPECT(
        git(kGame, {"config", "diff.mark.command", markerScript(kRoot, "driver", kMarks / "driver").string()}));
    RAWFRAME_EXPECT(
        git(kGame, {"config", "diff.conv.textconv", markerScript(kRoot, "textconv", kMarks / "textconv").string()}));
    const std::filesystem::path kHook = markerScript(kRoot, "pre-commit", kMarks / "hook");
    std::filesystem::copy_file(kHook, kGame / ".git" / "hooks" / "pre-commit", error);
    std::ofstream{kGame / ".gitattributes"} << "*.scene diff=mark\n*.ui diff=conv\n";
    std::ofstream{kGame / "level.scene"} << "first\n";
    std::ofstream{kGame / "hud.ui"} << "first\n";
    RAWFRAME_EXPECT(git(kGame, {"-c", "core.fsmonitor=false", "add", "--all"}));
    RAWFRAME_EXPECT(git(kGame, {"-c", "core.fsmonitor=false", "commit", "--quiet", "--no-verify", "-m", "start"}));
    std::ofstream{kGame / "level.scene"} << "second\n";
    std::ofstream{kGame / "hud.ui"} << "second\n";
    const auto kMarked = [&kMarks] {
        return !std::filesystem::is_empty(kMarks);
    };
    RAWFRAME_EXPECT(!kMarked());

    // Not trusted: nothing asked is kept, and no git starts, a commit's hook
    // least of all.
    VersionControl control;
    control.ask("status", {"status", "--porcelain=v1", "--", "."});
    control.ask("add", {"add", "--all", "--", "."});
    control.ask("commit", {"commit", "--quiet", "-m", "untrusted", "--", "."});
    RAWFRAME_EXPECT(!control.waiting());
    RAWFRAME_EXPECT(!control.start(pathVariable(), kGame, kOutput).has_value());
    RAWFRAME_EXPECT(control.started() == 0 && !control.running());
    RAWFRAME_EXPECT(!kMarked());

    // Trusted: status and each file's diff run, and none of the readers'
    // programs does.
    control.trust();
    control.ask("status", {"status", "--porcelain=v1", "--", "."});
    control.ask("diff", {"diff", "HEAD", "--", "level.scene"});
    control.ask("diff", {"diff", "HEAD", "--", "hud.ui"});
    RAWFRAME_EXPECT(runAll(control, kGame, kOutput) == 3);
    RAWFRAME_EXPECT(control.started() == 3);
    RAWFRAME_EXPECT(!std::filesystem::exists(kMarks / "monitor"));
    RAWFRAME_EXPECT(!std::filesystem::exists(kMarks / "external"));
    RAWFRAME_EXPECT(!std::filesystem::exists(kMarks / "driver"));
    RAWFRAME_EXPECT(!std::filesystem::exists(kMarks / "textconv"));

    // A commit runs the repository's hook: that is what trusting it means.
    control.ask("add", {"add", "--all", "--", "."});
    control.ask("commit", {"commit", "--quiet", "-m", "trusted", "--", "."});
    RAWFRAME_EXPECT(runAll(control, kGame, kOutput) == 2);
    RAWFRAME_EXPECT(std::filesystem::exists(kMarks / "hook"));
    RAWFRAME_EXPECT(!std::filesystem::exists(kMarks / "monitor"));
    std::filesystem::remove_all(kRoot, error);
}
#endif
