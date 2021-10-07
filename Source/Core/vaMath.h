///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "vaCoreTypes.h"

#include <math.h>
#include <cmath>
#include <limits>

#ifndef VA_PI
#define VA_PI               	(3.1415926535897932384626433832795)
#endif
#ifndef VA_PIf
#define VA_PIf              	(3.1415926535897932384626433832795f)
#endif

// legacy - these should go out and be replaced by std equivalents
#ifndef VA_EPSf
#define VA_EPSf             	    ( std::numeric_limits<float>::epsilon() * 2.0f )
#define VA_EPSd             	    ( std::numeric_limits<double>::epsilon() * 2.0f )
#endif

#define VA_FLOAT_HIGHEST    	    ( std::numeric_limits<float>::max() )
#define VA_FLOAT_LOWEST     	    ( std::numeric_limits<float>::lowest() )
#define VA_FLOAT_MIN_POSITIVE 	    ( std::numeric_limits<float>::min() )
#define VA_FLOAT_ONE_MINUS_EPSILON  (0x1.fffffep-1)                                         // biggest float smaller than 1 (see OneMinusEpsilon in pbrt book!)

#define VA_DOUBLE_HIGHEST   	    ( std::numeric_limits<double>::max() )
#define VA_DOUBLE_LOWEST    	    ( std::numeric_limits<double>::lowest() )
#define VA_DOUBLE_MIN_POSITIVE 	    ( std::numeric_limits<double>::min() )
#define VA_DOUBLE_ONE_MINUS_EPSILON (0x1.fffffffffffffp-1)                                  // biggest double smaller than 1 (see OneMinusEpsilon in pbrt book!)


namespace Vanilla
{
    // TODO: most of this is no longer needed as it's part of std::xxx, so clean up the codebase and remove it
    // (leave the stuff that makes sense to stay in for any kind of framework-wide optimization/profiling/tracking purposes)
    // TODO: see https://www.pbr-book.org/3ed-2018/Utilities/Main_Include_File and pick up naming convention (with attribution ofc) as it's much better
    // TODO: also see https://www.pbr-book.org/3ed-2018/Utilities/Mathematical_Routines 
    class vaMath
    {
    public:

        static inline float         TimeIndependentLerpF( float deltaTime, float lerpRate );

        template<class T>
        static inline T             Min( T const& a, T const& b );

        template<class T>
        static inline T             Min( T const& a, T const& b, T const& c );

        template<class T>
        static inline T             Min( T const& a, T const& b, T const& c, T const& d );

        template<class T>
        static inline T             Max( T const& a, T const& b );

        template<class T>
        static inline T             Max( T const& a, T const& b, T const& c );

        template<class T>
        static inline T             Max( T const& a, T const& b, T const& c, T const& d );

        template<class T>
        static inline T             Clamp( T const& v, T const& min, T const& max );

        template<class T>
        static inline T             Saturate( T const& a );

        template<class T>
        static inline T             Lerp( T const& a, T const& b, const float f );

        // why not just use std::swap?
        //template<class T>
        //static inline void      Swap(T & a, T & b);

        static inline int           Abs( int a );
        static inline float         Abs( float a );
        static inline double        Abs( double a );

        static inline double        Frac( double a );
        static inline float         Frac( float a );

        // integer Log2
        static constexpr uint32     FloorLog2( uint32 n )                   { assert( n > 0 ); return n == 1 ? 0 : 1 + FloorLog2( n >> 1 ); }
        static constexpr uint32     CeilLog2( uint32 n )                    { assert( n > 0 ); return n == 1 ? 0 : FloorLog2( n - 1 ) + 1; }

        // round 'size' up to 'alignment'
        static constexpr uint64     Align( uint64 size, uint64 alignment )  { assert( alignment > 0 ); return ( size + ( alignment - 1 ) ) & ~( alignment - 1 ); }
        static constexpr uint32     Align( uint32 size, uint32 alignment )  { assert( alignment > 0 ); return ( size + ( alignment - 1 ) ) & ~( alignment - 1 ); }

        static inline float         WrapMax( float x, float max );
        static inline float         WrapMinMax( float x, float min, float max );

        static inline float         AngleWrap( float angle );
        static inline float         AngleSmallestDiff( float a, float b );

        static inline float         DegreeToRadian( float degree ) { return degree * VA_PIf / 180.0f; }
        static inline float         RadianToDegree( float radian ) { return radian * 180.0f / VA_PIf; }

        static inline bool          IsPowOf2( int32 val );
        static inline bool          IsPowOf2( uint32 val );
        // a.k.a smallest power of 2 that is >= val
        static inline int           PowOf2Ceil( int val );
        static inline int           Log2( int val );

        static inline float         Log2( float val );

        template<class T>
        static inline T             Sq( const T& a );

        static inline float         Sqrt( float a ) { return ::sqrtf( a ); }
        static inline double        Sqrt( double a ) { return ::sqrt( a ); }

        static inline float         Pow( float a, float p ) { return powf( a, p ); }
        static inline double        Pow( double a, double p ) { return pow( a, p ); }

        static inline float         Exp( float p ) { return expf( p ); }
        static inline double        Exp( double p ) { return exp( p ); }

        static inline float         Sin( float a ) { return sinf( a ); }
        static inline double        Sin( double a ) { return sin( a ); }

        static inline float         Cos( float a ) { return cosf( a ); }
        static inline double        Cos( double a ) { return cos( a ); }

        static inline float         ASin( float a ) { return asinf( a ); }
        static inline double        ASin( double a ) { return asin( a ); }

        static inline float         ACos( float a ) { return acosf( a ); }
        static inline double        ACos( double a ) { return acos( a ); }

        static inline float         ATan2( float y, float x ) { return atan2f( y, x ); }

        static inline float         Round( float x ) { return ::roundf( x ); }
        static inline double        Round( double x ) { return ::round( x ); }

        static inline float         Ceil( float x ) { return ::ceilf( x ); }
        static inline double        Ceil( double x ) { return ::ceil( x ); }

        static inline float         Floor( float x ) { return ::floorf( x ); }
        static inline double        Floor( double x ) { return ::floor( x ); }

        template<class T>
        static inline T             Sign( const T& a );

        // not really ideal, for better see https://bitbashing.io/comparing-floats.html / http://floating-point-gui.de/references/
        static inline bool          NearEqual( float a, float b, float epsilon = VA_EPSf ) { return vaMath::Abs( a - b ) < epsilon; }

        static inline float         Smoothstep( const float t );                                                                        // gives something similar to "sin( (x-0.5) * PI)*0.5 + 0.5 )" for [0, 1]

        static inline float         PSNR( float MSE, float maxI ) { return 10.0f * ::log10f( maxI / MSE ); }
        static inline double        PSNR( double MSE, double maxI ) { return 10.0 * ::log10( maxI / MSE ); }

        static inline float         SampleVariance( float samples[], int sampleCount );

        // these mirrors the shader code
        static inline uint          Hash32( uint x );
        static inline float         Hash32ToFloat(uint hash)                            {  return hash * (1.0f / 4294967296.0f); }
        static inline float         Hash32NextFloatAndAdvance( uint & hash );
        static inline uint32        Hash32Combine( uint32 seed, const uint32 value );
    };

    // // From http://guihaire.com/code/?p=1135
    // static inline float VeryApproxLog2f( float x )
    // {
    //     union { float f; uint32_t i; } ux;
    //     ux.f = x;
    //     return (float)ux.i * 1.1920928955078125e-7f - 126.94269504f;
    // }

    //
    // Basic implementation of a 2D value noise (http://www.scratchapixel.com/lessons/3d-advanced-lessons/noise-part-1/creating-a-simple-2d-noise/)
    //
    class vaSimple2DNoiseA
    {
    public:
        vaSimple2DNoiseA( )
        {
            m_r = NULL;
            m_kMaxVertices = 256;
            m_kMaxVerticesMask = m_kMaxVertices - 1;
        }

        ~vaSimple2DNoiseA( )
        {
            delete[] m_r;
        }

        void Initialize( int seed = 0 );

        void Destroy( );

        /// Evaluate the noise function at position x
        float Eval( const class vaVector2& pt ) const;

    private:
        unsigned int        m_kMaxVertices;
        unsigned int        m_kMaxVerticesMask;
        float* m_r;
    };


    template< class ElementType, int cNumberOfElements >
    struct vaEquidistantSampleLinearGraph
    {
        ElementType     Elements[cNumberOfElements];

        void            SetAll( ElementType val ) { for( int i = 0; i < cNumberOfElements; i++ ) Elements[i] = val; }
        ElementType     Eval( float pos )
        {
            float   posFlt = vaMath::Clamp( pos * ( cNumberOfElements - 1 ), 0.0f, (float)cNumberOfElements - 1.0f );
            int     posIndex = vaMath::Clamp( (int)posFlt, 0, cNumberOfElements - 2 );
            float   posFrac = posFlt - (float)posIndex;

            return vaMath::Lerp( Elements[posIndex], Elements[posIndex + 1], posFrac );
        }
    };


    // Time independent lerp function. The bigger the lerpRate, the faster the lerp! (based on continuously compounded interest rate I think)
    inline float vaMath::TimeIndependentLerpF( float deltaTime, float lerpRate )
    {
        return 1.0f - expf( -fabsf( deltaTime * lerpRate ) );
    }

    template<class T>
    inline T vaMath::Min( T const& a, T const& b )
    {
        return ( a < b ) ? ( a ) : ( b );
    }

    template<class T>
    inline T vaMath::Min( T const& a, T const& b, T const& c )
    {
        return Min( Min( a, b ), c );
    }

    template<class T>
    inline T vaMath::Min( T const& a, T const& b, T const& c, T const& d )
    {
        return Min( Min( a, b ), Min( c, d ) );
    }

    template<class T>
    inline T vaMath::Max( T const& a, T const& b )
    {
        return ( a > b ) ? ( a ) : ( b );
    }

    template<class T>
    inline T vaMath::Max( T const& a, T const& b, T const& c )
    {
        return Max( Max( a, b ), c );
    }

    template<class T>
    inline T vaMath::Max( T const& a, T const& b, T const& c, T const& d )
    {
        return Max( Max( a, b ), Max( c, d ) );
    }

    template<class T>
    inline T vaMath::Clamp( T const& v, T const& min, T const& max )
    {
        assert( max >= min );
        if( v < min ) return min;
        if( v > max ) return max;
        return v;
    }

    // short for clamp( a, 0, 1 )
    template<>
    inline float vaMath::Saturate( float const & a )
    {
        assert( !std::isnan(a) );
        return Clamp( a, 0.0f, 1.0f );
    }

    // short for clamp( a, 0, 1 )
    template<class T>
    inline T vaMath::Saturate( T const& a )
    {
        return Clamp( a, (T)0.0, (T)1.0 );
    }

    template<class T>
    inline T vaMath::Lerp( T const& a, T const& b, const float f )
    {
        return a + ( b - a ) * f;
    }

    // why not just use std::swap?
    //template<class T>
    //inline void vaMath::Swap(T & a, T & b)
    //{
    //    assert( &a != &b ); // swapping with itself? 
    //	T temp = b;
    //	b = a;
    //	a = temp;
    //}

    template<class T>
    inline T vaMath::Sq( const T& a )
    {
        return a * a;
    }

    inline double vaMath::Frac( double a )
    {
        return fmod( a, 1.0 );
    }

    inline float vaMath::Frac( float a )
    {
        return fmodf( a, 1.0 );
    }

    //////////////////////////////////////////////////////////////////////////
    // from https://stackoverflow.com/a/29871193/335646
    inline float vaMath::WrapMax( float x, float max )
    {
        return fmodf( max + fmodf( x, max ), max );
    }
    //
    inline float vaMath::WrapMinMax( float x, float min, float max )
    {
        return min + WrapMax( x - min, max - min );
    }
    //////////////////////////////////////////////////////////////////////////

    // check out the above and replace if better
    inline float vaMath::AngleWrap( float angle )
    {
        return ( angle > 0 ) ? ( fmodf( angle + VA_PIf, VA_PIf * 2.0f ) - VA_PIf ) : ( fmodf( angle - VA_PIf, VA_PIf * 2.0f ) + VA_PIf );
    }

    inline float vaMath::AngleSmallestDiff( float a, float b )
    {
        assert( false ); // not sure this is correct
        a = AngleWrap( a );
        b = AngleWrap( b );
        float v = AngleWrap( a - b );
        if( v > VA_PIf )
            v -= VA_PIf * 2.0f;
        return v;
    }

    inline bool vaMath::IsPowOf2( int32 val )
    {
        if( val < 1 ) return false;
        return ( val & ( val - 1 ) ) == 0;
    }

    inline bool vaMath::IsPowOf2( uint32 val )
    {
        return ( val & ( val - 1 ) ) == 0;
    }

    inline int vaMath::PowOf2Ceil( int val )
    {
        int l2 = Log2( Max( 0, val - 1 ) ) + 1;
        return 1 << l2;
    }

    inline int vaMath::Log2( int val )
    {
        unsigned r = 0;

        while( val >>= 1 )
        {
            r++;
        }

        return r;
    }

    inline float vaMath::Log2( float val )
    {
        return log2f( val );
    }

    inline int vaMath::Abs( int a )
    {
        return abs( a );
    }

    inline float vaMath::Abs( float a )
    {
        return fabsf( a );
    }

    inline double vaMath::Abs( double a )
    {
        return fabs( a );
    }

    template<class T>
    inline T vaMath::Sign( const T& a )
    {
        if( a > 0 ) return 1;
        if( a < 0 ) return -1;
        return 0;
    }

    // "fat" version:
    // float smoothstep(float edge0, float edge1, float x)
    // {
    //	// Scale, bias and saturate x to 0..1 range
    //	x = clamp((x - edge0)/(edge1 - edge0), 0.0, 1.0); 
    //  // Evaluate polynomial
    //	return x*x*(3 - 2*x);
    //}

    inline float vaMath::Smoothstep( const float t )
    {
        return t * t * ( 3 - 2 * t );
    }

    inline float vaMath::SampleVariance( float samples[], int n )
    {
        float sum = 0, sum_sq = 0;
        for (int i = 0; i < n; ++i) {
            sum += samples[i];
            sum_sq += samples[i] * samples[i];
        }
        //return sum_sq / (n - 1) - sum * sum / ((n - 1) * n);
        return (sum_sq - sum * sum / n) / (n - 1);
    }

    // This little gem is from https://nullprogram.com/blog/2018/07/31/, "Prospecting for Hash Functions" by Chris Wellons
    // There's also the inverse for the lowbias32, and a 64bit version.
    inline uint vaMath::Hash32( uint x )
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

    inline float vaMath::Hash32NextFloatAndAdvance( uint & hash )
    {
        float rand = Hash32ToFloat( hash );
        hash = Hash32( hash );
        return rand;
    }
    // float2 Hash32NextFloat2AndAdvance( inout uint hash )
    // {   // yes yes could be 1-liner but I'm not 100% certain ordering is same across all compilers
    //     float rand1 = Hash32NextFloatAndAdvance( hash );
    //     float rand2 = Hash32NextFloatAndAdvance( hash );
    //     return float2( rand1, rand2 );
    // }
    //
    // popular hash_combine (boost, etc.)
    inline uint32 vaMath::Hash32Combine( uint32 seed, const uint32 value )
    {
        return seed ^ (Hash32(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
    }
}