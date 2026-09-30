// The 3D scene's models (D284), vertex entry "vs": a mesh's vertex placed
// by its instance's model rows relative to the eye, then seen through the
// frame's view and reversed-Z projection, jittered. The depth prepass runs
// the same entry, so both land every vertex on the same depth. Where the
// vertex is seen without the jitter, and where it was seen the frame
// before, go on for its motion (D291).

#version 450

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 viewProjection;
    vec4 toSun;
    vec4 sun;
    vec4 sky;
    vec4 exposure;
    // The eye's forward; each cascade's far end and texel; the cascades,
    // the shadows' distance, and a cascade's side in texels (D289).
    vec4 forward;
    vec4 cascadeFar;
    vec4 cascadeTexel;
    vec4 shadow;
    mat4 cascades[4];
    // The clusters' tiles across and down, their slices, and the lights;
    // their near end, and the slices over the log of their far over near
    // (D290).
    vec4 clusterGrid;
    vec4 clusterDepth;
    // The view and projection without the jitter, and the frame before's
    // taking this frame's places (D291).
    mat4 unjittered;
    mat4 previous;
    // The ground's luminance below the horizon (D304).
    vec4 ground;
    // The sky's picture's levels and irradiance (D322).
    vec4 environment;
    vec4 irradiance[9];
    // Whether the view's ambient occlusion is on (D327).
    vec4 occlusion;
}
frame;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inModel0;
layout(location = 3) in vec4 inModel1;
layout(location = 4) in vec4 inModel2;
layout(location = 5) in vec3 inNormal0;
layout(location = 6) in vec3 inNormal1;
layout(location = 7) in vec3 inNormal2;
layout(location = 8) in vec4 inColor;
layout(location = 9) in vec4 inPrevious0;
layout(location = 10) in vec4 inPrevious1;
layout(location = 11) in vec4 inPrevious2;
layout(location = 12) in float inMaterial;
// The vertex's texture coordinates, the mesh's first set (D309).
layout(location = 13) in vec2 inUv;
// The vertex's tangent and the bitangent's sign (D313).
layout(location = 14) in vec4 inTangent;
// The reflection probe it reflects, nought for the sky's picture (D325).
layout(location = 15) in float inProbe;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec4 outColor;
layout(location = 2) out vec3 outPlaced;
layout(location = 3) out vec3 outNow;
layout(location = 4) out vec3 outBefore;
layout(location = 5) flat out uint outMaterial;
layout(location = 6) out vec2 outUv;
layout(location = 7) out vec4 outTangent;
layout(location = 8) flat out uint outProbe;

invariant gl_Position;

void main()
{
    const vec4 kVertex = vec4(inPosition, 1.0);
    const vec3 kPlaced = vec3(dot(inModel0, kVertex), dot(inModel1, kVertex), dot(inModel2, kVertex));
    gl_Position = frame.viewProjection * vec4(kPlaced, 1.0);
    outNormal = mat3(inNormal0, inNormal1, inNormal2) * inNormal;
    outColor = inColor;
    outPlaced = kPlaced;
    const vec3 kWas = vec3(dot(inPrevious0, kVertex), dot(inPrevious1, kVertex), dot(inPrevious2, kVertex));
    outNow = (frame.unjittered * vec4(kPlaced, 1.0)).xyw;
    outBefore = (frame.previous * vec4(kWas, 1.0)).xyw;
    outMaterial = uint(inMaterial);
    outUv = inUv;
    outProbe = uint(inProbe);
    // A tangent turns with the model; a mirrored model flips its sign.
    const vec3 kTurned = vec3(dot(inModel0.xyz, inTangent.xyz), dot(inModel1.xyz, inTangent.xyz),
                              dot(inModel2.xyz, inTangent.xyz));
    const float kMirror = dot(inModel0.xyz, cross(inModel1.xyz, inModel2.xyz)) < 0.0 ? -1.0 : 1.0;
    outTangent = vec4(kTurned, inTangent.w * kMirror);
}
