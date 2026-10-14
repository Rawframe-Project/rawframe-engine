#include "views.h"

#include "rawframe/graph/graph.h"
#include "rawframe/render_scene/components.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace rawframe::render_scene {

namespace {

constexpr diagnostics::EventIdentity kViewRefused{"scene", "view_refused"};
constexpr diagnostics::EventIdentity kViewsSummary{"scene", "scene_views_summary"};

} // namespace

TextureViews::TextureViews(std::optional<schema::ComponentTypeId> view,
                           std::span<const world_kest::GameRenderTexture> textures)
    : viewComponent_(view) {
    for (const world_kest::GameRenderTexture& kTexture : textures) {
        views_.push_back(TextureView{.id = kTexture.id,
                                     .width = kTexture.width,
                                     .height = kTexture.height,
                                     .onDemand = kTexture.update == world_kest::RenderTextureUpdate::OnDemand});
    }
    frames_.resize(views_.size());
}

bool TextureViews::empty() const noexcept {
    return views_.empty();
}

std::span<const TextureFrame> TextureViews::frames() const noexcept {
    return frames_;
}

void TextureViews::forget() noexcept {
    for (TextureFrame& each : frames_) {
        each.frame = nullptr;
    }
}

void TextureViews::extract(world::World& world,
                           const SceneSettings& settings,
                           double shown,
                           const world_animation::AnimationQueries* poses,
                           const GameCameras& cameras,
                           diagnostics::Emitter& emitter) {
    for (TextureView& each : views_) {
        each.entity.reset();
        each.order = 0;
        each.request = 0;
        each.tied = false;
        each.due = false;
    }
    if (!viewComponent_.has_value() || views_.empty()) {
        return;
    }
    const auto kView = world.registry().find(*viewComponent_);
    if (!kView.has_value()) {
        return;
    }
    if (!viewQuery_.has_value()) {
        const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kView, world::Access::Read}};
        auto query = world::ColumnQuery::resolve(kTerms, world.registry());
        if (!query.has_value()) {
            return;
        }
        viewQuery_ = std::move(*query);
    }
    viewQuery_->forEachChunk(world, [&](const world::ColumnChunk& chunk) {
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            View asked;
            std::memcpy(&asked, chunk.columns[0] + (row * sizeof(View)), sizeof(View));
            const auto kTarget = std::ranges::find(views_, asked.target, &TextureView::id);
            if (kTarget == views_.end()) {
                continue;
            }
            if (kTarget->entity.has_value()) {
                ++leftOut_;
                if (asked.order > kTarget->order) {
                    continue;
                }
                kTarget->tied = asked.order == kTarget->order;
                if (kTarget->tied) {
                    continue;
                }
            }
            kTarget->entity = chunk.entities[row];
            kTarget->order = asked.order;
            kTarget->request = asked.request;
            kTarget->tied = false;
        }
    });
    for (TextureView& each : views_) {
        if (!each.entity.has_value()) {
            continue;
        }
        if (each.tied) {
            ++refused_;
            emitter.log(diagnostics::Severity::Warning,
                        kViewRefused,
                        "two views of one order name a render texture: it shows neither",
                        {diagnostics::field("texture", graph::nodeIdText(each.id)),
                         diagnostics::field("order", static_cast<std::int64_t>(each.order))});
            each.entity.reset();
            continue;
        }
        each.due = !each.onDemand || !each.shown.has_value() || each.shown->entity != *each.entity ||
                   each.shown->request != each.request;
        if (!each.due) {
            ++kept_;
            continue;
        }
        each.resend = false;
        if (each.scene == nullptr) {
            auto made = Scene::create(world.registry(), settings);
            if (!made.has_value()) {
                each.entity.reset();
                continue;
            }
            each.scene = std::move(*made);
        }
        each.scene->shownAt(shown);
        each.scene->extract(world, poses);
        static_cast<void>(cameras.read(world, *each.entity, each.camera));
    }
}

void TextureViews::present(execution::MonotonicInstant now, ScenePictures& pictures) {
    for (std::size_t at = 0; at < views_.size(); ++at) {
        TextureView& each = views_[at];
        frames_[at] = TextureFrame{.id = each.id, .width = each.width, .height = each.height};
        if (!each.entity.has_value() || each.scene == nullptr) {
            each.presented.reset();
            each.shown.reset();
            each.offered = nullptr;
            each.resend = false;
            continue;
        }
        if (!each.due) {
            // A frame the device missed offered again as it was.
            if (each.resend) {
                frames_[at].frame = each.offered;
                each.resend = false;
            }
            continue;
        }
        each.shown = Shown{.entity = *each.entity, .request = each.request};
        each.camera.aspect = static_cast<float>(each.width) / static_cast<float>(each.height);
        each.camera.elapsed =
            each.presented.has_value() ? static_cast<float>((now - *each.presented).nanoseconds) / 1e9F : 0.0F;
        each.presented = now;
        const SceneFrame& kFrame = each.scene->queue(each.camera);
        pictures.ask(kFrame.lights.environment);
        pictures.ask(kFrame.grading.table);
        frames_[at].frame = &kFrame;
        each.offered = &kFrame;
        ++drawn_;
    }
}

void TextureViews::missed(std::uint64_t id) noexcept {
    const auto kView = std::ranges::find(views_, id, &TextureView::id);
    if (kView != views_.end() && kView->offered != nullptr) {
        kView->resend = true;
        ++missed_;
    }
}

void TextureViews::replaceMaterials(const std::vector<SceneMaterial>& materials,
                                    const std::vector<ScenePostProcessMaterial>& postProcesses) {
    for (TextureView& each : views_) {
        if (each.scene != nullptr) {
            each.scene->replaceMaterials(materials, postProcesses);
        }
    }
}

void TextureViews::summarize(diagnostics::Emitter& emitter) const {
    if (views_.empty()) {
        return;
    }
    emitter.log(diagnostics::Severity::Info,
                kViewsSummary,
                "what one client's render textures were drawn with",
                {diagnostics::field("drawn", drawn_),
                 diagnostics::field("kept", kept_),
                 diagnostics::field("missed", missed_),
                 diagnostics::field("leftOut", leftOut_),
                 diagnostics::field("refused", refused_)});
}

} // namespace rawframe::render_scene
