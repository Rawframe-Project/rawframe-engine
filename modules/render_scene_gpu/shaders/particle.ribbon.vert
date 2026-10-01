// The ribbons (ADR-0053, D354), vertex entry "ribbon": a trail's or a
// beam's points joined by quads facing the eye, two triangles between each
// point and the next, each point's edges across the way the ribbon runs
// there (from the point before it to the one after) and the way to the eye,
// half its width each side; the fragments are a particle's.

#version 450

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 viewProjection;
    vec4 toSun;
    vec4 sun;
    vec4 sky;
    vec4 exposure;
    // The eye's forward; each cascade's far end and texel; the cascades,
    // the shadows' distance, a cascade's side in texels (D289), and one
    // where the shadows are filtered soft (D330).
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
    // The sky's picture (D322): its levels, nought for none; and the
    // irradiance over π it gives, per unit of the sky's light, as nine
    // spherical harmonics' coefficients.
    vec4 environment;
    vec4 irradiance[9];
    // Whether the view's ambient occlusion is on (D327), its screen-space
    // reflections (D331), its contact shadows (D338), and whether it has
    // decals (D339).
    vec4 occlusion;
    vec4 reflections;
    vec4 contact;
    vec4 decals;
}
frame;

// The eye's right and up in the World's axes, and the particle clock now;
// and the near plane (D353).
layout(set = 0, binding = 1, std140) uniform View
{
    vec4 right;
    vec4 up;
    vec4 lens;
}
view;

// The ribbon: its first point among the frame's, how many, and its
// material's place.
layout(set = 0, binding = 2, std140) uniform Ribbon
{
    uvec4 range;
}
drawnRibbon;

// A point: where it is relative to the eye and its width; its color; and
// its texture's coordinate along the ribbon.
struct Point
{
    vec4 placeWidth;
    vec4 color;
    vec4 along;
};

layout(set = 0, binding = 3, std430) readonly buffer Points
{
    Point points[];
}
ribbonPoints;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;
layout(location = 2) out float outSoft;
layout(location = 3) flat out uint outMaterial;
layout(location = 4) out vec2 outShape;

void main()
{
    // Corners of a segment: its first point's two edges, then its next's.
    const uint kCorner = uint[6](0u, 1u, 2u, 2u, 1u, 3u)[gl_VertexIndex % 6];
    const uint kFirst = drawnRibbon.range.x;
    const uint kLast = drawnRibbon.range.x + drawnRibbon.range.y - 1u;
    const uint kAt = kFirst + uint(gl_VertexIndex) / 6u + (kCorner >> 1u);
    const Point kPoint = ribbonPoints.points[kAt];
    const vec3 kRuns = ribbonPoints.points[min(kAt + 1u, kLast)].placeWidth.xyz -
                       ribbonPoints.points[max(kAt, kFirst + 1u) - 1u].placeWidth.xyz;
    vec3 across = cross(kRuns, -kPoint.placeWidth.xyz);
    across = dot(across, across) > 1e-12 ? normalize(across) : view.right.xyz;
    const float kSide = float(kCorner & 1u) - 0.5;
    outUv = vec2(kPoint.along.x, float(kCorner & 1u));
    outColor = kPoint.color;
    outSoft = max(0.5 * kPoint.placeWidth.w, 1e-4);
    outMaterial = drawnRibbon.range.z;
    outShape = vec2(0.0, kSide * 2.0);
    gl_Position = frame.viewProjection * vec4(kPoint.placeWidth.xyz + across * (kSide * kPoint.placeWidth.w), 1.0);
}
