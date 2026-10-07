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
// are.
public uint blobOf(uint material)
{
    uint count;
    uint stride;
    materials.GetDimensions(count, stride);
    return min(material, count / 9u - 1u) * 9u;
}

// A texture's channel a number is read from: one to four, red to alpha;
// nought for none, which reads one.
public float channelOf(float4 texel, float channel)
{
    return channel < 0.5 ? 1.0 : channel < 1.5 ? texel.r : channel < 2.5 ? texel.g : channel < 3.5 ? texel.b : texel.a;
}

// Where a material is evaluated: the mesh's texture coordinates and their
// derivatives across the pixel, taken before anything branches; the
// surface's normal, normalized, and its tangent with the bitangent's sign;
// the model's color; and the draw's material among the frame's.
public struct Point
{
    public float2 uv;
    public float2 dx;
    public float2 dy;
    public float3 normal;
    public float4 tangent;
    public float4 color;
    public uint material;
}

// What a material gives the lit shading model (ADR-0031's Surface, D299):
// the base color, the model's color applied; the metalness; the specular
// color times its weight; the roughness; the emission in nits; the index of
// refraction; the opacity, the model's alpha applied; the occlusion; the
// normal, bent as the material bends it; and whether it stands unlit.
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
    public bool unlit;
}

public interface IMaterial
{
    static Evaluated evaluate(Point point);
}
