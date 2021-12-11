///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Handles point lights, meant to be included by the Material implementations (vaStandardPBR.hlsl, etc.) and requires
// access to material's BxDF.

// Also needs access to 
//    #include "vaLighting.hlsl"
//    #include "../Filament/ambient_occlusion.va.fs"


#ifdef VA_RAYTRACING
#define RAYTRACED_SHADOWS
#endif

#ifndef RAYTRACED_SHADOWS
void EvaluateShadowMap( const ShaderLightPoint lightRaw, const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface, inout LightParams light )
{
    uint cubeShadowIndex = lightRaw.Flags & VA_LIGHT_FLAG_CUBEMAP_MASK;
    float shadowAttenuation = 1.0;
    [branch]
    if( light.Attenuation > 0 && cubeShadowIndex != VA_LIGHT_FLAG_CUBEMAP_MASK )
        shadowAttenuation *= ComputeCubeShadow( materialSurface.CubeShadows, materialSurface.GetWorldGeometricNormalVector(), cubeShadowIndex, light.L, light.Dist, lightRaw.Radius, lightRaw.Range );
    else    
    {
        float bentNoL = saturate( dot( materialSurface.BentNormal, light.L ) );
        shadowAttenuation = min( shadowAttenuation, computeMicroShadowing( bentNoL, materialSurface.DiffuseAmbientOcclusion ) );
    }
    light.Attenuation *= shadowAttenuation;
}
#endif

void EvaluatePointLightsForward( const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface, inout float3 radiance, const bool debugPixel )
{

#if 0
    [branch]
    if( DebugOnce() )
    {
        for( i = 0; i < g_lighting.LightCountPoint; i++ )
        {
            ShaderLightPoint lightRaw = g_lightsPoint[i];
            lightRaw.Intensity  *= g_globals.PreExposureMultiplier;
            DebugDraw3DLightViz( lightRaw.Center/*+g_globals.WorldBase.xyz*/, lightRaw.Direction, lightRaw.Radius, lightRaw.Range, lightRaw.SpotInnerAngle, lightRaw.SpotOuterAngle, lightRaw.Color * lightRaw.Intensity );
        }
    }
#endif

    // Iterate simple point lights
    for( int i = 0; i < g_lighting.LightCountPoint; i++ )
    {
        ShaderLightPoint lightRaw = g_lightsPoint[i];

        LightParams light = EvaluateLightAtSurface( lightRaw, geometrySurface.WorldspacePos, materialSurface.Normal );
#ifndef RAYTRACED_SHADOWS
        EvaluateShadowMap( lightRaw, geometrySurface, materialSurface, light );
#endif

        [branch]
        if( light.Attenuation > 0 )
        {
            // float3 specularDominantDir = materialSurface.Reflected;
            // 
            // float3 L = light.L*light.Dist;
            // float3 centerToRay = dot( L, specularDominantDir ) * specularDominantDir - L;
            // float3 closestPoint = L + centerToRay * saturate( lightRaw.Radius / length(centerToRay) );
            // 
            // float3 specularWi = normalize( closestPoint );

            if( debugPixel && ((lightRaw.Flags & VA_LIGHT_FLAG_DEBUG_DRAW) != 0) )
            {
                //DebugDraw3DLightViz( lightRaw.Center/*+g_globals.WorldBase.xyz*/, lightRaw.Direction, lightRaw.Radius, lightRaw.Range, lightRaw.SpotInnerAngle, lightRaw.SpotOuterAngle, lightRaw.Color * lightRaw.Intensity );
                DebugDraw3DLine( geometrySurface.WorldspacePos, lightRaw.Center, float4( 0, 1, 1, 1.0 ) );
                // DebugDraw3DCone( geometrySurface.WorldspacePos, lightRaw.Center, light.SubtendedHalfAngle, float4( 0, 1, 1, 1.0 ) );

                // DebugDraw3DCone( geometrySurface.WorldspacePos, geometrySurface.WorldspacePos+materialSurface.Reflected, 0.01, float4( 1, 0, 0, 1.0 ) );
                // DebugDraw3DCone( lightRaw.Center, lightRaw.Center+centerToRay*3, 0.02, float4( 0, 1, 0, 1.0 ) );
                // DebugDraw3DSphere( geometrySurface.WorldspacePos+closestPoint, 0.5, float4( 0, 0, 1, 1.0 ) );
            }

            // const float lightDistance   = light.Dist;
            // const float lightRadius     = lightRaw.Radius;
            // float specularNormalization = pow( materialSurface.Roughness / clamp(materialSurface.Roughness + lightRadius / (2.0 * lightDistance), 0.0, 1.0), 2.0);

            radiance += Material_BxDF( materialSurface, light.L, light.Dist, lightRaw.Radius, false ) * light.ColorIntensity.rgb * (light.Attenuation);
        }
    }

}

#ifdef VA_RAYTRACING

// based on https://schuttejoe.github.io/post/arealightsampling/ 
void SampleSphereLight( const float3 surfacePos, ShaderLightPoint light, const float2 ldSample2D, const bool debugDraw, out float3 outDir, out float outDistance )
{
    float3 c = light.Center;
    float  r = light.Radius;

    float3 o = surfacePos;

    float3 w = c - o;
    float distanceToCenter = max( 1e-16, length( w ) );
    w /= distanceToCenter;

    float q = sqrt( 1.0 - (r / distanceToCenter) * (r / distanceToCenter) );    // <- optimize

    float3x3 toWorld = ComputeOrthonormalBasis( w );

    float r0 = ldSample2D.x;
    float r1 = ldSample2D.y;

    float theta = acos(1 - r0 + r0 * q);       // <- optimize? fast acos? maybe not?
    float phi   = 2 * VA_PI * r1;

    float3 local = SphericalToCartesian( phi, theta );
    float3 nwp = mul( local, toWorld );

    float t = IntersectRaySphere( o, nwp, c, r );   // can be optimized (we already have center-origin in 'w')
    if( t == 0 ) 
        t = r;  // not sure when/if this ever happens but account for it

    t = max( 0, t - light.ShadowRayShorten );

    float3 xp = o + nwp * t;

    // we don't need the normal currently
    float3 lightSurfaceNormal = xp - c;

    float hh = saturate( 1 - local.z*local.z*local.z );

    outDir      = nwp;
    outDistance = t;
}

// This is a weird-ish mix of classic old school attenuation done by EvaluateLightAtSurface and RT direct light sampling 
// I should rewrite it all based on https://pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Light_Sources, (see "\pbrt-v3\src\shapes\sphere.cpp"), but at the moment this will do.
NEESampleDesc SampleSinglePointLightDirectRT( uint lightIndex, float invProbability, const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface, const float ldSample1D, const float2 ldSample2D, bool debugDraw )
{
    NEESampleDesc retDesc = { {0,0,0}, 0, {0,0,0}, 1.0/invProbability };
    
    if( lightIndex >= g_lighting.LightCountPoint )
        return retDesc;
        
    ShaderLightPoint lightRaw = g_lightsPoint[lightIndex];

    debugDraw &= (lightRaw.Flags & VA_LIGHT_FLAG_DEBUG_DRAW) != 0;  // only display debugging info if enabled on the light itself, otherwise it overwhelms everything

    lightRaw.Intensity  *= invProbability;

    LightParams light = EvaluateLightAtSurface( lightRaw, geometrySurface.WorldspacePos, materialSurface.Normal );
    // Not sure what to do about this for now - microshadowing could make sense in raytracing scenarios for micro-crevices but AO term in 
    // most gltf models seems to be more large-scale, almost overlapping SSAO-scale and that doesn't make sense to use in path tracing.
    // light.Attenuation *= computeMicroShadowing( light.NoL, materialSurface.DiffuseAmbientOcclusion );

    [branch]
    if( light.Attenuation <= 0 )
        return retDesc;

    float3 visibilityRayFrom    = geometrySurface.WorldspacePos;
    float3 visibilityRayDir;
    float visibilityRayLength;

#if 0 // this could be a bit more expensive to leave in, so compiling it out
    if( debugDraw )
    {
        for( int i = 0; i < 64; i++ )
        {
            const float2 _ldSample2D = frac( ldSample2D + R2seq(i, 0) );
            SampleSphereLight( geometrySurface.WorldspacePos, lightRaw, _ldSample2D, debugDraw, visibilityRayDir, visibilityRayLength );
            if( debugDraw )
            {
                DebugDraw3DLine( geometrySurface.WorldspacePos, geometrySurface.WorldspacePos + visibilityRayDir*visibilityRayLength, float4( 1, 0.5, 0, 1 ) );
                DebugDraw3DSphere( geometrySurface.WorldspacePos + visibilityRayDir*visibilityRayLength, 0.02, float4( 0, 0, 1, 1 ) );
            }
        }
    }
#endif

    // Get ray direction
    SampleSphereLight( geometrySurface.WorldspacePos, lightRaw, ldSample2D, debugDraw, visibilityRayDir, visibilityRayLength );

    retDesc.Direction   = visibilityRayDir;
    retDesc.Distance    = visibilityRayLength;
    retDesc.Radiance    = Material_BxDF( materialSurface, visibilityRayDir, false ) * light.ColorIntensity.rgb * light.Attenuation;

    return retDesc;
}

struct LightSelection
{
    uint    Index;
    float   InvProbability;
};

LightSelection UniformSampleLights( const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface, uint sampleIndex, uint pathHashSeed, float ldSample1D, float2 ldSample2D, const bool debugDraw )
{
    LightSelection ret;
    ret.Index = (uint)(ldSample1D * g_lighting.LightCountPoint);
    ret.InvProbability = (float)g_lighting.LightCountPoint;
    return ret;
}

LightSelection IntensitySampleLights( const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface, uint sampleIndex, uint pathHashSeed, float ldSample1D, float2 ldSample2D, const bool debugDraw )
{
    LightSelection ret;
    const int treeBottomLevelSize   = g_lighting.LightTreeBottomLevelSize;
    const int treeBottomLevelOffset = g_lighting.LightTreeBottomLevelOffset;
    const float intensitySumAll = g_lightTree[1].IntensitySum;
    float nextRnd   = ldSample1D * intensitySumAll;
    float sumSoFar  = 0.0f;
    int lightIndex = g_lighting.LightCountPoint;
    float invProbability = 0;
    for( int nodeIndex = treeBottomLevelOffset; nodeIndex < (treeBottomLevelOffset+treeBottomLevelSize); nodeIndex++ )
    {
        sumSoFar += g_lightTree[nodeIndex].IntensitySum;
        if( sumSoFar >= nextRnd )
        {
            lightIndex = nodeIndex - treeBottomLevelOffset;
            invProbability = 1.0 / (g_lightTree[nodeIndex].IntensitySum / intensitySumAll);
            break;
        }
    }
    ret.Index = lightIndex;
    ret.InvProbability = invProbability;
    return ret;
}

LightSelection WeightTestSampleLights( const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface, uint sampleIndex, uint pathHashSeed, float ldSample1D, float2 ldSample2D, const bool debugDraw )
{
    LightSelection ret;
    const int treeBottomLevelSize   = g_lighting.LightTreeBottomLevelSize;
    const int treeBottomLevelOffset = g_lighting.LightTreeBottomLevelOffset;
    const float intensitySumAll = g_lightTree[1].IntensitySum;
    float weightSumAll = 0;
    for( int nodeIndex = treeBottomLevelOffset; nodeIndex < (treeBottomLevelOffset+treeBottomLevelSize); nodeIndex++ )
        weightSumAll += Material_WeighLight( g_lightTree[nodeIndex], geometrySurface, materialSurface );
    float nextRnd   = ldSample1D * weightSumAll;
    float sumSoFar  = 0.0f;
    int lightIndex = g_lighting.LightCountPoint;
    float invProbability = 0;
    for( nodeIndex = treeBottomLevelOffset; nodeIndex < (treeBottomLevelOffset+treeBottomLevelSize); nodeIndex++ )
    {
        float weight = Material_WeighLight( g_lightTree[nodeIndex], geometrySurface, materialSurface );
        sumSoFar += weight;
        if( sumSoFar >= nextRnd )
        {
            lightIndex = nodeIndex - treeBottomLevelOffset;
            invProbability = 1.0 / (weight / weightSumAll);
            break;
        }
    }
    ret.Index = lightIndex;
    ret.InvProbability = invProbability;
    return ret;
}

LightSelection TreeSampleLights( const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface, uint sampleIndex, uint pathHashSeed, float ldSample1D, float2 ldSample2D, const bool debugDraw )
{
    LightSelection ret;
    const int treeDepth = g_lighting.LightTreeDepth;
    const int treeBottomLevelSize   = g_lighting.LightTreeBottomLevelSize;
    const int treeBottomLevelOffset = g_lighting.LightTreeBottomLevelOffset;
    uint nodeIndex = 1;  // top node is 1 (0 is empty) - makes it convenient because children are always n*2+0 and n*2+1
    float probability = 1.0;
    const float weightNormalization = 0.08;        // give some random chance to 
    float localRand = ldSample1D;
    [loop]
    for( uint depth = 0; depth < treeDepth-1; depth++ )
    {
        nodeIndex *= 2; // move indexing to next level

        const ShaderLightTreeNode subNodeL = g_lightTree[nodeIndex+0];
        const ShaderLightTreeNode subNodeR = g_lightTree[nodeIndex+1];

        // maybe we could remove this branch at some point, but beware of not messing up probability
        [branch]
        if( subNodeR.IsDummy() )
            continue;

        float weightL = Material_WeighLight( subNodeL, geometrySurface, materialSurface );
        float weightR = Material_WeighLight( subNodeR, geometrySurface, materialSurface );
        float weightSum = weightL+weightR;
        //if( weightSum == 0 )
        //{
        //    DebugAssert(false, depth);
        //}

        float lr = saturate( weightL / (weightSum) );

        lr = lr * (1-weightNormalization*2) + weightNormalization;

        [branch]
        if( lr > localRand )           // '>' because if lr is zero, it guarantees that the 'right' path is taken and if lr is one it guarantees that the 'left path is taken - assuming localRand is [0, 1)
        {   // pick left path
            probability *= lr;
            nodeIndex = nodeIndex+0;     
            // recycle random number:
            // this preserves sample's low discrepancy, as long as there's enough precision - and there should be, otherwise our probability is going wild and we have other problems too
            localRand = saturate(localRand/lr);             // assuming infinte precision, localRand should stay [0, 1) - it probably won't in practice, need to figure out if this is a problem
        }
        else
        {   // pick right path
            probability *= (1-lr);
            nodeIndex = nodeIndex+1;     
            // recycle random number:
            // this preserves sample's low discrepancy, as long as there's enough precision - and there should be, otherwise our probability is going wild and we have other problems too
            localRand = saturate((localRand-lr)/(1-lr));    // assuming infinte precision, localRand should stay [0, 1) - it probably won't in practice, need to figure out if this is a problem
        }

        // if( lr < 0.001 || lr > 0.999 )
        //     DebugAssert( false, 0 );

        //if( debugDraw )
        //{
        //    DebugDraw2DText( float2( 500, 100 + 15 * depth), float4(1,0.2,0,1), hashLocal );
        //}
    }
    float invProbability = 1.0 / probability;
    uint lightIndex = nodeIndex - treeBottomLevelOffset;
    ret.Index = lightIndex;
    ret.InvProbability = invProbability;
    return ret;
}

// used for 'Next Event Estimation' - see https://developer.nvidia.com/blog/conquering-noisy-images-in-ray-tracing-with-next-event-estimation/ 
NEESampleDesc SamplePointLightsDirectRT( const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface, uint sampleIndex, uint pathHashSeed, float ldSample1D, float2 ldSample2D, const bool debugDraw )
{
    const uint totalLights = g_lighting.LightCountPoint;

#if   0 // uniform monte carlo integration (best case performance)
    LightSelection lightSel = UniformSampleLights( geometrySurface, materialSurface, sampleIndex, pathHashSeed, ldSample1D, ldSample2D, debugDraw );
#elif 0 // intensity importance sampling based (only on intensity sum, super-slow because I didn't pre-compute)
    LightSelection lightSel = IntensitySampleLights( geometrySurface, materialSurface, sampleIndex, pathHashSeed, ldSample1D, ldSample2D, debugDraw );
#elif 0 // best case reference: importance sampling based on weight (super-slow)
    LightSelection lightSel = WeightTestSampleLights( geometrySurface, materialSurface, sampleIndex, pathHashSeed, ldSample1D, ldSample2D, debugDraw );
#elif 0 // light tree
    LightSelection lightSel = TreeSampleLights( geometrySurface, materialSurface, sampleIndex, pathHashSeed, ldSample1D, ldSample2D, debugDraw );
#else
    LightSelection lightSel;

    switch( g_pathTracerConsts.LightSamplingMode )
    {
    case( 0 ): lightSel = UniformSampleLights( geometrySurface, materialSurface, sampleIndex, pathHashSeed, ldSample1D, ldSample2D, debugDraw ); break;
    case( 1 ): lightSel = IntensitySampleLights( geometrySurface, materialSurface, sampleIndex, pathHashSeed, ldSample1D, ldSample2D, debugDraw ); break;
    case( 2 ): lightSel = TreeSampleLights( geometrySurface, materialSurface, sampleIndex, pathHashSeed, ldSample1D, ldSample2D, debugDraw ); break;
    }
#endif

    return SampleSinglePointLightDirectRT( lightSel.Index, lightSel.InvProbability, geometrySurface, materialSurface, ldSample1D, ldSample2D, debugDraw );
}

#endif // VA_RAYTRACING

