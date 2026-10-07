#pragma once

// The material importer (`rawframe.material`, ADR-0027, D303): a surface
// material's graph document, read as hostile input in its canonical form,
// compiled, and cooked into the runtime's material. It takes no settings.
// A graph the blob cannot fold is written as Slang and cooked with its own
// program, where the shader toolchain is given (D484).

#include "rawframe/base/sha256.h"
#include "rawframe/cook/cook.h"

#include <filesystem>
#include <optional>

namespace rawframe::cook {

/// The shader toolchain a program material is built with (D484): a Python
/// and the engine's shader generator (`tools/gen_shaders.py`), run as
/// `<python> <generator> --material <source> <directory>`, which links the
/// scene's containers with the material, with the pinned Slang compiler
/// and the cross compilers it finds; and the digest of all it runs, which
/// enters the key of every material.
struct ShaderTools {
    std::filesystem::path python;
    std::filesystem::path generator;
    base::Sha256Digest identity{};
};

/// Without the toolchain, a graph the blob cannot fold is refused
/// (`ToolFailed`), saying the toolchain is needed.
[[nodiscard]] Importer materialImporter(std::optional<ShaderTools> tools = std::nullopt);
/// The post-process importer (`rawframe.postprocess`, D348): a post
/// process's graph document, read and folded alike. It takes no settings.
[[nodiscard]] Importer postProcessImporter() noexcept;
/// The canvas material importer (`rawframe.canvasmaterial`, D355): a
/// canvas material's graph document, read and folded alike. It takes no
/// settings.
[[nodiscard]] Importer canvasImporter() noexcept;

} // namespace rawframe::cook
