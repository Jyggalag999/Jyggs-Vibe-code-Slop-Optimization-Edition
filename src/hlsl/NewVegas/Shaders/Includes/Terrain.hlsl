#if defined(__INTELLISENSE__)
    #include "Helpers.hlsl"
    #include "Pointlights.hlsl"
    #include "PBR.hlsl"
#else
    #include "includes/Pointlights.hlsl"
    #include "includes/PBR.hlsl"
#endif

float4 TESR_TerrainData : register(c89);
float4 TESR_TerrainExtraData : register(c90);

// Parallax port from Vegas Shaders (adeesababa/VegasShaders, GPL-3.0). x: IsoHeightTaps (Parallax.hlsl): the parallax height taps use
// one isotropic footprint (the pixel's shorter axis) instead of the real gradients. y: SkipEmptyLayers: blendDiffuseMaps and
// blendNormalMaps skip layers whose blend weight is exactly 0 (same picture, fewer taps). c94 is free in every land variant.
float4 TESR_TerrainParallaxIso : register(c94);

// SkipEmptyLayers: a layer with weight exactly 0 adds exactly 0, so its lookups are skipped. Tested != 0 (not > 0) so a slightly
// negative weight at an MSAA edge still adds its texel as with the option off. Inside a branch the lookups take explicit gradients
// (odx/ody = ddx/ddy of the same uv, taken at the top level: what tex2D used implicitly).
float3 blendDiffuseMaps(float3 vertexColor, float2 uv, float2 odx, float2 ody, int texCount, sampler2D tex[7], float blends[7]) {
    float3 color = float3(0, 0, 0);
    [unroll] for (int i = 0; i < texCount; i++) {
        bool wanted = blends[i] != 0 || TESR_TerrainParallaxIso.y < 0.5f;
        [branch] if (wanted) color += tex2Dgrad(tex[i], uv, odx, ody).xyz * blends[i];
    }

    return color * vertexColor;
}

float3 blendNormalMaps(float2 uv, float2 odx, float2 ody, int texCount, sampler2D tex[7], float blends[7], float spec[7], out float gloss, out float specExponent) {
    gloss = 0.0f;
    specExponent = 0.0f;

    float3 blendedNormal = float3(0, 0, 0);

    float4 normal;
    [unroll] for (int i = 0; i < texCount; i++) {
        bool wanted = blends[i] != 0 || TESR_TerrainParallaxIso.y < 0.5f;
        [branch] if (wanted) {
            normal = tex2Dgrad(tex[i], uv, odx, ody);
            blendedNormal += normal.xyz * blends[i];
            gloss += normal.w * blends[i] * (spec[i] > 0 ? 1.0f : 0.0f);
            specExponent += spec[i] * blends[i];
        }
    }

    gloss = saturate(gloss);
    return normalize(expand(blendedNormal));
}

float3 getVanillaLightingAtt(float3 lightDir, float att, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float gloss, float glossPower) {
    lightDir = normalize(lightDir);
    viewDir = normalize(viewDir);
    float3 halfwayDir = normalize(lightDir + viewDir);
    
    float NdotL = shades(normal.xyz, lightDir.xyz);
    
    float specStrength = gloss * pow(abs(shades(normal.xyz, halfwayDir.xyz)), glossPower);
    float3 lighting = albedo.rgb * NdotL * lightColor.rgb * att;
    lighting += saturate(((0.2 >= NdotL ? (specStrength * saturate(NdotL + 0.5)) : specStrength) * lightColor.rgb) * att);
    
    return lighting;
}

float3 getPointLightLighting(float3 lightDir, float att, float3 lightColor, float3 eyeDir, float3 normal, float3 albedo, float gloss = 0.0, float glossPower = 0.0, float metallicness = 1.0) {
    float3 pointlightColor = lightColor * TESR_TerrainData.z;

    [branch]
    if (TESR_TerrainExtraData.x){
        // PBR. 
        float roughness = saturate((1 - gloss) * TESR_TerrainData.y);
        float3 lighting = PBR(saturate(metallicness * TESR_TerrainData.x), roughness, albedo, normal, eyeDir, lightDir, pointlightColor);
        
        return max(0, lighting * att);
    } else {
        // Vanilla.    
        lightDir = normalize(lightDir);
        
        float3 lighting = getVanillaLightingAtt(lightDir, att, lightColor, eyeDir, normal, albedo, gloss, glossPower);
        
        return lighting;
    }
}

float3 getSunLighting(float3 lightDir, float3 sunColor, float3 eyeDir, float3 normal, float3 AmbientColor, float3 albedo, float gloss = 0.0, float glossPower = 0.0, float metallicness = 1.0, float parallaxMultiplier = 1.0) {
    float3 lightColor = sunColor * TESR_TerrainData.z * parallaxMultiplier;
    float3 ambientColor = AmbientColor * TESR_TerrainData.w;
    float3 color = albedo;
    color = lerp(luma(albedo), color, TESR_TerrainExtraData.y);

    [branch]
    if (TESR_TerrainExtraData.x) {
        // PBR.
        float roughness = saturate((1 - gloss) * TESR_TerrainData.y);
        float3 lighting = PBRSun(saturate(metallicness * TESR_TerrainData.x), roughness, color, normal, eyeDir, lightDir, lightColor);
        return max(0, lighting + ambientColor * color);
    } else {
        // Vanilla, no specular.
        float3 lighting = getVanillaLightingAtt(lightDir, 1.0, sunColor, eyeDir, normal, albedo, gloss, glossPower);
        
        return lighting + ambientColor * color;
    }
}
