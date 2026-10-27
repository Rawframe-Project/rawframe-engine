// A surface material written by rawframe.material; generated, never edited.
import material;

struct Generated : IMaterial
{
    static Evaluated evaluate(Point point)
    {
        Evaluated made;
        made.color = point.color.rgb * (float3(0.8, 0.8, 0.8));
        made.metalness = 0.0;
        made.specular = (float3(1.0, 1.0, 1.0)) * (1.0);
        made.roughness = 0.3;
        made.glowing = (float3(1.0, 1.0, 1.0)) * (0.0);
        made.ior = 1.5;
        made.opacity = point.color.a * (1.0);
        made.occlusion = 1.0;
        made.normal = point.normal;
        made.coat = 0.0;
        made.coatRoughness = 0.0;
        made.coatNormal = point.normal;
        made.anisotropy = 0.0;
        made.anisotropyRotation = 0.0;
        made.transmission = 0.0;
        made.transmissionColor = float3(1.0, 1.0, 1.0);
        made.transmissionDepth = 0.0;
        made.thickness = 0.0;
        made.thinWalled = false;
        made.unlit = false;
        return made;
    }

    static float3 offset(Placed placed)
    {
        const float3 v0 = float3(0.0, (placed.time.x + placed.time.y), 0.0);
        return v0;
    }
}

export struct Material : IMaterial = Generated;
