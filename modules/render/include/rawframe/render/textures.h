#pragma once

// Textures held on the device (SPEC-0024, D307): each made once from the
// decoded texture a drawer names, and made again when a reload gives it a
// new one; its levels uploaded within the budget the drawer gives the
// frame, and a texture that would pass it left for a later frame. The canvas's sprites
// and the scene's materials each hold theirs in one of these. Ids of the
// open frame are named as `requestKey` names them.

#include "rawframe/render/device.h"
#include "rawframe/result/result.h"
#include "rawframe/texture/texture.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace rawframe::render {

/// SPEC-0024's limit point on what a drawer holds on the device: the
/// textures held at once; one chosen past them is not drawn.
struct TextureLimits {
    std::size_t maximumTextures = 256;
};

struct TextureStatistics {
    std::uint64_t texturesUploaded = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t uploadsDeferred = 0;
    /// Textures made again for a reload's new revision.
    std::uint64_t texturesReplaced = 0;
};

class DeviceTextures {
public:
    /// Holds textures on `device`, which must be ready and must outlive
    /// this.
    [[nodiscard]] static result::Result<std::unique_ptr<DeviceTextures>> create(Device& device,
                                                                                TextureLimits limits = {});

    DeviceTextures(const DeviceTextures&) = delete;
    DeviceTextures& operator=(const DeviceTextures&) = delete;
    ~DeviceTextures();

    /// Starts choosing what the frame being declared draws from, within
    /// `budget` bytes of uploads (the drawer's share of SPEC-0024's
    /// `upload_bytes_per_frame`). A texture larger than every budget is
    /// never drawn.
    void begin(std::uint64_t budget) noexcept;
    /// Chooses the texture the drawer names `id`, decoded as `image`: made
    /// on the device if it is not held, or held from another image. Whether
    /// the frame can draw from it: not for no image, a format the device
    /// does not take, one past the textures held, or one deferred.
    bool choose(std::uint64_t id, const std::shared_ptr<const texture::Texture>& image);
    /// Brings every chosen texture into the open frame.
    [[nodiscard]] result::Status import();

    /// The chosen textures' ids in the open frame, for the passes that
    /// sample them to declare.
    [[nodiscard]] std::vector<std::uint64_t> chosen() const;
    /// Those of them whose levels this frame writes, for the pass `write`
    /// records into to declare as copy destinations.
    [[nodiscard]] std::vector<std::uint64_t> uploading() const;
    /// The chosen texture `id`'s id in the open frame, and whether its
    /// levels are block-compressed; 0 and false for one not chosen.
    [[nodiscard]] std::uint64_t resource(std::uint64_t id) const noexcept;
    [[nodiscard]] bool compressed(std::uint64_t id) const noexcept;
    /// Whether the chosen texture `id` is a cube, bound as one (D320).
    [[nodiscard]] bool cube(std::uint64_t id) const noexcept;

    /// Writes the levels of those uploading, in the open pass `pass`.
    [[nodiscard]] result::Status write(std::uint64_t pass);
    /// The frame ended: what it uploaded is held uploaded only if it was
    /// submitted.
    void ended(bool submitted) noexcept;

    [[nodiscard]] const TextureStatistics& statistics() const noexcept;

    struct State;

private:
    explicit DeviceTextures(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::render
