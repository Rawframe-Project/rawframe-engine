// modules/material/tests/seeds/faded.material written by hand (ADR-0026,
// D490): the base texture's color faded by its own alpha, its roughness
// that alpha, lit; the rest at the Surface's defaults. faded_graph.slang is
// what rawframe.material writes from that graph (D483), and the two are
// built by tools/gen_shaders.py alike, as the cook builds a game's (D484).
import material;

struct Faded : IMaterial
{
    static Evaluated evaluate(Point point)
    {
        const float4 texel = baseTexture.Sample(baseSampler, point.uv);
        const float3 faded = texel.rgb * texel.a;
        Evaluated made;
        made.color = point.color.rgb * faded;
        made.metalness = 0.0;
        made.specular = float3(1.0);
        made.roughness = texel.a;
        made.glowing = float3(0.0);
        made.ior = 1.5;
        made.opacity = point.color.a;
        made.occlusion = 1.0;
        made.normal = point.normal;
        made.unlit = false;
        return made;
    }
}

export struct Material : IMaterial = Faded;
