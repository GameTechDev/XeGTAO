//------------------------------------------------------------------------------
// Punctual lights evaluation
//------------------------------------------------------------------------------

//#include "../SLC/SLCMain.hlsl"

#ifdef VA_RAYTRACING
#define RAYTRACED_SHADOWS
#endif

/**
 * Returns a Light structure (see common_lighting.fs) describing a spot light.
 * The colorIntensity field will store the *pre-exposed* intensity of the light
 * in the w component.
 *
 * The light parameters used to compute the Light structure are fetched from the
 * lightsUniforms uniform buffer.
 */
LightParams getSpotLight( const SurfaceInteraction surface, const ShadingParams shading, const ShaderLightPoint lightIn )//uint index ) 
{
    LightParams light = EvaluateLightAtSurface( lightIn, surface.WorldspacePos, shading.Normal );

#if !defined(RAYTRACED_SHADOWS)
    float shadowAttenuation = 1.0;
    [branch]
    if( light.Attenuation > 0 && lightIn.CubeShadowIndex >= 0 )
        shadowAttenuation *= ComputeCubeShadow( shading.CubeShadows, shading.GetWorldGeometricNormalVector(), lightIn.CubeShadowIndex, light.L, light.Dist, lightIn.Size, lightIn.Range );
    else    
    {
        float bentNoL = saturate( dot( shading.BentNormal, light.L ) );
        shadowAttenuation = min( shadowAttenuation, computeMicroShadowing( bentNoL, shading.DiffuseAmbientOcclusion ) );
    }
    light.Attenuation *= shadowAttenuation;
#endif

    return light;
}

/**
 * Evaluates all punctual lights that my affect the current fragment.
 * The result of the lighting computations is accumulated in the color
 * parameter, as linear HDR RGB.
 */
void evaluatePunctualLights( const SurfaceInteraction surface, const ShadingParams shading, const PixelParams pixel, inout float3 radiance )
{

#if 0
    [branch]
    if( DebugOnce() )
    {
        for( i = 0; i < g_lighting.LightCountPoint; i++ )
        {
            ShaderLightPoint lightRaw = g_lightsPoint[i];
            lightRaw.Intensity  *= g_globals.PreExposureMultiplier;
            DebugDraw3DLightViz( lightRaw.Position/*+g_globals.WorldBase.xyz*/, lightRaw.Direction, lightRaw.Size, lightRaw.Range, lightRaw.SpotInnerAngle, lightRaw.SpotOuterAngle, lightRaw.Color * lightRaw.Intensity );
        }
    }
#endif

#if 1
    // Iterate simple point lights
    for( int i = 0; i < g_lighting.LightCountPoint; i++ )
    {
        ShaderLightPoint lightRaw = g_lightsPoint[i];
        
        LightParams light = getSpotLight( surface, shading, lightRaw );

        [branch]
        if( light.Attenuation > 0 )
        {
            surfaceShading( shading, pixel, light, 1.0, radiance );
        }
    }

#else

    bool debugPixel = IsUnderCursor( surface.Position.xy );

    int lightcutNodes[SLC_MAX_LIGHT_SAMPLES];
    int lightcutNodeCount;
    const int maxLightSamples = 16; //SLC_MAX_LIGHT_SAMPLES;
    SLCSelectCut( surface.WorldspacePos.xyz + g_globals.WorldBase.xyz, shading.Normal, shading.View, lightcutNodes, lightcutNodeCount, maxLightSamples, debugPixel );
   
	RandomSequence rng;
    uint2 pixelPos = (uint2)surface.Position.xy;
	RandomSequence_Initialize( rng, g_globals.ViewportSize.x * pixelPos.y + pixelPos.x, 0/*maxLightSamples * frameId + passId*/ );
	rng.Type = 0;

    // [branch]
    // if( IsUnderCursor( surface.Position.xy ) )
    //     DebugWrite( uint4( lightcutNodeCount, 0, 0, 0 ) );

    // DebugAssert( !(debugPixel && surface.Position.x > 1900), surface.Position.x );

    const int numPasses = 2;
    for( int pass = 0; pass < numPasses; pass++ )
    {
        for( i = 0; i < lightcutNodeCount; i++ )
        {
            // [branch] if( debugPixel )
            // {
            //     DebugDraw3DText( surface.WorldspacePos.xyz, float2(0, 20 * i), float4( 1, 0.5, 0, 1 ), (uint)lightcutNodes[i] );
            // }

            ShaderLightPoint lightRaw = SLCGetLightFromNode( surface.WorldspacePos.xyz + g_globals.WorldBase.xyz, shading.Normal, shading.View, lightcutNodes[i], rng, debugPixel );
        
            lightRaw.Intensity  /= (float)numPasses;

            LightParams light = getSpotLight( surface, shading, lightRaw );

            // this is only a property of punctual lights: add it directly to diffuse here!
            SpecialEmissiveLight( surface, shading.PrecomputedEmissive, lightRaw, diffuseColor );

            [branch]
            if( light.Attenuation > 0 )
            {
                light.Attenuation *= computeMicroShadowing( light.NoL, shading.DiffuseAmbientOcclusion );
                surfaceShading( shading, pixel, light, 1.0, diffuseColor, specularColor );
            }
        }
        }

#if 0
    [branch] if( IsUnderCursor( surface.Position.xy ) )
    {
        DebugDraw2DLine( surface.Position.xy - float2(2,0), surface.Position.xy + float2(2,0), float4( 1, 0, 0, 1 ) );
        DebugDraw2DLine( surface.Position.xy - float2(0,2), surface.Position.xy + float2(0,2), float4( 0, 1, 0, 1 ) );
        DebugDraw2DCircle( surface.Position.xy, 10, float4( 1, 1, 1, 1 ) );
        DebugDraw2DRectangle( surface.Position.xy - float2(10,10), surface.Position.xy + float2(10,10), float4( 1, 0, 0, 1 ) );
        //DebugDraw3DSphere( surface.WorldspacePos.xyz, 0.1, float4( 1, 1, 0, 0.9 ) );
        DebugDraw3DArrow( surface.WorldspacePos.xyz, surface.WorldspacePos.xyz + shading.Normal, 0.06, float4( 0, 0.8, 0.8, 1.0 ) );
        DebugDraw3DText( surface.WorldspacePos.xyz, float2(0, 20), float4( 1, 0.5, 0, 1 ), float4( shading.Normal.xyz, 0 ) );

        //DebugDraw3DSphereCone( surface.WorldspacePos.xyz, shading.Normal, 0.5, 0.3, float4( 0, 0.8, 0.8, 1.0 ) );
    }
#endif

#endif

}

#ifdef VA_RAYTRACING

void ApplyLightRT( uint lightIndex, float invProbability, const SurfaceInteraction surface, const ShadingParams shading, const PixelParams pixel, const float3 nextRayOrigin, float ldSample1D, float2 ldSample2D, inout float3 directRadiance, const ShaderPathPayload rayPayload, const bool debugDraw )
{
    float2 disk = UniformSampleDisk( ldSample2D ); //ConcentricSampleDisk( u );

    ShaderLightPoint lightRaw = g_lightsPoint[lightIndex];
    lightRaw.Intensity  *= invProbability;

#ifdef RAYTRACED_SHADOWS   // raytraced visibility
    // disable shadow maps :)
    lightRaw.CubeShadowIndex = -1;
#endif

    LightParams light = getSpotLight( surface, shading, lightRaw );

    light.Attenuation *= computeMicroShadowing( light.NoL, shading.DiffuseAmbientOcclusion );

#ifdef RAYTRACED_SHADOWS
    float3 visibilityRayFrom    = nextRayOrigin;
    float3 visibilityRayTo      = lightRaw.Position;
    float3 visibilityRayDir     = visibilityRayTo - visibilityRayFrom;
    float visibilityRayLength   = length( visibilityRayDir );
    [branch]
    if( light.Attenuation > 0 && visibilityRayLength > 0 )
    {

        // sphere monte carlo sampling - works for visibility and (partial) direction only at the moment (see the manual update of light.L and light.NoL below)
        // this is a compromise between rasterized (analytical) and path traced version
        #if 1
        {
            visibilityRayDir /= visibilityRayLength;
            float3 tsX, tsY;
            ComputeOrthonormalBasis( visibilityRayDir, tsX, tsY );
            float3 disk3D = lightRaw.Position + (disk.x * tsX + disk.y * tsY - visibilityRayDir * max( 0, sqrt( 1-disk.x*disk.x-disk.y*disk.y) ) ) * (lightRaw.Size * lightRaw.RTSizeModifier);

            // update target (light) pos and dependencies for visibility testing
            visibilityRayTo     = disk3D;
            visibilityRayDir    = visibilityRayTo - visibilityRayFrom;
            visibilityRayLength = length( visibilityRayDir );

            // debugging code
            //for( int i = 0; i < 64; i++ )
            //{
            //    float2 disk = UniformSampleDisk(u); //ConcentricSampleDisk( u );
            //
            //    float3 disk3D = lightRaw.Position + (disk.x * tsX + disk.y * tsY - visibilityRayDir * max( 0, sqrt( 1-disk.x*disk.x-disk.y*disk.y) ) ) * lightRaw.Size;
            //
            //    if( debugDraw )
            //    {
            //        DebugDraw3DLine( surface.WorldspacePos, disk3D, float4(1,1,1,0.5) );
            //        DebugDraw3DLine( disk3D, disk3D + tsX * 0.02 * lightRaw.Size, float4(1,0,0,1) );
            //        DebugDraw3DLine( disk3D, disk3D + tsY * 0.02 * lightRaw.Size, float4(0,1,0,1) );
            //    }
            //}
        }
        #endif

        // (re)compute visibility ray params
        visibilityRayDir    /= visibilityRayLength;
        visibilityRayLength = max( 0, visibilityRayLength-lightRaw.Size );  // intentionally not using lightRaw.RTSizeModifier here - it's only supposed to rescale the disk

        // setup visibility ray DXR structure
        RayDesc visibilityRay;
        visibilityRay.Origin      = visibilityRayFrom;
        visibilityRay.Direction   = visibilityRayDir;
        visibilityRay.TMin        = 0.0;
        visibilityRay.TMax        = visibilityRayLength;

#if 1
        ShaderMultiPassRayPayload rayPayloadViz;
        rayPayloadViz.PathIndex         = 0;        // pass in 0 which means there was a ClosestHit; if miss, MissVisibility will set it to 1
        rayPayloadViz.ConeSpreadAngle   = rayPayload.ConeSpreadAngle;
        rayPayloadViz.ConeWidth         = rayPayload.ConeWidth;
        const uint missShaderIndex      = 1;    // visibility (primary) miss shader
        TraceRay( g_raytracingScene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, ~0, 0, 0, missShaderIndex, visibilityRay, rayPayloadViz );
        const bool visibilityRayMiss = rayPayloadViz.PathIndex != 0;
#else
        const bool visibilityRayMiss = true;
#endif

        // update these for more accurate/softer spherical integration when 
        light.L     = lerp( light.L, visibilityRayDir, 0.5 );   // <- meet half way there due to the way current art is tuned - this all goes out of the window in a future pass because it's hacky
        light.NoL   = saturate( dot( shading.Normal, light.L ) );

        if( debugDraw )
        {
            const float4 color = visibilityRayMiss?float4(1,1, 0, 1):float4(1,0,0,0.5);
            DebugDraw3DLine( visibilityRayFrom, visibilityRayFrom + visibilityRayDir * visibilityRayLength, color );
            //DebugDraw3DLightViz( lightRaw.Position, lightRaw.Direction, lightRaw.Size, lightRaw.Range, lightRaw.SpotInnerAngle, lightRaw.SpotOuterAngle, lightRaw.Color * lightRaw.Intensity );
            DebugDraw3DText( visibilityRayFrom + visibilityRayDir * (visibilityRayLength - lightRaw.Size), float2(0,0), color, float4( lightIndex, light.Attenuation, 0, lightRaw.Size ) );
        }

        [branch]
        if( visibilityRayMiss )
            surfaceShading( shading, pixel, light, 1.0, directRadiance );
    }

#else
    [branch]
    if( light.Attenuation > 0 )
        surfaceShading( shading, pixel, light, 1.0, directRadiance );
#endif
    // if( debugDraw )
    // {
    //     //DebugText( float4( lightRaw.Color * lightRaw.Intensity, light.Attenuation ) );
    //     //DebugText( float4( shading.Normal, 0 ) );
    //     //surface.DebugText();
    // }
}

void evaluatePunctualLightsRT( const SurfaceInteraction surface, const ShadingParams shading, const PixelParams pixel, const float3 nextRayOrigin, uint sampleIndex, uint pathHashSeed, float ldSample1D, float2 ldSample2D, inout float3 directRadiance, const ShaderPathPayload rayPayload, const bool debugDraw )
{
    const uint totalLights = g_lighting.LightCountPoint;
    const int treeDepth = g_lighting.LightTreeDepth;
    const int treeBottomLevelSize   = g_lighting.LightTreeBottomLevelSize;
    const int treeBottomLevelOffset = g_lighting.LightTreeBottomLevelOffset;

    //uint hashLocal = Hash32Combine( pathHashSeed, VA_RAYTRACING_HASH_SEED_LIGHTING_SPEC );
    //float localRand = LDSample1D( sampleIndex, hashLocal );

#if 0 // uniform monte carlo integration (best case performance)
    //float ldSample1D   = LDSample1D( sampleIndex*lightsToIntegrate + step, hashSeed1D );
    uint lightIndex = (uint)(ldSample1D * totalLights);
    float invProbability = totalLights;
    {
#elif 0 // importance sampling based only on intensity sum (slow)
    const float intensitySumAll = g_lightTree[1].IntensitySum;

    float nextRnd   = ldSample1D * intensitySumAll;
    float sumSoFar  = 0.0f;
    int lightIndex = totalLights;
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
    if( lightIndex < totalLights )
    {
#elif 0 // best case reference: importance sampling based on weight (super-slow)
    const float intensitySumAll = g_lightTree[1].IntensitySum;

    float weightSumAll = 0;
    for( int nodeIndex = treeBottomLevelOffset; nodeIndex < (treeBottomLevelOffset+treeBottomLevelSize); nodeIndex++ )
        weightSumAll += Material_EstimateLightWeight( g_lightTree[nodeIndex], surface, shading, pixel );

    float nextRnd   = ldSample1D * weightSumAll;
    float sumSoFar  = 0.0f;
    int lightIndex = totalLights;
    float invProbability = 0;
    for( nodeIndex = treeBottomLevelOffset; nodeIndex < (treeBottomLevelOffset+treeBottomLevelSize); nodeIndex++ )
    {
        float weight = Material_EstimateLightWeight( g_lightTree[nodeIndex], surface, shading, pixel );
        sumSoFar += weight;
        if( sumSoFar >= nextRnd )
        {
            lightIndex = nodeIndex - treeBottomLevelOffset;
            invProbability = 1.0 / (weight / weightSumAll);
            break;
        }
    }
    if( lightIndex < totalLights )
    {
#elif 1 // traverse that tree
    uint nodeIndex = 1;  // top node is 1 (0 is empty) - makes it convenient because children are always n*2+0 and n*2+1
    float probability = 1.0;
    const float weightNormalization = 0.08;        // give some random chance to 
    float localRand = ldSample1D;
    for( uint depth = 0; depth < treeDepth-1; depth++ )
    {
        nodeIndex *= 2; // move indexing to next level

        const ShaderLightTreeNode subNodeL = g_lightTree[nodeIndex+0];
        const ShaderLightTreeNode subNodeR = g_lightTree[nodeIndex+1];

        // maybe we could remove this branch at some point, but beware of not messing up probability
        if( subNodeR.IsDummy() )
            continue;

        float weightL = Material_EstimateLightWeight( subNodeL, surface, shading, pixel );
        float weightR = Material_EstimateLightWeight( subNodeR, surface, shading, pixel );
        float weightSum = weightL+weightR;
        //if( weightSum == 0 )
        //{
        //    DebugAssert(false, depth);
        //}

        float lr = saturate( weightL / (weightSum) );

        lr = lr * (1-weightNormalization*2) + weightNormalization;

        if( lr > localRand )           // '>' because if lr is zero, it guarantees that the 'right' path is taken and if lr is one it guarantees that the 'left path is taken - assuming localRand is [0, 1)
        {   // pick left path
            probability *= lr;
            nodeIndex = nodeIndex+0;     
            // this preserves sample's low discrepancy, as long as there's enough precision - and there should be, otherwise our probability is going wild and we have other problems too
            localRand = saturate(localRand/lr);             // assuming infinte precision, localRand should stay [0, 1) - it probably won't in practice, need to figure out if this is a problem
        }
        else
        {   // pick right path
            probability *= (1-lr);
            nodeIndex = nodeIndex+1;     
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
    if( lightIndex < totalLights )
    {
#else // ALL LIGHTS AT ONCE? is that even legal?
    float invProbability = 1;
    for( uint lightIndex = 0; lightIndex < totalLights; lightIndex++ )
    {
#endif
        ApplyLightRT( lightIndex, invProbability, surface, shading, pixel, nextRayOrigin, ldSample1D, ldSample2D, directRadiance, rayPayload, debugDraw );
    }
}

#endif // VA_RAYTRACING