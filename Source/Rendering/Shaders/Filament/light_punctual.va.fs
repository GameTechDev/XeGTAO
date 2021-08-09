//------------------------------------------------------------------------------
// Punctual lights evaluation
//------------------------------------------------------------------------------

#include "../SLC/SLCMain.hlsl"

#ifdef VA_RAYTRACING
#define RAYTRACED_SHADOWS
#endif

float getSquareFalloffAttenuation( float distanceSquare, float range )
{
    // falloff looks like this: https://www.desmos.com/calculator/ej4tpxmaco (for factor^2) - if link doesn't work set "k" to factor and graph to "\max\left(0,\ \min\left(1,\ 1-\left(x^2\cdot k\right)^3\right)\right)^2" 
    float falloff = 1.0 / (range * range);
    float factor = distanceSquare * falloff;
    float smoothFactor = saturate(1.0 - factor * factor * factor); // modded to factor^3 instead of factor^2 for a bit sharper falloff
    // We would normally divide by the square distance here
    // but we do it at the call site
    return smoothFactor * smoothFactor;
}

float getDistanceAttenuation( const float distance, const float distanceSquare, const float range, const float size )
{
    float attenuation = getSquareFalloffAttenuation( distanceSquare, range );

#if 0 // old Vanilla approach
    return attenuation * 1.0 / max( distanceSquare, size*size );
#else // new approach - from http://www.cemyuksel.com/research/pointlightattenuation/ 
    float sizeSquare = size * size;
    return attenuation * 2.0 / (distanceSquare + sizeSquare + distance * sqrt(distanceSquare+sizeSquare) );
#endif
}

float getAngleAttenuation( const vec3 lightDir, const vec3 l, const vec2 scaleOffset )
{
    float cd = dot(lightDir, l);
    float attenuation  = saturate(cd * scaleOffset.x + scaleOffset.y);
    return attenuation * attenuation;
}

/**
 * Light setup common to point and spot light. This function sets the light vector
 * "l" and the attenuation factor in the Light structure. The attenuation factor
 * can be partial: it only takes distance attenuation into account. Spot lights
 * must compute an additional angle attenuation.
 */
LightParams setupPunctualLight( const SurfaceInteraction surface, const ShadingParams shading, const ShaderLightPoint lightIn )//, const highp vec4 positionFalloff )
{
    LightParams light;

    highp float3 worldPosition = surface.WorldspacePos.xyz;
    highp float3 posToLight = lightIn.Position - worldPosition;
    float distanceSquare = dot( posToLight, posToLight );
    light.Dist = sqrt( distanceSquare );
    light.L = posToLight / light.Dist; // normalize(posToLight); // TODO: just use rsq(distanceSquare)*posToLight
    light.Attenuation = getDistanceAttenuation( light.Dist, distanceSquare, lightIn.Range, lightIn.Size );
    light.NoL = saturate( dot( shading.Normal, light.L ) );

    light.ColorIntensity    = float4( lightIn.Color, lightIn.Intensity );

    return light;
}

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
    LightParams light = setupPunctualLight( surface, shading, lightIn );

    // ivec2 texCoord = getRecordTexCoord(index);
    // uint lightIndex = texelFetch(light_records, texCoord, 0).r;
    // 
    // highp vec4 positionFalloff = lightsUniforms.lights[lightIndex][0];
    // highp vec4 colorIntensity  = lightsUniforms.lights[lightIndex][1];
    //       vec4 directionIES    = lightsUniforms.lights[lightIndex][2];
    //       vec2 scaleOffset     = lightsUniforms.lights[lightIndex][3].xy;

#if defined(MATERIAL_CAN_SKIP_LIGHTING)
    [branch]
    if (light.NoL <= 0.0) 
    {
        light.Attenuation = 0;
        return light;
    }
    else
#endif
    {

    // light.attenuation *= getAngleAttenuation( -directionIES.xyz, light.l, scaleOffset );
    // use Vanilla attenuation approach for now: 
    //[branch]
    //if( i < g_lighting.LightCountSpotOnly )
    {
        // TODO: remove acos, switch to dot-product based one like filament? this must be slow.
        float angle = acos( dot( lightIn.Direction, -light.L ) );
        float spotAttenuation = saturate( (lightIn.SpotOuterAngle - angle) / (lightIn.SpotOuterAngle - lightIn.SpotInnerAngle) );
        // squaring of spot attenuation is just for a softer outer->inner curve that I like more visually
        light.Attenuation *= spotAttenuation*spotAttenuation;
    }

#if VA_RM_ACCEPTSHADOWS && !defined(RAYTRACED_SHADOWS)
    [branch]
    if( light.Attenuation > 0 && lightIn.CubeShadowIndex >= 0 )
        light.Attenuation *= ComputeCubeShadow( shading.CubeShadows, shading.GetWorldGeometricNormalVector(), lightIn.CubeShadowIndex, light.L, light.Dist, lightIn.Size, lightIn.Range );
#endif

    return light;
    }
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
        lightRaw.Intensity  *= g_globals.PreExposureMultiplier;
        
        // // these _could_ be avoided (done earlier) but I'll do them here for simplicity for now
        // lightRaw.Position   = lightRaw.Position - g_globals.WorldBase.xyz;
        // lightRaw.Intensity  *= g_globals.PreExposureMultiplier;

        LightParams light = getSpotLight( surface, shading, lightRaw );

        // this is only a property of punctual lights: add it directly to diffuse here!
        SpecialEmissiveLight( surface, shading.PrecomputedEmissive, lightRaw, radiance );

        [branch]
        if( light.Attenuation > 0 )
        {
            light.Attenuation *= computeMicroShadowing( light.NoL, shading.DiffuseAmbientOcclusion );
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
        
            // // these _could_ be avoided (done earlier) but I'll do them here for simplicity for now
            // lightRaw.Position   = lightRaw.Position - g_globals.WorldBase.xyz;
            // lightRaw.Intensity  *= g_globals.PreExposureMultiplier;
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

void evaluatePunctualLightsRT( const SurfaceInteraction surface, const ShadingParams shading, const PixelParams pixel, const float3 nextRayOrigin, float ldSample1D, float2 ldSample2D, inout float3 directRadiance, inout float3 specialEmissiveRadiance, inout ShaderPathTracerRayPayload rayPayload, const bool debugDraw )
{
    const uint totalLights = g_lighting.LightCountPoint;

    [branch]
    if( totalLights == 0 )  // could be avoided on the CPU side if a single 'null' light is added in this case
        return;

    // it's ok to use the same disk for all lights because they're all non-correlated (like, mostly, hopefully, I hope - should think this through a bit - what if there's a ton of lights on a grid?)
    // float2 u = LDSample2D( sampleIndex, hashSeed2D );
    float2 disk = UniformSampleDisk( ldSample2D ); //ConcentricSampleDisk( u );

#if 1 // monte carlo integration
    #if 0
        uint lightsToIntegrate = ceil(totalLights / 6.0); // warning: if this changes between samples, or the order of lights changes, the index coverage could be inadequate and/or overlapping; could also use fixed count
        lightsToIntegrate = min( lightsToIntegrate, totalLights );
        for( uint step = 0; step < lightsToIntegrate; step++ )
        #error enable the 'ldSample1D = Rand1D' below for this to work
    #else
        const uint lightsToIntegrate = 1;
        const uint step = 0;
    #endif
    {   
        //float ldSample1D   = LDSample1D( sampleIndex*lightsToIntegrate + step, hashSeed1D );
        uint lightIndex = (uint)(ldSample1D * totalLights);
        float invProbability = totalLights / (float)lightsToIntegrate;
        
        // // this is only if you're integrating more than 1 light per bounce for whatever testing - it's no longer a low discrepancy sample after Rand1D though!
        // ldSample1D = Rand1D( ldSample1D );

#else
    float invProbability = 1;
    for( uint lightIndex = 0; lightIndex < totalLights; lightIndex++ )
    {
#endif
        ShaderLightPoint lightRaw = g_lightsPoint[lightIndex];
        lightRaw.Intensity  *= g_globals.PreExposureMultiplier;
        lightRaw.Intensity  *= invProbability;

        // this is only a property of punctual lights: add it directly to diffuse here!
        SpecialEmissiveLight( surface, shading.PrecomputedEmissive, lightRaw, specialEmissiveRadiance );

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

            const uint missShaderIndex      = 1;    // visibility (primary) miss shader
#if 1   // workaround for a compiler bug or something - these should be identical but they aren't <shrug>
            ShaderRayPayloadBase rayPayloadViz;
            rayPayloadViz.DispatchRaysIndex = rayPayload.DispatchRaysIndex;
            rayPayloadViz.ConeSpreadAngle   = rayPayload.ConeSpreadAngle;
            rayPayloadViz.ConeWidth         = rayPayload.ConeWidth;
            TraceRay( g_raytracingScene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, ~0, 0, 0, missShaderIndex, visibilityRay, rayPayloadViz );
            const bool visibilityRayMiss = rayPayloadViz.DispatchRaysIndex.x >= (1U << 15); // co-opted this for now; it can't be negative in normal use cases so -123 is a flag that says "miss shader executed"
            //rayPayloadViz.DispatchRaysIndex.x = rayPayloadViz.DispatchRaysIndex.x & (~(1U << 15));
#else
            TraceRay( g_raytracingScene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, ~0, 0, 0, missShaderIndex, visibilityRay, rayPayload );
            const bool visibilityRayMiss = rayPayload.DispatchRaysIndex.x >= (1U << 15); // co-opted this for now; it can't be negative in normal use cases so -123 is a flag that says "miss shader executed"
            rayPayload.DispatchRaysIndex.x = rayPayload.DispatchRaysIndex.x & (~(1U << 15));
#endif

            // update these for more accurate/softer spherical integration when 
            light.L     = lerp( light.L, visibilityRayDir, 0.5 );   // <- meet half way there due to the way current art is tuned to rasterized; !!!!!!!!!!THIS IS A HACK!!!!!!!!!!
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
}

#endif // VA_RAYTRACING