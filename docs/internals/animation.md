# Animation

Skeletal and property animation as deterministic simulation, played on the server where gameplay reads it and on clients where it is shown. The modules are `animation` (documents, sampling, graphs and their evaluation), `world_animation` (animators in the World) and `animation_import` (glTF skins and animations into documents).

## Documents

Animation is authored as text documents in one canonical form each, the `.rfanim` family, which the cook keeps as resources the runtime reads as it would the source:

- a **skeleton**: an ordered bone hierarchy, each bone with a stable target identity and a bind pose;
- a **clip**: tracks of keys over a duration, with events and sync markers; property tracks and flipbooks are discrete tracks;
- a **graph**: the one animation authority, a DAG of nodes on the shared graph grammar ([materials.md](materials.md#graph-documents)), driven by typed parameters: clips, blends, blend spaces with phase sync, additive layers, state machines with transitions, masks, two-bone IK and look-at stages, and root motion;
- a **mask**: which bones of a skeleton a part of a graph owns.

A graph names its clips, and a clip its skeleton, by resource identity. `rawframe-animation from-gltf` writes a glTF's skin and animations as documents.

## Evaluation

Sampling is pure and deterministic: the same clip, time and advance give the same pose and events on every machine, with no platform trigonometry in them. A graph is compiled once against its skeleton and clips and shared by every entity that plays it; each entity keeps its own instance, advanced then posed.

## In the World

An entity with a `rawframe.animation.animator` component plays the graph of the game's animator it names (`animator` lines). The game's parameter component, one field for each of the graph's parameters, sets them each step. One system, `rawframe.animation.step`, plays every animator once per tick in the simulation phase, in entity order, and tells each entity how many events it fired; a program may ask the step for a transition. Programs read bones and events through the `Animation.bone` and `Animation.event` doors, so a sword follows its bone and a footstep plays its sound.

A dedicated server poses only what gameplay needs: an animator's `subset` names a mask whose bones, with every bone above them, are all the server poses, so its hitboxes cost only what they read. Root motion moves the entity through a `RootMotion` component, and kinematic bodies may follow an animated bone.

## On clients

A client poses the animators it mirrors for drawing; skinned meshes are posed by a compute pass on the device. Presentation animation (a sprite sheet's frames, a look that is only shown) runs in present systems over the client's mirror and never reaches the server.
