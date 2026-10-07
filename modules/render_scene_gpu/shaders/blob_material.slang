// The engine's own material (D303, D482): the surface the draw's blob
// gives, its textures sampled where its flags and its packed channels say
// (D309 to D313). The scene is linked with it for the containers the
// engine ships.
import material;

struct BlobMaterial : IMaterial
{
    static Evaluated evaluate(Point point)
    {
        const uint at = blobOf(point.material);
        const float4 base = materials[at];
        const float4 specular = materials[at + 1u];
        const float4 emission = materials[at + 2u];
        const float4 rest = materials[at + 3u];
        // Where each texture is sampled (D311), and what the packed and
        // the emission textures feed (D312).
        const float4 baseMap = materials[at + 4u];
        const float4 packedMap = materials[at + 5u];
        const float4 emissionMap = materials[at + 6u];
        const float4 normalMap = materials[at + 7u];
        const float4 channels = materials[at + 8u];
        const uint flags = uint(rest.w);
        float4 sampled = float4(1.0);
        if ((flags & 6u) != 0u)
        {
            sampled = baseTexture.SampleGrad(
                baseSampler, point.uv * baseMap.xy + baseMap.zw, point.dx * baseMap.xy, point.dy * baseMap.xy);
        }
        float4 packedTexel = float4(1.0);
        if (channels.x + channels.y + channels.z > 0.5)
        {
            packedTexel = packedTexture.SampleGrad(packedSampler,
                                                   point.uv * packedMap.xy + packedMap.zw,
                                                   point.dx * packedMap.xy,
                                                   point.dy * packedMap.xy);
        }
        float3 glow = float3(1.0);
        if ((flags & 8u) != 0u)
        {
            glow = emissionTexture
                       .SampleGrad(emissionSampler,
                                   point.uv * emissionMap.xy + emissionMap.zw,
                                   point.dx * emissionMap.xy,
                                   point.dy * emissionMap.xy)
                       .rgb;
        }
        // The normal, bent by the normal texture across the tangent frame
        // (D313): its x and y scaled, glTF's green up the image.
        float3 normal = point.normal;
        if ((flags & 16u) != 0u)
        {
            float3 bent = normalTexture
                              .SampleGrad(normalSampler,
                                          point.uv * normalMap.xy + normalMap.zw,
                                          point.dx * normalMap.xy,
                                          point.dy * normalMap.xy)
                              .xyz *
                              2.0 -
                          1.0;
            bent.xy *= channels.w;
            const float3 across = normalize(point.tangent.xyz - normal * dot(normal, point.tangent.xyz));
            const float3 up = cross(normal, across) * point.tangent.w;
            normal = normalize(across * bent.x + up * bent.y + normal * bent.z);
        }
        Evaluated made;
        made.color = point.color.rgb * base.rgb * ((flags & 2u) != 0u ? sampled.rgb : float3(1.0));
        made.metalness = base.w * channelOf(packedTexel, channels.x);
        made.specular = specular.rgb;
        made.roughness = specular.w * channelOf(packedTexel, channels.y);
        made.glowing = emission.rgb * glow;
        made.ior = emission.w;
        // How much of what is behind it a translucent model hides (D305):
        // its material's opacity times its color's alpha.
        made.opacity = rest.x * point.color.a * ((flags & 4u) != 0u ? sampled.a : 1.0);
        made.occlusion = rest.y * channelOf(packedTexel, channels.z);
        made.normal = normal;
        // Unlit (KHR_materials_unlit).
        made.unlit = (flags & 1u) != 0u;
        return made;
    }
}

export struct Material : IMaterial = BlobMaterial;
