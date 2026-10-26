# Physics and navigation

Physics is simulation: it runs on the server, steps on the World's tick, and gives the same bits on every machine. The modules are `physics` (what 2D and 3D share), `physics2d` over Maul2D, `physics3d` over Maul3D, and `navigation` over Maul Nav. Each library's types stay behind its module.

## Contents

- [Two dimensions and three](#two-dimensions-and-three)
- [The step](#the-step)
- [Collision](#collision)
- [Characters, joints and attachments](#characters-joints-and-attachments)
- [Queries](#queries)
- [Navigation](#navigation)

## Two dimensions and three

2D and 3D are equal and separate: a game's `physics2d` or `physics3d` line (one at most) gives its World one physics world, the engine's physics components (`rawframe.physics2d.body`, `.pose`, `.velocity`, `.impulse`, `.contact` and the 3D equivalents) and one step system. A 2D game carries no 3D cost. What both share is the vocabulary in `physics`: motion kinds (static, kinematic, dynamic; sleep is a state, never a fourth motion), the collision document, joint axes and the ground state of a character.

## The step

One system per World, `rawframe.physics2d.step` or `rawframe.physics3d.step`, steps in the simulation phase:

1. Bodies follow their entities, in entity order: an entity that gained the body components gets a body, one that lost them loses it, and a changed body component makes the body again.
2. What gameplay wrote since the last step goes in. A pose that is not what the last step wrote is a teleport, which makes the body again there, so a body put somewhere carries nothing of where it was. A velocity is a new velocity; an impulse is applied and cleared.
3. Joints follow their entities as bodies do.
4. Every character finds how far it can move along the velocity gameplay wants and is given the velocity that takes it there; then the world steps once, with the game's substeps.
5. Contacts are written from the step's contact and overlap streams, and every body's pose and velocity are written back.
6. Attached entities are put at their parent's pose and offset, parents before children.

Gameplay that pushes bodies runs `before` the step; gameplay that reads where they went runs `after`. Nothing in the step depends on addresses, hash order or time, so a predicting client that makes its player's body again where the server says steps exactly as the server did ([prediction.md](prediction.md)).

## Collision

A game has one collision document (`collision` lines): classes, each with a name and a durable identity, a rule between pairs of classes (`collide`, `trigger` or `ignore`), and the rule for every pair not listed. It becomes the libraries' category and mask bits: a solid meets another class's solid where the rule is `collide`, and its sensors where the rule is not `ignore`. Bodies may be shapes, meshes the cook made (`mesh` lines, including blockouts and terrain) or a tilemap's tiles.

## Characters, joints and attachments

A character controller moves a body along the velocity a game wants, slides along what it meets, and reports its ground state: on a surface no steeper than it may stand on, or pressed against one too steep. Joints hold each axis of their frame free, limited or locked. An attachment puts a child entity at its parent's pose and offset, after the step, and a kinematic body may follow an animated bone.

## Queries

Rays, sweeps and overlaps are doors of `rawframe.physics2d` and `rawframe.physics3d`, read against the last step. A lag-compensated query (`castRayAt`) casts back to the moment a player saw, against the poses the physics world kept for that many ticks, rewinding only what that player's connection had been sent. The bytes a World may keep of past poses are bounded.

## Navigation

A game's `navigation` line (with `physics3d`) has the server bake one navmesh for one agent (radius, height, step, slope and cell) from the static bodies, which `physics3d` gives as triangles, tile by tile, and bake again only the tiles whose bodies change. The same geometry and settings give the same bytes everywhere; the navmesh's fingerprint, a hash of every tile's bytes in place order, is a golden value. Navmeshes are baked in memory and never stored, so a new Maul Nav simply bakes its own.

Programs ask the way with `navigation.next`. Agents with the `rawframe.navigation.agent` component steer round each other: the `rawframe.navigation.avoid` system, before the 3D step, turns the velocity a game set into the nearest one that keeps clear of the other agents (ORCA). The same agents give the same velocities in any order, on every target.
