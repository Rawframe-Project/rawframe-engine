// The thumbnail tool (SPEC-0051's asset browser, D899, D900): a cooked mesh,
// or a cooked material on a ball, drawn offscreen by the scene's renderer,
// its picture kept as a texture of the engine's with its smaller levels,
// which the authoring session hands an asset browser as it hands a
// texture's. Run by an authoring session, or by anyone; never part of a
// client or a server.
//
//   rawframe-thumbnail <out directory> <plan>
//
// The plan is a file of lines, their fields apart by tabs:
//
//   mesh      <cooked mesh object>
//   material  <cooked material object>  <resource id>  <representation>
//   base      <resource id>  <cooked material object>  <representation>
//   texture   <texture id>  <cooked texture object>
//
// Each mesh is drawn grey under the engine's default light, turned to show
// its top and a side, filling the picture; each material on the engine's
// ball, an instance with its base, which a `base` line names, and the
// textures it samples, which `texture` lines name by the identities its
// cooked bytes give them. Each is written as `<out>/<object's file
// name>.texture`, 256 pixels square and each half below to 16, and how much
// of the picture it covers is said. Exits 69, saying "skip: no device", on
// a machine with no adapter; 1 if any was not drawn.

#include "rawframe/base/files.h"
#include "rawframe/material/material.h"
#include "rawframe/mesh/mesh.h"
#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene/errors.h"
#include "rawframe/render_scene/materials.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/schema/registry.h"
#include "rawframe/texture/texture.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace rawframe;

constexpr std::uint32_t kSide = 256;
constexpr std::uint32_t kSmallest = 16;
/// How long a frame may take: lavapipe makes a pipeline on the CPU the
/// first time a frame uses it, slowly on a loaded machine (D476).
constexpr std::uint64_t kFrameWait = 60'000'000'000;

void say(const char* what, const std::string& about) {
    std::fprintf(stderr, "rawframe-thumbnail: %s: %s\n", what, about.c_str());
}

std::optional<std::vector<std::byte>> readBytes(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    std::vector<char> read{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(read.size());
    std::ranges::transform(read, bytes.begin(), [](char each) {
        return static_cast<std::byte>(each);
    });
    return bytes;
}

std::unique_ptr<render::Device> opened() {
    auto device = render::Device::request({.allowSoftware = true});
    if (!device.has_value()) {
        return nullptr;
    }
    for (int poll = 0; poll < 1000; ++poll) {
        const auto kOpen = (*device)->open();
        if (!kOpen.has_value()) {
            return nullptr;
        }
        if (*kOpen) {
            return std::move(*device);
        }
    }
    return nullptr;
}

/// `mesh`, known to the renderer as `id`, turned a little toward the eye
/// from above and its side, scaled so its bounding ball's radius is one,
/// and three of those ahead of the eye, where the ball just fits the view's
/// quarter turn.
render_scene::SceneDraw placed(const mesh::Mesh& mesh, std::uint64_t id) {
    std::array<float, 3> low{mesh.positions.front()[0], mesh.positions.front()[1], mesh.positions.front()[2]};
    std::array<float, 3> high = low;
    for (const mesh::Vector3& kAt : mesh.positions) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], kAt[axis]);
            high[axis] = std::max(high[axis], kAt[axis]);
        }
    }
    std::array<float, 3> middle{};
    float radius = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        middle[axis] = (low[axis] + high[axis]) / 2;
        radius += (high[axis] - low[axis]) * (high[axis] - low[axis]) / 4;
    }
    radius = std::max(std::sqrt(radius), 1e-4F);
    // A turn about Y by 35 degrees, then about X by 25 degrees toward the eye.
    const float kYaw = 35.0F * std::numbers::pi_v<float> / 180;
    const float kPitch = 25.0F * std::numbers::pi_v<float> / 180;
    const float kCy = std::cos(kYaw);
    const float kSy = std::sin(kYaw);
    const float kCp = std::cos(kPitch);
    const float kSp = std::sin(kPitch);
    // Rows of the turn: X then Y applied to a column vector.
    const std::array<std::array<float, 3>, 3> kTurn = {
        {{kCy, 0, kSy}, {kSp * kSy, kCp, -kSp * kCy}, {-kCp * kSy, kSp, kCp * kCy}}};
    const float kScale = 1 / radius;
    render_scene::SceneDraw draw{.mesh = id, .color = {0.72F, 0.72F, 0.75F, 1}};
    for (std::size_t column = 0; column < 3; ++column) {
        for (std::size_t row = 0; row < 3; ++row) {
            draw.model[(column * 4) + row] = kTurn[row][column] * kScale;
            draw.normal[(column * 4) + row] = kTurn[row][column];
        }
    }
    // The middle moved three radii ahead of the eye.
    for (std::size_t row = 0; row < 3; ++row) {
        float moved = 0;
        for (std::size_t column = 0; column < 3; ++column) {
            moved += kTurn[row][column] * kScale * middle[column];
        }
        draw.model[12 + row] = -moved + (row == 2 ? -3.0F : 0.0F);
    }
    draw.model[15] = 1;
    draw.previous = draw.model;
    return draw;
}

/// How much of a picture of `side` square is not the color of its corner,
/// which is the sky's: what the mesh covers, nought to one.
double covering(const std::vector<std::byte>& pixels, std::uint32_t side) {
    std::size_t covered = 0;
    for (std::size_t at = 0; at < std::size_t{side} * side; ++at) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            if (std::abs(std::to_integer<int>(pixels[(at * 4) + channel]) - std::to_integer<int>(pixels[channel])) >
                8) {
                ++covered;
                break;
            }
        }
    }
    return static_cast<double>(covered) / (static_cast<double>(side) * side);
}

/// `pixels`, RGBA8 sRGB rows of `side`, halved by averaging each two by two.
std::vector<std::byte> halved(const std::vector<std::byte>& pixels, std::uint32_t side) {
    const std::uint32_t kHalf = side / 2;
    std::vector<std::byte> made(std::size_t{kHalf} * kHalf * 4);
    for (std::uint32_t y = 0; y < kHalf; ++y) {
        for (std::uint32_t x = 0; x < kHalf; ++x) {
            for (std::uint32_t channel = 0; channel < 4; ++channel) {
                unsigned sum = 0;
                for (const auto& [kDx, kDy] :
                     {std::pair{0U, 0U}, std::pair{1U, 0U}, std::pair{0U, 1U}, std::pair{1U, 1U}}) {
                    sum += std::to_integer<unsigned>(
                        pixels[((std::size_t{(2 * y) + kDy} * side) + (2 * x) + kDx) * 4 + channel]);
                }
                made[((std::size_t{y} * kHalf) + x) * 4 + channel] = static_cast<std::byte>((sum + 2) / 4);
            }
        }
    }
    return made;
}

/// The most lines a plan holds, and bytes: a plan past them is refused.
constexpr std::size_t kMostLines = 16384;
constexpr std::uintmax_t kMostPlanBytes = std::uintmax_t{4} << 20U;

/// One thing a plan asks drawn.
struct Asked {
    bool mesh = true;
    std::filesystem::path object;
    base::Bits128 id{};
    std::string representation;
};

/// A plan read: what it asks drawn, and the bases and textures its
/// materials read.
struct Plan {
    std::vector<Asked> asked;
    std::map<base::Bits128, std::pair<std::filesystem::path, std::string>> bases;
    std::map<std::uint64_t, std::filesystem::path> textures;
};

std::vector<std::string> fieldsOf(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream read{line};
    std::string field;
    while (std::getline(read, field, '\t')) {
        fields.push_back(field);
    }
    return fields;
}

std::optional<Plan> planOf(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t kSize = std::filesystem::file_size(path, error);
    std::ifstream file{path};
    if (error || kSize > kMostPlanBytes || !file) {
        return std::nullopt;
    }
    Plan plan;
    std::string line;
    std::size_t lines = 0;
    while (std::getline(file, line)) {
        if (++lines > kMostLines) {
            return std::nullopt;
        }
        const std::vector<std::string> kFields = fieldsOf(line);
        if (kFields.empty() || kFields[0].empty()) {
            continue;
        }
        if (kFields[0] == "mesh" && kFields.size() == 2) {
            plan.asked.push_back({.mesh = true, .object = kFields[1]});
        } else if (kFields[0] == "material" && kFields.size() == 4 && base::parseBits128Hex(kFields[2]).parsed) {
            plan.asked.push_back({.mesh = false,
                                  .object = kFields[1],
                                  .id = base::parseBits128Hex(kFields[2]).value,
                                  .representation = kFields[3]});
        } else if (kFields[0] == "base" && kFields.size() == 4 && base::parseBits128Hex(kFields[1]).parsed) {
            plan.bases[base::parseBits128Hex(kFields[1]).value] = {kFields[2], kFields[3]};
        } else if (kFields[0] == "texture" && kFields.size() == 3) {
            std::uint64_t id = 0;
            const auto [kEnd, kError] = std::from_chars(kFields[1].data(), kFields[1].data() + kFields[1].size(), id);
            if (kError != std::errc{} || kEnd != kFields[1].data() + kFields[1].size()) {
                return std::nullopt;
            }
            plan.textures[id] = kFields[2];
        } else {
            return std::nullopt;
        }
    }
    return plan;
}

/// The engine's ball, known to the renderer as its own mesh, of radius
/// one, 3.4 of its radii ahead of the eye: a quarter turn's view holds it
/// whole, with room about it.
render_scene::SceneDraw ball() {
    render_scene::SceneDraw draw{.mesh = render_scene::kSphere, .color = {1, 1, 1, 1}};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        draw.model[(axis * 4) + axis] = 1;
        draw.normal[(axis * 4) + axis] = 1;
    }
    draw.model[14] = -3.4F;
    draw.model[15] = 1;
    draw.previous = draw.model;
    return draw;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fputs("usage: rawframe-thumbnail <out directory> <plan>\n", stderr);
        return 64;
    }
    const std::filesystem::path kOut = argv[1];
    const std::optional<Plan> kPlan = planOf(argv[2]);
    if (!kPlan.has_value()) {
        say("not a plan", argv[2]);
        return 64;
    }
    std::error_code error;
    std::filesystem::create_directories(kOut, error);
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        std::puts("skip: no device");
        return 69;
    }
    auto framer = render::Framer::create(*kDevice);
    auto renderer = render_scene_gpu::SceneRenderer::create(*kDevice);
    const auto kSchema = schema::RegistryBuilder{}.freeze();
    if (!framer.has_value() || !renderer.has_value() || !kSchema.has_value()) {
        say("the scene's renderer could not be made", "");
        return 70;
    }
    auto scene = render_scene::Scene::create(**kSchema, {});
    if (!scene.has_value()) {
        say("the scene could not be made", "");
        return 70;
    }
    // The textures materials sample, read as first asked and kept.
    std::map<std::uint64_t, std::shared_ptr<const texture::Texture>> read;
    const render_scene_gpu::TextureSource kTextures =
        [&read, &kPlan](std::uint64_t id) -> std::shared_ptr<const texture::Texture> {
        if (const auto kHeld = read.find(id); kHeld != read.end()) {
            return kHeld->second;
        }
        const auto kPath = kPlan->textures.find(id);
        const auto kBytes = kPath != kPlan->textures.end() ? readBytes(kPath->second) : std::nullopt;
        auto decoded = texture::decode(kBytes.value_or(std::vector<std::byte>{}));
        auto made = decoded.has_value() ? std::make_shared<const texture::Texture>(std::move(*decoded)) : nullptr;
        read.emplace(id, made);
        return made;
    };
    const render_scene::CookedMaterials kBases =
        [&kPlan](base::Bits128 id) -> result::Result<render_scene::CookedMaterial> {
        const auto kBase = kPlan->bases.find(id);
        const auto kBytes = kBase != kPlan->bases.end() ? readBytes(kBase->second.first) : std::nullopt;
        if (!kBytes.has_value()) {
            return result::fail(result::ErrorClass::NotFound,
                                render_scene::kRenderSceneDomain,
                                code(render_scene::RenderSceneError::MaterialUnreadable),
                                "the plan names no such base");
        }
        return render_scene::CookedMaterial{.representation = kBase->second.second, .bytes = *kBytes};
    };
    render_scene::Programs programs;
    int failed = 0;
    std::uint64_t meshId = 0;
    for (const Asked& kAsked : kPlan->asked) {
        const auto kBytes = readBytes(kAsked.object);
        render_scene::SceneFrame frame =
            (*scene)->queue({.fovY = std::numbers::pi_v<float> / 4, .near = 0.05F, .exposure = 15, .aspect = 1});
        frame.temporal.enabled = false;
        std::shared_ptr<const mesh::Mesh> shown;
        bool program = false;
        if (kAsked.mesh) {
            auto decoded = mesh::decode(kBytes.value_or(std::vector<std::byte>{}));
            if (!decoded.has_value() || decoded->positions.empty()) {
                say("not a cooked mesh", kAsked.object.string());
                ++failed;
                continue;
            }
            shown = std::make_shared<const mesh::Mesh>(std::move(*decoded));
            // Each mesh its own identity: the renderer keeps a mesh it was
            // given by its identity, and would draw the first again.
            frame.draws.push_back(placed(*shown, ++meshId));
        } else {
            auto made = render_scene::sceneMaterialOf(
                1,
                kAsked.id,
                render_scene::CookedMaterial{.representation = kAsked.representation,
                                             .bytes = kBytes.value_or(std::vector<std::byte>{})},
                material::Quality::High,
                programs,
                kBases);
            if (!made.has_value()) {
                say("not a cooked surface material",
                    kAsked.object.string() + ": " + std::string{made.error().description()});
                ++failed;
                continue;
            }
            shown = render_scene::engineMesh(render_scene::kSphere);
            render_scene::SceneDraw draw = ball();
            frame.materials.push_back(made->blob);
            frame.textures.resize(frame.materials.size() - 1);
            frame.textures.push_back(made->textures);
            frame.programs.resize(frame.materials.size());
            frame.programs.back() = made->program;
            draw.material = static_cast<std::uint32_t>(frame.materials.size() - 1);
            frame.translucent = made->translucent ? 1 : 0;
            frame.draws.push_back(draw);
            program = made->program != nullptr;
        }
        const std::uint64_t kMeshId = frame.draws.back().mesh;
        const render_scene_gpu::MeshSource kMeshes = [shown, kMeshId](std::uint64_t id) {
            return id == kMeshId ? shown : nullptr;
        };
        // Drawn once its pipelines are made, a material's own program's
        // too, a few frames at most, then twice more for its textures.
        const std::array<render::FrameRecorder*, 1> kRecorders = {renderer->get()};
        const render_scene_gpu::RendererStatistics kBefore = (*renderer)->statistics();
        const auto kWaiting = [&renderer, &kBefore, program] {
            const render_scene_gpu::RendererStatistics& kNow = (*renderer)->statistics();
            return kNow.frames == kBefore.frames || (program && kNow.programModelsDrawn == kBefore.programModelsDrawn &&
                                                     kNow.programsRefused == kBefore.programsRefused);
        };
        bool drawn = true;
        int after = 0;
        for (int attempt = 0; attempt < 1000 && drawn && (kWaiting() || after++ < 2); ++attempt) {
            (*renderer)->prepare(&frame, kMeshes, kTextures);
            drawn = (*framer)->finish(kFrameWait).has_value() &&
                    (*framer)->make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).has_value();
        }
        std::optional<std::vector<std::byte>> pixels =
            drawn && (*framer)->finish(kFrameWait).has_value() ? (*framer)->pixels() : std::nullopt;
        if (!pixels.has_value()) {
            say("it could not be drawn", kAsked.object.string());
            ++failed;
            continue;
        }
        texture::Texture picture{.format = texture::Format::Rgba8Srgb};
        std::uint32_t side = kSide;
        picture.levels.push_back(texture::Level{.width = side, .height = side, .bytes = *pixels});
        while (side > kSmallest) {
            picture.levels.push_back(texture::Level{
                .width = side / 2, .height = side / 2, .bytes = halved(picture.levels.back().bytes, side)});
            side /= 2;
        }
        const auto kEncoded = texture::encode(picture);
        const std::filesystem::path kWritten = kOut / (kAsked.object.filename().string() + ".texture");
        if (!kEncoded.has_value() || !base::replaceFile(kWritten, *kEncoded)) {
            say("the thumbnail could not be written", kWritten.string());
            ++failed;
            continue;
        }
        std::printf("thumbnail %s %s covering %.2f\n",
                    kAsked.object.string().c_str(),
                    kWritten.string().c_str(),
                    covering(picture.levels.front().bytes, kSide));
    }
    return failed == 0 ? 0 : 1;
}
