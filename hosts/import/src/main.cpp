// The sound import tool (ADR-0058): reads a WAVE, Ogg Vorbis, or MP3 source
// and writes it in the runtime's short-form tier. Import tooling, run by an
// author on their own files; never part of a client or a server.
//
//   rawframe-import <source> <cooked.wav>

#include "rawframe/audio_import/import.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fputs("usage: rawframe-import <source> <cooked.wav>\n", stderr);
        return 2;
    }
    std::ifstream source{argv[1], std::ios::binary};
    if (!source) {
        std::fprintf(stderr, "rawframe-import: cannot read %s\n", argv[1]);
        return 1;
    }
    const std::vector<char> kRead{std::istreambuf_iterator<char>{source}, std::istreambuf_iterator<char>{}};
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(kRead.data()), kRead.size()};
    const auto kForm = rawframe::audio_import::sniff(kBytes);
    auto clip = rawframe::audio_import::importSound(kBytes);
    if (!clip.has_value()) {
        std::fprintf(stderr,
                     "rawframe-import: %s: %.*s\n",
                     argv[1],
                     static_cast<int>(clip.error().description().size()),
                     clip.error().description().data());
        return 1;
    }
    const std::vector<std::byte> kCooked = rawframe::audio_import::cook(*clip);
    std::ofstream cooked{argv[2], std::ios::binary};
    cooked.write(reinterpret_cast<const char*>(kCooked.data()), static_cast<std::streamsize>(kCooked.size()));
    if (!cooked) {
        std::fprintf(stderr, "rawframe-import: cannot write %s\n", argv[2]);
        return 1;
    }
    const std::string_view kName = rawframe::audio_import::describe(*kForm);
    std::printf("imported %.*s: %u channels at %u Hz, %zu frames, %zu bytes cooked\n",
                static_cast<int>(kName.size()),
                kName.data(),
                clip->channels,
                clip->rate,
                clip->frames(),
                kCooked.size());
    return 0;
}
