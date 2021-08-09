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

#include "vaTonemappers.hlsli"

RWTexture2D<float4>             g_outColor0                     : register( u0 );
RWTexture2D<float>              g_outAvgLumScratch              : register( u1 );
RWTexture2D<float>              g_outAvgLum                     : register( u2 );


Texture2D                       g_sourceTexure0                 : register( T_CONCATENATER( POSTPROCESS_TONEMAP_TEXTURE_SLOT0 ) );

RWTexture2D<unorm float>        g_exportLuma                    : register( u1 );

float3 Tonemap( float3 sourceRadiance )
{
    // this no longer needed due to pre-exposing lighting
    // sourceRadiance *= g_postProcessTonemapConsts.Exp2Exposure;

#if 0
    float luminance = CalcLuminance( sourceRadiance );

    // Reinhard's modified tonemapping
	float tonemappedLuminance = luminance * (1.0 + luminance / (g_postProcessTonemapConsts.WhiteLevelSquared)) / (1.0 + luminance);
	float3 tonemapValue = tonemappedLuminance * pow( max( 0.0, sourceRadiance / luminance ), g_postProcessTonemapConsts.Saturation);
    return clamp( tonemapValue, 0.0, 1.0 ); // upper range will be higher than 1.0 if this ever gets extended for HDR output / HDR displays!
#elif 1
    return Tonemap_Lottes( sourceRadiance, 0.8 );
#elif 0
    return Tonemap_Uchimura( sourceRadiance );
#elif 1
    return Tonemap_ACES( sourceRadiance );
#else
    return ACESFitted( sourceRadiance ) );
#endif
}

float4 LoadAndTonemap( const float4 Position, const float2 Texcoord, uniform bool outputLuma )
{
    int i;
    uint2 pixelCoord    = (uint2)Position.xy;

    float3 color = Tonemap( g_sourceTexure0.Load( int3( pixelCoord, 0 ) ).rgb );

    // color = InverseTonemap_ACES( Tonemap_ACES( color ) );

    if( g_postProcessTonemapConsts.DbgGammaTest )
    {
        float2 texDims;
        g_sourceTexure0.GetDimensions( texDims.x, texDims.y );
        color = GammaTest( Position.xy, texDims );
    }

    // todo - custom gamma tweak, here?
    float gammaMod = 0;
    color = pow( color, 1 + gammaMod );

    if( outputLuma )
        g_exportLuma[ pixelCoord ] = RGBToLumaForEdges( color.rgb );

    return float4( color, 1.0 );
}

float4 PSTonemap( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    return LoadAndTonemap( Position, Texcoord, false );
}

float4 PSTonemapWithLumaExport( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    return LoadAndTonemap( Position, Texcoord, true );
}

// consider using clamp from http://graphicrants.blogspot.com/2013/12/tone-mapping.html instead
float3 BloomMaxClamp( float3 color )
{
#if 0
    return min( color, g_postProcessTonemapConsts.BloomMaxClampPE.xxx );
#else
    float m = max( 1, max( max( color.r, color.g ), color.b ) / g_postProcessTonemapConsts.BloomMaxClampPE );
    return color / m.xxx;
#endif
}

float4 PSAddBloom( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    //int2 pixelCoord         = (int2)Position.xy;
    //float3 bloom            = g_sourceTexure0.Load( int3( pixelCoord, 0 ) ).rgb;

    float2 srcUV = Position.xy * g_postProcessTonemapConsts.BloomSampleUVMul;
    float3 bloom = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, srcUV, 0.0 ).xyz;

    bloom = bloom - g_postProcessTonemapConsts.BloomMinThresholdPE;

    return float4( max( 0.0, bloom ) * g_postProcessTonemapConsts.BloomMultiplier, 0.0 );
}

float4 PSPassThrough( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    int2 pixelCoord = (int2)Position.xy;
    return g_sourceTexure0.Load( int3( pixelCoord, 0 ) ).rgba;
}

[numthreads(8, 8, 1)]
void CSDebugColorTest( uint2 dispatchThreadID : SV_DispatchThreadID )
{
    float2 texDims;
    g_outColor0.GetDimensions( texDims.x, texDims.y );

    //g_outColor0[dispatchThreadID] = g_outColor0[dispatchThreadID].bgra;
    //g_outColor0[dispatchThreadID] = g_outColor0[dispatchThreadID] * 2;
    //g_outColor0[dispatchThreadID] = float4( 1, 0, 1, 1 ); //float4( GammaTest( float2(dispatchThreadID) + 0.5, texDims ), 1 );

    float3 color = g_outColor0[dispatchThreadID].rgb;
    const float y  = dot( color, float3( 0.25f, 0.5f, 0.25f ) );
    const float co = dot( color, float3( 0.5f, 0.f, -0.5f ) );
    const float cg = dot( color, float3( -0.25f, 0.5f, -0.25f ) );
    g_outColor0[dispatchThreadID] = float4( y, co, cg, 1 );
}

groupshared float ts_scratchAvgLum[8][8];
[numthreads(8, 8, 1)]
void CSHalfResDownsampleAndAvgLum( uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID )
{
    // collect 2x2 source pixels
    const uint2 baseCoord = dispatchThreadID;
    const float2 texTL = float2( baseCoord * 2 + 0.5 ) * g_postProcessTonemapConsts.ViewportPixelSize;
    float3 col00    = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, texTL, 0, int2(0,0) ).rgb;
    float3 col10    = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, texTL, 0, int2(1,0) ).rgb;
    float3 col01    = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, texTL, 0, int2(0,1) ).rgb;
    float3 col11    = g_sourceTexure0.SampleLevel( g_samplerLinearClamp, texTL, 0, int2(1,1) ).rgb;

    // output halfres color for bloom, with the BloomMaxClamp filter applied
    float4 halfResColor = float4( ( BloomMaxClamp(col00)+BloomMaxClamp(col10)+BloomMaxClamp(col01)+BloomMaxClamp(col11) ) * 0.25, 1 );
    g_outColor0[baseCoord] = halfResColor;

    // compute really-low-res luminance map below

    // 'g_postProcessTonemapConsts.PreExposureMultiplier' is to revert the pre-exposure to get correct luminance
    float lum00 = CalcLuminance( col00 ) / g_postProcessTonemapConsts.PreExposureMultiplier;
    float lum10 = CalcLuminance( col10 ) / g_postProcessTonemapConsts.PreExposureMultiplier;
    float lum01 = CalcLuminance( col01 ) / g_postProcessTonemapConsts.PreExposureMultiplier;
    float lum11 = CalcLuminance( col11 ) / g_postProcessTonemapConsts.PreExposureMultiplier;
    
    // when computing geometric mean - exp( avg( log( x ) ) )
    lum00 = log( lum00 ); 
    lum10 = log( lum10 ); 
    lum01 = log( lum01 ); 
    lum11 = log( lum11 );

    ts_scratchAvgLum[ groupThreadID.x ][ groupThreadID.y ] = (lum00+lum10+lum01+lum11) / 4.0;

    GroupMemoryBarrierWithGroupSync( );

    // 'MIP 2'
    [branch]
    if( all( ( groupThreadID.xy % 2.xx ) == 0 ) )
    {
        float inTL = ts_scratchAvgLum[groupThreadID.x+0][groupThreadID.y+0];
        float inTR = ts_scratchAvgLum[groupThreadID.x+1][groupThreadID.y+0];
        float inBL = ts_scratchAvgLum[groupThreadID.x+0][groupThreadID.y+1];
        float inBR = ts_scratchAvgLum[groupThreadID.x+1][groupThreadID.y+1];
        float dm2 = inTL+inTR+inBL+inBR;
        ts_scratchAvgLum[ groupThreadID.x ][ groupThreadID.y ] = dm2 / 4.0;
    }

    GroupMemoryBarrierWithGroupSync( );

    // 'MIP 3'
    [branch]
    if( all( ( groupThreadID.xy % 4.xx ) == 0 ) )
    {
        float inTL = ts_scratchAvgLum[groupThreadID.x+0][groupThreadID.y+0];
        float inTR = ts_scratchAvgLum[groupThreadID.x+2][groupThreadID.y+0];
        float inBL = ts_scratchAvgLum[groupThreadID.x+0][groupThreadID.y+2];
        float inBR = ts_scratchAvgLum[groupThreadID.x+2][groupThreadID.y+2];
        float dm3 = inTL+inTR+inBL+inBR;
        ts_scratchAvgLum[ groupThreadID.x ][ groupThreadID.y ] = dm3 / 4.0;
    }

    GroupMemoryBarrierWithGroupSync( );

    // 'MIP 4'
    [branch]
    if( all( ( groupThreadID.xy % 8.xx ) == 0 ) )
    {
        float inTL = ts_scratchAvgLum[groupThreadID.x+0][groupThreadID.y+0];
        float inTR = ts_scratchAvgLum[groupThreadID.x+4][groupThreadID.y+0];
        float inBL = ts_scratchAvgLum[groupThreadID.x+0][groupThreadID.y+4];
        float inBR = ts_scratchAvgLum[groupThreadID.x+4][groupThreadID.y+4];
        float dm4 = inTL+inTR+inBL+inBR;
        float final = dm4 / 4.0;
        g_outAvgLumScratch[ baseCoord / 8 ] = final;
    }
}

[numthreads(64, 1, 1)]
void CSAvgLumHoriz( uint dispatchThreadID : SV_DispatchThreadID, uint groupThreadID : SV_GroupThreadID )
{
    uint texSizeX, texSizeY;
    g_outAvgLumScratch.GetDimensions( texSizeX, texSizeY );
    float sum = 0.0;
    for( int y = 0; y < texSizeY; y++ )
        sum += g_outAvgLumScratch[ uint2(dispatchThreadID, y) ];
    sum /= (float)(texSizeY);
    g_outAvgLumScratch[ uint2(dispatchThreadID, 0) ].x = sum;
}

[numthreads(1, 1, 1)]   // this could be easily optimized for ex by using 8 threads and using https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/waveallsum :)
void CSAvgLumVert( uint dispatchThreadID : SV_DispatchThreadID, uint groupThreadID : SV_GroupThreadID )
{
    uint texSizeX, texSizeY;
    g_outAvgLumScratch.GetDimensions( texSizeX, texSizeY );
    float sum = 0.0;
    for( int x = 0; x < texSizeX; x++ )
        sum += g_outAvgLumScratch[ uint2( x, 0 ) ];
    sum /= (float)(texSizeX);
    g_outAvgLum[ uint2(0,0) ] = sum;
}