// The export tool (ADR-0041's standalone export; ADR-0042: export is a
// headless operation over the one container model, with receipts, D396):
// a game made into a folder that plays on this machine with nothing else.
// It runs the cook and the build tool as they run for any Build (the game
// cooked, packed into a Build signed by the publisher's key, installed into
// the folder's library, which pins the publisher's key set, and named by a
// Composition), copies the dedicated server, the client, and the launcher
// beside it, and writes their configurations: the launcher starts the
// server on this machine's loopback at the port given, and the client,
// pinned to the certificate the server makes as it starts, plays it.
//
//   rawframe-export <game directory> <output directory> [--game <file>]
//                   [--port <port>] [--version <version>] [--tools <directory>]
//                   [--key <secret key> --publisher <name>]
//
// With `--key`, the Build is signed by that secret (`rawframe-build key`
// writes it beside `<publisher>.keys`, which the folder's library pins);
// the secret is read, never copied. Without it, a key is made for this
// export under the publisher `local`, and its secret deleted once the Build
// is signed.
//
// The game is `<directory name>.game` unless named. The tools (rawframe-cook,
// rawframe-build, rawframe-server, rawframe-client, rawframe-play) are found
// beside this program unless `--tools` names their directory, or a
// `--<tool> <path>` names one (`--cook`, `--build`, `--server`, `--client`,
// `--play`). The output directory must not exist, or be empty. Running the
// folder's `rawframe-play` plays the game; `export.receipt` lists what was
// written, each file's SHA-256, the Build's root, and the Composition.

#include "rawframe/base/sha256.h"
#include "rawframe/document/json.h"
#include "rawframe/process/child.h"
#include "rawframe/process/self.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace rawframe;

std::optional<std::string> readText(const fs::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

bool writeText(const fs::path& path, std::string_view text) {
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(file);
}

std::string hexOf(const base::Sha256Digest& digest) {
    static constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    for (const std::byte kByte : digest) {
        text += kDigits[std::to_integer<unsigned>(kByte) >> 4U];
        text += kDigits[std::to_integer<unsigned>(kByte) & 15U];
    }
    return text;
}

/// Runs a tool to its end and returns what it printed, or nothing (with why
/// said) when it did not end with code 0.
std::optional<std::string> runTool(const fs::path& program, std::vector<std::string> arguments, const fs::path& log) {
    auto child = process::Child::start({.program = program, .arguments = std::move(arguments), .output = log});
    if (!child.has_value()) {
        std::fprintf(stderr, "rawframe-export: %s cannot be started\n", program.string().c_str());
        return std::nullopt;
    }
    while (!child->exited().has_value()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    auto printed = readText(log);
    if (*child->exited() != 0 || !printed.has_value()) {
        std::fprintf(stderr,
                     "rawframe-export: %s failed:\n%s",
                     program.filename().string().c_str(),
                     printed.value_or("").c_str());
        return std::nullopt;
    }
    return printed;
}

/// The word after `label` in a tool's line `<label> <word> ...`.
std::optional<std::string> wordAfter(const std::string& printed, std::string_view label) {
    const std::string kPrefix = std::string{label} + " ";
    std::size_t at = 0;
    while (at < printed.size()) {
        const std::size_t kEnd = printed.find('\n', at);
        const std::string_view kLine{printed.data() + at, (kEnd == std::string::npos ? printed.size() : kEnd) - at};
        if (kLine.starts_with(kPrefix)) {
            const std::string_view kRest = kLine.substr(kPrefix.size());
            return std::string{kRest.substr(0, kRest.find(' '))};
        }
        at = kEnd == std::string::npos ? printed.size() : kEnd + 1;
    }
    return std::nullopt;
}

constexpr std::string_view kPlatform =
#if defined(_WIN32)
    "windows";
#elif defined(__APPLE__)
    "macos";
#else
    "linux";
#endif

constexpr std::string_view kArchitecture =
#if defined(__aarch64__) || defined(_M_ARM64)
    "arm64";
#else
    "x86_64";
#endif

/// The 32 hexadecimal digits after `"resourceId": "` in a sidecar.
std::optional<std::string> resourceOf(const std::string& sidecar) {
    constexpr std::string_view kKey = "\"resourceId\": \"";
    const std::size_t kAt = sidecar.find(kKey);
    if (kAt == std::string::npos || sidecar.size() < kAt + kKey.size() + 32) {
        return std::nullopt;
    }
    std::string id = sidecar.substr(kAt + kKey.size(), 32);
    for (const char kDigit : id) {
        if ((kDigit < '0' || kDigit > '9') && (kDigit < 'a' || kDigit > 'f')) {
            return std::nullopt;
        }
    }
    return id;
}

/// A port, 1 to 65535, written plainly.
bool portLike(const std::string& text) {
    if (text.empty() || text.size() > 5 || text.front() == '0') {
        return false;
    }
    std::uint32_t value = 0;
    for (const char kDigit : text) {
        if (kDigit < '0' || kDigit > '9') {
            return false;
        }
        value = (value * 10U) + static_cast<std::uint32_t>(kDigit - '0');
    }
    return value <= 65535U;
}

int usage() {
    std::fputs("usage: rawframe-export <game directory> <output directory> [--game <file>] [--port <port>]\n"
               "                       [--version <version>] [--tools <directory>] [--<tool> <path>]...\n"
               "                       [--key <secret key> --publisher <name>]\n",
               stderr);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        return usage();
    }
    const fs::path kGame = argv[1];
    const fs::path kOutput = argv[2];
    std::error_code error;
    const fs::path kSelf = fs::canonical(process::ownExecutable(), error);
    const std::string kSuffix = kSelf.extension().string();
    std::string gameFile = kGame.filename().string() + ".game";
    std::string port = "47217";
    std::string version = "0.1.0";
    std::optional<fs::path> key;
    std::string publisher = "local";
    fs::path toolDirectory = kSelf.parent_path();
    std::map<std::string, fs::path, std::less<>> tools;
    for (int at = 3; at + 1 < argc; at += 2) {
        const std::string_view kOption = argv[at];
        const std::string kValue = argv[at + 1];
        if (kOption == "--game") {
            gameFile = kValue;
        } else if (kOption == "--port") {
            port = kValue;
        } else if (kOption == "--version") {
            version = kValue;
        } else if (kOption == "--key") {
            key = fs::absolute(kValue);
        } else if (kOption == "--publisher") {
            publisher = kValue;
        } else if (kOption == "--tools") {
            toolDirectory = kValue;
        } else if (kOption == "--cook" || kOption == "--build" || kOption == "--server" || kOption == "--client" ||
                   kOption == "--play") {
            tools.emplace(kOption.substr(2), kValue);
        } else {
            return usage();
        }
    }
    if (argc % 2 == 0 || !portLike(port)) {
        return usage();
    }
    const auto kTool = [&](const char* name) {
        const auto kNamed = tools.find(name);
        return kNamed != tools.end() ? fs::absolute(kNamed->second)
                                     : toolDirectory / (std::string{"rawframe-"} + name + kSuffix);
    };
    if (fs::exists(kOutput) && !fs::is_empty(kOutput, error)) {
        std::fprintf(stderr, "rawframe-export: %s is not empty\n", kOutput.string().c_str());
        return 1;
    }
    const std::string kName = kGame.filename().string();
    const auto kSidecar = readText(kGame / (gameFile + ".rfmeta"));
    const auto kResource = kSidecar ? resourceOf(*kSidecar) : std::nullopt;
    if (!kResource.has_value()) {
        std::fprintf(stderr, "rawframe-export: %s has no sidecar naming its resource\n", gameFile.c_str());
        return 1;
    }
    const std::string& kGameResource = *kResource;

    // The working files under the output, gone at the end.
    const fs::path kWork = kOutput / ".export";
    const fs::path kLibrary = kOutput / "library";
    fs::create_directories(kWork, error);
    fs::create_directories(kLibrary / "keys", error);
    if (error) {
        std::fprintf(stderr, "rawframe-export: %s cannot be written\n", kOutput.string().c_str());
        return 1;
    }
    if (!runTool(kTool("cook"),
                 {kGame.string(), (kWork / "cooked").string(), (kWork / "cache").string()},
                 kWork / "cook.log")) {
        return 1;
    }
    // The publisher's key set pinned by the library: the one beside the
    // secret given, or one made for this export.
    fs::path secret;
    if (key.has_value()) {
        fs::copy_file(key->parent_path() / (publisher + ".keys"), kLibrary / "keys" / (publisher + ".keys"), error);
        if (error) {
            std::fprintf(stderr, "rawframe-export: no %s.keys beside the key\n", publisher.c_str());
            return 1;
        }
        secret = *key;
    } else {
        const auto kKeyMade =
            runTool(kTool("build"), {"key", publisher, (kLibrary / "keys").string()}, kWork / "key.log");
        const auto kKid = kKeyMade ? wordAfter(*kKeyMade, "key") : std::nullopt;
        if (!kKid.has_value()) {
            return 1;
        }
        secret = kLibrary / "keys" / (*kKid + ".key");
    }
    const auto kBuilt = runTool(kTool("build"),
                                {(kWork / "cooked").string(),
                                 (kWork / "build").string(),
                                 publisher + "/" + kName,
                                 version,
                                 std::string{kPlatform},
                                 std::string{kArchitecture},
                                 "client",
                                 std::string{"build."} + RAWFRAME_CONFIGURATION_NAME,
                                 "tool",
                                 secret.string()},
                                kWork / "build.log");
    // A secret made here goes once it has signed; one given stays where it is.
    if (!key.has_value()) {
        fs::remove(secret, error);
    }
    if (!kBuilt.has_value()) {
        return 1;
    }
    const auto kInstalled =
        runTool(kTool("build"), {"install", (kWork / "build").string(), kLibrary.string()}, kWork / "install.log");
    const auto kRoot = kInstalled ? wordAfter(*kInstalled, "installed") : std::nullopt;
    if (!kRoot.has_value()) {
        return 1;
    }
    const std::string kRecord = kName + ".composition";
    const auto kComposed = runTool(kTool("build"),
                                   {"compose", kLibrary.string(), *kRoot, "tool", (kOutput / kRecord).string()},
                                   kWork / "compose.log");
    const auto kComposition = kComposed ? wordAfter(*kComposed, "composition") : std::nullopt;
    if (!kComposition.has_value()) {
        return 1;
    }

    // The programs, and what each reads: every path under the folder.
    std::vector<std::string> written;
    for (const char* name : {"server", "client", "play"}) {
        const std::string kFile = std::string{"rawframe-"} + name + kSuffix;
        fs::copy_file(kTool(name), kOutput / kFile, fs::copy_options::overwrite_existing, error);
        if (error) {
            std::fprintf(stderr, "rawframe-export: %s cannot be copied\n", kTool(name).string().c_str());
            return 1;
        }
        written.push_back(kFile);
    }
    const std::string kContent = "kest.game_resource = " + kGameResource + "\ncontent.composition = " + kRecord +
                                 "\ncontent.library = library\n";
    const std::string kServer = "# The game's dedicated server, on this machine's loopback, with an identity\n"
                                "# made as it starts.\n"
                                "host.iteration_rate = 120\nworld.tick_rate = 60\n" +
                                kContent +
                                "network.quic.self_signed = true\nnetwork.quic.fingerprint_file = fingerprint\n"
                                "replication.endpoint = 127.0.0.1:" +
                                port + "\n";
    const std::string kClient = "# The player, from this window, pinned to the server's identity.\n"
                                "host.iteration_rate = 120\nbots.player = true\n" +
                                kContent +
                                "kest.plan_only = true\nnetwork.quic.pin_file = fingerprint\n"
                                "bots.endpoint = 127.0.0.1:" +
                                port + "\n";
    const std::string kPlay = "# Run rawframe-play" + kSuffix +
                              " to play: it starts the server, then the client.\n"
                              "play.server = rawframe-server" +
                              kSuffix + "\nplay.server_config = server.conf\nplay.server_log = server.log\n" +
                              "play.client = rawframe-client" + kSuffix +
                              "\nplay.client_config = client.conf\nplay.client_log = client.log\n"
                              "play.fingerprint = fingerprint\n";
    for (const auto& [file, text] : std::array<std::pair<const char*, const std::string*>, 3>{
             {{"server.conf", &kServer}, {"client.conf", &kClient}, {"play.conf", &kPlay}}}) {
        if (!writeText(kOutput / file, *text)) {
            std::fprintf(stderr, "rawframe-export: %s cannot be written\n", file);
            return 1;
        }
        written.emplace_back(file);
    }
    written.push_back(kRecord);
    fs::remove_all(kWork, error);

    // The receipt: what was written, by digest, and the Build it plays.
    document::Value files = document::Value::array();
    for (const std::string& file : written) {
        const auto kBytes = readText(kOutput / file);
        document::Value each = document::Value::object();
        each.add("path", document::Value::string(file));
        each.add("sha256", document::Value::string(hexOf(base::sha256(kBytes.value_or("")))));
        files.push(std::move(each));
    }
    document::Value receipt = document::Value::object();
    receipt.add("build", document::Value::string(*kRoot));
    receipt.add("composition", document::Value::string(*kComposition));
    receipt.add("files", std::move(files));
    receipt.add("formatVersion", document::Value::integer(1));
    receipt.add("game", document::Value::string(kGameResource));
    receipt.add("kind", document::Value::string("export.receipt"));
    receipt.add("port", document::Value::integer(std::stoll(port)));
    const auto kReceipt = document::writeCanonicalRecord(receipt);
    if (!kReceipt.has_value() || !writeText(kOutput / "export.receipt", *kReceipt)) {
        std::fputs("rawframe-export: the receipt cannot be written\n", stderr);
        return 1;
    }
    std::printf("exported %s, build %s, composition %s\n", kName.c_str(), kRoot->c_str(), kComposition->c_str());
    return 0;
}
