///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __VA_SHARED_HLSL__
#define __VA_SHARED_HLSL__

#ifndef VA_COMPILED_AS_SHADER_CODE
#error not intended to be included outside of HLSL!
#endif

#include "vaSharedTypes.h"

#include "vaConversions.h"

#include "vaStandardSamplers.hlsl"


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Some useful constants
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
#ifndef VA_PI
#define VA_PI               	(3.1415926535897932384626433832795)
#endif
//
#ifndef VA_FLOAT_ONE_MINUS_EPSILON
#define VA_FLOAT_ONE_MINUS_EPSILON  (0x1.fffffep-1)                                         // biggest float smaller than 1 (see OneMinusEpsilon in pbrt book!)
#endif
//
#define HALF_FLT_MAX        65504.0
#define HALF_FLT_MIN        0.00006103515625

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float Sq( float x ) 
{ 
    return x*x; 
}

float Pow5( float x )
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float Max3(const float3 v) 
{
    return max(v.x, max(v.y, v.z));
}

float Min3(const float3 v) 
{
    return min(v.x, min(v.y, v.z));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Normal from heightmap (for terrains, etc.)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// for Texel to Vertex mapping (3x3 kernel, the texel centers are at quad vertices)
float3  ComputeHeightmapNormal( float h00, float h10, float h20, float h01, float h11, float h21, float h02, float h12, float h22, const float3 pixelWorldSize )
{
    // Sobel 3x3
	//    0,0 | 1,0 | 2,0
	//    ----+-----+----
	//    0,1 | 1,1 | 2,1
	//    ----+-----+----
	//    0,2 | 1,2 | 2,2

    h00 -= h11;
    h10 -= h11;
    h20 -= h11;
    h01 -= h11;
    h21 -= h11;
    h02 -= h11;
    h12 -= h11;
    h22 -= h11;
   
	// The Sobel X kernel is:
	//
	// [ 1.0  0.0  -1.0 ]
	// [ 2.0  0.0  -2.0 ]
	// [ 1.0  0.0  -1.0 ]
	
	float Gx = h00 - h20 + 2.0 * h01 - 2.0 * h21 + h02 - h22;
				
	// The Sobel Y kernel is:
	//
	// [  1.0    2.0    1.0 ]
	// [  0.0    0.0    0.0 ]
	// [ -1.0   -2.0   -1.0 ]
	
	float Gy = h00 + 2.0 * h10 + h20 - h02 - 2.0 * h12 - h22;
	
	// The 0.5f leading coefficient can be used to control
	// how pronounced the bumps are - less than 1.0 enhances
	// and greater than 1.0 smoothes.
	
	//return float4( 0, 0, 0, 0 );
	
   float stepX = pixelWorldSize.x;
   float stepY = pixelWorldSize.y;
   float sizeZ = pixelWorldSize.z;
   
	Gx = Gx * stepY * sizeZ;
	Gy = Gy * stepX * sizeZ;
	
	float Gz = stepX * stepY * 8;
	
    return normalize( float3( Gx, Gy, Gz ) );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Random doodads
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
float3 normalize_safe( float3 val )
{
    float len = max( 0.0001, length( val ) );
    return val / len;
}
//
float3 normalize_safe( float3 val, float threshold )
{
    float len = max( threshold, length( val ) );
    return val / len;
}
//
float GLSL_mod( float x, float y )
{
    return x - y * floor( x / y );
}
float2 GLSL_mod( float2 x, float2 y )
{
    return x - y * floor( x / y );
}
float3 GLSL_mod( float3 x, float3 y )
{
    return x - y * floor( x / y );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A couple of nice gradient for various visualization
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// from https://www.shadertoy.com/view/lt2GDc - New Gradients from (0-1 float) Created by ChocoboBreeder in 2015-Jun-3
float3 GradientPalette( in float t, in float3 a, in float3 b, in float3 c, in float3 d )
{
    return a + b*cos( 6.28318*(c*t+d) );
}
// rainbow gradient
float3 GradientRainbow( in float t )
{
    return GradientPalette( t, float3(0.55,0.4,0.3), float3(0.50,0.51,0.35)+0.1, float3(0.8,0.75,0.8), float3(0.075,0.33,0.67)+0.21 );
}
// from https://www.shadertoy.com/view/llKGWG - Heat map, Created by joshliebe in 2016-Oct-15
float3 GradientHeatMap( in float greyValue )
{
    float3 heat;      
    heat.r = smoothstep(0.5, 0.8, greyValue);
    if(greyValue >= 0.90) {
    	heat.r *= (1.1 - greyValue) * 5.0;
    }
	if(greyValue > 0.7) {
		heat.g = smoothstep(1.0, 0.7, greyValue);
	} else {
		heat.g = smoothstep(0.0, 0.7, greyValue);
    }    
	heat.b = smoothstep(1.0, 0.0, greyValue);          
    if(greyValue <= 0.3) {
    	heat.b *= greyValue / 0.3;     
    }
    return heat;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fast (but imprecise) math stuff!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
float FastRcpSqrt(float fx )
{
    // http://h14s.p5r.org/2012/09/0x5f3759df.html, [Drobot2014a] Low Level Optimizations for GCN
    int x = asint(fx);
    x = 0x5f3759df - (x >> 1);
    return asfloat(x);
}
//
float FastSqrt( float x )
{
    // http://h14s.p5r.org/2012/09/0x5f3759df.html, [Drobot2014a] Low Level Optimizations for GCN
    // https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf slide 63
    return asfloat( 0x1fbd1df5 + ( asint( x ) >> 1 ) );
}
//
// From https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
// input [-1, 1] and output [0, PI]
float FastAcos(float inX) 
{ 
    float x = abs(inX); 
    float res = -0.156583f * x + VA_PI*0.5; 
    res *= FastSqrt(1.0f - x);                  // consider using normal sqrt here?
    return (inX >= 0) ? res : VA_PI - res; 
}
//
// Approximates acos(x) with a max absolute error of 9.0x10^-3.
// Input [0, 1]
float FastAcosPositive(float x) 
{
    float p = -0.1565827f * x + 1.570796f;
    return p * sqrt(1.0 - x);
}
//
// input [-1, 1] and output [-PI/2, PI/2]
float ASin(float x)
{
    const float HALF_PI = 1.570796f;
    return HALF_PI - FastAcos(x);
}
//
// input [0, infinity] and output [0, PI/2]
float FastAtanPos(float inX) 
{ 
    const float HALF_PI = 1.570796f;
    float t0 = (inX < 1.0f) ? inX : 1.0f / inX;
    float t1 = t0 * t0;
    float poly = 0.0872929f;
    poly = -0.301895f + poly * t1;
    poly = 1.0f + poly * t1;
    poly = poly * t0;
    return (inX < 1.0f) ? poly : HALF_PI - poly;
}
//
// input [-infinity, infinity] and output [-PI/2, PI/2]
float FastAtan(float x) 
{     
    float t0 = FastAtanPos(abs(x));     
    return (x < 0.0f) ? -t0: t0; 
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GTAO snippets
// https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
//
float3 GTAOMultiBounce(float visibility, float3 albedo)
{
 	float3 a =  2.0404 * albedo - 0.3324;   
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c =  2.7552 * albedo + 0.6903;
    
    float3 x = visibility.xxx;
    return max(x, ((x * a + b) * x + c) * x);
}
//
uint EncodeVisibilityBentNormal( float visibility, float3 bentNormal )
{
    return FLOAT4_to_R8G8B8A8_UNORM( float4( bentNormal * 0.5 + 0.5, visibility ) );
}

void DecodeVisibilityBentNormal( const uint packedValue, out float visibility, out float3 bentNormal )
{
    float4 decoded = R8G8B8A8_UNORM_to_FLOAT4( packedValue );
    bentNormal = decoded.xyz * 2.0.xxx - 1.0.xxx;   // could normalize - don't want to since it's done so many times, better to do it at the final step only
    visibility = decoded.w;
}

float DecodeVisibilityBentNormal_VisibilityOnly( const uint packedValue )
{
    float visibility; float3 bentNormal;
    DecodeVisibilityBentNormal( packedValue, visibility, bentNormal );
    return visibility;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Various filters
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Manual bilinear filter: input 'coords' is standard [0, 1] texture uv coords multiplied by [textureWidth, textureHeight] minus [0.5, 0.5]
float      BilinearFilter( float c00, float c10, float c01, float c11, float2 coords )
{
    float2 intPt    = floor(coords);
    float2 fractPt  = frac(coords);
    float top       = lerp( c00, c10, fractPt.x );
    float bottom    = lerp( c01, c11, fractPt.x );
    return lerp( top, bottom, fractPt.y );
}
//
float3      BilinearFilter( float3 c00, float3 c10, float3 c01, float3 c11, float2 coords )
{
    float2 intPt    = floor(coords);
    float2 fractPt  = frac(coords);
    float3 top      = lerp( c00, c10, fractPt.x );
    float3 bottom   = lerp( c01, c11, fractPt.x );
    return lerp( top, bottom, fractPt.y );
}
//
float4      BilinearFilter( float4 c00, float4 c10, float4 c01, float4 c11, float2 coords )
{
    float2 intPt    = floor(coords);
    float2 fractPt  = frac(coords);
    float4 top      = lerp( c00, c10, fractPt.x );
    float4 bottom   = lerp( c01, c11, fractPt.x );
    return lerp( top, bottom, fractPt.y );
}
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sRGB <-> linear conversions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
float FLOAT_to_SRGB( float val )
{
    if( val < 0.0031308 )
        val *= float( 12.92 );
    else
        val = float( 1.055 ) * pow( abs( val ), float( 1.0 ) / float( 2.4 ) ) - float( 0.055 );
    return val;
}
//
float3 FLOAT3_to_SRGB( float3 val )
{
    float3 outVal;
    outVal.x = FLOAT_to_SRGB( val.x );
    outVal.y = FLOAT_to_SRGB( val.y );
    outVal.z = FLOAT_to_SRGB( val.z );
    return outVal;
}
//
float SRGB_to_FLOAT( float val )
{
    if( val < 0.04045 )
        val /= float( 12.92 );
    else
        val = pow( abs( val + float( 0.055 ) ) / float( 1.055 ), float( 2.4 ) );
    return val;
}
//
float3 SRGB_to_FLOAT3( float3 val )
{
    float3 outVal;
    outVal.x = SRGB_to_FLOAT( val.x );
    outVal.y = SRGB_to_FLOAT( val.y );
    outVal.z = SRGB_to_FLOAT( val.z );
    return outVal;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Interpolation between two uniformly distributed random values using Cumulative Distribution Function
// (see page 4 http://cwyman.org/papers/i3d17_hashedAlpha.pdf or https://en.wikipedia.org/wiki/Cumulative_distribution_function)
float LerpCDF( float lhs, float rhs, float s )
{
    // Interpolate alpha threshold from noise at two scales 
    float x = (1-s)*lhs + s*rhs;

    // Pass into CDF to compute uniformly distrib threshold 
    float a = min( s, 1-s ); 
    float3 cases = float3( x*x/(2*a*(1-a)), (x-0.5*a)/(1-a), 1.0-((1-x)*(1-x)/(2*a*(1-a))) );

    // Find our final, uniformly distributed alpha threshold 
    return (x < (1-a)) ? ((x < a) ? cases.x : cases.y) : cases.z;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// From DXT5_NM standard
float3 UnpackNormalDXT5_NM( float4 packedNormal )
{
    float3 normal;
    normal.xy = packedNormal.wy * 2.0 - 1.0;
    normal.z = sqrt( 1.0 - normal.x*normal.x - normal.y * normal.y );
    return normal;
}

float3 DisplayNormal( float3 normal )
{
    return normal * 0.5 + 0.5;
}

float3 DisplayNormalSRGB( float3 normal )
{
    return pow( abs( normal * 0.5 + 0.5 ), 2.2 );
}

// this codepath is disabled - here's one simple idea for future improvements: http://iquilezles.org/www/articles/fog/fog.htm
//float3 FogForwardApply( float3 color, float viewspaceDistance )
//{
//    //return frac( viewspaceDistance / 10.0 );
//    float d = max(0.0, viewspaceDistance - g_lighting.FogDistanceMin);
//    float fogStrength = exp( - d * g_lighting.FogDensity ); 
//    return lerp( g_lighting.FogColor.rgb, color.rgb, fogStrength );
//}

// float3 DebugViewGenericSceneVertexTransformed( in float3 inColor, const in GenericSceneVertexTransformed input )
// {
// //    inColor.x = 1.0;
// 
//     return inColor;
// }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// Normals encode/decode
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//float3 GBufferEncodeNormal( float3 normal )
//{
//    float3 encoded = normal * 0.5 + 0.5;
//
//    return encoded;
//}
//
//float3 GBufferDecodeNormal( float3 encoded )
//{
//    float3 normal = encoded * 2.0 - 1.0;
//    return normalize( normal );
//}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Space conversions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// normalized device coordinates (SV_Position from PS) to viewspace depth
float NDCToViewDepth( float ndcDepth )
{
    float depthHackMul = g_globals.DepthUnpackConsts.x;
    float depthHackAdd = g_globals.DepthUnpackConsts.y;

    // Optimised version of "-cameraClipNear / (cameraClipFar - projDepth * (cameraClipFar - cameraClipNear)) * cameraClipFar"

    // Set your depthHackMul and depthHackAdd to:
    // depthHackMul = ( cameraClipFar * cameraClipNear) / ( cameraClipFar - cameraClipNear );
    // depthHackAdd = cameraClipFar / ( cameraClipFar - cameraClipNear );

    return depthHackMul / ( depthHackAdd - ndcDepth );
}

// reversed NDCToViewDepth
float ViewToNDCDepth( float viewDepth )
{
    float depthHackMul = g_globals.DepthUnpackConsts.x;
    float depthHackAdd = g_globals.DepthUnpackConsts.y;
    return -depthHackMul / viewDepth + depthHackAdd;
}

// from [0, width], [0, height] to [-1, 1], [-1, 1]
float2 NDCToClipSpacePositionXY( float2 ndcPos )
{
    return (ndcPos - float2( -1.0f, 1.0f )) / float2( g_globals.ViewportPixel2xSize.x, -g_globals.ViewportPixel2xSize.y );
}

// from [0, width], [0, height] to [-1, 1], [-1, 1]
float2 ClipSpaceToNDCPositionXY( float2 svPos )
{
    return svPos * float2( g_globals.ViewportPixel2xSize.x, -g_globals.ViewportPixel2xSize.y ) + float2( -1.0f, 1.0f );
}

float3 NDCToViewspacePosition( float2 SVPos, float viewspaceDepth )
{
    return float3( g_globals.CameraTanHalfFOV.xy * viewspaceDepth * ClipSpaceToNDCPositionXY( SVPos ), viewspaceDepth );
}

float3 ClipSpaceToViewspacePosition( float2 clipPos, float viewspaceDepth )
{
    return float3( g_globals.CameraTanHalfFOV.xy * viewspaceDepth * clipPos, viewspaceDepth );
}

// not entirely sure these are correct w.r.t. to y being upside down
float3 CubemapGetDirectionFor(uint face, float2 uv)
{
    // map [0, dim] to [-1,1] with (-1,-1) at bottom left
    float cx = (uv.x * 2.0) - 1;
    float cy = 1 - (uv.y * 2.0);    // <- not entirely sure about this bit

    float3 dir;
    const float l = sqrt(cx * cx + cy * cy + 1);
    switch (face) 
    {
        case 0:  dir = float3(   1, cy, -cx ); break;  // PX
        case 1:  dir = float3(  -1, cy,  cx ); break;  // NX
        case 2:  dir = float3(  cx,  1, -cy ); break;  // PY
        case 3:  dir = float3(  cx, -1,  cy ); break;  // NY
        case 4:  dir = float3(  cx, cy,   1 ); break;  // PZ
        case 5:  dir = float3( -cx, cy,  -1 ); break;  // NZ
        default: dir = 0.0.xxx; break;
    }
    return dir * (1 / l);
}
//
float3 CubemapGetDirectionFor( uint cubeDim, uint face, uint ux, uint uy )
{
    return CubemapGetDirectionFor( face, float2(ux+0.5,uy+0.5) / cubeDim.xx );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stuff
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Disallow "too high" values, as defined by camera settings
float3 HDRClamp( float3 color )
{
    float m = max( 1, max( max( color.r, color.g ), color.b ) / g_globals.HDRClamp );
    return color / m.xxx;
}
//
float CalcLuminance( float3 color )
{
    // https://en.wikipedia.org/wiki/Relative_luminance - Rec. 709
    return max( 0.0000001, dot(color, float3(0.2126f, 0.7152f, 0.0722f) ) );
}
//
// color -> log luma conversion used for edge detection
float RGBToLumaForEdges( float3 linearRGB )
{
#if 0
    // this matches Miniengine luma path
    float Luma = dot( linearRGB, float3(0.212671, 0.715160, 0.072169) );
    return log2(1 + Luma * 15) / 4;
#else
    // this is what original FXAA (and consequently CMAA2) use by default - these coefficients correspond to Rec. 601 and those should be
    // used on gamma-compressed components (see https://en.wikipedia.org/wiki/Luma_(video)#Rec._601_luma_versus_Rec._709_luma_coefficients), 
    float luma = dot( sqrt( linearRGB.rgb ), float3( 0.299, 0.587, 0.114 ) );  // http://en.wikipedia.org/wiki/CCIR_601
    // using sqrt luma for now but log luma like in miniengine provides a nicer curve on the low-end
    return luma;
#endif
}
//
// see https://en.wikipedia.org/wiki/Luma_(video) for luma / luminance conversions
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The following code is licensed under the MIT license: https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae
//
// Samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16.
// See http://vec3.ca/bicubic-filtering-in-fewer-taps/ / https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1 for more details
float4 SampleBicubic9(in Texture2D<float4> tex, in SamplerState linearSampler, in float2 uv) // a.k.a. SampleTextureCatmullRom
{
    float2 texSize; tex.GetDimensions( texSize.x, texSize.y );
    float2 invTexSize = 1.f / texSize;

    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    float2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    float2 texPos0 = texPos1 - 1;
    float2 texPos3 = texPos1 + 2;
    float2 texPos12 = texPos1 + offset12;

    texPos0  *= invTexSize;
    texPos3  *= invTexSize;
    texPos12 *= invTexSize;

    float4 result = 0.0f;
    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos0.y), 0.0f) * w0.x * w0.y;     // apparently for 5-tap version it's ok to just remove these
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos0.y), 0.0f) * w12.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos0.y), 0.0f) * w3.x * w0.y;     // apparently for 5-tap version it's ok to just remove these

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos12.y), 0.0f) * w0.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos12.y), 0.0f) * w12.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos12.y), 0.0f) * w3.x * w12.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos3.y), 0.0f) * w0.x * w3.y;     // apparently for 5-tap version it's ok to just remove these
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos3.y), 0.0f) * w12.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos3.y), 0.0f) * w3.x * w3.y;     // apparently for 5-tap version it's ok to just remove these

    return result;
}
//
/*
// See http://vec3.ca/bicubic-filtering-in-fewer-taps/, http://pastebin.com/raw/YLLSBRFq
float4 SampleBicubic4( in Texture2D<float4> tex, in SamplerState linearSampler, in float2 uv )
{
    //--------------------------------------------------------------------------------------
    // Calculate the center of the texel to avoid any filtering

    float2 textureDimensions; tex.GetDimensions( textureDimensions.x, textureDimensions.y );
    float2 invTextureDimensions = 1.f / textureDimensions;

    uv *= textureDimensions;

    float2 texelCenter   = floor( uv - 0.5f ) + 0.5f;
    float2 fracOffset    = uv - texelCenter;
    float2 fracOffset_x2 = fracOffset * fracOffset;
    float2 fracOffset_x3 = fracOffset * fracOffset_x2;

    //--------------------------------------------------------------------------------------
    // Calculate the filter weights (B-Spline Weighting Function)

    float2 weight0 = fracOffset_x2 - 0.5f * ( fracOffset_x3 + fracOffset );
    float2 weight1 = 1.5f * fracOffset_x3 - 2.5f * fracOffset_x2 + 1.f;
    float2 weight3 = 0.5f * ( fracOffset_x3 - fracOffset_x2 );
    float2 weight2 = 1.f - weight0 - weight1 - weight3;

    //--------------------------------------------------------------------------------------
    // Calculate the texture coordinates

    float2 scalingFactor0 = weight0 + weight1;
    float2 scalingFactor1 = weight2 + weight3;

    float2 f0 = weight1 / ( weight0 + weight1 );
    float2 f1 = weight3 / ( weight2 + weight3 );

    float2 texCoord0 = texelCenter - 1.f + f0;
    float2 texCoord1 = texelCenter + 1.f + f1;

    texCoord0 *= invTextureDimensions;
    texCoord1 *= invTextureDimensions;

    //--------------------------------------------------------------------------------------
    // Sample the texture

    return tex.SampleLevel( texSampler, float2( texCoord0.x, texCoord0.y ), 0 ) * scalingFactor0.x * scalingFactor0.y +
           tex.SampleLevel( texSampler, float2( texCoord1.x, texCoord0.y ), 0 ) * scalingFactor1.x * scalingFactor0.y +
           tex.SampleLevel( texSampler, float2( texCoord0.x, texCoord1.y ), 0 ) * scalingFactor0.x * scalingFactor1.y +
           tex.SampleLevel( texSampler, float2( texCoord1.x, texCoord1.y ), 0 ) * scalingFactor1.x * scalingFactor1.y;
}
*/
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// System helpers
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaDebugging.hlsl"

void OutputGenericData( const in float row[SHADERGLOBAL_GENERICDATACAPTURE_COLUMNS] )
{
    uint itemIndex = 0;
    InterlockedAdd( g_GenericOutputDataUAV[ uint2(0, 0) ], 1, itemIndex );
    if( itemIndex < ( SHADERGLOBAL_GENERICDATACAPTURE_ROWS ) )
    {
        for( int c = 0; c < SHADERGLOBAL_GENERICDATACAPTURE_COLUMNS; c++ )
            g_GenericOutputDataUAV[ uint2(c, itemIndex+1) ] = asuint( row[c] );
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // __VA_SHARED_HLSL__