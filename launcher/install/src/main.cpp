// The install tool (SPEC-0038): a launcher's installing and updating as a
// process, over rawframe.install. It updates a library to a Composition from
// an origin, rolls it back, heals it, collects what it no longer needs, and
// says what is installed. Never part of a client or a server.
//
//   rawframe-install update <library> <record> <origin>
//   rawframe-install rollback <library>
//   rawframe-install heal <library> <origin>
//   rawframe-install collect <library>
//   rawframe-install status <library>
//
// An origin is a mirror laid out as a library. `update` and `heal` print
// what they fetched; `status` prints the active Composition and those
// retained, newest first, each with the path of its kept record, which a
// launcher names as `content.composition` beside `content.library`.

#include "rawframe/content/identity.h"
#include "rawframe/content/library.h"
#include "rawframe/install/installation.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

using rawframe::install::Installation;

void print(const rawframe::result::Error& error) {
    std::fprintf(
        stderr, "rawframe-install: %.*s\n", static_cast<int>(error.description().size()), error.description().data());
    for (const auto& field : error.context()) {
        std::fprintf(stderr,
                     "  %.*s: %.*s\n",
                     static_cast<int>(field.key.size()),
                     field.key.data(),
                     static_cast<int>(field.value.size()),
                     field.value.data());
    }
}

void report(std::string_view what, const rawframe::install::UpdateReport& done) {
    std::printf("%.*s: fetched %zu blobs, %llu bytes, healed %zu\n",
                static_cast<int>(what.size()),
                what.data(),
                done.fetched,
                static_cast<unsigned long long>(done.fetchedBytes),
                done.healed);
}

void line(std::string_view what, const std::filesystem::path& library, const rawframe::base::Sha256Digest& id) {
    std::printf("%.*s %s %s\n",
                static_cast<int>(what.size()),
                what.data(),
                rawframe::content::ContentDigest{.bytes = id}.text().c_str(),
                (library / rawframe::content::compositionPathOf(id)).string().c_str());
}

int run(std::string_view command, const std::filesystem::path& library, int argc, char** argv) {
    auto installation = Installation::open(library);
    if (!installation.has_value()) {
        print(installation.error());
        return 1;
    }
    if (command == "update" && argc == 5) {
        std::ifstream file{argv[3], std::ios::binary};
        const std::string kRecord{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        auto origin = rawframe::install::mirrorAt(argv[4]);
        const auto kDone = origin.has_value() ? installation->update(kRecord, **origin)
                                              : rawframe::result::Result<rawframe::install::UpdateReport>{
                                                    std::unexpected<rawframe::result::Error>{origin.error().clone()}};
        if (!kDone.has_value()) {
            print(kDone.error());
            return 1;
        }
        report("updated", *kDone);
        return 0;
    }
    if (command == "heal" && argc == 4) {
        auto origin = rawframe::install::mirrorAt(argv[3]);
        const auto kDone = origin.has_value() ? installation->heal(**origin)
                                              : rawframe::result::Result<rawframe::install::UpdateReport>{
                                                    std::unexpected<rawframe::result::Error>{origin.error().clone()}};
        if (!kDone.has_value()) {
            print(kDone.error());
            return 1;
        }
        report("healed", *kDone);
        return 0;
    }
    if (command == "rollback" && argc == 3) {
        const auto kDone = installation->rollback();
        if (!kDone.has_value()) {
            print(kDone.error());
            return 1;
        }
        line("active", library, *installation->installed().active);
        return 0;
    }
    if (command == "collect" && argc == 3) {
        const auto kRemoved = installation->collect();
        if (!kRemoved.has_value()) {
            print(kRemoved.error());
            return 1;
        }
        std::printf("collected %zu blobs\n", *kRemoved);
        return 0;
    }
    if (command == "status" && argc == 3) {
        if (installation->installed().active.has_value()) {
            line("active", library, *installation->installed().active);
        }
        for (const rawframe::base::Sha256Digest& each : installation->installed().retained) {
            line("retained", library, each);
        }
        return 0;
    }
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    const int kStatus = argc >= 3 ? run(argv[1], argv[2], argc, argv) : 2;
    if (kStatus == 2) {
        std::fputs("usage: rawframe-install update <library> <record> <origin>\n"
                   "       rawframe-install rollback <library>\n"
                   "       rawframe-install heal <library> <origin>\n"
                   "       rawframe-install collect <library>\n"
                   "       rawframe-install status <library>\n",
                   stderr);
    }
    return kStatus;
}
