# Rendering

How a client draws its mirrored World. The modules are `render` (the one device and its frames), `render_canvas` and `render_canvas_gpu` (2D), `render_scene` and `render_scene_gpu` (3D), `particles` and `particles_gpu`, `view` (what each local player sees), and `material` ([materials.md](materials.md)). Every GPU call goes through Maul RHI, which only these modules include.

## Contents

- [The device](#the-device)
- [Frames and stages](#frames-and-stages)
- [Views](#views)
- [The canvas](#the-canvas)
- [The scene](#the-scene)
- [Color](#color)
- [Shaders](#shaders)
- [What a server carries](#what-a-server-carries)

## The device

`render` owns the process's one device: Maul RHI's instance, the adapter chosen and the device opened on it, lent to the canvas and scene renderers. Nothing else holds a device. Opening is a request answered later, as Maul RHI answers every request, so a client starts and plays before its device is ready. Vulkan draws on Linux and Android, Direct3D 12 on Windows, Metal on Apple's platforms, and WebGPU in the browser. In a headset the device is made by the XR runtime. With no GPU the tests draw on lavapipe, Mesa's CPU rasterizer, and read pixels back.

## Frames and stages

Each Host iteration makes one frame:

1. **Extract** (`presentation_extract`): the CPU halves (`render_canvas`, `render_scene`) copy what they draw out of the World, so the World is free again before any drawing work.
2. **Queue** (`present`): they cull against each view's cameras, order and batch, and queue draws.
3. **Record**: the GPU halves record the queued draws into the frame `render` owns, the scene first, then the canvas, then the UI over both.
4. **Display**: the frame is submitted once and shown on each window's surface.

Textures and meshes are uploaded once, and again when a reload replaces them; a frame's uploads keep within a budget, and what would pass it waits for a later frame. A view whose frames would draw the same picture as before draws nothing, so an idle editor viewport costs no processor.

## Views

A local player sees the game through the camera on their entity, in their region of the window: the game's `layout` lines split the window for local players, and its `aspect` line keeps each view's shape, with bars of a declared color around it. Each player's view is drawn apart and copied into its region, joining the frame when its region is first placed, as a preview's split views do. One frame holds every view it draws, so the device allows each frame 512 passes and 4 MiB of commands (`render::DeviceSettings`), room for four views of a scene like the plaza. A client's `frame_summary` says the most any frame took of each limit (`mostPasses`, `mostCommandBytes` and the others) and the limit its frames came closest to, in percent (`fullestLimit`, `fullestShare`); a frame a limit refused ends the frames with `frame_failed`, whose `limit` names it. A render texture (`rendertexture` lines) is a picture a camera's view is drawn into, every frame or when asked, which a material reads as it reads a texture. Each view's render scale follows its frames: fewer pixels while frames take clearly longer than they should, more once they keep well within it. In a headset each eye is a view of its own.

## The canvas

The canvas draws 2D: sprites (`rawframe.canvas.Sprite`) where their poses put them, tilemaps, particle emitters and trails, culled against the player's camera, put in draw order, and batched by texture into quads. Canvas materials color and light what a sprite shows.

## The scene

The scene draws 3D: models (`rawframe.model`) with a physically based lit model, skinned by a compute pass where animated; a sun with cascaded shadows; clustered point and spot lights with atlas shadows and soft filtering; an image-lit sky and reflection probes; ambient occlusion, screen-space reflections and contact shadows; decals; bloom, motion blur and depth of field; temporal, FXAA and multisampled anti-aliasing; and post-process materials inserted into the chain. Probes are baked offline by `rawframe-bake`.

## Color

Lighting is computed in linear light, in physical units. The picture is tonemapped (AgX, PBR Neutral, or a linear clamp for measuring), graded by tables, and drawn into an sRGB texture, blended in linear light, before display. A window's output mode is chosen from the closed set its surface offers, and reported with the reference white standard-range content is shown at.

## Shaders

Slang is the one first-party GPU language. No shipped program compiles a shader: `tools/gen_shaders.py` compiles each module's shaders offline with the pinned Slang compiler into containers carrying every driver family's code (SPIR-V for Vulkan, WGSL for WebGPU, MSL for Metal, DXIL for Direct3D 12), which are committed. A build embeds only the container its driver reads, so the web client carries no Metal or Direct3D 12 code. Every engine shader's WGSL is compiled by a real browser's WebGPU in the `full` tier.

A fragment shader must read every input it declares: crossed to HLSL for Direct3D 12, an unread input is left out and the rest no longer line up with the vertex stage's outputs. `tools/check_repo.py` enforces it.

## What a server carries

The scene's CPU half is client only. A dedicated server links no rendering, window, UI or audio code; it carries the same components as plain data, which replicate to clients that draw them.
