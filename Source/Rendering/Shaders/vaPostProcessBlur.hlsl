///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaShared.hlsl"

#include "vaPostProcessShared.h"

Texture2D            g_sourceTexure0               : register( T_CONCATENATER( POSTPROCESS_BLUR_TEXTURE_SLOT0 ) );
Texture2D            g_sourceTexure1               : register( T_CONCATENATER( POSTPROCESS_BLUR_TEXTURE_SLOT1 ) );

/*
float4 PSBlurA( in float4 inPos : SV_POSITION, in float2 inUV : TEXCOORD0 ) : SV_Target
{
    float2 halfPixel = g_postProcessBlurConsts.PixelSize * 0.5f;

    // scale by blur factor
    halfPixel *= g_postProcessBlurConsts.Factor0;

    float4 vals[4];
    vals[0] = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, inUV + float2( -halfPixel.x, -halfPixel.y * 3 ), 0.0 );
    vals[1] = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, inUV + float2( +halfPixel.x * 3, -halfPixel.y ), 0.0 );
    vals[2] = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, inUV + float2( +halfPixel.x, +halfPixel.y * 3 ), 0.0 );
    vals[3] = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, inUV + float2( -halfPixel.x * 3, +halfPixel.y ), 0.0 );

    float4 avgVal = (vals[0] + vals[1] + vals[2] + vals[3]) * 0.25;

    // avgVal = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, inUV, 0 );

    return avgVal;
}

float4 PSBlurB( in float4 inPos : SV_POSITION, in float2 inUV : TEXCOORD0 ) : SV_Target
{
    float2 halfPixel = g_postProcessBlurConsts.PixelSize * 0.5f;

    // scale by blur factor
    halfPixel *= g_postProcessBlurConsts.Factor0;

    float4 vals[4];
    vals[0] = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, inUV + float2( -halfPixel.x * 3, -halfPixel.y ), 0.0 );
    vals[1] = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, inUV + float2( +halfPixel.x, -halfPixel.y * 3 ), 0.0 );
    vals[2] = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, inUV + float2( +halfPixel.x * 3, +halfPixel.y ), 0.0 );
    vals[3] = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, inUV + float2( -halfPixel.x, +halfPixel.y * 3 ), 0.0 );

    float4 avgVal = (vals[0] + vals[1] + vals[2] + vals[3]) * 0.25;

    // avgVal = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, inUV, 0 );

    return avgVal;
}
*/

float3 GaussianBlur( float2 centreUV, float2 pixelOffset )
{
    float3 colOut = float3( 0, 0, 0 );

    const int stepCount = g_postProcessBlurConsts.GaussIterationCount;

    for( int i = 0; i < stepCount; i++ )                                                                                                                             
    {
        float offset = g_postProcessBlurConsts.GaussOffsetsWeights[i].x;
        float weight = g_postProcessBlurConsts.GaussOffsetsWeights[i].y;

        float2 texCoordOffset = offset.xx * pixelOffset;

        float3 col = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, centreUV + texCoordOffset, 0.0 ).xyz
                   + g_sourceTexure0.SampleLevel( g_samplerLinearClamp, centreUV - texCoordOffset, 0.0 ).xyz;

        colOut += weight.xxx * col;                                                                                                                               
    }                                                                                                                                                                

    return colOut;                                                                                                                                                   
}

RWTexture2D<float4>             g_outColor0                     : register( u0 );

[numthreads(8, 8, 1)]
void CSGaussHorizontal( uint2 dispatchThreadID : SV_DispatchThreadID )
{
    float2 inUV = (dispatchThreadID + 0.5) * g_postProcessBlurConsts.PixelSize.xy;

    float3 col = GaussianBlur( inUV, float2( g_postProcessBlurConsts.PixelSize.x, 0.0 ) );

    g_outColor0[dispatchThreadID] = float4( col, 1.0 );
}

[numthreads(8, 8, 1)]
void CSGaussVertical( uint2 dispatchThreadID : SV_DispatchThreadID )
{
    float2 inUV = (dispatchThreadID + 0.5) * g_postProcessBlurConsts.PixelSize.xy;

    float3 col = GaussianBlur( inUV, float2( 0.0, g_postProcessBlurConsts.PixelSize.y ) );

    g_outColor0[dispatchThreadID] = float4( col, 1.0 );
}