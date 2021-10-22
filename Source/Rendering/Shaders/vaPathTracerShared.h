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
#define VA_PATH_TRACER_MAX_RECURSION                        2       // without VA_USE_RAY_SORTING we only need 1 for the main ray and 1 for visibility

#define VA_PATH_TRACER_RADIANCE_SRV_SLOT                    0
#define VA_PATH_TRACER_SKYBOX_SRV_SLOT                      1
#define VA_PATH_TRACER_VIEWSPACE_DEPTH_SRV_SLOT             2
#define VA_PATH_TRACER_NULL_ACC_STRUCT                      3
#define VA_PATH_TRACER_DENOISE_AUX_ALBEDO_SRV_SLOT          4
#define VA_PATH_TRACER_DENOISE_AUX_NORMALS_SRV_SLOT         5

#define VA_PATH_TRACER_DISPATCH_TILE_SIZE                   8

#define VA_USE_RAY_SORTING                                  1

    // this changes once per frame (or etc.)
    struct ShaderPathTracerConstants
    {
        ShaderSkyboxConstants   Sky;

        int                     AccumFrameCount;            // how many frames have we accumulated so far (after resetting/clearing). If 0 - this is the first.
        int                     AccumFrameTargetCount;      // how many frames are we aiming to accumulate; stop when we hit!
        int                     MaxBounces;                 // how many max bounces before we terminate the ray
        int                     EnableAA;                   // anti-aliasing

        float                   FireflyClampThreshold;
        bool                    DenoisingEnabled;           // collect AUX buffers and etc
        float                   DummyParam1;
        float                   DummyParam2;

        uint                    ViewportX;
        uint                    ViewportY;
        uint                    MaxPathCount;               // total starting ray count (for ex. 1920x1080 it's 1920*1080 or more because each dimension is rounded up to VA_PATH_TRACER_DISPATCH_TILE_SIZE)
        uint                    PerBounceSortEnabled;       // 

        uint                    Flags;                      // see VA_RAYTRACING_FLAG_XXX
        ShaderDebugViewType     DebugViewType;
        float                   DebugDivergenceTest;        
        int                     DebugPathVizDim;

        bool                    IgnoreResults( )            { return ( AccumFrameCount >= AccumFrameTargetCount ); }            // do not write out color results
        uint                    SampleIndex( )              { return VA_MIN( AccumFrameCount, AccumFrameTargetCount-1 ); }   // index of currently computed sample
    };

    // these are globals that change during path tracing (such as counters)
    struct ShaderPathTracerControl
    {
        uint                    CurrentRayCount;
        uint                    NextRayCount;
    };

    // stored inside a RWStructuredBuffer
    struct ShaderPathPayload
    {
        // vaVector2               Barycentrics;
        // uint                    InstanceIndex;
        // uint                    PrimitiveIndex;
        vaVector2ui             PixelPos;
        float                   ConeSpreadAngle;            // set by caller, updated by callee: see RayTracingGems, Chapter 20 Texture Level of Detail Strategies for Real-Time Ray Tracing
        float                   ConeWidth;                  // set by caller, updated by callee: see RayTracingGems, Chapter 20 Texture Level of Detail Strategies for Real-Time Ray Tracing
        vaVector3               AccumulatedRadiance;        // initialized by caller, updated by callee
        uint                    HashSeed;                   // set by caller, updated on the way
        vaVector3               Beta;                       // initialized by caller, updated by callee; a.k.a. accumulatedBSDF - Beta *= BSDFSample::F / BSDFSample::PDF
        uint                    Flags;                      // Various VA_RAYTRACING_FLAG_* flags
#if !VA_USE_RAY_SORTING
        vaVector3               NextRayOrigin;              // fill in to continue path tracing or ignore
#endif
        int                     BounceIndex;                // each bounce adds one! (intentionally int)
#if !VA_USE_RAY_SORTING
        vaVector3               NextRayDirection;           // fill in to continue path tracing
#endif
        float                   MaxRoughness;               // max roughness on the path so far - used as a "poor man's path regularization" - for a proper solution see https://www2.in.tu-clausthal.de/~cgstore/publications/2019_Jendersie_brdfregularization.pdf
        float                   LastSpecularness;           // In pbrt this is a binary. Here it's a [0,1] scalar measure of amount of 'perfect specular response' - not tightly defined yet but could be something like 1 - solid_angle_subtending_standard_deviation_of_all_reflected_light / (4*PI). Currently it's 'totally an ad-hoc heuristic; I should come back and formalize it :)
        float                   PathSpecularness;           // PathSpecularness = PathSpecularness*LastSpecularness
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

RaytracingAccelerationStructure g_nullAccelerationStructure     : register( T_CONCATENATER( VA_PATH_TRACER_NULL_ACC_STRUCT ) );

RWTexture2D<float4>             g_radianceAccumulation          : register( u0 );
RWTexture2D<float4>             g_viewspaceDepth                : register( u1 );

// in path tracing these two are indexed using the same index
RWStructuredBuffer<ShaderGeometryHitPayload>
                                g_pathGeometryHitPayloads       : register( u2 );
RWStructuredBuffer<ShaderPathPayload>
                                g_pathPayloads                  : register( u3 );

//RWStructuredBuffer<ShaderPathTracerControl>
//                                g_pathTracerControl             : register( u4 );

RWStructuredBuffer<uint>        g_pathListSorted                : register( u4 );   // this could/should be a SRV
RWStructuredBuffer<uint>        g_pathSortKeys                  : register( u5 );

RWTexture2D<float4>             g_denoiserAuxAlbedo             : register( u6 );
RWTexture2D<float4>             g_denoiserAuxNormals            : register( u7 );

Texture2D<float4>               g_radianceAccumulationSRV       : register( T_CONCATENATER( VA_PATH_TRACER_RADIANCE_SRV_SLOT ) );
Texture2D<float>                g_viewspaceDepthSRV             : register( T_CONCATENATER( VA_PATH_TRACER_VIEWSPACE_DEPTH_SRV_SLOT ) );

Texture2D<float4>               g_denoiserAuxAlbedoSRV          : register( T_CONCATENATER( VA_PATH_TRACER_DENOISE_AUX_ALBEDO_SRV_SLOT  ) );
Texture2D<float4>               g_denoiserAuxNormalsSRV         : register( T_CONCATENATER( VA_PATH_TRACER_DENOISE_AUX_NORMALS_SRV_SLOT ) );

void PathTracerCommit( const in uint pathIndex, const in ShaderPathPayload rayPayload )
{
    const uint2 pixelPos            = rayPayload.PixelPos.xy;

    const uint sampleIndex          = g_pathTracerConsts.SampleIndex();

    const bool debugDrawRays        = (rayPayload.Flags & VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_VIZ) != 0;
    const bool debugDrawRayDetails  = (rayPayload.Flags & VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_DETAIL_VIZ) != 0;

    float3 combinedRadiance = rayPayload.AccumulatedRadiance;

    if( debugDrawRayDetails )
    {
        // DebugDraw2DText( pixelPos + float2( -10, 20 ), float4( 0.5, 1.0, 0.5, 0.5 ), float4( combinedRadiance, sampleIndex ) );
        // DebugDraw2DText( pixelPos + float2( -10, 35 ), float4( 0.3, 0.8, 0.3, 0.5 ), float4( rayPayload.Beta, rayPayload.BounceIndex ) );
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
        if( g_pathTracerConsts.DebugViewType == ShaderDebugViewType::BounceIndex )
        { 
            g_radianceAccumulation[pixelPos.xy].x = max( 0, rayPayload.BounceIndex-1 );
        }
        else if( (uint)g_pathTracerConsts.DebugViewType >= (uint)ShaderDebugViewType::SurfacePropsBegin && (uint)g_pathTracerConsts.DebugViewType <= (uint)ShaderDebugViewType::SurfacePropsEnd )
        {
            g_radianceAccumulation[pixelPos.xy].xyz = rayPayload.AccumulatedRadiance;
        }
    }

    // signal we're done to subsequent passes
    g_pathPayloads[ pathIndex ].Flags |= VA_RAYTRACING_FLAG_STOPPED;

    // sort key 0 indicates inactive paths - so they quickly get skipped together
    g_pathSortKeys[ pathIndex ] = 0;
}

void PathTracerOutputAUX( uint2 pixelPos, const in ShaderPathPayload rayPayload, float3 worldPos, float3 materialAlbedo, float3 materialNormal )
{
    // output depth if needed
    const int bounceIndex = rayPayload.BounceIndex;
    const uint sampleIndex  = g_pathTracerConsts.SampleIndex();
    if( bounceIndex == 0 && sampleIndex == 0 )
        g_viewspaceDepth[pixelPos] = clamp( dot( g_globals.CameraDirection.xyz, worldPos.xyz - g_globals.CameraWorldPosition.xyz ), g_globals.CameraNearFar.x, g_globals.CameraNearFar.y );

    // only first bounce for now
    if( g_pathTracerConsts.DenoisingEnabled && bounceIndex == 0 )
    {
        float3 albedoPrev = 0;
        float3 normalPrev = 0;
        if( sampleIndex > 0 )
        {
            albedoPrev = g_denoiserAuxAlbedo [pixelPos].xyz;
            normalPrev = g_denoiserAuxNormals[pixelPos].xyz;
        }
        g_denoiserAuxAlbedo [pixelPos].xyz = albedoPrev + materialAlbedo; //lerp( materialAlbedo, float3(1,1,1), saturate((rayPayload.PathSpecularness-0.95)/0.05) );
        g_denoiserAuxNormals[pixelPos].xyz = normalPrev + materialNormal;
    }
}

// Very rudimentary first pass implementation; 
// Threshold depends on:
//  * path specularness so far (higher threshold for mirror surface)
//  * pre-exposure (implicit)
//  * multiplied by roughly sqrt(g_pathTracerConsts.AccumFrameTargetCount) as variance reduces roughly with the square of sample number
float3 RadianceCombineAndFireflyClamp( float pathSpecularness, float3 bxdf, float3 Li )
{
    const float userSetting = g_pathTracerConsts.FireflyClampThreshold;
    const float threshold = userSetting * (4+sqrt( (float)g_pathTracerConsts.AccumFrameTargetCount ));   // to disable set to a very large number
    // no need to modify threshold with g_globals.PreExposureMultiplier because Li values are pre-exposed
    return min( threshold * 0.02 + threshold * pathSpecularness, bxdf * Li );
}

#endif

#endif // VA_PATH_TRACER_SHARED_H