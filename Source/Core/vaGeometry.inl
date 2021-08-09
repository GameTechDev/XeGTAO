///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////
// vaVector2
///////////////////////////////////////////////////////////////////////////////////////////////////


inline vaVector2::vaVector2( const vaVector2i & c ) : x((float)c.x), y((float)c.y)          
{ 
}

inline vaVector4::vaVector4( const vaVector4i & c ) : x( (float)c.x ), y( (float)c.y ), z( (float)c.z ), w( (float)c.w )
{
}

// assignment
inline vaVector2&       vaVector2::operator += ( const vaVector2& v )
{
    x += v.x;
    y += v.y;
    return *this;
}

inline vaVector2&       vaVector2::operator -= ( const vaVector2& v )
{
    x -= v.x;
    y -= v.y;
    return *this;
}

inline vaVector2&       vaVector2::operator *= ( float f )
{
    x *= f;
    y *= f;
    return *this;
}

inline vaVector2&       vaVector2::operator /= ( float f )
{
    float oneOverF = 1.0f / f;
    x *= oneOverF;
    y *= oneOverF;
    return *this;
}

// unary
inline vaVector2        vaVector2::operator + () const
{
    return *this;
}

inline vaVector2        vaVector2::operator - () const
{
    return vaVector2(-x, -y);
}

// binary
inline vaVector2        vaVector2::operator + ( const vaVector2& v ) const
{
    return vaVector2(x + v.x, y + v.y);
}

inline vaVector2        vaVector2::operator - ( const vaVector2& v ) const
{
    return vaVector2(x - v.x, y - v.y);
}

inline vaVector2        vaVector2::operator * ( float f ) const
{
    return vaVector2(x * f, y * f);
}

inline vaVector2        vaVector2::operator / ( float f ) const
{
    float oneOverF = 1.0f / f;
    return vaVector2(x * oneOverF, y * oneOverF);
}

inline bool             vaVector2::operator == ( const vaVector2& v ) const
{
    return x == v.x && y == v.y;
}

inline bool             vaVector2::operator != ( const vaVector2& v ) const
{
    return x != v.x || y != v.y;
}

// friend
inline vaVector2 operator * ( float f, const vaVector2& v )
{
    return vaVector2(f * v.x, f * v.y);
}

// other
inline float         vaVector2::Length( ) const
{ 
   return vaMath::Sqrt( x * x + y * y ); 
}

inline float         vaVector2::LengthSq( ) const
{ 
   return x * x + y * y;
}

inline vaVector2     vaVector2::Normalized( ) const
{ 
   vaVector2 ret;
   float length = Length();

   if ( length < VA_EPSf )
   {
      ret.x = 0.0f;
      ret.y = 0.0f;
   }
   else
   {
      ret.x = this->x / length;
      ret.y = this->y / length;
   }

   return ret;
}

inline vaVector2     vaVector2::ComponentAbs( ) const
{
    return vaVector2( vaMath::Abs( x ), vaMath::Abs( y ) );
}

// static

inline float         vaVector2::Dot( const vaVector2 & a, const vaVector2 & b )
{
   return a.x * b.x + a.y * b.y;
}

inline float         vaVector2::Cross( const vaVector2 & a, const vaVector2 & b )
{
   return a.x * b.y - a.y * b.x; // ( vaVector3( a, 0 ) cross vaVector3( b, 0 ) )
}

inline bool         vaVector2::NearEqual( const vaVector2 & a, const vaVector2 & b, float epsilon )
{
    vaVector2 r = ( a - b ).ComponentAbs( );
    return ( r.x < epsilon ) && ( r.y < epsilon );
}

inline vaVector2     vaVector2::ComponentMul( const vaVector2 & a, const vaVector2 & b )
{
    return vaVector2( a.x * b.x, a.y * b.y );
}

inline vaVector2     vaVector2::ComponentDiv( const vaVector2 & a, const vaVector2 & b )
{
    return vaVector2( a.x / b.x, a.y / b.y );
}

inline vaVector2     vaVector2::ComponentMin( const vaVector2 & a, const vaVector2 & b )
{
   return vaVector2( vaMath::Min( a.x, b.x ), vaMath::Min( a.y, b.y ) );
}

inline vaVector2     vaVector2::ComponentMax( const vaVector2 & a, const vaVector2 & b )
{
   return vaVector2( vaMath::Max( a.x, b.x ), vaMath::Max( a.y, b.y ) );
}

inline vaVector2     vaVector2::BaryCentric( const vaVector2 & v1, const vaVector2 & v2, const vaVector2 & v3, float f, float g )
{
   return v1 + f * ( v2 - v1 ) + g * ( v3 - v1 );
}

inline vaVector2     vaVector2::Hermite( const vaVector2 & v1, const vaVector2 & t1, const vaVector2 & v2, const vaVector2 & t2, float s )
{
   float h1, h2, h3, h4;
 
   h1 = 2.0f * s * s * s - 3.0f * s * s + 1.0f;
   h2 = s * s * s - 2.0f * s * s + s;
   h3 = -2.0f * s * s * s + 3.0f * s * s;
   h4 = s * s * s - s * s;
 
   vaVector2 ret;
   ret.x = h1 * v1.x + h2 * t1.x + h3 * v2.x + h4 * t2.x;
   ret.y = h1 * v1.y + h2 * t1.y + h3 * v2.y + h4 * t2.y;
   return ret;
}

inline vaVector2     vaVector2::CatmullRom( const vaVector2 & v0, const vaVector2 & v1, const vaVector2 & v2, const vaVector2 & v3, float s )
{
   vaVector2 ret;
   ret.x = 0.5f * (2.0f * v1.x + (v2.x - v0.x) *s + (2.0f * v0.x - 5.0f * v1.x + 4.0f * v2.x - v3.x) * s * s + (v3.x - 3.0f * v2.x + 3.0f * v1.x - v0.x) * s * s * s);
   ret.y = 0.5f * (2.0f * v1.y + (v2.y - v0.y) *s + (2.0f * v0.y - 5.0f * v1.y + 4.0f * v2.y - v3.y) * s * s + (v3.y - 3.0f * v2.y + 3.0f * v1.y - v0.y) * s * s * s);
   return ret;
}

inline vaVector4     vaVector2::Transform( const vaVector2 & v, const vaMatrix4x4 & mat )
{
   vaVector4 ret;
   ret.x = mat.m[0][0] * v.x + mat.m[1][0] * v.y  + mat.m[3][0];
   ret.y = mat.m[0][1] * v.x + mat.m[1][1] * v.y  + mat.m[3][1];
   ret.z = mat.m[0][2] * v.x + mat.m[1][2] * v.y  + mat.m[3][2];
   ret.w = mat.m[0][3] * v.x + mat.m[1][3] * v.y  + mat.m[3][3];
   return ret;
}

inline vaVector2     vaVector2::TransformCoord( const vaVector2 & v, const vaMatrix4x4 & mat )
{
   vaVector2 ret;
   float norm = mat.m[0][3] * v.x + mat.m[1][3] * v.y + mat.m[3][3];
   ret.x = (mat.m[0][0] * v.x + mat.m[1][0] * v.y + mat.m[3][0]) / norm;
   ret.y = (mat.m[0][1] * v.x + mat.m[1][1] * v.y + mat.m[3][1]) / norm;
   return ret;
}

inline vaVector2     vaVector2::TransformNormal( const vaVector2 & v, const vaMatrix4x4 & mat )
{
   vaVector2 ret;
   ret.x = mat.m[0][0] * v.x + mat.m[1][0] * v.y;
   ret.y = mat.m[0][1] * v.x + mat.m[1][1] * v.y;
   return ret;
}

inline vaVector2 vaVector2::RandomPointOnCircle( vaRandom & randomGeneratorToUse )
{
    float a = randomGeneratorToUse.NextFloatRange( 0.0f, VA_PIf * 2.0f );
    return vaVector2( vaMath::Cos( a ), vaMath::Sin( a ) );
}

inline vaVector2 vaVector2::RandomPointOnDisk( vaRandom & randomGeneratorToUse )
{
    vaVector2 ptOnCircle = RandomPointOnCircle( randomGeneratorToUse );

    float r = randomGeneratorToUse.NextFloat();

    // to get uniform distribution on the disk
    r = vaMath::Sqrt(r);

    return ptOnCircle * r;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// vaVector3
///////////////////////////////////////////////////////////////////////////////////////////////////

// assignment operators
inline vaVector3 &   vaVector3::operator += ( const vaVector3 & v )
{
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
}

inline vaVector3 &   vaVector3::operator -= ( const vaVector3 & v )
{
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
}

inline vaVector3 &   vaVector3::operator *= ( float f )
{
    x *= f;
    y *= f;
    z *= f;
    return *this;
}

inline vaVector3 &   vaVector3::operator /= ( float f )
{
    float oneOverF = 1.0f / f;
    x *= oneOverF;
    y *= oneOverF;
    z *= oneOverF;
    return *this;
}

// unary
inline vaVector3     vaVector3::operator + () const
{
    return *this;
}

inline vaVector3     vaVector3::operator - () const
{
    return vaVector3( -x, -y, -z );
}

// binary operators
inline vaVector3     vaVector3::operator + ( const vaVector3 & v ) const
{
    return vaVector3( x + v.x, y + v.y, z + v.z );
}

inline vaVector3     vaVector3::operator - ( const vaVector3 & v ) const
{
    return vaVector3( x - v.x, y - v.y, z - v.z );
}

inline vaVector3     vaVector3::operator * ( const vaVector3 & v ) const
{
    return vaVector3( x * v.x, y * v.y, z * v.z );
}

inline vaVector3     vaVector3::operator / ( const vaVector3 & v ) const
{
    return vaVector3( x / v.x, y / v.y, z / v.z );
}

inline vaVector3     vaVector3::operator + ( float f ) const
{
    return vaVector3( x + f, y + f, z + f );
}

inline vaVector3     vaVector3::operator - ( float f ) const
{
    return vaVector3( x - f, y - f, z - f );
}

inline vaVector3     vaVector3::operator * ( float f ) const
{
    return vaVector3( x * f, y * f, z * f );
}

inline vaVector3     vaVector3::operator / ( float f ) const
{
    float oneOverF = 1.0f / f;
    return vaVector3( x * oneOverF, y * oneOverF, z * oneOverF );
}

// friend
inline vaVector3 operator * ( float f, const class vaVector3 & v )
{
    return vaVector3(f * v.x, f * v.y, f * v.z);
}

inline bool          vaVector3::operator == ( const vaVector3 & v ) const
{
    return x == v.x && y == v.y && z == v.z;
}

inline bool          vaVector3::operator != ( const vaVector3 & v ) const
{
    return x != v.x || y != v.y || z != v.z;
}

// other
inline float         vaVector3::Length( ) const
{ 
   return vaMath::Sqrt( x * x + y * y + z * z ); 
}
inline float         vaVector3::LengthSq( ) const
{ 
   return x * x + y * y + z * z;
}
inline vaVector3     vaVector3::Normalized( ) const
{ 
   vaVector3 ret;
   float length = Length();   

   if ( length < VA_EPSf )
   {
      ret.x = 0.0f;
      ret.y = 0.0f;
      ret.z = 0.0f;
   }
   else
   {
      ret.x = this->x / length;
      ret.y = this->y / length;
      ret.z = this->z / length;
   }

   return ret;
}

inline bool vaVector3::IsUnit( float epsilon ) const
{
    return vaMath::Abs( Length( ) - 1.0f ) <= epsilon;
}

inline vaVector3     vaVector3::ComponentAbs( ) const
{
    return vaVector3( vaMath::Abs(x), vaMath::Abs(y), vaMath::Abs(z) );
}

// static

inline float         vaVector3::Dot( const vaVector3 & a, const vaVector3 & b )
{
   return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline vaVector3     vaVector3::Cross( const vaVector3 & a, const vaVector3 & b )
{
   vaVector3 ret;
   ret.x = a.y * b.z - a.z * b.y;
   ret.y = a.z * b.x - a.x * b.z;
   ret.z = a.x * b.y - a.y * b.x;
   return ret;
}

inline bool vaVector3::NearEqual( const vaVector3 & a, const vaVector3 & b, float epsilon )
{
    vaVector3 r = ( a-b ).ComponentAbs();
    return (r.x < epsilon) && (r.y < epsilon) && (r.z < epsilon);
}

inline vaVector3     vaVector3::ComponentMul( const vaVector3 & a, const vaVector3 & b )
{
    return vaVector3( a.x * b.x, a.y * b.y, a.z * b.z );
}

inline vaVector3     vaVector3::ComponentDiv( const vaVector3 & a, const vaVector3 & b )
{
    return vaVector3( a.x / b.x, a.y / b.y, a.z / b.z );
}

inline vaVector3     vaVector3::ComponentMin( const vaVector3 & a, const vaVector3 & b )
{
   return vaVector3( vaMath::Min( a.x, b.x ), vaMath::Min( a.y, b.y ), vaMath::Min( a.z, b.z ) );
}

inline vaVector3     vaVector3::ComponentMax( const vaVector3 & a, const vaVector3 & b )
{
   return vaVector3( vaMath::Max( a.x, b.x ), vaMath::Max( a.y, b.y ), vaMath::Max( a.z, b.z ) );
}

inline vaVector3     vaVector3::Saturate( const vaVector3 & a )
{
    return vaVector3( vaMath::Saturate(a.x), vaMath::Saturate(a.y), vaMath::Saturate(a.z) );
}

inline vaVector3     vaVector3::BaryCentric( const vaVector3 & v1, const vaVector3 & v2, const vaVector3 & v3, float f, float g )
{
   return v1 + f * ( v2 - v1 ) + g * ( v3 - v1 );
}

inline vaVector3     vaVector3::TriangleNormal( const vaVector3& a, const vaVector3& b, const vaVector3& c, bool counterClockwise )
{
    if( counterClockwise )
        return vaVector3::Cross( c - a, b - a ).Normalized();
    else
        return vaVector3::Cross( b - a, c - a ).Normalized();
}

inline vaVector3     vaVector3::Lerp( const vaVector3 & v1, const vaVector3 & v2, float s )
{
    return v1 + (v2 - v1) * s;
}

inline vaVector3     vaVector3::Hermite( const vaVector3 & v1, const vaVector3 & t1, const vaVector3 & v2, const vaVector3 &t2, float s )
{
   float h1, h2, h3, h4;

   h1 = 2.0f * s * s * s - 3.0f * s * s + 1.0f;
   h2 = s * s * s - 2.0f * s * s + s;
   h3 = -2.0f * s * s * s + 3.0f * s * s;
   h4 = s * s * s - s * s;

   vaVector3 ret;
   ret.x = h1 * v1.x + h2 * t1.x + h3 * v2.x + h4 * t2.x;
   ret.y = h1 * v1.y + h2 * t1.y + h3 * v2.y + h4 * t2.y;
   ret.z = h1 * v1.z + h2 * t1.z + h3 * v2.z + h4 * t2.z;
   return ret;
}

inline vaVector3     vaVector3::CatmullRom( const vaVector3 &v0, const vaVector3 &v1, const vaVector3 & v2, const vaVector3 & v3, float s )
{
   vaVector3 ret;
   ret.x = 0.5f * (2.0f * v1.x + (v2.x - v0.x) *s + (2.0f * v0.x - 5.0f * v1.x + 4.0f * v2.x - v3.x) * s * s + (v3.x - 3.0f * v2.x + 3.0f * v1.x - v0.x) * s * s * s);
   ret.y = 0.5f * (2.0f * v1.y + (v2.y - v0.y) *s + (2.0f * v0.y - 5.0f * v1.y + 4.0f * v2.y - v3.y) * s * s + (v3.y - 3.0f * v2.y + 3.0f * v1.y - v0.y) * s * s * s);
   ret.z = 0.5f * (2.0f * v1.z + (v2.z - v0.z) *s + (2.0f * v0.z - 5.0f * v1.z + 4.0f * v2.z - v3.z) * s * s + (v3.z - 3.0f * v2.z + 3.0f * v1.z - v0.z) * s * s * s);
   return ret;
}

inline vaVector4     vaVector3::Transform( const vaVector3 & v, const vaMatrix4x4 & mat )
{
   vaVector4 ret;
   ret.x = mat.m[0][0] * v.x + mat.m[1][0] * v.y + mat.m[2][0] * v.z + mat.m[3][0];
   ret.y = mat.m[0][1] * v.x + mat.m[1][1] * v.y + mat.m[2][1] * v.z + mat.m[3][1];
   ret.z = mat.m[0][2] * v.x + mat.m[1][2] * v.y + mat.m[2][2] * v.z + mat.m[3][2];
   ret.w = mat.m[0][3] * v.x + mat.m[1][3] * v.y + mat.m[2][3] * v.z + mat.m[3][3];
   return ret;
}

inline vaVector3     vaVector3::TransformCoord( const vaVector3 & v, const vaMatrix4x4 & mat )
{
   vaVector3 ret;
   float norm = mat.m[0][3] * v.x + mat.m[1][3] * v.y + mat.m[2][3] * v.z + mat.m[3][3];
   ret.x = (mat.m[0][0] * v.x + mat.m[1][0] * v.y + mat.m[2][0] * v.z + mat.m[3][0]) / norm;
   ret.y = (mat.m[0][1] * v.x + mat.m[1][1] * v.y + mat.m[2][1] * v.z + mat.m[3][1]) / norm;
   ret.z = (mat.m[0][2] * v.x + mat.m[1][2] * v.y + mat.m[2][2] * v.z + mat.m[3][2]) / norm;
   return ret;
}

inline vaVector3     vaVector3::TransformNormal( const vaVector3 & v, const vaMatrix4x4 & mat )
{
   vaVector3 ret;
   ret.x = mat.m[0][0] * v.x + mat.m[1][0] * v.y + mat.m[2][0] * v.z;
   ret.y = mat.m[0][1] * v.x + mat.m[1][1] * v.y + mat.m[2][1] * v.z;
   ret.z = mat.m[0][2] * v.x + mat.m[1][2] * v.y + mat.m[2][2] * v.z;
   return ret;
}

inline vaVector3     vaVector3::TransformNormal( const vaVector3 & v, const vaMatrix3x3 & mat )
{
    vaVector3 ret;
    ret.x = mat.m[0][0] * v.x + mat.m[1][0] * v.y + mat.m[2][0] * v.z;
    ret.y = mat.m[0][1] * v.x + mat.m[1][1] * v.y + mat.m[2][1] * v.z;
    ret.z = mat.m[0][2] * v.x + mat.m[1][2] * v.y + mat.m[2][2] * v.z;
    return ret;
}

inline vaVector3     vaVector3::Random( vaRandom & randomGeneratorToUse )
{
    return vaVector3( randomGeneratorToUse.NextFloat(), randomGeneratorToUse.NextFloat(), randomGeneratorToUse.NextFloat() );
}

inline vaVector3     vaVector3::RandomNormal( vaRandom & randomGeneratorToUse )
{
    float a = randomGeneratorToUse.NextFloatRange( 0.0f, VA_PIf * 2.0f );
    float z = randomGeneratorToUse.NextFloatRange( -1.0f, 1.0f );

    vaVector3 ret;
    ret.x = vaMath::Sqrt( 1.0f - z*z ) * vaMath::Cos( a );
    ret.y = vaMath::Sqrt( 1.0f - z*z ) * vaMath::Sin( a );
    ret.z = z;
    return ret;
}

inline float vaVector3::AngleBetweenVectors( const vaVector3 & a, const vaVector3 & b )
{
    return vaMath::ACos( vaVector3::Dot( a, b ) / (a.Length() * b.Length()) );
}

inline vaVector3     vaVector3::Project( const vaVector3 & v, const vaViewport & viewport, 
   const vaMatrix4x4 & projection, const vaMatrix4x4 & view, const vaMatrix4x4 & world)
{
   vaVector3 ret;
   vaMatrix4x4 m = vaMatrix4x4::Multiply(world, view);
   m = vaMatrix4x4::Multiply(m, projection);
   ret = vaVector3::TransformCoord(v, m);
   ret.x = viewport.X +  ( 1.0f + ret.x ) * viewport.Width / 2.0f;
   ret.y = viewport.Y +  ( 1.0f - ret.y ) * viewport.Height / 2.0f;
   ret.z = viewport.MinDepth + ret.z * ( viewport.MaxDepth - viewport.MinDepth );
   return ret;
}

inline vaVector3 vaVector3::Unproject( const vaVector3 & v, const vaViewport & viewport,
         const vaMatrix4x4 & projection, const vaMatrix4x4 & view, const vaMatrix4x4 & world)
{
   vaVector3 ret;
   vaMatrix4x4 m = vaMatrix4x4::Multiply(world, view);
   m = vaMatrix4x4::Multiply(m, projection);
   m.Inverse(m, NULL);
   ret.x = 2.0f * ( v.x - viewport.X ) / viewport.Width - 1.0f;
   ret.y = 1.0f - 2.0f * ( v.y - viewport.Y ) / viewport.Height;
   ret.z = ( v.z - viewport.MinDepth) / ( viewport.MaxDepth - viewport.MinDepth );
   ret = vaVector3::TransformCoord(ret, m);
   return ret;
}

inline void vaVector3::ComputeOrthonormalBasis( const vaVector3 & n, vaVector3 & b1, vaVector3 & b2 )
{
    // from https://graphics.pixar.com/library/OrthonormalB/paper.pdf
    float sign = copysignf(1.0f, n.z);
    const float a = -1.0f / (sign + n.z);
    const float b = n.x * n.y * a;
    b1 = vaVector3(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
    b2 = vaVector3(b, sign + n.y * n.y * a, -n.y);
}

inline vaVector3 vaVector3::LinearToSRGB( const vaVector3& colour )
{
    return vaVector3( vaColor::LinearToSRGB( colour.x ), vaColor::LinearToSRGB( colour.y ), vaColor::LinearToSRGB( colour.z ) );
}

inline vaVector3 vaVector3::SRGBToLinear( const vaVector3& colour )
{
    return vaVector3( vaColor::SRGBToLinear( colour.x ), vaColor::SRGBToLinear( colour.y ), vaColor::SRGBToLinear( colour.z ) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// vaVector4
///////////////////////////////////////////////////////////////////////////////////////////////////

// assignment
inline vaVector4 &   vaVector4::operator += ( const vaVector4 & v )
{
    x += v.x;
    y += v.y;
    z += v.z;
    w += v.w;
    return *this;
}

inline vaVector4 &   vaVector4::operator -= ( const vaVector4 & v )
{
    x -= v.x;
    y -= v.y;
    z -= v.z;
    w -= v.w;
    return *this;
}

inline vaVector4 &   vaVector4::operator *= ( float f )
{
    x *= f;
    y *= f;
    z *= f;
    w *= f;
    return *this;
}

inline vaVector4 &   vaVector4::operator /= ( float f )
{
    float oneOverF = 1.0f / f;
    x *= oneOverF;
    y *= oneOverF;
    z *= oneOverF;
    w *= oneOverF;
    return *this;
}

// unary
inline vaVector4  vaVector4::operator + () const
{
    return *this;
}

inline vaVector4  vaVector4::operator - () const
{
    return vaVector4(-x, -y, -z, -w);
}

// binary
inline vaVector4  vaVector4::operator + ( const vaVector4 & v ) const
{
    return vaVector4(x + v.x, y + v.y, z + v.z, w + v.w);
}

inline vaVector4  vaVector4::operator - ( const vaVector4 & v ) const
{
    return vaVector4(x - v.x, y - v.y, z - v.z, w - v.w);
}

inline vaVector4  vaVector4::operator * ( const vaVector4 & v ) const
{
    return vaVector4( x * v.x, y * v.y, z * v.z, w * v.w );
}

inline vaVector4  vaVector4::operator / ( const vaVector4 & v ) const
{
    return vaVector4( x / v.x, y / v.y, z / v.z, w / v.w );
}

inline vaVector4  vaVector4::operator + ( float f ) const
{
    return vaVector4( x + f, y + f, z + f, w + f );
}

inline vaVector4  vaVector4::operator - ( float f ) const
{
    return vaVector4( x - f, y - f, z - f, w - f );
}

inline vaVector4  vaVector4::operator * ( float f ) const
{
    return vaVector4(x * f, y * f, z * f, w * f);
}

inline vaVector4  vaVector4::operator / ( float f ) const
{
    float oneOverF = 1.0f / f;
    return vaVector4(x * oneOverF, y * oneOverF, z * oneOverF, w * oneOverF);
}

// friend
inline vaVector4  operator * ( float f, const vaVector4 & v )
{
    return vaVector4(f * v.x, f * v.y, f * v.z, f * v.w);
}

inline bool vaVector4::operator == ( const vaVector4& v ) const
{
    return x == v.x && y == v.y && z == v.z && w == v.w;
}

inline bool vaVector4::operator != ( const vaVector4& v ) const
{
    return x != v.x || y != v.y || z != v.z || w != v.w;
}

// other

inline float         vaVector4::Length( ) const
{ 
   return vaMath::Sqrt( x * x + y * y + z * z + w * w ); 
}
inline float         vaVector4::LengthSq( ) const
{ 
   return x * x + y * y + z * z + w * w;
}
inline vaVector4     vaVector4::Normalized( ) const
{ 
   vaVector4 ret;
   float length = Length();   

   if ( length < VA_EPSf )
   {
      ret.x = 0.0f;
      ret.y = 0.0f;
      ret.z = 0.0f;
      ret.w = 0.0f;
   }
   else
   {
      ret.x = this->x / length;
      ret.y = this->y / length;
      ret.z = this->z / length;
      ret.w = this->w / length;
   }

   return ret;
}


// static

inline float         vaVector4::Dot( const vaVector4 & a, const vaVector4 & b )
{
   return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

inline vaVector4     vaVector4::Cross( const vaVector4 & a, const vaVector4 & b, const vaVector4 & c )
{
    vaVector4 ret;
    ret.x = a.y * (b.z * c.w - c.z * b.w) - a.z * (b.y * c.w - c.y * b.w) + a.w * (b.y * c.z - b.z *c.y);
    ret.y = -(a.x * (b.z * c.w - c.z * b.w) - a.z * (b.x * c.w - c.x * b.w) + a.w * (b.x * c.z - c.x * b.z));
    ret.z = a.x * (b.y * c.w - c.y * b.w) - a.y * (b.x *c.w - c.x * b.w) + a.w * (b.x * c.y - c.x * b.y);
    ret.w = -(a.x * (b.y * c.z - c.y * b.z) - a.y * (b.x * c.z - c.x *b.z) + a.z * (b.x * c.y - c.x * b.y));
    return ret;
}

inline vaVector4d     vaVector4d::Cross( const vaVector4d & a, const vaVector4d & b, const vaVector4d & c )
{
    vaVector4d ret;
    ret.x = a.y * ( b.z * c.w - c.z * b.w ) - a.z * ( b.y * c.w - c.y * b.w ) + a.w * ( b.y * c.z - b.z *c.y );
    ret.y = -( a.x * ( b.z * c.w - c.z * b.w ) - a.z * ( b.x * c.w - c.x * b.w ) + a.w * ( b.x * c.z - c.x * b.z ) );
    ret.z = a.x * ( b.y * c.w - c.y * b.w ) - a.y * ( b.x *c.w - c.x * b.w ) + a.w * ( b.x * c.y - c.x * b.y );
    ret.w = -( a.x * ( b.y * c.z - c.y * b.z ) - a.y * ( b.x * c.z - c.x *b.z ) + a.z * ( b.x * c.y - c.x * b.y ) );
    return ret;
}

inline vaVector4     vaVector4::ComponentMul( const vaVector4 & a, const vaVector4 & b )
{
    return vaVector4( a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w );
}

inline vaVector4     vaVector4::ComponentDiv( const vaVector4 & a, const vaVector4 & b )
{
    return vaVector4( a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w );
}

inline vaVector4     vaVector4::ComponentMin( const vaVector4 & a, const vaVector4 & b )
{
   return vaVector4( vaMath::Min( a.x, b.x ), vaMath::Min( a.y, b.y ), vaMath::Min( a.z, b.z ), vaMath::Min( a.w, b.w ) );
}

inline vaVector4     vaVector4::ComponentMax( const vaVector4 & a, const vaVector4 & b )
{
   return vaVector4( vaMath::Max( a.x, b.x ), vaMath::Max( a.y, b.y ), vaMath::Max( a.z, b.z ), vaMath::Max( a.w, b.w ) );
}

inline vaVector4     vaVector4::Saturate( const vaVector4& a )
{
    return vaVector4( vaMath::Saturate( a.x ), vaMath::Saturate( a.y ), vaMath::Saturate( a.z ), vaMath::Saturate( a.w ) );
}

inline vaVector4     vaVector4::BaryCentric( const vaVector4 & v1, const vaVector4 & v2, const vaVector4 & v3, float f, float g )
{
   return v1 + f * ( v2 - v1 ) + g * ( v3 - v1 );
}

inline vaVector4     vaVector4::Hermite( const vaVector4 & v1, const vaVector4 &t1, const vaVector4 &v2, const vaVector4 &t2, float s )
{
   vaVector4 ret;
   float h1, h2, h3, h4;

   h1 = 2.0f * s * s * s - 3.0f * s * s + 1.0f;
   h2 = s * s * s - 2.0f * s * s + s;
   h3 = -2.0f * s * s * s + 3.0f * s * s;
   h4 = s * s * s - s * s;

   ret.x = h1 * v1.x + h2 * t1.x + h3 * v2.x + h4 * t2.x;
   ret.y = h1 * v1.y + h2 * t1.y + h3 * v2.y + h4 * t2.y;
   ret.z = h1 * v1.z + h2 * t1.z + h3 * v2.z + h4 * t2.z;
   ret.w = h1 * v1.w + h2 * t1.w + h3 * v2.w + h4 * t2.w;
   return ret;
}

inline vaVector4     vaVector4::CatmullRom( const vaVector4 & v0, const vaVector4 & v1, const vaVector4 & v2, const vaVector4 & v3, float s )
{
   vaVector4 ret;
   ret.y = 0.5f * (2.0f * v1.y + (v2.y - v0.y) *s + (2.0f * v0.y - 5.0f * v1.y + 4.0f * v2.y - v3.y) * s * s + (v3.y - 3.0f * v2.y + 3.0f * v1.y - v0.y) * s * s * s);
   ret.z = 0.5f * (2.0f * v1.z + (v2.z - v0.z) *s + (2.0f * v0.z - 5.0f * v1.z + 4.0f * v2.z - v3.z) * s * s + (v3.z - 3.0f * v2.z + 3.0f * v1.z - v0.z) * s * s * s);
   ret.w = 0.5f * (2.0f * v1.w + (v2.w - v0.w) *s + (2.0f * v0.w - 5.0f * v1.w + 4.0f * v2.w - v3.w) * s * s + (v3.w - 3.0f * v2.w + 3.0f * v1.w - v0.w) * s * s * s);
   ret.x = 0.5f * (2.0f * v1.x + (v2.x - v0.x) *s + (2.0f * v0.x - 5.0f * v1.x + 4.0f * v2.x - v3.x) * s * s + (v3.x - 3.0f * v2.x + 3.0f * v1.x - v0.x) * s * s * s);
   return ret;
}

inline vaVector4     vaVector4::Transform( const vaVector4 & v, const vaMatrix4x4 & mat )
{
   vaVector4 ret;
   ret.x = mat.m[0][0] * v.x + mat.m[1][0] * v.y + mat.m[2][0] * v.z + mat.m[3][0] * v.w;
   ret.y = mat.m[0][1] * v.x + mat.m[1][1] * v.y + mat.m[2][1] * v.z + mat.m[3][1] * v.w;
   ret.z = mat.m[0][2] * v.x + mat.m[1][2] * v.y + mat.m[2][2] * v.z + mat.m[3][2] * v.w;
   ret.w = mat.m[0][3] * v.x + mat.m[1][3] * v.y + mat.m[2][3] * v.z + mat.m[3][3] * v.w;
   return ret;
}

inline vaVector4     vaVector4::Random( vaRandom & randomGeneratorToUse )
{
    return vaVector4( randomGeneratorToUse.NextFloat( ), randomGeneratorToUse.NextFloat( ), randomGeneratorToUse.NextFloat( ), randomGeneratorToUse.NextFloat( ) );
}

inline vaVector4     vaVector4::FromBGRA( uint32 colour )
{
   vaVector4 ret;
   const float f = 1.0f / 255.0f;
   ret.x = f * (float) (unsigned char) (colour >> 16);
   ret.y = f * (float) (unsigned char) (colour >>  8);
   ret.z = f * (float) (unsigned char) (colour >>  0);
   ret.w = f * (float) (unsigned char) (colour >> 24);
   return ret;
}

inline vaVector4     vaVector4::FromRGBA( uint32 colour )
{
    vaVector4 ret;
    const float f = 1.0f / 255.0f;
    ret.x = f * (float) (unsigned char) (colour >>  0);
    ret.y = f * (float) (unsigned char) (colour >>  8);
    ret.z = f * (float) (unsigned char) (colour >> 16);
    ret.w = f * (float) (unsigned char) (colour >> 24);
    return ret;
}

inline vaVector4     vaVector4::FromABGR( uint32 colour )
{
   vaVector4 ret;
   const float f = 1.0f / 255.0f;
   ret.x = f * (float) (unsigned char) (colour >> 24);
   ret.y = f * (float) (unsigned char) (colour >> 16);
   ret.z = f * (float) (unsigned char) (colour >>  8);
   ret.w = f * (float) (unsigned char) (colour >>  0);
   return ret;
}

inline uint32       vaVector4::ToBGRA( const vaVector4 & colour )
{
    uint32 ret = 0;
    ret += (0xFF & (int)(colour.x * 255.0f + 0.5)) << 16;
    ret += (0xFF & (int)(colour.y * 255.0f + 0.5)) <<  8;
    ret += (0xFF & (int)(colour.z * 255.0f + 0.5)) <<  0;
    ret += (0xFF & (int)(colour.w * 255.0f + 0.5)) << 24;
    return ret;
}

inline uint32       vaVector4::ToRGBA( const vaVector4 & colour )
{
    uint32 ret = 0;
    ret += ( 0xFF & (int)( colour.x * 255.0f + 0.5 ) ) << 0;
    ret += ( 0xFF & (int)( colour.y * 255.0f + 0.5 ) ) << 8;
    ret += ( 0xFF & (int)( colour.z * 255.0f + 0.5 ) ) << 16;
    ret += ( 0xFF & (int)( colour.w * 255.0f + 0.5 ) ) << 24;
    return ret;
}

inline uint32       vaVector4::ToABGR( const vaVector4 & colour )
{
    uint32 ret = 0;
    ret += (0xFF & (int)(colour.x * 255.0f + 0.5)) << 24;
    ret += (0xFF & (int)(colour.y * 255.0f + 0.5)) << 16;
    ret += (0xFF & (int)(colour.z * 255.0f + 0.5)) <<  8;
    ret += (0xFF & (int)(colour.w * 255.0f + 0.5)) <<  0;
    return ret;
}

inline vaVector4 vaVector4::LinearToSRGB( const vaVector4& colour )
{
    return vaVector4( vaColor::LinearToSRGB( colour.x ), vaColor::LinearToSRGB( colour.y ), vaColor::LinearToSRGB( colour.z ), colour.w );
}

inline vaVector4 vaVector4::SRGBToLinear( const vaVector4& colour )
{
    return vaVector4( vaColor::SRGBToLinear( colour.x ), vaColor::SRGBToLinear( colour.y ), vaColor::SRGBToLinear( colour.z ), colour.w );
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// vaMatrix3x3
///////////////////////////////////////////////////////////////////////////////////////////////////


inline vaMatrix3x3::vaMatrix3x3( float f11, float f12, float f13,
                                 float f21, float f22, float f23,
                                 float f31, float f32, float f33 )
{
    _11 = f11; _12 = f12; _13 = f13; 
    _21 = f21; _22 = f22; _23 = f23; 
    _31 = f31; _32 = f32; _33 = f33; 
}

inline vaMatrix3x3::vaMatrix3x3( const vaMatrix4x4 & t )
{ 
    *this = t.GetRotationMatrix3x3( ); 
}

inline vaMatrix3x3 vaMatrix3x3::FromQuaternion( const vaQuaternion & q )      
{ 
    vaMatrix3x3 ret;
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

inline vaMatrix3x3 &    vaMatrix3x3::operator *= ( const vaMatrix3x3 & mat )
{
   *this = vaMatrix3x3::Multiply( *this, mat );
   return *this;
}

inline vaMatrix3x3 &    vaMatrix3x3::operator += ( const vaMatrix3x3 & mat )
{
    _11 += mat._11; _12 += mat._12; _13 += mat._13;
    _21 += mat._21; _22 += mat._22; _23 += mat._23;
    _31 += mat._31; _32 += mat._32; _33 += mat._33;
    return *this;
}

inline vaMatrix3x3 &    vaMatrix3x3::operator -= ( const vaMatrix3x3 & mat )
{
    _11 -= mat._11; _12 -= mat._12; _13 -= mat._13;
    _21 -= mat._21; _22 -= mat._22; _23 -= mat._23;
    _31 -= mat._31; _32 -= mat._32; _33 -= mat._33;
    return *this;
}

inline vaMatrix3x3 &    vaMatrix3x3::operator *= ( float f )
{
    _11 *= f; _12 *= f; _13 *= f; 
    _21 *= f; _22 *= f; _23 *= f; 
    _31 *= f; _32 *= f; _33 *= f; 
    return *this;
}

inline vaMatrix3x3 &    vaMatrix3x3::operator /= ( float f )
{
    float oneOverF = 1.0f / f;
    _11 *= oneOverF; _12 *= oneOverF; _13 *= oneOverF;
    _21 *= oneOverF; _22 *= oneOverF; _23 *= oneOverF;
    _31 *= oneOverF; _32 *= oneOverF; _33 *= oneOverF;
    return *this;
}

// binary
inline vaMatrix3x3      vaMatrix3x3::operator * ( const vaMatrix3x3 & mat ) const
{
   return vaMatrix3x3::Multiply( *this, mat );
}

inline vaMatrix3x3      vaMatrix3x3::operator + ( const vaMatrix3x3 & mat ) const
{
    return vaMatrix3x3( _11 + mat._11, _12 + mat._12, _13 + mat._13, 
                        _21 + mat._21, _22 + mat._22, _23 + mat._23, 
                        _31 + mat._31, _32 + mat._32, _33 + mat._33 );
}

inline vaMatrix3x3      vaMatrix3x3::operator - ( const vaMatrix3x3 & mat ) const
{
    return vaMatrix3x3( _11 - mat._11, _12 - mat._12, _13 - mat._13,
                        _21 - mat._21, _22 - mat._22, _23 - mat._23,
                        _31 - mat._31, _32 - mat._32, _33 - mat._33 );
}

inline vaMatrix3x3      vaMatrix3x3::operator * ( float f ) const
{
    return vaMatrix3x3( _11 * f, _12 * f, _13 * f,
                        _21 * f, _22 * f, _23 * f,
                        _31 * f, _32 * f, _33 * f );
}

inline vaMatrix3x3      vaMatrix3x3::operator / ( float f ) const
{
    float oneOverF = 1.0f / f;
    return vaMatrix3x3( _11 * oneOverF, _12 * oneOverF, _13 * oneOverF,
                        _21 * oneOverF, _22 * oneOverF, _23 * oneOverF,
                        _31 * oneOverF, _32 * oneOverF, _33 * oneOverF );
}

inline vaMatrix3x3      operator * ( float f, const vaMatrix3x3 & mat )
{
    return vaMatrix3x3( f * mat._11, f * mat._12, f * mat._13,
                        f * mat._21, f * mat._22, f * mat._23,
                        f * mat._31, f * mat._32, f * mat._33 );
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// vaMatrix4x4
///////////////////////////////////////////////////////////////////////////////////////////////////


inline vaMatrix4x4::vaMatrix4x4( float f11, float f12, float f13, float f14,
                                 float f21, float f22, float f23, float f24,
                                 float f31, float f32, float f33, float f34,
                                 float f41, float f42, float f43, float f44 )
{
    _11 = f11; _12 = f12; _13 = f13; _14 = f14;
    _21 = f21; _22 = f22; _23 = f23; _24 = f24;
    _31 = f31; _32 = f32; _33 = f33; _34 = f34;
    _41 = f41; _42 = f42; _43 = f43; _44 = f44;
}

inline vaMatrix4x4 &    vaMatrix4x4::operator *= ( const vaMatrix4x4 & mat )
{
   *this = vaMatrix4x4::Multiply( *this, mat );
   return *this;
}

inline vaMatrix4x4 &    vaMatrix4x4::operator += ( const vaMatrix4x4 & mat )
{
    _11 += mat._11; _12 += mat._12; _13 += mat._13; _14 += mat._14;
    _21 += mat._21; _22 += mat._22; _23 += mat._23; _24 += mat._24;
    _31 += mat._31; _32 += mat._32; _33 += mat._33; _34 += mat._34;
    _41 += mat._41; _42 += mat._42; _43 += mat._43; _44 += mat._44;
    return *this;
}

inline vaMatrix4x4 &    vaMatrix4x4::operator -= ( const vaMatrix4x4 & mat )
{
    _11 -= mat._11; _12 -= mat._12; _13 -= mat._13; _14 -= mat._14;
    _21 -= mat._21; _22 -= mat._22; _23 -= mat._23; _24 -= mat._24;
    _31 -= mat._31; _32 -= mat._32; _33 -= mat._33; _34 -= mat._34;
    _41 -= mat._41; _42 -= mat._42; _43 -= mat._43; _44 -= mat._44;
    return *this;
}

inline vaMatrix4x4 &    vaMatrix4x4::operator *= ( float f )
{
    _11 *= f; _12 *= f; _13 *= f; _14 *= f;
    _21 *= f; _22 *= f; _23 *= f; _24 *= f;
    _31 *= f; _32 *= f; _33 *= f; _34 *= f;
    _41 *= f; _42 *= f; _43 *= f; _44 *= f;
    return *this;
}

inline vaMatrix4x4 &    vaMatrix4x4::operator /= ( float f )
{
    float oneOverF = 1.0f / f;
    _11 *= oneOverF; _12 *= oneOverF; _13 *= oneOverF; _14 *= oneOverF;
    _21 *= oneOverF; _22 *= oneOverF; _23 *= oneOverF; _24 *= oneOverF;
    _31 *= oneOverF; _32 *= oneOverF; _33 *= oneOverF; _34 *= oneOverF;
    _41 *= oneOverF; _42 *= oneOverF; _43 *= oneOverF; _44 *= oneOverF;
    return *this;
}

// binary
inline vaMatrix4x4      vaMatrix4x4::operator * ( const vaMatrix4x4 & mat ) const
{
   return vaMatrix4x4::Multiply( *this, mat );
}

inline vaMatrix4x4      vaMatrix4x4::operator + ( const vaMatrix4x4 & mat ) const
{
    return vaMatrix4x4( _11 + mat._11, _12 + mat._12, _13 + mat._13, _14 + mat._14,
                        _21 + mat._21, _22 + mat._22, _23 + mat._23, _24 + mat._24,
                        _31 + mat._31, _32 + mat._32, _33 + mat._33, _34 + mat._34,
                        _41 + mat._41, _42 + mat._42, _43 + mat._43, _44 + mat._44 );
}

inline vaMatrix4x4      vaMatrix4x4::operator - ( const vaMatrix4x4 & mat ) const
{
    return vaMatrix4x4( _11 - mat._11, _12 - mat._12, _13 - mat._13, _14 - mat._14,
                        _21 - mat._21, _22 - mat._22, _23 - mat._23, _24 - mat._24,
                        _31 - mat._31, _32 - mat._32, _33 - mat._33, _34 - mat._34,
                        _41 - mat._41, _42 - mat._42, _43 - mat._43, _44 - mat._44 );
}

inline vaMatrix4x4      vaMatrix4x4::operator * ( float f ) const
{
    return vaMatrix4x4( _11 * f, _12 * f, _13 * f, _14 * f,
                        _21 * f, _22 * f, _23 * f, _24 * f,
                        _31 * f, _32 * f, _33 * f, _34 * f,
                        _41 * f, _42 * f, _43 * f, _44 * f );
}

inline vaMatrix4x4      vaMatrix4x4::operator / ( float f ) const
{
    float oneOverF = 1.0f / f;
    return vaMatrix4x4( _11 * oneOverF, _12 * oneOverF, _13 * oneOverF, _14 * oneOverF,
                        _21 * oneOverF, _22 * oneOverF, _23 * oneOverF, _24 * oneOverF,
                        _31 * oneOverF, _32 * oneOverF, _33 * oneOverF, _34 * oneOverF,
                        _41 * oneOverF, _42 * oneOverF, _43 * oneOverF, _44 * oneOverF );
}

inline vaMatrix4x4      operator * ( float f, const vaMatrix4x4 & mat )
{
    return vaMatrix4x4( f * mat._11, f * mat._12, f * mat._13, f * mat._14,
                        f * mat._21, f * mat._22, f * mat._23, f * mat._24,
                        f * mat._31, f * mat._32, f * mat._33, f * mat._34,
                        f * mat._41, f * mat._42, f * mat._43, f * mat._44 );
}

// static

// Build a matrix from rotation & translation
inline vaMatrix4x4 vaMatrix4x4::FromRotationTranslation( const vaQuaternion & rot, const vaVector3 & trans )
{
    vaMatrix4x4 ret = vaMatrix4x4::FromQuaternion( rot );
    ret.SetTranslation( trans );
    return ret;
}

// Build a matrix from translation
inline vaMatrix4x4 vaMatrix4x4::FromTranslation( const vaVector3& trans )
{
    vaMatrix4x4 ret = vaMatrix4x4::Identity;
    ret.SetTranslation( trans );
    return ret;
}

// Build a matrix from rotation & translation
inline vaMatrix4x4 vaMatrix4x4::FromRotationTranslation( const vaMatrix3x3 & rot, const vaVector3 & trans )
{
    vaMatrix4x4 ret;
    ret.SetRotation( rot );
    ret.SetTranslation( trans );
    return ret;
}

// Build a matrix from rotation & translation & scale
inline vaMatrix4x4 vaMatrix4x4::FromScaleRotationTranslation( const vaVector3 & scale, const vaQuaternion & rot, const vaVector3 & trans )
{
    vaMatrix4x4 ret = vaMatrix4x4::FromQuaternion( rot );
    ret.SetTranslation( trans );
    ret = vaMatrix4x4::Scaling( scale ) * ret;
    return ret;
}

// Build a matrix from rotation & translation & scale
inline vaMatrix4x4 vaMatrix4x4::FromScaleRotationTranslation( const vaVector3 & scale, const vaMatrix3x3 & rot, const vaVector3 & trans )
{
    vaMatrix4x4 ret;
    ret.SetRotation( rot );
    ret.SetTranslation( trans );
    ret = vaMatrix4x4::Scaling( scale ) * ret;
    return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// vaQuaternion
///////////////////////////////////////////////////////////////////////////////////////////////////

// assignment 
inline vaQuaternion & vaQuaternion::operator += ( const vaQuaternion & q )
{
    x += q.x;
    y += q.y;
    z += q.z;
    w += q.w;
    return *this;
}

inline vaQuaternion & vaQuaternion::operator -= ( const vaQuaternion& q )
{
    x -= q.x;
    y -= q.y;
    z -= q.z;
    w -= q.w;
    return *this;
}

inline vaQuaternion & vaQuaternion::operator *= ( const vaQuaternion& q )
{
   *this = vaQuaternion::Multiply( *this, q );
    return *this;
}

inline vaQuaternion & vaQuaternion::operator *= ( float f )
{
    x *= f;
    y *= f;
    z *= f;
    w *= f;
    return *this;
}

inline vaQuaternion & vaQuaternion::operator /= ( float f )
{
    float oneOverF = 1.0f / f;
    x *= oneOverF;
    y *= oneOverF;
    z *= oneOverF;
    w *= oneOverF;
    return *this;
}

// unary
inline vaQuaternion vaQuaternion::operator + () const
{
    return *this;
}

inline vaQuaternion vaQuaternion::operator - () const
{
    return vaQuaternion(-x, -y, -z, -w);
}

// binary
inline vaQuaternion vaQuaternion::operator + ( const vaQuaternion & q ) const
{
    return vaQuaternion(x + q.x, y + q.y, z + q.z, w + q.w);
}

inline vaQuaternion vaQuaternion::operator - ( const vaQuaternion & q ) const
{
    return vaQuaternion(x - q.x, y - q.y, z - q.z, w - q.w);
}

inline vaQuaternion vaQuaternion::operator * ( const vaQuaternion & q ) const
{
   return vaQuaternion::Multiply( *this, q );
}

inline vaQuaternion vaQuaternion::operator * ( float f ) const
{
    return vaQuaternion(x * f, y * f, z * f, w * f);
}

inline vaQuaternion vaQuaternion::operator / ( float f ) const
{
    float oneOverF = 1.0f / f;
    return vaQuaternion(x * oneOverF, y * oneOverF, z * oneOverF, w * oneOverF);
}

inline vaQuaternion operator * ( float f, const vaQuaternion & q )
{
    return vaQuaternion(f * q.x, f * q.y, f * q.z, f * q.w);
}

inline bool vaQuaternion::operator == ( const vaQuaternion & q ) const
{
    return x == q.x && y == q.y && z == q.z && w == q.w;
}

inline bool vaQuaternion::operator != ( const vaQuaternion & q ) const
{
    return x != q.x || y != q.y || z != q.z || w != q.w;
}

// static
inline vaQuaternion vaQuaternion::Multiply( const vaQuaternion & a, const vaQuaternion & b )
{
   vaQuaternion ret;
   ret.x = b.w * a.x + b.x * a.w + b.y * a.z - b.z * a.y;
   ret.y = b.w * a.y - b.x * a.z + b.y * a.w + b.z * a.x;
   ret.z = b.w * a.z + b.x * a.y - b.y * a.x + b.z * a.w;
   ret.w = b.w * a.w - b.x * a.x - b.y * a.y - b.z * a.z;
   return ret;
}

inline vaQuaternion vaQuaternion::Normalized( ) const
{
   return (*this) / Length();
}

inline vaQuaternion vaQuaternion::Inversed( ) const
{
   vaQuaternion ret;
   
   float norm = LengthSq( );
   
   ret.x = -this->x / norm;
   ret.y = -this->y / norm;
   ret.z = -this->z / norm;
   ret.w = this->w / norm;
   
   return ret;
}

inline float vaQuaternion::Length( ) const
{
   return vaMath::Sqrt( x * x + y * y + z * z + w * w );
}

inline float vaQuaternion::LengthSq( ) const
{
   return x * x + y * y + z * z + w * w;
}

inline vaQuaternion vaQuaternion::Conjugate( ) const
{
   return vaQuaternion( -x, -y, -z, w );
}

inline void vaQuaternion::ToAxisAngle( vaVector3 & outAxis, float & outAngle ) const
{
   // http://www.euclideanspace.com/maths/geometry/rotations/conversions/quaternionToAngle/
   // although here we're assuming quaternion is already normalized
   
   outAngle = 2.0f * vaMath::ACos( this->w );
   float s = vaMath::Sqrt( 1-w*w ); // assuming quaternion normalized then w is less than 1, so term always positive.
   if (s < 0.001) 
   { // test to avoid divide by zero, s is always positive due to sqrt
     // if s close to zero then direction of axis not important
     outAxis.x = x; // if it is important that axis is normalised then replace with x=1; y=z=0;
     outAxis.y = y;
     outAxis.z = z;
   } else 
   {
     outAxis.x = x / s; // normalize axis
     outAxis.y = y / s;
     outAxis.z = z / s;
   }
}

inline vaQuaternion vaQuaternion::Ln( ) const
{
   vaQuaternion ret;

   float norm, normvec, theta;

   norm = LengthSq( );

   if ( norm > 1.0001f )
   {
      ret.x = this->x;
      ret.y = this->y;
      ret.z = this->z;
      ret.w = 0.0f;
   }
   else if( norm > 0.99999f)
   {
      normvec = vaMath::Sqrt( this->x * this->x + this->y * this->y + this->z * this->z );
      theta = vaMath::ATan2( normvec, this->w ) / normvec;
      ret.x = theta * this->x;
      ret.y = theta * this->y;
      ret.z = theta * this->z;
      ret.w = 0.0f;
   }
   else
   {
      assert( false );
      // FIXME("The quaternion (%f, %f, %f, %f) has a norm <1. This should not happen. Windows returns a result anyway. This case is not implemented yet.\n", pq->x, pq->y, pq->z, pq->w);
   }

   return ret;
}

inline vaQuaternion vaQuaternion::Exp( ) const
{
   vaQuaternion ret;
   float norm;
   
   norm = vaMath::Sqrt( this->x * this->x + this->y * this->y + this->z * this->z );
   if (norm )
   {
    ret.x = vaMath::Sin(norm) * this->x / norm;
    ret.y = vaMath::Sin(norm) * this->y / norm;
    ret.z = vaMath::Sin(norm) * this->z / norm;
    ret.w = vaMath::Cos(norm);
   }
   else
   {
    ret.x = 0.0f;
    ret.y = 0.0f;
    ret.z = 0.0f;
    ret.w = 1.0f;
   }
   return ret;
}

inline vaVector3 vaQuaternion::GetAxisX( ) const
{
    vaVector3 ret;

    ret[0] = 1.0f - 2.0f * ( this->y * this->y + this->z * this->z );
    ret[1] = 2.0f * ( this->x *this->y + this->z * this->w );
    ret[2] = 2.0f * ( this->x * this->z - this->y * this->w );

    return ret;
}

inline vaVector3 vaQuaternion::GetAxisY( ) const
{
    vaVector3 ret;

    ret[0] = 2.0f * ( this->x * this->y - this->z * this->w );
    ret[1] = 1.0f - 2.0f * ( this->x * this->x + this->z * this->z );
    ret[2] = 2.0f * ( this->y *this->z + this->x *this->w );

    return ret;
}

inline vaVector3 vaQuaternion::GetAxisZ( ) const
{
    vaVector3 ret;

    ret[0] = 2.0f * ( this->x * this->z + this->y * this->w );
    ret[1] = 2.0f * ( this->y *this->z - this->x *this->w );
    ret[2] = 1.0f - 2.0f * ( this->x * this->x + this->y * this->y );

    return ret;
}

// static

inline float vaQuaternion::Dot( const vaQuaternion & a, const vaQuaternion & b )
{
   return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

inline vaQuaternion vaQuaternion::BaryCentric( const vaQuaternion & q1, const vaQuaternion & q2, const vaQuaternion & q3, float f, float g )
{
   vaQuaternion temp1   = vaQuaternion::Slerp( q1, q2, f + g );
   vaQuaternion temp2   = vaQuaternion::Slerp( q1, q3, f + g );
   return vaQuaternion::Slerp( temp1, temp2, g / (f + g) );
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// vaPlane
///////////////////////////////////////////////////////////////////////////////////////////////////

// assignment 
inline vaPlane & vaPlane::operator *= ( float f )
{
    a *= f;
    b *= f;
    c *= f;
    d *= f;
    return *this;
}

inline vaPlane & vaPlane::operator /= ( float f )
{
    float oneOverF = 1.0f / f;
    a *= oneOverF;
    b *= oneOverF;
    c *= oneOverF;
    d *= oneOverF;
    return *this;
}

// unary
inline vaPlane vaPlane::operator + () const
{
    return *this;
}

inline vaPlane vaPlane::operator - () const
{
    return vaPlane(-a, -b, -c, -d);
}

// binary operators
inline vaPlane vaPlane::operator * ( float f ) const
{
    return vaPlane(a * f, b * f, c * f, d * f);
}

inline vaPlane vaPlane::operator / ( float f ) const
{
    float oneOverF = 1.0f / f;
    return vaPlane(a * oneOverF, b * oneOverF, c * oneOverF, d * oneOverF);
}

inline vaPlane operator * (float f, const vaPlane & p )
{
    return vaPlane(f * p.a, f * p.b, f * p.c, f * p.d);
}

inline bool vaPlane::operator == ( const vaPlane & p ) const
{
    return a == p.a && b == p.b && c == p.c && d == p.d;
}

inline bool vaPlane::operator != ( const vaPlane & p ) const
{
    return a != p.a || b != p.b || c != p.c || d != p.d;
}

inline float vaPlane::Dot( const vaPlane & plane, const vaVector4 & v )
{
   return plane.a * v.x + plane.b * v.y + plane.c * v.z + plane.d * v.w;
}

inline float vaPlane::DotCoord( const vaPlane & plane, const vaVector3 & v )
{
   return plane.a * v.x + plane.b * v.y + plane.c * v.z + plane.d;
}

inline float vaPlane::DotNormal( const vaPlane & plane, const vaVector3 & v )
{
   return plane.a * v.x + plane.b * v.y + plane.c * v.z;
}

inline vaPlane vaPlane::PlaneNormalized( ) const
{
   vaPlane ret;
   float norm;

   norm = vaMath::Sqrt( this->a * this->a + this->b * this->b + this->c * this->c );
   if ( norm )
   {
      ret.a = this->a / norm;
      ret.b = this->b / norm;
      ret.c = this->c / norm;
      ret.d = this->d / norm;
   }
   else
   {
      ret.a = 0.0f;
      ret.b = 0.0f;
      ret.c = 0.0f;
      ret.d = 0.0f;
   }

   return ret;
}

inline vaPlane vaPlane::FromPointNormal( const vaVector3 & point, const vaVector3 & normal )
{
   vaPlane ret;
   ret.a = normal.x;
   ret.b = normal.y;
   ret.c = normal.z;
   ret.d = -vaVector3::Dot( point, normal );
   return ret;
}

inline vaPlane vaPlane::FromPoints( const vaVector3 & v1, const vaVector3 & v2, const vaVector3 & v3 )
{
   vaVector3 edge1 = v2 - v1;
   vaVector3 edge2 = v3 - v1;

   vaVector3 normal = vaVector3::Cross( edge1, edge2 );
   vaVector3 Nnormal = normal.Normalized();
   return vaPlane::FromPointNormal( v1, Nnormal );
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// vaVector2i
///////////////////////////////////////////////////////////////////////////////////////////////////

// assignment
inline vaVector2i &       vaVector2i::operator += ( const vaVector2i & v )
{
    x += v.x;
    y += v.y;
    return *this;
}

inline vaVector2i &       vaVector2i::operator -= ( const vaVector2i & v )
{
    x -= v.x;
    y -= v.y;
    return *this;
}

// unary
inline vaVector2i        vaVector2i::operator + () const
{
    return *this;
}

inline vaVector2i        vaVector2i::operator - () const
{
    return vaVector2i(-x, -y);
}

// binary
inline vaVector2i        vaVector2i::operator + ( const vaVector2i & v ) const
{
    return vaVector2i(x + v.x, y + v.y);
}

inline vaVector2i        vaVector2i::operator - ( const vaVector2i & v ) const
{
    return vaVector2i(x - v.x, y - v.y);
}

inline bool             vaVector2i::operator == ( const vaVector2i& v ) const
{
    return x == v.x && y == v.y;
}

inline bool             vaVector2i::operator != ( const vaVector2i& v ) const
{
    return x != v.x || y != v.y;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// vaVector3i
///////////////////////////////////////////////////////////////////////////////////////////////////

// assignment
inline vaVector3i &       vaVector3i::operator += ( const vaVector3i & v )
{
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
}

inline vaVector3i &       vaVector3i::operator -= ( const vaVector3i & v )
{
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
}

// unary
inline vaVector3i        vaVector3i::operator + ( ) const
{
    return *this;
}

inline vaVector3i        vaVector3i::operator - ( ) const
{
    return vaVector3i( -x, -y, -z );
}

// binary
inline vaVector3i        vaVector3i::operator + ( const vaVector3i & v ) const
{
    return vaVector3i( x + v.x, y + v.y, z + v.z );
}

inline vaVector3i        vaVector3i::operator - ( const vaVector3i & v ) const
{
    return vaVector3i( x - v.x, y - v.y, z - v.z );
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// vaVector4i
///////////////////////////////////////////////////////////////////////////////////////////////////

// assignment
inline vaVector4i &       vaVector4i::operator += ( const vaVector4i & v )
{
    x += v.x;
    y += v.y;
    z += v.z;
    w += v.w;
    return *this;
}

inline vaVector4i &       vaVector4i::operator -= ( const vaVector4i & v )
{
    x -= v.x;
    y -= v.y;
    z -= v.z;
    w -= v.w;
    return *this;
}

// unary
inline vaVector4i        vaVector4i::operator + ( ) const
{
    return *this;
}

inline vaVector4i        vaVector4i::operator - ( ) const
{
    return vaVector4i( -x, -y, -z, -w );
}

// binary
inline vaVector4i        vaVector4i::operator + ( const vaVector4i & v ) const
{
    return vaVector4i( x + v.x, y + v.y, z + v.z, w + v.w );
}

inline vaVector4i        vaVector4i::operator - ( const vaVector4i & v ) const
{
    return vaVector4i( x - v.x, y - v.y, z - v.z, w - v.w );
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// vaVector4ui
///////////////////////////////////////////////////////////////////////////////////////////////////

// assignment
inline vaVector4ui &       vaVector4ui::operator += ( const vaVector4ui & v )
{
    x += v.x;
    y += v.y;
    z += v.z;
    w += v.w;
    return *this;
}

inline vaVector4ui &       vaVector4ui::operator -= ( const vaVector4ui & v )
{
    x -= v.x;
    y -= v.y;
    z -= v.z;
    w -= v.w;
    return *this;
}

// binary
inline vaVector4ui        vaVector4ui::operator + ( const vaVector4ui & v ) const
{
    return vaVector4ui( x + v.x, y + v.y, z + v.z, w + v.w );
}

inline vaVector4ui        vaVector4ui::operator - ( const vaVector4ui & v ) const
{
    return vaVector4ui( x - v.x, y - v.y, z - v.z, w - v.w );
}


////////////////////////////////////////////////////////////////////////////////////////////////
// vaGeometry
////////////////////////////////////////////////////////////////////////////////////////////////

inline bool vaGeometry::NearEqual( float a, float b, const float fEps )
{
    return vaMath::Abs( a - b ) < fEps;
}

inline bool vaGeometry::NearEqual( const vaVector2 & a, const vaVector2 & b, const float fEps )
{
   return ( vaMath::Abs( a.x - b.x ) < fEps ) && ( vaMath::Abs( a.y - b.y ) < fEps );
}

inline bool vaGeometry::NearEqual( const vaVector3 & a, const vaVector3 & b, const float fEps )
{
   return ( vaMath::Abs( a.x - b.x ) < fEps ) && ( vaMath::Abs( a.y - b.y ) < fEps ) && ( vaMath::Abs( a.z - b.z ) < fEps );
}

inline bool vaGeometry::NearEqual( const vaVector4 & a, const vaVector4 & b, const float fEps )
{
   return ( vaMath::Abs( a.x - b.x ) < fEps ) && ( vaMath::Abs( a.y - b.y ) < fEps ) && ( vaMath::Abs( a.z - b.z ) < fEps ) && ( vaMath::Abs( a.w - b.w ) < fEps );
}

inline bool vaGeometry::IntersectSegments2D( const vaVector2 & p1, const vaVector2 & p2, const vaVector2 & p3, const vaVector2 & p4, vaVector2 & outPt )
{
   // Store the values for fast access and easy
   // equations-to-code conversion
   float x1 = p1.x, x2 = p2.x, x3 = p3.x, x4 = p4.x;
   float y1 = p1.y, y2 = p2.y, y3 = p3.y, y4 = p4.y;

   float d = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
   // If d is zero, there is no intersection
   if (d == 0) return NULL;

   // Get the x and y
   float pre = (x1*y2 - y1*x2), post = (x3*y4 - y3*x4);
   float x = ( pre * (x3 - x4) - (x1 - x2) * post ) / d;
   float y = ( pre * (y3 - y4) - (y1 - y2) * post ) / d;

   // Check if the x and y coordinates are within both lines
   if ( x < vaMath::Min(x1, x2) || x > vaMath::Max(x1, x2) ||
      x < vaMath::Min(x3, x4) || x > vaMath::Max(x3, x4) )
      return false;
   if ( y < vaMath::Min(y1, y2) || y > vaMath::Max(y1, y2) ||
      y < vaMath::Min(y3, y4) || y > vaMath::Max(y3, y4) )
      return false;

   outPt.x = x;
   outPt.y = y;

   return true;
}

inline vaVector3 vaGeometry::WorldToViewportSpace( const vaVector3 & worldPos, const vaMatrix4x4 & viewProj, const vaViewport & viewport )
{
   vaVector3 retVal = vaVector3::TransformCoord( worldPos, viewProj );
   retVal.x = ((retVal.x * 0.5f + 0.5f) * viewport.Width );
   retVal.y = ((-retVal.y * 0.5f + 0.5f) * viewport.Height );
   return retVal;   
}

inline vaVector3 vaGeometry::ViewportToWorldSpace( const vaVector3 & screenPos, const vaMatrix4x4 & inverseViewProj, const vaViewport & viewport )
{
   vaVector4 retVal;
   retVal.x = ( screenPos.x / viewport.Width - 0.5f ) * 2.0f;
   retVal.y = ( -screenPos.y / viewport.Height + 0.5f ) * 2.0f;
   retVal.z = screenPos.z;
   retVal.w = 1.0f;

   retVal = vaVector4::Transform( retVal, inverseViewProj );
   retVal /= retVal.w;
   return vaVector3( retVal.x, retVal.y, retVal.z );
}

inline void vaGeometry::CartesianToSpherical( const vaVector3& inVector, float & outAzimuthAngle, float & outPolarAngle, float & outRadialDistance )
{
    outRadialDistance = inVector.Length();
    if( outRadialDistance <= VA_EPSf )
        { outRadialDistance = 0.0f; outPolarAngle = 0.0f; outAzimuthAngle = 0.0f; return; }
    float xyrs = std::max( VA_EPSf, inVector.x * inVector.x + inVector.y * inVector.y );
    outAzimuthAngle = std::acos( inVector.x / sqrt( xyrs ) ) * ( inVector.y < 0 ? -1 : 1 );
    outPolarAngle  = std::acos( inVector.z / outRadialDistance );
}

inline void vaGeometry::SphericalToCartesian( float azimuthAngle, float polarAngle, float radialDistance, vaVector3 & outVector )
{
    outVector.x = std::sin( polarAngle ) * std::cos( azimuthAngle ) * radialDistance;
    outVector.y = std::sin( polarAngle ) * std::sin( azimuthAngle ) * radialDistance;
    outVector.z = std::cos( polarAngle ) * radialDistance;
}


inline vaRay3D vaRay3D::FromTwoPoints( const vaVector3 & p1, const vaVector3 & p2 )
{
   vaRay3D ret;

   ret.Origin      = p1;

   vaVector3 d = p2 - p1;
   ret.Direction = d.Normalized();

   return ret;
}

inline vaRay3D vaRay3D::FromOriginAndDirection( const vaVector3 & origin, const vaVector3 & direction )
{
   vaRay3D ret;

   ret.Origin     = origin;
   ret.Direction  = direction;

   return ret;
}

inline vaVector3 vaRay3D::GetPointAt( float dist ) const
{
   return vaVector3( Origin.x + dist * Direction.x, Origin.y + dist * Direction.y, Origin.z + dist * Direction.z );
}

////////////////////////////////////////////////////////////////////////////////////////////////
// misc
////////////////////////////////////////////////////////////////////////////////////////////////


inline float vaBoundingBox::NearestDistanceToPoint( const vaVector3 & point ) const
{
    vaVector3 max = Max( );
    float dist = 0.0f;

    if( point.x < Min.x )
    {
        float d = point.x - Min.x;
        dist += d*d;
    }
    else
    {
        if( point.x > max.x )
        {
            float d = point.x - max.x;
            dist += d*d;
        }
    }

    if( point.y < Min.y )
    {
        float d = point.y - Min.y;
        dist += d*d;
    }
    else
    {
        if( point.y > max.y )
        {
            float d = point.y - max.y;
            dist += d*d;
        }
    }

    if( point.z < Min.z )
    {
        float d = point.z - Min.z;
        dist += d*d;
    }
    else
    {
        if( point.z > max.z )
        {
            float d = point.z - max.z;
            dist += d*d;
        }
    }

    return vaMath::Sqrt( dist );
}

inline float vaBoundingBox::FarthestDistanceToPoint( const vaVector3 & point ) const
{
    vaVector3 max = Max();
    vaVector3 dist;
    dist.x = vaMath::Max( vaMath::Abs( point.x - Min.x ), vaMath::Abs( point.x - max.x ) );
    dist.y = vaMath::Max( vaMath::Abs( point.y - Min.y ), vaMath::Abs( point.y - max.y ) );
    dist.z = vaMath::Max( vaMath::Abs( point.z - Min.z ), vaMath::Abs( point.z - max.z ) );
    return dist.Length();
}


inline vaOrientedBoundingBox vaOrientedBoundingBox::FromAABBAndTransform( const vaBoundingBox & box, const vaMatrix4x4 & transform )
{
    vaOrientedBoundingBox ret;

#if 0
	// old code, ignores scale
    ret.Extents = box.Size * 0.5f;
    ret.Center  = box.Min + ret.Extents;

    ret.Center  = vaVector3::TransformCoord( ret.Center, transform );
    ret.Axis    = transform.GetRotationMatrix3x3( );

#else
    ret.Extents = box.Size * 0.5f;
    ret.Center  = vaVector3::TransformCoord( box.Min + ret.Extents, transform );

    vaVector3 translation, scale;
    vaMatrix3x3 rotation;
    transform.Decompose( scale, ret.Axis, translation );

    // apply scale to extents
    ret.Extents = ret.Extents * scale;
#endif

    return ret;
}

inline void vaOrientedBoundingBox::ToAABBAndTransform( vaBoundingBox & outBox, vaMatrix4x4 & outTransform ) const
{
    outTransform    = vaMatrix4x4::FromRotationTranslation( this->Axis, this->Center );
    outBox          = vaBoundingBox( -this->Extents, this->Extents * 2.0f );
}

inline vaOrientedBoundingBox vaOrientedBoundingBox::FromScaledTransform( const vaMatrix4x4& transform )
{
    vaOrientedBoundingBox ret;

    transform.Decompose( ret.Extents, ret.Axis, ret.Center );

    return ret;
}

inline vaMatrix4x4 vaOrientedBoundingBox::ToScaledTransform( ) const
{
    return vaMatrix4x4::FromScaleRotationTranslation( this->Extents, this->Axis, this->Center );
}

inline vaBoundingBox vaBoundingBox::Combine( const vaBoundingBox & a, const vaBoundingBox & b )
{
    vaVector3 bmaxA = a.Min + a.Size;
    vaVector3 bmaxB = b.Min + b.Size;

    vaVector3 finalMin = vaVector3::ComponentMin( a.Min, b.Min );
    vaVector3 finalMax = vaVector3::ComponentMax( bmaxA, bmaxB );

    return vaBoundingBox( finalMin, finalMax - finalMin );
}

inline string vaBoundingBox::ToString( const vaBoundingBox & a )
{
    char buffer[512];
    sprintf_s( buffer, _countof( buffer ), "{{%f,%f,%f},{%f,%f,%f}}", a.Min.x, a.Min.y, a.Min.z, a.Size.x, a.Size.y, a.Size.z );
    return buffer;
}

inline int vaOrientedBoundingBox::IntersectPlane( const vaPlane & plane )
{
    // From Christer Ericson "Real Time Collision Detection" page 163

    // Compute the projection interval radius of b onto L(t) = b.c + t * p.n
    float r =   Extents.x * vaMath::Abs( vaVector3::Dot( plane.Normal(), Axis.Row(0) ) ) +
                Extents.y * vaMath::Abs( vaVector3::Dot( plane.Normal(), Axis.Row(1) ) ) +
                Extents.z * vaMath::Abs( vaVector3::Dot( plane.Normal(), Axis.Row(2) ) );
        
    // Compute distance of box center from plane
    float s = vaVector3::Dot( plane.Normal(), this->Center ) + plane.d;

    // Intersection occurs when distance s falls within [-r,+r] interval
    if( vaMath::Abs( s ) <= r )
        return 0;

    return (s < r)?(-1):(1);
}

// 0 means intersect any, <0 means it's wholly outside, >0 means it's wholly outside
inline vaIntersectType vaOrientedBoundingBox::IntersectFrustum( const vaPlane planes[], const int planeCount )
{
    for( int i = 0; i < planeCount; i++ )
    {
        int rk = IntersectPlane( planes[i] );
        
        // if it's completely out of any plane, bail out
        if( rk < 0 )
            return vaIntersectType::Outside;
        if( rk == 0 )
            return vaIntersectType::Intersect;
    }
    // otherwise, we're in!
    return vaIntersectType::Inside;
}

inline vaVector3 vaOrientedBoundingBox::RandomPointInside( vaRandom & randomGeneratorToUse )
{
    vaVector3 pos;
    pos.x = randomGeneratorToUse.NextFloatRange( -1.0f, 1.0f ) * this->Extents.x;
    pos.y = randomGeneratorToUse.NextFloatRange( -1.0f, 1.0f ) * this->Extents.y;
    pos.z = randomGeneratorToUse.NextFloatRange( -1.0f, 1.0f ) * this->Extents.z;

    vaMatrix4x4 transform = vaMatrix4x4::FromRotationTranslation( this->Axis, this->Center );

    return vaVector3::TransformCoord( pos, transform );
}

inline vaOrientedBoundingBox vaOrientedBoundingBox::Transform( const vaOrientedBoundingBox & obb, const vaMatrix4x4 & mat )
{
    // !! NOT THE MOST OPTIMAL IMPLEMENTATION - THIS IS JUST AN UGLY PROTOTYPE !!

    vaVector3 newCenter     = vaVector3::TransformCoord( obb.Center, mat );
    vaVector3 newCorner     = vaVector3::TransformCoord( obb.Center + vaVector3::TransformNormal( obb.Extents, obb.Axis ), mat ) - newCenter;
    
    vaMatrix4x4 normTransform = mat.InversedHighPrecision( nullptr, false ).Transposed( );

    vaVector3 axisX         = vaVector3::TransformNormal( obb.Axis.Row(0), normTransform ).Normalized();
    vaVector3 axisY         = vaVector3::TransformNormal( obb.Axis.Row(1), normTransform ).Normalized();
    vaVector3 axisZ         = vaVector3::TransformNormal( obb.Axis.Row(2), normTransform ).Normalized();
    vaMatrix3x3 newAxis     = vaMatrix3x3( axisX, axisY, axisZ );

    vaVector3 newExtents;
    newExtents.x = vaVector3::Dot( newCorner, newAxis.Row(0) );
    newExtents.y = vaVector3::Dot( newCorner, newAxis.Row(1) );
    newExtents.z = vaVector3::Dot( newCorner, newAxis.Row(2) );
    
    return vaOrientedBoundingBox( newCenter, newExtents, newAxis );
}

// From Real Time Collision Detection by Christer Ericson, Chapter 4, "AABB Recomputed from Rotated AABB"
inline vaBoundingBox vaOrientedBoundingBox::ComputeEnclosingAABB( ) const
{
    vaVector3 aMin = -this->Extents, aMax = this->Extents;
    vaVector3 bMin, bMax;

    // For all three axes
    for (int i = 0; i < 3; i++) 
    {
        // Start by adding in translation
        bMin[i] = bMax[i] = Center[i];
        // Form extent by summing smaller and larger terms respectively
        for (int j = 0; j < 3; j++) 
        {
            float e = this->Axis.m[i][j] * aMin[j];
            float f = this->Axis.m[i][j] * aMax[j];
            if (e < f) 
            {
                bMin[i] += e;
                bMax[i] += f;
            } 
            else 
            {
                bMin[i] += f;
                bMax[i] += e;
            }
        }
    }
    return vaBoundingBox( bMin, bMax-bMin );
}

inline float vaOrientedBoundingBox::NearestDistanceToPoint( const vaVector3 & pt ) const
{
    // // unoptimized naive initial implementation
    // vaBoundingBox aabb; vaMatrix4x4 transform;
    // ToAABBAndTransform( aabb, transform );
    // vaVector3 ptt = vaVector3::TransformCoord( pt, transform.Inverse() );
    // return aabb.NearestDistanceToPoint( ptt );

    // From Christer Ericson "Real Time Collision Detection" page 134
    vaVector3 v = pt - this->Center;
    float sqDist = 0.0f;
    for( int i = 0; i < 3; i++ )
    {
        // Project vector from box center to p on each axis, getting the distance 
        // of p along that axis, and count any excess distance outside box extents
        float d = vaVector3::Dot( v, this->Axis.Row(i)), excess = 0.0f;
        if( d < -this->Extents[i] )
            excess = d + this->Extents[i];
        else if( d > this->Extents[i] )
            excess = d - this->Extents[i];
        sqDist += excess * excess;
    }
    
    return vaMath::Sqrt( sqDist );
}

inline float vaOrientedBoundingBox::FarthestDistanceToPoint( const vaVector3 & pt ) const
{
    // // unoptimized naive initial implementation
    // vaBoundingBox aabb; vaMatrix4x4 transform;
    // ToAABBAndTransform( aabb, transform );
    // vaVector3 ptt = vaVector3::TransformCoord( pt, transform.Inverse() );
    // return aabb.FarthestDistanceToPoint( ptt );

    // From Christer Ericson "Real Time Collision Detection" page 134
    // (modified to return farthest instead of nearest)
    vaVector3 v = pt - this->Center;
    float sqDist = 0.0f;
    for( int i = 0; i < 3; i++ )
    {
        // Project vector from box center to p on each axis, getting the distance 
        // of p along that axis, and count any distance from the box far edge
        float d = vaVector3::Dot( v, this->Axis.Row(i)), excess = 0.0f;
        if( d < 0 )
            excess = this->Extents[i] - d;
        else
            excess = d + this->Extents[i];
        sqDist += excess * excess;
    }

    return vaMath::Sqrt( sqDist );
}

inline float vaOrientedBoundingBox::NearestDistanceToPlane( const vaPlane & plane ) const
{
    // From Christer Ericson "Real Time Collision Detection" page 163
    // (modified to just return nearest distance to plane)

    // Compute the projection interval radius of b onto L(t) = b.c + t * p.n
    float r =   Extents.x * vaMath::Abs( vaVector3::Dot( plane.Normal(), Axis.Row(0) ) ) +
                Extents.y * vaMath::Abs( vaVector3::Dot( plane.Normal(), Axis.Row(1) ) ) +
                Extents.z * vaMath::Abs( vaVector3::Dot( plane.Normal(), Axis.Row(2) ) );

    // Compute distance of box center from plane
    float s = vaVector3::Dot( plane.Normal(), this->Center ) + plane.d;

    // We actually return the 'smallest distance to the plane' (different from nearest) - maybe refactor/rename? or do max( 0, )?
    return s - r;
}

inline float vaOrientedBoundingBox::FarthestDistanceToPlane( const vaPlane & plane ) const
{
    // From Christer Ericson "Real Time Collision Detection" page 163
    // (modified to just return farthest distance to plane)

    // Compute the projection interval radius of b onto L(t) = b.c + t * p.n
    float r =   Extents.x * vaMath::Abs( vaVector3::Dot( plane.Normal(), Axis.Row(0) ) ) +
                Extents.y * vaMath::Abs( vaVector3::Dot( plane.Normal(), Axis.Row(1) ) ) +
                Extents.z * vaMath::Abs( vaVector3::Dot( plane.Normal(), Axis.Row(2) ) );

    // Compute distance of box center from plane
    float s = vaVector3::Dot( plane.Normal(), this->Center ) + plane.d;

    // We actually return the 'largest distance to the plane' (different from nearest) - maybe refactor/rename? or do max( 0, )?
    return s + r;
}


template< typename T >
inline void vaStaticArray2D<T>::CopyFrom( const vaStaticArray2D<T> & other )
{
    if( ( other.GetWidth( ) != GetWidth( ) ) || ( other.GetHeight( ) != GetHeight( ) ) )
    {
        assert( false );
        return;
    }
    memcpy( m_data, other.m_data, sizeof(T) * m_width * m_height );
}

////////////////////////////////////////////////////////////////////////////////////////////////
// vaViewport
////////////////////////////////////////////////////////////////////////////////////////////////

inline bool             vaViewport::operator == ( const vaViewport & other ) const
{
    return  X                  == other.X                  &&
            Y                  == other.Y                  &&
            Width              == other.Width              &&
            Height             == other.Height             &&
            MinDepth           == other.MinDepth           &&
            MaxDepth           == other.MaxDepth           &&
            ScissorRect        == other.ScissorRect        &&
            ScissorRectEnabled == other.ScissorRectEnabled;
}

////////////////////////////////////////////////////////////////////////////////////////////////
// vaGeometry
////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////
// vaColor
////////////////////////////////////////////////////////////////////////////////////////////////

inline float vaColor::LinearToSRGB( float val )
{
    if( val < 0.0031308f )
        val *= float( 12.92 );
    else
        val = float( 1.055 ) * powf( vaMath::Abs( val ), float( 1.0 ) / float( 2.4 ) ) - float( 0.055 );
    return val;
}

inline float vaColor::SRGBToLinear( float val )
{
    if( val < 0.04045 )
        val /= float( 12.92 );
    else
        val = powf( vaMath::Abs( val + float( 0.055 ) ) / float( 1.055 ), float( 2.4 ) );
    return val;
}

