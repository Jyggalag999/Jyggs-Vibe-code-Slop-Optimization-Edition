#ifdef TERRAIN
float4 TESR_TerrainParallaxData : register(c91);
float4 TESR_TerrainParallaxExtraData : register(c92);
// TESR_TerrainParallaxIso (IsoHeightTaps, SkipEmptyLayers): declared in Terrain.hlsl, which every land template includes first.
#else
float4 TESR_ParallaxData : register(c35);
#endif

#ifdef TERRAIN
// Complex Parallax Materials for Community Shaders
// https://bartwronski.com/wp-content/uploads/2014/03/ac4_gdc.pdf
// https://www.artstation.com/blogs/andreariccardi/3VPo/a-new-approach-for-parallax-mapping-presenting-the-contact-refinement-parallax-mapping-technique
float getTerrainHeight(float2 coords, float2 dx, float2 dy, float blendFactor, int texCount, sampler2D tex[7], float blends[7], float status[7], out float weights[7]) {
    weights = blends;

    float blendPower = blendFactor * 4;
    float total = 0;
    [unroll] for (int i = 0; i < texCount; i++){
        weights[i] = pow(abs(blends[i]), 1 + 1 * blendFactor);
        if (weights[i] > 0.0) {
            weights[i] *= 0.001 + pow(abs(status[i] ? tex2Dgrad(tex[i], coords, dx, dy).a : 0.5f), blendPower);
        }
        total += weights[i];
    }
    
    float invtotal = rcp(total);
	
    [unroll] for (i = 0; i < texCount; i++)
    {
        weights[i] *= invtotal;
    }
    
    return pow(total, rcp(blendPower));
}
#endif

// ParallaxLite's secant steps (terrain and objects), after Vegas Shaders (adeesababa/VegasShaders, GPL-3.0): 1 step costs about 1.5x
// the picture difference of HighQuality off for -17..-31% time.
#ifndef LITE_SECANT_STEPS
#define LITE_SECANT_STEPS 1
#endif

#ifndef TERRAIN
// ParallaxLite on objects (TESR_ParallaxData.w = 1): getParallaxCoords' search cut down: up to 8 steps in batches of four, then
// LITE_SECANT_STEPS secant steps in place of the contact refinement. Same start shift, bounds and final interpolation.
float2 objectLiteParallax(float viewDist, float2 uv0, float2 gx, float2 gy, float3 eyeTS, sampler2D hmap) {
    float farness = saturate(viewDist / 2048.0f);
    if (farness >= 1.0) return uv0;

    eyeTS = normalize(eyeTS);
    float fullDepth = 0.1 * TESR_ParallaxData.x;
    float halfDepth = fullDepth * 0.5;

    int stepsLeft = int((8.0f * (1.0 - farness)) + 0.5);
    stepsLeft = clamp(((stepsLeft + 3) / 4) * 4, 4, 8);
    float layerH = rcp((float) stepsLeft);
    float2 uvPerLayer = eyeTS.xy * fullDepth * layerH;
    float2 lastUV = eyeTS.xy * halfDepth + uv0.xy;
    float lastRay = 1.0;
    float lastSurf = 1.0;
    float2 ptHit = 0;
    float2 ptPrev = 0;
    bool hit = false;

    [loop] while (stepsLeft > 0 && !hit) {
        float4 tapUV[2];
        tapUV[0] = lastUV.xyxy - float4(1, 1, 2, 2) * uvPerLayer.xyxy;
        tapUV[1] = lastUV.xyxy - float4(3, 3, 4, 4) * uvPerLayer.xyxy;
        float4 rayAt = lastRay.xxxx - float4(1, 2, 3, 4) * layerH;
        float4 surfAt;
        surfAt.x = tex2Dgrad(hmap, tapUV[0].xy, gx, gy).r;
        surfAt.y = tex2Dgrad(hmap, tapUV[0].zw, gx, gy).r;
        surfAt.z = tex2Dgrad(hmap, tapUV[1].xy, gx, gy).r;
        surfAt.w = tex2Dgrad(hmap, tapUV[1].zw, gx, gy).r;
        bool4 hits = surfAt >= rayAt;
        [branch] if (any(hits)) {
            [flatten] if (hits.w) { ptHit = float2(rayAt.w, surfAt.w); ptPrev = float2(rayAt.z, surfAt.z); }
            [flatten] if (hits.z) { ptHit = float2(rayAt.z, surfAt.z); ptPrev = float2(rayAt.y, surfAt.y); }
            [flatten] if (hits.y) { ptHit = float2(rayAt.y, surfAt.y); ptPrev = float2(rayAt.x, surfAt.x); }
            [flatten] if (hits.x) { ptHit = float2(rayAt.x, surfAt.x); ptPrev = float2(lastRay, lastSurf); }
            hit = true;
        }
        else {
            lastUV = tapUV[1].zw;
            lastRay = rayAt.w;
            lastSurf = surfAt.w;
            stepsLeft -= 4;
        }
    }

    [branch] if (hit) {
        [unroll] for (int r = 0; r < LITE_SECANT_STEPS; r++) {
            float gapA = ptPrev.x - ptPrev.y;
            float gapB = ptHit.x - ptHit.y;
            float gapSpan = gapA - gapB;
            float xing = gapSpan == 0.0 ? ptHit.x : (ptHit.x * gapA - ptPrev.x * gapB) / gapSpan;
            float xingSurf = tex2Dgrad(hmap, eyeTS.xy * ((1.0 - xing) * -fullDepth + halfDepth) + uv0.xy, gx, gy).r;
            if (xingSurf >= xing) ptHit = float2(xing, xingSurf);
            else ptPrev = float2(xing, xingSurf);
        }
    }

    float gapAbove = ptPrev.x - ptPrev.y;
    float gapBelow = ptHit.x - ptHit.y;
    float span = gapAbove - gapBelow;
    float depthT = span == 0.0 ? 0.0 : (ptHit.x * gapAbove - ptPrev.x * gapBelow) / span;
    float shift = (1.0 - depthT) * -fullDepth + halfDepth;
    // The bumps fade as before (1 - d^2) up to 3/4 of the view distance, then ease out to nothing (Vegas Shaders).
    const float strength = (1.0 - farness * farness) * (1.0 - smoothstep(0.75, 1.0, farness));
    return lerp(uv0, eyeTS.xy * shift + uv0.xy, strength);
}
#endif

#ifdef TERRAIN
float2 getParallaxCoords(
    float distance, 
    float2 coords, 
    float2 dx, 
    float2 dy, 
    float3 viewDirTS, 
    int texCount, 
    sampler2D tex[7], 
    float blends[7], 
    float status[7], 
    out float weights[7]
) {
#else
float2 getParallaxCoords(float distance, float2 coords, float2 dx, float2 dy, float3 viewDirTS, sampler2D heightMap) {
#endif
    #ifdef TERRAIN
        // Check if parallax is active first.
        if (!TESR_TerrainParallaxData.x) {
            weights = blends;
            return coords;
        }
    
        // Variables.
        // TESR_TerrainParallaxData.w: 0 = 8 steps, 1 = 16 steps (HighQuality), 2 = ParallaxLite (8 steps, secant step instead of
        // the contact refinement, two shadow taps instead of four).
        static const bool liteMode = TESR_TerrainParallaxData.w > 1.5f;
        static const float maxSteps = (TESR_TerrainParallaxData.w == 1.0f) ? 16.0f : 8.0f;
        float maxDistance = TESR_TerrainParallaxExtraData.x;
        float height = TESR_TerrainParallaxExtraData.y;
    #else
        // ParallaxLite is a separate function so the default path compiles exactly as before.
        [branch] if (TESR_ParallaxData.w > 0.5f) return objectLiteParallax(distance, coords, dx, dy, viewDirTS, heightMap);

        static const float maxSteps = 16.0f;
        float maxDistance = 2048;
        float height = 0.1 * TESR_ParallaxData.x;
    #endif
    
    float distanceBlend = saturate(distance / maxDistance);
    float quality = saturate(1.0 - distanceBlend);
    
    #ifdef TERRAIN
        float blendFactor = TESR_TerrainParallaxData.z ? quality : 0.25;
    #endif

    viewDirTS = normalize(viewDirTS);

    #ifdef TERRAIN
        // IsoHeightTaps (Vegas Shaders): only the height taps below see these gradients.
        [branch] if (TESR_TerrainParallaxIso.x > 0.5f) { float L = sqrt(min(dot(dx, dx), dot(dy, dy))); dx = float2(L, 0); dy = float2(0, L); }
        // Fix for angles.
        viewDirTS.z = ((viewDirTS.z * 0.7) + 0.3);
        viewDirTS.xy /= viewDirTS.z;
    #endif

    float maxHeight = height;
    float minHeight = maxHeight * 0.5;

    float2 output;
    if (distanceBlend < 1.0)
    {
        int numSteps = int((maxSteps * (1.0 - distanceBlend)) + 0.5);
        numSteps = ((numSteps + 3) / 4) * 4;
        numSteps = clamp(numSteps, 4, maxSteps);

        float stepSize = rcp((float) numSteps);

        float2 offsetPerStep = viewDirTS.xy * float2(maxHeight, maxHeight) * stepSize.xx;
        float2 prevOffset = viewDirTS.xy * float2(minHeight, minHeight) + coords.xy;

        float prevBound = 1.0;
        float prevHeight = 1.0;

        float2 pt1 = 0;
        float2 pt2 = 0;
        
        int numStepOrig = numSteps;
        bool done = false;
        bool contactRefinement = false;

        // Need fastopt otherwise compile times are crazy.
        [loop][fastopt] while (numSteps > 0 && !done) {
            float4 currentOffset[2];
            currentOffset[0] = prevOffset.xyxy - float4(1, 1, 2, 2) * offsetPerStep.xyxy;
            currentOffset[1] = prevOffset.xyxy - float4(3, 3, 4, 4) * offsetPerStep.xyxy;
            float4 currentBound = prevBound.xxxx - float4(1, 2, 3, 4) * stepSize;

            float4 currHeight;
            
            #ifdef TERRAIN
                currHeight.x = getTerrainHeight(currentOffset[0].xy, dx, dy, blendFactor, texCount, tex, blends, status, weights);
                currHeight.y = getTerrainHeight(currentOffset[0].zw, dx, dy, blendFactor, texCount, tex, blends, status, weights);
                currHeight.z = getTerrainHeight(currentOffset[1].xy, dx, dy, blendFactor, texCount, tex, blends, status, weights);
                currHeight.w = getTerrainHeight(currentOffset[1].zw, dx, dy, blendFactor, texCount, tex, blends, status, weights);
            #else
                currHeight.x = tex2Dgrad(heightMap, currentOffset[0].xy, dx, dy).r;
                currHeight.y = tex2Dgrad(heightMap, currentOffset[0].zw, dx, dy).r;
                currHeight.z = tex2Dgrad(heightMap, currentOffset[1].xy, dx, dy).r;
                currHeight.w = tex2Dgrad(heightMap, currentOffset[1].zw, dx, dy).r;
            #endif

            bool4 testResult = currHeight >= currentBound;

            [branch] if (any(testResult))
            {
                float2 lastOffset = 0;
                [flatten] if (testResult.w)
                {
                    lastOffset = currentOffset[1].xy;
                    pt1 = float2(currentBound.w, currHeight.w);
                    pt2 = float2(currentBound.z, currHeight.z);
                }
                [flatten] if (testResult.z)
                {
                    lastOffset = currentOffset[0].zw;
                    pt1 = float2(currentBound.z, currHeight.z);
                    pt2 = float2(currentBound.y, currHeight.y);
                }
                [flatten] if (testResult.y)
                {
                    lastOffset = currentOffset[0].xy;
                    pt1 = float2(currentBound.y, currHeight.y);
                    pt2 = float2(currentBound.x, currHeight.x);
                }
                [flatten] if (testResult.x)
                {
                    lastOffset = prevOffset;
                    pt1 = float2(currentBound.x, currHeight.x);
                    pt2 = float2(prevBound, prevHeight);
                }
                
                #ifdef TERRAIN
                if (contactRefinement || liteMode) {
                #else
                if (contactRefinement) {
                #endif
                    done = true;
                }
                else {
                    contactRefinement = true;
                    prevOffset = lastOffset;
                    prevBound = pt2.x;
                    numSteps = numStepOrig;
                    stepSize /= (float) numSteps;
                    offsetPerStep /= (float) numSteps;
                }
            }
            else {
                prevOffset = currentOffset[1].zw;
                prevBound = currentBound.w;
                prevHeight = currHeight.w;
                numSteps -= 4;
            }
        }
        
        #ifdef TERRAIN
        // ParallaxLite: in place of the contact refinement, secant steps: evaluate the height where the line between the bracket's
        // two samples crosses the ray and keep the half that still brackets the hit.
        [branch] if (liteMode && done) {
            [loop] for (int r = 0; r < LITE_SECANT_STEPS; r++) {
                float gapA = pt2.x - pt2.y;
                float gapB = pt1.x - pt1.y;
                float gapSpan = gapA - gapB;
                float xing = gapSpan == 0.0 ? pt1.x : (pt1.x * gapA - pt2.x * gapB) / gapSpan;
                float2 xingUV = viewDirTS.xy * ((1.0 - xing) * -maxHeight + minHeight) + coords.xy;
                float xingSurf = getTerrainHeight(xingUV, dx, dy, blendFactor, texCount, tex, blends, status, weights);
                if (xingSurf >= xing) pt1 = float2(xing, xingSurf);
                else pt2 = float2(xing, xingSurf);
            }
        }
        #endif

        float delta2 = pt2.x - pt2.y;
        float delta1 = pt1.x - pt1.y;

        float denominator = delta2 - delta1;

        float parallaxAmount = 0.0;
        if (denominator == 0.0)
        {
            parallaxAmount = 0.0;
        }
        else
        {
            parallaxAmount = (pt1.x * delta2 - pt2.x * delta1) / denominator;
        }
        
        #ifdef TERRAIN
        // ParallaxLite: ease out to nothing at the cut-off (Vegas Shaders).
        [branch] if (liteMode) {
            float offsetL = (1.0 - parallaxAmount) * -maxHeight + minHeight;
            const float strength = (1.0 - distanceBlend * distanceBlend) * (1.0 - smoothstep(0.75, 1.0, distanceBlend));
            return lerp(coords, viewDirTS.xy * offsetL + coords.xy, strength);
        }
        #endif

        distanceBlend *= distanceBlend;
        
        float offset = (1.0 - parallaxAmount) * -maxHeight + minHeight;
        return lerp(viewDirTS.xy * offset + coords.xy, coords, distanceBlend);
    }
    
    #ifdef TERRAIN
        weights = blends;
    #endif
    return coords;
}

#ifdef TERRAIN
float getParallaxShadowMultipler(float distance, float2 coords, float2 dx, float2 dy, float3 lightTS, int texCount, float blends[7], float status[7], sampler2D tex[7]) {
    if (!TESR_TerrainParallaxData.y)
        return 1.0;
    
    float maxDistance = TESR_TerrainParallaxExtraData.x;
    float shadowsIntensity = TESR_TerrainParallaxExtraData.z;
    
    float quality = 1.0 - distance / maxDistance;
    
    if (quality > 0.0)
    {
        [branch] if (TESR_TerrainParallaxIso.x > 0.5f) { float L = sqrt(min(dot(dx, dx), dot(dy, dy))); dx = float2(L, 0); dy = float2(0, L); }  // IsoHeightTaps
        float weights[7] = { 0, 0, 0, 0, 0, 0, 0 };
        float sh0 = getTerrainHeight(coords, dx, dy, quality, texCount, tex, blends, status, weights);

        const float2 rayDir = lightTS.xy * 0.1;
        float4 multipliers = rcp((float4(1, 2, 3, 4)));

        float4 sh = getTerrainHeight(coords + rayDir * multipliers.x, dx, dy, quality, texCount, tex, blends, status, weights);
        // ParallaxLite (TESR_TerrainParallaxData.w = 2): two taps, at 1 and 1/2 of the ray, each counted twice so the four-tap sum
        // keeps its scale. The second tap fades in between quality 0.15 and 0.35, and the last quarter of the distance eases out.
        [branch] if (TESR_TerrainParallaxData.w > 1.5f) {
            if (quality > 0.15) {
                const float h2 = getTerrainHeight(coords + rayDir * multipliers.y, dx, dy, quality, texCount, tex, blends, status, weights);
                sh.yw = lerp(sh.xx, h2.xx, saturate((quality - 0.15) / 0.2));
            }
            return 1.0 - saturate(dot(max(0, sh - sh0), 1.0) * shadowsIntensity) * quality * smoothstep(0.0, 0.25, quality);
        }
        if (quality > 0.25)
            sh.y = getTerrainHeight(coords + rayDir * multipliers.y, dx, dy, quality, texCount, tex, blends, status, weights);
        if (quality > 0.5)
            sh.z = getTerrainHeight(coords + rayDir * multipliers.z, dx, dy, quality, texCount, tex, blends, status, weights);
        if (quality > 0.75)
            sh.w = getTerrainHeight(coords + rayDir * multipliers.w, dx, dy, quality, texCount, tex, blends, status, weights);
        
        return 1.0 - saturate(dot(max(0, sh - sh0), 1.0) * shadowsIntensity) * quality;
    }
    
    return 1.0;
}
#else
float getParallaxShadowMultipler(float distance, float2 coords, float2 dx, float2 dy, float3 lightTS, sampler2D heightMap) {
    float maxDistance = 2048;
    float shadowsIntensity = 2;
    
    float quality = 1.0 - distance / maxDistance;
    
    if (quality > 0.0)
    {
        float sh0 = tex2Dgrad(heightMap, coords, dx, dy).r;

        const float2 rayDir = lightTS.xy * 0.04;
        float4 multipliers = rcp((float4(1, 2, 3, 4)));

        float4 sh = tex2Dgrad(heightMap, coords + rayDir * multipliers.x, dx, dy).r;
        if (quality > 0.25)
            sh.y = tex2Dgrad(heightMap, coords + rayDir * multipliers.y, dx, dy).r;
        if (quality > 0.5)
            sh.z = tex2Dgrad(heightMap, coords + rayDir * multipliers.z, dx, dy).r;
        if (quality > 0.75)
            sh.w = tex2Dgrad(heightMap, coords + rayDir * multipliers.w, dx, dy).r;
        
        return 1.0 - saturate(dot(max(0, sh - sh0), 1.0) * shadowsIntensity) * quality;
    }
    
    return 1.0;
}
#endif
