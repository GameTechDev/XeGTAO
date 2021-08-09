///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaSharedTypes.h"

#include "vaRaytracingShared.h"

#ifndef VA_PATH_TRACER_SHARED_H
#define VA_PATH_TRACER_SHARED_H

#ifndef VA_COMPILED_AS_SHADER_CODE
namespace Vanilla
{
#endif

#define VA_PATH_TRACER_CONSTANTBUFFER_SLOT                  0
#define VA_PATH_TRACER_MAX_RECURSION                        2       // without VA_RAYTRACING_USE_TAIL_END_RECURSION, we only need 1 for the main ray and 1 for visibility

#define VA_PATH_TRACER_RADIANCE_SRV_SLOT                    0
#define VA_PATH_TRACER_SKYBOX_SRV_SLOT                      1
#define VA_PATH_TRACER_VIEWSPACE_DEPTH_SRV_SLOT             2

    struct ShaderPathTracerConstants
    {
        ShaderSkyboxConstants   Sky;

        int                     AccumFrameCount;            // how many frames have we accumulated so far (after resetting/clearing). If 0 - this is the first.
        int                     AccumFrameTargetCount;      // how many frames are we aiming to accumulate; stop when we hit!
        int                     MaxBounces;                 // how many max bounces before we terminate the ray
        int                     EnableAA;                   // anti-aliasing

        int                     DispatchRaysWidth;
        int                     DispatchRaysHeight;
        int                     DispatchRaysDepth;
        uint                    Flags;                      // see VA_RAYTRACING_FLAG_XXX

        ShaderDebugViewType     DebugViewType;
        float                   DebugDivergenceTest;        
        int                     DebugPathVizDim;
        int                     Padding2;

        bool                    IgnoreResults( )            { return ( AccumFrameCount >= AccumFrameTargetCount ); }            // do not write out color results
        uint                    SampleIndex( )              { return ShaderMin( AccumFrameCount, AccumFrameTargetCount-1 ); }   // index of currently computed sample
    };

#ifndef VA_COMPILED_AS_SHADER_CODE
} // namespace Vanilla
#endif

#ifdef VA_COMPILED_AS_SHADER_CODE
cbuffer ShaderPathTracerConstantsBuffer                         : register( B_CONCATENATER( VA_PATH_TRACER_CONSTANTBUFFER_SLOT ) )
{
    ShaderPathTracerConstants   g_pathTracerConsts;
}

TextureCube                     g_skyCubemap                    : register( T_CONCATENATER( VA_PATH_TRACER_SKYBOX_SRV_SLOT ) );

RWTexture2D<float4>             g_textureOutput                 : register( u0 );
RWTexture2D<float4>             g_radianceAccumulation          : register( u1 );
RWTexture2D<float4>             g_viewspaceDepth                : register( u2 );

Texture2D<float4>               g_radianceAccumulationSRV       : register( T_CONCATENATER( VA_PATH_TRACER_RADIANCE_SRV_SLOT ) );
Texture2D<float>                g_viewspaceDepthSRV             : register( T_CONCATENATER( VA_PATH_TRACER_VIEWSPACE_DEPTH_SRV_SLOT ) );

void PathTracerCommitPath( const in ShaderPathTracerRayPayload rayPayload )
{
    const uint2 pixelPos            = rayPayload.DispatchRaysIndex.xy;

    const uint sampleIndex          = g_pathTracerConsts.SampleIndex();

    const bool debugDrawRays        = (rayPayload.Flags & VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_VIZ) != 0;
    const bool debugDrawRayDetails  = (rayPayload.Flags & VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_DETAIL_VIZ) != 0;

    float3 combinedRadiance = rayPayload.AccumulatedRadiance;

    if( debugDrawRayDetails )
    {
        // DebugDraw2DText( pixelPos + float2( -10, 20 ), float4( 0.5, 1.0, 0.5, 0.5 ), float4( combinedRadiance, sampleIndex ) );
        // DebugDraw2DText( pixelPos + float2( -10, 35 ), float4( 0.3, 0.8, 0.3, 0.5 ), float4( rayPayload.Beta, rayPayload.BounceCount ) );
    }

    // Un-pre-expose! Since we're storing in 32bit float, we have enough precision to store the actual value; makes averaging easier.
    combinedRadiance /= g_globals.PreExposureMultiplier;

    // Accumulate & write outputs except if we've finished accumulating!
    if( !g_pathTracerConsts.IgnoreResults() )
    {
        // Read current accumulation, if any
        float3 accumRadiance = (sampleIndex==0)?(float3(0,0,0)):(g_radianceAccumulation[pixelPos.xy].rgb);

        // Write the raytraced color to the output texture.
        g_radianceAccumulation[pixelPos.xy].xyz = float3( accumRadiance.rgb + combinedRadiance );

        // // Only output (viewspace) depth with the first sample
        // // (We output viewspace depth so some rasterized systems can still work, mostly for debugging use; this might need changing once transparencies get re-enabled)
        // if( sampleIndex == 0 )
        //     g_viewspaceDepth[pixelPos.xy] = rayPayload.FirstBounceViewspaceDepth;
    }

    if( g_pathTracerConsts.DebugViewType != ShaderDebugViewType::None )
    {
        if( g_pathTracerConsts.DebugViewType == ShaderDebugViewType::BounceCount )
        { 
            g_radianceAccumulation[pixelPos.xy].x = max( 0, rayPayload.BounceCount-1 );
        }
        else if( (uint)g_pathTracerConsts.DebugViewType >= (uint)ShaderDebugViewType::SurfacePropsBegin && (uint)g_pathTracerConsts.DebugViewType <= (uint)ShaderDebugViewType::SurfacePropsEnd )
        {
            g_radianceAccumulation[pixelPos.xy].xyz = rayPayload.AccumulatedRadiance;
        }
    }
}

#endif

#endif // VA_PATH_TRACER_SHARED_H