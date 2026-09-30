// A probe's picture baked from six faces (D326): each direction of the
// picture is taken from the face that looks its way, and within a face
// from the pixel the face's own view and projection put it at.

#include "rawframe/render_scene/scene.h"
#include "rawframe/scene_bake/bake.h"
#include "rawframe/test/test.h"

#include <array>
#include <cmath>
#include <vector>

using namespace rawframe;
using namespace rawframe::scene_bake;

namespace {

/// The faces the six cameras see, as the queue stage makes their views,
/// each `side` pixels square and lit by `light(face, column, row)`.
template <typename Light> std::vector<BakedFace> facesLit(std::uint32_t side, Light light) {
    const auto kSchema = *schema::RegistryBuilder{}.freeze();
    auto scene = *render_scene::Scene::create(*kSchema, {});
    std::vector<BakedFace> faces;
    const std::array<render_scene::SceneCamera, 6> kCameras = faceCameras({1000, 2, -3}, 1, 10);
    for (std::size_t face = 0; face < 6; ++face) {
        const render_scene::SceneFrame& kFrame = scene->queue(kCameras[face]);
        BakedFace made{.view = kFrame.view, .projection = kFrame.projection};
        made.light = {.width = side, .height = side};
        for (std::uint32_t row = 0; row < side; ++row) {
            for (std::uint32_t column = 0; column < side; ++column) {
                const std::array<float, 3> kLight = light(face, column, row);
                made.light.light.insert(made.light.light.end(), kLight.begin(), kLight.end());
            }
        }
        faces.push_back(std::move(made));
    }
    return faces;
}

std::array<float, 3> at(const texture_import::LightImage& picture, double u, double v) {
    const auto kX = static_cast<std::size_t>(u * picture.width);
    const auto kY = static_cast<std::size_t>(v * picture.height);
    const std::size_t kAt = ((kY * picture.width) + kX) * 3;
    return {picture.rgb[kAt], picture.rgb[kAt + 1], picture.rgb[kAt + 2]};
}

} // namespace

RAWFRAME_TEST(EachDirectionIsTakenFromTheFaceThatLooksItsWay) {
    // Each face lit by its number, one to six.
    const std::vector<BakedFace> kFaces = facesLit(16, [](std::size_t face, std::uint32_t, std::uint32_t) {
        return std::array<float, 3>{static_cast<float>(face + 1), 0, 0};
    });
    const auto kPicture = pictureOf(kFaces, 64);
    RAWFRAME_EXPECT(kPicture.has_value() && kPicture->width == 64 && kPicture->height == 32 &&
                    kPicture->rgb.size() == std::size_t{64} * 32 * 3);
    if (!kPicture.has_value()) {
        return;
    }
    // +X, -X, +Y, -Y, +Z, -Z, where cookEnvironment reads them.
    RAWFRAME_EXPECT(at(*kPicture, 0.75, 0.5)[0] == 1 && at(*kPicture, 0.25, 0.5)[0] == 2 &&
                    at(*kPicture, 0.4, 0.01)[0] == 3 && at(*kPicture, 0.4, 0.99)[0] == 4 &&
                    at(*kPicture, 0.01, 0.5)[0] == 5 && at(*kPicture, 0.5, 0.5)[0] == 6);
    // None without a face that fills its sides.
    std::vector<BakedFace> empty(1);
    RAWFRAME_EXPECT(!pictureOf(empty, 64).has_value());
}

RAWFRAME_TEST(WithinAFaceEachDirectionIsTakenWhereItsViewPutsIt) {
    // Each face's pixels lit by their column and row.
    const std::vector<BakedFace> kFaces = facesLit(32, [](std::size_t, std::uint32_t column, std::uint32_t row) {
        return std::array<float, 3>{static_cast<float>(column), static_cast<float>(row), 0};
    });
    const auto kPicture = pictureOf(kFaces, 256);
    RAWFRAME_EXPECT(kPicture.has_value());
    if (!kPicture.has_value()) {
        return;
    }
    // Looking along -Z, the middle of the face; rightward, its columns
    // rise, and downward, its rows.
    const std::array<float, 3> kAhead = at(*kPicture, 0.5, 0.5);
    const std::array<float, 3> kRight = at(*kPicture, 0.55, 0.5);
    const std::array<float, 3> kBelow = at(*kPicture, 0.5, 0.6);
    RAWFRAME_EXPECT(std::abs(kAhead[0] - 15.5F) < 1 && std::abs(kAhead[1] - 15.5F) < 1);
    RAWFRAME_EXPECT(kRight[0] > kAhead[0] + 2 && std::abs(kRight[1] - kAhead[1]) < 1);
    RAWFRAME_EXPECT(kBelow[1] > kAhead[1] + 2 && std::abs(kBelow[0] - kAhead[0]) < 1);
}
