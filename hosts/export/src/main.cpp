// The export tool (ADR-0041's standalone export; ADR-0042: export is a
// headless operation over the one container model, with receipts, D396):
// a game made into a folder that plays on this machine with nothing else.
// It runs the cook and the build tool as they run for any Build (the game
// cooked, packed into a Build signed by the publisher's key, installed into
// the folder's library, which pins the publisher's key set, and named by a
// Composition), copies the dedicated server, the client, and the launcher
// beside it, and writes their configurations: the launcher starts the
// server on this machine's loopback at the port given (by default one of
// 20000 to 29999 taken from the game's resource identity, so two exported
// games can run at once, D402), and the client,
// pinned to the certificate the server makes as it starts, plays it.
//
//   rawframe-export <game directory> <output directory> [--game <file>]
//                   [--port <port>] [--version <version>] [--tools <directory>]
//                   [--key <secret key> --publisher <name>] [--target web]
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
//
// With `--target web` (D397) the game is packed for the web and the folder
// holds a site and its server instead. `web/` is served as it is by any
// static file server: its page (`index.html`, with the page's modules under
// `page/`), the web client (`client.wasm`), Maul Window's and Maul RHI's
// page sides, the Composition, the library, and `play.json`, which names
// them, the configuration the client plays, and the server's port.
// `server/` holds the dedicated server and its configuration: it listens on
// every address at the port, for browsers over WebTransport, with an
// identity made as it starts, whose fingerprint it writes into `web/` for
// the page to trust; the page reaches it on the host the page came from.
// The web client and the page sides are found under `<tools>/web/`
// (`rawframe-web-client.wasm`, `maul-window.mjs`, `maul-rhi.mjs`, and
// `page/`, the page's own files) unless named (`--web-client`,
// `--maul-window`, `--maul-rhi`, `--page`).

#include "rawframe/base/sha256.h"
#include "rawframe/document/json.h"
#include "rawframe/process/child.h"
#include "rawframe/process/self.h"

#include <algorithm>
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

/// Every file under `directory`, by its path from there with `/` between
/// names, in order.
std::vector<std::string> filesUnder(const fs::path& directory) {
    std::vector<std::string> files;
    std::error_code error;
    for (fs::recursive_directory_iterator walk{directory, error}, end; !error && walk != end; walk.increment(error)) {
        if (walk->is_regular_file(error)) {
            files.push_back(fs::relative(walk->path(), directory, error).generic_string());
        }
    }
    std::ranges::sort(files);
    return files;
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

/// The port a game's export uses unless one is given: 20000 to 29999, from
/// FNV-1a over the game's resource identity, so two exported games running
/// at once do not reach for the same one (D402). The window is under the
/// ports systems hand out for outgoing connections (from 32768 on Linux,
/// 49152 on Windows and macOS).
std::string portOf(const std::string& gameResource) {
    std::uint32_t hash = 2166136261U;
    for (const char kByte : gameResource) {
        hash = (hash ^ static_cast<std::uint8_t>(kByte)) * 16777619U;
    }
    return std::to_string(20000U + (hash % 10000U));
}

/// What every export writes its configurations from.
struct Exported {
    fs::path output;
    /// The Composition's record, beside the library it names.
    std::string record;
    std::string gameResource;
    std::string port;
    /// The programs' suffix on this system (`.exe` on Windows).
    std::string suffix;
};

/// Copies `from` to `file` under the output and adds it to `written`.
bool copyInto(const fs::path& from,
              const Exported& exported,
              const std::string& file,
              std::vector<std::string>& written) {
    std::error_code error;
    fs::create_directories((exported.output / file).parent_path(), error);
    fs::copy_file(from, exported.output / file, fs::copy_options::overwrite_existing, error);
    if (error) {
        std::fprintf(stderr, "rawframe-export: %s cannot be copied\n", from.string().c_str());
        return false;
    }
    written.push_back(file);
    return true;
}

/// Writes `text` to `file` under the output and adds it to `written`.
bool writeInto(const Exported& exported,
               const std::string& file,
               std::string_view text,
               std::vector<std::string>& written) {
    if (!writeText(exported.output / file, text)) {
        std::fprintf(stderr, "rawframe-export: %s cannot be written\n", file.c_str());
        return false;
    }
    written.push_back(file);
    return true;
}

/// The folder that plays on this machine: the server, the client, the
/// launcher, and their configurations.
bool writeNative(const Exported& exported, const std::array<fs::path, 3>& programs, std::vector<std::string>& written) {
    const std::string& kSuffix = exported.suffix;
    const std::array<std::string, 3> kFiles{
        "rawframe-server" + kSuffix, "rawframe-client" + kSuffix, "rawframe-play" + kSuffix};
    for (std::size_t at = 0; at < programs.size(); ++at) {
        if (!copyInto(programs[at], exported, kFiles[at], written)) {
            return false;
        }
    }
    const std::string kContent = "kest.game_resource = " + exported.gameResource +
                                 "\ncontent.composition = " + exported.record + "\ncontent.library = library\n";
    const std::string kServer = "# The game's dedicated server, on this machine's loopback, with an identity\n"
                                "# made as it starts.\n"
                                "host.iteration_rate = 120\nworld.tick_rate = 60\n" +
                                kContent +
                                "network.quic.self_signed = true\nnetwork.quic.fingerprint_file = fingerprint\n"
                                "replication.endpoint = 127.0.0.1:" +
                                exported.port + "\n";
    const std::string kClient = "# The player, from this window, pinned to the server's identity, heard on\n"
                                "# this machine's sound device.\n"
                                "host.iteration_rate = 120\nbots.player = true\naudio.play = device\n" +
                                kContent +
                                "kest.plan_only = true\nnetwork.quic.pin_file = fingerprint\n"
                                "bots.endpoint = 127.0.0.1:" +
                                exported.port + "\n";
    const std::string kPlay = "# Run rawframe-play" + kSuffix +
                              " to play: it starts the server, then the client.\n"
                              "play.server = rawframe-server" +
                              kSuffix + "\nplay.server_config = server.conf\nplay.server_log = server.log\n" +
                              "play.client = rawframe-client" + kSuffix +
                              "\nplay.client_config = client.conf\nplay.client_log = client.log\n"
                              "play.fingerprint = fingerprint\n";
    if (!writeInto(exported, "server.conf", kServer, written) ||
        !writeInto(exported, "client.conf", kClient, written) || !writeInto(exported, "play.conf", kPlay, written)) {
        return false;
    }
    written.push_back(exported.record);
    return true;
}

/// What a web export takes from the web build and the native one.
struct WebFiles {
    fs::path client;
    fs::path window;
    fs::path device;
    /// The page's own files: `play.html` and its modules.
    fs::path page;
    fs::path server;
};

/// The site under `web/` and its server under `server/` (D397).
bool writeWeb(const Exported& exported, const WebFiles& from, std::vector<std::string>& written) {
    if (!copyInto(from.page / "play.html", exported, "web/index.html", written) ||
        !copyInto(from.client, exported, "web/client.wasm", written) ||
        !copyInto(from.window, exported, "web/maul-window.mjs", written) ||
        !copyInto(from.device, exported, "web/maul-rhi.mjs", written) ||
        !copyInto(from.server, exported, "server/rawframe-server" + exported.suffix, written)) {
        return false;
    }
    for (const char* module : {"client.mjs", "sound.mjs", "transport.mjs", "wasi.mjs"}) {
        if (!copyInto(from.page / module, exported, std::string{"web/page/"} + module, written)) {
            return false;
        }
    }
    // What the page fetches and hands the client, and what the client plays;
    // the page adds where the server is.
    document::Value files = document::Value::array();
    for (const std::string& file : filesUnder(exported.output / "web" / "library")) {
        files.push(document::Value::string(file));
    }
    document::Value play = document::Value::object();
    play.add("composition", document::Value::string(exported.record));
    play.add("configuration",
             document::Value::string("host.iteration_rate = 120\nbots.player = true\naudio.play = sink\n"
                                     "kest.game_resource = " +
                                     exported.gameResource + "\ncontent.composition = " + exported.record +
                                     "\ncontent.library = library\nkest.plan_only = true\n"));
    play.add("files", std::move(files));
    play.add("port", document::Value::integer(std::stoll(exported.port)));
    const auto kPlay = document::writeCanonicalRecord(play);
    const std::string kServer = "# The game's dedicated server, on every address, for browsers over\n"
                                "# WebTransport, with an identity made as it starts, whose fingerprint the\n"
                                "# page reads beside itself. Run it from anywhere: rawframe-server" +
                                exported.suffix +
                                " --config server.conf\n"
                                "host.iteration_rate = 120\nworld.tick_rate = 60\n"
                                "kest.game_resource = " +
                                exported.gameResource + "\ncontent.composition = ../web/" + exported.record +
                                "\ncontent.library = ../web/library\n"
                                "network.quic.self_signed = true\nnetwork.quic.webtransport = true\n"
                                "network.quic.fingerprint_file = ../web/fingerprint\n"
                                "replication.endpoint = :" +
                                exported.port + "\n";
    if (!kPlay.has_value() || !writeInto(exported, "web/play.json", *kPlay, written) ||
        !writeInto(exported, "server/server.conf", kServer, written)) {
        return false;
    }
    written.push_back("web/" + exported.record);
    return true;
}

int usage() {
    std::fputs("usage: rawframe-export <game directory> <output directory> [--game <file>] [--port <port>]\n"
               "                       [--version <version>] [--tools <directory>] [--<tool> <path>]...\n"
               "                       [--key <secret key> --publisher <name>] [--target web]\n",
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
    std::optional<std::string> port;
    std::string version = "0.1.0";
    std::optional<fs::path> key;
    std::string publisher = "local";
    bool web = false;
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
        } else if (kOption == "--target" && (kValue == "web" || kValue == "native")) {
            web = kValue == "web";
        } else if (kOption == "--cook" || kOption == "--build" || kOption == "--server" || kOption == "--client" ||
                   kOption == "--play" || kOption == "--web-client" || kOption == "--maul-window" ||
                   kOption == "--maul-rhi" || kOption == "--page") {
            tools.emplace(kOption.substr(2), kValue);
        } else {
            return usage();
        }
    }
    if (argc % 2 == 0 || (port.has_value() && !portLike(*port))) {
        return usage();
    }
    const auto kTool = [&](const char* name) {
        const auto kNamed = tools.find(name);
        return kNamed != tools.end() ? fs::absolute(kNamed->second)
                                     : toolDirectory / (std::string{"rawframe-"} + name + kSuffix);
    };
    // What a web export takes from the web build, under `<tools>/web/`.
    const auto kWebFile = [&](const char* name, const char* file) {
        const auto kNamed = tools.find(name);
        return kNamed != tools.end() ? fs::absolute(kNamed->second) : toolDirectory / "web" / file;
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
    if (!port.has_value()) {
        port = portOf(kGameResource);
    }

    // The working files under the output, gone at the end.
    const fs::path kWork = kOutput / ".export";
    // The folder the client plays from: the site, for the web.
    const fs::path kSite = web ? kOutput / "web" : kOutput;
    const fs::path kLibrary = kSite / "library";
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
                                 std::string{web ? "web" : kPlatform},
                                 std::string{web ? "wasm32" : kArchitecture},
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
                                   {"compose", kLibrary.string(), *kRoot, "tool", (kSite / kRecord).string()},
                                   kWork / "compose.log");
    const auto kComposition = kComposed ? wordAfter(*kComposed, "composition") : std::nullopt;
    if (!kComposition.has_value()) {
        return 1;
    }

    // The programs, and what each reads: every path under the folder.
    const Exported kExported{
        .output = kOutput, .record = kRecord, .gameResource = kGameResource, .port = *port, .suffix = kSuffix};
    std::vector<std::string> written;
    const bool kWritten = web ? writeWeb(kExported,
                                         {.client = kWebFile("web-client", "rawframe-web-client.wasm"),
                                          .window = kWebFile("maul-window", "maul-window.mjs"),
                                          .device = kWebFile("maul-rhi", "maul-rhi.mjs"),
                                          .page = kWebFile("page", "page"),
                                          .server = kTool("server")},
                                         written)
                              : writeNative(kExported, {kTool("server"), kTool("client"), kTool("play")}, written);
    if (!kWritten) {
        return 1;
    }
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
    receipt.add("port", document::Value::integer(std::stoll(*port)));
    receipt.add("target", document::Value::string(web ? "web" : "native"));
    const auto kReceipt = document::writeCanonicalRecord(receipt);
    if (!kReceipt.has_value() || !writeText(kOutput / "export.receipt", *kReceipt)) {
        std::fputs("rawframe-export: the receipt cannot be written\n", stderr);
        return 1;
    }
    std::printf("exported %s, build %s, composition %s\n", kName.c_str(), kRoot->c_str(), kComposition->c_str());
    return 0;
}
