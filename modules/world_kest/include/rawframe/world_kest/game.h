#pragma once

// A Kest-scripted game as the engine loads it (D21). Kest types give each
// component its fields and widths; this text gives what Kest cannot say yet:
// stable identity, which functions are systems, and what the World starts
// with. It is parsed, never executed:
//
//   # A comment runs to the end of its line.
//   program movers.kest
//   component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 game.position Position
//   component 5b1c9e22-4f07-4d3a-8c6b-91e7d2a0f4c8 game.velocity Velocity
//   system game.integrate simulation integrate write game.position read game.velocity
//   spawn 3 game.position x=0 y=0 game.velocity dx=1 dy=2
//
// `program` is relative to the description; a program that creates or names
// entities imports `rawframe.world`, and one that draws random numbers
// imports `rawframe.random`, both found through its `kest.project`. A
// `system` line takes an identity, a phase, the Kest function, then its
// columns in the function's order: `entities`, or `read`, `write`, `with`, or
// `without` before a component; `after` or `before` before another system of
// the same phase; and `random` before each stream it draws from. Every
// component may be inserted and removed by the program through
// `<Kest type>.insert` and `<Kest type>.remove`. A `spawn` line creates
// entities with the listed components; a field not given is zero. A
// `scene <file>` line names a scene document (rawframe/scene/scene.h)
// beside the description whose entities the World starts with too; its
// components are the description's, each with the layout the scene was
// authored against. A `prefab <16 hex digits> <file>` line names a scene a
// program may spawn, whole, while it runs, with `scene.spawn(<identity>)`
// from `rawframe.scene`. For networked play, `replicate` lists the components that replicate, `player`
// the components each connected player's entity starts with, and `input` the
// one component a player's input is written into; `input <component>
// perceived` also gives each player a `rawframe.replication.perception`
// (rawframe.replication's Perception) holding the moment its client saw
// when it gave the command being run, for lag-compensated queries such as
// physics2d.castRayAt. `predict` lists the
// player's components a client predicts, and a system marked `predicted` runs
// on predicting clients too, over the player alone (SPEC-0041); it may write
// no replicated component that is not predicted and draw from no World
// stream, whose state a client does not have. An `effect <name> predicted`
// or `effect <name> confirmed_only` line declares a presentation effect,
// which one predicted system with `emits <name>` emits through
// `Effects.<name>(entity)`; a predicting client delivers it once (D219), a
// server keeps nothing of it. `nearby` lists replicated
// components of other entities a predicting client puts beside its player,
// as last heard, whenever it simulates again, so the player's steps meet
// them (D39): a game with physics lists the three body components. It
// changes what a client predicts, never what it compares. `interpolate`
// lists the
// replicated components a client shows other entities' values of between
// the states it receives, a little in the past, rather than as each arrives.
// An `interest` line makes what each connection is sent spatial: an entity
// whose component's coordinate fields lie within the radius of the player's
// is sent, one without the component is sent to every connection, and the
// player always is:
//
//   interest game.position x y within 40
//
// A `save <document> <component>...` line declares the game's save: the
// components it keeps of every persistent entity (world/persistent.h), a
// scene's entity carrying `rawframe.world.persistent` or one a program
// persists. A `save player <document> <component>...` line declares what
// each player keeps of their entity, among the components players start
// with, under the identity of the session they ask for. Where and when saves
// are kept is the operator's (`save.*`).
//
// An `admission <function>` line names the game's own admission rule, a
// function of the program the server asks about each client it would admit
// (rawframe.admission says its shape and answers); a game without one
// admits every client the engine does. It needs `replicate`.
//
// A `physics2d` line gives the World 2D physics (D33): the game gains the
// engine's physics components, named `rawframe.physics2d.body`, `.pose`,
// `.velocity`, `.impulse`, and `.contact`, whose Kest types are
// rawframe.physics2d's, and a step in the simulation phase,
// `rawframe.physics2d.step`, that systems order themselves around with
// `before` and `after`:
//
//   physics2d gravity 0 -10 substeps 4
//
// A `physics3d` line does the same in three dimensions (D44), with
// `rawframe.physics3d` components and step, three numbers of gravity, and
// y up. A game has one physics line at most.
//
//   physics3d gravity 0 -9.8 0
//
// `collision` lines are SPEC-0037's collision document: classes, each a name
// and a durable identity of sixteen hex digits; rules between two classes,
// `collide`, `trigger`, or `ignore`; and the rule for every pair not ruled
// (`collide` unless said). A body's `collisionClass` in a spawn line may be
// written as the class's name:
//
//   collision class player 7a31c0de00000001
//   collision class coin 7a31c0de00000002
//   collision rule player coin trigger
//   collision default collide
//
// An `animator` line gives entities a graph to play (D124): the game gains
// the engine's `rawframe.animation.animator` component (rawframe.animation's
// Animator), whose `graph` names the animator by its identity, or in a spawn
// line by its graph file. The graph is a document beside the description;
// its clips and their skeleton are found by resource identity, beside the
// description by their sidecars in development and in the game's content
// once cooked. `parameters` names the component whose fields, one of the
// same name for each of the graph's parameters, set them every step: a
// `float` from an f32, an `int` from a u32, a `bool` from a bool, and a
// `vec2` from `<name>.x` and `<name>.y`. `subset` names a mask document
// whose bones, with every bone above them, are all a dedicated server poses
// (SPEC-0035), so its hitboxes cost only what they read:
//
//   animator 51f0c0de00000001 walker.rfanim parameters game.gait
//   animator 51f0c0de00000002 archer.rfanim subset 5d0a7e3c91b24f68a0c3e7d1942b6f85
//
// An `entity` line names a component's fields that hold a
// `rawframe.world.Entity`, which a checkpoint writes as a reference rather
// than as numbers:
//
//   entity game.target who

#include "rawframe/base/bits128.h"
#include "rawframe/physics/collision.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world_replication/prediction.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// The most lines a game description may have.
inline constexpr std::size_t kMaximumGameLines = 4096;
/// The most entities one spawn line may create.
inline constexpr std::uint32_t kMaximumSpawnCount = 1U << 20U;
/// SPEC-0042's `extension_points_per_game_max` (D195).
inline constexpr std::size_t kMaximumExtensionPoints = 64;
/// SPEC-0042's `mod_api_surface_bytes_max` (D195): the bytes of the words
/// of a game's Mod API lines, a space after each.
inline constexpr std::size_t kMaximumModApiSurfaceBytes = 16 * 1024;

struct GameComponent {
    schema::ComponentTypeId id;
    std::string name;
    std::string kestType;
};

struct GameColumn {
    world::Access access = world::Access::Read;
    std::string component;
    /// The archetype's entities rather than a component.
    bool entities = false;
};

struct GameSystem {
    std::string identity;
    world::Phase phase = world::Phase::Simulation;
    std::string entry;
    std::vector<GameColumn> columns;
    std::vector<std::string> after;
    std::vector<std::string> before;
    /// World random streams, drawn from by place through `rawframe.random`.
    std::vector<std::string> randomStreams;
    /// Runs on predicting clients too, over the player alone.
    bool predicted = false;
    /// The effects it emits, from `emits <name>` pairs (D219); only a
    /// predicted system emits, and each effect has one emitter.
    std::vector<std::string> emits;
};

/// A presentation effect predicted systems emit (SPEC-0041, D219), from an
/// `effect <name> predicted|confirmed_only` line: a program emits it with
/// `Effects.<name>(entity)`, and a predicting client delivers it once.
struct GameEffect {
    std::string name;
    world_replication::EffectClass effectClass = world_replication::EffectClass::Predicted;
};

/// Effect kinds a game declares at most.
inline constexpr std::size_t kMaximumEffects = 64;

struct GameFieldValue {
    std::string field;
    std::string value;
};

struct GameSpawnComponent {
    std::string component;
    std::vector<GameFieldValue> fields;
};

struct GameSpawn {
    std::uint32_t count = 0;
    std::vector<GameSpawnComponent> components;
};

/// A collision class: a name for lines and spawns, and its durable identity.
struct GameCollisionClass {
    std::string name;
    std::uint64_t id = 0;
};

struct GameCollisionRule {
    std::string first;
    std::string second;
    physics::CollisionRule rule = physics::CollisionRule::Collide;
};

/// The game's physics, in two dimensions or three, and how its world is set
/// up.
struct GamePhysics {
    std::uint8_t dimensions = 2;
    float gravityX = 0;
    float gravityY = -10;
    float gravityZ = 0;
    std::uint32_t substeps = 4;
};

/// SPEC-0037's collision document, by line.
struct GameCollision {
    std::vector<GameCollisionClass> classes;
    std::vector<GameCollisionRule> rules;
    physics::CollisionRule fallback = physics::CollisionRule::Collide;
};

/// Spatial interest: the position component, its coordinate fields, and the
/// radius within which the player is sent another entity.
struct GameInterest {
    std::string component;
    std::vector<std::string> axes;
    double radius = 0;
};

/// A component field holding an entity, by its path in the Kest type.
struct GameEntityField {
    std::string component;
    std::string field;
};

/// How a client makes its input from a player's controls (ADR-0037): an
/// `input.actions` document, and a Kest program whose function `entry` fills
/// the input component from committed actions. Paths are beside the
/// description. Clients read them; a server never opens either.
struct GameControls {
    std::string actions;
    std::string program;
    std::string entry;
};

/// A declared sound, by the identity emitters name it by (ADR-0038).
struct GameSound {
    std::uint64_t id = 0;
    /// Its `audio.sound` document, beside the description.
    std::string path;
};

/// How a client sounds the game: its mixer layout and its sounds. Clients
/// read the files; a server never opens them.
struct GameAudio {
    std::string mixer;
    std::vector<GameSound> sounds;
};

/// A scene a program may spawn at run time with `Scene.spawn`, by the
/// identity it is declared with (D98).
struct GamePrefab {
    std::uint64_t id = 0;
    /// Its scene document, beside the description.
    std::string path;
};

/// A mesh a body may be made of (D112), by the identity a
/// `rawframe.physics3d.mesh` names it by.
struct GameMesh {
    std::uint64_t id = 0;
    /// Its source beside the description, which the cook makes a mesh
    /// resource (`rawframe.mesh`); a process reads the resource.
    std::string path;
};

/// An animator (ADR-0039, D124): a graph an entity's
/// `rawframe.animation.animator` plays by the identity its line gives it.
struct GameAnimator {
    std::uint64_t id = 0;
    /// Its graph document, beside the description, which names its clips
    /// and they their skeleton, each by resource identity.
    std::string path;
    /// The game's component the graph's parameters are read from, a field
    /// for each by its name; none for a graph that plays on its defaults.
    std::string parameters;
    /// The mask whose bones, and every bone above them, a dedicated server
    /// poses (SPEC-0035); none for all of them.
    std::optional<base::Bits128> subset;
};

/// A save document (ADR-0057): its name and the components it keeps of the
/// game's persistent entities.
struct GameSave {
    std::string document;
    std::vector<std::string> components;
};

/// Who may mod a game (ADR-0023): no one, the mods the game approves by
/// subject, or any correctly signed one meeting its Mod API.
enum class ModPolicy : std::uint8_t {
    Closed,
    Curated,
    Open,
};

/// One extension point of a game's Mod API (SPEC-0042). A `data` point,
/// from `extension <name> data <component> multi|exclusive [required]`,
/// takes declarative values of one component, never executed (D177). An
/// `event` point, from `extension <name> event <component> after <system>
/// [write <component>]... multi|exclusive [required]`, takes handlers: Kest
/// functions of a mod's own untrusted machine, run as systems right after
/// the game's system, over the entities holding the event's component, which
/// they read, and the components the point lets them write (D181). A
/// `service` point, from `extension <name> service <component> exclusive
/// [required]`, takes one provider: a Kest function of a mod's machine that
/// the game calls through the door `Mods.<name>(value: T) -> T`, T the
/// component's type, and that rewrites the value it is lent (D199). A
/// `replacement` point, from `extension <name> replacement <system>
/// exclusive [required]`, lets one mod run its own function in place of a
/// game system's, over the same columns in the same place of the schedule,
/// on the mod's machine; a predicted system is never replaceable (D200).
struct GameExtensionPoint {
    enum class Kind : std::uint8_t {
        Data,
        Event,
        Service,
        Replacement
    };
    /// Its identity is `<namespace>/<name>`.
    std::string name;
    Kind kind = Kind::Data;
    /// The component each contribution is a value of, that an event's
    /// entities hold, or whose type a service call takes and answers; for a
    /// replacement, the game system it replaces.
    std::string accepts;
    /// An event's: the game system its handlers run after, and what they
    /// may write.
    std::string after;
    std::vector<std::string> writes;
    /// At most one contribution; otherwise any number.
    bool exclusive = false;
    /// For an exclusive point, from a `prefer <point> <publisher/name>...`
    /// line: when several mods claim it, the first of these that does keeps
    /// it, and the others' claims on it are set aside (D202).
    std::vector<std::string> preferred;
    /// At least one contribution, or the Composition is invalid.
    bool required = false;
};

/// A game's Mod API surface (SPEC-0042, D177): its policy from a `mods
/// closed|curated|open` line (closed without one), its namespace and
/// version from a `modapi <namespace> <version>` line, the mods a curated
/// game approves from `approve <publisher/name>` lines, and its points.
struct GameModApi {
    ModPolicy policy = ModPolicy::Closed;
    std::string modNamespace;
    std::uint32_t version = 0;
    std::vector<std::string> approved;
    std::vector<GameExtensionPoint> points;
};

struct GameDescription {
    std::string program;
    std::vector<GameComponent> components;
    std::vector<GameSystem> systems;
    std::vector<GameEffect> effects;
    std::vector<GameSpawn> spawns;
    /// Scene documents (rawframe/scene/scene.h) whose entities the World
    /// starts with, beside any `spawn` lines, from `scene <file>` lines.
    std::vector<std::string> scenes;
    /// Scenes a program spawns at run time, from `prefab <16 hex digits>
    /// <file>` lines.
    std::vector<GamePrefab> prefabs;
    /// From `mesh <16 hex digits> <file>` lines.
    std::vector<GameMesh> meshes;
    /// From `animator <16 hex digits> <graph file> [parameters <component>]
    /// [subset <32 hex digits>]` lines.
    std::vector<GameAnimator> animators;
    /// Networked play: what replicates, what a player starts with, and
    /// which component a player's input is written into.
    std::vector<std::string> replicated;
    std::vector<std::string> player;
    std::string input;
    /// Each command carries the moment its client saw, which the player's
    /// `rawframe.replication.perception` holds when the command runs.
    bool inputPerceived = false;
    /// The player's components a client predicts from its own input.
    std::vector<std::string> predicted;
    /// Components of other entities a predicting client steps its player
    /// among.
    std::vector<std::string> nearby;
    /// Components shown between states on every entity but the player's.
    std::vector<std::string> interpolated;
    std::vector<GameEntityField> entityFields;
    std::optional<GameInterest> interest;
    std::optional<GamePhysics> physics;
    GameCollision collision;
    /// From an `actions <file>` line and a `sample <program> <entry>` line,
    /// which come together and need an `input` line.
    std::optional<GameControls> controls;
    /// From a `mixer <file>` line and `sound <16 hex digits> <file>` lines,
    /// which need it.
    std::optional<GameAudio> audio;
    /// String tables and translations (SPEC-0033) a client's text comes
    /// from, from `text <file>` lines, each a document the cook makes a
    /// resource of (`rawframe.text`); a process reads the resources by
    /// identity, and a dedicated server not at all (D147).
    std::vector<std::string> texts;
    /// The project's default locale, from a `locale <tag>` line; empty for
    /// each table's source locale. A client checks the tag.
    std::string locale;
    /// Who may mod the game and where (SPEC-0042).
    GameModApi mods;
    /// The program's admission rule, from an `admission <function>` line;
    /// empty admits every client the engine does.
    std::string admission;
    /// What a save keeps, from a `save <document> <component>...` line;
    /// no document, no save.
    GameSave save;
    /// What each player's save keeps of their entity, from a `save player
    /// <document> <component>...` line.
    GameSave playerSave;
};

/// Parses a description. Refuses (`invalid_argument`, with the line number as
/// context) an unknown keyword, a malformed ID or phase, a missing or second
/// `program`, a column or spawn naming a component the text does not declare,
/// and more than kMaximumGameLines lines.
[[nodiscard]] result::Result<GameDescription> parseGame(std::string_view text);

/// Every scene file the description names: its scene lines', then its
/// prefabs', each once.
[[nodiscard]] std::vector<std::string> sceneNames(const GameDescription& game);

/// A spawn line's value with the game's names made identities: a collision
/// class's name as a body's `collisionClass`, a mesh's file as a
/// `rawframe.physics3d.mesh`'s `mesh`, and an animator's graph file as a
/// `rawframe.animation.animator`'s `graph`. Anything else as written.
[[nodiscard]] std::string
spawnValue(const GameDescription& game, std::string_view component, const GameFieldValue& value);

} // namespace rawframe::world_kest
