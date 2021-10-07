///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __VA_NOISE_HLSL__
#define __VA_NOISE_HLSL__

#ifndef VA_COMPILED_AS_SHADER_CODE
#error not intended to be included outside of HLSL!
#endif

#include "vaShared.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Note: For more ideas on noise see "The Book of Shaders - Random": https://thebookofshaders.com/10/
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Perlin simplex noise
//
// Description : Array and textureless GLSL 2D simplex noise function.
//      Author : Ian McEwan, Ashima Arts.
//  Maintainer : ijm
//     Lastmod : 20110409 (stegu)
//     License : Copyright (C) 2011 Ashima Arts. All rights reserved.
//               Distributed under the MIT License. See LICENSE file.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
float3 permute(float3 x) { return GLSL_mod(((x*34.0)+1.0)*x, 289.0); }
//
float snoise(float2 v)
{
   const float4 C = float4( 0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439 );
   float2 i  = floor(v + dot(v, C.yy) );
   float2 x0 = v -   i + dot(i, C.xx);
   float2 i1;
   i1 = (x0.x > x0.y) ? float2(1.0, 0.0) : float2(0.0, 1.0);
   float4 x12 = x0.xyxy + C.xxzz;
   x12.xy -= i1;
   i = GLSL_mod(i, 289.0);
   float3 p = permute( permute( i.y + float3( 0.0, i1.y, 1.0 ) ) + i.x + float3( 0.0, i1.x, 1.0 ) );
   float3 m = max(0.5 - float3(dot(x0,x0), dot(x12.xy,x12.xy), dot(x12.zw,x12.zw)), 0.0);
   m = m*m ;
   m = m*m ;
   float3 x = 2.0 * frac(p * C.www) - 1.0;
   float3 h = abs(x) - 0.5;
   float3 ox = floor(x + 0.5);
   float3 a0 = x - ox;
   m *= 1.79284291400159 - 0.85373472095314 * ( a0*a0 + h*h );
   float3 g;
   g.x  = a0.x  * x0.x  + h.x  * x0.y;
   g.yz = a0.yz * x12.xz + h.yz * x12.yw;
   return 130.0 * dot(m, g);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hash functions from https://www.shadertoy.com/view/lt2yDm / https://www.shadertoy.com/view/4djSRW
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// nicely get 'next' [0, 1) float random! this isn't a good uniform distribution quasi-random
float Rand1D( float u )
{
    return frac(sin(u) * 1e4); 
}
// Expect (roughly) [0, 1) inputs
float Rand2D( float2 uv )
{
    const float HASHSCALE1 = 443.8975;
	float3 p3  = frac(float3(uv.xyx) * HASHSCALE1);
    p3 += dot(p3, p3.yzx + 19.19);
    return frac((p3.x + p3.y) * p3.z);
}
// Expect (roughly) [0, 4096+] inputs - should work up to 16k
float Rand2DScreen( float2 uv )
{
    const float HASHSCALE1 = 0.1031;
    float3 p3  = frac(float3(uv.xyx) * HASHSCALE1);
    p3 += dot(p3, p3.yzx + 19.19);
    return frac((p3.x + p3.y) * p3.z);
}
// Expect (roughly) [0, 1] inputs
float Rand3D( float3 p3 )
{
    const float HASHSCALE1 = 443.8975; // 0.1031;    // use 443.8975 for [0, 1] inputs
	p3  = frac( p3 * HASHSCALE1 );
    p3 += dot( p3, p3.yzx + 19.19 );
    return frac( (p3.x + p3.y) * p3. z);
}
//
// This little gem is from https://nullprogram.com/blog/2018/07/31/, "Prospecting for Hash Functions" by Chris Wellons
// There's also the inverse for the lowbias32, and a 64bit version.
uint Hash32( uint x )    // just use something like x + (y << 15) for screen coords
{
#if 1   // faster, higher bias
    // exact bias: 0.17353355999581582
    // uint32_t lowbias32(uint32_t x)
    // {
        x ^= x >> 16;
        x *= uint(0x7feb352d);
        x ^= x >> 15;
        x *= uint(0x846ca68b);
        x ^= x >> 16;
        return x;
    //}
#else   // slower, lower bias
    // exact bias: 0.020888578919738908
    // uint32_t triple32(uint32_t x)
    // {
        x ^= x >> 17;
        x *= uint(0xed5ad4bb);
        x ^= x >> 11;
        x *= uint(0xac4c1b51);
        x ^= x >> 15;
        x *= uint(0x31848bab);
        x ^= x >> 14;
        return x;
    //}
#endif
}
// converting the whole 32bit uint to [0, 1)
float Hash32ToFloat(uint hash)
{ 
    return hash / 4294967296.0;
}
// converting the upper 24bits to [0, 1) because the higher bits have better distribution in some hash algorithms (like sobol)
float Hash24ToFloat(uint hash)
{ 
    return (hash>>8) / 16777216.0;
}
float Hash32NextFloatAndAdvance( inout uint hash )
{
    float rand = Hash32ToFloat( hash );
    hash = Hash32( hash );
    return rand;
}
float2 Hash32NextFloat2AndAdvance( inout uint hash )
{   // yes yes could be 1-liner but I'm not 100% certain ordering is same across all compilers
    float rand1 = Hash32NextFloatAndAdvance( hash );
    float rand2 = Hash32NextFloatAndAdvance( hash );
    return float2( rand1, rand2 );
}
//
// popular hash_combine (boost, etc.)
uint Hash32Combine( const uint seed, const uint value )
{
    return seed ^ (Hash32(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Object-space noise
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
#if 1
//
// For object-space noise! 
// https://casual-effects.com/research/Wyman2017Hashed/Wyman2017Hashed.pdf
//
float Noise3D( const float3 referencePos, const float referenceLOD, float coneWidth, float coneWidthProjected, out float noise )
{
    const float cHashScale      = 0.3448285;
    const float pixScaleLog2    = -referenceLOD * 2.0 - cHashScale;

    // Find two nearest log-discretized noise scales 
    float pixScaleL = exp2( floor( pixScaleLog2 ) / 2.0 );
    float pixScaleH = exp2( ceil( pixScaleLog2 ) / 2.0 );

    // Compute alpha thresholds at our two noise scales 
    float alphaL = Rand3D( floor( pixScaleL * referencePos.xyz ) / 1e3 );
    float alphaH = Rand3D( floor( pixScaleH * referencePos.xyz ) / 1e3 );

    // Factor to interpolate lerp with 
    float lerpFactor = frac( pixScaleLog2 );

    noise = LerpCDF( alphaL, alphaH, lerpFactor );
    noise = saturate( noise );

    noise = frac( noise + g_globals.Noise.x ); //Hash2D( float2( noise, g_globals.Noise.x ) );

    // this noise function is not that great at handling anisotropy at glancing angles so it optionally provides this value to attenuate anything that's causing a lot of aliasing or etc.
    float noiseAttenuation = saturate( coneWidthProjected / coneWidth * 0.2 - 0.2 );
    return noiseAttenuation;
}

#else // this is older code used to get to the code above!
//
// https://casual-effects.com/research/Wyman2017Hashed/Wyman2017Hashed.pdf
//
void Noise3D( float3 worldCoord, out float noise, out float noiseAttenuation )
{
    const float cHashScale = 1.27;

    // // to make it stable across x period (say 1024), here's an idea:
    // float3 worldBase = QuadReadLaneAt( worldCoord, 0 ); //min( min( QuadReadLaneAt( worldCoord, 0 ), QuadReadLaneAt( worldCoord, 1 ) ), min( QuadReadLaneAt( worldCoord, 2 ), QuadReadLaneAt( worldCoord, 3 ) ) );
    // worldCoord -= int3( worldBase / 1024 ) * 1024;
    // // however DXC shader compiler bug prevents this from working

    // todo: code below can be optimized!

#ifdef VA_RAYTRACING
    // TEMP
    noise = 0.5;
    noiseAttenuation = 1.0;
#else

#if 1
    // Find the discretized derivatives of our coordinates 
    //float maxDeriv = max( length(ddx_fine(worldCoord.xyz)), length(ddy_fine(worldCoord.xyz)) ); 
    float maxDeriv = length( (abs( ddx_fine(worldCoord.xyz) ) + abs( ddy_fine(worldCoord.xyz) ) ) ) * 0.5;
    float pixScale = 1.0/(cHashScale*maxDeriv);

    const float lerpScale = 2.0;
    const float pixScaleLog2 = log2( pixScale ) * lerpScale;

    // Find two nearest log-discretized noise scales 
    float pixScaleL = exp2( floor( pixScaleLog2 ) / lerpScale );
    float pixScaleH = exp2( ceil( pixScaleLog2 ) / lerpScale );

    // Compute alpha thresholds at our two noise scales 
    float alphaL = Hash3D( floor( pixScaleL * worldCoord.xyz ) );
    float alphaH = Hash3D( floor( pixScaleH * worldCoord.xyz ) );

    // Factor to interpolate lerp with 
    float lerpFactor = frac( pixScaleLog2 );

    noise = LerpCDF( alphaL, alphaH, lerpFactor );
    noise = saturate( noise );

    noise = frac( noise + g_globals.Noise.x ); //Hash2D( float2( noise, g_globals.Noise.x ) );

    {
        float lengthX = length( ddx_fine(worldCoord.xyz) );
        float lengthY = length( ddy_fine(worldCoord.xyz) );
        noiseAttenuation = saturate( ( max( lengthX, lengthY ) / min( lengthX, lengthY ) ) * 0.1 - 0.2 ) * 0.75;
    }
#else
    // based on ideas from https://twitter.com/_cwyman_/status/839233419502444544 
    // but it's not that much better and lerp-ing actually requires a full 3d lerp (8 hash values, 4+2+1 Hash3D-s or a better CDF function)
    float3 pixelDeriv   = max( abs( ddx_fine(worldCoord.xyz) ), abs( ddy_fine(worldCoord.xyz) ) ); // <- try avg instead of max or max * 0.8 + min * 0.2 for ex
    //float3 pixelDeriv   = ( abs( ddx_fine(worldCoord.xyz) ) + abs( ddy_fine(worldCoord.xyz) ) ) * 0.5; // <- try avg instead of max or max * 0.8 + min * 0.2 for ex
    //float3 pixelDeriv   = abs( ddx_fine(worldCoord.xyz) ); // + abs( ddy_fine(worldCoord.xyz) ) ) * 0.5; // <- try avg instead of max or max * 0.8 + min * 0.2 for ex

    pixelDeriv = lerp( pixelDeriv, length(pixelDeriv ) * 1.33, 0.0001 );

    float3 pixelScale   = 1.5 / (cHashScale * pixelDeriv);

    float3 pixScaleLog2 = log2( pixelScale );
    float3 anisoFloor0  = exp2( floor( pixScaleLog2 - 0.3333333 ) );
    float3 anisoFloor1  = exp2( floor( pixScaleLog2 - 0.6666667 ) );
    float3 anisoFloor2  = exp2( floor( pixScaleLog2 + 0.3333333 ) );
    float3 anisoFloor3  = exp2( floor( pixScaleLog2 + 0.6666667 ) );
    //float3 triLerpFact  = frac( pixScaleLog2 * 2.0 );

    float hash0 = Hash3D( floor( anisoFloor0 * worldCoord.xyz ) );
    float hash1 = Hash3D( floor( anisoFloor1 * worldCoord.xyz ) );
    float hash2 = Hash3D( floor( anisoFloor2 * worldCoord.xyz ) );
    float hash3 = Hash3D( floor( anisoFloor3 * worldCoord.xyz ) );
    //return hash0;
    //return LerpCDF( hash0, hash1, 0.5 );
    return LerpCDF( LerpCDF( hash0, hash1, 0.5 ), LerpCDF( hash2, hash3, 0.5 ), 0.5 );

    // triLerpFact = saturate( triLerpFact * 3 - 2 );
    // float3 activeLerps = triLerpFact > 0;
    // 
    // float3 c0 = uint3( floor( pixScaleLog2 + 256 ) ) % 2;
    // float3 c1 = uint3( floor( pixScaleLog2 + 256 + activeLerps ) ) % 2;
    // 
    // float lerpFact = max( max( triLerpFact.x, triLerpFact.y ), triLerpFact.z );
    // 
    // return lerp( c0, c1, lerpFact );
    // return triLerpFact;

    // triLerpFact = floor( triLerpFact * 3 ) / 3;
    // anisoFloor = lerp( anisoFloor, anisoFloor * 2, triLerpFact );

    /*
    float3 ca = floor( anisoFloor * worldCoord.xyz );
    float3 cb = floor( anisoFloor * worldCoord.xyz * 2.0 );

    float hx = LerpCDF( wang_hash_f( ca.x ), wang_hash_f( cb.x ), triLerpFact.x );

    return hx;
    */

    // {
    //     float3 p3 = floor( anisoFloor * worldCoord.xyz );
    //     float3 q3 = floor( anisoFloor * worldCoord.xyz * 2 );
    // 
    //     const float HASHSCALE1 = 0.1031;    // use 443.8975 for [0, 1] inputs
	//     p3 = frac( p3 * HASHSCALE1 );
    //     q3 = frac( q3 * HASHSCALE1 );
    //     
    //     //p3.x = LerpCDF( p3.x, q3.x, triLerpFact.x );
    //     //p3.y = LerpCDF( p3.y, q3.y, triLerpFact.y );
    //     //p3.z = LerpCDF( p3.z, q3.z, triLerpFact.z );
    //     //return p3;
    //     //return triLerpFact;
    // 
    //     p3 += dot( p3, p3.yzx + 19.19 );
    //     return frac( (p3.x + p3.y) * p3. z).xxx;
    // }

#if 0
    float3 anisoCeil    = exp2( ceil( pixScaleLog2 ) );
    float3 triLerpFact  = frac( pixScaleLog2 );

    // Compute alpha thresholds at our two noise scales 
    float alphaL = Hash3D( floor( anisoFloor * worldCoord.xyz ) );
    float alphaH = Hash3D( floor( anisoCeil * worldCoord.xyz ) );

    //float lerpFactor    = length( triLerpFact ) / ( length( triLerpFact ) + length( 1.0.xxx - triLerpFact ) );

    //ok uradi rgb vizualizaciju prvo 
    //    onda razmisli o tome da se uvek lerp-uje na samo jedan sledeci pravac - ostale ignorisi

    // lerp factor weights
    float3 lfws         = (abs(triLerpFact - 0.5) + 0.5);
    float lerpFactor    = dot( triLerpFact, lfws ) / dot( 1, lfws );
#endif

    // return lerp( c0, c1, 1 );
#endif

#if 0

    //return alphaH;
    //  return alphaL;
    //return lerpFactor;

    // Interpolate alpha threshold from noise at two scales 
    float x = (1-lerpFactor)*alphaL + lerpFactor*alphaH;

    // Pass into CDF to compute uniformly distrib threshold 
    float a = min( lerpFactor, 1-lerpFactor ); 
    float3 cases = float3( x*x/(2*a*(1-a)), (x-0.5*a)/(1-a), 1.0-((1-x)*(1-x)/(2*a*(1-a))) );

    // Find our final, uniformly distributed alpha threshold 
    float noise = (x < (1-a)) ? ((x < a) ? cases.x : cases.y) : cases.z;

    // // Avoids ?? == 0. Could also do ??=1-?? 
    // noise = clamp( noise, 1.0e-6, 1.0 );

    // (in Vanilla this is generalized to just noise, so no need to clamp to 1.0e-6)
    return saturate( noise );
#endif

#endif //#ifdef VA_RAYTRACING
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Quasi-random sequences - R*
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
// https://www.shadertoy.com/view/4lVcRm, https://www.shadertoy.com/view/MtVBRw, 
// https://www.shadertoy.com/view/3ltSzS
float R1seq(int n)
{
    return frac(float(n) * 0.618033988749894848204586834365641218413556121186522017520);
}
float2 R2seq(int n)
{
    return frac(float2(n.xx) * float2(0.754877666246692760049508896358532874940835564978799543103, 0.569840290998053265911399958119574964216147658520394151385));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Based on "Practical Hash-based Owen Scrambling", Brent Burley, Walt Disney Animation Studios
// With simplifications/optimizations taken out from https://www.shadertoy.com/view/wlyyDm# (relevant reddit thread:
// https://www.reddit.com/r/GraphicsProgramming/comments/l1go2r/owenscrambled_sobol_02_sequences_shadertoy/)
// This simplification uses Laine-Kerras permutation for the 1st dimension and Sobol' only for second, to achieve
// a good low 2D discrepancy (2 dimensional stratification).
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
uint bhos_sobol(uint index) 
{
    const uint directions[32] = {
        0x80000000u, 0xc0000000u, 0xa0000000u, 0xf0000000u,
        0x88000000u, 0xcc000000u, 0xaa000000u, 0xff000000u,
        0x80800000u, 0xc0c00000u, 0xa0a00000u, 0xf0f00000u,
        0x88880000u, 0xcccc0000u, 0xaaaa0000u, 0xffff0000u,
        0x80008000u, 0xc000c000u, 0xa000a000u, 0xf000f000u,
        0x88008800u, 0xcc00cc00u, 0xaa00aa00u, 0xff00ff00u,
        0x80808080u, 0xc0c0c0c0u, 0xa0a0a0a0u, 0xf0f0f0f0u,
        0x88888888u, 0xccccccccu, 0xaaaaaaaau, 0xffffffffu
    };

    uint X = 0u;
    for (int bit = 0; bit < 32; bit++) {
        uint mask = (index >> bit) & 1u;
        X ^= mask * directions[bit];
    }
    return X;
}
uint bhos_reverse_bits(uint x) 
{
#if 1
    return reversebits(x);  // hey we've got this in HLSL! awesome.
#else
    x = (((x & 0xaaaaaaaau) >> 1) | ((x & 0x55555555u) << 1));
    x = (((x & 0xccccccccu) >> 2) | ((x & 0x33333333u) << 2));
    x = (((x & 0xf0f0f0f0u) >> 4) | ((x & 0x0f0f0f0fu) << 4));
    x = (((x & 0xff00ff00u) >> 8) | ((x & 0x00ff00ffu) << 8));
    return ((x >> 16) | (x << 16));
#endif
}
uint bhos_laine_karras_permutation(uint x, uint seed) 
{
#if 0 // this is the original
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
#else // this is from https://psychopath.io/post/2021_01_30_building_a_better_lk_hash
    x *= 0x788aeeed;
    x ^= x * 0x41506a02;
    x += seed;
    x *= seed | 1;
    x ^= x * 0x7483dc64;
#endif
    return x;
}
uint bhos_nested_uniform_scramble_base2( uint x, uint seed )
{
    x = bhos_reverse_bits(x);
    x = bhos_laine_karras_permutation(x, seed);
    x = bhos_reverse_bits(x);
    return x;
}
float2 burley_shuffled_scrambled_sobol_pt( uint index, uint seed ) 
{
    uint shuffle_seed = Hash32Combine( seed, 0 );
    uint x_seed = Hash32Combine( seed, 1 );
    uint y_seed = Hash32Combine( seed, 2 );

    uint shuffled_index = bhos_nested_uniform_scramble_base2(index, shuffle_seed);

    uint x = bhos_reverse_bits(shuffled_index);
    uint y = bhos_sobol(shuffled_index);
    x = bhos_nested_uniform_scramble_base2(x, x_seed);
    y = bhos_nested_uniform_scramble_base2(y, y_seed);

    return float2( Hash32ToFloat(x), Hash32ToFloat(y) );
}
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Low Discrepancy Sampler
//  * returns [0, 1) float; uint possible too
//  * 1D and 2D supported so far (providing low discrepancy for 1 or 2 dimensions)
//  * 'index' stays the same for the whole path
//  * 'hashSeed' needs to be advanced (seed = Hash32(seed)) for every bounce and every effect to decorrelate sampling
//  * define VA_LDS_RANDOM_FALLBACK to switch to hash based uncorrelated noisy sampler
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//#define VA_LDS_RANDOM_FALLBACK
float LDSample1D( uint index, uint seed )
{
#ifdef VA_LDS_RANDOM_FALLBACK
    return Hash32ToFloat( Hash32( Hash32Combine( seed, index ) ) );
#else
    uint shuffle_seed = Hash32Combine( seed, 0 );
    uint x_seed = Hash32Combine( seed, 1 );
    uint shuffled_index = bhos_nested_uniform_scramble_base2(index, shuffle_seed);
    uint x = bhos_reverse_bits(shuffled_index);
    x = bhos_nested_uniform_scramble_base2(x, x_seed);
    return Hash32ToFloat(x);
#endif
}
float2 LDSample2D( uint index, uint seed )
{
#ifdef VA_LDS_RANDOM_FALLBACK
    seed = Hash32Combine( seed, index );
    uint x = Hash32( seed );
    uint y = Hash32( x );
    return float2( Hash32ToFloat( x ), Hash32ToFloat( y ) );
#else
    return burley_shuffled_scrambled_sobol_pt( index, seed );
#endif
}
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // __VA_NOISE_HLSL__