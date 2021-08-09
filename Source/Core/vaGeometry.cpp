///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaGeometry.h"

#define VA_USE_SSE

#ifdef VA_USE_SSE
#include <xmmintrin.h>
#include <smmintrin.h>
#endif

using namespace Vanilla;

///////////////////////////////////////////////////////////////////////////////////////////////////
// vaVector2
///////////////////////////////////////////////////////////////////////////////////////////////////

vaVector2    vaVector2::Zero( 0, 0 );

///////////////////////////////////////////////////////////////////////////////////////////////////
// vaVector3
///////////////////////////////////////////////////////////////////////////////////////////////////

vaVector3    vaVector3::Zero( 0, 0, 0 );

string vaVector3::ToString( const vaVector3 & a )
{
    char buffer[512];
    sprintf_s( buffer, _countof(buffer), "{%f,%f,%f}", a.x, a.y, a.z );
    return buffer;
}

bool vaVector3::FromString( const string & a, vaVector3 & outVal )
{
    return sscanf_s( a.c_str(), "{%f,%f,%f}", &outVal.x, &outVal.y, &outVal.z ) == 3;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// vaVector4
///////////////////////////////////////////////////////////////////////////////////////////////////

vaVector4    vaVector4::Zero( 0, 0, 0, 0 );

string vaVector4::ToString( const vaVector4 & a )
{
    char buffer[512];
    sprintf_s( buffer, _countof(buffer), "{%f,%f,%f,%f}", a.x, a.y, a.z, a.w );
    return buffer;
}

bool vaVector4::FromString( const string & a, vaVector4 & outVal )
{
    return sscanf_s( a.c_str(), "{%f,%f,%f,%f}", &outVal.x, &outVal.y, &outVal.z, &outVal.w ) == 4;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// vaMatrix3x3
///////////////////////////////////////////////////////////////////////////////////////////////////

vaMatrix3x3 vaMatrix3x3::Identity = vaMatrix3x3( 1, 0, 0, 
                                                0, 1, 0,
                                                0, 0, 1 );

string vaMatrix3x3::ToString( const vaMatrix3x3& a )
{
    char buffer[1024];
    sprintf_s( buffer, _countof( buffer ), "{%f,%f,%f,%f,%f,%f,%f,%f,%f}",
        a._11, a._12, a._13,
        a._21, a._22, a._23,
        a._31, a._32, a._33 );
    return buffer;
}

bool vaMatrix3x3::FromString( const string& str, vaMatrix3x3& outVal )
{
    return sscanf_s( str.c_str( ), "{%f,%f,%f,%f,%f,%f,%f,%f,%f}",
        &outVal._11, &outVal._12, &outVal._13,
        &outVal._21, &outVal._22, &outVal._23,
        &outVal._31, &outVal._32, &outVal._33 ) == 9;
}


vaMatrix3x3::vaMatrix3x3( const float * p )
{
    assert( p != NULL );
    memcpy( &_11, p, sizeof( vaMatrix3x3 ) );
}

vaMatrix3x3 vaMatrix3x3::RotationX( float angle )
{
    vaMatrix3x3 ret = vaMatrix3x3::Identity;
    ret.m[1][1] = vaMath::Cos( angle );
    ret.m[2][2] = vaMath::Cos( angle );
    ret.m[1][2] = vaMath::Sin( angle );
    ret.m[2][1] = -vaMath::Sin( angle );
    return ret;
}

vaMatrix3x3 vaMatrix3x3::RotationY( float angle )
{
    vaMatrix3x3 ret = vaMatrix3x3::Identity;
    ret.m[0][0] = vaMath::Cos( angle );
    ret.m[2][2] = vaMath::Cos( angle );
    ret.m[0][2] = -vaMath::Sin( angle );
    ret.m[2][0] = vaMath::Sin( angle );
    return ret;
}

vaMatrix3x3 vaMatrix3x3::RotationZ( float angle )
{
    vaMatrix3x3 ret = vaMatrix3x3::Identity;
    ret.m[0][0] = vaMath::Cos( angle );
    ret.m[1][1] = vaMath::Cos( angle );
    ret.m[0][1] = vaMath::Sin( angle );
    ret.m[1][0] = -vaMath::Sin( angle );
    return ret;
}


vaMatrix3x3 vaMatrix3x3::RotationAxis( const vaVector3 & vec, float angle )
{
    vaMatrix3x3 ret;
    vaVector3 v = vec.Normalized( );

    ret.m[0][0] = ( 1.0f - vaMath::Cos( angle ) ) * v.x * v.x + vaMath::Cos( angle );
    ret.m[1][0] = ( 1.0f - vaMath::Cos( angle ) ) * v.x * v.y - vaMath::Sin( angle ) * v.z;
    ret.m[2][0] = ( 1.0f - vaMath::Cos( angle ) ) * v.x * v.z + vaMath::Sin( angle ) * v.y;
    ret.m[0][1] = ( 1.0f - vaMath::Cos( angle ) ) * v.y * v.x + vaMath::Sin( angle ) * v.z;
    ret.m[1][1] = ( 1.0f - vaMath::Cos( angle ) ) * v.y * v.y + vaMath::Cos( angle );
    ret.m[2][1] = ( 1.0f - vaMath::Cos( angle ) ) * v.y * v.z - vaMath::Sin( angle ) * v.x;
    ret.m[0][2] = ( 1.0f - vaMath::Cos( angle ) ) * v.z * v.x - vaMath::Sin( angle ) * v.y;
    ret.m[1][2] = ( 1.0f - vaMath::Cos( angle ) ) * v.z * v.y + vaMath::Sin( angle ) * v.x;
    ret.m[2][2] = ( 1.0f - vaMath::Cos( angle ) ) * v.z * v.z + vaMath::Cos( angle );

    return ret;
}

// static

vaMatrix3x3 vaMatrix3x3::Multiply( const vaMatrix3x3 & a, const vaMatrix3x3 & b )
{
    vaMatrix3x3 ret;

    for( int i = 0; i < 3; i++ )
    {
        for( int j = 0; j < 3; j++ )
        {
            ret.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }

    return ret;
}

void vaMatrix3x3::DecomposeRotationYawPitchRoll( float& yaw, float& pitch, float& roll )
{
    //pitch = (float)vaMath::ASin( -m[2][1] ); 
    pitch = (float)vaMath::ASin( -m[0][2] );

    float threshold = 0.001f;

    float test = (float)vaMath::Cos( pitch );

    if( test > threshold )
    {

        //roll = (float)vaMath::ATan2( m[0][1], m[1][1] ); 
        roll = (float)vaMath::ATan2( m[1][2], m[2][2] );

        //yaw = (float)vaMath::ATan2( m[2][0], m[2][2] );
        yaw = (float)vaMath::ATan2( m[0][1], m[0][0] );

    }
    else
    {
        //roll = (float)vaMath::ATan2( -m[1][0], m[0][0] ); 
        roll = (float)vaMath::ATan2( -m[2][1], m[1][1] );
        yaw = 0.0f;
    }
}

vaMatrix3x3 vaMatrix3x3::FromYawPitchRoll( float yaw, float pitch, float roll )
{
    vaMatrix3x3 ret;

    const float    Sx = sinf( -roll );
    const float    Sy = sinf( -pitch );
    const float    Sz = sinf( -yaw );
    const float    Cx = cosf( -roll );
    const float    Cy = cosf( -pitch );
    const float    Cz = cosf( -yaw );

    //case ORDER_XYZ:
    ret.m[0][0] = Cy * Cz;
    ret.m[0][1] = -Cy * Sz;
    ret.m[0][2] = Sy;
    ret.m[1][0] = Cz * Sx * Sy + Cx * Sz;
    ret.m[1][1] = Cx * Cz - Sx * Sy * Sz;
    ret.m[1][2] = -Cy * Sx;
    ret.m[2][0] = -Cx * Cz * Sy + Sx * Sz;
    ret.m[2][1] = Cz * Sx + Cx * Sy * Sz;
    ret.m[2][2] = Cx * Cy;

    return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// vaMatrix4x4
///////////////////////////////////////////////////////////////////////////////////////////////////


vaMatrix4x4 vaMatrix4x4::Identity = vaMatrix4x4( 1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1 );

string vaMatrix4x4::ToString( const vaMatrix4x4 & a )
{
    char buffer[1024];
    sprintf_s( buffer, _countof( buffer ), "{%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f}", 
        a._11, a._12, a._13, a._14,
        a._21, a._22, a._23, a._24,
        a._31, a._32, a._33, a._34,
        a._41, a._42, a._43, a._44 );
    return buffer;
}

bool vaMatrix4x4::FromString( const string & str, vaMatrix4x4& outVal )
{
    return sscanf_s( str.c_str( ), "{%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f}",
        &outVal._11, &outVal._12, &outVal._13, &outVal._14,
        &outVal._21, &outVal._22, &outVal._23, &outVal._24,
        &outVal._31, &outVal._32, &outVal._33, &outVal._34,
        &outVal._41, &outVal._42, &outVal._43, &outVal._44 ) == 16;
}

vaMatrix4x4::vaMatrix4x4( const float * p )
{
    assert( p != NULL );
    memcpy( &_11, p, sizeof( vaMatrix4x4 ) );
}

bool vaMatrix4x4::operator == ( const vaMatrix4x4 & mat ) const
{
    return 0 == memcmp( &this->_11, &mat._11, sizeof( vaMatrix4x4 ) );
}

bool vaMatrix4x4::operator != ( const vaMatrix4x4 & mat ) const
{
    return 0 != memcmp( &this->_11, &mat._11, sizeof( vaMatrix4x4 ) );
}

float vaMatrix4x4::Determinant( ) const
{
    vaVector4 minor, v1, v2, v3;
    float det;

    v1.x = m[0][0]; v1.y = m[1][0]; v1.z = m[2][0]; v1.w = m[3][0];
    v2.x = m[0][1]; v2.y = m[1][1]; v2.z = m[2][1]; v2.w = m[3][1];
    v3.x = m[0][2]; v3.y = m[1][2]; v3.z = m[2][2]; v3.w = m[3][2];
    minor = vaVector4::Cross( v1, v2, v3 );
    det = -( m[0][3] * minor.x + m[1][3] * minor.y + m[2][3] * minor.z + m[3][3] * minor.w );

    return det;
}

double vaMatrix4x4::DeterminantD( ) const
{
    vaVector4d minor, v1, v2, v3;
    double det;

    v1.x = m[0][0]; v1.y = m[1][0]; v1.z = m[2][0]; v1.w = m[3][0];
    v2.x = m[0][1]; v2.y = m[1][1]; v2.z = m[2][1]; v2.w = m[3][1];
    v3.x = m[0][2]; v3.y = m[1][2]; v3.z = m[2][2]; v3.w = m[3][2];
    minor = vaVector4d::Cross( v1, v2, v3 );
    det = -( m[0][3] * minor.x + m[1][3] * minor.y + m[2][3] * minor.z + m[3][3] * minor.w );

    return det;
}

vaMatrix3x3 vaMatrix3x3::Transposed( ) const
{
    vaMatrix3x3 ret;
    for( int i = 0; i < 3; i++ )
    {
        for( int j = 0; j < 3; j++ )
        {
            ret.m[i][j] = m[j][i];
        }
    }
    return ret;
}

void vaMatrix4x4::Transpose( )
{
    std::swap( m[1][0], m[0][1] );
    std::swap( m[2][0], m[0][2] );
    std::swap( m[3][0], m[0][3] );
    std::swap( m[2][1], m[1][2] );
    std::swap( m[3][1], m[1][3] );
    std::swap( m[3][2], m[2][3] );
}

vaMatrix4x4 vaMatrix4x4::Transposed( ) const
{
    vaMatrix4x4 ret;
    for( int i = 0; i < 4; i++ )
    {
        for( int j = 0; j < 4; j++ )
        {
            ret.m[i][j] = m[j][i];
        }
    }
    return ret;
}

bool vaMatrix4x4::Inverse( vaMatrix4x4 & outMat, float * outDeterminant ) const
{
    float det = Determinant( );

    if( vaMath::Abs( det ) < VA_EPSf )
        return false;

    if( outDeterminant != NULL )
        *outDeterminant = det;

    int a;
    vaVector4 v, vec[3];

    float sign = 1;
    for( int i = 0; i < 4; i++ )
    {
        for( int j = 0; j < 4; j++ )
        {
            if( j != i )
            {
                a = j;
                if( j > i ) a = a - 1;
                vec[a].x = m[j][0];
                vec[a].y = m[j][1];
                vec[a].z = m[j][2];
                vec[a].w = m[j][3];
            }
        }
        v = vaVector4::Cross( vec[0], vec[1], vec[2] );

        outMat.m[0][i] = (float)( sign * v.x / det );
        outMat.m[1][i] = (float)( sign * v.y / det );
        outMat.m[2][i] = (float)( sign * v.z / det );
        outMat.m[3][i] = (float)( sign * v.w / det );
        sign *= -1;
    }

    return true;
}

// This comes from Eric's blog: https://lxjk.github.io/2017/09/03/Fast-4x4-Matrix-Inverse-with-SSE-SIMD-Explained.html
vaMatrix4x4 vaMatrix4x4::FastTransformInversed( ) const
{
#if 0
    return this->Inversed();
#else

#define SMALL_NUMBER		(1.e-8f)
#define MakeShuffleMask(x,y,z,w)           (x | (y<<2) | (z<<4) | (w<<6))

    // vec(0, 1, 2, 3) -> (vec[x], vec[y], vec[z], vec[w])
#define VecSwizzleMask(vec, mask)          _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(vec), mask))
#define VecSwizzle(vec, x, y, z, w)        VecSwizzleMask(vec, MakeShuffleMask(x,y,z,w))
#define VecSwizzle1(vec, x)                VecSwizzleMask(vec, MakeShuffleMask(x,x,x,x))
// special swizzle
#define VecSwizzle_0022(vec)               _mm_moveldup_ps(vec)
#define VecSwizzle_1133(vec)               _mm_movehdup_ps(vec)

// return (vec1[x], vec1[y], vec2[z], vec2[w])
#define VecShuffle(vec1, vec2, x,y,z,w)    _mm_shuffle_ps(vec1, vec2, MakeShuffleMask(x,y,z,w))
// special shuffle
#define VecShuffle_0101(vec1, vec2)        _mm_movelh_ps(vec1, vec2)
#define VecShuffle_2323(vec1, vec2)        _mm_movehl_ps(vec2, vec1)

    struct alignas(16) SSEM4x4
    {
        union 
        {
            vaMatrix4x4     M;
            __m128          mVec[4];
        };
        SSEM4x4( ) {}
    };
    const SSEM4x4 & inM = *reinterpret_cast<const SSEM4x4*>(this);

	SSEM4x4 r;

	// transpose 3x3, we know m03 = m13 = m23 = 0
	__m128 t0 = VecShuffle_0101(inM.mVec[0], inM.mVec[1]); // 00, 01, 10, 11
	__m128 t1 = VecShuffle_2323(inM.mVec[0], inM.mVec[1]); // 02, 03, 12, 13
	r.mVec[0] = VecShuffle(t0, inM.mVec[2], 0,2,0,3); // 00, 10, 20, 23(=0)
	r.mVec[1] = VecShuffle(t0, inM.mVec[2], 1,3,1,3); // 01, 11, 21, 23(=0)
	r.mVec[2] = VecShuffle(t1, inM.mVec[2], 0,2,2,3); // 02, 12, 22, 23(=0)

	// (SizeSqr(mVec[0]), SizeSqr(mVec[1]), SizeSqr(mVec[2]), 0)
	__m128 sizeSqr;
	sizeSqr =                     _mm_mul_ps(r.mVec[0], r.mVec[0]);
	sizeSqr = _mm_add_ps(sizeSqr, _mm_mul_ps(r.mVec[1], r.mVec[1]));
	sizeSqr = _mm_add_ps(sizeSqr, _mm_mul_ps(r.mVec[2], r.mVec[2]));

	// optional test to avoid divide by 0
	__m128 one = _mm_set1_ps(1.f);
	// for each component, if(sizeSqr < SMALL_NUMBER) sizeSqr = 1;
	__m128 rSizeSqr = _mm_blendv_ps(
		_mm_div_ps(one, sizeSqr),
		one,
		_mm_cmplt_ps(sizeSqr, _mm_set1_ps(SMALL_NUMBER))
		);

	r.mVec[0] = _mm_mul_ps(r.mVec[0], rSizeSqr);
	r.mVec[1] = _mm_mul_ps(r.mVec[1], rSizeSqr);
	r.mVec[2] = _mm_mul_ps(r.mVec[2], rSizeSqr);

	// last line
	r.mVec[3] =                       _mm_mul_ps(r.mVec[0], VecSwizzle1(inM.mVec[3], 0));
	r.mVec[3] = _mm_add_ps(r.mVec[3], _mm_mul_ps(r.mVec[1], VecSwizzle1(inM.mVec[3], 1)));
	r.mVec[3] = _mm_add_ps(r.mVec[3], _mm_mul_ps(r.mVec[2], VecSwizzle1(inM.mVec[3], 2)));
	r.mVec[3] = _mm_sub_ps(_mm_setr_ps(0.f, 0.f, 0.f, 1.f), r.mVec[3]);

#if 0 // simple validation (yeah comparing absolute numbers here is not useful)
    auto A = this->Inversed();
    assert( vaMatrix4x4::NearEqual( A, r.M, 1e-2f ) );
#endif

	return r.M;
#endif

    
}

bool vaMatrix4x4::InverseHighPrecision( vaMatrix4x4 & outMat, double * outDeterminant ) const
{
    double det = Determinant( );

    if( vaMath::Abs( det ) < VA_EPSd )
        return false;

    if( outDeterminant != NULL )
        *outDeterminant = det;

    int a;
    vaVector4d v, vec[3];

    float sign = 1;
    for( int i = 0; i < 4; i++ )
    {
        for( int j = 0; j < 4; j++ )
        {
            if( j != i )
            {
                a = j;
                if( j > i ) a = a - 1;
                vec[a].x = m[j][0];
                vec[a].y = m[j][1];
                vec[a].z = m[j][2];
                vec[a].w = m[j][3];
            }
        }
        v = vaVector4d::Cross( vec[0], vec[1], vec[2] );

        outMat.m[0][i] = (float)( sign * v.x / det );
        outMat.m[1][i] = (float)( sign * v.y / det );
        outMat.m[2][i] = (float)( sign * v.z / det );
        outMat.m[3][i] = (float)( sign * v.w / det );
        sign *= -1;
    }

    return true;
}
bool vaMatrix4x4::Decompose( vaVector3 & outScale, vaQuaternion & outRotation, vaVector3 & outTranslation ) const
{
    vaMatrix3x3 normalizedRotation3x3;
    Decompose( outScale, normalizedRotation3x3, outTranslation );

    outRotation = vaQuaternion::FromRotationMatrix( normalizedRotation3x3 );

    return true;
}

bool vaMatrix4x4::Decompose( vaVector3 & outScale, vaMatrix3x3 & outRotation, vaVector3 & outTranslation ) const
{
    vaVector3 vec;

    // Scaling 
    vec.x = this->m[0][0];
    vec.y = this->m[0][1];
    vec.z = this->m[0][2];
    outScale.x = vec.Length( );

    vec.x = this->m[1][0];
    vec.y = this->m[1][1];
    vec.z = this->m[1][2];
    outScale.y = vec.Length( );

    vec.x = this->m[2][0];
    vec.y = this->m[2][1];
    vec.z = this->m[2][2];
    outScale.z = vec.Length( );

    // Translation
    outTranslation.x = this->m[3][0];
    outTranslation.y = this->m[3][1];
    outTranslation.z = this->m[3][2];

    /*Let's calculate the rotation now*/
    if( ( outScale.x == 0.0f ) || ( outScale.y == 0.0f ) || ( outScale.z == 0.0f ) ) return false;

    outRotation.m[0][0] = this->m[0][0] / outScale.x;
    outRotation.m[0][1] = this->m[0][1] / outScale.x;
    outRotation.m[0][2] = this->m[0][2] / outScale.x;
    outRotation.m[1][0] = this->m[1][0] / outScale.y;
    outRotation.m[1][1] = this->m[1][1] / outScale.y;
    outRotation.m[1][2] = this->m[1][2] / outScale.y;
    outRotation.m[2][0] = this->m[2][0] / outScale.z;
    outRotation.m[2][1] = this->m[2][1] / outScale.z;
    outRotation.m[2][2] = this->m[2][2] / outScale.z;

    return true;
}


void vaMatrix4x4::DecomposeRotationYawPitchRoll( float & yaw, float & pitch, float & roll )
{
    //pitch = (float)vaMath::ASin( -m[2][1] ); 
    pitch = (float)vaMath::ASin( -m[0][2] );

    float threshold = 0.001f;

    float test = (float)vaMath::Cos( pitch );

    if( test > threshold )
    {

        //roll = (float)vaMath::ATan2( m[0][1], m[1][1] ); 
        roll = (float)vaMath::ATan2( m[1][2], m[2][2] );

        //yaw = (float)vaMath::ATan2( m[2][0], m[2][2] );
        yaw = (float)vaMath::ATan2( m[0][1], m[0][0] );

    }
    else
    {
        //roll = (float)vaMath::ATan2( -m[1][0], m[0][0] ); 
        roll = (float)vaMath::ATan2( -m[2][1], m[1][1] );
        yaw = 0.0f;
    }
}

// static

vaMatrix4x4 vaMatrix4x4::Multiply( const vaMatrix4x4 & a, const vaMatrix4x4 & b )
{
    vaMatrix4x4 ret;

    for( int i = 0; i < 4; i++ )
    {
        for( int j = 0; j < 4; j++ )
        {
            ret.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
        }
    }

    return ret;
}

vaMatrix4x4 vaMatrix4x4::Scaling( float sx, float sy, float sz )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = sx;
    ret.m[1][1] = sy;
    ret.m[2][2] = sz;
    return ret;
}

vaMatrix4x4 vaMatrix4x4::Translation( float x, float y, float z )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[3][0] = x;
    ret.m[3][1] = y;
    ret.m[3][2] = z;
    return ret;
}

vaMatrix4x4 vaMatrix4x4::RotationX( float angle )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[1][1] = vaMath::Cos( angle );
    ret.m[2][2] = vaMath::Cos( angle );
    ret.m[1][2] = vaMath::Sin( angle );
    ret.m[2][1] = -vaMath::Sin( angle );
    return ret;
}

vaMatrix4x4 vaMatrix4x4::RotationY( float angle )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = vaMath::Cos( angle );
    ret.m[2][2] = vaMath::Cos( angle );
    ret.m[0][2] = -vaMath::Sin( angle );
    ret.m[2][0] = vaMath::Sin( angle );
    return ret;
}

vaMatrix4x4 vaMatrix4x4::RotationZ( float angle )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = vaMath::Cos( angle );
    ret.m[1][1] = vaMath::Cos( angle );
    ret.m[0][1] = vaMath::Sin( angle );
    ret.m[1][0] = -vaMath::Sin( angle );
    return ret;
}

vaMatrix4x4 vaMatrix4x4::RotationAxis( const vaVector3 & vec, float angle )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    vaVector3 v = vec.Normalized( );

    ret.m[0][0] = ( 1.0f - vaMath::Cos( angle ) ) * v.x * v.x + vaMath::Cos( angle );
    ret.m[1][0] = ( 1.0f - vaMath::Cos( angle ) ) * v.x * v.y - vaMath::Sin( angle ) * v.z;
    ret.m[2][0] = ( 1.0f - vaMath::Cos( angle ) ) * v.x * v.z + vaMath::Sin( angle ) * v.y;
    ret.m[0][1] = ( 1.0f - vaMath::Cos( angle ) ) * v.y * v.x + vaMath::Sin( angle ) * v.z;
    ret.m[1][1] = ( 1.0f - vaMath::Cos( angle ) ) * v.y * v.y + vaMath::Cos( angle );
    ret.m[2][1] = ( 1.0f - vaMath::Cos( angle ) ) * v.y * v.z - vaMath::Sin( angle ) * v.x;
    ret.m[0][2] = ( 1.0f - vaMath::Cos( angle ) ) * v.z * v.x - vaMath::Sin( angle ) * v.y;
    ret.m[1][2] = ( 1.0f - vaMath::Cos( angle ) ) * v.z * v.y + vaMath::Sin( angle ) * v.x;
    ret.m[2][2] = ( 1.0f - vaMath::Cos( angle ) ) * v.z * v.z + vaMath::Cos( angle );

    return ret;
}

vaMatrix4x4 vaMatrix4x4::FromQuaternion( const vaQuaternion & q )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = 1.0f - 2.0f * ( q.y * q.y + q.z * q.z );
    ret.m[0][1] = 2.0f * ( q.x *q.y + q.z * q.w );
    ret.m[0][2] = 2.0f * ( q.x * q.z - q.y * q.w );
    ret.m[1][0] = 2.0f * ( q.x * q.y - q.z * q.w );
    ret.m[1][1] = 1.0f - 2.0f * ( q.x * q.x + q.z * q.z );
    ret.m[1][2] = 2.0f * ( q.y *q.z + q.x *q.w );
    ret.m[2][0] = 2.0f * ( q.x * q.z + q.y * q.w );
    ret.m[2][1] = 2.0f * ( q.y *q.z - q.x *q.w );
    ret.m[2][2] = 1.0f - 2.0f * ( q.x * q.x + q.y * q.y );
    return ret;
}


vaMatrix4x4 vaMatrix4x4::FromYawPitchRoll( float yaw, float pitch, float roll )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;

    const float    Sx = sinf( -roll );
    const float    Sy = sinf( -pitch );
    const float    Sz = sinf( -yaw );
    const float    Cx = cosf( -roll );
    const float    Cy = cosf( -pitch );
    const float    Cz = cosf( -yaw );
    
    //case ORDER_XYZ:
        ret.m[0][0] = Cy*Cz;
        ret.m[0][1] = -Cy*Sz;
        ret.m[0][2] = Sy;
        ret.m[1][0] = Cz*Sx*Sy + Cx*Sz;
        ret.m[1][1] = Cx*Cz - Sx*Sy*Sz;
        ret.m[1][2] = -Cy*Sx;
        ret.m[2][0] = -Cx*Cz*Sy + Sx*Sz;
        ret.m[2][1] = Cz*Sx + Cx*Sy*Sz;
        ret.m[2][2] = Cx*Cy;
    //    break;
    //
    // //case ORDER_YZX:
    //     ret.m[0][0] = Cy*Cz;
    //     ret.m[0][1] = Sx*Sy - Cx*Cy*Sz;
    //     ret.m[0][2] = Cx*Sy + Cy*Sx*Sz;
    //     ret.m[1][0] = Sz;
    //     ret.m[1][1] = Cx*Cz;
    //     ret.m[1][2] = -Cz*Sx;
    //     ret.m[2][0] = -Cz*Sy;
    //     ret.m[2][1] = Cy*Sx + Cx*Sy*Sz;
    //     ret.m[2][2] = Cx*Cy - Sx*Sy*Sz;
    // //    break;
    // //
    // //case ORDER_ZXY:
    //     ret.m[0][0] = Cy*Cz - Sx*Sy*Sz;
    //     ret.m[0][1] = -Cx*Sz;
    //     ret.m[0][2] = Cz*Sy + Cy*Sx*Sz;
    //     ret.m[1][0] = Cz*Sx*Sy + Cy*Sz;
    //     ret.m[1][1] = Cx*Cz;
    //     ret.m[1][2] = -Cy*Cz*Sx + Sy*Sz;
    //     ret.m[2][0] = -Cx*Sy;
    //     ret.m[2][1] = Sx;
    //     ret.m[2][2] = Cx*Cy;
    // //    break;
    // //
    // //case ORDER_ZYX:
    //     ret.m[0][0] = Cy*Cz;
    //     ret.m[0][1] = Cz*Sx*Sy - Cx*Sz;
    //     ret.m[0][2] = Cx*Cz*Sy + Sx*Sz;
    //     ret.m[1][0] = Cy*Sz;
    //     ret.m[1][1] = Cx*Cz + Sx*Sy*Sz;
    //     ret.m[1][2] = -Cz*Sx + Cx*Sy*Sz;
    //     ret.m[2][0] = -Sy;
    //     ret.m[2][1] = Cy*Sx;
    //     ret.m[2][2] = Cx*Cy;
    // //    break;
    // //
    // //case ORDER_YXZ:
    //     ret.m[0][0] = Cy*Cz + Sx*Sy*Sz;
    //     ret.m[0][1] = Cz*Sx*Sy - Cy*Sz;
    //     ret.m[0][2] = Cx*Sy;
    //     ret.m[1][0] = Cx*Sz;
    //     ret.m[1][1] = Cx*Cz;
    //     ret.m[1][2] = -Sx;
    //     ret.m[2][0] = -Cz*Sy + Cy*Sx*Sz;
    //     ret.m[2][1] = Cy*Cz*Sx + Sy*Sz;
    //     ret.m[2][2] = Cx*Cy;
    // //    break;
    // //
    // //case ORDER_XZY:
    //     ret.m[0][0] = Cy*Cz;
    //     ret.m[0][1] = -Sz;
    //     ret.m[0][2] = Cz*Sy;
    //     ret.m[1][0] = Sx*Sy + Cx*Cy*Sz;
    //     ret.m[1][1] = Cx*Cz;
    //     ret.m[1][2] = -Cy*Sx + Cx*Sy*Sz;
    //     ret.m[2][0] = -Cx*Sy + Cy*Sx*Sz;
    //     ret.m[2][1] = Cz*Sx;
    //     ret.m[2][2] = Cx*Cy + Sx*Sy*Sz;
    // //    break;

    return ret;
}

vaMatrix4x4 vaMatrix4x4::LookAtRH( const vaVector3 & inEye, const vaVector3 & inAt, const vaVector3 & inUp )
{
    vaMatrix4x4 ret;
    vaVector3 right, rightn, up, upn, vec, vec2;
    vec2 = inAt - inEye;
    vec = vec2.Normalized( );
    right = vaVector3::Cross( inUp, vec );
    up = vaVector3::Cross( vec, right );
    rightn = right.Normalized( );
    upn = up.Normalized( );
    ret.m[0][0] = -rightn.x;
    ret.m[1][0] = -rightn.y;
    ret.m[2][0] = -rightn.z;
    ret.m[3][0] = vaVector3::Dot( rightn, inEye );
    ret.m[0][1] = upn.x;
    ret.m[1][1] = upn.y;
    ret.m[2][1] = upn.z;
    ret.m[3][1] = -vaVector3::Dot( upn, inEye );
    ret.m[0][2] = -vec.x;
    ret.m[1][2] = -vec.y;
    ret.m[2][2] = -vec.z;
    ret.m[3][2] = vaVector3::Dot( vec, inEye );
    ret.m[0][3] = 0.0f;
    ret.m[1][3] = 0.0f;
    ret.m[2][3] = 0.0f;
    ret.m[3][3] = 1.0f;
    return ret;
}

//using namespace DirectX;

vaMatrix4x4 vaMatrix4x4::LookAtLH( const vaVector3 & inEye, const vaVector3 & inAt, const vaVector3 & inUp )
{
    //    FXMVECTOR EyePosition   = XMLoadFloat3( &XMFLOAT3( &inEye.x ) );
    //    FXMVECTOR FocusPosition = XMLoadFloat3( &XMFLOAT3( &inAt.x ) );
    //    FXMVECTOR UpDirection   = XMLoadFloat3( &XMFLOAT3( &inUp.x ) );
    //    XMMATRIX    mm = XMMatrixLookAtLH( EyePosition, FocusPosition, UpDirection );
    //    XMFLOAT4X4 mmo;
    //    XMStoreFloat4x4( &mmo, mm );

    vaMatrix4x4 ret;
    vaVector3 right, up, vec;
    vec = ( inAt - inEye ).Normalized( );
    right = vaVector3::Cross( inUp, vec ).Normalized( );
    up = vaVector3::Cross( vec, right ).Normalized( );
    ret.m[0][0] = right.x;
    ret.m[1][0] = right.y;
    ret.m[2][0] = right.z;
    ret.m[3][0] = -vaVector3::Dot( right, inEye );
    ret.m[0][1] = up.x;
    ret.m[1][1] = up.y;
    ret.m[2][1] = up.z;
    ret.m[3][1] = -vaVector3::Dot( up, inEye );
    ret.m[0][2] = vec.x;
    ret.m[1][2] = vec.y;
    ret.m[2][2] = vec.z;
    ret.m[3][2] = -vaVector3::Dot( vec, inEye );
    ret.m[0][3] = 0.0f;
    ret.m[1][3] = 0.0f;
    ret.m[2][3] = 0.0f;
    ret.m[3][3] = 1.0f;
    return ret;
}

vaMatrix4x4 vaMatrix4x4::PerspectiveRH( float w, float h, float zn, float zf )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = 2.0f * zn / w;
    ret.m[1][1] = 2.0f * zn / h;
    ret.m[2][2] = zf / ( zn - zf );
    ret.m[3][2] = ( zn * zf ) / ( zn - zf );
    ret.m[2][3] = -1.0f;
    ret.m[3][3] = 0.0f;
    return ret;
}

vaMatrix4x4 vaMatrix4x4::PerspectiveLH( float w, float h, float zn, float zf )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = 2.0f * zn / w;
    ret.m[1][1] = 2.0f * zn / h;
    ret.m[2][2] = zf / ( zf - zn );
    ret.m[3][2] = ( zn * zf ) / ( zn - zf );
    ret.m[2][3] = 1.0f;
    ret.m[3][3] = 0.0f;
    return ret;
}

vaMatrix4x4 vaMatrix4x4::PerspectiveFovRH( float fovy, float aspect, float zn, float zf )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = 1.0f / ( aspect * tan( fovy / 2.0f ) );
    ret.m[1][1] = 1.0f / tan( fovy / 2.0f );
    ret.m[2][2] = zf / ( zn - zf );
    ret.m[2][3] = -1.0f;
    ret.m[3][2] = ( zf * zn ) / ( zn - zf );
    ret.m[3][3] = 0.0f;
    return ret;
}

vaMatrix4x4 vaMatrix4x4::PerspectiveFovLH( float fovy, float aspect, float zn, float zf )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = 1.0f / ( aspect * tan( fovy / 2.0f ) );
    ret.m[1][1] = 1.0f / tan( fovy / 2.0f );
    ret.m[2][2] = zf / ( zf - zn );
    ret.m[2][3] = 1.0f;
    ret.m[3][2] = ( zf * zn ) / ( zn - zf );
    ret.m[3][3] = 0.0f;
    return ret;

    // note:
    // to extract near far planes from matrix returned from this function use
    //  float clipNear1 = -mb / ma;
    //  float clipFar1 = ( ma * clipNear1 ) / ( ma - 1.0f );
    // where ma = m[2][2] and mb = m[3][2]
    // but beware of float precision issues!
}

vaMatrix4x4 vaMatrix4x4::PerspectiveOffCenterRH( float l, float r, float b, float t, float zn, float zf )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = 2.0f * zn / ( r - l );
    ret.m[1][1] = -2.0f * zn / ( b - t );
    ret.m[2][0] = 1.0f + 2.0f * l / ( r - l );
    ret.m[2][1] = -1.0f - 2.0f * t / ( b - t );
    ret.m[2][2] = zf / ( zn - zf );
    ret.m[3][2] = ( zn * zf ) / ( zn - zf );
    ret.m[2][3] = -1.0f;
    ret.m[3][3] = 0.0f;
    return ret;
}

// Build a perspective projection matrix. (left-handed)
vaMatrix4x4 vaMatrix4x4::PerspectiveOffCenterLH( float l, float r, float b, float t, float zn, float zf )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = 2.0f * zn / ( r - l );
    ret.m[1][1] = -2.0f * zn / ( b - t );
    ret.m[2][0] = -1.0f - 2.0f * l / ( r - l );
    ret.m[2][1] = 1.0f + 2.0f * t / ( b - t );
    ret.m[2][2] = -zf / ( zn - zf );
    ret.m[3][2] = ( zn * zf ) / ( zn - zf );
    ret.m[2][3] = 1.0f;
    ret.m[3][3] = 0.0f;
    return ret;
}

vaMatrix4x4 vaMatrix4x4::OrthoRH( float w, float h, float zn, float zf )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = 2.0f / w;
    ret.m[1][1] = 2.0f / h;
    ret.m[2][2] = 1.0f / ( zn - zf );
    ret.m[3][2] = zn / ( zn - zf );
    return ret;
}

vaMatrix4x4 vaMatrix4x4::OrthoLH( float w, float h, float zn, float zf )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = 2.0f / w;
    ret.m[1][1] = 2.0f / h;
    ret.m[2][2] = 1.0f / ( zf - zn );
    ret.m[3][2] = zn / ( zn - zf );
    return ret;
}

vaMatrix4x4 vaMatrix4x4::OrthoOffCenterRH( float l, float r, float b, float t, float zn, float zf )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = 2.0f / ( r - l );
    ret.m[1][1] = 2.0f / ( t - b );
    ret.m[2][2] = 1.0f / ( zf - zn );
    ret.m[3][0] = -1.0f - 2.0f *l / ( r - l );
    ret.m[3][1] = 1.0f + 2.0f * t / ( b - t );
    ret.m[3][2] = zn / ( zn - zf );
    return ret;
}

vaMatrix4x4 vaMatrix4x4::OrthoOffCenterLH( float l, float r, float b, float t, float zn, float zf )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = 2.0f / ( r - l );
    ret.m[1][1] = 2.0f / ( t - b );
    ret.m[2][2] = 1.0f / ( zn - zf );
    ret.m[3][0] = -1.0f - 2.0f *l / ( r - l );
    ret.m[3][1] = 1.0f + 2.0f * t / ( b - t );
    ret.m[3][2] = zn / ( zn - zf );
    return ret;
}

vaMatrix4x4 vaMatrix4x4::Shadow( const vaVector4 & light, const vaPlane & plane )
{
    vaPlane Nplane = plane.PlaneNormalized( );
    float dot = vaPlane::Dot( Nplane, light );

    vaMatrix4x4 ret;
    ret.m[0][0] = dot - Nplane.a * light.x;
    ret.m[0][1] = -Nplane.a * light.y;
    ret.m[0][2] = -Nplane.a * light.z;
    ret.m[0][3] = -Nplane.a * light.w;
    ret.m[1][0] = -Nplane.b * light.x;
    ret.m[1][1] = dot - Nplane.b * light.y;
    ret.m[1][2] = -Nplane.b * light.z;
    ret.m[1][3] = -Nplane.b * light.w;
    ret.m[2][0] = -Nplane.c * light.x;
    ret.m[2][1] = -Nplane.c * light.y;
    ret.m[2][2] = dot - Nplane.c * light.z;
    ret.m[2][3] = -Nplane.c * light.w;
    ret.m[3][0] = -Nplane.d * light.x;
    ret.m[3][1] = -Nplane.d * light.y;
    ret.m[3][2] = -Nplane.d * light.z;
    ret.m[3][3] = dot - Nplane.d * light.w;
    return ret;

}

vaMatrix4x4 vaMatrix4x4::Reflect( const vaPlane & plane )
{
    vaPlane Nplane = plane.PlaneNormalized( );
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.m[0][0] = 1.0f - 2.0f * Nplane.a * Nplane.a;
    ret.m[0][1] = -2.0f * Nplane.a * Nplane.b;
    ret.m[0][2] = -2.0f * Nplane.a * Nplane.c;
    ret.m[1][0] = -2.0f * Nplane.a * Nplane.b;
    ret.m[1][1] = 1.0f - 2.0f * Nplane.b * Nplane.b;
    ret.m[1][2] = -2.0f * Nplane.b * Nplane.c;
    ret.m[2][0] = -2.0f * Nplane.c * Nplane.a;
    ret.m[2][1] = -2.0f * Nplane.c * Nplane.b;
    ret.m[2][2] = 1.0f - 2.0f * Nplane.c * Nplane.c;
    ret.m[3][0] = -2.0f * Nplane.d * Nplane.a;
    ret.m[3][1] = -2.0f * Nplane.d * Nplane.b;
    ret.m[3][2] = -2.0f * Nplane.d * Nplane.c;
    return ret;
}

bool vaMatrix4x4::NearEqual( const vaMatrix4x4 & a, const vaMatrix4x4 & b, float epsilon )
{
    for( int i = 0; i < 4; i++ )
        for( int j = 0; j < 4; j++ )
            if( !vaMath::NearEqual( a.m[i][j], b.m[i][j], epsilon ) )
                return false;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// vaQuaternion
///////////////////////////////////////////////////////////////////////////////////////////////////

vaQuaternion vaQuaternion::Identity = vaQuaternion( 0, 0, 0, 1 );

vaQuaternion vaQuaternion::FromRotationMatrix( const vaMatrix4x4 & mat )
{
    vaQuaternion ret;

#if 0
    int i, maxi;
    float maxdiag, S, trace;

    trace = mat.m[0][0] + mat.m[1][1] + mat.m[2][2] + 1.0f;
    if( trace > 1.0f )
    {
        ret.x = ( mat.m[1][2] - mat.m[2][1] ) / ( 2.0f * sqrt( trace ) );
        ret.y = ( mat.m[2][0] - mat.m[0][2] ) / ( 2.0f * sqrt( trace ) );
        ret.z = ( mat.m[0][1] - mat.m[1][0] ) / ( 2.0f * sqrt( trace ) );
        ret.w = sqrt( trace ) / 2.0f;
        return ret;
    }
    maxi = 0;
    maxdiag = mat.m[0][0];
    for( i = 1; i < 3; i++ )
    {
        if( mat.m[i][i] > maxdiag )
        {
            maxi = i;
            maxdiag = mat.m[i][i];
        }
    }
    switch( maxi )
    {
    case 0:
        S = 2.0f * sqrt( 1.0f + mat.m[0][0] - mat.m[1][1] - mat.m[2][2] );
        ret.x = 0.25f * S;
        ret.y = ( mat.m[0][1] + mat.m[1][0] ) / S;
        ret.z = ( mat.m[0][2] + mat.m[2][0] ) / S;
        ret.w = ( mat.m[1][2] - mat.m[2][1] ) / S;
        break;
    case 1:
        S = 2.0f * sqrt( 1.0f + mat.m[1][1] - mat.m[0][0] - mat.m[2][2] );
        ret.x = ( mat.m[0][1] + mat.m[1][0] ) / S;
        ret.y = 0.25f * S;
        ret.z = ( mat.m[1][2] + mat.m[2][1] ) / S;
        ret.w = ( mat.m[2][0] - mat.m[0][2] ) / S;
        break;
    case 2:
        S = 2.0f * sqrt( 1.0f + mat.m[2][2] - mat.m[0][0] - mat.m[1][1] );
        ret.x = ( mat.m[0][2] + mat.m[2][0] ) / S;
        ret.y = ( mat.m[1][2] + mat.m[2][1] ) / S;
        ret.z = 0.25f * S;
        ret.w = ( mat.m[0][1] - mat.m[1][0] ) / S;
        break;
    }
#else
    // from http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/, converted to row-major
    float trace = mat.m[0][0] + mat.m[1][1] + mat.m[2][2]; // I removed + 1.0f; see discussion with Ethan
    if( trace > 0 ) // I changed M_EPSILON to 0
    {
        float s = 0.5f / sqrtf(trace+ 1.0f);
        ret.w = 0.25f / s;
        ret.x = ( mat.m[1][2] - mat.m[2][1] ) * s;
        ret.y = ( mat.m[2][0] - mat.m[0][2] ) * s;
        ret.z = ( mat.m[0][1] - mat.m[1][0] ) * s;
    } 
    else 
    {
        if ( mat.m[0][0] > mat.m[1][1] && mat.m[0][0] > mat.m[2][2] ) 
        {
            float s = 2.0f * sqrtf( 1.0f + mat.m[0][0] - mat.m[1][1] - mat.m[2][2]);
            ret.w = (mat.m[1][2] - mat.m[2][1] ) / s;
            ret.x = 0.25f * s;
            ret.y = (mat.m[1][0] + mat.m[0][1] ) / s;
            ret.z = (mat.m[2][0] + mat.m[0][2] ) / s;
        } 
        else if (mat.m[1][1] > mat.m[2][2]) 
        {
            float s = 2.0f * sqrtf( 1.0f + mat.m[1][1] - mat.m[0][0] - mat.m[2][2]);
            ret.w = (mat.m[2][0] - mat.m[0][2] ) / s;
            ret.x = (mat.m[1][0] + mat.m[0][1] ) / s;
            ret.y = 0.25f * s;
            ret.z = (mat.m[2][1] + mat.m[1][2] ) / s;
        } 
        else 
        {
            float s = 2.0f * sqrtf( 1.0f + mat.m[2][2] - mat.m[0][0] - mat.m[1][1] );
            ret.w = (mat.m[0][1] - mat.m[1][0] ) / s;
            ret.x = (mat.m[2][0] + mat.m[0][2] ) / s;
            ret.y = (mat.m[2][1] + mat.m[1][2] ) / s;
            ret.z = 0.25f * s;
        }
    }
#endif
    return ret;
}

vaQuaternion vaQuaternion::FromRotationMatrix( const vaMatrix3x3 & mat )
{
    vaQuaternion ret;

#if 0
    int i, maxi;
    float maxdiag, S, trace;

    trace = mat.m[0][0] + mat.m[1][1] + mat.m[2][2] + 1.0f;
    if( trace > 1.0f )
    {
        ret.x = ( mat.m[1][2] - mat.m[2][1] ) / ( 2.0f * sqrt( trace ) );
        ret.y = ( mat.m[2][0] - mat.m[0][2] ) / ( 2.0f * sqrt( trace ) );
        ret.z = ( mat.m[0][1] - mat.m[1][0] ) / ( 2.0f * sqrt( trace ) );
        ret.w = sqrt( trace ) / 2.0f;
        return ret;
    }
    maxi = 0;
    maxdiag = mat.m[0][0];
    for( i = 1; i < 3; i++ )
    {
        if( mat.m[i][i] > maxdiag )
        {
            maxi = i;
            maxdiag = mat.m[i][i];
        }
    }
    switch( maxi )
    {
    case 0:
        S = 2.0f * sqrt( 1.0f + mat.m[0][0] - mat.m[1][1] - mat.m[2][2] );
        ret.x = 0.25f * S;
        ret.y = ( mat.m[0][1] + mat.m[1][0] ) / S;
        ret.z = ( mat.m[0][2] + mat.m[2][0] ) / S;
        ret.w = ( mat.m[1][2] - mat.m[2][1] ) / S;
        break;
    case 1:
        S = 2.0f * sqrt( 1.0f + mat.m[1][1] - mat.m[0][0] - mat.m[2][2] );
        ret.x = ( mat.m[0][1] + mat.m[1][0] ) / S;
        ret.y = 0.25f * S;
        ret.z = ( mat.m[1][2] + mat.m[2][1] ) / S;
        ret.w = ( mat.m[2][0] - mat.m[0][2] ) / S;
        break;
    case 2:
        S = 2.0f * sqrt( 1.0f + mat.m[2][2] - mat.m[0][0] - mat.m[1][1] );
        ret.x = ( mat.m[0][2] + mat.m[2][0] ) / S;
        ret.y = ( mat.m[1][2] + mat.m[2][1] ) / S;
        ret.z = 0.25f * S;
        ret.w = ( mat.m[0][1] - mat.m[1][0] ) / S;
        break;
    }
#else
    // from http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/, converted to row-major
    float trace = mat.m[0][0] + mat.m[1][1] + mat.m[2][2]; // I removed + 1.0f; see discussion with Ethan
    if( trace > 0 ) // I changed M_EPSILON to 0
    {
        float s = 0.5f / sqrtf(trace+ 1.0f);
        ret.w = 0.25f / s;
        ret.x = ( mat.m[1][2] - mat.m[2][1] ) * s;
        ret.y = ( mat.m[2][0] - mat.m[0][2] ) * s;
        ret.z = ( mat.m[0][1] - mat.m[1][0] ) * s;
    } 
    else 
    {
        if ( mat.m[0][0] > mat.m[1][1] && mat.m[0][0] > mat.m[2][2] ) 
        {
            float s = 2.0f * sqrtf( 1.0f + mat.m[0][0] - mat.m[1][1] - mat.m[2][2]);
            ret.w = (mat.m[1][2] - mat.m[2][1] ) / s;
            ret.x = 0.25f * s;
            ret.y = (mat.m[1][0] + mat.m[0][1] ) / s;
            ret.z = (mat.m[2][0] + mat.m[0][2] ) / s;
        } 
        else if (mat.m[1][1] > mat.m[2][2]) 
        {
            float s = 2.0f * sqrtf( 1.0f + mat.m[1][1] - mat.m[0][0] - mat.m[2][2]);
            ret.w = (mat.m[2][0] - mat.m[0][2] ) / s;
            ret.x = (mat.m[1][0] + mat.m[0][1] ) / s;
            ret.y = 0.25f * s;
            ret.z = (mat.m[2][1] + mat.m[1][2] ) / s;
        } 
        else 
        {
            float s = 2.0f * sqrtf( 1.0f + mat.m[2][2] - mat.m[0][0] - mat.m[1][1] );
            ret.w = (mat.m[0][1] - mat.m[1][0] ) / s;
            ret.x = (mat.m[2][0] + mat.m[0][2] ) / s;
            ret.y = (mat.m[2][1] + mat.m[1][2] ) / s;
            ret.z = 0.25f * s;
        }
    }
#endif
    return ret;
}

vaQuaternion vaQuaternion::RotationAxis( const vaVector3 & v, float angle )
{
    vaVector3 temp = v.Normalized( );

    vaQuaternion ret;
    float hsin = vaMath::Sin( angle / 2.0f );
    ret.x = hsin * temp.x;
    ret.y = hsin * temp.y;
    ret.z = hsin * temp.z;
    ret.w = vaMath::Cos( angle / 2.0f );
    return ret;
}

vaQuaternion vaQuaternion::FromYawPitchRoll( float yaw, float pitch, float roll )
{
    vaQuaternion ret;
    ret.x = vaMath::Cos( yaw / 2.0f ) * vaMath::Cos( pitch / 2.0f ) * vaMath::Sin( roll / 2.0f ) - vaMath::Sin( yaw / 2.0f ) * vaMath::Sin( pitch / 2.0f ) * vaMath::Cos( roll / 2.0f );
    ret.y = vaMath::Sin( yaw / 2.0f ) * vaMath::Cos( pitch / 2.0f ) * vaMath::Sin( roll / 2.0f ) + vaMath::Cos( yaw / 2.0f ) * vaMath::Sin( pitch / 2.0f ) * vaMath::Cos( roll / 2.0f );
    ret.z = vaMath::Sin( yaw / 2.0f ) * vaMath::Cos( pitch / 2.0f ) * vaMath::Cos( roll / 2.0f ) - vaMath::Cos( yaw / 2.0f ) * vaMath::Sin( pitch / 2.0f ) * vaMath::Sin( roll / 2.0f );
    ret.w = vaMath::Cos( yaw / 2.0f ) * vaMath::Cos( pitch / 2.0f ) * vaMath::Cos( roll / 2.0f ) + vaMath::Sin( yaw / 2.0f ) * vaMath::Sin( pitch / 2.0f ) * vaMath::Sin( roll / 2.0f );
    return ret;
}

vaQuaternion vaQuaternion::Slerp( const vaQuaternion & q1, const vaQuaternion & q2, float t )
{
    vaQuaternion ret;

    float dot, epsilon, temp, u;

    epsilon = 1.0f;
    temp = 1.0f - t;
    u = t;
    dot = vaQuaternion::Dot( q1, q2 );
    if( dot < 0.0f )
    {
        epsilon = -1.0f;
        dot = -dot;
    }
    if( 1.0f - dot > 0.001f )
    {
        float theta = acos( dot );
        temp = sin( theta * temp ) / sin( theta );
        u = sin( theta * u ) / sin( theta );
    }
    ret.x = temp * q1.x + epsilon * u * q2.x;
    ret.y = temp * q1.y + epsilon * u * q2.y;
    ret.z = temp * q1.z + epsilon * u * q2.z;
    ret.w = temp * q1.w + epsilon * u * q2.w;
    return ret;
}

vaQuaternion vaQuaternion::CatmullRom( const vaQuaternion &v0, const vaQuaternion &v1, const vaQuaternion & v2, const vaQuaternion & v3, float s )
{
   vaQuaternion ret;
   ret.x = 0.5f * (2.0f * v1.x + (v2.x - v0.x) *s + (2.0f * v0.x - 5.0f * v1.x + 4.0f * v2.x - v3.x) * s * s + (v3.x - 3.0f * v2.x + 3.0f * v1.x - v0.x) * s * s * s);
   ret.y = 0.5f * (2.0f * v1.y + (v2.y - v0.y) *s + (2.0f * v0.y - 5.0f * v1.y + 4.0f * v2.y - v3.y) * s * s + (v3.y - 3.0f * v2.y + 3.0f * v1.y - v0.y) * s * s * s);
   ret.z = 0.5f * (2.0f * v1.z + (v2.z - v0.z) *s + (2.0f * v0.z - 5.0f * v1.z + 4.0f * v2.z - v3.z) * s * s + (v3.z - 3.0f * v2.z + 3.0f * v1.z - v0.z) * s * s * s);
   ret.w = 0.5f * (2.0f * v1.w + (v2.w - v0.w) *s + (2.0f * v0.w - 5.0f * v1.w + 4.0f * v2.w - v3.w) * s * s + (v3.w - 3.0f * v2.w + 3.0f * v1.w - v0.w) * s * s * s);
   return ret.Normalized();
}

vaQuaternion vaQuaternion::Squad( const vaQuaternion & q1, const vaQuaternion & q2, const vaQuaternion & q3, const vaQuaternion & q4, float t )
{
    vaQuaternion temp1 = vaQuaternion::Slerp( q1, q4, t );
    vaQuaternion temp2 = vaQuaternion::Slerp( q2, q3, t );

    return vaQuaternion::Slerp( temp2, temp1, 2.0f * t * ( 1.0f - t ) );
}

void vaQuaternion::DecomposeYawPitchRoll( float & yaw, float & pitch, float & roll ) const
{
    // should make a quaternion-specific version at some point... 
    vaMatrix4x4 rotMat = vaMatrix4x4::FromQuaternion( *this );
    rotMat.DecomposeRotationYawPitchRoll( yaw, pitch, roll );
    // (the version below is broken somehow)

    /*
    const float Epsilon = 0.0009765625f;
    const float Threshold = 0.5f - Epsilon;

    float XY = this->x * this->y;
    float ZW = this->z * this->w;

    float TEST = XY + ZW;

    if (TEST < -Threshold || TEST > Threshold)
    {
    int sign = (int)vaMath::Sign( TEST );

    yaw = sign * 2 * (float)vaMath::ATan2( this->x, this->w );

    pitch = sign * VA_PIf / 2.0f;;

    roll = 0;

    }
    else
    {

    float XX = this->x * this->x;
    float XZ = this->x * this->z;
    float XW = this->x * this->w;

    float YY = this->y * this->y;
    float YW = this->y * this->w;
    float YZ = this->y * this->z;

    float ZZ = this->z * this->z;

    yaw = (float)vaMath::ATan2(2 * YW - 2 * XZ, 1 - 2 * YY - 2 * ZZ);

    pitch = (float)vaMath::ATan2(2 * XW - 2 * YZ, 1 - 2 * XX - 2 * ZZ);

    roll = (float)vaMath::ASin(2 * TEST);
    }
    */
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// vaPlane
///////////////////////////////////////////////////////////////////////////////////////////////////

bool vaPlane::IntersectLine( vaVector3 & outPt, const vaVector3 & lineStart, const vaVector3 & lineEnd )
{
    vaVector3 direction = lineStart - lineEnd;

    return IntersectRay( outPt, lineStart, lineEnd );
}

bool vaPlane::IntersectRay( vaVector3 & outPt, const vaVector3 & lineStart, const vaVector3 & direction )
{
    float dot, temp;

    dot = vaVector3::Dot( Normal(), direction );
    if( !dot ) return false;

    temp = ( this->d + vaVector3::Dot( Normal(), lineStart ) ) / dot;
    outPt.x = lineStart.x - temp * direction.x;
    outPt.y = lineStart.y - temp * direction.y;
    outPt.z = lineStart.z - temp * direction.z;

    return true;
}

vaPlane vaPlane::Transform( const vaPlane & plane, const vaMatrix4x4 & mat )
{
    vaPlane ret;
    ret.a = mat.m[0][0] * plane.a + mat.m[1][0] * plane.b + mat.m[2][0] * plane.c + mat.m[3][0] * plane.d;
    ret.b = mat.m[0][1] * plane.a + mat.m[1][1] * plane.b + mat.m[2][1] * plane.c + mat.m[3][1] * plane.d;
    ret.c = mat.m[0][2] * plane.a + mat.m[1][2] * plane.b + mat.m[2][2] * plane.c + mat.m[3][2] * plane.d;
    ret.d = mat.m[0][3] * plane.a + mat.m[1][3] * plane.b + mat.m[2][3] * plane.c + mat.m[3][3] * plane.d;
    return ret;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// vaGeometry
///////////////////////////////////////////////////////////////////////////////////////////////////

float vaGeometry::FresnelTerm( float cosTheta, float refractionIndex )
{
    float a, d, g, result;

    g = vaMath::Sqrt( refractionIndex * refractionIndex + cosTheta * cosTheta - 1.0f );
    a = g + cosTheta;
    d = g - cosTheta;
    result = ( cosTheta * a - 1.0f ) * ( cosTheta * a - 1.0f ) / ( ( cosTheta * d + 1.0f ) * ( cosTheta * d + 1.0f ) ) + 1.0f;
    result = result * 0.5f * d * d / ( a * a );
    return result;
}

void vaGeometry::CalculateFrustumPlanes( vaPlane planes[6], const vaMatrix4x4 & mCameraViewProj )
{
    // Left clipping plane
    planes[0] = vaPlane(
        mCameraViewProj( 0, 3 ) + mCameraViewProj( 0, 0 ),
        mCameraViewProj( 1, 3 ) + mCameraViewProj( 1, 0 ),
        mCameraViewProj( 2, 3 ) + mCameraViewProj( 2, 0 ),
        mCameraViewProj( 3, 3 ) + mCameraViewProj( 3, 0 ) );

    // Right clipping plane
    planes[1] = vaPlane(
        mCameraViewProj( 0, 3 ) - mCameraViewProj( 0, 0 ),
        mCameraViewProj( 1, 3 ) - mCameraViewProj( 1, 0 ),
        mCameraViewProj( 2, 3 ) - mCameraViewProj( 2, 0 ),
        mCameraViewProj( 3, 3 ) - mCameraViewProj( 3, 0 ) );

    // Top clipping plane
    planes[2] = vaPlane(
        mCameraViewProj( 0, 3 ) - mCameraViewProj( 0, 1 ),
        mCameraViewProj( 1, 3 ) - mCameraViewProj( 1, 1 ),
        mCameraViewProj( 2, 3 ) - mCameraViewProj( 2, 1 ),
        mCameraViewProj( 3, 3 ) - mCameraViewProj( 3, 1 ) );

    // Bottom clipping plane
    planes[3] = vaPlane(
        mCameraViewProj( 0, 3 ) + mCameraViewProj( 0, 1 ),
        mCameraViewProj( 1, 3 ) + mCameraViewProj( 1, 1 ),
        mCameraViewProj( 2, 3 ) + mCameraViewProj( 2, 1 ),
        mCameraViewProj( 3, 3 ) + mCameraViewProj( 3, 1 ) );

    // Far clipping plane
    planes[4] = vaPlane(
        mCameraViewProj( 0, 2 ),
        mCameraViewProj( 1, 2 ),
        mCameraViewProj( 2, 2 ),
        mCameraViewProj( 3, 2 ) );

    // Near clipping plane
    planes[5] = vaPlane(
        mCameraViewProj( 0, 3 ) - mCameraViewProj( 0, 2 ),
        mCameraViewProj( 1, 3 ) - mCameraViewProj( 1, 2 ),
        mCameraViewProj( 2, 3 ) - mCameraViewProj( 2, 2 ),
        mCameraViewProj( 3, 3 ) - mCameraViewProj( 3, 2 ) );

    // Normalize the plane equations
    for( int i = 0; i < 6; i++ )
        planes[i] = planes[i].PlaneNormalized( );
}

vaPlane vaPlane::Degenerate( 0, 0, 0, 0 );

#pragma warning( suppress : 4056 4756 )
vaBoundingBox               vaBoundingBox::Degenerate( vaVector3( INFINITY, INFINITY, INFINITY ), vaVector3( -INFINITY, -INFINITY, -INFINITY ) );
#pragma warning( suppress : 4056 4756 )
vaOrientedBoundingBox       vaOrientedBoundingBox::Degenerate( vaVector3( INFINITY, INFINITY, INFINITY ), vaVector3( -INFINITY, -INFINITY, -INFINITY ), vaMatrix3x3( 1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f ) );
#pragma warning( suppress : 4056 4756 )
vaBoundingSphere            vaBoundingSphere::Degenerate( vaVector3( INFINITY, INFINITY, INFINITY ), -INFINITY );

vaBoundingSphere vaBoundingSphere::FromAABB( const vaBoundingBox & aabb )
{
    return { aabb.Center(), aabb.Size.Length() };
}

vaBoundingSphere vaBoundingSphere::FromOBB( const vaOrientedBoundingBox & obb )
{
    return { obb.Center, obb.Extents.Length() };
}

vaBoundingSphere vaBoundingSphere::Transform( const vaBoundingSphere & bs, const vaMatrix4x4 & transform )
{
    // there must be a better way to do this.................
    vaVector3 outScale;
    vaMatrix3x3 outRotation;
    vaVector3 outTranslation;
    transform.Decompose( outScale, outRotation, outTranslation );

    return { vaVector3::TransformCoord( bs.Center, transform ), std::max( std::max( outScale.x, outScale.y ), outScale.z ) * bs.Radius };
}

string vaOrientedBoundingBox::ToString( const vaOrientedBoundingBox & a )
{
    char buffer[1024];
    sprintf_s( buffer, _countof( buffer ), "{{%f,%f,%f},{%f,%f,%f},{%f,%f,%f,%f,%f,%f,%f,%f,%f}",
        a.Center.x, a.Center.y, a.Center.z,
        a.Extents.x, a.Extents.y, a.Extents.z,
        a.Axis._11, a.Axis._12, a.Axis._13,
        a.Axis._21, a.Axis._22, a.Axis._23,
        a.Axis._31, a.Axis._32, a.Axis._33 );
    return buffer;
}

bool vaOrientedBoundingBox::FromString( const string & str, vaOrientedBoundingBox & outVal )
{
    return sscanf_s( str.c_str( ), "{{%f,%f,%f},{%f,%f,%f},{%f,%f,%f,%f,%f,%f,%f,%f,%f}",
        &outVal.Center.x, &outVal.Center.y, &outVal.Center.z,
        &outVal.Extents.x, &outVal.Extents.y, &outVal.Extents.z,
        &outVal.Axis._11, &outVal.Axis._12, &outVal.Axis._13,
        &outVal.Axis._21, &outVal.Axis._22, &outVal.Axis._23,
        &outVal.Axis._31, &outVal.Axis._32, &outVal.Axis._33 ) == (3+3+9);
}

void vaBoundingBox::GetCornerPoints( vaVector3 corners[] ) const
{
    vaVector3 max = Max( );

    corners[0].x = Min.x;
    corners[0].y = Min.y;
    corners[0].z = Min.z;

    corners[1].x = Min.x;
    corners[1].y = max.y;
    corners[1].z = Min.z;

    corners[2].x = max.x;
    corners[2].y = Min.y;
    corners[2].z = Min.z;

    corners[3].x = max.x;
    corners[3].y = max.y;
    corners[3].z = Min.z;

    corners[4].x = Min.x;
    corners[4].y = Min.y;
    corners[4].z = max.z;

    corners[5].x = Min.x;
    corners[5].y = max.y;
    corners[5].z = max.z;

    corners[6].x = max.x;
    corners[6].y = Min.y;
    corners[6].z = max.z;

    corners[7].x = max.x;
    corners[7].y = max.y;
    corners[7].z = max.z;
}

vaIntersectType vaBoundingSphere::IntersectFrustum( const vaPlane planes[], const int planeCount ) const
{
    vaIntersectType result = vaIntersectType::Inside;

    for( int i = 0; i < planeCount; i++ ) 
    {
        float centDist = vaPlane::DotCoord( planes[i], Center );
        if( centDist < -Radius )
            return vaIntersectType::Outside;
        else if( centDist < Radius )
            result = vaIntersectType::Intersect;
    }
    return result ;
}

vaIntersectType vaBoundingBox::IntersectFrustum( const vaPlane planes[], const int planeCount ) const
{
    assert( planes != nullptr || planeCount == 0 );

    vaVector3 corners[9];
    GetCornerPoints(corners);
    corners[8] = Center();

    vaVector3 boxSize = Size;
    float size = Size.Length();

    // test box's bounding sphere against all planes - removes some false positives, adds one more check
    for( int p = 0; p < planeCount; p++ )
    {
        float centDist = vaPlane::DotCoord( planes[p], corners[8] );
        if( centDist < -size / 2 )
            return vaIntersectType::Outside;
    }

    int totalIn = 0;
    size /= 6.0f; //reduce size to 1/4 (half of radius) for more precision!! // tweaked to 1/6, more sensible

    // test all 8 corners and 9th center point against the planes
    // if all points are behind 1 specific plane, we are out
    // if we are in with all points, then we are fully in
    for( int p = 0; p < planeCount; p++ )
    {

        int inCount = 9;
        int ptIn = 1;

        for( int i = 0; i < 9; ++i )
        {

            // test this point against the planes
            float distance = vaPlane::DotCoord( planes[p], corners[i] );
            if( distance < -size )
            {
                ptIn = 0;
                inCount--;
            }
        }

        // were all the points outside of plane p?
        if( inCount == 0 )
        {
            //assert( completelyIn == false );
            return vaIntersectType::Outside;
        }

        // check if they were all on the right side of the plane
        totalIn += ptIn;
    }

    if( totalIn == planeCount )
        return vaIntersectType::Inside;

    return vaIntersectType::Intersect;
}

bool vaBoundingBox::PointInside( const vaVector3 & point ) const
{
    vaVector3 max = Max();
    return point.x >= Min.x && point.y >= Min.y && point.z >= Min.z && point.x <= max.x && point.y <= max.y && point.z <= max.z;
}

void vaColor::NormalizeLuminance( vaVector3 & inoutColor, float & inoutIntensity )
{
    float luminance = vaVector3::LinearToLuminance( inoutColor );
    if( luminance < VA_EPSf )
    {
        inoutColor = { 1,1,1 };
        inoutIntensity = 0.0f;
    }
    else
    {
        inoutColor /= luminance;
        inoutIntensity *= luminance;
    }
}
