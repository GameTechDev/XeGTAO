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

#include "vaCore.h"

#include "vaMath.h"

#include "vaRandom.h"

#include <algorithm>

// vaGeometry is designed to match Microsoft's D3DX (old, DX9-gen) vector & matrix library as much as
// possible in functionality and interface, in order to simplify porting of my existing DX9-based codebase.
// However not all the functionality is the same and not all of it is tested so beware. Also, it is
// designed with simplicity and readability as a goal, therefore sometimes sacrificing performance.
//
// to fill in missing stuff consult http://doxygen.reactos.org/de/d57/dll_2directx_2wine_2d3dx9__36_2math_8c.html
// (ReactOS, Wine)
//
// ! MATRIX LAYOUT
// vaMatrix4x4 and vaMatrix3x3 have row-major layout in memory and store translation in the 4th row (vector
// multiplication is done using a row vector on the left).
// ! MATRIX LAYOUT
//
// ! COORDINATE SYSTEM !
// Vanilla uses a left-handed coordinate system in which 
//    FRONT (forward) is    +X, 
//    RIGHT is              +Y,
//    UP    is              +Z
// This is different from some functions in DirectX (that assume +Y is up, +Z is forward, +X right). So far, 
// this is only relevant for functions such as various RollPitchYaw, etc., which should be made more generic anyway.
 // ! COORDINATE SYSTEM !
//
// A number of functions are based on ideas from http://clb.demon.fi/MathGeoLib

namespace Vanilla
{
    class vaVector4;
    class vaMatrix4x4;
    class vaMatrix3x3;

    enum class vaIntersectType : int32
    {
       Outside,
       Intersect,
       Inside
    };

    enum class vaSortType : int32
    {
        None,
        BackToFront,
        FrontToBack
    };

    enum class vaWindingOrder : int32
    {
        Undefined           = 0,            // todo: remove this, must have winding?
        Clockwise           = 1,
        CounterClockwise    = 2
    };

    enum class vaFaceCull : int32
    {
        None,
        Front,
        Back
    };

    struct vaViewport;

    class vaVector2
    {
    public:
        float x, y;

    public:
        static vaVector2    Zero;

    public:
        vaVector2( ) { };
        explicit vaVector2( const float * p ) { assert( p != NULL ); x = p[0]; y = p[1]; }
        vaVector2( float x, float y ) : x( x ), y( y ) { }
        explicit vaVector2( const class vaVector2i & );

        // assignment operators
        vaVector2 &   operator += ( const vaVector2 & );
        vaVector2 &   operator -= ( const vaVector2 & );
        vaVector2 &   operator *= ( float );
        vaVector2 &   operator /= ( float );

        // unary operators
        vaVector2     operator + ( ) const;
        vaVector2     operator - ( ) const;

        // binary operators
        vaVector2     operator + ( const vaVector2 & ) const;
        vaVector2     operator - ( const vaVector2 & ) const;
        vaVector2     operator * ( float ) const;
        vaVector2     operator / ( float ) const;

        friend vaVector2 operator * ( float, const class vaVector2 & );

        bool operator == ( const vaVector2 & ) const;
        bool operator != ( const vaVector2 & ) const;

        float                   Length( ) const;
        float                   LengthSq( ) const;
        vaVector2               Normalized( ) const;
        vaVector2               ComponentAbs( ) const;

    public:
        static float            Dot( const vaVector2 & a, const vaVector2 & b );
        static float            Cross( const vaVector2 & a, const vaVector2 & b );

        // not really ideal, for better see https://bitbashing.io/comparing-floats.html / http://floating-point-gui.de/references/
        static bool             NearEqual( const vaVector2 & a, const vaVector2 & b, float epsilon = VA_EPSf );

        static vaVector2        ComponentMul( const vaVector2 & a, const vaVector2 & b );
        static vaVector2        ComponentDiv( const vaVector2 & a, const vaVector2 & b );
        static vaVector2        ComponentMin( const vaVector2 & a, const vaVector2 & b );
        static vaVector2        ComponentMax( const vaVector2 & a, const vaVector2 & b );

        static vaVector2        BaryCentric( const vaVector2 & v1, const vaVector2 & v2, const vaVector2 & v3, float f, float g );

        // Hermite interpolation between position v1, tangent t1 (when s == 0) and position v2, tangent t2 (when s == 1).
        static vaVector2        Hermite( const vaVector2 & v1, const vaVector2 & t1, const vaVector2 & v2, const vaVector2 & t2, float s );

        // CatmullRom interpolation between v1 (when s == 0) and v2 (when s == 1)
        static vaVector2        CatmullRom( const vaVector2 & v0, const vaVector2 & v1, const vaVector2 & v2, const vaVector2 & v3, float s );

        // Transform (x, y, 0, 1) by matrix.
        static vaVector4        Transform( const vaVector2 & v, const vaMatrix4x4 & mat );

        // Transform (x, y, 0, 1) by matrix, project result back into w=1.
        static vaVector2        TransformCoord( const vaVector2 & v, const vaMatrix4x4 & mat );

        // Transform (x, y, 0, 0) by matrix.
        static vaVector2        TransformNormal( const vaVector2 & v, const vaMatrix4x4 & mat );

        // Random point on circle with radius 1.0
        static vaVector2        RandomPointOnCircle( vaRandom & randomGeneratorToUse = vaRandom::Singleton );

        // Random point on or within a circle of radius 1.0 (on the disk)
        static vaVector2        RandomPointOnDisk( vaRandom & randomGeneratorToUse = vaRandom::Singleton );

        static vaVector2        Clamp( const vaVector2 & v, const vaVector2 & vmin, const vaVector2 & vmax )            { return { vaMath::Clamp( v.x, vmin.x, vmax.x ), vaMath::Clamp( v.y, vmin.x, vmax.x ) }; }
    };



    //--------------------------
    // 3D Vector
    //--------------------------
    class vaVector3
    {
    public:
        float x, y, z;

    public:
        static vaVector3    Zero;

    public:
        vaVector3( ) { };
        explicit vaVector3( const float * p ) { assert( p != NULL ); x = p[0]; y = p[1]; z = p[2]; }
        vaVector3( float x, float y, float z ) : x( x ), y( y ), z( z ) { }
        vaVector3( const vaVector2 & a, float z ) : x( a.x ), y( a.y ), z( z ) { }

        const float & operator [] ( int i ) const       { assert( i >= 0 && i < 3 ); return (&x)[i]; }
        float & operator [] ( int i )                   { assert( i >= 0 && i < 3 ); return (&x)[i]; }

        // assignment operators
        vaVector3& operator += ( const vaVector3 & );
        vaVector3& operator -= ( const vaVector3 & );
        vaVector3& operator *= ( float );
        vaVector3& operator /= ( float );

        // unary operators
        vaVector3 operator + ( ) const;
        vaVector3 operator - ( ) const;

        // binary operators
        vaVector3 operator + ( const vaVector3 & ) const;
        vaVector3 operator - ( const vaVector3 & ) const;
        vaVector3 operator * ( const vaVector3 & ) const;
        vaVector3 operator / ( const vaVector3 & ) const;

        vaVector3 operator * ( float ) const;
        vaVector3 operator / ( float ) const;
        vaVector3 operator + ( float ) const;
        vaVector3 operator - ( float ) const;

        friend vaVector3 operator * ( float, const class vaVector3 & );

        bool operator == ( const vaVector3 & ) const;
        bool operator != ( const vaVector3 & ) const;

        float                   Length( ) const;
        float                   LengthSq( ) const;
        vaVector3               Normalized( ) const;
        vaVector3               ComponentAbs( ) const;
        bool                    IsUnit( float epsilon = VA_EPSf ) const;

        vaVector2 &             AsVec2( ) { return *( (vaVector2*)( &x ) ); }
        const vaVector2 &       AsVec2( ) const { return *( (vaVector2*)( &x ) ); }

    public:
        static float            Dot( const vaVector3 & a, const vaVector3 & b );
        static vaVector3        Cross( const vaVector3 & a, const vaVector3 & b );
        static vaVector3        Normalize( const vaVector3 & a )                        { return a.Normalized(); }

        // not really ideal, for better see https://bitbashing.io/comparing-floats.html / http://floating-point-gui.de/references/
        static bool             NearEqual( const vaVector3 & a, const vaVector3 & b, float epsilon = VA_EPSf );

        static vaVector3        ComponentMul( const vaVector3 & a, const vaVector3 & b );
        static vaVector3        ComponentDiv( const vaVector3 & a, const vaVector3 & b );
        static vaVector3        ComponentMin( const vaVector3 & a, const vaVector3 & b );
        static vaVector3        ComponentMax( const vaVector3 & a, const vaVector3 & b );
        static vaVector3        Saturate( const vaVector3 & a );

        static vaVector3        BaryCentric( const vaVector3 & v1, const vaVector3 & v2, const vaVector3 & v3, float f, float g );

        static vaVector3        TriangleNormal( const vaVector3 & a, const vaVector3 & b, const vaVector3 & c, bool counterClockwise = true );

        // Linear interpolation
        static vaVector3        Lerp( const vaVector3 & v1, const vaVector3 & v2, float s );

        // Hermite interpolation between position v1, tangent t1 (when s == 0) and position v2, tangent t2 (when s == 1).
        static vaVector3        Hermite( const vaVector3 & v1, const vaVector3 & t1, const vaVector3 & v2, const vaVector3 &t2, float s );

        // CatmullRom interpolation between v1 (when s == 0) and v2 (when s == 1)
        static vaVector3        CatmullRom( const vaVector3 &v0, const vaVector3 &v1, const vaVector3 & v2, const vaVector3 & v3, float s );

        // Transform (x, y, z, 1) by matrix.
        static vaVector4        Transform( const vaVector3 & v, const vaMatrix4x4 & mat );

        // Transform (x, y, z, 1) by matrix, project result back into w=1.
        static vaVector3        TransformCoord( const vaVector3 & v, const vaMatrix4x4 & mat );

        // Transform (x, y, z, 0) by matrix.  If you are transforming a normal by a 
        // non-affine matrix, the matrix you pass to this function should be the 
        // transpose of the inverse of the matrix you would use to transform a coord.
        static vaVector3        TransformNormal( const vaVector3 & v, const vaMatrix4x4 & mat );

        // Same as above, just with a 3x3 matrix.
        static vaVector3        TransformNormal( const vaVector3 & v, const vaMatrix3x3 & mat );

        static vaVector3        Random( vaRandom & randomGeneratorToUse = vaRandom::Singleton );

        static vaVector3        RandomNormal( vaRandom & randomGeneratorToUse = vaRandom::Singleton );
        static vaVector3        RandomPointOnSphere( vaRandom & randomGeneratorToUse = vaRandom::Singleton ) { return RandomNormal( randomGeneratorToUse ); }

        static float            AngleBetweenVectors( const vaVector3 & a, const vaVector3 & b );

        // Project vector from object space into screen space
        static vaVector3        Project( const vaVector3 & v, const vaViewport & viewport,
            const vaMatrix4x4 & projection, const vaMatrix4x4 & view, const vaMatrix4x4 & world );

        // Project vector from screen space into object space
        static vaVector3        Unproject( const vaVector3 & v, const vaViewport & viewport,
            const vaMatrix4x4 & projection, const vaMatrix4x4 & view, const vaMatrix4x4 & world );

        static vaVector3        LinearToSRGB( const vaVector3 & colour );
        static vaVector3        SRGBToLinear( const vaVector3 & colour );
        
        static vaVector3        DegreeToRadian( const vaVector3 & degree )      { return { vaMath::DegreeToRadian(degree.x), vaMath::DegreeToRadian(degree.y), vaMath::DegreeToRadian(degree.z) }; }
        static vaVector3        RadianToDegree( const vaVector3 & radian )      { return { vaMath::RadianToDegree(radian.x), vaMath::RadianToDegree(radian.y), vaMath::RadianToDegree(radian.z) }; }

        static string           ToString( const vaVector3 & a );
        static bool             FromString( const string & a, vaVector3 & outVal );

        static void             ComputeOrthonormalBasis( const vaVector3 & n, vaVector3 & b1, vaVector3 & b2 );

        static vaVector3        Clamp( const vaVector3 & v, const vaVector3 & vmin, const vaVector3 & vmax )            { return { vaMath::Clamp( v.x, vmin.x, vmax.x ), vaMath::Clamp( v.y, vmin.x, vmax.x ), vaMath::Clamp( v.z, vmin.z, vmax.z ) }; }
    };


    class vaVector4
    {
    public:
        float x, y, z, w;

    public:
        static vaVector4    Zero;

    public:
        vaVector4( ) { };
        explicit vaVector4( const float * p ) { assert( p != NULL ); x = p[0]; y = p[1]; z = p[2]; w = p[3]; }
        vaVector4( float x, float y, float z, float w ) : x( x ), y( y ), z( z ), w( w ) { };
        vaVector4( const vaVector3 & v, float w ) : x( v.x ), y( v.y ), z( v.z ), w( w ) { };
        vaVector4( const vaVector2 & xy, const vaVector2 & zw ) : x( xy.x ), y( xy.y ), z( zw.x ), w( zw.y ) { };
        vaVector4( const vaVector2 & xy, float z, float w ) : x( xy.x ), y( xy.y ), z( z ), w( w ) { };
        explicit vaVector4( const class vaVector4i & );

        // assignment operators
        vaVector4 & operator += ( const vaVector4 & );
        vaVector4 & operator -= ( const vaVector4 & );
        vaVector4 & operator *= ( float );
        vaVector4 & operator /= ( float );

        // unary operators
        vaVector4 operator + ( ) const;
        vaVector4 operator - ( ) const;

        // binary operators
        vaVector4 operator + ( const vaVector4 & ) const;
        vaVector4 operator - ( const vaVector4 & ) const;
        vaVector4 operator * ( const vaVector4 & ) const;
        vaVector4 operator / ( const vaVector4 & ) const;
        vaVector4 operator + ( float ) const;
        vaVector4 operator - ( float ) const;
        vaVector4 operator * ( float ) const;
        vaVector4 operator / ( float ) const;

        friend vaVector4 operator * ( float, const vaVector4 & );

        bool operator == ( const vaVector4 & ) const;
        bool operator != ( const vaVector4 & ) const;

        float                   Length( ) const;
        float                   LengthSq( ) const;
        vaVector4               Normalized( ) const;

        vaVector3 &             AsVec3( ) { return *( (vaVector3*)( &x ) ); }
        const vaVector3 &       AsVec3( ) const { return *( (vaVector3*)( &x ) ); }

        vaVector2 &             AsVec2( ) { return *( (vaVector2*)( &x ) ); }
        const vaVector2 &       AsVec2( ) const { return *( (vaVector2*)( &x ) ); }

        uint32                  ToBGRA( )                                       { return vaVector4::ToBGRA( *this ); }
        uint32                  ToRGBA( )                                       { return vaVector4::ToRGBA( *this ); }
        uint32                  ToABGR( )                                       { return vaVector4::ToABGR( *this ); }

    public:
        static float            Dot( const vaVector4 & a, const vaVector4 & b );
        static vaVector4        Cross( const vaVector4 & a, const vaVector4 & b, const vaVector4 & c );

        static vaVector4        ComponentMul( const vaVector4 & a, const vaVector4 & b );
        static vaVector4        ComponentDiv( const vaVector4 & a, const vaVector4 & b );
        static vaVector4        ComponentMin( const vaVector4 & a, const vaVector4 & b );
        static vaVector4        ComponentMax( const vaVector4 & a, const vaVector4 & b );
        static vaVector4        Saturate( const vaVector4 & a );

        static vaVector4        BaryCentric( const vaVector4 & v1, const vaVector4 & v2, const vaVector4 & v3, float f, float g );

        static vaVector4        Random( vaRandom & randomGeneratorToUse = vaRandom::Singleton );

        // Hermite interpolation between position v1, tangent t1 (when s == 0) and position v2, tangent t2 (when s == 1).
        static vaVector4        Hermite( const vaVector4 & v1, const vaVector4 &t1, const vaVector4 &v2, const vaVector4 &t2, float s );

        // CatmullRom interpolation between V1 (when s == 0) and V2 (when s == 1)
        static vaVector4        CatmullRom( const vaVector4 & v0, const vaVector4 & v1, const vaVector4 & v2, const vaVector4 & v3, float s );

        // Transform vector by matrix.
        static vaVector4        Transform( const vaVector4 & v, const vaMatrix4x4 & mat );

        static vaVector4        FromBGRA( uint32 colour );
        static vaVector4        FromRGBA( uint32 colour );
        static vaVector4        FromABGR( uint32 colour );

        static uint32           ToBGRA( const vaVector4 & colour );
        static uint32           ToRGBA( const vaVector4 & colour );
        static uint32           ToABGR( const vaVector4 & colour );

        inline static uint32    ToBGRA( float r, float g, float b, float a )    { return ToBGRA( vaVector4( r, g, b, a ) ); }
        inline static uint32    ToRGBA( float r, float g, float b, float a )    { return ToRGBA( vaVector4( r, g, b, a ) ); }
        inline static uint32    ToABGR( float r, float g, float b, float a )    { return ToABGR( vaVector4( r, g, b, a ) ); }

        static vaVector4        LinearToSRGB( const vaVector4 & colour );
        static vaVector4        SRGBToLinear( const vaVector4 & colour );

        static string           ToString( const vaVector4 & a );
        static bool             FromString( const string & a, vaVector4 & outVal );

        static vaVector4        Clamp( const vaVector4 & v, const vaVector4 & vmin, const vaVector4 & vmax )            { return { vaMath::Clamp( v.x, vmin.x, vmax.x ), vaMath::Clamp( v.y, vmin.x, vmax.x ), vaMath::Clamp( v.z, vmin.z, vmax.z ), vaMath::Clamp( v.w, vmin.w, vmax.w ) }; }
    };

    class vaVector4d
    {
    public:
        double x, y, z, w;

        static vaVector4d       Cross( const vaVector4d & a, const vaVector4d & b, const vaVector4d & c );
    };

    class vaQuaternion
    {
    public:
        float x, y, z, w;

    public:
        static vaQuaternion        Identity;

    public:
        vaQuaternion( ) { }
        explicit vaQuaternion( const float * p ) { assert( p != NULL ); x = p[0]; y = p[1]; z = p[2]; w = p[3]; }
        vaQuaternion( float x, float y, float z, float w ) : x( x ), y( y ), z( z ), w( w ) { }
        explicit vaQuaternion( const vaVector4 & v ) : x( v.x ), y( v.y ), z( v.z ), w( v.w ) { }

        // assignment
        vaQuaternion & operator += ( const vaQuaternion & );
        vaQuaternion & operator -= ( const vaQuaternion & );
        vaQuaternion & operator *= ( const vaQuaternion & );
        vaQuaternion & operator *= ( float );
        vaQuaternion & operator /= ( float );

        // unary
        vaQuaternion  operator + ( ) const;
        vaQuaternion  operator - ( ) const;

        // binary
        vaQuaternion operator + ( const vaQuaternion & ) const;
        vaQuaternion operator - ( const vaQuaternion & ) const;
        vaQuaternion operator * ( const vaQuaternion & ) const;
        vaQuaternion operator * ( float ) const;
        vaQuaternion operator / ( float ) const;

        friend vaQuaternion operator * ( float, const vaQuaternion & );

        bool operator == ( const vaQuaternion & ) const;
        bool operator != ( const vaQuaternion & ) const;


        float                Length( ) const;
        float                LengthSq( ) const;
        vaQuaternion         Conjugate( ) const;
        void                 ToAxisAngle( vaVector3 & outAxis, float & outAngle ) const;
        vaQuaternion         Normalized( ) const;
        vaQuaternion         Inversed( ) const;

        // Expects unit quaternions.
        vaQuaternion         Ln( ) const;

        // Expects pure quaternions. (w == 0)  w is ignored in calculation.
        vaQuaternion         Exp( ) const;

        // Could also be called DecomposeEuler( float & zAngle, float & yAngle, float & xAngle );
        // Yaw around the +Z (up) axis, a pitch around the +Y (right) axis, and a roll around the +X (forward) axis.
        void                 DecomposeYawPitchRoll( float & yaw, float & pitch, float & roll ) const;

        // VA convention: X is forward
        vaVector3             GetAxisX( ) const;

        // VA convention: Y is right
        vaVector3             GetAxisY( ) const;

        // VA convention: Z is up
        vaVector3             GetAxisZ( ) const;

    public:
        static float         Dot( const vaQuaternion & a, const vaQuaternion & b );

        // Quaternion multiplication. The result represents the rotation b followed by the rotation a.
        static vaQuaternion  Multiply( const vaQuaternion & a, const vaQuaternion & b );

        // Build quaternion from rotation matrix.
        static vaQuaternion  FromRotationMatrix( const class vaMatrix4x4 & mat );
        static vaQuaternion  FromRotationMatrix( const class vaMatrix3x3 & mat );

        // Build quaternion from axis and angle.
        static vaQuaternion  RotationAxis( const vaVector3 & v, float angle );

        // Yaw around the +Z (up) axis, a pitch around the +Y (right) axis, and a roll around the +X (forward) axis.
        static vaQuaternion  FromYawPitchRoll( float yaw, float pitch, float roll );

        // Spherical linear interpolation between Q1 (t == 0) and Q2 (t == 1).
        // Expects unit quaternions.
        static vaQuaternion  Slerp( const vaQuaternion & q1, const vaQuaternion & q2, float t );

        // Spherical quadrangle interpolation.
        static vaQuaternion  Squad( const vaQuaternion & q1, const vaQuaternion & q2, const vaQuaternion & q3, const vaQuaternion & q4, float t );

        // Component-wise catmull-rom interpolation with renormalization
        static vaQuaternion  CatmullRom( const vaQuaternion &v0, const vaQuaternion &v1, const vaQuaternion & v2, const vaQuaternion & v3, float s );


        // Barycentric interpolation.
        // Slerp(Slerp(Q1, Q2, f+g), Slerp(Q1, Q3, f+g), g/(f+g))
        static vaQuaternion  BaryCentric( const vaQuaternion & q1, const vaQuaternion & q2, const vaQuaternion & q3, float f, float g );
    };

    class vaPlane
    {
    public:
        float                   a, b, c, d;

        static vaPlane          Degenerate;

    public:
        vaPlane( ) { }
        explicit vaPlane( const float * p ) { assert( p != NULL ); a = p[0]; b = p[1]; c = p[2]; d = p[3]; }
        vaPlane( float a, float b, float c, float d ) : a( a ), b( b ), c( c ), d( d ) { }
        explicit vaPlane( const vaVector4 & v ) : a( v.x ), b( v.y ), c( v.z ), d( v.w ) { }

        // assignment operators
        vaPlane& operator *= ( float );
        vaPlane& operator /= ( float );

        // unary operators
        vaPlane operator + ( ) const;
        vaPlane operator - ( ) const;

        // binary operators
        vaPlane operator * ( float ) const;
        vaPlane operator / ( float ) const;

        friend vaPlane operator * ( float, const vaPlane& );
        vaVector3 &             Normal( )                               { return *reinterpret_cast<vaVector3*>(&a); }
        const vaVector3 &       Normal( ) const                         { return *reinterpret_cast<const vaVector3*>(&a); }

        vaVector4 &             AsVec4( )                               { return *reinterpret_cast<vaVector4*>(&a); }
        const vaVector4 &       AsVec4( ) const                         { return *reinterpret_cast<const vaVector4*>(&a); }

        bool operator == ( const vaPlane& ) const;
        bool operator != ( const vaPlane& ) const;

        vaPlane                 PlaneNormalized( ) const;

        bool                    IntersectLine( vaVector3 & outPt, const vaVector3 & lineStart, const vaVector3 & lineEnd );
        bool                    IntersectRay( vaVector3 & outPt, const vaVector3 & lineStart, const vaVector3 & direction );

        float                   Dot( const vaVector4 & v ) const                    { return vaPlane::Dot( *this, v ); }
        float                   DotCoord( const vaVector3 & v ) const               { return vaPlane::DotCoord( *this, v ); }
        float                   DotNormal( const vaVector3 & v ) const              { return vaPlane::DotNormal( *this, v ); }

    public:
        static float            Dot( const vaPlane & plane, const vaVector4 & v );
        static float            DotCoord( const vaPlane & plane, const vaVector3 & v );
        static float            DotNormal( const vaPlane & plane, const vaVector3 & v );

        static vaPlane          FromPointNormal( const vaVector3 & point, const vaVector3 & normal );
        static vaPlane          FromPoints( const vaVector3 & v1, const vaVector3 & v2, const vaVector3 & v3 );

        // Transform a plane by a matrix.  The vector (a,b,c) must be normalized. M should be the inverse transpose of the transformation desired.
        vaPlane                 Transform( const vaPlane & p, const vaMatrix4x4 & mat );
    };

    // this one is unfinished
    class vaMatrix3x3
    {
    public:
        union
        {
            struct
            {
                float       _11, _12, _13;
                float       _21, _22, _23;
                float       _31, _32, _33;
            };
            float m[3][3];
        };

    public:
        static vaMatrix3x3      Identity;

    public:
        vaMatrix3x3( ) { };
        vaMatrix3x3( float _11, float _12, float _13,
            float _21, float _22, float _23,
            float _31, float _32, float _33 );
        vaMatrix3x3( const vaVector3 & axisX, const vaVector3 & axisY, const vaVector3 & axisZ ) { Row(0) = axisX; Row(1) = axisY; Row(2) = axisZ; }
        explicit vaMatrix3x3( const float * );
        explicit vaMatrix3x3( const vaMatrix4x4 & t );

        // access grants
        float& operator () ( uint32 row, uint32 col ) { assert( row < 3 && col < 3 ); return m[row][col]; }
        float  operator () ( uint32 row, uint32 col ) const { assert( row < 3 && col < 3 ); return m[row][col]; }
        vaVector3 &         Row( uint32 row )               { assert( row < 3 ); return *reinterpret_cast<vaVector3*>(m[row]); }
        const vaVector3 &   Row( uint32 row ) const         { assert( row < 3 ); return *reinterpret_cast<const vaVector3*>(m[row]); }

        // assignment operators
        vaMatrix3x3 & operator *= ( const vaMatrix3x3 & mat );
        vaMatrix3x3 & operator += ( const vaMatrix3x3 & mat );
        vaMatrix3x3 & operator -= ( const vaMatrix3x3 & mat );
        vaMatrix3x3 & operator *= ( float );
        vaMatrix3x3 & operator /= ( float );

        // unary operators
        vaMatrix3x3 operator + ( ) const { return *this; }
        vaMatrix3x3 operator - ( ) const { return vaMatrix3x3( -_11, -_12, -_13, -_21, -_22, -_23, -_31, -_32, -_33 ); }

        // binary operators
        vaMatrix3x3 operator * ( const vaMatrix3x3 & ) const;
        vaMatrix3x3 operator + ( const vaMatrix3x3 & ) const;
        vaMatrix3x3 operator - ( const vaMatrix3x3 & ) const;
        vaMatrix3x3 operator * ( float ) const;
        vaMatrix3x3 operator / ( float ) const;

        friend vaMatrix3x3 operator * ( float, const vaMatrix3x3& );

        bool                    operator == ( const vaMatrix3x3 & eq ) const                    { return this->Row(0) == eq.Row(0) && this->Row(1) == eq.Row(1) && this->Row(2) == eq.Row(2); }
        bool                    operator != ( const vaMatrix3x3 & neq ) const                   { return !(*this==neq); }

        // Could also be called DecomposeRotationEuler( float & zAngle, float & yAngle, float & xAngle );
        // Yaw around the +Z (up) axis, a pitch around the +Y (right) axis, and a roll around the +X (forward) axis.
        void                    DecomposeRotationYawPitchRoll( float& yaw, float& pitch, float& roll );
        // Yaw around the +Z (up) axis, a pitch around the +Y (right) axis, and a roll around the +X (forward) axis.
        static vaMatrix3x3      FromYawPitchRoll( float yaw, float pitch, float roll );

        // Build a matrix from quaternion
        static vaMatrix3x3      FromQuaternion( const vaQuaternion & q );

        vaMatrix3x3             Transposed( ) const;

        // Build rotation matrix around X
        static vaMatrix3x3      RotationX( float angle );

        // Build a matrix which rotates around the Y axis
        static vaMatrix3x3      RotationY( float angle );

        // Build a matrix which rotates around the Z axis
        static vaMatrix3x3      RotationZ( float angle );

        // Build a matrix which rotates around an arbitrary axis
        static vaMatrix3x3      RotationAxis( const vaVector3 & vec, float angle );

        // Matrix multiplication.  The result represents transformation b followed by transformation a.
        static vaMatrix3x3      Multiply( const vaMatrix3x3 & a, const vaMatrix3x3 & b );

        static string           ToString( const vaMatrix3x3 & a );
        static bool             FromString( const string & str, vaMatrix3x3 & outVal );
    };

    class alignas(16) vaMatrix4x4
    {
    public:
        union
        {
            struct
            {
                float       _11, _12, _13, _14;
                float       _21, _22, _23, _24;
                float       _31, _32, _33, _34;
                float       _41, _42, _43, _44;
            };
            float m[4][4];
        };

    public:
        static vaMatrix4x4      Identity;

    public:
        vaMatrix4x4( ) { };
        explicit vaMatrix4x4( const float * );
        vaMatrix4x4( float _11, float _12, float _13, float _14,
            float _21, float _22, float _23, float _24,
            float _31, float _32, float _33, float _34,
            float _41, float _42, float _43, float _44 );
        explicit vaMatrix4x4( const vaMatrix3x3 & rm ) { m[0][0] = rm.m[0][0]; m[0][1] = rm.m[0][1]; m[0][2] = rm.m[0][2]; m[1][0] = rm.m[1][0], m[1][1] = rm.m[1][1], m[1][2] = rm.m[1][2], m[2][0] = rm.m[2][0], m[2][1] = rm.m[2][1], m[2][2] = rm.m[2][2]; m[0][3] = 0.0f; m[1][3] = 0.0f; m[2][3] = 0.0f; m[3][0] = 0.0f; m[3][1] = 0.0f; m[3][2] = 0.0f; m[3][3] = 1.0f; };

        // access grants
        float& operator () ( uint32 row, uint32 col )       { assert( row < 4 && col < 4 ); return m[row][col]; }
        float  operator () ( uint32 row, uint32 col ) const { assert( row < 4 && col < 4 ); return m[row][col]; }
        vaVector4 &         Row( uint32 row )               { assert( row < 4 ); return *reinterpret_cast<vaVector4*>(m[row]); }
        const vaVector4 &   Row( uint32 row ) const         { assert( row < 4 ); return *reinterpret_cast<const vaVector4*>(m[row]); }

        // assignment operators
        vaMatrix4x4 & operator *= ( const vaMatrix4x4 & mat );
        vaMatrix4x4 & operator += ( const vaMatrix4x4 & mat );
        vaMatrix4x4 & operator -= ( const vaMatrix4x4 & mat );
        vaMatrix4x4 & operator *= ( float );
        vaMatrix4x4 & operator /= ( float );

        // unary operators
        vaMatrix4x4 operator + ( ) const { return *this; }
        vaMatrix4x4 operator - ( ) const { return vaMatrix4x4( -_11, -_12, -_13, -_14, -_21, -_22, -_23, -_24, -_31, -_32, -_33, -_34, -_41, -_42, -_43, -_44 ); }

        // binary operators
        vaMatrix4x4 operator * ( const vaMatrix4x4 & ) const;
        vaMatrix4x4 operator + ( const vaMatrix4x4 & ) const;
        vaMatrix4x4 operator - ( const vaMatrix4x4 & ) const;
        vaMatrix4x4 operator * ( float ) const;
        vaMatrix4x4 operator / ( float ) const;

        friend vaMatrix4x4 operator * ( float, const vaMatrix4x4& );

        bool operator == ( const vaMatrix4x4& ) const;
        bool operator != ( const vaMatrix4x4& ) const;

        float                   Determinant( ) const;
        double                  DeterminantD( ) const;
        vaMatrix4x4             Transposed( ) const;
        bool                    Decompose( vaVector3 & outScale, vaQuaternion & outRotation, vaVector3 & outTranslation ) const;
        bool                    Decompose( vaVector3 & outScale, vaMatrix3x3 & outRotation, vaVector3 & outTranslation ) const;
        void                    Transpose( );

        // Could also be called DecomposeRotationEuler( float & zAngle, float & yAngle, float & xAngle );
        // Yaw around the +Z (up) axis, a pitch around the +Y (right) axis, and a roll around the +X (forward) axis.
        void                    DecomposeRotationYawPitchRoll( float & yaw, float & pitch, float & roll );

        bool                    Inverse( vaMatrix4x4 & outMat, float * outDeterminant = nullptr ) const;
        bool                    InverseHighPrecision( vaMatrix4x4 & outMat, double * outDeterminant = nullptr ) const;
        vaMatrix4x4             Inversed( float * outDeterminant = nullptr, bool assertOnFail = true ) const { vaMatrix4x4 ret; bool res = Inverse( ret, outDeterminant ); if( assertOnFail ) { assert( res ); }; if( !res ) return vaMatrix4x4::Identity; else return ret; }
        vaMatrix4x4             InversedHighPrecision( double * outDeterminant = nullptr, bool assertOnFail = true ) const { vaMatrix4x4 ret; bool res = InverseHighPrecision( ret, outDeterminant ); if( assertOnFail ) { assert( res ); }; if( !res ) return vaMatrix4x4::Identity; else return ret; }

        vaMatrix4x4             FastTransformInversed( ) const;

        vaVector3               GetRotationX( ) const { return vaVector3( m[0][0], m[0][1], m[0][2] ); }
        vaVector3               GetRotationY( ) const { return vaVector3( m[1][0], m[1][1], m[1][2] ); }
        vaVector3               GetRotationZ( ) const { return vaVector3( m[2][0], m[2][1], m[2][2] ); }

        vaMatrix3x3             GetRotationMatrix3x3( ) const { return vaMatrix3x3( m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2] ); }

        // VA convention: X is forward
        vaVector3               GetAxisX( ) const { return vaVector3( m[0][0], m[0][1], m[0][2] ); }

        // VA convention: Y is right
        vaVector3               GetAxisY( ) const { return vaVector3( m[1][0], m[1][1], m[1][2] ); }

        // VA convention: Z is up
        vaVector3               GetAxisZ( ) const { return vaVector3( m[2][0], m[2][1], m[2][2] ); }

        vaVector3               GetTranslation( ) const { return vaVector3( m[3][0], m[3][1], m[3][2] ); }

        void                    SetRotation( const vaMatrix3x3 & rm ) { m[0][0] = rm.m[0][0]; m[0][1] = rm.m[0][1]; m[0][2] = rm.m[0][2]; m[1][0] = rm.m[1][0], m[1][1] = rm.m[1][1], m[1][2] = rm.m[1][2], m[2][0] = rm.m[2][0], m[2][1] = rm.m[2][1], m[2][2] = rm.m[2][2]; m[0][3] = 0.0f; m[1][3] = 0.0f; m[2][3] = 0.0f; }
        void                    SetTranslation( const vaVector3 & vec ) { m[3][0] = vec.x; m[3][1] = vec.y; m[3][2] = vec.z; m[3][3] = 1.0f; }

    public:

        // Matrix multiplication.  The result represents transformation b followed by transformation a.
        static vaMatrix4x4      Multiply( const vaMatrix4x4 & a, const vaMatrix4x4 & b );

        // Build scaling matrix
        static vaMatrix4x4      Scaling( float sx, float sy, float sz );
        static vaMatrix4x4      Scaling( const vaVector3 & vec ) { return Scaling( vec.x, vec.y, vec.z ); }

        // Build translation matrix 
        static vaMatrix4x4      Translation( float x, float y, float z );
        static vaMatrix4x4      Translation( const vaVector3 & vec ) { return Translation( vec.x, vec.y, vec.z ); }

        // Build rotation matrix around X
        static vaMatrix4x4      RotationX( float angle );

        // Build a matrix which rotates around the Y axis
        static vaMatrix4x4      RotationY( float angle );

        // Build a matrix which rotates around the Z axis
        static vaMatrix4x4      RotationZ( float angle );

        // Build a matrix which rotates around an arbitrary axis
        static vaMatrix4x4      RotationAxis( const vaVector3 & vec, float angle );

        // Yaw around the +Z (up) axis, a pitch around the +Y (right) axis, and a roll around the +X (forward) axis.
        static vaMatrix4x4      FromYawPitchRoll( float yaw, float pitch, float roll );

        // Build a lookat matrix. (right-handed)
        static vaMatrix4x4      LookAtRH( const vaVector3 & eye, const vaVector3 & at, const vaVector3 & up );

        // Build a lookat matrix. (left-handed)
        static vaMatrix4x4      LookAtLH( const vaVector3 & eye, const vaVector3 & at, const vaVector3 & up );

        // Build a perspective projection matrix. (right-handed)
        static vaMatrix4x4      PerspectiveRH( float w, float h, float zn, float zf );

        // Build a perspective projection matrix. (left-handed)
        static vaMatrix4x4      PerspectiveLH( float w, float h, float zn, float zf );

        // Build a perspective projection matrix. (right-handed)
        static vaMatrix4x4      PerspectiveFovRH( float fovy, float Aspect, float zn, float zf );

        // Build a perspective projection matrix. (left-handed)
        static vaMatrix4x4      PerspectiveFovLH( float fovy, float Aspect, float zn, float zf );

        // Build a perspective projection matrix. (right-handed)
        static vaMatrix4x4      PerspectiveOffCenterRH( float l, float r, float b, float t, float zn, float zf );

        // Build a perspective projection matrix. (left-handed)
        static vaMatrix4x4      PerspectiveOffCenterLH( float l, float r, float b, float t, float zn, float zf );

        // Build an ortho projection matrix. (right-handed)
        static vaMatrix4x4      OrthoRH( float w, float h, float zn, float zf );

        // Build an ortho projection matrix. (left-handed)
        static vaMatrix4x4      OrthoLH( float w, float h, float zn, float zf );

        // Build an ortho projection matrix. (right-handed)
        static vaMatrix4x4      OrthoOffCenterRH( float l, float r, float b, float t, float zn, float zf );

        // Build an ortho projection matrix. (left-handed)
        static vaMatrix4x4      OrthoOffCenterLH( float l, float r, float b, float t, float zn, float zf );

        // Build a matrix which flattens geometry into a plane, as if casting a shadow from a light.
        static vaMatrix4x4      Shadow( const vaVector4 & light, const vaPlane & plane );

        // Build a matrix which reflects the coordinate system about a plane
        static vaMatrix4x4      Reflect( const vaPlane & plane );

        // Build a matrix from quaternion
        static vaMatrix4x4      FromQuaternion( const vaQuaternion & q );

        // Build a matrix from translation
        static vaMatrix4x4      FromTranslation( const vaVector3 & trans );

        // Build a matrix from rotation & translation, in two flavors
        static vaMatrix4x4      FromRotationTranslation( const vaQuaternion & rot, const vaVector3 & trans );
        static vaMatrix4x4      FromRotationTranslation( const vaMatrix3x3 & rot, const vaVector3 & trans );

        // Build a matrix from rotation & translation & scale, in two flavors
        static vaMatrix4x4      FromScaleRotationTranslation( const vaVector3 & scale, const vaQuaternion & rot, const vaVector3 & trans );
        static vaMatrix4x4      FromScaleRotationTranslation( const vaVector3 & scale, const vaMatrix3x3 & rot, const vaVector3 & trans );

        // not really ideal, for better see https://bitbashing.io/comparing-floats.html / http://floating-point-gui.de/references/
        static bool             NearEqual( const vaMatrix4x4 & a, const vaMatrix4x4 & b, float epsilon = 1e-4f );

        static string           ToString( const vaMatrix4x4& a );
        static bool             FromString( const string& a, vaMatrix4x4& outVal );
    };

    // Useful when you wish to cut off the last column - enough for affine transformations!
    // Is loaded in shaders as matrix3x4 (see 
    class vaMatrix4x3
    {
    public:
        union
        {
            struct
            {
                float       _11, _12, _13;
                float       _21, _22, _23;
                float       _31, _32, _33;
                float       _41, _42, _43;
            };
            float m[4][3];
        };
        vaMatrix4x3( ) { };
        // explicit vaMatrix4x3( const float * );
        explicit vaMatrix4x3( const vaMatrix4x4 & m ) : _11(m._11), _12(m._12), _13(m._13), _21(m._21), _22(m._22), _23(m._23), _31(m._31), _32(m._32), _33(m._33), _41(m._41), _42(m._42), _43(m._43) { }
    };

    class vaRay3D
    {
    public:
        vaVector3                         Origin;
        vaVector3                         Direction;

    public:
        vaRay3D( ) {};

        inline vaVector3                  GetPointAt( float dist ) const;

        static inline vaRay3D             FromTwoPoints( const vaVector3 & p1, const vaVector3 & p2 );
        static inline vaRay3D             FromOriginAndDirection( const vaVector3 & origin, const vaVector3 & direction );
    };

    class vaBoundingSphere
    {
    public:
        vaVector3                       Center;
        float                           Radius;

        static vaBoundingSphere         Degenerate;

    public:
        vaBoundingSphere( ) { }
        vaBoundingSphere( const vaVector3 & center, float radius ) : Center( center ), Radius( radius )                 { }

        bool operator ==                ( const vaBoundingSphere & other ) const                                        { return Center == other.Center && Radius == other.Radius; }
        bool operator !=                ( const vaBoundingSphere & other ) const                                        { return Center != other.Center || Radius != other.Radius; }

        vaVector3                       RandomPointOnSurface( vaRandom & randomGeneratorToUse = vaRandom::Singleton )   { return vaVector3::RandomNormal( randomGeneratorToUse ) * Radius + Center; }
        vaVector3                       RandomPointInside( vaRandom & randomGeneratorToUse = vaRandom::Singleton )      { return vaVector3::RandomNormal( randomGeneratorToUse ) * ( vaMath::Pow( randomGeneratorToUse.NextFloat( ), 1.0f / 3.0f ) * Radius ) + Center; }

        vaIntersectType                 IntersectFrustum( const vaPlane planes[], const int planeCount ) const;

        bool                            PointInside( const vaVector3 & point )                                          { return (point - Center).LengthSq() <= (Radius*Radius); }

        static vaBoundingSphere         FromOBB( const class vaOrientedBoundingBox & obb );
        static vaBoundingSphere         FromAABB( const class vaBoundingBox & aabb );

        static vaBoundingSphere         Transform( const vaBoundingSphere & bs, const vaMatrix4x4 & transform );

        // Compute smallest enclosing sphere
        static vaBoundingSphere         Merge( const vaBoundingSphere & s0, const vaBoundingSphere & s1 );
    };

    class vaBoundingBox
    {
    public:
        vaVector3                       Min;
        vaVector3                       Size;

        static vaBoundingBox            Degenerate;

    public:
        vaBoundingBox( ) { };
        vaBoundingBox( const vaVector3 & bmin, const vaVector3 & bsize ) : Min( bmin ), Size( bsize ) { }

        bool                            operator == ( const vaBoundingBox & eq ) const                      { return this->Min == eq.Min && this->Size == eq.Size; }
        bool                            operator != ( const vaBoundingBox & eq ) const                      { return !(*this == eq); }

        vaVector3                       Center( ) const { return Min + Size * 0.5f; }
        vaVector3                       Max( ) const { return Min + Size; }

        void                            GetCornerPoints( vaVector3 corners[] ) const;
        vaIntersectType                 IntersectFrustum( const vaPlane planes[], const int planeCount ) const;
        vaIntersectType                 IntersectFrustum( const std::vector<vaPlane> & planes ) const            { return (planes.size() == 0)?(vaIntersectType::Inside):( IntersectFrustum( &planes[0], (int)planes.size() ) ); }

        bool                            PointInside( const vaVector3 & point ) const;

        float                           NearestDistanceToPoint( const vaVector3 & pt ) const;
        float                           FarthestDistanceToPoint( const vaVector3 & pt ) const;

        static vaBoundingBox            Combine( const vaBoundingBox & a, const vaBoundingBox & b );

        static string                   ToString( const vaBoundingBox & a );
    };

    class vaOrientedBoundingBox
    {
    public:
        vaVector3                        Center;
        vaVector3                        Extents;    // aka half-size
        vaMatrix3x3                      Axis;

        static vaOrientedBoundingBox     Degenerate;

    public:
        vaOrientedBoundingBox( ) { }
        vaOrientedBoundingBox( const vaVector3 & center, const vaVector3 & halfSize, const vaMatrix3x3 & axis ) : Center( center ), Extents( halfSize ), Axis( axis ) { }
        vaOrientedBoundingBox( const vaBoundingBox & box, const vaMatrix4x4 & transform ) { *this = FromAABBAndTransform( box, transform ); }

        bool                            operator == ( const vaOrientedBoundingBox & eq ) const                    { return this->Center == eq.Center && this->Extents == eq.Extents && this->Axis == eq.Axis; }
        bool                            operator != ( const vaOrientedBoundingBox & eq ) const                    { return !(*this == eq); }

        vaVector3                       Min( ) const { return Center - Extents; }
        vaVector3                       Max( ) const { return Center + Extents; }

        static vaOrientedBoundingBox    FromAABBAndTransform( const vaBoundingBox & box, const vaMatrix4x4 & transform );
        void                            ToAABBAndTransform( vaBoundingBox & outBox, vaMatrix4x4 & outTransform ) const;

        static vaOrientedBoundingBox    FromScaledTransform( const vaMatrix4x4& transform );
        vaMatrix4x4                     ToScaledTransform( ) const;

        vaBoundingBox                   ComputeEnclosingAABB( ) const;

        // 0 means intersect, -1 means it's wholly in the negative halfspace of the plane, 1 means it's in the positive half-space of the plane
        int                             IntersectPlane( const vaPlane & plane );

        vaIntersectType                 IntersectFrustum( const vaPlane planes[], const int planeCount );
        vaIntersectType                 IntersectFrustum( const std::vector<vaPlane> & planes )                  { return (planes.size() == 0)?(vaIntersectType::Inside):( IntersectFrustum( &planes[0], (int)planes.size() ) ); }

        vaVector3                       RandomPointInside( vaRandom & randomGeneratorToUse = vaRandom::Singleton );

        // supports only affine transformations
        static vaOrientedBoundingBox    Transform( const vaOrientedBoundingBox & obb, const vaMatrix4x4 & mat );

        float                           NearestDistanceToPoint( const vaVector3 & pt ) const;
        float                           FarthestDistanceToPoint( const vaVector3 & pt ) const;

        float                           NearestDistanceToPlane( const vaPlane & plane ) const;
        float                           FarthestDistanceToPlane( const vaPlane & plane ) const;

        static string                   ToString( const vaOrientedBoundingBox & a );
        static bool                     FromString( const string & str, vaOrientedBoundingBox & outVal );
    };

    class vaVector2i
    {
    public:
        int x, y;

    public:
        vaVector2i( ) { };
        vaVector2i( int x, int y ) : x( x ), y( y ) { };
        explicit vaVector2i( const vaVector2 & v ) : x( (int)v.x ), y( (int)v.y ) { };

        // assignment operators
        vaVector2i &    operator += ( const vaVector2i & );
        vaVector2i &    operator -= ( const vaVector2i & );

        // unary operators
        vaVector2i      operator + ( ) const;
        vaVector2i      operator - ( ) const;

        // binary operators
        vaVector2i      operator + ( const vaVector2i & ) const;
        vaVector2i      operator - ( const vaVector2i & ) const;
        bool            operator == ( const vaVector2i & ) const;
        bool            operator != ( const vaVector2i & ) const;

        vaVector2i      operator * ( int value ) const  { return vaVector2i( this->x * value, this->y * value ); }

        // cast operators
        explicit operator vaVector2 ( ) { return vaVector2( (float)x, (float)y ); }
    };

    class vaVector2ui
    {
    public:
        uint32 x, y;

    public:
        vaVector2ui( ) { };
        vaVector2ui( uint32 x, uint32 y ) : x( x ), y( y ) { };
        explicit vaVector2ui( const vaVector2 & v ) : x( (uint32)v.x ), y( (uint32)v.y ) { };

        // assignment operators
        vaVector2ui &   operator += ( const vaVector2ui & );
        vaVector2ui &   operator -= ( const vaVector2ui & );

        // unary operators
        vaVector2ui     operator + ( ) const;
        vaVector2ui     operator - ( ) const;

        // binary operators
        vaVector2ui     operator + ( const vaVector2ui & ) const;
        vaVector2ui     operator - ( const vaVector2ui & ) const;
        bool            operator == ( const vaVector2ui & ) const;
        bool            operator != ( const vaVector2ui & ) const;

        vaVector2ui     operator * ( int value ) const  { return vaVector2ui( this->x * value, this->y * value ); }

        // cast operators
        explicit operator vaVector2 ( ) { return vaVector2( (float)x, (float)y ); }
    };

    class vaVector3i
    {
    public:
        int x, y, z;

    public:
        vaVector3i( ) { };
        vaVector3i( int x, int y, int z ) : x( x ), y( y ), z( z ) { };
        //explicit vaVector3i( const vaVector4 & v ) : x( (int)v.x ), y( (int)v.y ), z( (int)v.z ), w( (int)v.w ) { };

        // assignment operators
        vaVector3i &        operator += ( const vaVector3i & );
        vaVector3i &        operator -= ( const vaVector3i & );

        // unary operators
        vaVector3i          operator + ( ) const;
        vaVector3i          operator - ( ) const;

        // binary operators
        vaVector3i          operator + ( const vaVector3i & ) const;
        vaVector3i          operator - ( const vaVector3i & ) const;
        bool                operator == ( const vaVector3i & eq ) const                     { return this->x == eq.x && this->y == eq.y && this->z == eq.z; }
        bool                operator != ( const vaVector3i & eq ) const                     { return this->x != eq.x || this->y != eq.y || this->z != eq.z; }

        vaVector2i &        AsVec2( )                                                       { return *( (vaVector2i*)( &x ) ); }
        const vaVector2i &  AsVec2( ) const                                                 { return *( (vaVector2i*)( &x ) ); }

        // // cast operators
        // explicit operator vaVector4 ( ) { return vaVector4( (float)x, (float)y, (float)z, (float)w ); }
    };

    class vaVector4i
    {
    public:
        int x, y, z, w;

    public:
        vaVector4i( ) { };
        vaVector4i( int x, int y, int z, int w ) : x( x ), y( y ), z( z ), w( w ) { };
        explicit vaVector4i( const vaVector4 & v ) : x( (int)v.x ), y( (int)v.y ), z( (int)v.z ), w( (int)v.w ) { };

        // assignment operators
        vaVector4i &    operator += ( const vaVector4i & );
        vaVector4i &    operator -= ( const vaVector4i & );

        // unary operators
        vaVector4i      operator + ( ) const;
        vaVector4i      operator - ( ) const;

        // binary operators
        vaVector4i      operator + ( const vaVector4i & ) const;
        vaVector4i      operator - ( const vaVector4i & ) const;
        bool            operator == ( const vaVector4i & eq ) const                    { return this->x == eq.x && this->y == eq.y && this->z == eq.z && this->w == eq.w; }
        bool            operator != ( const vaVector4i & eq ) const                    { return this->x != eq.x || this->y != eq.y || this->z != eq.z || this->w != eq.w; }

        // cast operators
        explicit operator vaVector4 ( ) { return vaVector4( (float)x, (float)y, (float)z, (float)w ); }
    };

    class vaVector4ui
    {
    public:
        uint32 x, y, z, w;

    public:
        vaVector4ui( ) { };
        vaVector4ui( uint32 x, uint32 y, uint32 z, uint32 w ) : x( x ), y( y ), z( z ), w( w ) { };

        // assignment operators
        vaVector4ui &   operator += ( const vaVector4ui & );
        vaVector4ui &   operator -= ( const vaVector4ui & );

        // binary operators
        vaVector4ui     operator + ( const vaVector4ui & ) const;
        vaVector4ui     operator - ( const vaVector4ui & ) const;
        bool            operator == ( const vaVector4i & eq ) const                    { return int(this->x) == eq.x && int(this->y) == eq.y && int(this->z) == eq.z && int(this->w) == eq.w; }
        bool            operator != ( const vaVector4i & eq ) const                    { return int(this->x) != eq.x || int(this->y) != eq.y || int(this->z) != eq.z || int(this->w) != eq.w; }
    };

    struct vaRecti
    {
    public:
        int left, top, right, bottom;

    public:
        vaRecti( ) { };
        vaRecti( int left, int top, int right, int bottom ) : left( left ), top( top ), right( right ), bottom ( bottom ) { };

        int Width( )        { return right - left; }
        int Height( )       { return bottom - top; }

        // binary operators
        bool            operator == ( const vaRecti & eq ) const                    { return this->left == eq.left && this->top == eq.top && this->right == eq.right && this->bottom == eq.bottom; }
        bool            operator != ( const vaRecti & eq ) const                    { return this->left != eq.left || this->top != eq.top || this->right != eq.right || this->bottom != eq.bottom; }
    };

    struct vaBoxi
    {
    public:
        int left, top, front, right, bottom, back;

    public:
        vaBoxi( ) { };
        vaBoxi( int left, int top, int front, int right, int bottom, int back ) : left( left ), top( top ), front( front ), right( right ), bottom( bottom ), back( back ) { };

        int Width( )        { return right - left; }
        int Height( )       { return bottom - top; }
        int Depth( )        { return back - front; }

        // binary operators
        bool            operator == ( const vaBoxi & eq ) const { return this->left == eq.left && this->top == eq.top && this->front == eq.front && this->right == eq.right && this->bottom == eq.bottom && this->back == eq.back; }
        bool            operator != ( const vaBoxi & eq ) const { return this->left != eq.left || this->top != eq.top || this->front != eq.front || this->right != eq.right || this->bottom != eq.bottom || this->back != eq.back; }
    };
    
    // a mix of render viewport and scissor since they seem to always go together - not really a geometry thing but used for some transforms so let's keep it here
    struct vaViewport
    {
        int         X                   = 0;
        int         Y                   = 0;
        int         Width               = 0;          
        int         Height              = 0;
        float       MinDepth            = 0.0f;
        float       MaxDepth            = 1.0f;
        vaRecti     ScissorRect         = vaRecti( 0, 0, 0, 0 );
        bool        ScissorRectEnabled  = false;

        vaViewport( ) { }
        vaViewport( int width, int height ) : Width( width ), Height( height ) { }
        vaViewport( int x, int y, int width, int height ) : X( x ), Y( y ), Width( width ), Height( height ) { }
        vaViewport( int x, int y, int width, int height, const vaRecti & scissorRect ) : X( x ), Y( y ), Width( width ), Height( height ), ScissorRect(scissorRect), ScissorRectEnabled(true) { }

        bool operator == ( const vaViewport & other ) const;
        bool operator != ( const vaViewport & other ) const  { return !(*this == other); }
    };

    class vaGeometry
    {
    public:

        static float            FresnelTerm( float cosTheta, float refractionIndex );

        static void             CalculateFrustumPlanes( vaPlane planes[6], const vaMatrix4x4 & cameraViewProj );

        // not really ideal, for better see https://bitbashing.io/comparing-floats.html / http://floating-point-gui.de/references/
        static inline bool      NearEqual( float a, float b, const float fEps = VA_EPSf );
        static inline bool      NearEqual( const vaVector2 & a, const vaVector2 & b, float epsilon = VA_EPSf );
        static inline bool      NearEqual( const vaVector3 & a, const vaVector3 & b, float epsilon = VA_EPSf );
        static inline bool      NearEqual( const vaVector4 & a, const vaVector4 & b, float epsilon = VA_EPSf );
        static inline bool      NearEqual( const vaBoundingSphere & a, const vaBoundingSphere & b, float epsilon = VA_EPSf );

        static inline bool      IntersectSegments2D( const vaVector2 & p1, const vaVector2 & p2, const vaVector2 & p3, const vaVector2 & p4, vaVector2 & outPt );

        static inline vaVector3 WorldToViewportSpace( const vaVector3 & worldPos, const vaMatrix4x4 & viewProj, const vaViewport & viewport );
        static inline vaVector3 ViewportToWorldSpace( const vaVector3 & screenPos, const vaMatrix4x4 & inverseViewProj, const vaViewport & viewport );

        // 3d vector to/from spherical coordinate system (https://en.wikipedia.org/wiki/Spherical_coordinate_system) conversions. 
        // Our reference frame is: Z is up (zenith), zero azimuth angle (reference xy vector with polar angle at 90deg) is [x = 1, y = 0], azimuth is measured clockwise looking down from Z+ (left-handed I guess?)
        static inline void      CartesianToSpherical( const vaVector3 & inVector, float & outAzimuthAngle, float & outPolarAngle, float & outRadialDistance );
        static inline void      SphericalToCartesian( float azimuthAngle, float polarAngle, float radialDistance, vaVector3 & outVector );
        static inline void      CartesianToSpherical( const vaVector3 & inVector, float & outAzimuthAngle, float & outPolarAngle )      { float dummy; CartesianToSpherical( inVector, outAzimuthAngle, outPolarAngle, dummy); }
        static inline vaVector3 SphericalToCartesian( float azimuthAngle, float polarAngle, float radialDistance )                      { vaVector3 ret; SphericalToCartesian( azimuthAngle, polarAngle, radialDistance, ret ); return ret; }
    };

    class vaColor
    {
    public:

        static inline float     LinearToSRGB( float val );
        static inline float     SRGBToLinear( float val );

        // https://en.wikipedia.org/wiki/Relative_luminance
        static float            LinearToLuminance( const vaVector3 & colour )   { return colour.x * 0.2126f + colour.y * 0.7152f + colour.z * 0.0722f; }

        static void             NormalizeLuminance( vaVector3 & inoutColor, float & inoutIntensity );
    };

    // this should go into Containers but is cool here for now
    template< typename T >
    class vaStaticArray2D
    {
        T *     m_data;
        int     m_width;
        int     m_height;

    public:
        vaStaticArray2D( ) : m_data( nullptr ), m_width( 0 ), m_height( 0 ) { }
        vaStaticArray2D( const vaStaticArray2D<T> & copy ) : m_data( nullptr ), m_width( 0 ), m_height( 0 ) { Create(copy.GetWidth(), copy.GetHeight()); memcpy( m_data, copy.GetData(), sizeof(T) * m_width * m_height ); }
        vaStaticArray2D( int width, int height )                { Create( width, height ); }
        ~vaStaticArray2D( )                                     { Destroy(); }

        void Create( int width, int height )                    { Destroy(); m_data = new T[ width * height ]; m_width = width; m_height = height;  }
        void Destroy( )                                         { delete[] m_data; m_data = nullptr; m_width = 0; m_height = 0; }
        bool IsCreated( ) const                                 { return m_data != nullptr; }
        void CopyFrom( const vaStaticArray2D<T> & other );

    public:
        T &         operator() ( int x, int y )                 { assert(m_data != nullptr); assert( x >= 0 && x < m_width ); assert( y >= 0 && y < m_height ); return m_data[ x + y * m_width ]; }
        const T &   operator() ( int x, int y ) const           { assert(m_data != nullptr); assert( x >= 0 && x < m_width ); assert( y >= 0 && y < m_height ); return m_data[ x + y * m_width ]; }
        T *         GetData( )                                  { return m_data; }
        const T *   GetData( ) const                            { return m_data; }
        int         GetPitch( ) const                           { return (int)sizeof(T) * m_width; } // pitch (stride) in bytes
        int         GetWidth( ) const                           { return m_width; }
        int         GetHeight( ) const                          { return m_height; }

        vaStaticArray2D<T> & operator = ( const vaStaticArray2D<T> & copy ) 
                                                                { Create(copy.GetWidth(), copy.GetHeight()); memcpy( m_data, copy.GetData(), sizeof(T) * m_width * m_height ); return *this; }
    };

    template< typename T >
    inline T            vaComponentMin( T a, T b )                                      { return std::min(a, b); }
    inline vaVector2    vaComponentMin( const vaVector2 & a, const vaVector2 & b )      { return vaVector2::ComponentMin(a,b); }
    inline vaVector3    vaComponentMin( const vaVector3 & a, const vaVector3 & b )      { return vaVector3::ComponentMin(a,b); }
    inline vaVector4    vaComponentMin( const vaVector4 & a, const vaVector4 & b )      { return vaVector4::ComponentMin(a,b); }
    template< typename T >
    inline float        vaComponentMax( T a, T b )                                      { return std::max(a, b); }
    inline vaVector2    vaComponentMax( const vaVector2 & a, const vaVector2 & b )      { return vaVector2::ComponentMax(a,b); }
    inline vaVector3    vaComponentMax( const vaVector3 & a, const vaVector3 & b )      { return vaVector3::ComponentMax(a,b); }
    inline vaVector4    vaComponentMax( const vaVector4 & a, const vaVector4 & b )      { return vaVector4::ComponentMax(a,b); }
    inline float        vaLength( const vaVector2 & a )                                 { return a.Length(); }
    inline float        vaLength( const vaVector3 & a )                                 { return a.Length(); }
    inline float        vaLength( const vaVector4 & a )                                 { return a.Length(); }

#include "vaGeometry.inl"

}
