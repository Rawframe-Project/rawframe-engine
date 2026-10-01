#include "rawframe/render_canvas_gpu/ui.h"

#include "generated/ui_container.h"
#include "rawframe/render/errors.h"
#include "rawframe/render/textures.h"

#include <array>
#include <cstring>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/pipeline.h>
#include <maul-rhi/resources.h>
#include <maul-rhi/shader.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::render_canvas_gpu {

namespace {

std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, render::kRenderDomain, code(render::RenderError::Device), why)
            .error()
            .withContext("outcome", std::string{mrhiResultName(outcome)})};
}

mrhiResourceId resourceOf(std::uint64_t key) noexcept {
    return mrhiResourceId{.index1 = static_cast<std::uint32_t>(key >> 32U),
                          .generation = static_cast<std::uint32_t>(key)};
}

/// A resource's access, every level of a texture's.
mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

mrhiBinding bufferAt(std::uint32_t slot, mrhiResourceId resource, std::uint64_t size) noexcept {
    return mrhiBinding{.slot = slot,
                       .resource = resource,
                       .offset = 0,
                       .size = size,
                       .viewKind = mrhi_texture2d,
                       .viewFormat = mrhi_formatNone,
                       .range = {},
                       .sampler = {}};
}

/// A box as the shader reads it (ui.vert): nine vectors of four; a clip
/// (ui.frag): three; an image (ui.image.vert): five.
constexpr std::size_t kBoxFloats = 36;
constexpr std::size_t kClipFloats = 12;
constexpr std::size_t kImageFloats = 20;
/// The UI's share of a frame's uploads, for its images.
constexpr std::uint64_t kImageUploadBytes = render::kFrameUploadBytes / 8;

/// A texture's binding, its every level.
mrhiBinding textureAt(std::uint32_t slot, mrhiResourceId resource) noexcept {
    return mrhiBinding{
        .slot = slot,
        .resource = resource,
        .offset = 0,
        .size = 0,
        .viewKind = mrhi_texture2d,
        .viewFormat = mrhi_formatNone,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}},
        .sampler = {}};
}

mrhiBinding samplerAt(std::uint32_t slot, mrhiSamplerId sampler) noexcept {
    return mrhiBinding{.slot = slot,
                       .resource = {},
                       .offset = 0,
                       .size = 0,
                       .viewKind = mrhi_texture2d,
                       .viewFormat = mrhi_formatNone,
                       .range = {},
                       .sampler = sampler};
}

/// `box`, in the shader's layout and the picture's pixels.
std::array<float, kBoxFloats> blockOf(const ui::Box& box, float scale) noexcept {
    std::array<float, kBoxFloats> block{};
    std::size_t at = 0;
    const auto kPut = [&](const std::array<float, 4>& values, float by) {
        for (const float kValue : values) {
            block[at++] = kValue * by;
        }
    };
    kPut({box.rect.x, box.rect.y, box.rect.width, box.rect.height}, scale);
    kPut(box.radii, scale);
    kPut(box.fill, 1);
    kPut(box.borderWidths, scale);
    for (const std::array<float, 4>& kColor : box.borderColors) {
        kPut(kColor, 1);
    }
    kPut({static_cast<float>(box.clip), 0, 0, 0}, 1);
    return block;
}

/// `image`, in the shader's layout and the picture's pixels, its texture
/// `width` by `height`.
std::array<float, kImageFloats>
imageBlockOf(const ui::Image& image, float scale, std::uint32_t width, std::uint32_t height) noexcept {
    return {image.rect.x * scale,
            image.rect.y * scale,
            image.rect.width * scale,
            image.rect.height * scale,
            image.uv.x,
            image.uv.y,
            image.uv.width,
            image.uv.height,
            image.slice[0],
            image.slice[1],
            image.slice[2],
            image.slice[3],
            image.tint[0],
            image.tint[1],
            image.tint[2],
            image.tint[3],
            static_cast<float>(image.clip),
            static_cast<float>(width),
            static_cast<float>(height),
            scale};
}

/// `list`'s clips in the shader's layout and the picture's pixels, the
/// first the placeholder for none; a parent that is not before its child
/// is taken as none, so every chain ends.
std::vector<float> clipsOf(const ui::DrawList& list) {
    std::vector<float> clips(kClipFloats, 0);
    for (std::size_t at = 1; at < list.clips.size(); ++at) {
        const ui::Clip& kClip = list.clips[at];
        const float kScale = list.scale;
        const std::uint32_t kParent = kClip.parent < at ? kClip.parent : 0;
        const std::array<float, kClipFloats> kBlock = {kClip.rect.x * kScale,
                                                       kClip.rect.y * kScale,
                                                       kClip.rect.width * kScale,
                                                       kClip.rect.height * kScale,
                                                       kClip.radii[0] * kScale,
                                                       kClip.radii[1] * kScale,
                                                       kClip.radii[2] * kScale,
                                                       kClip.radii[3] * kScale,
                                                       static_cast<float>(kParent),
                                                       kClip.invert ? 1.0F : 0.0F,
                                                       0,
                                                       0};
        clips.insert(clips.end(), kBlock.begin(), kBlock.end());
    }
    return clips;
}

} // namespace

struct UiRenderer::State {
    render::Device* device = nullptr;
    mrhiDevice* native = nullptr;
    mrhiShaderId shader{};
    /// Boxes, then images.
    std::array<mrhiGraphicsPipelineId, 2> pipelines{};
    std::array<std::uint64_t, 2> requests{};
    bool ready = false;
    mrhiSamplerId nearest{};
    mrhiSamplerId linear{};
    std::unique_ptr<render::DeviceTextures> held;
    /// What an image whose texture is not held samples: none drawn, but a
    /// binding all the same.
    std::shared_ptr<const texture::Texture> clear;
    UiStatistics statistics;
    const ui::DrawList* list = nullptr;
    ImageSource images;
    /// What the open frame declared, until it is recorded and ends.
    bool declared = false;
    std::vector<float> blocks;
    std::vector<float> clips;
    std::vector<float> imageBlocks;
    /// Each image's held texture this frame, none for one not drawn.
    std::vector<std::uint64_t> imageTextures;
    std::array<float, 4> view{};
    mrhiResourceId viewResource{};
    mrhiResourceId boxesResource{};
    mrhiResourceId clipsResource{};
    mrhiResourceId imagesResource{};
    mrhiPassId upload{};
    mrhiPassId drawing{};

    ~State() {
        if (native == nullptr) {
            return;
        }
        held.reset();
        // Maul RHI retires what a frame still uses once the frame is done.
        for (const mrhiGraphicsPipelineId kPipeline : pipelines) {
            static_cast<void>(mrhiDestroyGraphicsPipeline(native, kPipeline));
        }
        static_cast<void>(mrhiDestroySampler(native, nearest));
        static_cast<void>(mrhiDestroySampler(native, linear));
        static_cast<void>(mrhiDestroyShader(native, shader));
    }

    result::Status make() {
        mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
        shaderDef.bytes = kUiContainer.data();
        shaderDef.byteCount = kUiContainer.size();
        if (const mrhiResult kMade = mrhiCreateShader(native, &shaderDef, &shader); kMade != mrhi_success) {
            return failed("the UI's shader could not be made", kMade);
        }
        constexpr std::array<std::array<std::string_view, 3>, 2> kEntries = {{
            {"rawframe.ui.boxes", "vs", "fs"},
            {"rawframe.ui.images", "imageVs", "imageFs"},
        }};
        for (std::size_t at = 0; at < kEntries.size(); ++at) {
            mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
            def.label = kEntries.at(at)[0].data();
            def.labelLength = kEntries.at(at)[0].size();
            def.shader = shader;
            def.vertexEntry = kEntries.at(at)[1].data();
            def.vertexEntryLength = kEntries.at(at)[1].size();
            def.fragmentEntry = kEntries.at(at)[2].data();
            def.fragmentEntryLength = kEntries.at(at)[2].size();
            def.colorTargetCount = 1;
            // Premultiplied, over what is behind, in linear light.
            def.colorTargets[0].format = mrhi_formatRgba8UnormSrgb;
            def.colorTargets[0].blend = true;
            def.colorTargets[0].color = mrhiBlendComponent{
                .srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendOneMinusSrcAlpha, .operation = mrhi_blendAdd};
            def.colorTargets[0].alpha = def.colorTargets[0].color;
            mrhiRequestId asked{};
            if (const mrhiResult kMade = mrhiCreateGraphicsPipeline(native, &def, &pipelines.at(at), &asked);
                kMade != mrhi_success) {
                return failed("a UI pipeline could not be asked for", kMade);
            }
            requests.at(at) = render::requestKey(asked.index1, asked.generation);
        }
        // Exact images (pixel art) sampled nearest, compressed ones
        // smoothly, as the canvas samples its sprites.
        for (auto [filter, sampler] :
             {std::pair{mrhi_filterNearest, &nearest}, std::pair{mrhi_filterLinear, &linear}}) {
            mrhiSamplerDef samplerDef = mrhiDefaultSamplerDef();
            samplerDef.magFilter = filter;
            samplerDef.minFilter = filter;
            samplerDef.mipFilter = filter;
            samplerDef.addressU = mrhi_addressClampToEdge;
            samplerDef.addressV = mrhi_addressClampToEdge;
            samplerDef.addressW = mrhi_addressClampToEdge;
            if (const mrhiResult kMade = mrhiCreateSampler(native, &samplerDef, sampler); kMade != mrhi_success) {
                return failed("the UI's sampler could not be made", kMade);
            }
        }
        RAWFRAME_TRY_ASSIGN(held, render::DeviceTextures::create(*device));
        texture::Texture clearTexture{.format = texture::Format::Rgba8Srgb};
        clearTexture.levels.push_back(
            texture::Level{.width = 1, .height = 1, .bytes = std::vector<std::byte>(4, std::byte{0})});
        clear = std::make_shared<const texture::Texture>(std::move(clearTexture));
        return {};
    }

    /// Chooses the images this frame draws: each held, or none.
    void choose() {
        held->begin(kImageUploadBytes);
        static_cast<void>(held->choose(0, clear));
        imageBlocks.clear();
        imageTextures.clear();
        for (const ui::Image& kImage : list->images) {
            const std::shared_ptr<const texture::Texture> kTexture =
                kImage.image != 0 && images ? images(kImage.image) : nullptr;
            const bool kHeld = kTexture != nullptr && !kTexture->levels.empty() && held->choose(kImage.image, kTexture);
            imageTextures.push_back(kHeld ? kImage.image : 0);
            const std::array<float, kImageFloats> kBlock = imageBlockOf(
                kImage, list->scale, kHeld ? kTexture->levels[0].width : 1, kHeld ? kTexture->levels[0].height : 1);
            imageBlocks.insert(imageBlocks.end(), kBlock.begin(), kBlock.end());
            if (kHeld) {
                ++statistics.images;
            } else {
                ++statistics.imagesWaiting;
            }
        }
    }

    result::Status declare(render::Frame& open) {
        declared = false;
        if (list == nullptr || list->commands.empty()) {
            return {};
        }
        if (!ready) {
            device->pump();
            bool all = true;
            for (const std::uint64_t kRequest : requests) {
                if (const auto kAnswer = device->answer(kRequest)) {
                    if (!kAnswer->has_value()) {
                        return std::unexpected<result::Error>{kAnswer->error().clone()};
                    }
                } else {
                    all = false;
                }
            }
            ready = all;
        }
        if (!ready) {
            return {};
        }
        blocks.clear();
        for (const ui::Box& kBox : list->boxes) {
            ui::Box box = kBox;
            box.clip = kBox.clip < list->clips.size() ? kBox.clip : 0;
            const std::array<float, kBoxFloats> kBlock = blockOf(box, list->scale);
            blocks.insert(blocks.end(), kBlock.begin(), kBlock.end());
        }
        clips = clipsOf(*list);
        choose();
        RAWFRAME_TRY(held->import());
        // A buffer bound holds one block at least.
        if (blocks.empty()) {
            blocks.assign(kBoxFloats, 0);
        }
        if (imageBlocks.empty()) {
            imageBlocks.assign(kImageFloats, 0);
        }
        view = {static_cast<float>(open.width), static_cast<float>(open.height), 0, 0};
        mrhiBufferDef viewDef = mrhiDefaultBufferDef();
        viewDef.size = sizeof(view);
        mrhiBufferDef boxesDef = mrhiDefaultBufferDef();
        boxesDef.size = blocks.size() * sizeof(float);
        mrhiBufferDef clipsDef = mrhiDefaultBufferDef();
        clipsDef.size = clips.size() * sizeof(float);
        mrhiBufferDef imagesDef = mrhiDefaultBufferDef();
        imagesDef.size = imageBlocks.size() * sizeof(float);
        if (mrhiDeclareBuffer(native, &viewDef, &viewResource) != mrhi_success ||
            mrhiDeclareBuffer(native, &boxesDef, &boxesResource) != mrhi_success ||
            mrhiDeclareBuffer(native, &clipsDef, &clipsResource) != mrhi_success ||
            mrhiDeclareBuffer(native, &imagesDef, &imagesResource) != mrhi_success) {
            return failed("the UI's boxes could not be declared", mrhi_errorCapacity);
        }
        std::vector<mrhiAccess> writes = {wholeOf(viewResource, mrhi_accessCopyDestination),
                                          wholeOf(boxesResource, mrhi_accessCopyDestination),
                                          wholeOf(clipsResource, mrhi_accessCopyDestination),
                                          wholeOf(imagesResource, mrhi_accessCopyDestination)};
        for (const std::uint64_t kTexture : held->uploading()) {
            writes.push_back(wholeOf(resourceOf(kTexture), mrhi_accessCopyDestination));
        }
        mrhiPassDef uploadDef = mrhiDefaultPassDef();
        uploadDef.passClass = mrhi_passTransfer;
        uploadDef.accesses = writes.data();
        uploadDef.accessCount = static_cast<std::uint32_t>(writes.size());
        if (const mrhiResult kAdded = mrhiAddPass(native, &uploadDef, &upload); kAdded != mrhi_success) {
            return failed("the UI's upload could not be added", kAdded);
        }
        std::vector<mrhiAccess> reads = {wholeOf(viewResource, mrhi_accessUniform),
                                         wholeOf(boxesResource, mrhi_accessStorageRead),
                                         wholeOf(clipsResource, mrhi_accessStorageRead),
                                         wholeOf(imagesResource, mrhi_accessStorageRead)};
        for (const std::uint64_t kTexture : held->chosen()) {
            reads.push_back(wholeOf(resourceOf(kTexture), mrhi_accessSampled));
        }
        mrhiPassDef drawDef = mrhiDefaultPassDef();
        drawDef.colorTargets[0].resource = resourceOf(open.picture);
        drawDef.colorTargets[0].load = open.clearsPicture() ? mrhi_loadClear : mrhi_loadKeep;
        drawDef.colorTargets[0].store = mrhi_storeKeep;
        drawDef.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
        drawDef.colorTargetCount = 1;
        drawDef.accesses = reads.data();
        drawDef.accessCount = static_cast<std::uint32_t>(reads.size());
        drawDef.neverCull = true;
        if (const mrhiResult kAdded = mrhiAddPass(native, &drawDef, &drawing); kAdded != mrhi_success) {
            return failed("the UI's drawing could not be added", kAdded);
        }
        declared = true;
        return {};
    }

    /// The bindings of a draw sampling `texture` (the clear one for none).
    [[nodiscard]] std::array<mrhiBinding, 6> bindings(std::uint64_t texture) const noexcept {
        const std::uint64_t kHeld = held->resource(texture);
        return {bufferAt(0, viewResource, sizeof(view)),
                bufferAt(1, boxesResource, blocks.size() * sizeof(float)),
                bufferAt(2, clipsResource, clips.size() * sizeof(float)),
                bufferAt(3, imagesResource, imageBlocks.size() * sizeof(float)),
                textureAt(4, resourceOf(kHeld != 0 ? kHeld : held->resource(0))),
                samplerAt(5, texture != 0 && held->compressed(texture) ? linear : nearest)};
    }

    result::Status record() {
        if (!declared) {
            return {};
        }
        if (mrhiBeginPass(native, upload) != mrhi_success ||
            mrhiWriteBuffer(native, upload, viewResource, 0, view.data(), sizeof(view)) != mrhi_success ||
            mrhiWriteBuffer(native, upload, boxesResource, 0, blocks.data(), blocks.size() * sizeof(float)) !=
                mrhi_success ||
            mrhiWriteBuffer(native, upload, clipsResource, 0, clips.data(), clips.size() * sizeof(float)) !=
                mrhi_success ||
            mrhiWriteBuffer(
                native, upload, imagesResource, 0, imageBlocks.data(), imageBlocks.size() * sizeof(float)) !=
                mrhi_success) {
            return failed("the UI's boxes could not be written", mrhi_errorCapacity);
        }
        RAWFRAME_TRY(held->write(render::requestKey(upload.index1, upload.generation)));
        if (mrhiEndPass(native, upload) != mrhi_success) {
            return failed("the UI's boxes could not be written", mrhi_errorState);
        }
        if (mrhiBeginPass(native, drawing) != mrhi_success) {
            return failed("the UI could not be drawn", mrhi_errorState);
        }
        // In paint order: each run of boxes one instanced draw, each image
        // drawn alone with its texture.
        const std::vector<ui::DrawCommand>& kCommands = list->commands;
        std::uint64_t boxes = 0;
        for (std::size_t at = 0; at < kCommands.size();) {
            const ui::DrawCommand& kCommand = kCommands[at];
            if (kCommand.kind == ui::DrawCommand::Kind::Box) {
                std::size_t end = at + 1;
                while (end < kCommands.size() && kCommands[end].kind == ui::DrawCommand::Kind::Box &&
                       kCommands[end].index == kCommands[end - 1].index + 1) {
                    ++end;
                }
                const auto kCount = static_cast<std::uint32_t>(end - at);
                const std::array<mrhiBinding, 6> kBindings = bindings(0);
                if (mrhiSetGraphicsPipeline(native, drawing, pipelines[0]) != mrhi_success ||
                    mrhiSetBindings(native, drawing, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
                    mrhiDraw(native, drawing, 6, kCount, 0, kCommand.index) != mrhi_success) {
                    return failed("the UI's boxes could not be drawn", mrhi_errorState);
                }
                boxes += kCount;
                at = end;
                continue;
            }
            const std::uint64_t kTexture = imageTextures.at(kCommand.index);
            if (kTexture != 0) {
                const std::array<mrhiBinding, 6> kBindings = bindings(kTexture);
                if (mrhiSetGraphicsPipeline(native, drawing, pipelines[1]) != mrhi_success ||
                    mrhiSetBindings(native, drawing, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
                    mrhiDraw(native, drawing, 6, 1, 0, kCommand.index) != mrhi_success) {
                    return failed("a UI image could not be drawn", mrhi_errorState);
                }
            }
            ++at;
        }
        if (mrhiEndPass(native, drawing) != mrhi_success) {
            return failed("the UI could not be drawn", mrhi_errorState);
        }
        ++statistics.frames;
        statistics.boxes += boxes;
        return {};
    }
};

UiRenderer::UiRenderer(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

UiRenderer::~UiRenderer() = default;

result::Result<std::unique_ptr<UiRenderer>> UiRenderer::create(render::Device& device) {
    auto state = std::make_unique<State>();
    state->device = &device;
    state->native = device.native();
    RAWFRAME_TRY(state->make());
    return std::unique_ptr<UiRenderer>{new UiRenderer{std::move(state)}};
}

void UiRenderer::prepare(const ui::DrawList* list, ImageSource images) noexcept {
    state_->list = list;
    state_->images = std::move(images);
}

result::Status UiRenderer::declare(render::Frame& frame) {
    return state_->declare(frame);
}

result::Status UiRenderer::record(render::Frame& /*frame*/) {
    return state_->record();
}

void UiRenderer::ended(bool submitted) noexcept {
    if (state_->declared) {
        state_->held->ended(submitted);
    }
    state_->declared = false;
}

const UiStatistics& UiRenderer::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::render_canvas_gpu
