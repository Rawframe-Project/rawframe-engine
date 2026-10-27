// The scene's lighting model: a point's surface as
// the BRDF takes it, and what it reflects and lets through of the light
// arriving from one way. A module the scene imports, not a container of
// its own.

module lighting;

public static const float kPi = 3.14159265;

// A point's surface as the BRDF takes it: the diffuse color (the base
// color less what metal takes), what it reflects head on (a dielectric's
// by its index of refraction and specular color, or a metal's base color),
// and its roughness; the coat over it, its weight, nought for none, its
// roughness, and its normal; and its anisotropy, nought for none,
// and the directions across the surface its roughness stretches along and
// stays across.
public struct Surface
{
    public float3 diffuse;
    public float3 headOn;
    public float roughness;
    public float coat;
    public float coatRoughness;
    public float3 coatNormal;
    public float anisotropy;
    public float3 along;
    public float3 across;
    // What a transmitting model lets through, and the color it is
    // tinted as it passes, its base's and its volume's.
    public float transmission;
    public float3 tint;
    public float ior;
}

// What a coat reflects head on: a dielectric's of index 1.6, OpenPBR's
// coat's.
public static const float kCoatHeadOn = 0.0532544;

// GGX's distribution about `normal` at `roughness` times Smith's
// height-correlated visibility, times the cosine the light arrives at: a
// specular lobe before its Fresnel.
public float lobe(float3 normal, float3 toEye, float3 toward, float3 halfway, float roughness)
{
    const float nl = max(dot(normal, toward), 0.0);
    const float nv = max(dot(normal, toEye), 1e-4);
    const float nh = max(dot(normal, halfway), 0.0);
    const float alpha = max(roughness * roughness, 1e-3);
    const float alpha2 = alpha * alpha;
    const float spread = nh * nh * (alpha2 - 1.0) + 1.0;
    const float distribution = alpha2 / (kPi * spread * spread);
    const float visibility =
        0.5 / (nl * sqrt(nv * nv * (1.0 - alpha2) + alpha2) + nv * sqrt(nl * nl * (1.0 - alpha2) + alpha2));
    return distribution * visibility * nl;
}

// An anisotropic surface's GGX distribution times its height-correlated
// visibility, as KHR_materials_anisotropy states them: the roughness
// stretched along `surface.along` toward one by the anisotropy squared.
public float anisotropic(Surface surface, float3 toEye, float3 toward, float3 halfway, float nl, float nv, float nh)
{
    const float alpha = max(surface.roughness * surface.roughness, 1e-3);
    const float stretched = lerp(alpha, 1.0, surface.anisotropy * surface.anisotropy);
    const float both = stretched * alpha;
    const float3 spread = float3(alpha * dot(surface.along, halfway), stretched * dot(surface.across, halfway), both * nh);
    const float weight = both / dot(spread, spread);
    const float distribution = both * weight * weight / kPi;
    const float fromEye = nl * length(float3(stretched * dot(surface.along, toEye), alpha * dot(surface.across, toEye), nv));
    const float fromLight =
        nv * length(float3(stretched * dot(surface.along, toward), alpha * dot(surface.across, toward), nl));
    return distribution * saturate(0.5 / (fromEye + fromLight));
}

// The BRDF beneath any coat.
public float3 beneath(Surface surface, float3 normal, float3 toEye, float3 toward)
{
    const float nl = max(dot(normal, toward), 0.0);
    if (nl <= 0.0)
    {
        return float3(0.0);
    }
    const float3 halfway = normalize(toEye + toward);
    const float nv = max(dot(normal, toEye), 1e-4);
    const float nh = max(dot(normal, halfway), 0.0);
    const float alpha = max(surface.roughness * surface.roughness, 1e-3);
    const float alpha2 = alpha * alpha;
    const float3 fresnel = surface.headOn + (1.0 - surface.headOn) * pow(1.0 - max(dot(toEye, halfway), 0.0), 5.0);
    const float spread = nh * nh * (alpha2 - 1.0) + 1.0;
    const float distribution = alpha2 / (kPi * spread * spread);
    const float visibility =
        0.5 / (nl * sqrt(nv * nv * (1.0 - alpha2) + alpha2) + nv * sqrt(nl * nl * (1.0 - alpha2) + alpha2));
    const float specular =
        surface.anisotropy > 0.0 ? anisotropic(surface, toEye, toward, halfway, nl, nv, nh) : distribution * visibility;
    return ((1.0 - fresnel) * surface.diffuse / kPi + fresnel * specular) * nl;
}

// Of the light arriving from `toward`, per unit of illuminance, what leaves
// toward the eye, times the cosine it arrives at: glTF's metallic-roughness
// BRDF, the diffuse color's Lambert under Schlick's Fresnel, and GGX with
// Smith's height-correlated visibility; under a coat, as
// KHR_materials_clearcoat layers it, what the coat lets through of that
// and the coat's own lobe, each by the coat's Fresnel.
public float3 reflected(Surface surface, float3 normal, float3 toEye, float3 toward)
{
    const float3 under = beneath(surface, normal, toEye, toward);
    if (surface.coat <= 0.0)
    {
        return under;
    }
    const float3 halfway = normalize(toEye + toward);
    const float coatFresnel = kCoatHeadOn + (1.0 - kCoatHeadOn) * pow(1.0 - max(dot(toEye, halfway), 0.0), 5.0);
    const float coatLobe = dot(surface.coatNormal, toward) > 0.0
                               ? lobe(surface.coatNormal, toEye, toward, halfway, surface.coatRoughness)
                               : 0.0;
    return under * (1.0 - surface.coat * coatFresnel) + surface.coat * coatFresnel * coatLobe;
}

// Of the light arriving from `toward` behind a transmitting surface, per
// unit of illuminance, what passes through it toward the eye, as
// KHR_materials_transmission's sample renderer has it: the way to the
// light mirrored through the surface's plane, GGX's lobe about the normal
// at the roughness its index of refraction spreads, by what the Fresnel
// does not reflect, tinted, times the cosine the mirrored light arrives at.
public float3 passed(Surface surface, float3 normal, float3 toEye, float3 toward)
{
    const float behind = -dot(normal, toward);
    if (surface.transmission <= 0.0 || behind <= 0.0)
    {
        return float3(0.0);
    }
    const float3 mirrored = toward + 2.0 * behind * normal;
    const float3 halfway = normalize(mirrored + toEye);
    const float3 fresnel = surface.headOn + (1.0 - surface.headOn) * pow(1.0 - max(dot(toEye, halfway), 0.0), 5.0);
    const float spread = surface.roughness * clamp(surface.ior * 2.0 - 2.0, 0.0, 1.0);
    return surface.transmission * (1.0 - fresnel) * surface.tint * lobe(normal, toEye, mirrored, halfway, spread);
}
