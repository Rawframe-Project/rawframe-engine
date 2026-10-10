#include "folders.h"

#include "rawframe/base/sha256.h"
#include "rawframe/content/composition_record.h"
#include "rawframe/content/library.h"
#include "rawframe/document/json.h"
#include "rawframe/network_quic/certificate.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rawframe::export_tool {

namespace {

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

/// Writes a secret to `file` under the output, readable and writable by its
/// owner alone before any of it is written (as `rawframe-build key` keeps a
/// publisher's), and adds it to `written`.
bool writeSecretInto(const Exported& exported,
                     const std::string& file,
                     std::string_view text,
                     std::vector<std::string>& written) {
    const fs::path kPath = exported.output / file;
    std::error_code error;
    std::ofstream out{kPath, std::ios::binary | std::ios::trunc};
    fs::permissions(kPath, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, error);
    if (!out || error) {
        std::fprintf(stderr, "rawframe-export: %s cannot be kept secret\n", file.c_str());
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        std::fprintf(stderr, "rawframe-export: %s cannot be written\n", file.c_str());
        return false;
    }
    written.push_back(file);
    return true;
}

/// Text as an XML property list's string holds it.
std::string escapedForPlist(std::string_view text) {
    std::string escaped;
    for (const char kEach : text) {
        escaped += kEach == '&'   ? std::string{"&amp;"}
                   : kEach == '<' ? std::string{"&lt;"}
                   : kEach == '>' ? std::string{"&gt;"}
                                  : std::string{kEach};
    }
    return escaped;
}

} // namespace

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

bool installRecord(const fs::path& output,
                   const fs::path& library,
                   const fs::path& record,
                   std::vector<std::string>& written) {
    const auto kText = readText(record);
    if (!kText.has_value() || !rawframe::content::readComposition(*kText).has_value()) {
        std::fputs("rawframe-export: the Composition the build tool wrote does not read\n", stderr);
        return false;
    }
    const rawframe::base::Sha256Digest kId = rawframe::content::compositionIdOf(*kText);
    const auto kPointer = rawframe::content::writeInstalled({.active = kId, .retained = {}});
    const std::string kKept = rawframe::content::compositionPathOf(kId);
    std::error_code error;
    fs::create_directories((library / kKept).parent_path(), error);
    if (!kPointer.has_value() || !writeText(library / kKept, *kText) ||
        !writeText(library / rawframe::content::kInstalledName, *kPointer)) {
        std::fputs("rawframe-export: the library cannot be installed\n", stderr);
        return false;
    }
    fs::remove(record, error);
    const std::string kUnder = fs::relative(library, output, error).generic_string();
    written.push_back(kUnder + "/" + kKept);
    written.push_back(kUnder + "/" + std::string{rawframe::content::kInstalledName});
    return true;
}

bool writeNative(const Exported& exported, const std::array<fs::path, 4>& programs, std::vector<std::string>& written) {
    const std::string& kSuffix = exported.suffix;
    const std::array<std::string, 4> kFiles{"rawframe-server" + kSuffix,
                                            "rawframe-client" + kSuffix,
                                            "rawframe-play" + kSuffix,
                                            "rawframe-install" + kSuffix};
    // The install program only for a game that follows a channel.
    for (std::size_t at = 0; at < (exported.follow.has_value() ? 4U : 3U); ++at) {
        if (!copyInto(programs[at], exported, kFiles[at], written)) {
            return false;
        }
    }
    // The library alone: its active Composition (D434).
    const std::string kContent = "kest.game_resource = " + exported.gameResource + "\ncontent.library = library\n";
    const std::string kServer = "# The game's dedicated server, on this machine's loopback, with an identity\n"
                                "# made as it starts.\n"
                                "host.iteration_rate = 120\nworld.tick_rate = 60\n" +
                                kContent +
                                "network.quic.self_signed = true\nnetwork.quic.fingerprint_file = fingerprint\n"
                                "replication.endpoint = 127.0.0.1:" +
                                exported.port + "\n";
    const std::string kClient = "# The player, from this window, pinned to the server's identity, drawn on\n"
                                "# this machine's graphics device (a software one where it has none) and\n"
                                "# heard on its sound device.\n"
                                "host.iteration_rate = 120\nbots.player = true\nrender.device = any\n"
                                "audio.play = device\n"
                                "# Its view drawn at fewer pixels, down to half each way, while the device\n"
                                "# cannot keep up (D533).\n"
                                "scene.render_scale_least_percent = 50\n"
                                "# No picture read back: that is for tests (D534).\n"
                                "render.read_every = 0\nwindow.title = " +
                                exported.title + "\n" + kContent +
                                "kest.plan_only = true\nnetwork.quic.pin_file = fingerprint\n"
                                "bots.endpoint = 127.0.0.1:" +
                                exported.port + "\n";
    const std::string kPlay =
        "# Run rawframe-play" + kSuffix +
        " to play: it starts the server, then the client.\n"
        "play.server = rawframe-server" +
        kSuffix + "\nplay.server_config = server.conf\nplay.server_log = server.log\n" +
        "play.client = rawframe-client" + kSuffix +
        "\nplay.client_config = client.conf\nplay.client_log = client.log\n"
        "play.fingerprint = fingerprint\n" +
        (exported.follow.has_value()
             ? "# Before each play, the game's channel followed and its Release installed.\n"
               "play.installer = rawframe-install" +
                   kSuffix + "\nplay.library = library\nplay.follow_subject = " + exported.subject +
                   "\nplay.follow_channel = " + exported.channel + "\nplay.follow_origin = " + *exported.follow +
                   "\nplay.follow_log = follow.log\n"
             : std::string{});
    if (!writeInto(exported, "server.conf", kServer, written) ||
        !writeInto(exported, "client.conf", kClient, written) || !writeInto(exported, "play.conf", kPlay, written)) {
        return false;
    }
    return true;
}

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

bool writeMobile(const Exported& exported,
                 const Mobile& mobile,
                 const std::string& address,
                 const fs::path& server,
                 std::vector<std::string>& written) {
    // Ten years: a package pins it for as long as it is played.
    const auto kIdentity = network_quic::makeSelfSignedCertificate("rawframe-game-server", 3650);
    if (!kIdentity.has_value() || !network_quic::fingerprintOf(*kIdentity).has_value()) {
        std::fputs("rawframe-export: the server's identity cannot be made\n", stderr);
        return false;
    }
    const network_quic::Fingerprint kFingerprint = *network_quic::fingerprintOf(*kIdentity);
    const std::string kContent = "kest.game_resource = " + exported.gameResource + "\ncontent.library = library\n";
    const std::string kServer = "# The game's dedicated server, on every address, with the identity the\n"
                                "# " +
                                mobile.what + " is pinned to. Run it from anywhere: rawframe-server" + exported.suffix +
                                " --config server.conf\n"
                                "host.iteration_rate = 120\nworld.tick_rate = 60\n"
                                "kest.game_resource = " +
                                exported.gameResource + "\ncontent.library = ../" + mobile.directory +
                                "/game/library\n"
                                "network.quic.certificate_file = identity.pem\n"
                                "network.quic.private_key_file = identity.key\n"
                                "replication.endpoint = :" +
                                exported.port + "\n";
    const std::string kClient =
        mobile.player +
        "host.iteration_rate = 120\nbots.player = true\nrender.device = any\n"
        "audio.play = device\nscene.render_scale_least_percent = 50\n"
        "render.read_every = 0\n" +
        kContent + "kest.plan_only = true\nnetwork.quic.pin = " + network_quic::formatFingerprint(kFingerprint) +
        "\nbots.endpoint = " + address + ":" + exported.port + "\n";
    return copyInto(server, exported, "server/rawframe-server" + exported.suffix, written) &&
           writeInto(exported, "server/identity.pem", kIdentity->certificatePem, written) &&
           writeSecretInto(exported, "server/identity.key", kIdentity->privateKeyPem, written) &&
           writeInto(exported, "server/server.conf", kServer, written) &&
           writeInto(exported, mobile.directory + "/game/client.conf", kClient, written);
}

bool bundleIdentifierLike(std::string_view identifier) {
    return !identifier.empty() && identifier.size() <= 155 && std::ranges::all_of(identifier, [](char each) {
        return std::isalnum(static_cast<unsigned char>(each)) != 0 || each == '.' || each == '-';
    });
}

bool writeApplication(const Exported& exported,
                      const fs::path& client,
                      const std::string& application,
                      const std::string& identifier,
                      std::vector<std::string>& written) {
    std::error_code error;
    const fs::path kApplication = exported.output / application;
    fs::copy(client, kApplication, fs::copy_options::recursive, error);
    fs::copy(exported.output / "ios" / "game", kApplication / "game", fs::copy_options::recursive, error);
    const auto kPlist = readText(kApplication / "Info.plist");
    if (error || !kPlist.has_value()) {
        std::fprintf(stderr, "rawframe-export: %s cannot be copied\n", client.string().c_str());
        return false;
    }
    std::string plist = *kPlist;
    for (const auto& [kFrom, kTo] :
         {std::pair<std::string, std::string>{"<string>dev.rawframe.client</string>",
                                              "<string>" + identifier + "</string>"},
          std::pair<std::string, std::string>{"<string>Rawframe</string>",
                                              "<string>" + escapedForPlist(exported.title) + "</string>"}}) {
        const std::size_t kAt = plist.find(kFrom);
        if (kAt == std::string::npos) {
            std::fprintf(stderr, "rawframe-export: %s is not the iOS client\n", client.string().c_str());
            return false;
        }
        plist.replace(kAt, kFrom.size(), kTo);
    }
    if (!writeText(kApplication / "Info.plist", plist)) {
        std::fprintf(stderr, "rawframe-export: %s cannot be written\n", application.c_str());
        return false;
    }
    for (const auto& kEntry : fs::recursive_directory_iterator(kApplication, error)) {
        if (kEntry.is_regular_file()) {
            written.push_back(fs::relative(kEntry.path(), exported.output).generic_string());
        }
    }
    return !error;
}

} // namespace rawframe::export_tool
