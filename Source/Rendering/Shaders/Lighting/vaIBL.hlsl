///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "../vaShared.hlsl"
#include "vaIBLShared.h"

// these are actually floats
RWTexture1D<uint>           g_SH_UAV                        : register( U_CONCATENATER( IBL_FILTER_UAV_SLOT ) );
RWTexture2DArray<float3>    g_CubeFacesRW                   : register( U_CONCATENATER( IBL_FILTER_CUBE_FACES_ARRAY_UAV_SLOT ) );

Texture2D                   g_GenericTexture0               : register( T_CONCATENATER( IBL_FILTER_GENERIC_TEXTURE_SLOT_0 ) );
Texture2DArray<float3>      g_CubeFaces                     : register( T_CONCATENATER( IBL_FILTER_CUBE_FACES_ARRAY_TEXTURE_SLOT ) );
//Texture1D<float>            g_ScalingK                      : register( T_CONCATENATER( IBL_FILTER_SCALING_FACTOR_K_TEXTURE_SLOT ) );
TextureCube                 g_Cubemap                       : register( T_CONCATENATER( IBL_FILTER_CUBE_TEXTURE_SLOT ) );

#ifdef IBL_NUM_SH_BANDS
    #define IBL_NUM_SH_COEFFS (IBL_NUM_SH_BANDS*IBL_NUM_SH_BANDS)
#endif

#define F_2_SQRTPI (1.12837916709551257389615890312154517 )
#define F_SQRT2    (1.41421356237309504880168872420969808 )
#define F_PI       (3.14159265358979323846264338327950288 )
#define F_1_PI     (0.318309886183790671537767526745028724)
#define F_SQRT1_2  (0.707106781186547524400844362104849039)
#define M_SQRT_3   (1.7320508076)
//

// consider using clamp from http://graphicrants.blogspot.com/2013/12/tone-mapping.html instead
float3 CubeHDRClamp( float3 color )
{
    return clamp( color, 0.0.xxx, 64512.0.xxx );  // 64512.0 is max encodeable by RGB111110
}

float2 DirToRectilinear(float3 s)
{
    float xf = atan2(s.x, -s.y) / VA_PI;    // range [-1.0, 1.0]        // why -y for y? no idea, probably not needed
    float yf = asin(s.z) * (2 / VA_PI);     // range [-1.0, 1.0]
    xf = (xf + 1.0f) * 0.5f; // * (width  - 1);        // range [0, width [
    yf = (1.0f - yf) * 0.5f; // * (height - 1);        // range [0, height[
    return float2(xf, yf);   // mirroring X here, might not be needed
};

[numthreads( 16, 16, 1 )]
void CSEquirectangularToCubemap( uint3 groupThreadID : SV_GroupThreadID, uint3 dispatchThreadID : SV_DispatchThreadID )
{
    float3 cubeDims;
    g_CubeFacesRW.GetDimensions( cubeDims.x, cubeDims.y, cubeDims.z );

    // compute direction
    float3 cubeDir = CubemapGetDirectionFor( cubeDims.x, dispatchThreadID.z, dispatchThreadID.x, dispatchThreadID.y );

    float2 texUV = DirToRectilinear( cubeDir );

    float4 color = g_GenericTexture0.SampleLevel( g_samplerLinearWrap, texUV, 0 );
    //float4 color = SampleBicubic9( g_GenericTexture0, g_samplerLinearWrap, texUV );

    color.xyz = CubeHDRClamp( color.xyz );

    g_CubeFacesRW[ dispatchThreadID.xyz ] = color.xyz;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// from https://www.gamedev.net/forums/topic/613648-dx11-interlockedadd-on-floats-in-pixel-shader-workaround/
void InterlockedAddFloat( uint addr, float value ) // Works perfectly! <- original comment, I won't remove because it inspires confidence
{
   uint i_val = asuint(value);
   uint tmp0 = 0;
   uint tmp1;
   [allow_uav_condition] while (true)
   {
      InterlockedCompareExchange( g_SH_UAV[addr], tmp0, i_val, tmp1);
      if (tmp1 == tmp0)
         break;
      tmp0 = tmp1;
      i_val = asuint(value + asfloat(tmp1));
   }
}

// from "filament\libs\ibl\src\CubemapUtils.cpp"
// Area of a cube face's quadrant projected onto a sphere
static inline float SphereQuadrantArea(float x, float y) 
{
    return atan2( x*y, sqrt( x*x + y*y + 1 ) );
}
// the sum of all segments should be 4*pi*r^2 or ~12.56637 :)
float CubemapSolidAngle(float cubeDim, uint ux, uint uy) 
{
    const float iDim = 1.0f / cubeDim;
    float s = ((ux + 0.5f) * 2 * iDim) - 1;
    float t = ((uy + 0.5f) * 2 * iDim) - 1;
    const float x0 = s - iDim;
    const float y0 = t - iDim;
    const float x1 = s + iDim;
    const float y1 = t + iDim;
    float solidAngle =  SphereQuadrantArea( x0, y0 ) -
                        SphereQuadrantArea( x0, y1 ) -
                        SphereQuadrantArea( x1, y0 ) +
                        SphereQuadrantArea( x1, y1 );
    return solidAngle;
}

#ifdef IBL_NUM_SH_COEFFS

// from "filament\libs\ibl\src\CubemapSH.cpp"
uint SHindex( int m, uint l ) 
{
    return l * ( l + 1 ) + m;
}
//
// Calculates non-normalized SH bases, i.e.:
//  m > 0, cos(m*phi)   * P(m,l)
//  m < 0, sin(|m|*phi) * P(|m|,l)
//  m = 0, P(0,l)
void ComputeShBasis( out float SHb[IBL_NUM_SH_COEFFS], float3 s )
{
#if 0
    // Reference implementation
    float phi = atan2(s.x, s.y);
    for (size_t l = 0; l < IBL_NUM_SH_BANDS; l++) {
        SHb[SHindex(0, l)] = Legendre(l, 0, s.z);
        for (size_t m = 1; m <= l; m++) {
            float p = Legendre(l, m, s.z);
            SHb[SHindex(-m, l)] = sin(m * phi) * p;
            SHb[SHindex( m, l)] = cos(m * phi) * p;
        }
    }
#endif

    /*
     * TODO: all the Legendre computation below is identical for all faces, so it
     * might make sense to pre-compute it once. Also note that there is
     * a fair amount of symmetry within a face (which we could take advantage of
     * to reduce the pre-compute table).
     */

    /*
     * Below, we compute the associated Legendre polynomials using recursion.
     * see: http://mathworld.wolfram.com/AssociatedLegendrePolynomial.html
     *
     * Note [0]: s.z == cos(theta) ==> we only need to compute P(s.z)
     *
     * Note [1]: We in fact compute P(s.z) / sin(theta)^|m|, by removing
     * the "sqrt(1 - s.z*s.z)" [i.e.: sin(theta)] factor from the recursion.
     * This is later corrected in the ( cos(m*phi), sin(m*phi) ) recursion.
     */

    // s = (x, y, z) = (sin(theta)*cos(phi), sin(theta)*sin(phi), cos(theta))

    // handle m=0 separately, since it produces only one coefficient
    float Pml_2 = 0;
    float Pml_1 = 1;
    SHb[0] =  Pml_1;
    for (uint l=1; l<IBL_NUM_SH_BANDS; l++) 
    {
        float Pml = ((2*l-1.0f)*Pml_1*s.z - (l-1.0f)*Pml_2) / l;
        Pml_2 = Pml_1;
        Pml_1 = Pml;
        SHb[SHindex(0, l)] = Pml;
    }
    float Pmm = 1;
    for (uint m=1 ; m<IBL_NUM_SH_BANDS ; m++) 
    {
        Pmm = (1.0f - 2*m) * Pmm;      // See [1], divide by sqrt(1 - s.z*s.z);
        Pml_2 = Pmm;
        Pml_1 = (2*m + 1.0f)*Pmm*s.z;
        // l == m
        SHb[SHindex(-int(m), m)] = Pml_2;
        SHb[SHindex( int(m), m)] = Pml_2;
        if (m+1 < IBL_NUM_SH_BANDS) 
        {
            // l == m+1
            SHb[SHindex(-int(m), m+1)] = Pml_1;
            SHb[SHindex( int(m), m+1)] = Pml_1;
#if VA_DIRECTX == 11 // avoid fxc compiler warning
            [unroll]
#endif
            for (uint l=m+2 ; l<IBL_NUM_SH_BANDS ; l++) 
            {
                float Pml = ((2*l - 1.0f)*Pml_1*s.z - (l + m - 1.0f)*Pml_2) / (l-m);
                Pml_2 = Pml_1;
                Pml_1 = Pml;
                SHb[SHindex(-int(m), l)] = Pml;
                SHb[SHindex( int(m), l)] = Pml;
            }
        }
    }

    // At this point, SHb contains the associated Legendre polynomials divided
    // by sin(theta)^|m|. Below we compute the SH basis.
    //
    // ( cos(m*phi), sin(m*phi) ) recursion:
    // cos(m*phi + phi) == cos(m*phi)*cos(phi) - sin(m*phi)*sin(phi)
    // sin(m*phi + phi) == sin(m*phi)*cos(phi) + cos(m*phi)*sin(phi)
    // cos[m+1] == cos[m]*s.x - sin[m]*s.y
    // sin[m+1] == sin[m]*s.x + cos[m]*s.y
    //
    // Note that (d.x, d.y) == (cos(phi), sin(phi)) * sin(theta), so the
    // code below actually evaluates:
    //      (cos((m*phi), sin(m*phi)) * sin(theta)^|m|
    {
        float Cm = s.x;
        float Sm = s.y;
        for (uint m = 1; m <= IBL_NUM_SH_BANDS; m++) 
        {
#if VA_DIRECTX == 11 // avoid fxc compiler warning
            [unroll]
#endif
            for (uint l = m; l < IBL_NUM_SH_BANDS; l++) 
            {
                SHb[SHindex(-int(m), l)] *= Sm;
                SHb[SHindex( int(m), l)] *= Cm;
            }
            float Cm1 = Cm * s.x - Sm * s.y;
            float Sm1 = Sm * s.x + Cm * s.y;
            Cm = Cm1;
            Sm = Sm1;
        }
    }
}

#define IBL_SH_COMPUTE_THREADGROUPSIZE 16

groupshared float3 g_tempStorage[IBL_SH_COMPUTE_THREADGROUPSIZE][IBL_SH_COMPUTE_THREADGROUPSIZE][IBL_NUM_SH_COEFFS];

[numthreads( IBL_SH_COMPUTE_THREADGROUPSIZE, IBL_SH_COMPUTE_THREADGROUPSIZE, 1 )]
void CSComputeSH( uint3 groupThreadID : SV_GroupThreadID, uint3 dispatchThreadID : SV_DispatchThreadID )
{
    // get color
    float3 cubeColor = g_CubeFaces.Load( int4( dispatchThreadID, 0 ) );
    cubeColor = CubeHDRClamp( cubeColor );

    float3 cubeDims;
    g_CubeFaces.GetDimensions( cubeDims.x, cubeDims.y, cubeDims.z );

    // compute direction
    float3 cubeDir = CubemapGetDirectionFor( cubeDims.x, dispatchThreadID.z, dispatchThreadID.x, dispatchThreadID.y );

    // take solid angle into account
    float solidAngle = CubemapSolidAngle( cubeDims.x, dispatchThreadID.x, dispatchThreadID.y );

    float SHB[IBL_NUM_SH_COEFFS];
    ComputeShBasis( SHB, cubeDir );

#if 1 // use reduce path
    // write out to groupshared
    for( uint i = 0; i < IBL_NUM_SH_COEFFS; i++ )
        g_tempStorage[groupThreadID.x][groupThreadID.y][i] = cubeColor * ( SHB[i] * solidAngle );

    // now reduce
    uint rstep = 1;
    uint rsize = IBL_SH_COMPUTE_THREADGROUPSIZE/2;
    for( ; rsize > 0; )
    {
        GroupMemoryBarrierWithGroupSync( );

        // drop threads that are no longer needed
        [branch] if( (groupThreadID.x < rsize) && (groupThreadID.y < rsize) )
        {
            int bx = groupThreadID.x * rstep * 2;
            int by = groupThreadID.y * rstep * 2;
            for( uint i = 0; i < IBL_NUM_SH_COEFFS; i++ )
            {
                float3 val =    g_tempStorage[bx+0    ][by+0    ][i] +
                                g_tempStorage[bx+rstep][by+0    ][i] +
                                g_tempStorage[bx+0    ][by+rstep][i] +
                                g_tempStorage[bx+rstep][by+rstep][i];

                if( rsize == 1 )
                {
                    // write out
                    InterlockedAddFloat( i*3 + 0, val.x );
                    InterlockedAddFloat( i*3 + 1, val.y );
                    InterlockedAddFloat( i*3 + 2, val.z );
                }
                else
                {
                    // reduce
                    g_tempStorage[bx][by][i] = val;
                }
            }
        }
        rsize /= 2;
        rstep *= 2;
    }

#else // use slow interlocked-on-all path

    // apply coefficients to the sampled color
    for( uint i = 0; i < IBL_NUM_SH_COEFFS; i++ )
    {
        float3 shOut = cubeColor * ( SHB[i] * solidAngle );
        InterlockedAddFloat( i*3 + 0, shOut.x );
        InterlockedAddFloat( i*3 + 1, shOut.y );
        InterlockedAddFloat( i*3 + 2, shOut.z );
    }
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
 * returns n! / d!
 */
float factorial( uint n, uint d /*= 1*/ ) 
{
   d = max( uint(1), d);
   n = max( uint(1), n);
   float r = 1.0;
   if (n == d) {
       // intentionally left blank
   } 
   else if (n > d) 
   {
#if VA_DIRECTX == 11 // avoid fxc compiler warning
        [unroll]
#endif
       for ( ; n>d ; n--) 
       {
           r *= n;
       }
   } 
   else 
   {
#if VA_DIRECTX == 11 // avoid fxc compiler warning
        [unroll]
#endif
       for ( ; d>n ; d--) 
       {
           r *= d;
       }
       r = 1.0f / r;
   }
   return r;
}
//
/*
 * SH scaling factors:
 *  returns sqrt((2*l + 1) / 4*pi) * sqrt( (l-|m|)! / (l+|m|)! )
 */
static float Kml(int m, uint l) 
{
    m = m < 0 ? -m : m;  // abs() is not constexpr
    const float K = (2 * l + 1) * factorial(uint(l - m), uint(l + m));
    return float(sqrt(K) * (F_2_SQRTPI * 0.25));
}
//
void Ki(out float K[IBL_NUM_SH_COEFFS]) 
{
    for (uint l = 0; l < IBL_NUM_SH_BANDS; l++) 
    {
        K[SHindex(0, l)] = Kml(0, l);
#if VA_DIRECTX == 11 // avoid fxc compiler warning
        [unroll]
#endif
        for (uint m = 1; m <= l; m++) 
        {
            K[SHindex(m, l)] =
            K[SHindex(-int(m), l)] = float(F_SQRT2 * Kml(m, l));
        }
    }
}
//
// < cos(theta) > SH coefficients pre-multiplied by 1 / K(0,l)
float ComputeTruncatedCosSh( uint l ) 
{
    if( l == 0 ) {
        return (float)F_PI;
    }
    else if( l == 1 ) {
        return float(2 * F_PI / 3);
    }
    else if( l & 1u ) {
        return 0.0f;
    }
    const uint l_2 = l / 2;
    float A0 = ( ( l_2 & 1u ) ? 1.0f : -1.0f ) / ( ( l + 2 ) * ( l - 1 ) );
    float A1 = factorial( l, l_2 ) / ( factorial( l_2, 1 ) * ( 1U << l ) );
    return float(2 * F_PI * A0 * A1);
}
//
/*
 * SH from environment with high dynamic range (or high frequencies -- high dynamic range creates
 * high frequencies) exhibit "ringing" and negative values when reconstructed.
 * To mitigate this, we need to low-pass the input image -- or equivalently window the SH by
 * coefficient that tapper towards zero with the band.
 *
 * We use ideas and techniques from
 *    Stupid Spherical Harmonics (SH)
 *    Deringing Spherical Harmonics
 * by Peter-Pike Sloan
 * https://www.ppsloan.org/publications/shdering.pdf
 *
 */
float SincWindow(uint l, float w)
{
    if (l == 0) {
        return 1.0f;
    } else if (float(l) >= w) {
        return 0.0f;
    }

    // we use a sinc window scaled to the desired window size in bands units
    // a sinc window only has zonal harmonics
    float x = (float(F_PI) * l) / w;
    x = sin(x) / (x+0.000000001);

    // The convolution of a SH function f and a ZH function h is just the product of both
    // scaled by 1 / K(0,l) -- the window coefficients include this scale factor.

    // Taking the window to power N is equivalent to applying the filter N times
    return pow(x, 4);
}
//
void multiply3( out float result[3], const float M[3][3], const float x[3] ) 
{
    result[0] = M[0][0] * x[0] + M[1][0] * x[1] + M[2][0] * x[2];
    result[1] = M[0][1] * x[0] + M[1][1] * x[1] + M[2][1] * x[2];
    result[2] = M[0][2] * x[0] + M[1][2] * x[1] + M[2][2] * x[2];
};
/*
 * utilities to rotate very low order spherical harmonics (up to 3rd band)
 */
void RotateSphericalHarmonicBand1( out float result[3], const float band1[3], const float M[3][3] )
{
    // inverse() is not constexpr -- so we pre-calculate it in mathematica
    //
    //    constexpr float3 N0{ 1, 0, 0 };
    //    constexpr float3 N1{ 0, 1, 0 };
    //    constexpr float3 N2{ 0, 0, 1 };
    //
    //    constexpr mat3f A1 = { // this is the projection of N0, N1, N2 to SH space
    //            float3{ -N0.y, N0.z, -N0.x },
    //            float3{ -N1.y, N1.z, -N1.x },
    //            float3{ -N2.y, N2.z, -N2.x }
    //    };
    //
    //    const mat3f invA1 = inverse(A1);

    const float invA1TimesK[3][3] = {
            {  0, -1,  0 },
            {  0,  0,  1 },
            { -1,  0,  0 }
    };
    
    const float R1OverK[3][3] = {
            { -M[0][1], M[0][2], -M[0][0] },
            { -M[1][1], M[1][2], -M[1][0] },
            { -M[2][1], M[2][2], -M[2][0] }
    };

    float temp0[3];
    multiply3( temp0, invA1TimesK, band1 );
    multiply3( result, R1OverK, temp0 );
    //return R1OverK * (invA1TimesK * band1);
}
//
static void multiply5( out float result[5], const float M[5][5], const float x[5] )
{
    result[0] = M[0][0] * x[0] + M[1][0] * x[1] + M[2][0] * x[2] + M[3][0] * x[3] + M[4][0] * x[4];
    result[1] = M[0][1] * x[0] + M[1][1] * x[1] + M[2][1] * x[2] + M[3][1] * x[3] + M[4][1] * x[4];
    result[2] = M[0][2] * x[0] + M[1][2] * x[1] + M[2][2] * x[2] + M[3][2] * x[3] + M[4][2] * x[4];
    result[3] = M[0][3] * x[0] + M[1][3] * x[1] + M[2][3] * x[2] + M[3][3] * x[3] + M[4][3] * x[4];
    result[4] = M[0][4] * x[0] + M[1][4] * x[1] + M[2][4] * x[2] + M[3][4] * x[3] + M[4][4] * x[4];
};
//
// This projects a vec3 to SH2/k space (i.e. we premultiply by 1/k)
// below can't be constexpr
void project5( out float result[5], const float s[3] )
{
    result[0] = ( s[1] * s[0] );
    result[1] = -( s[1] * s[2] );
    result[2] = 1 / ( 2 * M_SQRT_3 ) * ( ( 3 * s[2] * s[2] - 1 ) );
    result[3] = -( s[2] * s[0] );
    result[4] = 0.5f * ( ( s[0] * s[0] - s[1] * s[1] ) );
}
//
void RotateSphericalHarmonicBand2( out float result[5], const float band2[5], const float M[3][3]) 
{
    //constexpr float M_SQRT_3  = 1.7320508076f;
    const float n = (float)F_SQRT1_2;

    //  Below we precompute (with help of Mathematica):
    //    constexpr float3 N0{ 1, 0, 0 };
    //    constexpr float3 N1{ 0, 0, 1 };
    //    constexpr float3 N2{ n, n, 0 };
    //    constexpr float3 N3{ n, 0, n };
    //    constexpr float3 N4{ 0, n, n };
    //    constexpr float M_SQRT_PI = 1.7724538509f;
    //    constexpr float M_SQRT_15 = 3.8729833462f;
    //    constexpr float k = M_SQRT_15 / (2.0f * M_SQRT_PI);
    //    --> k * inverse(mat5{project(N0), project(N1), project(N2), project(N3), project(N4)})
    float invATimesK[5][5] = {
            {    0,        1,   2,   0,  0 },
            {   -1,        0,   0,   0, -2 },
            {    0, M_SQRT_3,   0,   0,  0 },
            {    1,        1,   0,  -2,  0 },
            {    2,        1,   0,   0,  0 }
    };

    // this is: invA * k * band2
    // 5x5 matrix by vec5 (this a lot of zeroes and constants, which the compiler should eliminate)
    float invATimesKTimesBand2[5];
    multiply5(invATimesKTimesBand2, invATimesK, band2);

    // this is: mat5{project(N0), project(N1), project(N2), project(N3), project(N4)} / k
    // (the 1/k comes from project(), see above)
    float ROverK[5][5]; 
    project5(ROverK[0], M[0]);                  // M * N0
    project5(ROverK[1], M[2]);                  // M * N1
    float3 _k0 = n * ( float3( M[0] ) + float3( M[1] ) );
    float3 _k1 = n * ( float3( M[0] ) + float3( M[2] ) );
    float3 _k2 = n * ( float3( M[1] ) + float3( M[2] ) );
    float k0[3] = { _k0.x, _k0.y, _k0.z };
    float k1[3] = { _k1.x, _k1.y, _k1.z };
    float k2[3] = { _k2.x, _k2.y, _k2.z };
    project5( ROverK[2], k0 );     // M * N2
    project5( ROverK[3], k1 );     // M * N3
    project5( ROverK[4], k2 );     // M * N4

    // notice how "k" disappears
    // this is: (R / k) * (invA * k) * band2 == R * invA * band2
    multiply5( result, ROverK, invATimesKTimesBand2 );
}
//
void WindowSH_RotateSh3Bands( inout float sh[9], float3x3 _M ) 
{
    float M[3][3];
    M[0][0] = _M[0][0];     M[0][1] = _M[0][1];     M[0][2] = _M[0][2];
    M[1][0] = _M[1][0];     M[1][1] = _M[1][1];     M[1][2] = _M[1][2];
    M[2][0] = _M[2][0];     M[2][1] = _M[2][1];     M[2][2] = _M[2][2];

    const float b0 = sh[0];
    const float band1[3] = { sh[1], sh[2], sh[3] };
    float b1[3];
    RotateSphericalHarmonicBand1( b1, band1, M );
    const float band2[5] = { sh[4], sh[5], sh[6], sh[7], sh[8] };
    float b2[5];
    RotateSphericalHarmonicBand2( b2, band2, M );
    sh[0] = b0;
    sh[1] = b1[0];
    sh[2] = b1[1];
    sh[3] = b1[2];
    sh[4] = b2[0];
    sh[5] = b2[1];
    sh[6] = b2[2];
    sh[7] = b2[3];
    sh[8] = b2[4];
};
// this is the function we're trying to minimize
float WindowSH_func( float a, float b, float c, float d, float x ) 
{
    // first term accounts for ZH + |m| = 2, second terms for |m| = 1
    return ( a * x * x + b * x + c ) + ( d * x * sqrt( 1 - x * x ) );
};

// This is func' / func'' -- this was computed with Mathematica
float WindowSH_increment( float a, float b, float c, float d, float x ) 
{   
    return ( x * x - 1 ) * ( d - 2 * d * x * x + ( b + 2 * a * x ) * sqrt( 1 - x * x ) )
        / ( 3 * d * x - 2 * d * x * x * x - 2 * a * pow( max( 0, 1 - x * x ), 1.5f ) );

};
//
//
float WindowSH_SHMin( in float f[9] ) 
{
    // See "Deringing Spherical Harmonics" by Peter-Pike Sloan
    // https://www.ppsloan.org/publications/shdering.pdf

    const float M_SQRT_PI = 1.7724538509f;
    const float M_SQRT_5 = 2.2360679775f;
    const float M_SQRT_15 = 3.8729833462f;
    const float A[9] = {
                  1.0f / ( 2.0f * M_SQRT_PI ),    // 0: 0  0
            -M_SQRT_3 / ( 2.0f * M_SQRT_PI ),    // 1: 1 -1
             M_SQRT_3 / ( 2.0f * M_SQRT_PI ),    // 2: 1  0
            -M_SQRT_3 / ( 2.0f * M_SQRT_PI ),    // 3: 1  1
             M_SQRT_15 / ( 2.0f * M_SQRT_PI ),    // 4: 2 -2
            -M_SQRT_15 / ( 2.0f * M_SQRT_PI ),    // 5: 2 -1
             M_SQRT_5 / ( 4.0f * M_SQRT_PI ),    // 6: 2  0
            -M_SQRT_15 / ( 2.0f * M_SQRT_PI ),    // 7: 2  1
             M_SQRT_15 / ( 4.0f * M_SQRT_PI )     // 8: 2  2
    };

    // first this to do is to rotate the SH to align Z with the optimal linear direction
    const float3 dir = normalize( float3( -f[3], -f[1], f[2] ) );
    const float3 z_axis = -dir;
    const float3 x_axis = normalize( cross( z_axis, float3( 0, 1, 0 ) ) );
    const float3 y_axis = cross( x_axis, z_axis );
    float3x3 M = { x_axis, y_axis, -z_axis };
    M = transpose( M );

    WindowSH_RotateSh3Bands( f, M );
    // here we're guaranteed to have normalize(float3{ -f[3], -f[1], f[2] }) == { 0, 0, 1 }


    // Find the min for |m| = 2
    // ------------------------
    //
    // Peter-Pike Sloan shows that the minimum can be expressed as a function
    // of z such as:  m2min = -m2max * (1 - z^2) =  m2max * z^2 - m2max
    //      with m2max = A[8] * std::sqrt(f[8] * f[8] + f[4] * f[4]);
    // We can therefore include this in the ZH min computation (which is function of z^2 as well)
    float m2max = A[8] * sqrt( f[8] * f[8] + f[4] * f[4] );

    // Find the min of the zonal harmonics
    // -----------------------------------
    //
    // This comes from minimizing the function:
    //      ZH(z) = (A[0] * f[0])
    //            + (A[2] * f[2]) * z
    //            + (A[6] * f[6]) * (3 * s.z * s.z - 1)
    //
    // We do that by finding where it's derivative d/dz is zero:
    //      dZH(z)/dz = a * z^2 + b * z + c
    //      which is zero for z = -b / 2 * a
    //
    // We also needs to check that -1 < z < 1, otherwise the min is either in z = -1 or 1
    //
    const float a = 3 * A[6] * f[6] + m2max;
    const float b = A[2] * f[2];
    const float c = A[0] * f[0] - A[6] * f[6] - m2max;

    const float zmin = -b / ( 2.0f * a );
    const float m0min_z = a * zmin * zmin + b * zmin + c;
    const float m0min_b = min( a + b + c, a - b + c );

    const float m0min = ( a > 0 && zmin >= -1 && zmin <= 1 ) ? m0min_z : m0min_b;

    // Find the min for l = 2, |m| = 1
    // -------------------------------
    //
    // Note l = 1, |m| = 1 is guaranteed to be 0 because of the rotation step
    //
    // The function considered is:
    //        Y(x, y, z) = A[5] * f[5] * s.y * s.z
    //                   + A[7] * f[7] * s.z * s.x
    float d = A[4] * sqrt( f[5] * f[5] + f[7] * f[7] );

    // the |m|=1 function is minimal in -0.5 -- use that to skip the Newton's loop when possible
    float minimum = m0min - 0.5f * d;
    if( minimum < 0 ) 
    {
        // We could be negative, to find the minimum we will use Newton's method
        // See https://en.wikipedia.org/wiki/Newton%27s_method_in_optimization

        float dz;
        float z = float(-F_SQRT1_2);   // we start guessing at the min of |m|=1 function
        int loopCount = 0;
        do {
            minimum = WindowSH_func( a, b, c, d, z ); // evaluate our function
            dz = WindowSH_increment( a, b, c, d, z ); // refine our guess by this amount
            z = z - dz;
            // exit if z goes out of range, or if we have reached enough precision
            loopCount++;
        } while( (abs( z ) <= 1) && (abs( dz ) > 1e-5f) && (loopCount<16) );

        if( abs( z ) > 1 ) {
            // z was out of range
            minimum = min( WindowSH_func( a, b, c, d, 1 ), WindowSH_func( a, b, c, d, -1 ) );
        }
    }
    return minimum;
};
//
void WindowSH_Windowing( out float result[9], float f[9], float cutoff )
{
    for( int i = 0; i < 9; i++ )
        result[i] = f[i];
    for( int l = 0; l < (int)IBL_NUM_SH_BANDS; l++ ) 
    {
        float w = SincWindow( l, cutoff );
        result[SHindex( 0, l )] *= w;
#if VA_DIRECTX == 11 // avoid fxc compiler warning
        [unroll]
#endif
        for( uint m = 1; m <= (uint)l; m++ ) 
        {
            result[SHindex( -int(m), l )] *= w;
            result[SHindex( int(m), l )] *= w;
        }
    }
}
//
/*
 * This computes the 3-bands SH coefficients of the Cubemap convoluted by the
 * truncated cos(theta) (i.e.: saturate(s.z)), pre-scaled by the reconstruction
 * factors.
 */
void PreprocessSHForShader( inout float SH[9] )
{
    // Coefficient for the polynomial form of the SH functions -- these were taken from
    // "Stupid Spherical Harmonics (SH)" by Peter-Pike Sloan
    // They simply come for expanding the computation of each SH function.
    //
    // To render spherical harmonics we can use the polynomial form, like this:
    //          c += sh[0] * A[0];
    //          c += sh[1] * A[1] * s.y;
    //          c += sh[2] * A[2] * s.z;
    //          c += sh[3] * A[3] * s.x;
    //          c += sh[4] * A[4] * s.y * s.x;
    //          c += sh[5] * A[5] * s.y * s.z;
    //          c += sh[6] * A[6] * (3 * s.z * s.z - 1);
    //          c += sh[7] * A[7] * s.z * s.x;
    //          c += sh[8] * A[8] * (s.x * s.x - s.y * s.y);
    //
    // To save math in the shader, we pre-multiply our SH coefficient by the A[i] factors.
    // Additionally, we include the lambertian diffuse BRDF 1/pi.

    float M_SQRT_PI = 1.7724538509f;
    float M_SQRT_5  = 2.2360679775f;
    float M_SQRT_15 = 3.8729833462f;
    float A[IBL_NUM_SH_COEFFS] = {
                  1.0f / (2.0f * M_SQRT_PI),    // 0  0
            -M_SQRT_3  / (2.0f * M_SQRT_PI),    // 1 -1
             M_SQRT_3  / (2.0f * M_SQRT_PI),    // 1  0
            -M_SQRT_3  / (2.0f * M_SQRT_PI),    // 1  1
             M_SQRT_15 / (2.0f * M_SQRT_PI),    // 2 -2
            -M_SQRT_15 / (2.0f * M_SQRT_PI),    // 3 -1
             M_SQRT_5  / (4.0f * M_SQRT_PI),    // 3  0
            -M_SQRT_15 / (2.0f * M_SQRT_PI),    // 3  1
             M_SQRT_15 / (4.0f * M_SQRT_PI)     // 3  2
    };

    for (uint i = 0; i < IBL_NUM_SH_COEFFS; i++)
        SH[i] *= float(A[i] * F_1_PI);
}

groupshared float g_windowingCutoff[3];

[numthreads( 3, 1, 1 )]
void CSPostProcessSH( uint groupThreadID : SV_GroupThreadID )
{
    float cutoff = (float)IBL_NUM_SH_BANDS * 4 + 1; // start at a large band

    float scalingK[IBL_NUM_SH_COEFFS];
#if 0
    for( uint j = 0; j < IBL_NUM_SH_COEFFS; j++ )
        scalingK[j] = g_ScalingK.Load( int2(j,0) ).x;
#else
    Ki( scalingK );
    // apply truncated cos (irradiance)
    bool irradianceScaling = true;
    if( irradianceScaling ) 
    {
        for( uint l = 0; l < IBL_NUM_SH_BANDS; l++ ) {
            const float truncatedCosSh = ComputeTruncatedCosSh( l );
            scalingK[SHindex( 0, l )] *= truncatedCosSh;
#if VA_DIRECTX == 11 // avoid fxc compiler warning
            [unroll]
#endif
            for( uint m = 1; m <= l; m++ ) {
                scalingK[SHindex( -int(m), l )] *= truncatedCosSh;
                scalingK[SHindex( int(m), l )] *= truncatedCosSh;
            }
        }
    }
#endif


    // TODO: At the moment every next channel cutoff search is influenced by previous one; this probably 
    // isn't needed but I'm leaving it like that for consistency & testing.
    // Once more performance is needed, this can be simply split into 3 separate independent threads.
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // !!!! They will need to share and sync up on minimum cutoff using groupshared and groupsync  !!!!
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

    uint i;

    uint channel = groupThreadID;
    //for( uint channel = 0; channel < 3; channel++ )
    {
        // load 1 channel of 3-band SH (processing each channel separately)
        // THIS ONLY WORKS WITH 3-band SH at the time; it could work with 1 and 2 but this wasn't tested
        float SH[9];
        for( i = 0; i < IBL_NUM_SH_COEFFS; i++ ) 
            SH[i] = asfloat(g_SH_UAV[i*3+channel]) * scalingK[i];

        // Windowing (see WindowSH on the .cpp side)
    
        // find a cut-off band that works
        float l = (float)IBL_NUM_SH_BANDS;
        float r = cutoff;
        for( i = 0; i < 16 && l + 0.1f < r; i++ ) 
        {
            float m = 0.5f * ( l + r );
            float SHTemp[9];
            WindowSH_Windowing( SHTemp, SH, m );

            if( WindowSH_SHMin( SHTemp ) < 0 ) 
            {
                r = m;
            }
            else 
            {
                l = m;
            }
        }
        cutoff = min( cutoff, l );
    }

    g_windowingCutoff[channel] = cutoff;

    GroupMemoryBarrierWithGroupSync( );

    cutoff = min( min( g_windowingCutoff[0], g_windowingCutoff[1] ), g_windowingCutoff[2] );

    //for( channel = 0; channel < 3; channel++ )
    {
        // all this can go away when we switch to 3 (mostly)independent channels
        float SH[9];
        for( i = 0; i < IBL_NUM_SH_COEFFS; i++ ) 
            SH[i] = asfloat(g_SH_UAV[i*3+channel]) * scalingK[i];

        ////////////////////////////////////////////////////////////////////////////////////
        // just for debugging
        // cutoff = 11.8445425;
        // just for debugging
        ////////////////////////////////////////////////////////////////////////////////////
        
        // apply windowing cutoff
        for( uint l = 0; l < (uint)IBL_NUM_SH_BANDS; l++ ) 
        {
            float w = SincWindow( l, cutoff );
            SH[SHindex( 0, l )] *= w;
#if VA_DIRECTX == 11 // avoid fxc compiler warning
            [unroll]
#endif
            for( uint m = 1; m <= l; m++ ) 
            {
                SH[SHindex( -int(m), l )] *= w;
                SH[SHindex( int(m), l )] *= w;
            }
        }
   
        PreprocessSHForShader( SH );

        // save 1 channel of 3-band SH
        for( i = 0; i < IBL_NUM_SH_COEFFS; i++ ) 
            g_SH_UAV[i*3+channel] = asuint(SH[i]);
    }
}

#endif // #ifdef IBL_NUM_SH_COEFFS

#ifdef IBL_ROUGHNESS_PREFILTER_NUM_SAMPLES

[numthreads( IBL_ROUGHNESS_PREFILTER_THREADGROUPSIZE, IBL_ROUGHNESS_PREFILTER_THREADGROUPSIZE, 1 )]
void CSCubePreFilter( uint3 groupThreadID : SV_GroupThreadID, uint3 dispatchThreadID : SV_DispatchThreadID )
{
    float3 cubeDims;
    g_CubeFacesRW.GetDimensions( cubeDims.x, cubeDims.y, cubeDims.z );

    // compute direction
    float3 cubeDir = CubemapGetDirectionFor( cubeDims.x, dispatchThreadID.z, dispatchThreadID.x, dispatchThreadID.y );

#if (IBL_ROUGHNESS_PREFILTER_NUM_SAMPLES == 0)
    // basically just copy the cube - also tests the above logic correctness
    g_CubeFacesRW[ dispatchThreadID.xyz ] = g_Cubemap.SampleLevel( g_samplerLinearWrap, cubeDir, 0 ).xyz;
#else
    float3 color = {0,0,0};

    // center the cone around the normal (handle case of normal close to up)
    const float3 up = abs(cubeDir.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3x3 R;
    R[0] = normalize(cross(up, cubeDir));
    R[1] = cross(cubeDir, R[0]);
    R[2] = cubeDir;

    for( uint i = 0; i < IBL_ROUGHNESS_PREFILTER_NUM_SAMPLES; i++ )
    {
        float4 sampleInfo = g_GenericTexture0.Load( int3( i % 8192, i / 8192, 0 ) );

        float3 L = { sampleInfo.x, sampleInfo.y, sqrt( clamp( 1 - sampleInfo.x * sampleInfo.x - sampleInfo.y * sampleInfo.y, 0.0f, 1.0f ) ) };
        float weight    = sampleInfo.z;
        float mipLevel  = sampleInfo.w;

        float3 sampleColor = g_Cubemap.SampleLevel( g_samplerLinearWrap, mul( L, R ), mipLevel ).xyz;

        color += sampleColor * weight;
    }
    g_CubeFacesRW[ dispatchThreadID.xyz ] = color; //float3( cubeDir * 0.5 + 0.5 );
#endif
}

#endif // #ifdef IBL_NUM_SAMPLES