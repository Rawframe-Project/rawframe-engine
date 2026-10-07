// A surface material written by rawframe.material (D483); generated, never edited.
import material;

struct Generated : IMaterial
{
    static Evaluated evaluate(Point point)
    {
        const float4 v0 = baseTexture.Sample(baseSampler, point.uv);
        const float3 v1 = v0.rgb * v0.a;
        Evaluated made;
        made.color = point.color.rgb * (v1);
        made.metalness = 0.0;
        made.specular = (float3(1.0, 1.0, 1.0)) * (1.0);
        made.roughness = v0.a;
        made.glowing = (float3(1.0, 1.0, 1.0)) * (0.0);
        made.ior = 1.5;
        made.opacity = point.color.a * (1.0);
        made.occlusion = 1.0;
        made.normal = point.normal;
        made.unlit = false;
        return made;
    }
}

export struct Material : IMaterial = Generated;
