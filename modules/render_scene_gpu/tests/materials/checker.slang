// A material written by hand for the scene's tests (ADR-0026, D485): a
// checkerboard of eight cells across the mesh's first coordinates, red and
// green, standing unlit as it is; the red cells clear, so a masked model
// of it is cut there (D487). tools/gen_shaders.py builds the scene
// with it as the cook builds a game's program material (D484), so the
// renderer's program path is tested on what a game would give it.
import material;

struct Checker : IMaterial
{
    static Evaluated evaluate(Point point)
    {
        const float2 cell = floor(point.uv * 8.0);
        const float sum = cell.x + cell.y;
        const float odd = sum - 2.0 * floor(sum / 2.0);
        Evaluated made;
        made.color = point.color.rgb * lerp(float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), odd);
        made.metalness = 0.0;
        made.specular = float3(1.0);
        made.roughness = 0.5;
        made.glowing = float3(0.0);
        made.ior = 1.5;
        made.opacity = point.color.a * odd;
        made.occlusion = 1.0;
        made.normal = point.normal;
        made.unlit = true;
        return made;
    }
}

export struct Material : IMaterial = Checker;
