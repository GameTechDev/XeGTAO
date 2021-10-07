///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VA_CONVERSIONS__H
#define VA_CONVERSIONS__H

#include "vaShaderCore.h"

#ifndef VA_COMPILED_AS_SHADER_CODE
namespace Vanilla
{
// These will get #undef-ed at the bottom of the file!

#define float3      vaVector3
#define float4      vaVector4

struct _FIU32 { union{ int32_t _i; uint32_t _ui; float _f; };               _FIU32( const int32_t & i ) { _i = i; } _FIU32( const uint32_t & ui ) { _ui = ui; } _FIU32( const float & f ) { _f = f; }  };
struct _FIU16 { union{ int16_t _i; uint16_t _ui; half_float::half _f; };    _FIU16( const int32_t & i ) { _i = (int16_t)i; } _FIU16( const uint32_t & ui ) { _ui = (uint16_t)ui; } _FIU16( const int16_t & i ) { _i = i; } _FIU16( const uint16_t & ui ) { _ui = ui; } _FIU16( const half_float::half & f ) { _f = f; }  };

#define asfloat(ui)     (_FIU32(ui))._f
#define asuint(f)       (_FIU32(f))._ui

#define f32tof16(x)     _FIU16(half_float::half(x))._ui
#define f16tof32(x)     _FIU16(x)._f  

#endif


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pixel packing helpers
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Color conversion functions below come mostly from: https://github.com/apitrace/dxsdk/blob/master/Include/d3dx_dxgiformatconvert.inl
// For additional future formats, refer to https://github.com/GPUOpen-LibrariesAndSDKs/nBodyD3D12/blob/master/MiniEngine/Core/Shaders/PixelPacking.hlsli 
// (and there's an excellent blogpost here: https://bartwronski.com/2017/04/02/small-float-formats-r11g11b10f-precision/)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sRGB <-> linear conversions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
VA_INLINE float LINEAR_to_SRGB( float val )
{
    if( val < 0.0031308 )
        val *= float( 12.92 );
    else
        val = float( 1.055 ) * pow( abs( val ), float( 1.0 ) / float( 2.4 ) ) - float( 0.055 );
    return val;
}

VA_INLINE float3 LINEAR_to_SRGB( float3 val )
{
    return float3( LINEAR_to_SRGB( val.x ), LINEAR_to_SRGB( val.y ), LINEAR_to_SRGB( val.z ) );
}

VA_INLINE float SRGB_to_LINEAR( float val )
{
    if( val < 0.04045 )
        val /= float( 12.92 );
    else
        val = pow( abs( val + float( 0.055 ) ) / float( 1.055 ), float( 2.4 ) );
    return val;
}
VA_INLINE float3 SRGB_to_LINEAR( float3 val )
{
    return float3( SRGB_to_LINEAR( val.x ), SRGB_to_LINEAR( val.y ), SRGB_to_LINEAR( val.z ) );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// B8G8R8A8_UNORM <-> float4
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
VA_INLINE float4 R8G8B8A8_UNORM_to_FLOAT4( uint packedInput )
{
    float4 unpackedOutput;
    unpackedOutput.x = (float)( packedInput & 0x000000ff ) / 255;
    unpackedOutput.y = (float)( ( ( packedInput >> 8 ) & 0x000000ff ) ) / 255;
    unpackedOutput.z = (float)( ( ( packedInput >> 16 ) & 0x000000ff ) ) / 255;
    unpackedOutput.w = (float)( packedInput >> 24 ) / 255;
    return unpackedOutput;
}
VA_INLINE uint FLOAT4_to_R8G8B8A8_UNORM( float4 unpackedInput )
{
    return ( ( uint( VA_SATURATE( unpackedInput.x ) * 255 + 0.5 ) ) |
             ( uint( VA_SATURATE( unpackedInput.y ) * 255 + 0.5 ) << 8 ) |
             ( uint( VA_SATURATE( unpackedInput.z ) * 255 + 0.5 ) << 16 ) |
             ( uint( VA_SATURATE( unpackedInput.w ) * 255 + 0.5 ) << 24 ) );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// R11G11B10_UNORM <-> float3
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
VA_INLINE float3 R11G11B10_UNORM_to_FLOAT3( uint packedInput )
{
    float3 unpackedOutput;
    unpackedOutput.x = (float)( ( packedInput       ) & 0x000007ff ) / 2047.0f;
    unpackedOutput.y = (float)( ( packedInput >> 11 ) & 0x000007ff ) / 2047.0f;
    unpackedOutput.z = (float)( ( packedInput >> 22 ) & 0x000003ff ) / 1023.0f;
    return unpackedOutput;
}
// 'unpackedInput' is float3 and not float3 on purpose as half float lacks precision for below!
VA_INLINE uint FLOAT3_to_R11G11B10_UNORM( float3 unpackedInput )
{
    uint packedOutput;
    packedOutput =( ( uint( VA_SATURATE( unpackedInput.x ) * 2047 + 0.5f ) ) |
                    ( uint( VA_SATURATE( unpackedInput.y ) * 2047 + 0.5f ) << 11 ) |
                    ( uint( VA_SATURATE( unpackedInput.z ) * 1023 + 0.5f ) << 22 ) );
    return packedOutput;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The less standard 32-bit HDR color format with 2-bit alpha.  Each float has a 5-bit exponent and no sign bit.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
VA_INLINE uint Pack_R10G10B10FLOAT_A2_UNORM( float4 rgba )
{
#ifndef VA_COMPILED_AS_SHADER_CODE
    assert( rgba.x >= 0 && rgba.y >= 0 && rgba.z >= 0 && rgba.w >= 0 && rgba.w <= 1 );
#endif
    // Clamp upper bound so that it doesn't accidentally round up to INF 
    // Exponent=15, Mantissa=1.11111
    float mf = asfloat(0x477C0000);
    uint r = ((f32tof16(VA_MIN(rgba.x, mf)) + 16) >> 5) & 0x000003FF;
    uint g = ((f32tof16(VA_MIN(rgba.y, mf)) + 16) << 5) & 0x000FFC00;
    uint b = ((f32tof16(VA_MIN(rgba.z, mf)) + 16) << 15) & 0x3FF00000;
    uint a = uint(VA_SATURATE( rgba.w ) * 3 + 0.5f) << 30;
    return r | g | b | a;
}

VA_INLINE float4 Unpack_R10G10B10FLOAT_A2_UNORM( uint rgba )
{
    float r = f16tof32((rgba << 5 ) & 0x7FE0);
    float g = f16tof32((rgba >> 5 ) & 0x7FE0);
    float b = f16tof32((rgba >> 15) & 0x7FE0);
    float a = (float)( rgba >> 30 ) / 3.0f;
    return float4(r, g, b, a);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Following R11G11B10 conversions taken from 
// https://github.com/Microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Core/Shaders/PixelPacking_R11G11B10.hlsli 
// Original license included:
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

// #include "ColorSpaceUtility.hlsli"

// The standard 32-bit HDR color format.  Each float has a 5-bit exponent and no sign bit.
VA_INLINE uint Pack_R11G11B10_FLOAT( float3 rgb )
{
#ifndef VA_COMPILED_AS_SHADER_CODE
    assert( rgb.x >= 0 && rgb.y >= 0 && rgb.z >= 0 );
#endif
    // Clamp upper bound so that it doesn't accidentally round up to INF 
    // Exponent=15, Mantissa=1.11111
    float mf = asfloat(0x477C0000);
    rgb = VA_MIN(rgb, float3(mf, mf, mf));  
    uint r = ((f32tof16(rgb.x) + 8) >> 4) & 0x000007FF;
    uint g = ((f32tof16(rgb.y) + 8) << 7) & 0x003FF800;
    uint b = ((f32tof16(rgb.z) + 16) << 17) & 0xFFC00000;
    return r | g | b;
}

VA_INLINE float3 Unpack_R11G11B10_FLOAT( uint rgb )
{
    float r = f16tof32((rgb << 4 ) & 0x7FF0);
    float g = f16tof32((rgb >> 7 ) & 0x7FF0);
    float b = f16tof32((rgb >> 17) & 0x7FE0);
    return float3(r, g, b);
}

// these not yet converted to work in .c/cpp
#ifdef VA_COMPILED_AS_SHADER_CODE

// An improvement to float is to store the mantissa in logarithmic form.  This causes a
// smooth and continuous change in precision rather than having jumps in precision every
// time the exponent increases by whole amounts.
uint Pack_R11G11B10_FLOAT_LOG( float3 rgb )
{
    float3 flat_mantissa = asfloat((asuint(rgb) & 0x7FFFFF) | 0x3F800000);
    float3 curved_mantissa = min(log2(flat_mantissa) + 1.0, asfloat(0x3FFFFFFF));
    rgb = asfloat( (asuint(rgb) & 0xFF800000) | (asuint(curved_mantissa) & 0x7FFFFF) );

    uint r = ((f32tof16(rgb.x) + 8) >>  4) & 0x000007FF;
    uint g = ((f32tof16(rgb.y) + 8) <<  7) & 0x003FF800;
    uint b = ((f32tof16(rgb.z) + 16) << 17) & 0xFFC00000;
    return r | g | b;
}

float3 Unpack_R11G11B10_FLOAT_LOG( uint p )
{
    float3 rgb = f16tof32(uint3(p << 4, p >> 7, p >> 17) & uint3(0x7FF0, 0x7FF0, 0x7FE0));
    float3 curved_mantissa = asfloat((asuint(rgb) & 0x7FFFFF) | 0x3F800000);
    float3 flat_mantissa = exp2(curved_mantissa - 1.0);
    return asfloat((asuint(rgb) & 0xFF800000) | (asuint(flat_mantissa) & 0x7FFFFF) );
}

// As an alternative to floating point, we can store the log2 of a value in fixed point notation.
// The 11-bit fields store 5.6 fixed point notation for log2(x) with an exponent bias of 15.  The
// 10-bit field uses 5.5 fixed point.  The disadvantage here is we don't handle underflow.  Instead
// we use the extra two exponent values to extend the range down through two more exponents.
// Range = [2^-16, 2^16)
uint Pack_R11G11B10_FIXED_LOG(float3 rgb)
{
    uint3 p = clamp((log2(rgb) + 16.0) * float3(64, 64, 32) + 0.5, 0.0, float3(2047, 2047, 1023));
    return p.b << 22 | p.g << 11 | p.r;
}

float3 Unpack_R11G11B10_FIXED_LOG(uint p)
{
    return exp2((uint3(p, p >> 11, p >> 21) & uint3(2047, 2047, 2046)) / 64.0 - 16.0);
}

// These next two encodings are great for LDR data.  By knowing that our values are [0.0, 1.0]
// (or [0.0, 2.0), incidentally), we can reduce how many bits we need in the exponent.  We can
// immediately eliminate all postive exponents.  By giving more bits to the mantissa, we can
// improve precision at the expense of range.  The 8E3 format goes one bit further, quadrupling
// mantissa precision but increasing smallest exponent from -14 to -6.  The smallest value of 8E3
// is 2^-14, while the smallest value of 7E4 is 2^-21.  Both are smaller than the smallest 8-bit
// sRGB value, which is close to 2^-12.

// This is like R11G11B10_FLOAT except that it moves one bit from each exponent to each mantissa.
uint Pack_R11G11B10_E4_FLOAT( float3 rgb )
{
    // Clamp to [0.0, 2.0).  The magic number is 1.FFFFF x 2^0.  (We can't represent hex floats in HLSL.)
    // This trick works because clamping your exponent to 0 reduces the number of bits needed by 1.
    rgb = clamp( rgb, 0.0, asfloat(0x3FFFFFFF) );
    uint r = ((f32tof16(rgb.r) + 4) >> 3 ) & 0x000007FF;
    uint g = ((f32tof16(rgb.g) + 4) << 8 ) & 0x003FF800;
    uint b = ((f32tof16(rgb.b) + 8) << 18) & 0xFFC00000;
    return r | g | b;
}

float3 Unpack_R11G11B10_E4_FLOAT( uint rgb )
{
    float r = f16tof32((rgb << 3 ) & 0x3FF8);
    float g = f16tof32((rgb >> 8 ) & 0x3FF8);
    float b = f16tof32((rgb >> 18) & 0x3FF0);
    return float3(r, g, b);
}

// This is like R11G11B10_FLOAT except that it moves two bits from each exponent to each mantissa.
uint Pack_R11G11B10_E3_FLOAT( float3 rgb )
{
    // Clamp to [0.0, 2.0).  Divide by 256 to bias the exponent by -8.  This shifts it down to use one
    // fewer bit while still taking advantage of the denormalization hardware.  In half precision,
    // the exponent of 0 is 0xF.  Dividing by 256 makes the max exponent 0x7--one fewer bit.
    rgb = clamp( rgb, 0.0, asfloat(0x3FFFFFFF) ) / 256.0;
    uint r = ((f32tof16(rgb.r) + 2) >> 2 ) & 0x000007FF;
    uint g = ((f32tof16(rgb.g) + 2) << 9 ) & 0x003FF800;
    uint b = ((f32tof16(rgb.b) + 4) << 19) & 0xFFC00000;
    return r | g | b;
}

float3 Unpack_R11G11B10_E3_FLOAT( uint rgb )
{
    float r = f16tof32((rgb << 2 ) & 0x1FFC);
    float g = f16tof32((rgb >> 9 ) & 0x1FFC);
    float b = f16tof32((rgb >> 19) & 0x1FF8);
    return float3(r, g, b) * 256.0;
}
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// these not yet converted to work in .c/cpp
#ifdef VA_COMPILED_AS_SHADER_CODE

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RGB <-> HSV/HSL/HCY/HCL
// Code borrowed from here http://www.chilliant.com/rgb2hsv.html
// (c) by Chilli Ant aka IAN TAYLOR
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Converting pure hue to RGB
float3 HUEtoRGB( const float H )
{
    float R = abs( H * 6 - 3 ) - 1;
    float G = 2 - abs( H * 6 - 2 );
    float B = 2 - abs( H * 6 - 4 );
    return saturate( float3( R, G, B ) );
}
//
// Converting RGB to hue/chroma/value
static const float ccEpsilon = 1e-10f;
float3 RGBtoHCV( const float3 RGB )
{
    // Based on work by Sam Hocevar and Emil Persson
    float4 P = ( RGB.g < RGB.b ) ? float4( RGB.bg, -1.0, 2.0 / 3.0 ) : float4( RGB.gb, 0.0, -1.0 / 3.0 );
    float4 Q = ( RGB.r < P.x ) ? float4( P.xyw, RGB.r ) : float4( RGB.r, P.yzx );
    float C = Q.x - min( Q.w, Q.y );
    float H = abs( ( Q.w - Q.y ) / ( 6 * C + ccEpsilon ) + Q.z );
    return float3( H, C, Q.x );
}
//
// Converting HSV to RGB
float3 HSVtoRGB( const float3 HSV )
{
    float3 RGB = HUEtoRGB( HSV.x );
    return ( ( RGB - 1 ) * HSV.y + 1 ) * HSV.z;
}
//
// Converting HSL to RGB
float3 HSLtoRGB( in float3 HSL )
{
    float3 RGB = HUEtoRGB( HSL.x );
    float C = ( 1 - abs( 2 * HSL.z - 1 ) ) * HSL.y;
    return ( RGB - 0.5 ) * C + HSL.z;
}
//
// Converting HCY to RGB
static const float3 ccHCYwts = float3( 0.299, 0.587, 0.114 ); // The weights of RGB contributions to luminance.
float3 HCYtoRGB( in float3 HCY )
{
    float3 RGB = HUEtoRGB( HCY.x );
    float Z = dot( RGB, ccHCYwts );
    if( HCY.z < Z )
    {
        HCY.y *= HCY.z / Z;
    }
    else if( Z < 1 )
    {
        HCY.y *= ( 1 - HCY.z ) / ( 1 - Z );
    }
    return ( RGB - Z ) * HCY.y + HCY.z;
}
//
// Converting HCL to RGB
static const float ccHCLgamma = 3;
static const float ccHCLy0 = 100;
static const float ccHCLmaxL = 0.530454533953517; // == exp(ccHCLgamma / ccHCLy0) - 0.5
static const float ccPI = 3.1415926536;
float3 HCLtoRGB( in float3 HCL )
{
    float3 RGB = 0;
    if( HCL.z != 0 )
    {
        float H = HCL.x;
        float C = HCL.y;
        float L = HCL.z * ccHCLmaxL;
        float Q = exp( ( 1 - C / ( 2 * L ) ) * ( ccHCLgamma / ccHCLy0 ) );
        float U = ( 2 * L - C ) / ( 2 * Q - 1 );
        float V = C / Q;
        float T = tan( ( H + min( frac( 2 * H ) / 4, frac( -2 * H ) / 8 ) ) * ccPI * 2 );
        H *= 6;
        if( H <= 1 )
        {
            RGB.r = 1;
            RGB.g = T / ( 1 + T );
        }
        else if( H <= 2 )
        {
            RGB.r = ( 1 + T ) / T;
            RGB.g = 1;
        }
        else if( H <= 3 )
        {
            RGB.g = 1;
            RGB.b = 1 + T;
        }
        else if( H <= 4 )
        {
            RGB.g = 1 / ( 1 + T );
            RGB.b = 1;
        }
        else if( H <= 5 )
        {
            RGB.r = -1 / T;
            RGB.b = 1;
        }
        else
        {
            RGB.r = 1;
            RGB.b = -T;
        }
        RGB = RGB * V + U;
    }
    return RGB;
}
//
// Converting RGB to HSV
float3 RGBtoHSV( in float3 RGB )
{
    float3 HCV = RGBtoHCV( RGB );
    float S = HCV.y / ( HCV.z + ccEpsilon );
    return float3( HCV.x, S, HCV.z );
}
//
// Converting RGB to HSL
float3 RGBtoHSL( in float3 RGB )
{
    float3 HCV = RGBtoHCV( RGB );
    float L = HCV.z - HCV.y * 0.5;
    float S = HCV.y / ( 1 - abs( L * 2 - 1 ) + ccEpsilon );
    return float3( HCV.x, S, L );
}
//
// Converting RGB to HCY
float3 RGBtoHCY( in float3 RGB )
{
    float3 HCV = RGBtoHCV( RGB );
    float Y = dot( RGB, ccHCYwts );
    if( HCV.y != 0 )
    {
        float Z = dot( HUEtoRGB( HCV.x ), ccHCYwts );
        if( Y > Z )
        {
            Y = 1 - Y;
            Z = 1 - Z;
        }
        HCV.y *= Z / Y;
    }
    return float3( HCV.x, HCV.y, Y );
}
//
// Converting RGB to HCL
float3 RGBtoHCL( in float3 RGB )
{
    float3 HCL;
    float H = 0;
    float U = min( RGB.r, min( RGB.g, RGB.b ) );
    float V = max( RGB.r, max( RGB.g, RGB.b ) );
    float Q = ccHCLgamma / ccHCLy0;
    HCL.y = V - U;
    if( HCL.y != 0 )
    {
        H = atan2( RGB.g - RGB.b, RGB.r - RGB.g ) / ccPI;
        Q *= U / V;
    }
    Q = exp( Q );
    HCL.x = frac( H / 2 - min( frac( H ), frac( -H ) ) / 6 );
    HCL.y *= Q;
    HCL.z = lerp( -U, V, Q ) / ( ccHCLmaxL * 2 );
    return HCL;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Normalmap encode/decode 
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// http://aras-p.info/texts/CompactNormalStorage.html

// #define NORMAL_COMPRESSION 2 		// 2 is LAEA
//
// // Spheremap Transform
// #if (NORMAL_COMPRESSION == 1)
// 
// float3 NormalmapEncode(float3 n)    // Spheremap Transform: http://www.crytek.com/sites/default/files/A_bit_more_deferred_-_CryEngine3.ppt
// {
//     //n.xyz = n.xzy;                  // swizzle for y up
// 
//     this needs testing
// 
//     n.z = 1-n.z;                    // positive z towards camera
// 
//     n.rgb = n.rgb * 2 - 1;          // [0, 1] to [-1, 1]
//     n.rgb = normalize( n.rgb );
// 
//     float2 enc = normalize(n.xy) * sqrt( n.z * 0.5 + 0.5 );
//     
//     enc = enc * 0.5 + 0.5;          // [-1, 1] to [0, 1]
//     
//     return float3( enc, 0 );
// }
// 
// float3 NormalmapDecode(float3 enc)
// {
//     enc.rg = enc.rg * 2 - 1;        // [0, 1] to [-1, 1]
// 
//     float3 ret;
//     ret.z = length( enc.xy ) * 2 - 1;
//     ret.xy = normalize( enc.xy ) * sqrt(1-ret.z*ret.z);
// 
//     ret = ret * 0.5 + 0.5;          // [-1, 1] to [0, 1]
// 
//     ret.z = 1 - ret.z;              // positive z towards camera
// 
//     //ret.xzy = ret.xyz;            // swizzle for y up
// 
//     return ret;
// }
//
//#elif (NORMAL_COMPRESSION == 2)

// LAEA - Lambert Azimuthal Equal-Area projection (http://en.wikipedia.org/wiki/Lambert_azimuthal_equal-area_projection)
float2 NormalmapEncodeLAEA(float3 n)
{
    //n.xyz = n.xzy;                  // swizzle for y up

    float f = sqrt(8*n.z+8);
    float2 ret = n.xy / f + 0.5;

    ret = ret * 0.5 + 0.5;          // [-1, 1] to [0, 1]

    return ret;
}
float3 NormalmapDecodeLAEA(float2 enc)
{
    enc.rg = enc.rg * 2 - 1;        // [0, 1] to [-1, 1]

    float2 fenc = enc.xy*4-2;
    float f = dot(fenc,fenc);
    float g = sqrt(1-f/4);
    float3 ret;
    ret.xy = fenc*g;
    ret.z = 1-f/2;

    return ret;
}

float3 NormalDecode_XYZ_UNORM( float3 normal )
{
    normal.xyz = normal.xyz * 2.0 - 1.0;
    return normalize(normal.xyz);
}

float3 NormalEncode_XYZ_UNORM( float3 normal )
{
    return (normal.xyz * 0.5) + 0.5;
}

float3 NormalDecode_XY_UNORM( float2 normalIn )
{
    float3 normal;
    normal.xy = normalIn.xy * 2.0 - 1.0;
    normal.z = saturate( 1.0 - normal.x*normal.x - normal.y * normal.y );
    return normalize(normal);
}

float2 NormalEncode_XY_UNORM( float3 normal )
{
    return (normal.xy * 0.5) + 0.5;
}

float3 NormalDecode_WY_UNORM( float4 normal )
{
    normal.xy = normal.wy * 2.0 - 1.0;
    normal.z = saturate( 1.0 - normal.x*normal.x - normal.y * normal.y );
    return normalize(normal.xyz);
}

float3 NormalDecode_XY_LAEA( float2 normalIn )
{
    return NormalmapDecodeLAEA( normalIn );
}



//#else   // no compression, just normalize
//
//float2 NormalmapEncode(float3 n)
//{
//    n = n * 0.5 + 0.5;              // [-1, 1] to [0, 1]
//    return n;
//}
//
//float3 NormalmapDecode(float3 enc)
//{
//    n.rgb = n.rgb * 2 - 1;          // [0, 1] to [-1, 1]
//    normal.z = sqrt(1.0 - normal.x * normal.x - normal.y * normal.y);   // unpack
//    //n.rgb = normalize( n.rgb );
//    return enc;
//}
//
//#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif

#ifndef VA_COMPILED_AS_SHADER_CODE

#undef float3     
#undef float4     
#undef asfloat
#undef asuint
#undef f32tof16
#undef f16tof32

} // namespace Vanilla
#endif


#endif // VA_CONVERSIONS__H