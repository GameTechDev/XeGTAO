///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __INTELLISENSE__
#include "vaPathTracerShared.h"
#endif

#define VA_RAYTRACING

#include "vaShared.hlsl"

#include "vaRenderMesh.hlsl"

[shader("raygeneration")]
void Raygen()
{
    uint2 pixelPos = DispatchRaysIndex().xy;
    float3 rayDir;
    float3 rayOrigin;
    float rayConeSpreadAngle;

    // Seed based on current pixel (no AA yet, that's intentional)
    uint  hashSeed = Hash32( pixelPos.x );
    hashSeed = Hash32Combine( hashSeed, pixelPos.y );
    hashSeed = Hash32( hashSeed );

    const uint hashSeedAA   = Hash32Combine( hashSeed, VA_RAYTRACING_HASH_SEED_AA );
    const uint sampleIndex  = g_pathTracerConsts.SampleIndex();
    
    // // for debugging, if you want to look at the sample 0 only, use this!
    //  if( g_pathTracerConsts.AccumFrameCount > g_pathTracerConsts.AccumFrameTargetCount-1 )
    //      sampleIndex = 0;

    float2 subPixelJitter = float2(0,0);
    if( g_pathTracerConsts.EnableAA )
    {
       subPixelJitter = LDSample2D( sampleIndex, hashSeedAA ) - 0.5;
    }

    // Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
    GenerateCameraRay(pixelPos.xy, subPixelJitter, rayOrigin, rayDir, rayConeSpreadAngle);
    
    [branch]if( g_pathTracerConsts.DebugDivergenceTest > 0 )
    {
        //float2 randomJitter = UniformSampleDisk( float2( Hash32ToFloat( hashSeed ), Hash32ToFloat( Hash32(hashSeed) ) ) );
        float2 randomJitter = UniformSampleDisk( LDSample2D( sampleIndex, hashSeed ) ); // <- use a nicer but more expensive pattern
        randomJitter = g_pathTracerConsts.DebugDivergenceTest * (randomJitter * 2 - 1);
        rayDir = normalize( rayDir - g_globals.CameraRightVector.xyz * randomJitter.x + g_globals.CameraUpVector.xyz * randomJitter.y );
    }

    const bool debugDrawRays = ((g_pathTracerConsts.Flags & VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_VIZ) != 0) && IsUnderCursorRange( pixelPos, int2(g_pathTracerConsts.DebugPathVizDim, g_pathTracerConsts.DebugPathVizDim) );
    const bool debugDrawRayDetails = ((g_pathTracerConsts.Flags & VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_VIZ) != 0) && IsUnderCursorRange( pixelPos, int2(1, 1) );

    // draw camera borders
    if( (g_pathTracerConsts.Flags & VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_VIZ) != 0 )
    {
        int step = 40;
        if( ((pixelPos.x == 0 || pixelPos.x == g_pathTracerConsts.DispatchRaysWidth - 1)    && (pixelPos.y % step == 0) ) || 
            ((pixelPos.y == 0 || pixelPos.y == g_pathTracerConsts.DispatchRaysHeight - 1 )  && (pixelPos.x % step == 0) ) )
        {
            float length = 0.5;

            DebugDraw3DLine( rayOrigin, rayOrigin + rayDir * length, float4( 1, 0.5, 0, 0.8 ) );
        }
    }

    // rayDir.x += subPixelJitter.x * 0.02;
    // rayDir.y += subPixelJitter.y * 0.02;
    // rayDir = normalize(rayDir);

    ShaderPathTracerRayPayload rayPayload;
    rayPayload.DispatchRaysIndex    = pixelPos.xy;
    rayPayload.ConeSpreadAngle      = rayConeSpreadAngle;
    rayPayload.ConeWidth            = 0;                        // it's 0 at pinhole camera focal point
    rayPayload.AccumulatedRadiance  = float3( 0, 0, 0 );
    rayPayload.HashSeed             = hashSeed;
    rayPayload.Beta                 = float3( 1, 1, 1 );
    rayPayload.Flags                = (g_pathTracerConsts.Flags & ~VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_VIZ) | (debugDrawRays?VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_VIZ:0) | (debugDrawRayDetails?VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_DETAIL_VIZ:0);
    rayPayload.NextRayOrigin        = rayOrigin;
    rayPayload.NextRayDirection     = rayDir;
    rayPayload.BounceCount          = 0;
    rayPayload.AccumulatedRayTravel = 0;
    rayPayload.DebugViewType        = (uint)g_pathTracerConsts.DebugViewType;
    rayPayload.LastSpecularness     = 1.0;
    rayPayload.PathSpecularness     = 1.0;
    rayPayload.MaxRoughness         = 0.0;
    //rayPayload.FirstBounceViewspaceDepth = 1e36f;


    // use NextRayDirection to signal that more bounces are needed
    while( length( rayPayload.NextRayDirection ) > 0 && rayPayload.BounceCount <= g_pathTracerConsts.MaxBounces )
    {
        RayDesc nextRay;
        nextRay.Origin      = rayPayload.NextRayOrigin;
        nextRay.Direction   = rayPayload.NextRayDirection;
        nextRay.TMin        = 0.0;
        nextRay.TMax        = 1000000.0;

        // no further bounces by default
        rayPayload.NextRayDirection = float3( 0, 0, 0 );

        //rayPayload.Flags &= ~VA_RAYTRACING_FLAG_LAST_BOUNCE; // clear all temporary flags
        rayPayload.Flags |= (rayPayload.BounceCount == g_pathTracerConsts.MaxBounces)?(VA_RAYTRACING_FLAG_LAST_BOUNCE):(0);

        const uint missShaderIndex      = 0;    // normal (primary) miss shader
        TraceRay( g_raytracingScene, RAY_FLAG_NONE/*RAY_FLAG_CULL_BACK_FACING_TRIANGLES*/, ~0, 0, 0, missShaderIndex, nextRay, rayPayload );

        // advance these two parts of the payload
        rayPayload.BounceCount++;
        rayPayload.HashSeed = Hash32( rayPayload.HashSeed );
    }

    // hitting the skybox - we're not bouncing around anymore!
    PathTracerCommitPath( rayPayload );
}                                                                   

// // if you want to override material-specific ones - not used except for testing
// [shader("anyhit")]
// void MyAnyHitShader( inout ShaderRaytracePayload payload/* : SV_RayPayload*/, BuiltInTriangleIntersectionAttributes attr/* : SV_IntersectionAttributes*/ )
// {
//     if( any( (payload.DispatchRaysIndex.xy % 4) < 2 ) )
//         IgnoreHit( );
// }
//
// [shader("closesthit")]
// void ClosestHit( inout ShaderRaytracePayload rayPayload, in BuiltInTriangleIntersectionAttributes attr )
// {
// }

[shader("miss")] // this one gets called with TraceRay::MissShaderIndex 1
void MissVisibility( inout ShaderRayPayloadBase rayPayloadViz )
{
    rayPayloadViz.DispatchRaysIndex.x = rayPayloadViz.DispatchRaysIndex.x | (1U << 15);
}

[shader("miss")] // this one gets called with TraceRay::MissShaderIndex 0
void Miss( inout ShaderPathTracerRayPayload rayPayload )
{
    const bool debugDrawRays = (rayPayload.Flags & VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_VIZ) != 0;

    // we need this for outputting depth - path tracing will actually end
    float3 nextRayOrigin = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    rayPayload.NextRayOrigin = nextRayOrigin;

    // output depth if needed
    const uint sampleIndex  = g_pathTracerConsts.SampleIndex();
    if( rayPayload.BounceCount == 0 && sampleIndex == 0 )
        g_viewspaceDepth[rayPayload.DispatchRaysIndex] = clamp( dot( g_globals.CameraDirection.xyz, nextRayOrigin.xyz - g_globals.CameraWorldPosition.xyz ), g_globals.CameraNearFar.x, g_globals.CameraNearFar.y );

    // // stop any further bounces
    // not needed anymore - this is the default
    // rayPayload.NextRayDirection = float3( 0, 0, 0 );

    // we'll need it for debugging purposes
    float endRayConeWidth = rayPayload.ConeWidth + RayTCurrent() * (rayPayload.ConeSpreadAngle/* + betaAngle*/);

    if( debugDrawRays )
    {
        float4 rayDebugColor = float4( GradientRainbow( rayPayload.BounceCount / 6.0 ), 0.5 );
        DebugDraw3DCylinder( WorldRayOrigin( ), nextRayOrigin, 
            rayPayload.ConeWidth * 0.5, endRayConeWidth * 0.5, rayDebugColor );
    }

    // light that comes from sky :)
    float3 skyRadiance;
    if( length(g_pathTracerConsts.Sky.ColorMul ) == 0 )
        skyRadiance = float3( 0, 0, 0 );
    else
    {
        // yes, this could be premultiplied on the C++ side but leave it like this for a while
        float3 skyDirection = normalize( mul( (float3x3)g_pathTracerConsts.Sky.CubemapRotate, WorldRayDirection() ) );
        
        // TODO/WARNING: sampling only level 0 mip while we could be sampling lower from cone angle
        skyRadiance = g_skyCubemap.SampleLevel( g_samplerLinearWrap, skyDirection.xyz, 0.0 ).xyz;
        skyRadiance *= g_globals.PreExposureMultiplier;
        skyRadiance *= g_pathTracerConsts.Sky.ColorMul.xyz;
    }

    // standard path trace accumulation
    rayPayload.AccumulatedRadiance  += rayPayload.Beta * skyRadiance;
}

// [numthreads(8, 8, 1)]
void WriteToOutputPS( const in float4 position : SV_Position, out float4 outColor : SV_Target0, out float outDepth : SV_Depth )
{
    int2 pixCoord = position.xy; // dispatchThreadID.xy;

    if( any( pixCoord >= g_globals.ViewportSize ) )
        return;

    // just stress testing the hash
#if 0
    uint seed   = pixCoord.x + (pixCoord.y << 13); //0x9E3779B1 * pixCoord.y;
    uint hash   = Hash32( seed );
    float noise = Hash32ToFloat( hash );
    for( int i = 0; i < 65536; i++ )
    {
        hash = Hash32( hash );
    }
    noise = Hash32ToFloat( hash );
    g_textureOutput[ pixCoord ] = float4( noise.xxx, 1.0 );
#endif

    const bool underMouseCursor = IsUnderCursorRange( pixCoord, int2(1,1) );

    // if( all( pixCoord == int2( 0, 0 ) ) )
    // {
    //     float k = VA_FLOAT_ONE_MINUS_EPSILON;
    //     DebugText( k );
    // }

    float4 pixelData = g_radianceAccumulationSRV[ pixCoord ];
    float viewspaceDepth = g_viewspaceDepthSRV[ pixCoord ];

    float accumFrameCount = min( g_pathTracerConsts.AccumFrameCount+1, g_pathTracerConsts.AccumFrameTargetCount );
    /*float4*/ outColor = (pixelData / accumFrameCount) * g_globals.PreExposureMultiplier;

//    if( underMouseCursor )
//        DebugDraw2DText( pixCoord + float2( 10, 75 ), float4( 1, 0, 1, 1 ), outColor );

    if( g_pathTracerConsts.DebugViewType != ShaderDebugViewType::None )
    {
        if( g_pathTracerConsts.DebugViewType == ShaderDebugViewType::BounceCount )
        {
            int bounceCount = g_radianceAccumulationSRV[ pixCoord ].x;
            outColor.rgb = GradientHeatMap( /*sqrt*/( bounceCount / (float)g_pathTracerConsts.MaxBounces ) );
            if( underMouseCursor )
                DebugDraw2DText( pixCoord + float2( 10, -16 ), float4( 1, 0, 1, 1 ), bounceCount );
        }
        else if( g_pathTracerConsts.DebugViewType == ShaderDebugViewType::ViewspaceDepth )
        {
            outColor.rgb = frac( viewspaceDepth );
            if( underMouseCursor )
                DebugDraw2DText( pixCoord + float2( 10, -16 ), float4( 1, 0, 1, 1 ), viewspaceDepth );
        }
        else if( g_pathTracerConsts.DebugViewType == ShaderDebugViewType::MaterialID || g_pathTracerConsts.DebugViewType == ShaderDebugViewType::ShaderID )
        {
            uint val = asuint(g_radianceAccumulationSRV[ pixCoord ].x);
            outColor.rgb = GradientRainbow( Hash32ToFloat( Hash32(val) ) );
            if( underMouseCursor )
                DebugDraw2DText( pixCoord + float2( 10, -16 ), float4( 1, 0, 1, 1 ), val );
        }
        else if( (uint)g_pathTracerConsts.DebugViewType >= (uint)ShaderDebugViewType::SurfacePropsBegin && (uint)g_pathTracerConsts.DebugViewType <= (uint)ShaderDebugViewType::SurfacePropsEnd )
        {
            outColor.rgb = g_radianceAccumulationSRV[ pixCoord ].rgb;
            outColor.a = 0;
            if( underMouseCursor )
                DebugDraw2DText( pixCoord + float2( 10, -16 ), float4( 1, 0, 1, 1 ), outColor );
        }

    }

    //if( g_pathTracerConsts.EnableCrosshairDebugViz )
    //{
    //    int2 cursorPos = int2(g_globals.CursorViewportPosition.xy);
    //    if( pixCoord.x == cursorPos.x || pixCoord.y == cursorPos.y )
    //    {
    //        if( underMouseCursor )
    //            DebugDraw2DText( pixCoord + float2( 10, -16 ), float4( 1, 0, 1, 1 ), outColor );
    //        if( length(float2(cursorPos) - float2(pixCoord)) < 1.5 )
    //            outColor.rgb = saturate( float3( 2, 1, 1 ) - normalize(outColor.rgb) );
    //    }
    //
    //}

    // g_textureOutput[ pixCoord ] = outColor;

    outColor.rgb = HDRClamp( outColor.rgb );

    outDepth = ViewToNDCDepth( viewspaceDepth );
     
    // g_textureOutput[ pixCoord ].rgb = frac( g_radianceAccumulationSRV[ pixCoord ].w ).xxx;
}
