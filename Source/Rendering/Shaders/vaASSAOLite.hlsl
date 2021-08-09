///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef VA_COMPILED_AS_SHADER_CODE
// Vanilla-specific; this include gets intercepted and macros are provided through it to allow for some macro
// shenanigans that don't work through the normal macro string pairs (like #include macros!).
#include "MagicMacrosMagicFile.h"
#endif

#ifdef VA_RAYTRACING   // special section below for raytracing - used only during development
#include "vaRaytracingShared.h"
#define ASSAO_DEFINE_EXTERNAL_SAMPLERS
#define ASSAO_DEFINE_EXTERNAL_SRVS_UAVS
#include "PoissonDisks\vaPoissonDisk16_2.0.h"
#endif


#ifndef __INTELLISENSE__
#include "vaASSAOLite_types.h"
#endif

// progressive poisson-like pattern; x, y are in [-1, 1] range, .z is length( float2(x,y) ), .w is log2( z )
#define INTELSSAO_MAIN_DISK_SAMPLE_COUNT (32)

static const float4 g_samplePatternMain[INTELSSAO_MAIN_DISK_SAMPLE_COUNT] =
{
     0.78488064,  0.56661671,  1.500000, -0.126083,        0.26022232, -0.29575172,  1.500000, -1.064030,        0.10459357,  0.08372527,  1.110000, -2.730563,       -0.68286800,  0.04963045,  1.090000, -0.498827,
    -0.13570161, -0.64190155,  1.250000, -0.532765,       -0.26193795, -0.08205118,  0.670000, -1.783245,       -0.61177456,  0.66664219,  0.710000, -0.044234,        0.43675563,  0.25119025,  0.610000, -1.167283,
     0.07884444,  0.86618668,  0.640000, -0.459002,       -0.12790935, -0.29869005,  0.600000, -1.729424,       -0.04031125,  0.02413622,  0.600000, -4.792042,        0.16201244, -0.52851415,  0.790000, -1.067055,
    -0.70991218,  0.47301072,  0.640000, -0.335236,        0.03277707, -0.22349690,  0.600000, -1.982384,        0.68921727,  0.36800742,  0.630000, -0.266718,        0.29251814,  0.37775412,  0.610000, -1.422520,
    -0.12224089,  0.96582592,  0.600000, -0.426142,        0.11071457, -0.16131058,  0.600000, -2.165947,        0.46562141, -0.59747696,  0.600000, -0.189760,       -0.51548797,  0.11804193,  0.600000, -1.246800,
     0.89141309, -0.42090443,  0.600000,  0.028192,       -0.32402530, -0.01591529,  0.600000, -1.543018,        0.60771245,  0.41635221,  0.600000, -0.605411,        0.02379565, -0.08239821,  0.600000, -3.809046,
     0.48951152, -0.23657045,  0.600000, -1.189011,       -0.17611565, -0.81696892,  0.600000, -0.513724,       -0.33930185, -0.20732205,  0.600000, -1.698047,       -0.91974425,  0.05403209,  0.600000,  0.062246,
    -0.15064627, -0.14949332,  0.600000, -1.896062,        0.53180975, -0.35210401,  0.600000, -0.758838,        0.41487166,  0.81442589,  0.600000, -0.505648,       -0.24106961, -0.32721516,  0.600000, -1.665244
};

// these values can be changed (up to INTELSSAO_MAIN_DISK_SAMPLE_COUNT) with no changes required elsewhere; they represent number of taps used by low/medium/high/highest presets
static const uint g_numTaps[3]   = { 4, 11, 22 };


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Optional parts that can be enabled for a required quality preset level and above (0 == Low, 1 == Medium, 2 == High, 3 == Highest/Adaptive, 4 == reference/unused )
// Each has its own cost. To disable just set to 5 or above.
//
// (experimental) tilts the disk (although only half of the samples!) towards surface normal; this helps with effect uniformity between objects but reduces effect distance and has other side-effects
#define ASSAO_TILT_SAMPLES_ENABLE_AT_QUALITY_PRESET                      (99)        // to disable simply set to 99 or similar
#define ASSAO_TILT_SAMPLES_AMOUNT                                        (0.4)
//
#define ASSAO_HALOING_REDUCTION_ENABLE_AT_QUALITY_PRESET                 (0)         // to disable simply set to 99 or similar
#define ASSAO_HALOING_REDUCTION_AMOUNT                                   (0.7)       // values from 0.0 - 1.0, 1.0 means max weighting (will cause artifacts, 0.8 is more reasonable)
//
#define ASSAO_NORMAL_BASED_EDGES_ENABLE_AT_QUALITY_PRESET                (1)         // to disable simply set to 99 or similar
#define ASSAO_NORMAL_BASED_EDGES_DOT_THRESHOLD                           (0.2)       // use 0-0.1 for super-sharp normal-based edges
//
#define ASSAO_DETAIL_AO_ENABLE_AT_QUALITY_PRESET                         (0)         // whether to use DetailAOStrength; to disable simply set to 99 or similar
//
#define ASSAO_DEPTH_MIPS_GLOBAL_OFFSET                                   (-4.1)      // best noise/quality/performance tradeoff, found empirically
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// define your ASSAO_DEFINE_EXTERNAL_SAMPLERS to provide samplers
// SamplerState  g_samplerLinearClamp : register( s1 );
// SamplerState  g_samplerPointClamp : register( s2 );
#ifdef ASSAO_DEFINE_EXTERNAL_SAMPLERS
ASSAO_DEFINE_EXTERNAL_SAMPLERS
#else
SamplerState                    g_samplerPointClamp     : register( S_CONCATENATER( ASSAO_POINTCLAMP_SAMPLERSLOT         ) ); 
SamplerState                    g_samplerLinearClamp    : register( S_CONCATENATER( ASSAO_LINEARCLAMP_SAMPLERSLOT        ) ); 
#endif

#ifdef ASSAO_DEFINE_EXTERNAL_CONSTANTBUFFER
ASSAO_DEFINE_EXTERNAL_CONSTANTBUFFER
#else
cbuffer ASSAOConstantBuffer                              : register( B_CONCATENATER( ASSAO_CONSTANTBUFFER_SLOT ) )
{
    ASSAOConstants               g_ASSAOConsts;
}
#endif

#ifdef ASSAO_DEFINE_EXTERNAL_SRVS_UAVS
ASSAO_DEFINE_EXTERNAL_SRVS_UAVS
#else

Texture2D<float>                g_sourceNDCDepth            : register( T_CONCATENATER( ASSAO_SRV_SOURCE_NDC_DEPTH_SLOT          ) );
#ifndef ASSAO_GENERATE_NORMALS // special decoding for external normals
Texture2D<uint>                 g_sourceNormalmap           : register( T_CONCATENATER( ASSAO_SRV_SOURCE_NORMALMAP_SLOT          ) );
#else
Texture2D                       g_sourceNormalmap           : register( T_CONCATENATER( ASSAO_SRV_SOURCE_NORMALMAP_SLOT          ) );
#endif
Texture2DArray<float>           g_workingDepth              : register( T_CONCATENATER( ASSAO_SRV_WORKING_DEPTH_SLOT             ) );
Texture2DArray                  g_workingOcclusionEdge      : register( T_CONCATENATER( ASSAO_SRV_WORKING_OCCLUSION_EDGE_SLOT    ) );

RWTexture2DArray<float>         g_outputDepthArray          : register( U_CONCATENATER( ASSAO_UAV_DEPTHS_SLOT                    ) );
RWTexture2DArray<float>         g_outputDepthArrayMIP1      : register( U_CONCATENATER( ASSAO_UAV_DEPTHS_MIP1_SLOT               ) );
RWTexture2DArray<float>         g_outputDepthArrayMIP2      : register( U_CONCATENATER( ASSAO_UAV_DEPTHS_MIP2_SLOT               ) );
RWTexture2DArray<float>         g_outputDepthArrayMIP3      : register( U_CONCATENATER( ASSAO_UAV_DEPTHS_MIP3_SLOT               ) );
RWTexture2D<unorm float3>       g_outputNormalmap           : register( U_CONCATENATER( ASSAO_UAV_NORMALMAP_SLOT                 ) ); 

RWTexture2DArray<unorm float2>  g_outputOcclusionEdge       : register( U_CONCATENATER( ASSAO_UAV_OCCLUSION_EDGE_SLOT            ) );
RWTexture2D<unorm float>        g_outputFinal           : register( U_CONCATENATER( ASSAO_UAV_FINAL_OCCLUSION_SLOT   ) );

#if defined( ASSAO_DEBUG_SHOWNORMALS ) || defined( ASSAO_DEBUG_SHOWEDGES )
RWTexture2D<unorm float4>       g_outputDebugImage      : register( U_CONCATENATER( ASSAO_UAV_DEBUG_IMAGE_SLOT                   ) );
#endif

#if defined(ASSAO_SHADER_PING) || defined(ASSAO_SHADER_PONG)  // if there are separate shaders for pinging ponging when custom slot assignment is not available on the engine side
Texture2DArray                  g_workingOcclusionEdgeB  : register( U_CONCATENATER( ASSAO_SRV_WORKING_OCCLUSION_EDGE_B_SLOT     ) );
RWTexture2DArray<unorm float2>  g_outputOcclusionEdgeB   : register( U_CONCATENATER( ASSAO_UAV_OCCLUSION_EDGE_B_SLOT             ) );
#endif

#endif // #ifdef ASSAO_DEFINE_EXTERNAL_SRVS_UAVS

// handle ping-pong shader compilation using macros
#ifdef ASSAO_SHADER_PONG
#define g_workingOcclusionEdge g_workingOcclusionEdgeB
#endif
#ifdef ASSAO_SHADER_PING
#define g_outputOcclusionEdge  g_outputOcclusionEdgeB
#endif
#if defined(ASSAO_SHADER_PING) && defined(ASSAO_SHADER_PONG)
#error Can't have ping and pong together!
#endif


// packing/unpacking for edges; 2 bits per edge mean 4 gradient values (0, 0.33, 0.66, 1) for smoother transitions!
float PackEdges( float4 edgesLRTB )
{
    // optimized, should be same as above
    edgesLRTB = round( saturate( edgesLRTB ) * 3.05 );
    return dot( edgesLRTB, float4( 64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0 ) ) ;
}

float4 UnpackEdges( float _packedVal )
{
    uint packedVal = (uint)(_packedVal * 255.5);
    float4 edgesLRTB;
    edgesLRTB.x = float((packedVal >> 6) & 0x03) / 3.0;          // there's really no need for mask (as it's an 8 bit input) but I'll leave it in so it doesn't cause any trouble in the future
    edgesLRTB.y = float((packedVal >> 4) & 0x03) / 3.0;
    edgesLRTB.z = float((packedVal >> 2) & 0x03) / 3.0;
    edgesLRTB.w = float((packedVal >> 0) & 0x03) / 3.0;

    return saturate( edgesLRTB + g_ASSAOConsts.InvSharpness );
}

float ScreenSpaceToViewSpaceDepth( float screenDepth )
{
    float depthLinearizeMul = g_ASSAOConsts.DepthUnpackConsts.x;
    float depthLinearizeAdd = g_ASSAOConsts.DepthUnpackConsts.y;

    // Optimised version of "-cameraClipNear / (cameraClipFar - projDepth * (cameraClipFar - cameraClipNear)) * cameraClipFar"

    // Set your depthLinearizeMul and depthLinearizeAdd to:
    // depthLinearizeMul = ( cameraClipFar * cameraClipNear) / ( cameraClipFar - cameraClipNear );
    // depthLinearizeAdd = cameraClipFar / ( cameraClipFar - cameraClipNear );

    return depthLinearizeMul / ( depthLinearizeAdd - screenDepth );
}

// from [0, width], [0, height] to [-1, 1], [-1, 1]
float2 ScreenSpaceToClipSpacePositionXY( float2 screenPos )
{
    return screenPos * g_ASSAOConsts.Viewport2xPixelSize.xy - float2( 1.0f, 1.0f );
}

float3 ScreenSpaceToViewSpacePosition( float2 screenPos, float viewspaceDepth )
{
    return float3( g_ASSAOConsts.CameraTanHalfFOV.xy * viewspaceDepth * ScreenSpaceToClipSpacePositionXY( screenPos ), viewspaceDepth );
}

float3 ClipSpaceToViewSpacePosition( float2 clipPos, float viewspaceDepth )
{
    return float3( g_ASSAOConsts.CameraTanHalfFOV.xy * viewspaceDepth * clipPos, viewspaceDepth );
}

float3 NDCToViewspace( float2 pos, float viewspaceDepth )
{
    float3 ret;

    ret.xy = (g_ASSAOConsts.NDCToViewMul * pos.xy + g_ASSAOConsts.NDCToViewAdd) * viewspaceDepth;

    ret.z = viewspaceDepth;

    return ret;
}

// calculate effect radius and fit our screen sampling pattern inside it
void CalculateRadiusParameters( const float pixCenterLength, const float2 pixelDirRBViewspaceSizeAtCenterZ, out float pixLookupRadiusMod, out float effectRadius, out float falloffCalcMulSq )
{
    effectRadius = g_ASSAOConsts.EffectRadius;

//    // leaving this out for performance reasons: use something similar if radius needs to scale based on distance
    effectRadius *= pow( pixCenterLength, g_ASSAOConsts.RadiusDistanceScalingFunctionPow);

    // when too close, on-screen sampling disk will grow beyond screen size; limit this to avoid closeup temporal artifacts
    const float tooCloseLimitMod = saturate( pixCenterLength * g_ASSAOConsts.EffectSamplingRadiusNearLimitRec ) * 0.8 + 0.2;
    
    effectRadius *= tooCloseLimitMod;

    // 0.85 is to reduce the radius to allow for more samples on a slope to still stay within influence
    pixLookupRadiusMod = (0.85 * effectRadius) / pixelDirRBViewspaceSizeAtCenterZ.x;

    // used to calculate falloff (both for AO samples and per-sample weights)
    falloffCalcMulSq= -1.0f / (effectRadius*effectRadius);
}

float4 CalculateEdges( const float centerZ, const float leftZ, const float rightZ, const float topZ, const float bottomZ )
{
    // slope-sensitive depth-based edge detection
    float4 edgesLRTB = float4( leftZ, rightZ, topZ, bottomZ ) - centerZ;
    float4 edgesLRTBSlopeAdjusted = edgesLRTB + edgesLRTB.yxwz;
    edgesLRTB = min( abs( edgesLRTB ), abs( edgesLRTBSlopeAdjusted ) );
    return saturate( ( 1.25 - edgesLRTB / (centerZ * 0.04) ) );

    // cheaper version but has artifacts
    // edgesLRTB = abs( float4( leftZ, rightZ, topZ, bottomZ ) - centerZ; );
    // return saturate( ( 1.3 - edgesLRTB / (pixZ * 0.06 + 0.1) ) );
}


float DepthLoadAndConvertToViewspace( int2 baseCoord, int2 offset )
{
    return ScreenSpaceToViewSpaceDepth( g_sourceNDCDepth.Load( int3(baseCoord, 0), offset ).x );
}

float3 DecodeNormal( float3 encodedNormal )
{
    float3 normal = encodedNormal * 2.0.xxx - 1.0.xxx;
    // normal = normalize( normal );    // normalize adds around 2.5% cost on High settings but makes little (PSNR 66.7) visual difference when normals are as in the sample (stored in R8G8B8A8_UNORM,
    //                                  // decoded in the shader), however it will likely be required if using different encoding/decoding or the inputs are not normalized, etc.
    return normal;
}

float3 LoadNormal( int2 pos, int2 offset )
{
#ifndef ASSAO_GENERATE_NORMALS // special decoding for external normals
    uint packedInput = g_sourceNormalmap.Load( int3( pos, 0 ), offset ).x;
    float3 unpackedOutput;
    unpackedOutput.x = (float)( ( packedInput       ) & 0x000007ff ) / 2047.0f;
    unpackedOutput.y = (float)( ( packedInput >> 11 ) & 0x000007ff ) / 2047.0f;
    unpackedOutput.z = (float)( ( packedInput >> 22 ) & 0x000003ff ) / 1023.0f;
    float3 normal = normalize(unpackedOutput * 2.0.xxx - 1.0.xxx);
    // worldspace to viewspace
    return mul( (float3x3)g_ASSAOConsts.ViewMatrix, normal );
#else    
    float3 encodedNormal = g_sourceNormalmap.Load( int3( pos, 0 ), offset ).xyz;
    return DecodeNormal( encodedNormal );
#endif
}

float3 LoadNormal( int2 pos )
{
    return LoadNormal( pos, int2(0,0) );
}

float3 CalculateNormal( const float4 edgesLRTB, float3 pixCenterPos, float3 pixLPos, float3 pixRPos, float3 pixTPos, float3 pixBPos )
{
    // Get this pixel's viewspace normal
    float4 acceptedNormals  = float4( edgesLRTB.x*edgesLRTB.z, edgesLRTB.z*edgesLRTB.y, edgesLRTB.y*edgesLRTB.w, edgesLRTB.w*edgesLRTB.x );

    pixLPos = normalize(pixLPos - pixCenterPos);
    pixRPos = normalize(pixRPos - pixCenterPos);
    pixTPos = normalize(pixTPos - pixCenterPos);
    pixBPos = normalize(pixBPos - pixCenterPos);

    float3 pixelNormal = float3( 0, 0, -0.0005 );
    pixelNormal += ( acceptedNormals.x ) * cross( pixLPos, pixTPos );
    pixelNormal += ( acceptedNormals.y ) * cross( pixTPos, pixRPos );
    pixelNormal += ( acceptedNormals.z ) * cross( pixRPos, pixBPos );
    pixelNormal += ( acceptedNormals.w ) * cross( pixBPos, pixLPos );
    pixelNormal = normalize( pixelNormal );
    
    return pixelNormal;
}

float ComputeDepthMIPAverage( float tl, float tr, float bl, float br )
{
    float dummyUnused1;
    float dummyUnused2;
    float falloffCalcMulSq, falloffCalcAdd;
 
    float4 depths = float4( tl, tr, bl, br );

    float closest = min( min( depths.x, depths.y ), min( depths.z, depths.w ) );

    CalculateRadiusParameters( abs( closest ), 1.0, dummyUnused1, dummyUnused2, falloffCalcMulSq );

    float4 dists = depths - closest.xxxx;

    float4 weights = saturate( dists * dists * falloffCalcMulSq + 1.0 );

    float smartAvg = dot( weights, depths ) / dot( weights, float4( 1.0, 1.0, 1.0, 1.0 ) );

    // const uint pseudoRandomIndex = ( pseudoRandomA + i ) % 4;

    //depthsOutArr[i] = closest;
    //depthsOutArr[i] = depths[ pseudoRandomIndex ];
    return smartAvg;
}

float3 EncodeNormal( float3 normal )
{
    return normal * 0.5 + 0.5;
}

groupshared float4 g_viewZs[ASSAO_NUMTHREADS_X][ASSAO_NUMTHREADS_Y];
[numthreads(ASSAO_NUMTHREADS_X, ASSAO_NUMTHREADS_Y, 1)] // <- this last one is not layered, always use 1 instead of ASSAO_NUMTHREADS_LAYERED_Z
void CSPrepareDepthsAndNormals( uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID )
{
// disabled because it crashes some drivers 
//    // early out if outside of viewport
//    [branch] if( any( dispatchThreadID.xy >= g_ASSAOConsts.HalfViewportSize ) )
//        return;

    uint2 baseCoord = uint2(dispatchThreadID.xy) * 2;
    // gather can be a bit faster but doesn't work with input depth buffers that don't match the working viewport
    float out0 = DepthLoadAndConvertToViewspace( baseCoord, int2( 0, 0 ) );
    float out1 = DepthLoadAndConvertToViewspace( baseCoord, int2( 1, 0 ) );
    float out2 = DepthLoadAndConvertToViewspace( baseCoord, int2( 0, 1 ) );
    float out3 = DepthLoadAndConvertToViewspace( baseCoord, int2( 1, 1 ) );

    g_outputDepthArray[ uint3(dispatchThreadID.xy, 0) ] = out0;
    g_outputDepthArray[ uint3(dispatchThreadID.xy, 1) ] = out1;
    g_outputDepthArray[ uint3(dispatchThreadID.xy, 2) ] = out2;
    g_outputDepthArray[ uint3(dispatchThreadID.xy, 3) ] = out3;

#ifdef ASSAO_GENERATE_NORMALS
    {
        float pixZs[4][4];

        // middle 4
        pixZs[1][1] = out0;
        pixZs[2][1] = out1;
        pixZs[1][2] = out2;
        pixZs[2][2] = out3;
        // left 2
        pixZs[0][1] = DepthLoadAndConvertToViewspace( baseCoord, int2( -1, 0 ) ); 
        pixZs[0][2] = DepthLoadAndConvertToViewspace( baseCoord, int2( -1, 1 ) ); 
        // right 2
        pixZs[3][1] = DepthLoadAndConvertToViewspace( baseCoord, int2(  2, 0 ) ); 
        pixZs[3][2] = DepthLoadAndConvertToViewspace( baseCoord, int2(  2, 1 ) ); 
        // top 2
        pixZs[1][0] = DepthLoadAndConvertToViewspace( baseCoord, int2(  0, -1 ) );
        pixZs[2][0] = DepthLoadAndConvertToViewspace( baseCoord, int2(  1, -1 ) );
        // bottom 2
        pixZs[1][3] = DepthLoadAndConvertToViewspace( baseCoord, int2(  0,  2 ) );
        pixZs[2][3] = DepthLoadAndConvertToViewspace( baseCoord, int2(  1,  2 ) );

        float4 edges0 = CalculateEdges( pixZs[1][1], pixZs[0][1], pixZs[2][1], pixZs[1][0], pixZs[1][2] );
        float4 edges1 = CalculateEdges( pixZs[2][1], pixZs[1][1], pixZs[3][1], pixZs[2][0], pixZs[2][2] );
        float4 edges2 = CalculateEdges( pixZs[1][2], pixZs[0][2], pixZs[2][2], pixZs[1][1], pixZs[1][3] );
        float4 edges3 = CalculateEdges( pixZs[2][2], pixZs[1][2], pixZs[3][2], pixZs[2][1], pixZs[2][3] );

        float3 pixPos[4][4];

        // there is probably a way to optimize the math below; however no approximation will work, has to be precise.
        float2 inPos = float2(dispatchThreadID.xy) + float2(0.5, 0.5);
        float2 upperLeftUV = (inPos.xy - 0.25) * g_ASSAOConsts.Viewport2xPixelSize;

        // middle 4
        pixPos[1][1] = NDCToViewspace( upperLeftUV + g_ASSAOConsts.ViewportPixelSize * float2( 0.0,  0.0 ), pixZs[1][1] );
        pixPos[2][1] = NDCToViewspace( upperLeftUV + g_ASSAOConsts.ViewportPixelSize * float2( 1.0,  0.0 ), pixZs[2][1] );
        pixPos[1][2] = NDCToViewspace( upperLeftUV + g_ASSAOConsts.ViewportPixelSize * float2( 0.0,  1.0 ), pixZs[1][2] );
        pixPos[2][2] = NDCToViewspace( upperLeftUV + g_ASSAOConsts.ViewportPixelSize * float2( 1.0,  1.0 ), pixZs[2][2] );
        // left 2
        pixPos[0][1] = NDCToViewspace( upperLeftUV + g_ASSAOConsts.ViewportPixelSize * float2( -1.0,  0.0), pixZs[0][1] );
        pixPos[0][2] = NDCToViewspace( upperLeftUV + g_ASSAOConsts.ViewportPixelSize * float2( -1.0,  1.0), pixZs[0][2] );
        // right 2                                                                                     
        pixPos[3][1] = NDCToViewspace( upperLeftUV + g_ASSAOConsts.ViewportPixelSize * float2(  2.0,  0.0), pixZs[3][1] );
        pixPos[3][2] = NDCToViewspace( upperLeftUV + g_ASSAOConsts.ViewportPixelSize * float2(  2.0,  1.0), pixZs[3][2] );
        // top 2                                                                                       
        pixPos[1][0] = NDCToViewspace( upperLeftUV + g_ASSAOConsts.ViewportPixelSize * float2( 0.0, -1.0 ), pixZs[1][0] );
        pixPos[2][0] = NDCToViewspace( upperLeftUV + g_ASSAOConsts.ViewportPixelSize * float2( 1.0, -1.0 ), pixZs[2][0] );
        // bottom 2                                                                                    
        pixPos[1][3] = NDCToViewspace( upperLeftUV + g_ASSAOConsts.ViewportPixelSize * float2( 0.0,  2.0 ), pixZs[1][3] );
        pixPos[2][3] = NDCToViewspace( upperLeftUV + g_ASSAOConsts.ViewportPixelSize * float2( 1.0,  2.0 ), pixZs[2][3] );

        float3 norm0 = CalculateNormal( edges0, pixPos[1][1], pixPos[0][1], pixPos[2][1], pixPos[1][0], pixPos[1][2] );
        float3 norm1 = CalculateNormal( edges1, pixPos[2][1], pixPos[1][1], pixPos[3][1], pixPos[2][0], pixPos[2][2] );
        float3 norm2 = CalculateNormal( edges2, pixPos[1][2], pixPos[0][2], pixPos[2][2], pixPos[1][1], pixPos[1][3] );
        float3 norm3 = CalculateNormal( edges3, pixPos[2][2], pixPos[1][2], pixPos[3][2], pixPos[2][1], pixPos[2][3] );

        g_outputNormalmap[ baseCoord.xy + int2( 0, 0 ) ] = EncodeNormal( norm0 );
        g_outputNormalmap[ baseCoord.xy + int2( 1, 0 ) ] = EncodeNormal( norm1 );
        g_outputNormalmap[ baseCoord.xy + int2( 0, 1 ) ] = EncodeNormal( norm2 );
        g_outputNormalmap[ baseCoord.xy + int2( 1, 1 ) ] = EncodeNormal( norm3 );
    }
#endif

    uint2 mip0Coords = groupThreadID.xy;
    g_viewZs[mip0Coords.x][mip0Coords.y] = float4( out0, out1, out2, out3 );

    GroupMemoryBarrierWithGroupSync( );
    [branch]
    if( all((groupThreadID.xy%2.xx)==0) )
    {
        float4 inTL = g_viewZs[groupThreadID.x+0][groupThreadID.y+0];
        float4 inTR = g_viewZs[groupThreadID.x+1][groupThreadID.y+0];
        float4 inBL = g_viewZs[groupThreadID.x+0][groupThreadID.y+1];
        float4 inBR = g_viewZs[groupThreadID.x+1][groupThreadID.y+1];

        out0 = ComputeDepthMIPAverage( inTL.x, inTR.x, inBL.x, inBR.x );
        out1 = ComputeDepthMIPAverage( inTL.y, inTR.y, inBL.y, inBR.y );
        out2 = ComputeDepthMIPAverage( inTL.z, inTR.z, inBL.z, inBR.z );
        out3 = ComputeDepthMIPAverage( inTL.w, inTR.w, inBL.w, inBR.w );

        g_outputDepthArrayMIP1[ uint3(dispatchThreadID.xy/2, 0) ] = out0;
        g_outputDepthArrayMIP1[ uint3(dispatchThreadID.xy/2, 1) ] = out1;
        g_outputDepthArrayMIP1[ uint3(dispatchThreadID.xy/2, 2) ] = out2;
        g_outputDepthArrayMIP1[ uint3(dispatchThreadID.xy/2, 3) ] = out3;
        g_viewZs[groupThreadID.x][groupThreadID.y] = float4( out0, out1, out2, out3 );
    }
    GroupMemoryBarrierWithGroupSync( );
    [branch]
    if( all((groupThreadID.xy%4.xx)==0) )
    {
        float4 inTL = g_viewZs[groupThreadID.x+0][groupThreadID.y+0];
        float4 inTR = g_viewZs[groupThreadID.x+2][groupThreadID.y+0];
        float4 inBL = g_viewZs[groupThreadID.x+0][groupThreadID.y+2];
        float4 inBR = g_viewZs[groupThreadID.x+2][groupThreadID.y+2];

        out0 = ComputeDepthMIPAverage( inTL.x, inTR.x, inBL.x, inBR.x );
        out1 = ComputeDepthMIPAverage( inTL.y, inTR.y, inBL.y, inBR.y );
        out2 = ComputeDepthMIPAverage( inTL.z, inTR.z, inBL.z, inBR.z );
        out3 = ComputeDepthMIPAverage( inTL.w, inTR.w, inBL.w, inBR.w );

        g_outputDepthArrayMIP2[ uint3(dispatchThreadID.xy/4, 0) ] = out0;
        g_outputDepthArrayMIP2[ uint3(dispatchThreadID.xy/4, 1) ] = out1;
        g_outputDepthArrayMIP2[ uint3(dispatchThreadID.xy/4, 2) ] = out2;
        g_outputDepthArrayMIP2[ uint3(dispatchThreadID.xy/4, 3) ] = out3;
        g_viewZs[groupThreadID.x][groupThreadID.y] = float4( out0, out1, out2, out3 );
    }
    GroupMemoryBarrierWithGroupSync( );
    [branch]
    if( all((groupThreadID.xy%8.xx)==0) )
    {
        float4 inTL = g_viewZs[0][0];
        float4 inTR = g_viewZs[4][0];
        float4 inBL = g_viewZs[0][4];
        float4 inBR = g_viewZs[4][4];

        out0 = ComputeDepthMIPAverage( inTL.x, inTR.x, inBL.x, inBR.x );
        out1 = ComputeDepthMIPAverage( inTL.y, inTR.y, inBL.y, inBR.y );
        out2 = ComputeDepthMIPAverage( inTL.z, inTR.z, inBL.z, inBR.z );
        out3 = ComputeDepthMIPAverage( inTL.w, inTR.w, inBL.w, inBR.w );

        g_outputDepthArrayMIP3[ uint3(dispatchThreadID.xy/8, 0) ] = out0;
        g_outputDepthArrayMIP3[ uint3(dispatchThreadID.xy/8, 1) ] = out1;
        g_outputDepthArrayMIP3[ uint3(dispatchThreadID.xy/8, 2) ] = out2;
        g_outputDepthArrayMIP3[ uint3(dispatchThreadID.xy/8, 3) ] = out3;
        //g_viewZs[mip3Coords.x][mip3Coords.y] = float4( out0, out1, out2, out3 );
    }
}

// all vectors in viewspace; returns .x obscurance, .y weight
float2 CalculatePixelObscurance( uniform int qualityLevel, float3 pixelNormal, float3 hitDelta, float falloffCalcMulSq )
{
    float lengthSq = dot( hitDelta, hitDelta );
    float NdotD = dot(pixelNormal, hitDelta) / sqrt(lengthSq);

    float falloffMult = max( 0.0, lengthSq * falloffCalcMulSq + 1.0 );

    float obscurance = max( 0, NdotD - g_ASSAOConsts.EffectHorizonAngleThreshold ) * falloffMult;
    float weight = 1.0;

    // this reduces the weight of samples that are very far from the radius
    if( qualityLevel >= ASSAO_HALOING_REDUCTION_ENABLE_AT_QUALITY_PRESET )
    {
        //float reduct = max( 0, abs(hitDelta.z) ); // version 1
        float reduct = max( 0, -hitDelta.z );   // version 2
        reduct = saturate( reduct * g_ASSAOConsts.NegRecEffectRadius + 2.0 ); // saturate( 2.0 - reduct / g_ASSAOConsts.EffectRadius );
        weight = ASSAO_HALOING_REDUCTION_AMOUNT * reduct + (1.0 - ASSAO_HALOING_REDUCTION_AMOUNT);
    }
    return float2( obscurance, weight );
}

float2 SSAOTapInner( uniform int qualityLevel, const float2 samplingUV, const float mipLevel, uniform float layer, const float3 pixCenterPos, const float3 negViewspaceDir,
					float3 pixelNormal, const float falloffCalcMulSq )
{
    // get depth at sample
    float viewspaceSampleZ = g_workingDepth.SampleLevel( g_samplerPointClamp, float3( samplingUV.xy, layer ), mipLevel ).x; // * g_ASSAOConsts.MaxViewspaceDepth;

    // convert to viewspace
    float3 hitPos = NDCToViewspace( samplingUV.xy, viewspaceSampleZ ).xyz;
    float3 hitDelta = hitPos - pixCenterPos;
    
    return CalculatePixelObscurance( qualityLevel, pixelNormal, hitDelta, falloffCalcMulSq );
}

void SSAOTap( const int qualityLevel, const int tapIndex, const float2x2 rotScale, const float3 pixCenterPos, const float3 negViewspaceDir, float3 pixelNormal, 
					const float2 normalizedScreenPos, const float mipOffset, uniform uint layer, const float falloffCalcMulSq, out float2 outSample0, out float2 outSample1)
{
    float4  newSample       = g_samplePatternMain[tapIndex];
    float2  sampleOffset    = mul( rotScale, newSample.xy );
    float   samplePow2Len   = newSample.w;                      // precalculated, same as: samplePow2Len = log2( length( newSample.xy ) );

    // snap to pixel center (more correct obscurance math, avoids artifacts)
    sampleOffset            = round(sampleOffset);
    // convert to normalized screen coords
    sampleOffset            *= g_ASSAOConsts.Viewport2xPixelSize;

    // calculate MIP based on the sample distance from the centre, similar to as described in http://graphics.cs.williams.edu/papers/SAOHPG12/.
    float mipLevel          = samplePow2Len + mipOffset;

    float2 samplingUV       = sampleOffset + normalizedScreenPos;
    outSample0 = SSAOTapInner( qualityLevel, samplingUV, mipLevel, layer, pixCenterPos, negViewspaceDir, pixelNormal, falloffCalcMulSq );

    // for the second tap, just use the mirrored offset (sample offset already snapped/rounded)
    float2 samplingUVMirror = -sampleOffset + normalizedScreenPos;
    outSample1 = SSAOTapInner( qualityLevel, samplingUVMirror, mipLevel, layer, pixCenterPos, negViewspaceDir, pixelNormal, falloffCalcMulSq );
}

// this function is designed to only work with half/half depth at the moment - there's a couple of hardcoded paths that expect pixel/texel size, so it will not work for full res
void GenerateSSAOShadowsInternal( const uint2 dispatchPos, uniform int qualityLevel, uniform uint layer )
{
    float4 valuesUL     = g_workingDepth.GatherRed( g_samplerPointClamp, float3( dispatchPos * g_ASSAOConsts.HalfViewportPixelSize, layer )               );
    float4 valuesBR     = g_workingDepth.GatherRed( g_samplerPointClamp, float3( dispatchPos * g_ASSAOConsts.HalfViewportPixelSize, layer ), int2( 1, 1 ) );

    // get this pixel's viewspace depth
    float pixZ = valuesUL.y; //float pixZ = g_workingDepth.SampleLevel( g_samplerPointClamp, normalizedScreenPos, 0.0 ).x; // * g_ASSAOConsts.MaxViewspaceDepth;

    // calculate distance-based fadeout (1 close, gradient, 0 far)
    float fadeOut = saturate( pixZ * g_ASSAOConsts.EffectFadeOutMul + g_ASSAOConsts.EffectFadeOutAdd );

    // early out if beyond range (sky for ex.)
    [branch] if( any( dispatchPos.xy >= g_ASSAOConsts.HalfViewportSize ) || (fadeOut == 0) )
    {
        g_outputOcclusionEdge[uint3(dispatchPos, layer)] = float2( 1, PackEdges( 0 ) );
        return;
    }
 
    const int numberOfTaps = g_numTaps[qualityLevel];

    const uint2 perPassFullResCoordOffset   = float2( layer % 2, layer / 2 );

    // get left right top bottom neighbouring pixels for edge detection (gets compiled out on qualityLevel == 0)
    float pixLZ   = valuesUL.x;
    float pixTZ   = valuesUL.z;
    float pixRZ   = valuesBR.z;
    float pixBZ   = valuesBR.x;

    float2 normalizedScreenPos = dispatchPos * g_ASSAOConsts.Viewport2xPixelSize + g_ASSAOConsts.Viewport2xPixelSize_x_025;
    float3 pixCenterPos = NDCToViewspace( normalizedScreenPos, pixZ ); // g

    // Load this pixel's viewspace normal
    uint2 fullResCoord = dispatchPos * 2 + perPassFullResCoordOffset;
    float3 pixelNormal = LoadNormal( fullResCoord );

    const float2 pixelDirRBViewspaceSizeAtCenterZ = pixCenterPos.z * g_ASSAOConsts.NDCToViewMul * g_ASSAOConsts.Viewport2xPixelSize;  // optimized approximation of:  float2 pixelDirRBViewspaceSizeAtCenterZ = NDCToViewspace( normalizedScreenPos.xy + g_ASSAOConsts.ViewportPixelSize.xy, pixCenterPos.z ).xy - pixCenterPos.xy;

    float pixLookupRadiusMod;
    float falloffCalcMulSq;

    // calculate effect radius and fit our screen sampling pattern inside it
    float effectViewspaceRadius;
    CalculateRadiusParameters( length( pixCenterPos ), pixelDirRBViewspaceSizeAtCenterZ, pixLookupRadiusMod, effectViewspaceRadius, falloffCalcMulSq );

    // calculate samples rotation/scaling
    float2x2 rotScale;
    {
        // load & update pseudo-random rotation matrix
        uint pseudoRandomIndex = uint( dispatchPos.y * 2 + dispatchPos.x ) % 5;
        float4 rs = g_ASSAOConsts.PatternRotScaleMatrices[ layer * 5 + pseudoRandomIndex ];
        rotScale = float2x2( rs.x * pixLookupRadiusMod, rs.y * pixLookupRadiusMod, rs.z * pixLookupRadiusMod, rs.w * pixLookupRadiusMod );
    }

    // edge mask for between this and left/right/top/bottom neighbour pixels - not used in quality level 0 so initialize to "no edge" (1 is no edge, 0 is edge)
    float4 edgesLRTB = float4( 1.0, 1.0, 1.0, 1.0 );

    // Move center pixel slightly towards camera to avoid imprecision artifacts due to using of 16bit depth buffer; a lot smaller offsets needed when using 32bit floats
    // If the depth precision is switched to 32bit float, this can be set to something closer to 1 (0.9999 is fine)
    pixCenterPos.z *= 0.9992;

    edgesLRTB = CalculateEdges( pixZ, pixLZ, pixRZ, pixTZ, pixBZ );

    // the main obscurance & sample weight storage
    float obscuranceSum = 0.0;
    float weightSum     = 0.0;

    // adds a more high definition sharp effect, which gets blurred out (reuses left/right/top/bottom samples that we used for edge detection)
    if( qualityLevel >= ASSAO_DETAIL_AO_ENABLE_AT_QUALITY_PRESET )
    {
        //approximate neighbouring pixels positions (actually just deltas or "positions - pixCenterPos" )
        float3 viewspaceDirZNormalized = float3( pixCenterPos.xy / pixCenterPos.zz, 1.0 );
        float3 pixLDelta  = float3( -pixelDirRBViewspaceSizeAtCenterZ.x, 0.0, 0.0 ) + viewspaceDirZNormalized * (pixLZ - pixCenterPos.z); // very close approximation of: float3 pixLPos  = NDCToViewspace( normalizedScreenPos + float2( -g_ASSAOConsts.HalfViewportPixelSize.x, 0.0 ), pixLZ ).xyz - pixCenterPos.xyz;
        float3 pixRDelta  = float3( +pixelDirRBViewspaceSizeAtCenterZ.x, 0.0, 0.0 ) + viewspaceDirZNormalized * (pixRZ - pixCenterPos.z); // very close approximation of: float3 pixRPos  = NDCToViewspace( normalizedScreenPos + float2( +g_ASSAOConsts.HalfViewportPixelSize.x, 0.0 ), pixRZ ).xyz - pixCenterPos.xyz;
        float3 pixTDelta  = float3( 0.0, -pixelDirRBViewspaceSizeAtCenterZ.y, 0.0 ) + viewspaceDirZNormalized * (pixTZ - pixCenterPos.z); // very close approximation of: float3 pixTPos  = NDCToViewspace( normalizedScreenPos + float2( 0.0, -g_ASSAOConsts.HalfViewportPixelSize.y ), pixTZ ).xyz - pixCenterPos.xyz;
        float3 pixBDelta  = float3( 0.0, +pixelDirRBViewspaceSizeAtCenterZ.y, 0.0 ) + viewspaceDirZNormalized * (pixBZ - pixCenterPos.z); // very close approximation of: float3 pixBPos  = NDCToViewspace( normalizedScreenPos + float2( 0.0, +g_ASSAOConsts.HalfViewportPixelSize.y ), pixBZ ).xyz - pixCenterPos.xyz;

        const float rangeReductionConst         = 4.0f;                         // this is to avoid various artifacts
        const float modifiedFalloffCalcMulSq    = rangeReductionConst * falloffCalcMulSq;
        
        // Detail strength is scaled by the number of taps so that it appears the same regardless of quality level (number of taps)
        const float detailAOStrength = g_ASSAOConsts.DetailAOStrength * (float(numberOfTaps)/ 22.0);

        float2 detailObsL = CalculatePixelObscurance( qualityLevel, pixelNormal, pixLDelta, modifiedFalloffCalcMulSq );
        float2 detailObsR = CalculatePixelObscurance( qualityLevel, pixelNormal, pixRDelta, modifiedFalloffCalcMulSq );
        float2 detailObsT = CalculatePixelObscurance( qualityLevel, pixelNormal, pixTDelta, modifiedFalloffCalcMulSq );
        float2 detailObsB = CalculatePixelObscurance( qualityLevel, pixelNormal, pixBDelta, modifiedFalloffCalcMulSq );
        detailObsL.x *= edgesLRTB.x;    // apply additional edge-based occlusion removal - removes artifacts
        detailObsR.x *= edgesLRTB.y;    // apply additional edge-based occlusion removal - removes artifacts
        detailObsT.x *= edgesLRTB.z;    // apply additional edge-based occlusion removal - removes artifacts
        detailObsB.x *= edgesLRTB.w;    // apply additional edge-based occlusion removal - removes artifacts
        // steal stuff from here for ML? but what about detailAOStrength? that needs to be included somehow too?
        detailObsL.x *= detailObsL.y;   // apply weights
        detailObsR.x *= detailObsR.y;   // apply weights
        detailObsT.x *= detailObsT.y;   // apply weights
        detailObsB.x *= detailObsB.y;   // apply weights
        float totalWeightedDetailObscurance = detailObsL.x + detailObsR.x + detailObsT.x + detailObsB.x;
        float totalDetailWeight = detailObsL.y + detailObsR.y + detailObsT.y + detailObsB.y;
        obscuranceSum = totalWeightedDetailObscurance * detailAOStrength;
        weightSum = totalDetailWeight * detailAOStrength;
    }

    // Sharp normals also create edges which further prevents haloing - but this adds to the cost as well
    if( qualityLevel >= ASSAO_NORMAL_BASED_EDGES_ENABLE_AT_QUALITY_PRESET )
    {
        float3 neighbourNormalL  = LoadNormal( fullResCoord, int2( -2,  0 ) );
        float3 neighbourNormalR  = LoadNormal( fullResCoord, int2(  2,  0 ) );
        float3 neighbourNormalT  = LoadNormal( fullResCoord, int2(  0, -2 ) );
        float3 neighbourNormalB  = LoadNormal( fullResCoord, int2(  0,  2 ) );

        const float dotThreshold = ASSAO_NORMAL_BASED_EDGES_DOT_THRESHOLD;

        float4 normalEdgesLRTB;
        normalEdgesLRTB.x = saturate( (dot( pixelNormal, neighbourNormalL ) + dotThreshold ) );
        normalEdgesLRTB.y = saturate( (dot( pixelNormal, neighbourNormalR ) + dotThreshold ) );
        normalEdgesLRTB.z = saturate( (dot( pixelNormal, neighbourNormalT ) + dotThreshold ) );
        normalEdgesLRTB.w = saturate( (dot( pixelNormal, neighbourNormalB ) + dotThreshold ) );
        edgesLRTB *= normalEdgesLRTB;
    }

#ifdef ASSAO_DEBUG_SHOWNORMALS
    g_outputDebugImage[ dispatchPos * 2 + perPassFullResCoordOffset ] = float4( pow( abs( pixelNormal.xyz * 0.5 + 0.5 ), 2.2 ), 1.0 );
#endif
#ifdef ASSAO_DEBUG_SHOWEDGES
    g_outputDebugImage[ dispatchPos * 2 + perPassFullResCoordOffset ] = 1.0 - float4( edgesLRTB.x, edgesLRTB.y * 0.5 + edgesLRTB.w * 0.5, edgesLRTB.z, 0.0 );
#endif

    const float mipOffset        = log2( pixLookupRadiusMod ) + ASSAO_DEPTH_MIPS_GLOBAL_OFFSET;

    const float3 negViewspaceDir = -normalize( pixCenterPos );

    //[unroll]
    for( int i = 0; i < numberOfTaps; i++ )
    {
        float2 outSample0, outSample1;
        SSAOTap( qualityLevel, i, rotScale, pixCenterPos, negViewspaceDir, pixelNormal, normalizedScreenPos, mipOffset, layer, falloffCalcMulSq, outSample0, outSample1);

        obscuranceSum   += outSample0.x * outSample0.y;
        weightSum       += outSample0.y;
        obscuranceSum   += outSample1.x * outSample1.y;
        weightSum       += outSample1.y;
    }

	// use MLASSAO or regular weighted average to compute obscurance
	float obscurance = 0.0f;
#ifdef 	ASSAO_ENABLE_MLSSAO
    obscurance = MLComputeObscurance(sampleObscurances); // / weightSum;
#else
    obscurance = obscuranceSum / weightSum;
#endif

    // Artist setting: strength
    obscurance = g_ASSAOConsts.EffectShadowStrength * obscurance;
    
    // Artist setting: clamp
    obscurance = min( obscurance, g_ASSAOConsts.EffectShadowClamp );
    
    // Reduce the SSAO shadowing if we're surrounded by "a lot of" edges - this removes temporal artifacts (when there's more than 2 opposite edges, start fading out the occlusion)
    float edgeFadeoutFactor = saturate( (1.0 - edgesLRTB.x - edgesLRTB.y) * 0.35) + saturate( (1.0 - edgesLRTB.z - edgesLRTB.w) * 0.35 );
    fadeOut *= saturate( 1.0 - edgeFadeoutFactor );

    // Fade out - based on pre-set distance (fade out far away) and the above edge aliasing reduction trick
    obscurance *= fadeOut;

    // switch to 'occlusion term' - none of this is physically based though, it's all a bit ad-hoc
    float occlusion = 1.0 - obscurance;

    // Artist setting: modify the gradient; note: this cannot be moved to a later pass because of loss of precision after storing in the render target
    occlusion = pow( saturate( occlusion ), g_ASSAOConsts.EffectShadowPow );

    // Outputs: final 'occlusion' term (0 means fully occluded, 1 means fully lit); edges used to prevent blurring between foreground/background; 1 means no edge, 0 means edge, 0.5 means half-edge
    g_outputOcclusionEdge[uint3(dispatchPos, layer)] = float2( occlusion, PackEdges( edgesLRTB ) );
}

[numthreads(ASSAO_NUMTHREADS_X, ASSAO_NUMTHREADS_Y, ASSAO_NUMTHREADS_LAYERED_Z)]
void CSGenerateQ0( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    GenerateSSAOShadowsInternal( dispatchThreadID.xy, 0, dispatchThreadID.z );
}

[numthreads(ASSAO_NUMTHREADS_X, ASSAO_NUMTHREADS_Y, ASSAO_NUMTHREADS_LAYERED_Z)]
void CSGenerateQ1( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    GenerateSSAOShadowsInternal( dispatchThreadID.xy, 1, dispatchThreadID.z );
}

[numthreads(ASSAO_NUMTHREADS_X, ASSAO_NUMTHREADS_Y, ASSAO_NUMTHREADS_LAYERED_Z)]
void CSGenerateQ2( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    GenerateSSAOShadowsInternal( dispatchThreadID.xy, 2, dispatchThreadID.z );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pixel shader that does smart edge-aware blurring that avoid bleeding between foreground and background objects
//
void AddSample( float ssaoValue, float edgeValue, inout float sum, inout float sumWeight )
{
    float weight = edgeValue;    

    sum += (weight * ssaoValue);
    sumWeight += weight;
}
//
float2 SampleBlurred( uint2 inPos, const uint layer )
{
    float2 packedCenter = g_workingOcclusionEdge.Load( uint4( inPos.xy, layer, 0 ) ).xy;
    float ssaoValue     = packedCenter.x;
    float packedEdges   = packedCenter.y;
    float4 edgesLRTB    = UnpackEdges( packedEdges );

    float2 inPosf       = float2( inPos );
    float4 valuesUL     = g_workingOcclusionEdge.GatherRed( g_samplerPointClamp, float3( ( inPosf                    ) * g_ASSAOConsts.HalfViewportPixelSize, layer ) );
    float4 valuesBR     = g_workingOcclusionEdge.GatherRed( g_samplerPointClamp, float3( ( inPosf + float2(1.0, 1.0) ) * g_ASSAOConsts.HalfViewportPixelSize, layer ) );

    float ssaoValueL    = valuesUL.x; // g_workingOcclusionEdge.Load( int3( inPos.xy, 0 ), int2( -1,  0 ) ).x;
    float ssaoValueT    = valuesUL.z; // g_workingOcclusionEdge.Load( int3( inPos.xy, 0 ), int2(  0, -1 ) ).x;
    float ssaoValueR    = valuesBR.z; // g_workingOcclusionEdge.Load( int3( inPos.xy, 0 ), int2(  1,  0 ) ).x;
    float ssaoValueB    = valuesBR.x; // g_workingOcclusionEdge.Load( int3( inPos.xy, 0 ), int2(  0,  1 ) ).x;

    float sumWeight = 0.5f;
    float sum = ssaoValue * sumWeight;

    AddSample( ssaoValueL, edgesLRTB.x, sum, sumWeight );
    AddSample( ssaoValueR, edgesLRTB.y, sum, sumWeight );

    AddSample( ssaoValueT, edgesLRTB.z, sum, sumWeight );
    AddSample( ssaoValueB, edgesLRTB.w, sum, sumWeight );

    float ssaoAvg = sum / sumWeight;

    ssaoValue = ssaoAvg; //min( ssaoValue, ssaoAvg ) * 0.2 + ssaoAvg * 0.8;

    return float2( ssaoValue, packedEdges );
}
//
// edge-aware blur
[numthreads(ASSAO_NUMTHREADS_X, ASSAO_NUMTHREADS_Y, ASSAO_NUMTHREADS_LAYERED_Z)]
void CSSmartBlur( uint3 dispatchThreadID : SV_DispatchThreadID ) //in float4 inPos : SV_POSITION, in float2 inUV : TEXCOORD0 ) : SV_Target
{
    // early out if outside of viewport
    [branch] if( any( dispatchThreadID.xy >= g_ASSAOConsts.HalfViewportSize ) )
        return;
    g_outputOcclusionEdge[dispatchThreadID.xyz] = SampleBlurred( dispatchThreadID.xy, dispatchThreadID.z );
}
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

[numthreads(ASSAO_NUMTHREADS_X, ASSAO_NUMTHREADS_Y, 1)] // <- this last one is not layered, always use 1 instead of ASSAO_NUMTHREADS_LAYERED_Z
void CSApply( uint2 dispatchThreadID : SV_DispatchThreadID )
{
//   g_outputFinal[dispatchThreadID] = LoadNormal( dispatchThreadID ).x;
//   g_outputFinal[dispatchThreadID] = frac( DepthLoadAndConvertToViewspace( dispatchThreadID, int2(0,0) ) );
//   return;

    float2 inPos        = float2(dispatchThreadID) + float2(0.5, 0.5);
    uint2 pixPos        = dispatchThreadID;
    uint2 pixPosHalf    = pixPos / uint2(2, 2);

    // early out if outside of viewport
    [branch] if( any( pixPos >= g_ASSAOConsts.ViewportSize ) )
        return;

    float ao;

    // calculate index in the four deinterleaved source array texture
    int mx = (pixPos.x % 2);
    int my = (pixPos.y % 2);
    int ic = mx + my * 2;       // center index
    int ih = (1-mx) + my * 2;   // neighbouring, horizontal
    int iv = mx + (1-my) * 2;   // neighbouring, vertical
    int id = (1-mx) + (1-my)*2; // diagonal
    
    float2 centerVal = g_workingOcclusionEdge.Load( int4( pixPosHalf, ic, 0 ) ).xy;

    ao = centerVal.x;

#if 1   // change to 0 if you want to disable last pass high-res blur (for debugging purposes, etc.)
    float4 edgesLRTB = UnpackEdges( centerVal.y );

    // convert index shifts to sampling offsets
    float fmx   = (float)mx;
    float fmy   = (float)my;
    
    // in case of an edge, push sampling offsets away from the edge (towards pixel center)
    float fmxe  = (edgesLRTB.y - edgesLRTB.x);
    float fmye  = (edgesLRTB.w - edgesLRTB.z);

    // calculate final sampling offsets and sample using bilinear filter
    float2  uvH = (inPos.xy + float2( fmx + fmxe - 0.5, 0.5 - fmy ) ) * 0.5 * g_ASSAOConsts.HalfViewportPixelSize;
    float   aoH = g_workingOcclusionEdge.SampleLevel( g_samplerLinearClamp, float3( uvH, ih ), 0 ).x;
    float2  uvV = (inPos.xy + float2( 0.5 - fmx, fmy - 0.5 + fmye ) ) * 0.5 * g_ASSAOConsts.HalfViewportPixelSize;
    float   aoV = g_workingOcclusionEdge.SampleLevel( g_samplerLinearClamp, float3( uvV, iv ), 0 ).x;
    float2  uvD = (inPos.xy + float2( fmx - 0.5 + fmxe, fmy - 0.5 + fmye ) ) * 0.5 * g_ASSAOConsts.HalfViewportPixelSize;
    float   aoD = g_workingOcclusionEdge.SampleLevel( g_samplerLinearClamp, float3( uvD, id ), 0 ).x;

    // reduce weight for samples near edge - if the edge is on both sides, weight goes to 0
    float4 blendWeights;
    blendWeights.x = 1.0;
    blendWeights.y = (edgesLRTB.x + edgesLRTB.y) * 0.5;
    blendWeights.z = (edgesLRTB.z + edgesLRTB.w) * 0.5;
    blendWeights.w = (blendWeights.y + blendWeights.z) * 0.5;

    // calculate weighted average
    float blendWeightsSum   = dot( blendWeights, float4( 1.0, 1.0, 1.0, 1.0 ) );
    ao = dot( float4( ao, aoH, aoV, aoD ), blendWeights ) / blendWeightsSum;
#endif

    g_outputFinal[pixPos] = ao;
}
