# Materials

Materials are text documents: graphs in one shared grammar, cooked into what the renderers read. The modules are `graph` (the grammar), `material` (the material families and their compilers) and the cook's `rawframe.material` importer.

## Graph documents

Every graph family (surface, canvas and post-process materials, animation graphs) shares one grammar in one canonical form: a format version, the family's kind, nodes by id with a type, params and inputs, connections from node outputs, and presentation data an editor keeps (where a node sits) apart from meaning. Node and type ids have a fixed form; a connection to no node or round a cycle is refused; nodes, inputs and bytes are bounded. A graph has one canonical spelling, so the same meaning always gives the same bytes and diffs stay small.

## Families

- **Surface** materials (`surface`) end in one `rawframe/surface@1` node whose inputs are the Surface contract's parameters: base color, metalness, roughness, normal, emission, coat, anisotropy, transmission and the rest, each a literal where unconnected and left out at its default. Optional declared states say how it is drawn: `shading` (`lit` or `unlit`), `blend` (`opaque`, `masked`, `translucent`), `alpha_cutoff` and `double_sided`.
- **Canvas** materials (`canvas`) end in `rawframe/canvas@1`: a color, multiplied by the draw's own, and an emission added.
- **Post-process** materials (`post_process`) end in `rawframe/post_process@1`: a color blended by its alpha over the picture at the chain's insertion point.
- **Instances** name a base surface material and override its interface inputs, never its nodes. An instance cooks to its base's program and parameters of its own.

The node library is closed and versioned. Standard subgraphs (the functions in `modules/material/library/`, written by `tools/make_functions.py`) are graphs over the core nodes that any graph may call. An inline-Slang node holds code of its own, with the inputs and outputs its params declare.

## Blob and program materials

The cook compiles a surface material one of two ways:

- **Blob.** Most graphs fold into a compiled parameter blob, little-endian, that the scene's shaders read without the document. Drawing a blob material needs no shader of its own, so it costs no compile and runs on every backend at once.
- **Program.** A graph the blob cannot fold (a coat, anisotropy, moving vertices, inline Slang, more texture logic than the blob holds) is written as Slang implementing the scene's `IMaterial` interface and linked with the scene's containers through the pinned shader toolchain, one container per backend, plus the shadow pass's. A program material that moves its vertices places them in every pass, the depth prepass and shadows included; one that transmits is drawn after the opaque models, reading a copy of what they made.

Without the shader toolchain, a program material is refused by the cook and everything else cooks as before. At run time nothing compiles a shader.

## Traps

- An unconnected input at its default is left out of the document; writing the default is refused, so a document has one form.
- A program material may have at most four textures, and may not sample a texture, a normal map or a tangent in the vertex stage.
- A material input left unread by a fragment entry is dropped by Slang, which is why the check refuses fragment inputs that are never read.
