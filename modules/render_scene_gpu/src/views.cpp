#include "pipelines.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "tables.h"

#include <algorithm>
#include <array>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/resources.h>
#include <utility>

namespace rawframe::render_scene_gpu {

struct TextureView::State {
    mrhiDevice* native = nullptr;
    std::uint64_t id = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    mrhiTextureId texture{};
    std::unique_ptr<SceneRenderer> renderer;
    /// What the next frame draws; the open frame's picture, and the frame
    /// its renderer drew into; and whether a submitted frame drew it.
    const render_scene::SceneFrame* frame = nullptr;
    std::optional<std::uint64_t> picture;
    render::Frame inner;
    bool drawing = false;
    bool drawn = false;
    /// Whether a submitted frame drew the frame it was given, and whether
    /// the one before went undrawn.
    bool shown = false;
    bool missed = false;
    /// Where the frame's picture takes it, and the passes clearing the
    /// frame's picture and copying it there.
    std::optional<Placement> placed;
    std::optional<mrhiPassId> clearing;
    std::optional<mrhiPassId> copying;
    mrhiResourceId imported{};
    std::uint32_t copyWidth = 0;
    std::uint32_t copyHeight = 0;

    /// Its picture made, `width` by `height`.
    result::Status make() {
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format = mrhi_formatRgba8UnormSrgb;
        def.width = width;
        def.height = height;
        def.usage =
            mrhi_textureSampled | mrhi_textureRenderTarget | mrhi_textureCopySource | mrhi_textureCopyDestination;
        if (const mrhiResult kMade = mrhiCreateTexture(native, &def, &texture); kMade != mrhi_success) {
            return failed("a view's picture could not be made", kMade);
        }
        return {};
    }

    ~State() {
        if (native != nullptr && texture.index1 != 0) {
            // Maul RHI retires what a frame still uses once the frame is done.
            static_cast<void>(mrhiDestroyTexture(native, texture));
        }
    }
};

TextureView::TextureView(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

TextureView::~TextureView() = default;

result::Result<std::unique_ptr<TextureView>> TextureView::create(render::Device& device,
                                                                 const SceneRenderer& sharing,
                                                                 std::uint64_t id,
                                                                 std::uint32_t width,
                                                                 std::uint32_t height,
                                                                 RendererLimits limits) {
    auto state = std::make_unique<State>();
    state->native = device.native();
    state->id = id;
    state->width = width;
    state->height = height;
    RAWFRAME_TRY_ASSIGN(state->renderer, SceneRenderer::create(device, sharing, limits));
    RAWFRAME_TRY(state->make());
    state->inner.device = &device;
    return std::unique_ptr<TextureView>{new TextureView{std::move(state)}};
}

void TextureView::prepare(const render_scene::SceneFrame* frame,
                          MeshSource meshes,
                          TextureSource textures,
                          std::span<TextureView* const> lent,
                          std::optional<Placement> placed) {
    state_->missed = state_->frame != nullptr && !state_->shown;
    state_->shown = false;
    state_->frame = frame;
    state_->placed = placed;
    state_->renderer->prepare(frame, std::move(meshes), std::move(textures), lent);
}

result::Status TextureView::resize(std::uint32_t width, std::uint32_t height) {
    State& state = *state_;
    if (width == state.width && height == state.height) {
        return {};
    }
    // Maul RHI retires what a frame still uses once the frame is done.
    static_cast<void>(mrhiDestroyTexture(state.native, state.texture));
    state.texture = {};
    state.width = width;
    state.height = height;
    state.drawn = false;
    return state.make();
}

std::uint32_t TextureView::width() const noexcept {
    return state_->width;
}

std::uint32_t TextureView::height() const noexcept {
    return state_->height;
}

result::Status TextureView::declare(render::Frame& frame) {
    State& state = *state_;
    state.picture.reset();
    state.drawing = false;
    state.clearing.reset();
    state.copying.reset();
    // Placed, the frame's picture cleared first if nothing drew into it
    // before: a copy keeps what is there.
    if (state.placed.has_value() && frame.clearsPicture()) {
        mrhiPassDef def = mrhiDefaultPassDef();
        def.colorTargets[0].resource = resourceOf(frame.picture);
        def.colorTargets[0].load = mrhi_loadClear;
        def.colorTargets[0].store = mrhi_storeKeep;
        def.colorTargets[0].clear = mrhiClearColor{.red = render::linearOf(state.placed->bars[0]),
                                                   .green = render::linearOf(state.placed->bars[1]),
                                                   .blue = render::linearOf(state.placed->bars[2]),
                                                   .alpha = 1};
        def.colorTargetCount = 1;
        def.neverCull = true;
        mrhiPassId made{};
        if (const mrhiResult kAdded = mrhiAddPass(state.native, &def, &made); kAdded != mrhi_success) {
            return failed("the frame's picture could not be cleared", kAdded);
        }
        state.clearing = made;
    }
    if (state.frame == nullptr && !state.drawn) {
        return {};
    }
    if (const mrhiResult kImported = mrhiImportTexture(state.native, state.texture, &state.imported);
        kImported != mrhi_success) {
        return failed("a view's picture could not join the frame", kImported);
    }
    const std::uint64_t kPicture = render::requestKey(state.imported.index1, state.imported.generation);
    // Its view drawn into it as into a frame's picture of its size, cleared
    // first.
    if (state.frame != nullptr) {
        state.inner =
            render::Frame{.device = frame.device, .width = state.width, .height = state.height, .picture = kPicture};
        RAWFRAME_TRY(state.renderer->declare(state.inner));
        RAWFRAME_TRY(state.renderer->composed().declare(state.inner));
        state.drawing = state.inner.drawn;
    }
    // Lent only once drawn: before, its texels are none of the view's.
    if (state.drawn || state.drawing) {
        state.picture = kPicture;
    }
    // Placed, copied into the frame's picture once drawn.
    if (state.placed.has_value() && state.picture.has_value() && state.placed->x < frame.width &&
        state.placed->y < frame.height) {
        const std::array<mrhiAccess, 2> kAccesses = {
            mrhiAccess{.resource = state.imported,
                       .kind = mrhi_accessCopySource,
                       .range = {.baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1, .aspect = {}}},
            mrhiAccess{.resource = resourceOf(frame.picture),
                       .kind = mrhi_accessCopyDestination,
                       .range = {.baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1, .aspect = {}}}};
        mrhiPassDef def = mrhiDefaultPassDef();
        def.passClass = mrhi_passTransfer;
        def.accesses = kAccesses.data();
        def.accessCount = static_cast<std::uint32_t>(kAccesses.size());
        def.neverCull = true;
        mrhiPassId made{};
        if (const mrhiResult kAdded = mrhiAddPass(state.native, &def, &made); kAdded != mrhi_success) {
            return failed("a view's picture could not be placed", kAdded);
        }
        state.copying = made;
        state.copyWidth = std::min(state.width, frame.width - state.placed->x);
        state.copyHeight = std::min(state.height, frame.height - state.placed->y);
    }
    return {};
}

result::Status TextureView::record(render::Frame& frame) {
    State& state = *state_;
    if (state.clearing.has_value() && (mrhiBeginPass(state.native, *state.clearing) != mrhi_success ||
                                       mrhiEndPass(state.native, *state.clearing) != mrhi_success)) {
        return failed("the frame's picture could not be cleared", mrhi_errorState);
    }
    if (state.frame != nullptr) {
        RAWFRAME_TRY(state.renderer->record(state.inner));
        RAWFRAME_TRY(state.renderer->composed().record(state.inner));
    }
    if (state.copying.has_value()) {
        const mrhiTextureCopy kFrom{.resource = state.imported};
        const mrhiTextureCopy kTo{.resource = resourceOf(frame.picture), .x = state.placed->x, .y = state.placed->y};
        const mrhiExtent3d kExtent{.width = state.copyWidth, .height = state.copyHeight, .depthOrLayers = 1};
        if (mrhiBeginPass(state.native, *state.copying) != mrhi_success ||
            mrhiCopyTexture(state.native, *state.copying, &kFrom, &kTo, &kExtent) != mrhi_success ||
            mrhiEndPass(state.native, *state.copying) != mrhi_success) {
            return failed("a view's picture could not be placed", mrhi_errorState);
        }
    }
    return {};
}

void TextureView::ended(bool submitted) noexcept {
    State& state = *state_;
    state.renderer->ended(submitted);
    state.renderer->composed().ended(submitted);
    state.shown = state.shown || (submitted && state.drawing);
    state.drawn = state.drawn || state.shown;
    state.drawing = false;
}

bool TextureView::missed() const noexcept {
    return state_->missed;
}

std::uint64_t TextureView::id() const noexcept {
    return state_->id;
}

std::optional<std::uint64_t> TextureView::picture() const noexcept {
    return state_->picture;
}

const RendererStatistics& TextureView::statistics() const noexcept {
    return state_->renderer->statistics();
}

} // namespace rawframe::render_scene_gpu
