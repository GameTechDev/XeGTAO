///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VA_SAMPLE_SHADOW_MAP_HLSL_INCLUDED
#define VA_SAMPLE_SHADOW_MAP_HLSL_INCLUDED

#include "vaShared.hlsl"

/*

#ifdef VA_VOLUME_SHADOWS_PLUGIN_USE
#include "vaVolumeShadowsPluginSelector.hlsl"
#endif

float SimpleShadowMapSampleInternalLowQ( float4 shadowClipSpace )
{
    return g_SimpleShadowMapTexture.SampleCmpLevelZero( g_SimpleShadowMapCmpSampler, shadowClipSpace.xy, shadowClipSpace.z );
}

float SimpleShadowMapSampleInternalMedQ( float4 shadowClipSpace, float2x2 rotScale )
{
    float ret = 0.0f;

    const int   sampleCount         = 6;
    const float samplingOffsetScale = 1.25f * g_SimpleShadowsGlobal.OneOverShadowMapRes;

    const float2 sampleOffsets[ sampleCount ] = 
    {
        0.779040f, -0.579830f,
        0.233270f,  0.082490f,
        0.508760f,  0.836340f,
       -0.476610f,  0.816540f,
       -0.824430f, -0.320580f,
       -0.105800f, -0.715330f
    };

    rotScale *= samplingOffsetScale;

    for( int i = 0; i < sampleCount; i++ )
        ret += g_SimpleShadowMapTexture.SampleCmpLevelZero( g_SimpleShadowMapCmpSampler, shadowClipSpace.xy + mul( rotScale, sampleOffsets[i] ), shadowClipSpace.z );

    return ret / (float)sampleCount;
}

float SimpleShadowMapSampleInternalHighQ( float4 shadowClipSpace, float2x2 rotScale )
{
    float ret = 0.0f;

    const int   sampleCount         = 14;
    const float samplingOffsetScale = 1.25f * g_SimpleShadowsGlobal.OneOverShadowMapRes;

    const float2 sampleOffsets[ sampleCount ] = 
    {
        0.045020f, -0.987710f,
        0.193940f, -0.397750f,
        0.557220f, -0.795750f,
        0.816880f, -0.353330f,
        0.593080f,  0.117890f,
        0.647170f,  0.742570f,
        0.182410f,  0.503600f,
       -0.213260f,  0.831700f,
       -0.740980f,  0.623680f,
       -0.336390f,  0.278780f,
       -0.850740f,  0.071420f,
       -0.823710f, -0.528410f,
       -0.386520f, -0.264500f,
       -0.434840f, -0.843600f
    };

    rotScale *= samplingOffsetScale;

    for( int i = 0; i < sampleCount; i++ )
        ret += g_SimpleShadowMapTexture.SampleCmpLevelZero( g_SimpleShadowMapCmpSampler, shadowClipSpace.xy + mul( rotScale, sampleOffsets[i] ), shadowClipSpace.z );

    return ret / (float)sampleCount;
}

float SimpleShadowMapSample( float3 viewspacePos )
{
    float4 shadowClipSpace = mul( g_SimpleShadowsGlobal.CameraViewToShadowUVNormalizedSpace, float4( viewspacePos, 1.0 ) );
    shadowClipSpace.xyz /= shadowClipSpace.w;

    //float noise = frac( sin( dot( shadowClipSpace.xy, g_SimpleShadowsGlobal.ShadowMapRes.xx ) ) * 43758.5453 );

#if( 1 && (SHADERSIMPLESHADOWSGLOBAL_QUALITY >= 2) )

    float2 noiseInput = shadowClipSpace.xy * g_SimpleShadowsGlobal.ShadowMapRes.xx * 15.0;

    float fadeOutNoise = saturate( (fwidth( noiseInput.x ) + fwidth( noiseInput.y )) * 0.1 );

#if 0 
    // much more stable, but more expensive noise
    float noise = snoise( noiseInput );
#else
    // very simple and shimmering noise
    float noise = frac( sin( dot( sin( noiseInput.xy * 0.01 ), float2( 12.9898, 78.233 ) ) ) * 43758.5453 );
#endif

    noise = lerp( noise, 0, fadeOutNoise );

    float angle = (noise * 2.0 - 1.0) * 3.1415;

    float ca = cos( angle );
    float sa = sin( angle );

    float2x2 rotScale = float2x2( ca, -sa, sa, ca );

#else

    float2x2 rotScale = {   1.0, 0.0,
                            0.0, 1.0 };

#endif

#if( SHADERSIMPLESHADOWSGLOBAL_QUALITY == 0 )
    float regularShadowOcclusion = SimpleShadowMapSampleInternalLowQ( shadowClipSpace );
#elif( SHADERSIMPLESHADOWSGLOBAL_QUALITY == 1 )
    float regularShadowOcclusion = SimpleShadowMapSampleInternalMedQ( shadowClipSpace, rotScale );
#elif( SHADERSIMPLESHADOWSGLOBAL_QUALITY == 2 )
    float regularShadowOcclusion = SimpleShadowMapSampleInternalHighQ( shadowClipSpace, rotScale );
#endif

#ifdef VA_VOLUME_SHADOWS_PLUGIN_USE

    // using shadow (light) viewspace depth for all volume shadows calculations
    float4 shadowViewspace = mul( g_SimpleShadowsGlobal.CameraViewToShadowView, float4( viewspacePos, 1.0 ) );
    shadowViewspace.xyz /= shadowViewspace.w;
    float volumetricShadowOcclusion = VolumeShadowSample( shadowClipSpace.xy, shadowViewspace.z );

#ifdef VA_VOLUME_SHADOWS_DISABLE_NONVOLUMETRIC_SHADOWS_FOR_DEBUGGING
    regularShadowOcclusion = 1.0;
#endif

    regularShadowOcclusion *= volumetricShadowOcclusion;

#endif

    return regularShadowOcclusion;
//    // noise test
    //float noise = frac( dot( sin( shadowClipSpace.xyz * float3( 53337, 59997, 41117 ) ), float3( 1.0, 1.0, 1.0 ) ) );  
    //return noise;
//    return SimpleShadowMapSampleInternal( shadowClipSpace + float4( noise * g_SimpleShadowsGlobal.SamplingOffsetScale, frac( noise * 10 ) * g_SimpleShadowsGlobal.SamplingOffsetScale, 0.0, 0.0 ) );
}


float SimpleShadowMapSampleVolumetricMedQ( float3 viewspaceEntry, float3 viewspaceExit, float noise, float oneMinusTransmittance )
{
    float4 shadowClipSpaceEntry = mul( g_SimpleShadowsGlobal.CameraViewToShadowUVNormalizedSpace, float4( viewspaceEntry, 1.0 ) );
    shadowClipSpaceEntry.xyz /= shadowClipSpaceEntry.w;
    float4 shadowClipSpaceExit = mul( g_SimpleShadowsGlobal.CameraViewToShadowUVNormalizedSpace, float4( viewspaceExit, 1.0 ) );
    shadowClipSpaceExit.xyz /= shadowClipSpaceExit.w;

    float regularShadowOcclusion = 0.0f;
    {
        const int   sampleCount         = 6;
        const float samplingOffsetScale = 1.25f * g_SimpleShadowsGlobal.OneOverShadowMapRes;

        const float2 sampleOffsets[ sampleCount ] = 
        {
            0.779040f, -0.579830f,
            0.233270f,  0.082490f,
            0.508760f,  0.836340f,
           -0.476610f,  0.816540f,
           -0.824430f, -0.320580f,
           -0.105800f, -0.715330f
        };

    //    float2x2 rotScale = {   1.0, 0.0,
    //                            0.0, 1.0 };
        float angle = (noise * 2.0 - 1.0) * 3.1415;

        float ca = cos( angle );
        float sa = sin( angle );

        float2x2 rotScale = float2x2( ca, -sa, sa, ca );

        rotScale *= samplingOffsetScale;

    #define ENABLE_REALISTIC_SLICE_BLENDING 0

    #if ENABLE_REALISTIC_SLICE_BLENDING

        oneMinusTransmittance = clamp( oneMinusTransmittance, 0.0, 0.999 );
        float sliceTransmittance = pow( 1.0 - oneMinusTransmittance, 1.0 / (float)sampleCount );

        float total         = 0.0;
        float weight        = 1.0;

    #else

        const float total   = (float)sampleCount;
        const float weight  = 1.0;

    #endif

        for( int i = 0; i < sampleCount; i++ )
        {
            float4 shadowClipSpace = lerp( shadowClipSpaceEntry, shadowClipSpaceExit, (i+noise) / (float)(sampleCount) );
        
            regularShadowOcclusion  += weight * g_SimpleShadowMapTexture.SampleCmpLevelZero( g_SimpleShadowMapCmpSampler, shadowClipSpace.xy + mul( rotScale, sampleOffsets[i] ), shadowClipSpace.z );
    #if ENABLE_REALISTIC_SLICE_BLENDING
            total                   += weight;
            weight                  *= sliceTransmittance;
    #endif
        }

        regularShadowOcclusion = regularShadowOcclusion / total; // / (float)sampleCount;
    }

#ifdef VA_VOLUME_SHADOWS_PLUGIN_USE

    float4 shadowClipSpaceC = (shadowClipSpaceEntry + shadowClipSpaceExit) * 0.5;

    // using shadow (light) viewspace depth for all volume shadows calculations
    float3 viewspacePos = (viewspaceEntry + viewspaceExit).xyz * 0.5;
    float4 shadowViewspace = mul( g_SimpleShadowsGlobal.CameraViewToShadowView, float4( viewspacePos, 1.0 ) );
    shadowViewspace.xyz /= shadowViewspace.w;
    float volumetricShadowOcclusion = VolumeShadowSample( shadowClipSpaceC.xy, shadowViewspace.z );

#ifdef VA_VOLUME_SHADOWS_DISABLE_NONVOLUMETRIC_SHADOWS_FOR_DEBUGGING
    regularShadowOcclusion = 1.0;
#endif

    regularShadowOcclusion *= volumetricShadowOcclusion;

#endif

    return regularShadowOcclusion;
}

*/

#endif // VA_SAMPLE_SHADOW_MAP_HLSL_INCLUDED