// The ribbons (ADR-0053, D354), vertex entry "ribbon": a trail's or a
// beam's points joined by quads facing the eye, two triangles between each
// point and the next, each point's edges across the way the ribbon runs
// there (from the point before it to the one after) and the way to the eye,
// half its width each side; the fragments are a particle's.

#version 450

// The view (D353, D357): its projection of the World relative to the eye;
// the eye's right and up in the World's axes, and the particle clock now
// and its period; and the near plane, and one where the view is flat (a
// canvas's), facing straight down its forward.
layout(set = 0, binding = 0, std140) uniform View
{
    mat4 viewProjection;
    vec4 right;
    vec4 up;
    vec4 lens;
}
view;

// The ribbon: its first point among the frame's, and how many.
layout(set = 0, binding = 1, std140) uniform Ribbon
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

layout(set = 0, binding = 2, std430) readonly buffer Points
{
    Point points[];
}
ribbonPoints;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;
layout(location = 2) out float outSoft;
layout(location = 3) out vec2 outShape;

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
    // The way to the eye: back along a flat view's forward, else from the
    // point to the eye.
    const vec3 kToEye = view.lens.y > 0.5 ? cross(view.right.xyz, view.up.xyz) : -kPoint.placeWidth.xyz;
    vec3 across = cross(kRuns, kToEye);
    across = dot(across, across) > 1e-12 ? normalize(across) : view.right.xyz;
    const float kSide = float(kCorner & 1u) - 0.5;
    outUv = vec2(kPoint.along.x, float(kCorner & 1u));
    outColor = kPoint.color;
    outSoft = max(0.5 * kPoint.placeWidth.w, 1e-4);
    outShape = vec2(0.0, kSide * 2.0);
    gl_Position = view.viewProjection * vec4(kPoint.placeWidth.xyz + across * (kSide * kPoint.placeWidth.w), 1.0);
}
