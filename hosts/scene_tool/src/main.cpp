// The scene tool (ADR-0048, D94): authoring help for scene documents, run
// by an author; never part of a client or a server.
//
//   rawframe-scene from-spawns <game description> <scene>
//
// `from-spawns` writes the description's spawn lines as a scene: each
// entity a line spawns, with a new SourceEntityId, its fields, and each
// component's layout mark from the game's program. The description is left
// as it is; replacing its spawn lines with a `scene` line is the author's.

#include "rawframe/scene/scene.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/spawn_scene.h"

#include <cstdio>
#include <fstream>
#include <random>
#include <string_view>

namespace {

void print(const rawframe::result::Error& error) {
    std::fprintf(
        stderr, "rawframe-scene: %.*s\n", static_cast<int>(error.description().size()), error.description().data());
    for (const auto& field : error.context()) {
        std::fprintf(stderr,
                     "  %.*s: %.*s\n",
                     static_cast<int>(field.key.size()),
                     field.key.data(),
                     static_cast<int>(field.value.size()),
                     field.value.data());
    }
}

/// A fresh SourceEntityId: random, as a version 4 UUID.
rawframe::base::Bits128 freshId() {
    std::random_device device;
    const auto kWord = [&device] {
        return (std::uint64_t{device()} << 32U) | std::uint64_t{device()};
    };
    rawframe::base::Bits128 id{.high = kWord(), .low = kWord()};
    id.high = (id.high & ~std::uint64_t{0xF000}) | std::uint64_t{0x4000};
    id.low = (id.low & ~(std::uint64_t{0xC} << 60U)) | (std::uint64_t{0x8} << 60U);
    return id;
}

int fromSpawns(const char* game, const char* output) {
    auto files = rawframe::world_kest::GameFiles::fromDirectory(game);
    if (!files.has_value()) {
        print(files.error());
        return 1;
    }
    auto program = files->compile(files->description().program);
    if (!program.has_value()) {
        print(program.error());
        return 1;
    }
    auto made = rawframe::world_kest::spawnsAsScene(files->description(), **program, &freshId);
    auto text = made.has_value() ? rawframe::scene::writeScene(*made)
                                 : rawframe::result::Result<std::string>{std::unexpected{made.error().clone()}};
    if (!text.has_value()) {
        print(text.error());
        return 1;
    }
    std::ofstream{output, std::ios::binary} << *text;
    std::printf("scene of %zu entities\n", made->entities.size());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string_view{argv[1]} == "from-spawns") {
        return fromSpawns(argv[2], argv[3]);
    }
    std::fputs("usage: rawframe-scene from-spawns <game description> <scene>\n", stderr);
    return 2;
}
