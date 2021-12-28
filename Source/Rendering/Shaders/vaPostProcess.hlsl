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

RWTexture2D<uint>   g_AVSMDeferredCounterUAV        : register( U_CONCATENATER( POSTPROCESS_COMPARISONRESULTS_UAV_SLOT ) );

Texture2D           g_sourceTexture0                 : register( T_CONCATENATER( POSTPROCESS_TEXTURE_SLOT0 ) );
Texture2D           g_sourceTexture1                 : register( T_CONCATENATER( POSTPROCESS_TEXTURE_SLOT1 ) );
Texture2D           g_sourceTexture2                 : register( T_CONCATENATER( POSTPROCESS_TEXTURE_SLOT2 ) );

// TODO: maybe do this in a compute shader, use SLM 
void PSCompareTextures( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 )
{
    int2 pixelCoord         = (int2)Position.xy;

#if POSTPROCESS_COMPARE_IN_SRGB_SPACE
    float3 col0 = FLOAT3_to_SRGB( g_sourceTexture0.Load( int3( pixelCoord, 0 ) ).rgb );
    float3 col1 = FLOAT3_to_SRGB( g_sourceTexture1.Load( int3( pixelCoord, 0 ) ).rgb );
#else
    float3 col0 = g_sourceTexture0.Load( int3( pixelCoord, 0 ) ).rgb;
    float3 col1 = g_sourceTexture1.Load( int3( pixelCoord, 0 ) ).rgb;
#endif

    // Mean Squared Error
    float3 diff3 = col0.rgb - col1.rgb;
    diff3 = diff3 * diff3;

    uint3 diff3Int = round(diff3*POSTPROCESS_COMPARISONRESULTS_FIXPOINT_MAX);

    uint sizeX, sizeY;
    g_sourceTexture0.GetDimensions( sizeX, sizeY );
    uint linearAddr = (uint)pixelCoord.x + (uint)pixelCoord.y * sizeX;
    int UAVAddr = linearAddr % (uint)(POSTPROCESS_COMPARISONRESULTS_SIZE);

    InterlockedAdd( g_AVSMDeferredCounterUAV[ int2(UAVAddr*2+0, 0) ], diff3Int.x );
    InterlockedAdd( g_AVSMDeferredCounterUAV[ int2(UAVAddr*2+1, 0) ], diff3Int.y );
    InterlockedAdd( g_AVSMDeferredCounterUAV[ int2(UAVAddr*2+2, 0) ], diff3Int.z );
}

float4 PSDrawTexturedQuad( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    int2 pixelCoord         = (int2)Position.xy;
    float4 col = g_sourceTexture0.Load( int3( pixelCoord, 0 ) );
    return col;
}

void VSStretchRect( out float4 xPos : SV_Position, in float2 UV : TEXCOORD0, out float2 outUV : TEXCOORD0 ) 
{ 
    xPos.x = (1.0 - UV.x) * g_postProcessConsts.Param1.x + UV.x * g_postProcessConsts.Param1.z;
    xPos.y = (1.0 - UV.y) * g_postProcessConsts.Param1.y + UV.y * g_postProcessConsts.Param1.w;
    xPos.z = 0;
    xPos.w = 1;

    outUV.x = (1.0 - UV.x) * g_postProcessConsts.Param2.x + UV.x * g_postProcessConsts.Param2.z;
    outUV.y = (1.0 - UV.y) * g_postProcessConsts.Param2.y + UV.y * g_postProcessConsts.Param2.w;
}

float4 PSStretchRectLinear( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    float4 col = g_sourceTexture0.Sample( g_samplerLinearClamp, Texcoord );
    return col * g_postProcessConsts.Param3 + g_postProcessConsts.Param4;
}

float4 PSStretchRectPoint( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    float4 col = g_sourceTexture0.Sample( g_samplerPointClamp, Texcoord );
    return col * g_postProcessConsts.Param3 + g_postProcessConsts.Param4;
}

float PSDepthToViewspaceLinear( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    float v = g_sourceTexture0.Load( int3( int2(Position.xy), 0 ) ).x;
    return NDCToViewDepth( v );
}

float PSDepthToViewspaceLinearDS2x2( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    int3 basePos = int3(Position.xy, 0) * 2;

    // yes, gather will be a better idea, I know. On todo list.
    float v0 = g_sourceTexture0.Load( basePos, int2( 0, 0 ) ).x;
    float v1 = g_sourceTexture0.Load( basePos, int2( 1, 0 ) ).x;
    float v2 = g_sourceTexture0.Load( basePos, int2( 0, 1 ) ).x;
    float v3 = g_sourceTexture0.Load( basePos, int2( 1, 1 ) ).x;
 
#ifdef VA_DEPTHDOWNSAMPLE_USE_LINEAR_AVERAGE
    return ( NDCToViewDepth( v0 ) + NDCToViewDepth( v1 ) + NDCToViewDepth( v2 ) + NDCToViewDepth( v3 ) ) / 4.0;
#else
    #ifdef VA_DEPTHDOWNSAMPLE_USE_MAX
        float v = max( max( v0, v1 ), max( v2, v3 ) );
    #else
        float v = min( min( v0, v1 ), min( v2, v3 ) );
    #endif
        return NDCToViewDepth( v );
#endif
}

float PSDepthToViewspaceLinearDS4x4( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    int3 basePos = int3(Position.xy, 0) * 4;

    float v;
    // yes, gather will be a better idea, I know. On todo list.
    {
        float v0 = g_sourceTexture0.Load( basePos, int2( 0 + 0, 0 + 0 ) ).x;
        float v1 = g_sourceTexture0.Load( basePos, int2( 0 + 1, 0 + 0 ) ).x;
        float v2 = g_sourceTexture0.Load( basePos, int2( 0 + 0, 0 + 1 ) ).x;
        float v3 = g_sourceTexture0.Load( basePos, int2( 0 + 1, 0 + 1 ) ).x;
    #ifdef VA_DEPTHDOWNSAMPLE_USE_LINEAR_AVERAGE
        v = NDCToViewDepth( v0 ) + NDCToViewDepth( v1 ) + NDCToViewDepth( v2 ) + NDCToViewDepth( v3 );
    #else
        #ifdef VA_DEPTHDOWNSAMPLE_USE_MAX
            v = max( max( v0, v1 ), max( v2, v3 ) );
        #else
            v = min( min( v0, v1 ), min( v2, v3 ) );
        #endif
    #endif
    }
    
    {
        float v0 = g_sourceTexture0.Load( basePos, int2( 2 + 0, 0 + 0 ) ).x;
        float v1 = g_sourceTexture0.Load( basePos, int2( 2 + 1, 0 + 0 ) ).x;
        float v2 = g_sourceTexture0.Load( basePos, int2( 2 + 0, 0 + 1 ) ).x;
        float v3 = g_sourceTexture0.Load( basePos, int2( 2 + 1, 0 + 1 ) ).x;
    #ifdef VA_DEPTHDOWNSAMPLE_USE_LINEAR_AVERAGE
        v += NDCToViewDepth( v0 ) + NDCToViewDepth( v1 ) + NDCToViewDepth( v2 ) + NDCToViewDepth( v3 );
    #else
        #ifdef VA_DEPTHDOWNSAMPLE_USE_MAX
            v = max( v, max( max( v0, v1 ), max( v2, v3 ) ) );
        #else
            v = min( v, min( min( v0, v1 ), min( v2, v3 ) ) );
        #endif
    #endif
    }
    {
        float v0 = g_sourceTexture0.Load( basePos, int2( 0 + 0, 2 + 0 ) ).x;
        float v1 = g_sourceTexture0.Load( basePos, int2( 0 + 1, 2 + 0 ) ).x;
        float v2 = g_sourceTexture0.Load( basePos, int2( 0 + 0, 2 + 1 ) ).x;
        float v3 = g_sourceTexture0.Load( basePos, int2( 0 + 1, 2 + 1 ) ).x;
    #ifdef VA_DEPTHDOWNSAMPLE_USE_LINEAR_AVERAGE
        v += NDCToViewDepth( v0 ) + NDCToViewDepth( v1 ) + NDCToViewDepth( v2 ) + NDCToViewDepth( v3 );
    #else
        #ifdef VA_DEPTHDOWNSAMPLE_USE_MAX
            v = max( v, max( max( v0, v1 ), max( v2, v3 ) ) );
        #else
            v = min( v, min( min( v0, v1 ), min( v2, v3 ) ) );
        #endif
    #endif
    }
    {
        float v0 = g_sourceTexture0.Load( basePos, int2( 2 + 0, 2 + 0 ) ).x;
        float v1 = g_sourceTexture0.Load( basePos, int2( 2 + 1, 2 + 0 ) ).x;
        float v2 = g_sourceTexture0.Load( basePos, int2( 2 + 0, 2 + 1 ) ).x;
        float v3 = g_sourceTexture0.Load( basePos, int2( 2 + 1, 2 + 1 ) ).x;
    #ifdef VA_DEPTHDOWNSAMPLE_USE_LINEAR_AVERAGE
        v += NDCToViewDepth( v0 ) + NDCToViewDepth( v1 ) + NDCToViewDepth( v2 ) + NDCToViewDepth( v3 );
    #else
        #ifdef VA_DEPTHDOWNSAMPLE_USE_MAX
            v = max( v, max( max( v0, v1 ), max( v2, v3 ) ) );
        #else
            v = min( v, min( min( v0, v1 ), min( v2, v3 ) ) );
        #endif
    #endif
    }

#ifdef VA_DEPTHDOWNSAMPLE_USE_LINEAR_AVERAGE
    return v / 16.0;
#else
    return NDCToViewDepth( v );
#endif
}


#ifdef VA_DRAWSINGLESAMPLEFROMMSTEXTURE_SAMPLE
Texture2DMS<float4>             g_screenTexture             : register( t0 );
float4 SingleSampleFromMSTexturePS( in float4 inPos : SV_Position ) : SV_Target0
{
    return g_screenTexture.Load( inPos.xy, VA_DRAWSINGLESAMPLEFROMMSTEXTURE_SAMPLE );
}
#endif

//#ifdef VA_POSTPROCESS_COLORPROCESS // just a laceholder for debugging stuff ATM
//#if VA_POSTPROCESS_COLORPROCESS_MS_COUNT == 1
//Texture2D<float>                g_srcTexture              : register( t0 );
//#else
//Texture2DMS<float>              g_srcTexture              : register( t0 );
//#endif
//float4 ColorProcessPS( in float4 inPos : SV_Position ) : SV_Target0
//{
//    float4 val, valRef;
//#if VA_POSTPROCESS_COLORPROCESS_MS_COUNT == 1
//    valRef   = g_srcTexture.Load( int3( inPos.xy + int2( 1, 0 ), 0 ) );
//    val      = g_srcTexture.Load( int3( inPos.xy, 0 ), int2( 1, 0 ) );
//#else
////    val = 1.0.xxxx;
////    for( uint i = 0; i < VA_POSTPROCESS_COLORPROCESS_MS_COUNT; i++ )
////        val = min( val, g_srcTexture.Load( int2( inPos.xy ), i, int2( 5, 0 ) ) );
//
//    valRef   = g_srcTexture.Load( int2( inPos.xy ) + int2( 1, 0 ), 0 );
//    val      = g_srcTexture.Load( int2( inPos.xy ), 0, int2( 1, 0 ) );
//
//#endif
//
//    bool thisIsWrong = val.x != valRef.x;
//
//    return float4( thisIsWrong, frac(val.x*10), frac(val.x*100), 1.0 );
//}
//#endif

#ifdef VA_POSTPROCESS_COLOR_HSBC
Texture2D<float4>                g_srcTexture              : register( t0 );
float4 ColorProcessHSBCPS( in float4 inPos : SV_Position ) : SV_Target0
{
    float3 color = g_srcTexture.Load( int3( inPos.xy, 0 ) ).rgb;
    //return float4( color.bgr, 1 );

    const float modHue      = g_postProcessConsts.Param1.x;
    const float modSat      = g_postProcessConsts.Param1.y;
    const float modBrig     = g_postProcessConsts.Param1.z;
    const float modContr    = g_postProcessConsts.Param1.w;

    float3 colorHSV = RGBtoHSV(color);

    colorHSV.x = GLSL_mod( colorHSV.x + modHue, 1.0 );
    colorHSV.y = saturate( colorHSV.y * modSat );
    colorHSV.z = colorHSV.z * modBrig;

    color = HSVtoRGB(colorHSV);

    color = lerp( color, float3( 0.5, 0.5, 0.5 ), -modContr );
    
    color = saturate(color);
    
    return float4( color.rgb, 1 );
}
#endif

#ifdef VA_POSTPROCESS_SIMPLE_BLUR_SHARPEN
Texture2D<float4>                g_srcTexture              : register( t0 );
float4 SimpleBlurSharpen( in float4 inPos : SV_Position ) : SV_Target0
{
    float3 colorSum = float3( 0, 0, 0 );
    float weights[3][3] = {    { g_postProcessConsts.Param1.x, g_postProcessConsts.Param1.y,   g_postProcessConsts.Param1.x },
                                { g_postProcessConsts.Param1.y, 1.0,                            g_postProcessConsts.Param1.y },
                                { g_postProcessConsts.Param1.x, g_postProcessConsts.Param1.y,   g_postProcessConsts.Param1.x } };
    float weightSum = g_postProcessConsts.Param1.x * 4 + g_postProcessConsts.Param1.y * 4 + 1;

    [unroll]
    for( int y = 0; y < 3; y++ )
        [unroll]
        for( int x = 0; x < 3; x++ )
            colorSum += g_srcTexture.Load( int3( inPos.xy, 0 ), int2( x-1, y-1 ) ).rgb * weights[x][y];

    colorSum /= weightSum;
    
    return float4( colorSum.rgb, 1 );
}
#endif

#ifdef VA_POSTPROCESS_LUMA_FOR_EDGES
Texture2D<float4>                g_srcTexture              : register( t0 );
float4 ColorProcessLumaForEdges( in float4 inPos : SV_Position ) : SV_Target0
{
    float3 color = g_srcTexture.Load( int3( inPos.xy, 0 ) ).rgb;

    return RGBToLumaForEdges( color ).xxxx;
}
#endif

#ifdef VA_POSTPROCESS_MIP_FILTERS
Texture2D<float4>                g_srcTexture              : register( t0 );
float4 MIPFilterNormalsXY_UNORM( in float4 inPos : SV_Position ) : SV_Target0
{
    float3 n0 = NormalDecode_XY_UNORM( g_srcTexture.Load( int3( inPos.xy, 0 ) * 2, int2( 0, 0 ) ).xy );
    float3 n1 = NormalDecode_XY_UNORM( g_srcTexture.Load( int3( inPos.xy, 0 ) * 2, int2( 1, 0 ) ).xy );
    float3 n2 = NormalDecode_XY_UNORM( g_srcTexture.Load( int3( inPos.xy, 0 ) * 2, int2( 0, 1 ) ).xy );
    float3 n3 = NormalDecode_XY_UNORM( g_srcTexture.Load( int3( inPos.xy, 0 ) * 2, int2( 1, 1 ) ).xy );

    float3 n = (n0 + n1 + n2 + n3)/4;
    n.z = sqrt( 1.00001 - n.x*n.x - n.y * n.y );

    return float4( NormalEncode_XY_UNORM( n ), 0, 1 );
}
#endif

#ifdef VA_POSTPROCESS_DOWNSAMPLE
Texture2D<float4>                g_srcTexture              : register( t0 );
float4 Downsample4x4to1x1( in float4 inPos : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target0
{
    float4 colorAccum = float4( 0, 0, 0, 0 );
    
    // // TODO: use bilinear filter to do it in 1/4 samples...
    // [unroll] for( int x = 0; x < 4; x++ )
    //     [unroll] for( int y = 0; y < 4; y++ )
    //         colorAccum += g_srcTexture.Load( int3( inPos.xy, 0 )*4, int2( x, y ) ).rgba;
    // 
    // colorAccum /= 16.0;

    float2 pixelSize    = g_postProcessConsts.Param1.xy;
    float2 centerUV     = Texcoord; // inPos.xy * 4 * pixelSize
    float sharpenScale  = g_postProcessConsts.Param1.z;

    colorAccum += g_srcTexture.Sample( g_samplerLinearClamp, centerUV + sharpenScale * float2( +pixelSize.x, +pixelSize.y ) );
    colorAccum += g_srcTexture.Sample( g_samplerLinearClamp, centerUV + sharpenScale * float2( -pixelSize.x, +pixelSize.y ) );
    colorAccum += g_srcTexture.Sample( g_samplerLinearClamp, centerUV + sharpenScale * float2( +pixelSize.x, -pixelSize.y ) );
    colorAccum += g_srcTexture.Sample( g_samplerLinearClamp, centerUV + sharpenScale * float2( -pixelSize.x, -pixelSize.y ) );

    colorAccum /= 4.0f;

    return colorAccum;
}
#endif

float4 PSSmartOffscreenUpsampleComposite( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    float refViewspaceDepth = NDCToViewDepth( g_sourceTexture2.Load( int3( Position.xy, 0 ) ).x );
    
    // when using FP16 for floats this is max value
    refViewspaceDepth = min( refViewspaceDepth, 65500.0 );

#if 0 // debug depths
    float dbgV = g_sourceTexture1.Sample( g_samplerPointClamp, Texcoord ).x;
    return float4( frac( dbgV.xxx ), 1 );
#endif

    float2 srcResolution = g_postProcessConsts.Param3.xy;

    float2 srcCoord = Texcoord * srcResolution - float2( 0.5, 0.5 );
    int2 srcBaseCoord = (int2)srcCoord;

    float4 col00  = g_sourceTexture0.Load( int3( srcBaseCoord, 0 ), int2( 0, 0 ) );
    float4 col10  = g_sourceTexture0.Load( int3( srcBaseCoord, 0 ), int2( 1, 0 ) );
    float4 col01  = g_sourceTexture0.Load( int3( srcBaseCoord, 0 ), int2( 0, 1 ) );
    float4 col11  = g_sourceTexture0.Load( int3( srcBaseCoord, 0 ), int2( 1, 1 ) );

    float  dpt00 = g_sourceTexture1.Load( int3( srcBaseCoord, 0 ), int2( 0, 0 ) ).x;
    float  dpt10 = g_sourceTexture1.Load( int3( srcBaseCoord, 0 ), int2( 1, 0 ) ).x;
    float  dpt01 = g_sourceTexture1.Load( int3( srcBaseCoord, 0 ), int2( 0, 1 ) ).x;
    float  dpt11 = g_sourceTexture1.Load( int3( srcBaseCoord, 0 ), int2( 1, 1 ) ).x;

    // return BilinearFilter( col00, col10, col01, col11, srcCoord );

    // weighted filter
    float4 col;
    {
        // reduce contribution based on depth delta - set it to 0.2 as max, but this is just a guess
        const float threshold = 0.2;
        float4 dpts     = float4( dpt00, dpt10, dpt01, dpt11 );
        float4 weights  = saturate( 1.0 - ( (max( refViewspaceDepth, dpts ) / min( refViewspaceDepth, dpts )) - 1.0) / threshold.xxxx ) + 0.0001;

        float2 intPt    = floor(srcCoord);
        float2 fractPt  = frac(srcCoord);
        float4 top      = lerp( col00 * weights.x, col10 * weights.y, fractPt.x );
        float4 bottom   = lerp( col01 * weights.z, col11 * weights.w, fractPt.x );
        col = lerp( top, bottom, fractPt.y );
        float topW      = lerp( weights.x, weights.y, fractPt.x );
        float bottomW   = lerp( weights.z, weights.w, fractPt.x );
        float filteredW = lerp( topW, bottomW, fractPt.y );

        // TODO: to further reduce acne, in case filteredW is smaller/same than 0.0001 (or whatever it is set to above), meaning all samples are invalid,
        // try sampling two additional colors and depths outside of the kernel itself, closest to the sampling point.
        col /= filteredW + 0.0001;
    }

    return col;
}


#ifdef VA_POSTPROCESS_MERGETEXTURES

#if VA_POSTPROCESS_MERGETEXTURES_UINT_VALUES
Texture2D<uint4>            g_srcA                 : register( T_CONCATENATER( POSTPROCESS_TEXTURE_SLOT0 ) );
Texture2D<uint4>            g_srcB                 : register( T_CONCATENATER( POSTPROCESS_TEXTURE_SLOT1 ) );
Texture2D<uint4>            g_srcC                 : register( T_CONCATENATER( POSTPROCESS_TEXTURE_SLOT2 ) );
#else
Texture2D<float4>           g_srcA                 : register( T_CONCATENATER( POSTPROCESS_TEXTURE_SLOT0 ) );
Texture2D<float4>           g_srcB                 : register( T_CONCATENATER( POSTPROCESS_TEXTURE_SLOT1 ) );
Texture2D<float4>           g_srcC                 : register( T_CONCATENATER( POSTPROCESS_TEXTURE_SLOT2 ) );
#endif
float4 PSMergeTextures( in float4 inPos : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target0
{
#if VA_POSTPROCESS_MERGETEXTURES_UINT_VALUES
    uint4 srcA = g_srcA[(uint2)inPos.xy]; //.Sample( g_samplerPointClamp, Texcoord );
    uint4 srcB = g_srcB[(uint2)inPos.xy]; //.Sample( g_samplerPointClamp, Texcoord );
    uint4 srcC = g_srcC[(uint2)inPos.xy]; //.Sample( g_samplerPointClamp, Texcoord );
#else
    float4 srcA = g_srcA[(uint2)inPos.xy]; //.Sample( g_samplerPointClamp, Texcoord );
    float4 srcB = g_srcB[(uint2)inPos.xy]; //.Sample( g_samplerPointClamp, Texcoord );
    float4 srcC = g_srcC[(uint2)inPos.xy]; //.Sample( g_samplerPointClamp, Texcoord );
#endif
    
    return VA_POSTPROCESS_MERGETEXTURES_CODE;
}
#endif

#ifdef VA_POSTPROCESS_3DTEXTURESTUFF
Texture2D<float4>                   g_srcTexture              : register( t0 );
RWTexture3D<float4>                 g_dstTexture              : register( u0 );
[numthreads(8, 8, 1)]
void CSCopySliceTo3DTexture( const uint2 pixCoord : SV_DispatchThreadID )
{
    if( !all( pixCoord < g_postProcessConsts.Param1.xy ) )
        return;

    g_dstTexture[ int3(pixCoord, g_postProcessConsts.Param1.z) ] = g_sourceTexture0.Load( int3(pixCoord, 0) );
}
#endif

#ifdef VA_POSTPROCESS_MOTIONVECTORS

Texture2D<float>            g_sourceDepth           : register( t0 );   // source (clip space) depth (in our case NDC too?)
RWTexture2D<float4>         g_outputMotionVectors   : register( u0 );
RWTexture2D<float>          g_outputDepths          : register( u1 );

//float ViewspaceDepthToTAACompDepthFunction( float viewspaceDepth )
//{
//    // better utilizes FP16 precision
//    return viewspaceDepth / 100.0;
//}

[numthreads(MOTIONVECTORS_BLOCK_SIZE_X, MOTIONVECTORS_BLOCK_SIZE_Y, 1)]
void CSGenerateMotionVectors( uint2 dispatchThreadID : SV_DispatchThreadID )
{
    uint2 pixCoord  = dispatchThreadID.xy;

    // already filled
    if( g_outputDepths[ pixCoord ] != 0 )
        return;

    float depthNDC  = g_sourceDepth.Load( int3(pixCoord, 0)/*, offset*/).x;

    float depth     = NDCToViewDepth( depthNDC );

    float2 screenXY = pixCoord + 0.5f;
    float2 ndcXY    = ScreenToNDCSpaceXY( screenXY ); //, depthNDC );

    float4 reprojectedPos = mul( g_globals.ReprojectionMatrix, float4( ndcXY.xy, depthNDC, 1 ) );
    reprojectedPos.xyz /= reprojectedPos.w;
    float reprojectedDepth = NDCToViewDepth( reprojectedPos.z );

    // reduce 16bit precision issues - push the older frame ever so slightly into foreground
    reprojectedDepth *= 0.99999;

    float3 delta;
    delta.xy = NDCToScreenSpaceXY( reprojectedPos.xy ) - screenXY;
    delta.z = reprojectedDepth - depth;

    // de-jitter! not sure if this is the best way to do it for everything, but it's required for TAA
    delta.xy -= g_globals.CameraJitterDelta;

    g_outputDepths[ pixCoord ]          = depth;
    g_outputMotionVectors[ pixCoord ]   = float4( delta.xyz, 0 ); //(uint)(frac( clip ) * 10000);
}

#endif

#if defined( VA_POSTPROCESS_CLEAR_UAV_TEX1D_1F ) // 1D textures 
RWTexture1D<float>          g_clearTarget   : register( u0 );
[numthreads(64, 1, 1)] void CSClearUAV_Tex1D_1F( uint dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[dispatchThreadID] = g_postProcessConsts.Param1.x; }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX1D_4F )
RWTexture1D<float4>         g_clearTarget   : register( u0 );
[numthreads(64, 1, 1)] void CSClearUAV_Tex1D_4F( uint dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[dispatchThreadID] = g_postProcessConsts.Param1.xyzw; }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX1D_1U )
RWTexture1D<uint>          g_clearTarget   : register( u0 );
[numthreads(64, 1, 1)] void CSClearUAV_Tex1D_1U( uint dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[dispatchThreadID] = asuint(g_postProcessConsts.Param1.x); }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX1D_4U )
RWTexture1D<uint4>         g_clearTarget   : register( u0 );
[numthreads(64, 1, 1)] void CSClearUAV_Tex1D_4U( uint dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[dispatchThreadID] = asuint(g_postProcessConsts.Param1.xyzw); }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX2D_1F ) // 2D textures 
RWTexture2D<float>          g_clearTarget   : register( u0 );
[numthreads(8, 8, 1)] void CSClearUAV_Tex2D_1F( uint2 dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[dispatchThreadID] = g_postProcessConsts.Param1.x; }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX2D_4F )
RWTexture2D<float4>         g_clearTarget   : register( u0 );
[numthreads(8, 8, 1)] void CSClearUAV_Tex2D_4F( uint2 dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[dispatchThreadID] = g_postProcessConsts.Param1.xyzw; }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX2D_1U )
RWTexture2D<uint>          g_clearTarget   : register( u0 );
[numthreads(8, 8, 1)] void CSClearUAV_Tex2D_1U( uint2 dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[dispatchThreadID] = asuint(g_postProcessConsts.Param1.x); }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX2D_4U )
RWTexture2D<uint4>         g_clearTarget   : register( u0 );
[numthreads(8, 8, 1)] void CSClearUAV_Tex2D_4U( uint2 dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[dispatchThreadID] = asuint(g_postProcessConsts.Param1.xyzw); }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_BUFF_1U ) // buffers
RWBuffer<uint>              g_clearTarget   : register( u0 );
[numthreads(64, 1, 1)] void CSClearUAV_Buff_1U( uint dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[dispatchThreadID] = asuint(g_postProcessConsts.Param1.x); }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_BUFF_4U )
RWBuffer<uint4>             g_clearTarget   : register( u0 );
[numthreads(64, 1, 1)] void CSClearUAV_Buff_4U( uint dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[dispatchThreadID] = asuint(g_postProcessConsts.Param1.xyzw); }
#endif