// A surface material written by rawframe.material (D483); generated, never edited.
import material;

struct Generated : IMaterial
{
    static Evaluated evaluate(Point point)
    {
        const float3 v0 = toObject(point.spaces, point.position, kPoint);
        const float3 v1 = abs(v0);
        const float3 v2 = lerp(float3(0.0, 0.0, 0.0), float3(1.0, 0.0, 0.0), float3((v1).b));
        const float3 v3 = toView(point.spaces, point.normal, kNormal);
        const float v4 = saturate((v3).b);
        const float3 v5 = lerp(float3(0.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(v4));
        const float3 v6 = v2 + v5;
        Evaluated made;
        made.color = point.color.rgb * (v6);
        made.metalness = 0.0;
        made.specular = (float3(1.0, 1.0, 1.0)) * (1.0);
        made.roughness = 0.3;
        made.glowing = (float3(1.0, 1.0, 1.0)) * (0.0);
        made.ior = 1.5;
        made.opacity = point.color.a * (1.0);
        made.occlusion = 1.0;
        made.normal = point.normal;
        made.unlit = true;
        return made;
    }

    static float3 offset(Placed placed)
    {
        return float3(0.0);
    }
}

export struct Material : IMaterial = Generated;
