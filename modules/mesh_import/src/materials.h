#pragma once

// A glTF's materials made Rawframe materials (D314), for the importer: each
// material and image made once, when a primitive first draws with it.

#include "rawframe/mesh_import/import.h"

#include <cgltf.h>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::mesh_import {

class MaterialMaker {
public:
    /// `read` and `identify` must outlive this.
    MaterialMaker(const ReadFile& read, const Identify& identify) noexcept : read_(&read), identify_(&identify) {
    }

    /// The identity of the material a primitive draws with, made when it is
    /// new; nought for a primitive with none.
    [[nodiscard]] result::Result<std::uint64_t> identityOf(const cgltf_material* material);

    /// Whether a material made samples a texture.
    [[nodiscard]] bool samples() const noexcept {
        return !textures_.empty();
    }

    /// What was made, each in key order.
    [[nodiscard]] std::vector<ImportedMaterial> takeMaterials();
    [[nodiscard]] std::vector<ImportedTexture> takeTextures();

private:
    [[nodiscard]] result::Result<material::SampledTexture> sampled(const cgltf_texture_view& view, bool color);
    [[nodiscard]] result::Result<std::uint64_t> imageOf(const cgltf_image& image, bool color);

    const ReadFile* read_ = nullptr;
    const Identify* identify_ = nullptr;
    std::map<const cgltf_material*, std::uint64_t> made_;
    /// Each image's identity and whether it is sampled as color.
    std::map<const cgltf_image*, std::pair<std::uint64_t, bool>> images_;
    std::set<std::string> keys_;
    std::vector<ImportedMaterial> materials_;
    std::vector<ImportedTexture> textures_;
};

} // namespace rawframe::mesh_import
