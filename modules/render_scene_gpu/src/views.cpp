#include "pipelines.h"
#include "rawframe/render_scene_gpu/renderer.h"

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
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8UnormSrgb;
    def.width = width;
    def.height = height;
    def.usage = mrhi_textureSampled | mrhi_textureRenderTarget | mrhi_textureCopySource | mrhi_textureCopyDestination;
    if (const mrhiResult kMade = mrhiCreateTexture(state->native, &def, &state->texture); kMade != mrhi_success) {
        return failed("a render texture could not be made", kMade);
    }
    state->inner.device = &device;
    return std::unique_ptr<TextureView>{new TextureView{std::move(state)}};
}

void TextureView::prepare(const render_scene::SceneFrame* frame, MeshSource meshes, TextureSource textures) {
    state_->missed = state_->frame != nullptr && !state_->shown;
    state_->shown = false;
    state_->frame = frame;
    state_->renderer->prepare(frame, std::move(meshes), std::move(textures));
}

result::Status TextureView::declare(render::Frame& frame) {
    State& state = *state_;
    state.picture.reset();
    state.drawing = false;
    if (state.frame == nullptr && !state.drawn) {
        return {};
    }
    mrhiResourceId imported{};
    if (const mrhiResult kImported = mrhiImportTexture(state.native, state.texture, &imported);
        kImported != mrhi_success) {
        return failed("a render texture could not join the frame", kImported);
    }
    const std::uint64_t kPicture = render::requestKey(imported.index1, imported.generation);
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
    return {};
}

result::Status TextureView::record(render::Frame& /*frame*/) {
    State& state = *state_;
    if (state.frame == nullptr) {
        return {};
    }
    RAWFRAME_TRY(state.renderer->record(state.inner));
    return state.renderer->composed().record(state.inner);
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
