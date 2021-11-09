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
//#define VA_PATH_TRACER_VIEWSPACE_DEPTH_SRV_SLOT             2
#define VA_PATH_TRACER_NULL_ACC_STRUCT                      2
#define VA_PATH_TRACER_DENOISE_AUX_ALBEDO_SRV_SLOT          3
#define VA_PATH_TRACER_DENOISE_AUX_NORMALS_SRV_SLOT         4
#define VA_PATH_TRACER_DENOISE_AUX_MOTIONVEC_SRV_SLOT       5

#define VA_PATH_TRACER_DISPATCH_TILE_SIZE                   8

#define VA_USE_RAY_SORTING                                  1

    // nice 32bit random primes from here: https://asecuritysite.com/encryption/random3?val=32
#define VA_PATH_TRACER_HASH_SEED_AA                      0x09FFF95B
#define VA_PATH_TRACER_HASH_SEED_DIR_INDIR_LIGHTING_1D   0x2FB8FF47      // these are 1D (choice) and 2D (sample) used by both direct and indirect lighting - this can be done because:
#define VA_PATH_TRACER_HASH_SEED_DIR_INDIR_LIGHTING_2D   0x74DDDA53      // Turquin - From Ray to Path Tracing: "Note that you can and should reuse the same sample for light and material sampling at a given depth, since they are independent integral computations, merely combined together in a weighted sum by MIS."
#define VA_PATH_TRACER_HASH_SEED_RUSSIAN_ROULETTE        0x1D6F5FC9
#define VA_PATH_TRACER_HASH_SEED_LIGHTING_SPEC           0xD19ED69B      // used for tree traversal or etc
#define VA_PATH_TRACER_HASH_SEED_PLACEHOLDER2            0xFBD0A37F
#define VA_PATH_TRACER_HASH_SEED_PLACEHOLDER3            0xC6456085
#define VA_PATH_TRACER_HASH_SEED_PLACEHOLDER4            0x8FCEC1EF

    //#define VA_RAYTRACING_BOUCE_COUNT_MASK                  (0xFFFF)        // surely no more than 65535 bounces?
#define VA_PATH_TRACER_FLAG_NOT_USED_AT_THE_MOMENT       (1 << 16)       // this is a visibility only ray - no closest hit shader; this flag serves dual purpose: miss shader clears it to indicate a miss
#define VA_PATH_TRACER_FLAG_LAST_BOUNCE                  (1 << 17)       // 
#define VA_PATH_TRACER_FLAG_PATH_REGULARIZATION          (1 << 18)
#define VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_VIZ          (1 << 19)
#define VA_PATH_TRACER_FLAG_SHOW_DEBUG_LIGHT_VIZ         (1 << 20)
#define VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_DETAIL_VIZ   (1 << 21)
#define VA_PATH_TRACER_FLAG_STOPPED                      (1 << 22)       // 

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
        int                     TemporalNoiseStep;          // i.e. 64 if used, 1 if not
        int                     TemporalNoiseIndex;         // i.e. frameCounter % TemporalNoiseStep if used, 0 if not

        uint                    ViewportX;
        uint                    ViewportY;
        uint                    MaxPathCount;               // total starting ray count (for ex. 1920x1080 it's 1920*1080 or more because each dimension is rounded up to VA_PATH_TRACER_DISPATCH_TILE_SIZE)
        uint                    PerBounceSortEnabled;       // 

        uint                    Flags;                      // see VA_PATH_TRACER_FLAG_XXX
        PathTracerDebugViewType     DebugViewType;
        float                   DebugDivergenceTest;        
        int                     DebugPathVizDim;

        bool                    IgnoreResults( )            { return ( AccumFrameCount >= AccumFrameTargetCount ); }        // do not write out color results
        bool                    IsFirstAccumSample( )       { return AccumFrameCount == 0; }                                // reset to 0 and start accumulating
        uint                    SampleIndex( )              { return VA_MIN( AccumFrameCount, AccumFrameTargetCount-1 ) * TemporalNoiseStep + TemporalNoiseIndex; }   // index of currently computed sample
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
        uint                    Flags;                      // Various VA_PATH_TRACER_FLAG_* flags
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

// in path tracing these two are indexed using the same index
RWStructuredBuffer<ShaderGeometryHitPayload>
                                g_pathGeometryHitPayloads       : register( u1 );
RWStructuredBuffer<ShaderPathPayload>
                                g_pathPayloads                  : register( u2 );

//RWStructuredBuffer<ShaderPathTracerControl>
//                                g_pathTracerControl             : register( u3 );

RWStructuredBuffer<uint>        g_pathListSorted                : register( u3 );   // this could/should be a SRV
RWStructuredBuffer<uint>        g_pathSortKeys                  : register( u4 );

RWTexture2D<float4>             g_denoiserAuxAlbedo             : register( u5 );
RWTexture2D<float4>             g_denoiserAuxNormals            : register( u6 );
RWTexture2D<float2>             g_denoiserAuxMotionVectors      : register( u7 );

Texture2D<float4>               g_radianceAccumulationSRV       : register( T_CONCATENATER( VA_PATH_TRACER_RADIANCE_SRV_SLOT ) );
//Texture2D<float>                g_viewspaceDepthSRV             : register( T_CONCATENATER( VA_PATH_TRACER_VIEWSPACE_DEPTH_SRV_SLOT ) );

Texture2D<float4>               g_denoiserAuxAlbedoSRV          : register( T_CONCATENATER( VA_PATH_TRACER_DENOISE_AUX_ALBEDO_SRV_SLOT  ) );
Texture2D<float4>               g_denoiserAuxNormalsSRV         : register( T_CONCATENATER( VA_PATH_TRACER_DENOISE_AUX_NORMALS_SRV_SLOT ) );
Texture2D<float2>               g_denoiserAuxMotionVectorsSRV   : register( T_CONCATENATER( VA_PATH_TRACER_DENOISE_AUX_MOTIONVEC_SRV_SLOT ) );

void PathTracerCommit( const in uint pathIndex, const in ShaderPathPayload rayPayload )
{
    const uint2 pixelPos            = rayPayload.PixelPos.xy;

    const bool isFirstAccumSample   = g_pathTracerConsts.IsFirstAccumSample();

    const bool debugDrawRays        = (rayPayload.Flags & VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_VIZ) != 0;
    const bool debugDrawRayDetails  = (rayPayload.Flags & VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_DETAIL_VIZ) != 0;

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
        float3 accumRadiance = (isFirstAccumSample)?(float3(0,0,0)):(g_radianceAccumulation[pixelPos.xy].rgb);

        // Write the raytraced color to the output texture.
        g_radianceAccumulation[pixelPos.xy].xyz = float3( accumRadiance.rgb + combinedRadiance );

        // // Only output (viewspace) depth with the first sample
        // // (We output viewspace depth so some rasterized systems can still work, mostly for debugging use; this might need changing once transparencies get re-enabled)
        // if( isFirstAccumSample )
        //     g_viewspaceDepth[pixelPos.xy] = rayPayload.FirstBounceViewspaceDepth;
    }

    if( g_pathTracerConsts.DebugViewType != PathTracerDebugViewType::None )
    {
        if( g_pathTracerConsts.DebugViewType == PathTracerDebugViewType::BounceIndex )
        { 
            g_radianceAccumulation[pixelPos.xy].x = max( 0, rayPayload.BounceIndex-1 );
        }
        else if( (uint)g_pathTracerConsts.DebugViewType >= (uint)PathTracerDebugViewType::SurfacePropsBegin && (uint)g_pathTracerConsts.DebugViewType <= (uint)PathTracerDebugViewType::SurfacePropsEnd )
        {
            g_radianceAccumulation[pixelPos.xy].xyz = rayPayload.AccumulatedRadiance;
        }
    }

    // signal we're done to subsequent passes
    g_pathPayloads[ pathIndex ].Flags |= VA_PATH_TRACER_FLAG_STOPPED;

    // sort key 0 indicates inactive paths - so they quickly get skipped together
    g_pathSortKeys[ pathIndex ] = 0;
}

void PathTracerOutputAUX( uint2 pixelPos, const in ShaderPathPayload rayPayload, float3 worldPos, float3 materialAlbedo, float3 materialNormal, float2 motionVectors )
{
    // convert to viewspace - this is what OptiX temporal wants and OIDN doesn't seem to care
    materialNormal = mul( (float3x3)g_globals.View, materialNormal );

    // output depth if needed
    const int bounceIndex = rayPayload.BounceIndex;
    const bool isFirstAccumSample = g_pathTracerConsts.IsFirstAccumSample();
    if( bounceIndex == 0 && isFirstAccumSample )
        g_radianceAccumulation[pixelPos].w = clamp( dot( g_globals.CameraDirection.xyz, worldPos.xyz - g_globals.CameraWorldPosition.xyz ), g_globals.CameraNearFar.x, g_globals.CameraNearFar.y );

    // only first bounce for now
    if( g_pathTracerConsts.DenoisingEnabled && bounceIndex == 0 )
    {
        float3 albedoPrev = 0;
        float3 normalPrev = 0;
        float2 motionPrev = 0;
        if( !isFirstAccumSample )
        {
            albedoPrev = g_denoiserAuxAlbedo [pixelPos].xyz;
            normalPrev = g_denoiserAuxNormals[pixelPos].xyz;
            motionPrev = g_denoiserAuxMotionVectors[pixelPos].xy;
        }
        g_denoiserAuxAlbedo [pixelPos].xyz          = albedoPrev + materialAlbedo; //lerp( materialAlbedo, float3(1,1,1), saturate((rayPayload.PathSpecularness-0.95)/0.05) );
        g_denoiserAuxNormals[pixelPos].xyz          = normalPrev + materialNormal;
        g_denoiserAuxMotionVectors[pixelPos].xy     = motionPrev + motionVectors;
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