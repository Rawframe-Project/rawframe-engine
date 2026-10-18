// The thumbnail tool (SPEC-0051's asset browser, D899): a cooked mesh drawn
// offscreen by the scene's renderer, its picture kept as a texture of the
// engine's with its smaller levels, which the authoring session hands an
// asset browser as it hands a texture's. Run by an authoring session, or
// by anyone; never part of a client or a server.
//
//   rawframe-thumbnail <out directory> <cooked mesh object>...
//
// Each mesh is drawn grey under the engine's default light, turned to show
// its top and a side, filling the picture, and written as `<out>/<object's
// file name>.texture`, 256 pixels square and each half below to 16; how
// much of the picture it covers is said. Exits 69, saying "skip: no
// device", on a machine with no adapter; 1 if any mesh was not drawn.

#include "rawframe/base/files.h"
#include "rawframe/mesh/mesh.h"
#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/schema/registry.h"
#include "rawframe/texture/texture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
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

/// `mesh`, known to the renderer as `id`, turned a little toward the eye from above and its side, scaled so
/// its bounding ball's radius is one, and three of those ahead of the eye,
/// where the ball just fits the view's quarter turn.
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fputs("usage: rawframe-thumbnail <out directory> <cooked mesh object>...\n", stderr);
        return 64;
    }
    const std::filesystem::path kOut = argv[1];
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
    int failed = 0;
    for (int at = 2; at < argc; ++at) {
        const std::filesystem::path kObject = argv[at];
        const auto kBytes = readBytes(kObject);
        auto decoded = mesh::decode(kBytes.value_or(std::vector<std::byte>{}));
        if (!decoded.has_value() || decoded->positions.empty()) {
            say("not a cooked mesh", kObject.string());
            ++failed;
            continue;
        }
        const auto kMesh = std::make_shared<const mesh::Mesh>(std::move(*decoded));
        render_scene::SceneFrame frame =
            (*scene)->queue({.fovY = std::numbers::pi_v<float> / 4, .near = 0.05F, .exposure = 15, .aspect = 1});
        frame.temporal.enabled = false;
        // Each mesh its own identity: the renderer keeps a mesh it was
        // given by its identity, and would draw the first again.
        const auto kId = static_cast<std::uint64_t>(at);
        frame.draws.push_back(placed(*kMesh, kId));
        const render_scene_gpu::MeshSource kMeshes = [kMesh, kId](std::uint64_t id) {
            return id == kId ? kMesh : nullptr;
        };
        // Drawn once its pipelines are made, a few frames at most.
        const std::array<render::FrameRecorder*, 1> kRecorders = {renderer->get()};
        const std::uint64_t kBefore = (*renderer)->statistics().frames;
        bool drawn = true;
        for (int attempt = 0; attempt < 1000 && (*renderer)->statistics().frames == kBefore && drawn; ++attempt) {
            (*renderer)->prepare(&frame, kMeshes, {});
            drawn = (*framer)->finish(kFrameWait).has_value() &&
                    (*framer)->make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).has_value();
        }
        std::optional<std::vector<std::byte>> pixels =
            drawn && (*framer)->finish(kFrameWait).has_value() ? (*framer)->pixels() : std::nullopt;
        if (!pixels.has_value()) {
            say("the mesh could not be drawn", kObject.string());
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
        const std::filesystem::path kWritten = kOut / (kObject.filename().string() + ".texture");
        if (!kEncoded.has_value() || !base::replaceFile(kWritten, *kEncoded)) {
            say("the thumbnail could not be written", kWritten.string());
            ++failed;
            continue;
        }
        std::printf("thumbnail %s %s covering %.2f\n",
                    kObject.string().c_str(),
                    kWritten.string().c_str(),
                    covering(picture.levels.front().bytes, kSide));
    }
    return failed == 0 ? 0 : 1;
}
