// What a material is to the scene's shading (ADR-0031, SPEC-0026, D482):
// where it is evaluated, what it gives, and the interface a material
// meets, with the bindings every material may read. The scene declares
// its material `extern` and is linked with one module that exports it:
// the blob's (blob_material.slang), the engine's own, or a material
// compiled for a game (D481).
module material;

// Every material's blob (D303), nine vectors each: the base color and
// metalness; the specular color times its weight, and the roughness; the
// emission in nits, and the index of refraction; the opacity, the
// occlusion, the alpha cutoff, and its flags: one when unlit, two when its
// base texture's color multiplies the base color, four when its alpha
// multiplies the opacity, eight when the emission texture's color
// multiplies the emission, sixteen when the normal texture bends the
// normal; the base, packed, emission, and normal textures' scale and
// offset (D311); the packed texture's channels for the metalness, the
// roughness, and the occlusion, and the normal's scale (D312, D313).
[[vk::binding(9, 0)]] public StructuredBuffer<float4> materials;
// The texture the draw's material samples, and how (D309): white for one
// sampling none. The packed and the emission textures (D312), and the
// normal texture (D313).
[[vk::binding(10, 0)]] public Texture2D baseTexture;
[[vk::binding(11, 0)]] public SamplerState baseSampler;
[[vk::binding(12, 0)]] public Texture2D packedTexture;
[[vk::binding(13, 0)]] public SamplerState packedSampler;
[[vk::binding(14, 0)]] public Texture2D emissionTexture;
[[vk::binding(15, 0)]] public SamplerState emissionSampler;
[[vk::binding(16, 0)]] public Texture2D normalTexture;
[[vk::binding(17, 0)]] public SamplerState normalSampler;

// The first vector of material `material`'s blob, held to the blobs there
// are: nine vectors of its blob, then eight of its interface inputs' values
// (D708).
public uint blobOf(uint material)
{
    uint count;
    uint stride;
    materials.GetDimensions(count, stride);
    return min(material, count / 17u - 1u) * 17u;
}

// A texture's channel a number is read from: one to four, red to alpha;
// nought for none, which reads one.
public float channelOf(float4 texel, float channel)
{
    return channel < 0.5 ? 1.0 : channel < 1.5 ? texel.r : channel < 2.5 ? texel.g : channel < 3.5 ? texel.b : texel.a;
}

// The spaces a material's transform reads (D714): the eye in the World's
// axes; the model's rows, which place a point of the model's own relative
// to the eye; the view, which turns the World's axes to the eye's (x to
// its right, y up, z toward it); and the view and projection, unjittered,
// which take a point relative to the eye to the clip's.
public struct Spaces
{
    public float3 eye;
    public float4 model0;
    public float4 model1;
    public float4 model2;
    public float4x4 view;
    public float4x4 clip;
}

// How a transform takes what it is given: a point, placed by every part of
// a space; a vector, turned and scaled but never moved; or a normal, kept
// across the surface it is normal to and made a unit again (D714).
public static const uint kPoint = 0u;
public static const uint kVector = 1u;
public static const uint kNormal = 2u;

// The model's turn and scale, its rows, and their inverse.
float3x3 linearOf(Spaces spaces)
{
    return float3x3(spaces.model0.xyz, spaces.model1.xyz, spaces.model2.xyz);
}

float3x3 inverseOf(float3x3 rows)
{
    const float3 a = cross(rows[1], rows[2]);
    const float3 b = cross(rows[2], rows[0]);
    const float3 c = cross(rows[0], rows[1]);
    return transpose(float3x3(a, b, c)) / dot(rows[0], a);
}

// From the model's own axes to the World's.
public float3 fromObject(Spaces spaces, float3 value, uint kind)
{
    const float3x3 rows = linearOf(spaces);
    if (kind == kNormal)
    {
        return normalize(mul(transpose(inverseOf(rows)), value));
    }
    const float3 turned = mul(rows, value);
    return kind == kPoint
               ? turned + float3(spaces.model0.w, spaces.model1.w, spaces.model2.w) + spaces.eye
               : turned;
}

// From the World's axes to the model's own.
public float3 toObject(Spaces spaces, float3 value, uint kind)
{
    const float3x3 rows = linearOf(spaces);
    if (kind == kNormal)
    {
        return normalize(mul(transpose(rows), value));
    }
    const float3 moved =
        kind == kPoint ? value - spaces.eye - float3(spaces.model0.w, spaces.model1.w, spaces.model2.w) : value;
    return mul(inverseOf(rows), moved);
}

// From the eye's axes to the World's: the view only turns.
public float3 fromView(Spaces spaces, float3 value, uint kind)
{
    const float3 turned = mul(float4(value, 0.0), spaces.view).xyz;
    return kind == kPoint ? turned + spaces.eye : kind == kNormal ? normalize(turned) : turned;
}

// From the World's axes to the eye's.
public float3 toView(Spaces spaces, float3 value, uint kind)
{
    const float3 turned = mul(spaces.view, float4(kind == kPoint ? value - spaces.eye : value, 0.0)).xyz;
    return kind == kNormal ? normalize(turned) : turned;
}

// From the surface's tangent frame (its tangent, bitangent, and normal) to
// the World's axes, and back: vectors and normals alone, where a point has
// no place.
float3x3 tangentFrameOf(float3 normal, float4 tangent)
{
    const float3 across = normalize(tangent.xyz - normal * dot(normal, tangent.xyz));
    return float3x3(across, cross(normal, across) * tangent.w, normal);
}

public float3 fromTangent(float3 normal, float4 tangent, float3 value, uint kind)
{
    const float3 turned = mul(value, tangentFrameOf(normal, tangent));
    return kind == kNormal ? normalize(turned) : turned;
}

public float3 toTangent(float3 normal, float4 tangent, float3 value, uint kind)
{
    const float3 turned = mul(tangentFrameOf(normal, tangent), value);
    return kind == kNormal ? normalize(turned) : turned;
}

// A point in the World's axes to the clip's, before the divide.
public float4 toClip(Spaces spaces, float3 value)
{
    return mul(spaces.clip, float4(value - spaces.eye, 1.0));
}

// Where a material is evaluated: the mesh's texture coordinates and their
// derivatives across the pixel, taken before anything branches; the
// surface's normal, normalized, and its tangent with the bitangent's sign;
// the model's color; the draw's material among the frame's; the point in
// the World's axes, and the presented World's time in seconds (D691), its
// whole seconds and the fraction left (D702); the way to the eye from it,
// a unit vector (D696); whether the face drawn is its front (D705); and
// the spaces a transform reads (D714).
public struct Point
{
    public float2 uv;
    public float2 dx;
    public float2 dy;
    public float3 normal;
    public float4 tangent;
    public float4 color;
    public uint material;
    public float3 position;
    public float2 time;
    public float3 view;
    public bool front;
    public Spaces spaces;
}

// Where a material moves a vertex (SPEC-0026's position offset, D691): the
// vertex placed by its model in the World's axes, its normal there, its
// texture coordinates, the model's color, the presented World's time in
// seconds, now or the frame before, its whole seconds and the fraction left
// (D702), the way to the eye, a unit vector (D696), the draw's material
// among the frame's, whose parameters it reads (D708), and the spaces a
// transform reads (D714).
public struct Placed
{
    public float3 position;
    public float3 normal;
    public float2 uv;
    public float4 color;
    public float2 time;
    public float3 view;
    public uint material;
    public Spaces spaces;
}

// What a material gives the lit shading model (ADR-0031's Surface, D299):
// the base color, the model's color applied; the metalness; the specular
// color times its weight; the roughness; the emission in nits; the index of
// refraction; the opacity, the model's alpha applied; the occlusion; the
// normal, bent as the material bends it; the coat's weight, its roughness,
// and its normal (KHR_materials_clearcoat, D716), a weight of nought for
// none; and whether it stands unlit.
public struct Evaluated
{
    public float3 color;
    public float metalness;
    public float3 specular;
    public float roughness;
    public float3 glowing;
    public float ior;
    public float opacity;
    public float occlusion;
    public float3 normal;
    public float coat;
    public float coatRoughness;
    public float3 coatNormal;
    public bool unlit;
}

// What a material gives where it is evaluated, and how far it moves a
// vertex, in the World's axes: nought for a material that moves none.
public interface IMaterial
{
    static Evaluated evaluate(Point point);
    static float3 offset(Placed placed);
}
