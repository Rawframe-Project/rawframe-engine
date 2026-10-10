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
//                   [--target android --address <host>
//                    [--android-client <library> --packager <script> [--package <name>]
//                     [--android-keystore <file> --android-key-alias <alias>]]]
//                   [--target ios --address <host> [--ios-client <application>]]
//                   [--follow <origin> [--channel <channel>]] [--title <title>]
//
// With `--key`, the Build is signed by that secret (`rawframe-build key`
// writes it beside `<publisher>.keys`, which the folder's library pins);
// the secret is read, never copied. Without it, a key is made for this
// export under the publisher `local`, and its secret deleted once the Build
// is signed.
//
// The client's window carries `--title`, the game's directory name unless
// given (D519).
//
// The game is `<directory name>.game` unless named. The tools (rawframe-cook,
// rawframe-build, rawframe-server, rawframe-client, rawframe-play) are found
// beside this program unless `--tools` names their directory, or a
// `--<tool> <path>` names one (`--cook`, `--build`, `--server`, `--client`,
// `--play`). The output directory must not exist, or be empty. Running the
// folder's `rawframe-play` plays the game; `export.receipt` lists what was
// written, each file's SHA-256, the Build's root, and the Composition.
//
// The folder's library is installed as `rawframe-install` leaves one: its
// Composition kept under `compositions/` and named active by `installed`,
// so the server and the client play the library's active Composition
// (D434). With `--follow`, the game follows its subject's channel
// (`stable` unless `--channel` names another) on that origin, a directory
// or an http or https URL: `rawframe-install` is copied beside the
// launcher, found as the other tools are (`--install`), and the launcher
// runs its `follow` before each play, so a Release the publisher points the
// channel at is installed, verified against the key set the library pins,
// before the game starts.
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
//
// With `--target android` (D555) the game is packed for Android and the
// folder holds what its package carries and the server it plays on.
// `android/game/` is the directory `tools/android_apk.sh` carries into the
// client's package (D553): the library with its active Composition, and the
// client's configuration, pinned to the server's identity and reaching it at
// `--address`, the host or IP address a phone reaches the server's machine
// by. `server/` holds the dedicated server, listening on every address at
// the port with that identity, which the export makes once (a self-signed
// P-256 certificate and its key, `server/identity.pem` and
// `server/identity.key`): a package is made once and cannot read a
// fingerprint a server writes as it starts, as a page or a native folder
// does. The identity is the game server's secret; the package holds only its
// fingerprint. With `--android-client` (the Android build's
// `librawframe_client.so`) and `--packager` (`tools/android_apk.sh`, which
// finds the SDK by ANDROID_HOME), the export makes the package itself,
// `android/<name>.apk`, named `<publisher>.<name>` unless `--package` names
// it (D556). With `--android-keystore` and `--android-key-alias`, the
// publisher's key signs a release package, not debuggable, the keystore's
// password read by the packager from RAWFRAME_ANDROID_KEYSTORE_PASSWORD and
// never given on a command line (D581).
//
// With `--target ios` (D587) the game is packed for iOS as for Android:
// `ios/game/` holds what the application carries, read by the iOS client
// from `game/` in its bundle (D586), and `server/` the server it plays on,
// pinned the same way. With `--ios-client` (the iOS build's
// `rawframe-client.app`) the export makes the application itself,
// `ios/<name>.app`, the client's bundle with `ios/game/` in it as `game/`,
// identified as `<publisher>.<name>` and named by the title. It is for the
// simulator, whose applications need no signature; one for a device is
// signed by its publisher, which this does not do yet.

#include "folders.h"
#include "rawframe/base/sha256.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/content/composition_record.h"
#include "rawframe/content/library.h"
#include "rawframe/document/json.h"
#include "rawframe/input/actions.h"
#include "rawframe/network_quic/certificate.h"
#include "rawframe/process/child.h"
#include "rawframe/process/self.h"
#include "rawframe/release/release.h"

#include <algorithm>
#include <array>
#include <cctype>
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
#include <utility>
#include <vector>

namespace {

using namespace rawframe;
using namespace rawframe::export_tool;

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
        std::string_view line{printed.data() + at, (kEnd == std::string::npos ? printed.size() : kEnd) - at};
        // A Windows tool's text output ends its lines with a carriage
        // return too, which is no part of the line's last word.
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        const std::string_view kLine = line;
        if (kLine.starts_with(kPrefix)) {
            const std::string_view kRest = kLine.substr(kPrefix.size());
            return std::string{kRest.substr(0, kRest.find(' '))};
        }
        at = kEnd == std::string::npos ? printed.size() : kEnd + 1;
    }
    return std::nullopt;
}

/// A control as a player reads it (D542), not as the action set names it:
/// `key_a` is A, `arrow_up` the up arrow, `face_south` the bottom face
/// button, `trigger_left` the left trigger.
std::string controlText(std::string_view name) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 19> kNamed = {{
        {"space", "Space"},
        {"arrow_up", "up arrow"},
        {"arrow_down", "down arrow"},
        {"enter", "Enter"},
        {"escape", "Esc"},
        {"tab", "Tab"},
        {"backspace", "Backspace"},
        {"face_south", "bottom face button (A, Cross)"},
        {"face_east", "right face button (B, Circle)"},
        {"face_west", "left face button (X, Square)"},
        {"face_north", "top face button (Y, Triangle)"},
        {"stick_left_click", "left stick press"},
        {"stick_right_click", "right stick press"},
        {"start", "Start"},
        {"select", "Select"},
        {"guide", "Guide"},
        {"middle", "middle button"},
        {"wheel_y", "wheel"},
        {"motion", "movement"},
    }};
    if (const auto kFound = std::ranges::find(kNamed, name, &std::pair<std::string_view, std::string_view>::first);
        kFound != kNamed.end()) {
        return std::string{kFound->second};
    }
    std::string text;
    if (name.starts_with("key_") || name.starts_with("digit_")) {
        for (const char kLetter : name.substr(name.find('_') + 1)) {
            text += static_cast<char>(std::toupper(static_cast<unsigned char>(kLetter)));
        }
        return text;
    }
    if (name.size() <= 3 && name.starts_with('f')) {
        return "F" + std::string{name.substr(1)};
    }
    if (name.starts_with("dpad_")) {
        return "D-pad " + std::string{name.substr(5)};
    }
    if (name == "left" || name == "right") {
        return std::string{name} + " button";
    }
    // `<thing>_left` is the left thing, and the rest is read with spaces.
    std::string_view rest = name;
    for (const std::string_view kSide : {std::string_view{"left"}, std::string_view{"right"}}) {
        if (rest.ends_with("_" + std::string{kSide})) {
            text = std::string{kSide} + " ";
            rest.remove_suffix(kSide.size() + 1);
        }
    }
    for (const char kLetter : rest) {
        text += kLetter == '_' ? ' ' : kLetter;
    }
    return text;
}

/// A binding as a player reads it: its device, then its control, or each
/// of a pair's or a quad's by what it does.
std::string bindingText(const rawframe::input::Binding& binding) {
    using rawframe::input::Composite;
    std::string text{rawframe::input::nameOf(binding.device)};
    const auto kControl = [&binding](std::size_t at) {
        return controlText(rawframe::input::nameOf(binding.controls.at(at)));
    };
    if (binding.composite == Composite::None) {
        return text + " " + kControl(0);
    }
    if (binding.composite == Composite::Pair) {
        return text + " " + kControl(0) + " and " + kControl(1);
    }
    return text + " " + kControl(0) + ", " + kControl(1) + ", " + kControl(2) + ", " + kControl(3) +
           " (up, down, left, right)";
}

/// What a player of the exported game reads first: how to start it, and
/// each action of the game's `actions` file with its default bindings.
std::string howToPlay(const std::filesystem::path& game, const std::string& gameFile, const std::string& suffix) {
    std::string text = "To play, run rawframe-play" + suffix +
                       " in this folder: it starts the game's server, then the game in a window.\n"
                       "Closing the window ends both. The server's and the game's records are\n"
                       "written beside them, in server.log and client.log. F11, or Alt and Enter,\n"
                       "puts the window on the whole screen and back; window.fullscreen = true in\n"
                       "client.conf starts it there. Where the graphics device cannot keep up, the\n"
                       "picture is drawn at fewer pixels, down to half each way;\n"
                       "scene.render_scale_least_percent = 100 in client.conf keeps every one.\n";
    std::ifstream description{game / gameFile};
    std::string line;
    while (std::getline(description, line)) {
        if (!line.starts_with("actions ")) {
            continue;
        }
        std::ifstream file{game / line.substr(8), std::ios::binary};
        const std::string kBytes{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        const auto kSet = rawframe::input::readActionSet(kBytes);
        if (!kSet.has_value()) {
            break;
        }
        text += "\nControls:\n";
        for (const rawframe::input::Action& kAction : kSet->actions) {
            text += "  " + (kAction.displayName.empty() ? kAction.name : kAction.displayName) + ":";
            const char* separator = " ";
            for (const rawframe::input::Binding& kBinding : kAction.bindings) {
                text += separator + bindingText(kBinding);
                separator = "; ";
            }
            text += "\n";
        }
        break;
    }
    return text;
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

/// Whether `address` names a host or an IPv4 address alone: letters,
/// digits, dots, and dashes.
bool addressLike(std::string_view address) {
    return !address.empty() && address.size() <= 253 && std::ranges::all_of(address, [](char each) {
        return std::isalnum(static_cast<unsigned char>(each)) != 0 || each == '.' || each == '-';
    });
}

/// Whether `name` is an Android package name: two or more dot-separated
/// parts, each a lower-case letter then lower-case letters, digits, or
/// underscores.
bool packageLike(std::string_view name) {
    if (name.empty() || name.size() > 255 || name.find('.') == std::string_view::npos) {
        return false;
    }
    bool partStart = true;
    for (const char kEach : name) {
        if (kEach == '.') {
            if (partStart) {
                return false;
            }
            partStart = true;
        } else if (partStart ? (kEach >= 'a' && kEach <= 'z')
                             : ((kEach >= 'a' && kEach <= 'z') || (kEach >= '0' && kEach <= '9') || kEach == '_')) {
            partStart = false;
        } else {
            return false;
        }
    }
    return !partStart;
}

/// Whether `title` fits one configuration value: a line, not blank, of
/// at most the value bound, with no space at either end to be trimmed off.
bool titleLike(std::string_view title) {
    return !title.empty() && title.size() <= rawframe::composition::kMaximumConfigurationValueBytes &&
           title.find_first_of("\r\n") == std::string_view::npos && title.front() != ' ' && title.back() != ' ' &&
           title.front() != '\t' && title.back() != '\t';
}

int usage() {
    std::fputs("usage: rawframe-export <game directory> <output directory> [--game <file>] [--port <port>]\n"
               "                       [--version <version>] [--tools <directory>] [--<tool> <path>]...\n"
               "                       [--key <secret key> --publisher <name>] [--target web]\n"
               "                       [--target android --address <host>]\n"
               "                       [--android-client <library> --packager <script> [--package <name>]\n"
               "                        [--android-keystore <file> --android-key-alias <alias>]]\n"
               "                       [--target ios --address <host> [--ios-client <application>]]\n"
               "                       [--follow <origin> [--channel <channel>]] [--title <title>]\n",
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
    enum class Target {
        Native,
        Web,
        Android,
        Ios
    };
    Target target = Target::Native;
    std::optional<std::string> address;
    std::optional<fs::path> androidClient;
    std::optional<fs::path> iosClient;
    std::optional<fs::path> packager;
    std::optional<std::string> package;
    std::optional<fs::path> keystore;
    std::optional<std::string> keyAlias;
    std::optional<std::string> follow;
    std::string channel = "stable";
    std::string title = kGame.filename().string();
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
        } else if (kOption == "--target" &&
                   (kValue == "web" || kValue == "native" || kValue == "android" || kValue == "ios")) {
            target = kValue == "web"       ? Target::Web
                     : kValue == "android" ? Target::Android
                     : kValue == "ios"     ? Target::Ios
                                           : Target::Native;
        } else if (kOption == "--address") {
            address = kValue;
        } else if (kOption == "--android-client") {
            androidClient = fs::absolute(kValue);
        } else if (kOption == "--ios-client") {
            iosClient = fs::absolute(kValue);
        } else if (kOption == "--packager") {
            packager = fs::absolute(kValue);
        } else if (kOption == "--package") {
            package = kValue;
        } else if (kOption == "--android-keystore") {
            keystore = fs::absolute(kValue);
        } else if (kOption == "--android-key-alias") {
            keyAlias = kValue;
        } else if (kOption == "--follow") {
            follow = kValue;
        } else if (kOption == "--channel") {
            channel = kValue;
        } else if (kOption == "--title") {
            title = kValue;
        } else if (kOption == "--cook" || kOption == "--build" || kOption == "--server" || kOption == "--client" ||
                   kOption == "--play" || kOption == "--install" || kOption == "--web-client" ||
                   kOption == "--maul-window" || kOption == "--maul-rhi" || kOption == "--page") {
            tools.emplace(kOption.substr(2), kValue);
        } else {
            return usage();
        }
    }
    // A web export's page updates as its site does; following is native.
    // A phone's package reaches its server at the address given, and only
    // it is given one.
    const bool kWeb = target == Target::Web;
    const bool kAndroid = target == Target::Android;
    const bool kIos = target == Target::Ios;
    const bool kMobile = kAndroid || kIos;
    // The package is made with both its library and its packager, and named
    // only where it is made.
    const bool kPackaged = androidClient.has_value() || packager.has_value() || package.has_value();
    if (argc % 2 == 0 || (port.has_value() && !portLike(*port)) || (target != Target::Native && follow.has_value()) ||
        kMobile != address.has_value() || (address.has_value() && !addressLike(*address)) ||
        (iosClient.has_value() && !kIos) ||
        (kPackaged && (!kAndroid || !androidClient.has_value() || !packager.has_value())) ||
        keystore.has_value() != keyAlias.has_value() || (keystore.has_value() && !androidClient.has_value()) ||
        !rawframe::release::channelNamed(channel).has_value() || !titleLike(title)) {
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
    // The folder the client plays from: the site, for the web, and what the
    // package carries, for a phone.
    const fs::path kSite = kWeb       ? kOutput / "web"
                           : kAndroid ? kOutput / "android" / "game"
                           : kIos     ? kOutput / "ios" / "game"
                                      : kOutput;
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
                                 std::string{kWeb       ? "web"
                                             : kAndroid ? "android"
                                             : kIos     ? "ios"
                                                        : kPlatform},
                                 std::string{kWeb      ? "wasm32"
                                             : kMobile ? "arm64"
                                                       : kArchitecture},
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
    // A native folder's library, and a phone package's, is installed as
    // rawframe-install leaves one: the record kept by its digest and named
    // active (D434).
    std::vector<std::string> installed;
    if (!kWeb && !installRecord(kOutput, kLibrary, kSite / kRecord, installed)) {
        return 1;
    }

    // The programs, and what each reads: every path under the folder.
    const Exported kExported{.output = kOutput,
                             .record = kRecord,
                             .subject = publisher + "/" + kName,
                             .follow = follow,
                             .channel = channel,
                             .gameResource = kGameResource,
                             .port = *port,
                             .title = title,
                             .suffix = kSuffix};
    std::vector<std::string> written = std::move(installed);
    const bool kWritten =
        kAndroid
            ? writeMobile(kExported,
                          {.directory = "android",
                           .what = "Android package",
                           .player = "# The player on Android, from the activity's window, pinned to the server's\n"
                                     "# identity, drawn on the phone's Vulkan device and heard on its sound.\n"},
                          *address,
                          kTool("server"),
                          written)
        : kIos ? writeMobile(kExported,
                             {.directory = "ios",
                              .what = "iOS application",
                              .player = "# The player on iOS, from the application's window, pinned to the server's\n"
                                        "# identity, drawn on the phone's Metal device and heard on its sound.\n"},
                             *address,
                             kTool("server"),
                             written)
        : kWeb ? writeWeb(kExported,
                          {.client = kWebFile("web-client", "rawframe-web-client.wasm"),
                           .window = kWebFile("maul-window", "maul-window.mjs"),
                           .device = kWebFile("maul-rhi", "maul-rhi.mjs"),
                           .page = kWebFile("page", "page"),
                           .server = kTool("server")},
                          written)
               : writeNative(kExported, {kTool("server"), kTool("client"), kTool("play"), kTool("install")}, written);
    if (!kWritten) {
        return 1;
    }
    // The package itself, carrying `android/game` (D556).
    if (androidClient.has_value()) {
        const std::string kPackage = package.value_or(publisher + "." + kName);
        const std::string kApk = "android/" + kName + ".apk";
        if (!packageLike(kPackage)) {
            std::fprintf(stderr, "rawframe-export: %s is not an Android package name\n", kPackage.c_str());
            return 1;
        }
        // Made under the work directory, where the packager keeps its debug
        // key beside it, and moved into the folder alone.
        std::vector<std::string> packed = {
            (kWork / "package.apk").string(), androidClient->string(), kPackage, title, kSite.string()};
        // A release package, under the publisher's key (D581).
        if (keystore.has_value()) {
            packed.push_back(keystore->string());
            packed.push_back(*keyAlias);
        }
        if (!runTool(*packager, std::move(packed), kWork / "package.log")) {
            return 1;
        }
        fs::rename(kWork / "package.apk", kOutput / kApk, error);
        if (error) {
            std::fprintf(stderr, "rawframe-export: %s cannot be written\n", kApk.c_str());
            return 1;
        }
        written.push_back(kApk);
    }
    // The application itself, carrying `ios/game` (D587).
    if (iosClient.has_value() && !bundleIdentifierLike(publisher + "." + kName)) {
        std::fprintf(stderr, "rawframe-export: %s.%s is not a bundle identifier\n", publisher.c_str(), kName.c_str());
        return 1;
    }
    if (iosClient.has_value() &&
        !writeApplication(kExported, *iosClient, "ios/" + kName + ".app", publisher + "." + kName, written)) {
        return 1;
    }
    // How to play it, for a native folder's player.
    if (target == Target::Native) {
        if (!writeText(kOutput / "README.txt", howToPlay(kGame, gameFile, kSuffix))) {
            std::fputs("rawframe-export: README.txt cannot be written\n", stderr);
            return 1;
        }
        written.emplace_back("README.txt");
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
    receipt.add("target", document::Value::string(kWeb ? "web" : kAndroid ? "android" : kIos ? "ios" : "native"));
    const auto kReceipt = document::writeCanonicalRecord(receipt);
    if (!kReceipt.has_value() || !writeText(kOutput / "export.receipt", *kReceipt)) {
        std::fputs("rawframe-export: the receipt cannot be written\n", stderr);
        return 1;
    }
    std::printf("exported %s, build %s, composition %s\n", kName.c_str(), kRoot->c_str(), kComposition->c_str());
    return 0;
}
