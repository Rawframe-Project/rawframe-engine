# Animation

Animation is part of the simulation: the server plays what gameplay reads (a sword's bone, a hitbox), and clients play what they show. How it works is in [animation internals](../internals/animation.md).

## Documents

Animation is authored as `.rfanim` documents, each with a sidecar:

- a **skeleton**: bones in order, each with a stable identity and a bind pose;
- a **clip**: tracks of keys over a duration, events and sync markers, and property tracks and flipbooks;
- a **graph**: what plays, driven by typed parameters: clips, blends, blend spaces, additive layers, state machines with transitions, masks, two-bone IK and look-at, and root motion;
- a **mask**: the bones a part of a graph owns.

A glTF's skin and animations become documents with:

```sh
rawframe-animation from-gltf <source.gltf | source.glb> <directory> [loop] [additive bind | additive first_frame]
```

## In a game

```
animator 51f0c0de00000001 walker.rfanim parameters game.gait
animator 51f0c0de00000002 archer.rfanim subset 5d0a7e3c91b24f68a0c3e7d1942b6f85
```

An entity with a `rawframe.animation.animator` component plays the graph its animator names. `parameters` names a component with one field for each of the graph's parameters, which set them every step: a `float` from an f32, an `int` from a u32, a `bool` from a bool, a `vec2` from `<name>.x` and `<name>.y`. One system, `rawframe.animation.step`, plays every animator once per tick in the simulation phase.

Programs read a bone's place and the events a step fired through `Animation.bone` and `Animation.event`, and may ask for a transition. Root motion moves the entity through a `RootMotion` component, and a kinematic body may follow an animated bone.

`subset` names a mask: a dedicated server poses only those bones and every bone above them, so its hitboxes cost only what they read.

## Drawn

A client draws skinned meshes posed by the animators it mirrors, and runs presentation animation (a sprite sheet's frames, a look only shown) in present systems over its mirror; that never reaches the server.
