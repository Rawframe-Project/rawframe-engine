#pragma once

// A scene's state (D283): what it reads from a World, what it extracted,
// and the frame it queues from that, shared by its extraction
// (extract.cpp) and its queueing (scene.cpp).

#include "bounded.h"
#include "clusters.h"
#include "lights.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/world/column_query.h"
#include "skinning.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace rawframe::render_scene {

struct Scene::State {
    SceneSettings settings;
    /// The presented World's time, and the one the frame before showed
    /// (D691).
    double seconds = 0;
    std::optional<double> queuedSeconds;
    std::vector<world::ColumnQuery> models;
    std::optional<world::ColumnQuery> sun;
    std::optional<world::ColumnQuery> sky;
    /// The point lights' queries, then the spot lights'.
    std::vector<std::pair<world::ColumnQuery, bool>> lightQueries;
    /// The reflection probes' queries, and what the frame extracted (D325).
    std::vector<world::ColumnQuery> probeQueries;
    std::vector<ProbeInstance> probes;
    /// The decals' queries, and what the frame extracted (D339).
    std::vector<world::ColumnQuery> decalQueries;
    std::vector<DecalInstance> decals;
    /// The particle emitters', trails', and beams' queries, what the frame
    /// extracted (D352, D354), and the view's accounting of them from
    /// frame to frame (D357).
    std::vector<world::ColumnQuery> emitterQueries;
    std::vector<world::ColumnQuery> trailQueries;
    std::vector<world::ColumnQuery> beamQueries;
    std::vector<particles::EmitterInstance> emitters;
    std::vector<particles::TrailInstance> trails;
    std::vector<particles::BeamInstance> beams;
    particles::Particles particles;
    /// The lines shown (D464).
    std::vector<SceneLine> lines;
    std::vector<LightInstance> punctual;
    std::optional<schema::ComponentRuntimeId> pose;
    /// Each entity's layer, and the layers whose models are left out
    /// (D791).
    std::optional<schema::ComponentRuntimeId> layer;
    std::vector<std::uint32_t> hiddenLayers;
    std::uint64_t modelsHidden = 0;
    /// The game's placements (D665), and its tracking (D676).
    std::vector<schema::ComponentRuntimeId> placements;
    std::vector<schema::ComponentRuntimeId> tracking;
    std::map<std::uint64_t, Bounded> meshes;
    /// Each game material's place in the frame's materials, and whether
    /// the material at each place is translucent.
    std::map<std::uint64_t, std::uint32_t> materials;
    std::vector<bool> translucent;
    std::vector<ModelInstance> extracted;
    /// The extracted skinned models' palettes, and the joints each mesh's
    /// skin names found among its skeleton's bones (D508).
    std::vector<Matrix> posed;
    Skinning skinning;

    /// A skinned model's palette, if its entity was played (D508).
    void posePalette(const world_animation::AnimationQueries* poses, ModelInstance& instance);
    std::optional<Sun> sunNow;
    std::optional<Sky> skyNow;
    /// Whether the sky is drawn graded (D925).
    bool gradedSky = false;
    std::vector<const ModelInstance*> order;
    SceneFrame frame;
    /// What the frame before drew (D291): each model's turn and scale, and
    /// its place in the World, by entity and component; and this frame's,
    /// as it is queued.
    struct Placement {
        std::array<float, 9> turned{};
        std::array<double, 3> position{};
        /// The palette it was posed with, if posed (D510).
        std::vector<Matrix> palette;
    };
    std::map<std::pair<world::EntityHandle, std::uint32_t>, Placement> placed;
    std::map<std::pair<world::EntityHandle, std::uint32_t>, Placement> placing;
    /// The eye the frame before was seen from, if it saw, and its view and
    /// projection, unjittered; the frames queued.
    /// Every model a punctual light's shadow may take, with its bounding
    /// sphere relative to the eye, in draw order; and whether each light
    /// kept for the view asks for shadows (D292).
    std::vector<ShadowCandidate> candidates;
    std::vector<bool> shadowed;
    /// What the clusters name this frame (D339).
    std::vector<ClusterName> named;
    /// The models near enough to cast into the sun's cascades (D298).
    std::vector<ShadowCandidate> sunCandidates;
    std::optional<std::array<double, 3>> previousEye;
    Matrix previousViewProjection{};
    std::uint64_t frames = 0;
    /// Whether the eye went on from the frame before, not first nor cut
    /// away; and whether that frame was metered.
    bool continuous = false;
    bool meteredBefore = false;

    /// The camera's post processes as the frame runs them (D349).
    void postProcessesOf(const SceneCamera& camera);

    void lights();

    /// The sun's cascades for this view (ADR-0051): the view from the near
    /// plane to the shadows' distance split between them, the logarithmic
    /// and uniform schemes blended; each a square in the sun's axes around
    /// its slice's bounding sphere, whose radius depends only on the lens so
    /// the square keeps its size as the eye turns, placed on whole texels
    /// of the World, not the eye, so it does not shimmer as the eye moves.
    /// Its depth reaches the shadows' distance toward the sun, for casters
    /// between the sun and the view.
    const SceneFrame& queue(const SceneCamera& camera);

    /// Each cascade's casters (D298): the models whose bounding spheres
    /// reach its box, which runs from the shadows' distance toward the sun
    /// to past its slice, in draw order, one after another; a model in two
    /// cascades is in both. Past the models' limit, the rest are left out.
    void castIntoCascades();

    /// The temporal inputs (D291): the jitter, and the frame before's view
    /// taking this frame's places, unless this is the first or the eye cut.
    void temporal(const SceneCamera& camera, bool sees);
};

} // namespace rawframe::render_scene
