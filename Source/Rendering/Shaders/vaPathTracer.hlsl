///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define VA_RAYTRACING

#ifndef __INTELLISENSE__
#include "vaPathTracerShared.h"
#endif

#include "vaShared.hlsl"

#include "vaRenderMesh.hlsl"

#include "Lighting/vaLighting.hlsl"

#if VA_USE_RAY_SORTING
[numthreads(32, 1, 1)] void CSKickoff( const uint pathIndex : SV_DispatchThreadID )  {} // dummy

[shader("raygeneration")]
void RaygenKickoff()
{
    uint pathIndex = DispatchRaysIndex().x;
#else
[numthreads(32, 1, 1)]
void CSKickoff( const uint pathIndex : SV_DispatchThreadID ) 
{
#endif

    //    if( DebugOnce( ) )
    //        DebugDraw2DText( float2( 500, 100 ), float4( 1, 0, 1, 1 ), 42 );
    //if( IsUnderCursorRange( pixelPos, int2(1,1) ) )
    //    DebugDraw2DText( pixelPos + float2( 10, 75 ), float4( 1, 0, 1, 1 ), 42 );

    if( pathIndex >= g_pathTracerConsts.MaxPathCount )
        return;

#if 0   // naive 
    uint2 pixelPos = uint2( pathIndex % g_pathTracerConsts.ViewportX, pathIndex / g_pathTracerConsts.ViewportX );
#else   // tiny 8x8 or so tiles
    uint tileSize = VA_PATH_TRACER_DISPATCH_TILE_SIZE;
    uint tileIndex      = pathIndex / (tileSize*tileSize);
    uint tilePixelIndex = pathIndex % (tileSize*tileSize);
    uint tileCountX = (g_pathTracerConsts.ViewportX + tileSize - 1) / tileSize;
    uint2 pixelPos = uint2( (tileIndex % tileCountX) * tileSize + tilePixelIndex % tileSize, (tileIndex / tileCountX) * tileSize + tilePixelIndex / tileSize );
#endif

    // Seed based on current pixel (no AA yet, that's intentional)
    uint  hashSeed = Hash32( pixelPos.x );
    hashSeed = Hash32Combine( hashSeed, pixelPos.y );
    hashSeed = Hash32( hashSeed );
    const uint hashSeedAA   = Hash32Combine( hashSeed, VA_PATH_TRACER_HASH_SEED_AA );

    // index of the sample within the pixel
    const uint sampleIndex  = g_pathTracerConsts.SampleIndex();

    // AA jitter (if any)
    float2 subPixelJitter = (!g_pathTracerConsts.EnableAA)?(float2(0,0)):(LDSample2D( sampleIndex, hashSeedAA ) - 0.5);

    // generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid
    float3 rayDir, rayOrigin; float rayConeSpreadAngle;
    GenerateCameraRay(pixelPos.xy, float2(g_pathTracerConsts.ViewportX, g_pathTracerConsts.ViewportY), subPixelJitter, rayOrigin, rayDir, rayConeSpreadAngle);

    // add noise for divergence test
    [branch]if( g_pathTracerConsts.DebugDivergenceTest > 0 )
    {
        float2 randomJitter = UniformSampleDisk( LDSample2D( sampleIndex, hashSeed ) ); // <- use a nicer but more expensive pattern
        randomJitter = g_pathTracerConsts.DebugDivergenceTest * (randomJitter * 2 - 1);
        rayDir = normalize( rayDir - g_globals.CameraRightVector.xyz * randomJitter.x + g_globals.CameraUpVector.xyz * randomJitter.y );
    }

    // debug draw for rays
    const bool debugDrawRays = ((g_pathTracerConsts.Flags & VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_VIZ) != 0) && IsUnderCursorRange( pixelPos, int2(g_pathTracerConsts.DebugPathVizDim, g_pathTracerConsts.DebugPathVizDim) );
    const bool debugDrawRayDetails = ((g_pathTracerConsts.Flags & VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_VIZ) != 0) && IsUnderCursorRange( pixelPos, int2(1, 1) );

    // draw camera frustum borders 
    if( (g_pathTracerConsts.Flags & VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_VIZ) != 0 )
    {
        const int step = 40; const float length = 0.5;
        if( ((pixelPos.x == 0 || pixelPos.x == g_pathTracerConsts.ViewportX - 1)    && (pixelPos.y % step == 0) ) || ((pixelPos.y == 0 || pixelPos.y == g_pathTracerConsts.ViewportY - 1 )  && (pixelPos.x % step == 0) ) )
            DebugDraw3DLine( rayOrigin, rayOrigin + rayDir * length, float4( 1, 0.5, 0, 0.8 ) );
    }

    uint flags = (g_pathTracerConsts.Flags & ~VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_VIZ) | (debugDrawRays?VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_VIZ:0) | (debugDrawRayDetails?VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_DETAIL_VIZ:0)
                | ( ( g_pathTracerConsts.MaxBounces == 0 )?VA_PATH_TRACER_FLAG_LAST_BOUNCE:0);

    // this is stored in UAVs
    ShaderPathPayload pathPayload;
    //pathPayload.Barycentrics        = float2(0,0);
    //pathPayload.InstanceIndex       = 0xFFFFFFFF;
    //pathPayload.PrimitiveIndex      = 0xFFFFFFFF;
    pathPayload.PixelPos            = pixelPos.xy;
    pathPayload.ConeSpreadAngle     = rayConeSpreadAngle;
    pathPayload.ConeWidth           = 0;                        // it's 0 at pinhole camera focal point
    pathPayload.AccumulatedRadiance = float3( 0, 0, 0 );
    pathPayload.HashSeed            = hashSeed;
    pathPayload.Beta                = float3( 1, 1, 1 );
    pathPayload.Flags               = flags;
#if !VA_USE_RAY_SORTING
    pathPayload.NextRayOrigin       = rayOrigin;
    pathPayload.NextRayDirection    = rayDir;
#endif
    pathPayload.BounceIndex         = 0;
    // pathPayload.AccumulatedRayTravel= 0;
    pathPayload.LastSpecularness    = 1.0;
    pathPayload.PathSpecularness    = 1.0;
    pathPayload.MaxRoughness        = 0.0;

    g_pathPayloads[ pathIndex ] = pathPayload;

    // in case sorting disabled, we must initialize indices to [0, 1, ..., maxPathCount-1]
    if( !g_pathTracerConsts.PerBounceSortEnabled )
        g_pathListSorted[ pathIndex ] = pathIndex;

#if VA_USE_RAY_SORTING
    // this gets sent through the path tracing API
    ShaderMultiPassRayPayload rayPayloadLocal;
    rayPayloadLocal.PathIndex       = pathIndex;
    rayPayloadLocal.ConeSpreadAngle = pathPayload.ConeSpreadAngle;
    rayPayloadLocal.ConeWidth       = pathPayload.ConeWidth;

    RayDesc nextRay;
    nextRay.Origin      = rayOrigin;
    nextRay.Direction   = rayDir;
    nextRay.TMin        = 0.0;
    nextRay.TMax        = 1000000.0;

    const uint missShaderIndex      = 0;    // normal (primary) miss shader
    TraceRay( g_raytracingScene, RAY_FLAG_NONE/*RAY_FLAG_CULL_BACK_FACING_TRIANGLES*/, ~0, 0, 0, missShaderIndex, nextRay, rayPayloadLocal );
#else
    //g_pathListSorted[pathIndex]     = pathIndex; // no need to do it, done on the CPU side
#endif
}

[numthreads(1, 1, 1)]
void CSTickCounters( )
{
    // not dynamically updating for now
    //g_pathTracerControl[0].CurrentRayCount  = g_pathTracerControl[0].NextRayCount;
    //g_pathTracerControl[0].NextRayCount     = 0;
}

[shader("raygeneration")]
void Raygen()
{
    uint pathListIndex = DispatchRaysIndex().x;
    if( pathListIndex >= g_pathTracerConsts.MaxPathCount ) // g_pathTracerControl[0].CurrentRayCount )
        return;

    uint pathIndex = g_pathListSorted[pathListIndex];

    uint2 pixelPos = g_pathPayloads[pathIndex].PixelPos;
    //ShaderPathPayload pathPayload = g_pathPayloads[pathIndex];

    bool stopped   = (g_pathPayloads[pathIndex].Flags & VA_PATH_TRACER_FLAG_STOPPED) != 0;

    // pathIndex can be > number of pixel when rounding up to right/bottom border tile size, so mark those out of screen pixels as invalid
    [branch]
    if( stopped || any( pixelPos >= uint2(g_pathTracerConsts.ViewportX, g_pathTracerConsts.ViewportY) ) )
        return;

    // this gets sent through the path tracing API
    ShaderMultiPassRayPayload rayPayloadLocal;
    rayPayloadLocal.PathIndex       = pathIndex;
    rayPayloadLocal.ConeSpreadAngle = g_pathPayloads[pathIndex].ConeSpreadAngle;
    rayPayloadLocal.ConeWidth       = g_pathPayloads[pathIndex].ConeWidth;

#if VA_USE_RAY_SORTING
    RayDesc nextRay;
    nextRay.Origin      = float3(0,0,0);
    nextRay.Direction   = float3(0,0,0);
    nextRay.TMin        = 0.0;
    nextRay.TMax        = 0.0;

    const uint missShaderIndex      = VA_RAYTRACING_SHADER_MISS_CALLABLES_SHADE_OFFSET + g_pathGeometryHitPayloads[pathIndex].MaterialIndex;
    TraceRay( g_nullAccelerationStructure, RAY_FLAG_NONE, 0, 0, 0, missShaderIndex, nextRay, rayPayloadLocal );
#else
    RayDesc nextRay;
    nextRay.Origin      = g_pathPayloads[pathIndex].NextRayOrigin;
    nextRay.Direction   = g_pathPayloads[pathIndex].NextRayDirection;
    nextRay.TMin        = 0.0;
    nextRay.TMax        = 1000000.0;

    const uint missShaderIndex      = 0;    // normal (primary) miss shader
    TraceRay( g_raytracingScene, RAY_FLAG_NONE/*RAY_FLAG_CULL_BACK_FACING_TRIANGLES*/, ~0, 0, 0, missShaderIndex, nextRay, rayPayloadLocal );
#endif
}

[shader("miss")] // this one gets called with TraceRay::MissShaderIndex 1
void MissVisibility( inout ShaderMultiPassRayPayload rayPayloadLocal )
{
    rayPayloadLocal.PathIndex = 1; // returning with the flag set means that we've missed so the visibility query is true!
}

[shader("miss")] // this one gets called with TraceRay::MissShaderIndex 0
void Miss( inout ShaderMultiPassRayPayload rayPayloadLocal )
{
    ShaderPathPayload pathPayload = g_pathPayloads[ rayPayloadLocal.PathIndex ];

    const bool debugDrawRays = (pathPayload.Flags & VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_VIZ) != 0;

    // we need this for outputting depth - path tracing will actually end
    float3 nextRayOrigin = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

#if !VA_USE_RAY_SORTING
    pathPayload.NextRayOrigin = nextRayOrigin;
#endif

    PathTracerOutputAUX( pathPayload.PixelPos, pathPayload, nextRayOrigin, float3(1,1,1), float3(0,0,1),
        ComputeScreenMotionVectors( pathPayload.PixelPos + float2(0.5, 0.5), nextRayOrigin, nextRayOrigin, float2(0,0) ).xy );

    // // stop any further bounces
    // not needed anymore - this is the default
    // pathPayload.NextRayDirection = float3( 0, 0, 0 );

    // we'll need it for debugging purposes
    float endRayConeWidth = pathPayload.ConeWidth + RayTCurrent() * (pathPayload.ConeSpreadAngle/* + betaAngle*/);

    if( debugDrawRays )
    {
        float4 rayDebugColor = float4( GradientRainbow( pathPayload.BounceIndex / 6.0 ), 0.5 );
        DebugDraw3DCylinder( WorldRayOrigin( ), nextRayOrigin, 
            pathPayload.ConeWidth * 0.5, endRayConeWidth * 0.5, rayDebugColor );
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

    // hack to get it in sync with distant IBL ambient lighting
    if( pathPayload.BounceIndex > 0 )
        skyRadiance += g_lighting.AmbientLightFromDistantIBL.rgb * g_globals.PreExposureMultiplier;

    // standard path trace accumulation
    pathPayload.AccumulatedRadiance  += RadianceCombineAndFireflyClamp( pathPayload.PathSpecularness, pathPayload.Beta, skyRadiance );

    // hitting the skybox - we can't be bouncing anymore, just call it quits!
    PathTracerCommit( rayPayloadLocal.PathIndex, pathPayload );
}

#if VA_USE_RAY_SORTING

[shader("closesthit")]
void ClosestHit( inout ShaderMultiPassRayPayload rayPayloadLocal, in BuiltInTriangleIntersectionAttributes attr )
{
    ShaderGeometryHitPayload geometryHitPayload;
    geometryHitPayload.InstanceIndex    = InstanceIndex( );
    geometryHitPayload.RayDirLength     = WorldRayDirection( ) * RayTCurrent( );
    geometryHitPayload.PrimitiveIndex   = PrimitiveIndex( );
    geometryHitPayload.Barycentrics     = attr.barycentrics;
    geometryHitPayload.MaterialIndex    = InstanceID( );
    g_pathGeometryHitPayloads[rayPayloadLocal.PathIndex] = geometryHitPayload;
    
    // sort by material indices only; if changing, search for 'const uint32 maxSortKey' in vaPathTRacer.cpp
    g_pathSortKeys[rayPayloadLocal.PathIndex] = geometryHitPayload.MaterialIndex+1;
}

#endif

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
// 

// [numthreads(8, 8, 1)]
void PSWriteToOutput( const in float4 position : SV_Position, out float4 outColor : SV_Target0, out float outDepth : SV_Depth )
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
//    g_textureOutput[ pixCoord ] = float4( noise.xxx, 1.0 );
#endif

    const bool underMouseCursor = IsUnderCursorRange( pixCoord, int2(1,1) );

    // if( all( pixCoord == int2( 0, 0 ) ) )
    // {
    //     float k = VA_FLOAT_ONE_MINUS_EPSILON;
    //     DebugText( k );
    // }

    float4 pixelData = g_radianceAccumulationSRV[ pixCoord ];
    float viewspaceDepth = pixelData.w; //g_viewspaceDepthSRV[ pixCoord ];

    float accumFrameCount = min( g_pathTracerConsts.AccumFrameCount+1, g_pathTracerConsts.AccumFrameTargetCount );
    /*float4*/ outColor.rgb = (pixelData.rgb / accumFrameCount) * g_globals.PreExposureMultiplier;
    outColor.a = 1.0;

//    if( underMouseCursor )
//        DebugDraw2DText( pixCoord + float2( 10, 75 ), float4( 1, 0, 1, 1 ), outColor );

    if( g_pathTracerConsts.DebugViewType != PathTracerDebugViewType::None )
    {
        if( g_pathTracerConsts.DebugViewType == PathTracerDebugViewType::BounceIndex )
        {
            int bounceIndex = g_radianceAccumulationSRV[ pixCoord ].x;
            outColor.rgb = GradientHeatMap( /*sqrt*/( bounceIndex / (float)g_pathTracerConsts.MaxBounces ) );
            if( underMouseCursor )
                DebugDraw2DText( pixCoord + float2( 10, -16 ), float4( 1, 0, 1, 1 ), bounceIndex );
        }
        else if( g_pathTracerConsts.DebugViewType == PathTracerDebugViewType::ViewspaceDepth )
        {
            outColor.rgb = frac( viewspaceDepth );
            if( underMouseCursor )
                DebugDraw2DText( pixCoord + float2( 10, -16 ), float4( 1, 0, 1, 1 ), viewspaceDepth );
        }
        else if( g_pathTracerConsts.DebugViewType == PathTracerDebugViewType::MaterialID || g_pathTracerConsts.DebugViewType == PathTracerDebugViewType::ShaderID )
        {
            uint val = asuint(g_radianceAccumulationSRV[ pixCoord ].x);
            outColor.rgb = GradientRainbow( Hash32ToFloat( Hash32(val) ) );
            if( underMouseCursor )
                DebugDraw2DText( pixCoord + float2( 10, -16 ), float4( 1, 0, 1, 1 ), val );
        }
        else if( (uint)g_pathTracerConsts.DebugViewType >= (uint)PathTracerDebugViewType::SurfacePropsBegin && (uint)g_pathTracerConsts.DebugViewType <= (uint)PathTracerDebugViewType::SurfacePropsEnd )
        {
            outColor.rgb = g_radianceAccumulationSRV[ pixCoord ].rgb;
            outColor.a = 0;
            if( underMouseCursor )
                DebugDraw2DText( pixCoord + float2( 10, -16 ), float4( 1, 0, 1, 1 ), outColor );
        }
        else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::DenoiserAuxAlbedo )
        {
            outColor.rgb = g_denoiserAuxAlbedoSRV[ pixCoord ].rgb;
            outColor.a = 0;
            if( underMouseCursor )
                DebugDraw2DText( pixCoord + float2( 10, -16 ), float4( 1, 0, 1, 1 ), outColor );
        }
        else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::DenoiserAuxNormals )
        {
            outColor.rgb = g_denoiserAuxNormalsSRV[ pixCoord ].rgb;
            outColor.a = 0;
            if( underMouseCursor )
                DebugDraw2DText( pixCoord + float2( 10, -16 ), float4( 1, 0, 1, 1 ), outColor );
            outColor.rgb = DisplayNormalSRGB( outColor.rgb );
        } 
        else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::DenoiserAuxMotionVectors )
        {
            float3 velocity = float3( g_denoiserAuxMotionVectorsSRV[ pixCoord ].rg, 0 );
            outColor.rgb = velocity;
            outColor.a = 0;
            if( underMouseCursor )
                DebugDraw2DText( pixCoord + float2( 10, -16 ), float4( 1, 0, 1, 1 ), outColor );
            outColor.rgb = DisplayNormalSRGB( saturate( sqrt( outColor.rgb / 16.0 ) ) );

            bool dbg1 = all( (pixCoord.xy+1) % 100 == 0.xx );
            bool dbg2 = all( (pixCoord.xy+49) % 100 == 0.xx );
            if( dbg1 || dbg2 )
            {
                float2 clipXY   = pixCoord + 0.5f;
                DebugDraw2DLine( clipXY, clipXY + velocity.xy, float4( dbg1, dbg2, 0, 1 ) );

                //if( all( (pixCoord.xy+1) % 400 == 0 ) )
                //    DebugDraw2DText( clipXY, float4( 1, 0.5, 1, 1 ), float4( velocity, g_depthBuffer.Load( uint3( pixCoord, 0 ) ) ) );
            }
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

    outColor.rgb = HDRClamp( outColor.rgb );

    outDepth = ViewToNDCDepth( viewspaceDepth );
}

[numthreads(8, 8, 1)]
void CSPrepareDenoiserInputs( const uint2 pixelPos : SV_DispatchThreadID )
{
    g_denoiserAuxAlbedo [pixelPos].xyz /= (float)g_genericRootConst;
    g_denoiserAuxNormals[pixelPos].xyz = normalize( g_denoiserAuxNormals[pixelPos].xyz );
    g_denoiserAuxMotionVectors[pixelPos].xy /= (float)g_genericRootConst;
}


#ifdef VA_OPTIX_DENOISER

RWBuffer<float>     g_denoiserSharedAlbedo              : register( u0 );   // these are buffers shared with CUDA/OptiX
RWBuffer<float>     g_denoiserSharedNormals             : register( u1 );   // these are buffers shared with CUDA/OptiX
RWBuffer<float>     g_denoiserSharedMotionVectors       : register( u2 );   // these are buffers shared with CUDA/OptiX
RWBuffer<float>     g_denoiserSharedInput               : register( u3 );   // these are buffers shared with CUDA/OptiX
RWBuffer<float>     g_denoiserSharedPreviousOutput      : register( u4 );   // these are buffers shared with CUDA/OptiX
RWBuffer<float>     g_denoiserSharedOutput              : register( u5 );   // these are buffers shared with CUDA/OptiX
RWTexture2D<float4> g_denoiserVanillaOutput             : register( u6 );

// see 'vaDenoiserOptiX::AllocateStagingBuffer' for details
int ComputeAddr( const uint2 pixelPos, const uint elemCount )
{
    const uint width = g_genericRootConst;
    return elemCount * ( pixelPos.x + pixelPos.y * width );
}

void WriteVec2( RWBuffer<float> output, const uint2 pixelPos, const float2 vec )
{
    const uint address = ComputeAddr(pixelPos, 2);
    output[address+0] = vec.x;
    output[address+1] = vec.y;
}
void WriteVec3( RWBuffer<float> output, const uint2 pixelPos, const float3 vec )
{
    const uint address = ComputeAddr(pixelPos, 3);
    output[address+0] = vec.x;
    output[address+1] = vec.y;
    output[address+2] = vec.z;
}
void WriteVec4( RWBuffer<float> output, const uint2 pixelPos, const float4 vec )
{
    const uint address = ComputeAddr(pixelPos, 4);
    output[address+0] = vec.x;
    output[address+1] = vec.y;
    output[address+2] = vec.z;
    output[address+3] = vec.w;
}
float4 ReadVec4( RWBuffer<float> input, const uint2 pixelPos )
{
    const uint address = ComputeAddr(pixelPos, 4);
    return float4( input[address+0], input[address+1], input[address+2], input[address+3] );
}
float3 ReadVec3( RWBuffer<float> input, const uint2 pixelPos )
{
    const uint address = ComputeAddr(pixelPos, 3);
    return float3( input[address+0], input[address+1], input[address+2] );
}
float2 ReadVec2( RWBuffer<float> input, const uint2 pixelPos )
{
    const uint address = ComputeAddr(pixelPos, 2);
    return float2( input[address+0], input[address+1] );
}

[numthreads(8, 8, 1)]
void CSVanillaToOptiX( const uint2 pixelPos : SV_DispatchThreadID )
{
//    g_radianceAccumulationSRV    
//    g_denoiserAuxAlbedoSRV       
//    g_denoiserAuxNormalsSRV      
//    g_denoiserAuxMotionVectorsSRV

    WriteVec3( g_denoiserSharedAlbedo,  pixelPos,   g_denoiserAuxAlbedoSRV.Load( int3(pixelPos,0) ).xyz );
    WriteVec3( g_denoiserSharedNormals, pixelPos,   g_denoiserAuxNormalsSRV.Load( int3(pixelPos,0) ).xyz );
    WriteVec3( g_denoiserSharedInput,   pixelPos,   g_radianceAccumulationSRV.Load( int3(pixelPos,0) ).xyz );

    // temporal part
    float2 motionVector = g_denoiserAuxMotionVectorsSRV.Load( int3(pixelPos, 0) );
    motionVector = -motionVector; // OptiX standard is inverted from Vanilla's
    WriteVec2( g_denoiserSharedMotionVectors, pixelPos, motionVector );

//    float4 prevOutput = ReadVec4( g_denoiserSharedOutput, pixelPos );
//
//    //prevOutput.xy = ((pixelPos.xy / 10) % 2);
//    //prevOutput.z = 0;
//
//    WriteVec4( g_denoiserSharedPreviousOutput, pixelPos, prevOutput );
}

[numthreads(8, 8, 1)]
void CSOptiXToVanilla( const uint2 pixelPos : SV_DispatchThreadID )
{
    // debug view
    // g_denoiserVanillaOutput[ pixelPos ].xyzw = ReadVec4( g_denoiserSharedPreviousOutput, pixelPos );
    // // g_denoiserVanillaOutput[ pixelPos ].xyzw = ReadVec4( g_denoiserSharedAlbedo, pixelPos );
    // // g_denoiserVanillaOutput[ pixelPos ].xyz = ReadVec3( g_denoiserSharedNormals, pixelPos );
    // // g_denoiserVanillaOutput[ pixelPos ].xy = ReadVec2( g_denoiserSharedMotionVectors, pixelPos );
    // // g_denoiserVanillaOutput[ pixelPos ].z = 0;
    // return;

    float3 outColor = ReadVec3( g_denoiserSharedOutput, pixelPos );

    // not correct but solves some temporal issues for now
    outColor = clamp( outColor, 0, 15 );

    g_denoiserVanillaOutput[ pixelPos ] = float4( outColor, 1 );
}

#endif // #ifdef VA_OPTIX_DENOISER

//[numthreads(8, 8, 1)]
//void CSDebugDisplayDenoiser( const uint2 pixCoord : SV_DispatchThreadID )
//{
//}

