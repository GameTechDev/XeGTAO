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

#include "vaNoise.hlsl"

#ifndef VA_RENDERINGSHARED_HLSL_INCLUDED
#define VA_RENDERINGSHARED_HLSL_INCLUDED

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Common shared types

// This is the vertex after vertex shading (also used as inputs/outputs for interpolation)
// "SV_Position" was taken out and is used manually on the rasterization side - on the raytracing side it's closest match is dispatchRaysIndex.xy
struct ShadedVertex
{
    float4 Color                : COLOR;            // rarely used in practice - perhaps remove?
    float3 WorldspacePos        : TEXCOORD0;        // this is with the -g_globals.WorldBase offset applied
    float3 WorldspaceNormal     : NORMAL0;
    float4 Texcoord01           : TEXCOORD1;

    float3 ObjectspacePos       : TEXCOORD2;

#ifdef VA_ENABLE_MANUAL_BARYCENTRICS
    float3 Barycentrics         : TEXCOORD3;
#endif
};

// Detailed surface info at the rasterized pixel or ray hit (interpolated vertex + related stuff) 
struct SurfaceInteraction : ShadedVertex
{
    // mikktspace tangent frame 
    float3  WorldspaceTangent;
    float3  WorldspaceBitangent;

    // non-interpolated, triangle normal
    float3  TriangleNormal;

    // rasterization: normalize(g_globals.CameraWorldPosition.xyz - vertex.WorldspacePos.xyz); raytracing: normalize(-rayDirLength);
    float3  View;
    // length of View above before 'normalize()'
    float   ViewDistance;

    float   NormalVariance;             // du = ddx( worldNormal ); dv = ddy( worldNormal ); dot(du, du) + dot(dv, dv)

                                        // these are the HIT cone definitions (computed from ray start cone spread angle & widths)
    float   RayConeSpreadAngle;         // spread angle computed at the hit point (enlarged or reduced based on surface curvature) - used as a starting value for the next ray, if any!
    float   RayConeWidth;               // ray cone width computed at the hit point - used as a starting value for the next ray, if any!
    float   RayConeWidthProjected;      // ray cone width computed at the hit point and projected to surface (approximation)

    float   BaseLODWorld;
    float   BaseLODObject;

#ifdef VA_RAYTRACING
    float   BaseLODTex0;
#endif

    // Moved noise to SurfaceInteraction (used to be, and might still be duplicated, in ShadingParams)
    float   ObjectspaceNoise;

    // Is the triangle backface being rendered (or raytraced)
    bool    IsFrontFace;

    // This is the unperturbed tangent space!
    float3x3    TangentToWorld( )           { return float3x3( WorldspaceTangent, WorldspaceBitangent, WorldspaceNormal ); }

    // not as precise as the DXR WorldRayOrigin()
    float3      RayOrigin( )                { return WorldspacePos + View * ViewDistance; }

    void        DebugText( );

    void DebugDrawTangentSpace( float size );

#ifdef VA_RAYTRACING
    // Raytraced version that interpolates, generates tangent space and front face info, etc.
    static SurfaceInteraction ComputeAtRayHit( const in ShadedVertex a, const in ShadedVertex b, const in ShadedVertex c, float2 barycentrics, float3 rayDirLength, float rayStartConeSpreadAngle, float rayStartConeWidth, uint frontFaceIsClockwise );
#else
    // Non-raytraced version
    static SurfaceInteraction Compute( const in ShadedVertex vertex, const bool isFrontFace );
#endif
};

//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Basic Phong reflection model

void ComputePhongDiffuseBRDF( float3 viewDir, float3 surfaceNormal, float3 surfaceToLightDir, out float outDiffuseTerm )
{
    outDiffuseTerm = max( 0.0, dot(surfaceNormal, surfaceToLightDir) );
}

void ComputePhongSpecularBRDF( float3 viewDir, float3 surfaceNormal, float3 surfaceToLightDir, float specularPower, out float outSpecularTerm )
{
    float3 reflectDir = reflect( surfaceToLightDir, surfaceNormal );
    float RdotV = max( 0.0, dot( reflectDir, viewDir ) );
    outSpecularTerm = pow(RdotV, specularPower);
}

//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ggx + specular AA; see links below
// http://www.neilblevins.com/cg_education/ggx/ggx.htm
// http://research.nvidia.com/sites/default/files/pubs/2016-06_Filtering-Distributions-of/NDFFiltering.pdf - http://blog.selfshadow.com/sandbox/specaa.html
// for future optimization: http://filmicworlds.com/blog/optimizing-ggx-shaders-with-dotlh/
// Specular antialiasing part
float2 ComputeFilterRegion(float3 h)
{
    // Compute half-vector derivatives
    float2 hpp   = h.xy/h.z;
    float2 hppDx = ddx(hpp);
    float2 hppDy = ddy(hpp);

    // Compute filtering region
    float2 rectFp = (abs(hppDx) + abs(hppDy)) * 0.5;

    // For grazing angles where the first-order footprint goes to very high values
    // Usually you don’t need such high values and the maximum value of 1.0 or even 0.1
    // is enough for filtering.
    return min(float2(0.2, 0.2), rectFp);
}
// Self-contained roughness filtering function, to be used in forward shading
void FilterRoughness(float3 h, inout float2 roughness)
{
    float2 rectFp = ComputeFilterRegion(h);

    // Covariance matrix of pixel filter's Gaussian (remapped in roughness units)
    // Need to x2 because roughness = sqrt(2) * pixel_sigma_hpp
    float2 covMx = rectFp * rectFp * 2.0;

    roughness = sqrt( roughness*roughness + covMx ); // Beckmann proxy convolution for GGX
}
// Self-contained roughness filtering function, to be used in the G-Buffer generation pass with deferred shading
void FilterRoughnessDeferred(float2 pixelScreenPos, float3x3 cotangentFrame, in float2 globalPixelScale, inout float2 roughness)
{
    // Estimate the approximate half vector w/o knowing the light position
    float3 approxHW = cotangentFrame[2]; // normal
    approxHW -= globalPixelScale.xxx * ddx( approxHW ) * ( GLSL_mod( pixelScreenPos.x, 2.0 ) - 0.5 );
    approxHW -= globalPixelScale.yyy * ddy( approxHW ) * ( GLSL_mod( pixelScreenPos.y, 2.0 ) - 0.5 );

    // Transform to the local space
    float3 approxH = normalize( mul( cotangentFrame, approxHW ) );

    // Do the regular filtering
    FilterRoughness(approxH, roughness);
}
//
float GGX(float3 h, float2 rgns)
{
    rgns = max( float2( 1e-3, 1e-3 ), rgns );
    float NoH2 = h.z * h.z;
    float2 Hproj = h.xy;
    float exponent = dot(Hproj / (rgns*rgns), Hproj);
    float root = NoH2 + exponent;
    return 1.0 / (VA_PI * rgns.x * rgns.y * root * root);
}
//
void ComputeGGXSpecularBRDF( float3 viewDir, float3x3 cotangentFrame, float3 surfaceToLightDir, float2 roughness, out float outSpecularTerm )
{
    // compute half-way vector
    float3 halfWayVec = normalize( -viewDir + surfaceToLightDir );

    // convert half-way vector to tangent space
    float3 h = normalize( mul( cotangentFrame, halfWayVec ) );

    // assuming input roughness has this already applied 
    // FilterRoughness( h, roughness );

    outSpecularTerm = GGX( h, roughness );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GGX/Beckmann <-> Phong - from https://gist.github.com/Kuranes/e43ec6f4b64c7303ef6e
//
// ================================================================================================
// Converts a Beckmann roughness parameter to a Phong specular power
// ================================================================================================
float RoughnessToSpecPower(in float m)
{
    return 2.0 / (m * m) - 2.0;
}
//
// ================================================================================================
// Converts a Blinn-Phong specular power to a Beckmann roughness parameter
// ================================================================================================
float SpecPowerToRoughness(in float s)
{
    return sqrt(2.0 / (s + 2.0));
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Various utility
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ReOrthogonalizeFrame( inout float3x3 frame, uniform const bool preserveHandedness )
{
    frame[1] = normalize( frame[1] - frame[2]*dot(frame[1], frame[2]) );
    // ^ same as normalize( cross( frame[2], frame[1] ) ) - check if faster
    
    if( preserveHandedness )   // this path works for both right handed and left handed cases
    {
        float3 newB = cross(frame[2], frame[1]);
        frame[0] = sign(dot(newB, frame[0])) * newB;
    }
    else
    {
        frame[0] = cross(frame[2], frame[1]);
    }
}

// todo: remove this, just use GenBasisTB below
// from http://www.thetenthplanet.de/archives/1180
float3x3 ComputeCotangentFrame( float3 N, float3 p, float2 uv )
{
    // get edge vectors of the pixel triangle
    float3 dp1  = ddx( p );
    float3 dp2  = ddy( p );
    float2 duv1 = ddx( uv );
    float2 duv2 = ddy( uv );
 
    // solve the linear system
    float3 dp2perp = cross( dp2, N );
    float3 dp1perp = cross( N, dp1 );
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
 
    // construct a scale-invariant frame
    float invmax = rsqrt( max( dot(T,T), dot(B,B) ) );
    return float3x3( T * invmax, B * invmax, N );
}

// from https://graphics.pixar.com/library/OrthonormalB/paper.pdf
void ComputeOrthonormalBasis( float3 n, out float3 b1, out float3 b2 )
{
    float sign = (n.z >= 0.0) * 2.0 - 1.0; //copysignf(1.0f, n.z); <- any better way to do this?
    const float a = -1.0 / (sign + n.z);
    const float b = n.x * n.y * a;
    b1 = float3(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
    b2 = float3(b, sign + n.y * n.y * a, -n.y);
}

// https://github.com/mmikk/Papers-Graphics-3D/blob/master/sgp.pdf - mikktspace!
float3x3 GenBasisTB( float3 worldNormal, float3 worldPos, float2 texST )
{
    const float3 nrmBaseNormal = worldNormal;   // just matching the naming convention
    const float3 relSurfacePos = worldPos;      // just matching the naming convention
    float3 vT; float3 vB;

    float3 dPdx = ddx_fine( relSurfacePos );
    float3 dPdy = ddy_fine( relSurfacePos );

    float3 sigmaX = dPdx - dot ( dPdx, nrmBaseNormal ) * nrmBaseNormal;
    float3 sigmaY = dPdy - dot ( dPdy, nrmBaseNormal ) * nrmBaseNormal;
    float flip_sign = dot ( dPdy , cross ( nrmBaseNormal , dPdx )) <0 ? -1 : 1;

    float2 dSTdx = ddx_fine( texST ), dSTdy = ddy_fine( texST ) ;
    float det = dot ( dSTdx , float2 ( dSTdy.y , -dSTdy.x ) );
    float sign_det = det <0 ? -1 : 1;
    // invC0 represents ( dXds , dYds ) ; but we don ’t divide by
    // determinant ( scale by sign instead )
    float2 invC0 = sign_det * float2 ( dSTdy .y , - dSTdx .y );
    vT = sigmaX * invC0.x + sigmaY * invC0 .y;
    if( abs ( det ) >0.0) vT = normalize ( vT ) ;
    vB = ( sign_det * flip_sign ) * cross ( nrmBaseNormal , vT );

    return float3x3( vT, vB, worldNormal );
}

// "Efficiently building a matrix to rotate one vector to another"
// http://cs.brown.edu/research/pubs/pdfs/1999/Moller-1999-EBA.pdf / https://dl.acm.org/doi/10.1080/10867651.1999.10487509
// (using https://github.com/assimp/assimp/blob/master/include/assimp/matrix3x3.inl#L275 as a code reference as it seems to be best)
float3x3 RotFromToMatrix( float3 from, float3 to )
{
    const float e       = dot(from, to);
    const float f       = abs(e); //(e < 0)? -e:e;

    // WARNING WARNING WARNING WARNING THIS NOT YET IMPLEMENTED/TESTED (see assimp code above as a reference)
    if( f > ( 1.0 - 0.00001 ) )
        return float3x3( 1, 0, 0, 0, 1, 0, 0, 0, 1 );

    const float3 v      = cross( from, to );
    /* ... use this hand optimized version (9 mults less) */
    const float h       = (1.0)/(1.0 + e);      /* optimization by Gottfried Chen */
    const float hvx     = h * v.x;
    const float hvz     = h * v.z;
    const float hvxy    = hvx * v.y;
    const float hvxz    = hvx * v.z;
    const float hvyz    = hvz * v.y;

    float3x3 mtx;
    mtx[0][0] = e + hvx * v.x;
    mtx[0][1] = hvxy - v.z;
    mtx[0][2] = hvxz + v.y;

    mtx[1][0] = hvxy + v.z;
    mtx[1][1] = e + h * v.y * v.y;
    mtx[1][2] = hvyz - v.x;

    mtx[2][0] = hvxz - v.y;
    mtx[2][1] = hvyz + v.x;
    mtx[2][2] = e + hvz * v.z;

    return mtx;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BSDFs and related
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Smith's shadowing-masking function G1 for GGX based on view vector 'v' (Wo), microfacet normal 'm' and anisotropic roughness 'alpha_x' and 'alpha_y' parameters
// (see http://jcgt.org/published/0007/04/01/paper.pdf, Equation 2)
float SmithGGXG1( const float3 v, const float3 m, float alpha_x, float alpha_y )
{
    // Assuming view vector never perpendicular and coming from correct side (otherwise we should return 1 - no shadowing / masking)
    float inner = (alpha_x*alpha_x * v.x*v.x + alpha_y*alpha_y * v.y*v.y) / (v.z*v.z);
    return 2.0 / ( 1 + sqrt(1 + inner) );

#if 0
    // alternative, same results, see pg 105 http://jcgt.org/published/0003/02/03/paper.pdf
    float a = 1.0 / (alpha_x * tan(acos(v.z)));
    float a2 = a*a;
    float Lambda = (-1.0 + sqrt(1.0 + 1.0/a2)) / 2.0;
    return 1.0 / (1.0 + Lambda);
#endif
}

// Microfacet distribution function based on microfacet normal 'm' and isotropic roughness 'roughness' (assumes m.z is > 0)
// (see http://jcgt.org/published/0007/04/01/paper.pdf, Equation 1)
float NDFDistribution( float3 m, float alpha_x, float alpha_y )
{
    float temp = m.z*m.z + ( (m.x*m.x) / (alpha_x * alpha_x) + (m.y*m.y) / (alpha_y * alpha_y) );
    float temp2 = temp*temp;
    return 1.0 / (VA_PI * alpha_x * alpha_y * temp2);
}

// Visible Microfacet distribution function based on microfacet normal 'm' and isotropic roughness 'roughness' (assumes m.z is > 0)
// (see http://jcgt.org/published/0007/04/01/paper.pdf, Equation 3 and also Appendix B)
float VNDFDistributionPDF( float3 v, float3 m, float alpha_x, float alpha_y/*, float fireflyFudge*/ )
{
    // 'D_Ve(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z' 
    float distribution = /*max( 0, dot(v,m) ) **/ SmithGGXG1( v, m, alpha_x, alpha_y ) * NDFDistribution( m, alpha_x, alpha_y ) / v.z;
    
    //PDF(Li) = D_Ve(Ne)/4*max(0, dot(Ve, Ne))
    float pdf = distribution / (4.0 /** max( 0.0, dot( v, m ) ) */);

    // note: the two 'max( 0.0, dot( v, m ) ) )' cancel themselves out
    return pdf;
}

// Original code from sampling the GGX Distribution of Visible Normals, Eric Heitz, http://jcgt.org/published/0007/04/01/
// float3 sampleGGXVNDF(float3 Ve, float alpha_x, float alpha_y, float U1, float U2)
// {
//     // Section 3.2: transforming the view direction to the hemisphere configuration
//     float3 Vh = normalize(float3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));
//     // Section 4.1: orthonormal basis (with special case if cross product is zero)
//     float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
//     float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1,0,0);
//     float3 T2 = cross(Vh, T1);
//     // Section 4.2: parameterization of the projected area
//     float r = sqrt(U1);	
//     float phi = 2.0 * VA_PI * U2;	
//     float t1 = r * cos(phi);
//     float t2 = r * sin(phi);
//     float s = 0.5 * (1.0 + Vh.z);
//     t2 = (1.0 - s)*sqrt(1.0 - t1*t1) + s*t2;
//     // Section 4.3: reprojection onto hemisphere
//     float3 Nh = t1*T1 + t2*T2 + sqrt(max(0.0, 1.0 - t1*t1 - t2*t2))*Vh;
//     // Section 3.4: transforming the normal back to the ellipsoid configuration
//     float3 Ne = normalize(float3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0, Nh.z)));	
//     return Ne;
// }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SurfaceInteraction implementation
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceInteraction::DebugText( )
{
    ::DebugText( );
    //::DebugText( Position         );
    ::DebugText( Color            );
    ::DebugText( WorldspacePos    );
    ::DebugText( WorldspaceNormal );
    ::DebugText( Texcoord01       );
    ::DebugText( ObjectspacePos   );
    //::DebugText( );
    ::DebugText( float4( WorldspaceTangent , 0 ) );
    ::DebugText( float4( WorldspaceBitangent, 0 ) );
    ::DebugText( float4( TriangleNormal    , 0 ) );
    ::DebugText( float4( View              , 0 ) );
    //::DebugText( );
    ::DebugText( ViewDistance );
    ::DebugText( NormalVariance );
    ::DebugText( float3( RayConeSpreadAngle, RayConeWidth, RayConeWidthProjected ) );
#ifdef VA_RAYTRACING
    ::DebugText( float3( BaseLODWorld, BaseLODObject, BaseLODTex0 ) );
#else
    ::DebugText( float2( BaseLODWorld, BaseLODObject ) );
#endif
    ::DebugText( ObjectspaceNoise );
    ::DebugText( (uint)IsFrontFace );
    ::DebugText( );
}

void SurfaceInteraction::DebugDrawTangentSpace( float size )
{
    ::DebugDraw3DArrow( WorldspacePos, WorldspacePos + WorldspaceTangent * size,    size * 0.01, float4( 1, 0, 0, 1 ) );
    ::DebugDraw3DArrow( WorldspacePos, WorldspacePos + WorldspaceBitangent * size,  size * 0.01, float4( 0, 1, 0, 1 ) );
    ::DebugDraw3DArrow( WorldspacePos, WorldspacePos + WorldspaceNormal * size,     size * 0.01, float4( 0, 0, 1, 1 ) );
}

#ifdef VA_RAYTRACING
    // Raytraced version that interpolates, generates tangent space and front face info, etc.
SurfaceInteraction SurfaceInteraction::ComputeAtRayHit( const in ShadedVertex a, const in ShadedVertex b, const in ShadedVertex c, float2 barycentrics, float3 rayDirLength, float rayStartConeSpreadAngle, float rayStartConeWidth, uint frontFaceIsClockwise )
#else
    // Non-raytraced version
SurfaceInteraction SurfaceInteraction::Compute( const in ShadedVertex vertex, const bool isFrontFace )
#endif
{
    SurfaceInteraction surface;                                                    

#ifdef VA_RAYTRACING
    float3 b3 = float3(1 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    //surface.Position         = b3.x * a.Position         + b3.y * b.Position         + b3.z * c.Position        ;
    surface.Color            = b3.x * a.Color            + b3.y * b.Color            + b3.z * c.Color           ;
    surface.WorldspacePos    = b3.x * a.WorldspacePos    + b3.y * b.WorldspacePos    + b3.z * c.WorldspacePos   ;
    surface.WorldspaceNormal = b3.x * a.WorldspaceNormal + b3.y * b.WorldspaceNormal + b3.z * c.WorldspaceNormal;
    surface.Texcoord01       = b3.x * a.Texcoord01       + b3.y * b.Texcoord01       + b3.z * c.Texcoord01      ;
    surface.ObjectspacePos   = b3.x * a.ObjectspacePos   + b3.y * b.ObjectspacePos   + b3.z * c.ObjectspacePos  ;
    // Proj -> NDC
    // surface.Position.xyz    /= surface.Position.w;
    // surface.Position.xy     = (surface.Position.xy * float2( 0.5, -0.5 ) + float2( 0.5, 0.5 ) ) * g_globals.ViewportSize.xy + 0.5;
#else // !defined(VA_RAYTRACING)
    // surface.Position            = vertex.Position;
    surface.Color               = vertex.Color;
    surface.WorldspacePos       = vertex.WorldspacePos;
    surface.WorldspaceNormal    = vertex.WorldspaceNormal;
    surface.Texcoord01          = vertex.Texcoord01;
    surface.ObjectspacePos      = vertex.ObjectspacePos;
    surface.IsFrontFace         = isFrontFace;
    float rayStartConeSpreadAngle   = g_globals.PixelFOVXY.x;
    float rayStartConeWidth         = 0; // always 0 for primary rays!
#endif
                                            // interpolated normal needs normalizing!
    surface.WorldspaceNormal.xyz = normalize( surface.WorldspaceNormal.xyz );

    // used below
#ifdef VA_RAYTRACING
    float3 objDDX   = b.ObjectspacePos.xyz-a.ObjectspacePos.xyz;
    float3 objDDY   = c.ObjectspacePos.xyz-a.ObjectspacePos.xyz;
#else
    float3 objDDX   = ddx_fine( surface.ObjectspacePos.xyz );
    float3 objDDY   = ddy_fine( surface.ObjectspacePos.xyz );
#endif
    // we'll need this below
#ifdef VA_RAYTRACING
    surface.View            = -rayDirLength;        // view vector is just -ray, right?
#else
    surface.View            = g_globals.CameraWorldPosition.xyz - surface.WorldspacePos.xyz;
#endif
    surface.ViewDistance    = length( surface.View );
    surface.View            = surface.View / surface.ViewDistance;

    ///************************ COMPUTE TANGENT SPACE ************************
    // See GenBasisTB() in vaRenderingShared.hlsl - this one has been hacked to work for raytracing too; seems to be ok?
    const float3 nrmBaseNormal = surface.WorldspaceNormal.xyz;   // just matching the naming convention
#ifdef VA_RAYTRACING
    float3 dPdx = b.WorldspacePos.xyz-a.WorldspacePos.xyz;
    float3 dPdy = c.WorldspacePos.xyz-a.WorldspacePos.xyz;
#else
    float3 dPdx = ddx_fine( surface.WorldspacePos.xyz );
    float3 dPdy = ddy_fine( surface.WorldspacePos.xyz );
#endif
    float3 sigmaX = dPdx - dot ( dPdx, nrmBaseNormal ) * nrmBaseNormal;
    float3 sigmaY = dPdy - dot ( dPdy, nrmBaseNormal ) * nrmBaseNormal;
    float flip_sign = dot ( dPdy , cross ( nrmBaseNormal , dPdx )) <0 ? -1 : 1;
#ifdef VA_RAYTRACING
    float2 dSTdx = b.Texcoord01.xy-a.Texcoord01.xy;     // these are not the same as ddx/ddy below but math works out
    float2 dSTdy = c.Texcoord01.xy-a.Texcoord01.xy;     // these are not the same as ddx/ddy below but math works out
#else
    float2 dSTdx = ddx_fine( surface.Texcoord01.xy );
    float2 dSTdy = ddy_fine( surface.Texcoord01.xy );
#endif

    float3 vT; float3 vB;
    float det = dot ( dSTdx , float2 ( dSTdy.y , -dSTdy.x ) );
    float sign_det = det <0 ? -1 : 1;
    // invC0 represents ( dXds , dYds ) ; but we don ’t divide by
    // determinant ( scale by sign instead )
    float2 invC0 = sign_det * float2 ( dSTdy .y , - dSTdx .y );
    vT = sigmaX * invC0.x + sigmaY * invC0 .y;
    float lengthT = length(vT);
    // if( abs ( det ) > 0.0) vT = normalize ( vT ) ;
    if( lengthT > 1e-10 )
    {
        vT /= lengthT;
        vB = ( sign_det * flip_sign ) * cross ( nrmBaseNormal , vT );
        surface.WorldspaceTangent       = vT;
        surface.WorldspaceBitangent     = vB;
    }
    else // sometimes UVs are just broken, so this is a fallback <shrug>
        ComputeOrthonormalBasis( nrmBaseNormal, surface.WorldspaceTangent, surface.WorldspaceBitangent );
    ///***********************************************************************

    ///*********************** COMPUTE TRIANGLE NORMAL ***********************
    // If we're rasterizing back face or the ray is hitting back face, by
    // convention we invert the normal (but keep the IsFrontFace flag in case
    // this information is later needed)
    surface.TriangleNormal      = normalize( cross( dPdx, dPdy ) );
    // surface.TriangleNormal      = normalize( (frontFaceIsClockwise)?(cross( dPdx, dPdy )):(cross( dPdy, dPdx ) ) );
#ifdef VA_RAYTRACING
    surface.IsFrontFace         = dot( surface.TriangleNormal, rayDirLength ) < 0;
    // surface.IsFrontFace         = (frontFaceIsClockwise)?(surface.IsFrontFace):(-surface.IsFrontFace);  // I'm <really> not sure what to do about this bit
#else
    surface.IsFrontFace         = isFrontFace;
#endif
    float frontFaceSign = (surface.IsFrontFace)?(1.0):(-1.0);
    surface.TriangleNormal      *= frontFaceSign;
    surface.WorldspaceNormal    *= frontFaceSign;
    //        float frontFaceSign = (surface.IsFrontFace)?(1.0):(-1.0);
    ///***********************************************************************


    ///***************** COMPUTE RAY CONE PARAMS AT HIT POINT ****************
    //
    // see RaytracingGems 1, Figure 20-6 illustrates the surface spread angle [beta], which will be zero for planar reflections, greater than zero for convex 
    // reflections, and less than zero for concave reflections.
    float betaAngle             = 0.0; // TODO: do this properly: see 20.3.4.4 SURFACE SPREAD ANGLE FOR REFLECTIONS
                                        //
                                        // see w0 in chapter 20.3.4.1, approx of "2 * rayLength * tan( ConeSpreadAngle / 2 )"
                                        // see see RaytracingGems 1, Equation 29
    surface.RayConeWidth            = rayStartConeWidth + surface.ViewDistance * (rayStartConeSpreadAngle + betaAngle);       // note: surface.ViewDistance is rayLength!
    surface.RayConeSpreadAngle      = rayStartConeSpreadAngle + betaAngle;
    //
    // see RayTracingGems 1, chapter 20.3.4.1, equation 25; added is the slope modifier 
    const float cMIPSlopeModifier = 0.45; // <- this is a customization; baked in here; it tweaks MIP selection to appear more like anisotropic (while it's just trilinear)
    surface.RayConeWidthProjected   = surface.RayConeWidth / pow( abs( dot( surface.TriangleNormal, surface.View ) ), cMIPSlopeModifier );
    ///***********************************************************************

    ///********************* COMPUTE VARIOUS MIP OFFSETS *********************
    // see RayTracingGems 1, chapter 20.3.4.1
    //float rayLength         = length(rayDirLength);
    //float3 rayDir           = rayDirLength / rayLength;
    //surface.RayConeWidth        = rayLength * rayStartConeSpreadAngle; // see w0 in chapter 20.3.4.1, approx of "2 * rayLength * tan( ConeSpreadAngle / 2 )"

    surface.BaseLODWorld    = log2( surface.RayConeWidthProjected );

#ifdef VA_RAYTRACING
    // see RayTracingGems 1, chapter 20.2, Equation 3 - except we leave out texture resolution here
    float2 dTex0_AC         = b.Texcoord01.xy-a.Texcoord01.xy;
    float2 dTex0_BC         = c.Texcoord01.xy-a.Texcoord01.xy;
    float triWorldAreaX2    = length( cross( dPdx, dPdy ) );
    float triTex0AreaX2     = abs( dTex0_AC.x * dTex0_BC.y - dTex0_BC.x * dTex0_AC.y );
    float baseLODTex0       = log2( sqrt(triTex0AreaX2 / triWorldAreaX2) ); // same as Delta from Equation 3, except texture solution left out
    surface.BaseLODTex0         = baseLODTex0 + surface.BaseLODWorld + g_globals.RaytracingMIPOffset;
#else
    float triWorldAreaX2    = length( cross( dPdx, dPdy ) );    // <- not actual triWorldAreaX2!! just area over a pixel (since inputs are ddx/ddy)
#endif

    float triObjAreaX2      = length( cross( objDDX, objDDY ) );  // <- not actual triObjAreaX2!! just area over a pixel (since inputs are ddx/ddy)
    float baseLODObject     = log2( sqrt(triObjAreaX2 / triWorldAreaX2) ); // same as Delta from Equation 3, except texture solution left out
    surface.BaseLODObject       = baseLODObject + surface.BaseLODWorld;
    ///***********************************************************************

    ///******************************** NOISE ********************************
    Noise3D( surface.ObjectspacePos.xyz, surface.BaseLODObject, surface.RayConeWidth, surface.RayConeWidthProjected, surface.ObjectspaceNoise );
    ///***********************************************************************

    ///********************* GEOMETRIC NORMAL VARIANCE ***********************
#ifdef VA_RAYTRACING
    float3 normDDX  = (b.WorldspaceNormal.xyz-a.WorldspaceNormal.xyz); // <- do this scaling for raytracing during camera ray setup on ray cone spread angle 
    float3 normDDY  = (c.WorldspaceNormal.xyz-a.WorldspaceNormal.xyz); // <- do this scaling for raytracing during camera ray setup on ray cone spread angle 
    surface.NormalVariance = length( cross( normDDX, normDDY ) ) / triWorldAreaX2 * surface.RayConeWidth * surface.RayConeWidth;
#else
    float3 normDDX  = ddx_fine( surface.WorldspaceNormal.xyz );
    float3 normDDY  = ddy_fine( surface.WorldspaceNormal.xyz );
    // surface.NormalVariance = (dot(normDDX, normDDX) + dot(normDDY, normDDY));   // < listing 2, http://www.jp.square-enix.com/tech/library/pdf/ImprovedGeometricSpecularAA.pdf
    surface.NormalVariance = length( cross( normDDX, normDDY ) );   // my hack until I figure how to unify raytrace and raster paths here :(
#endif
    surface.NormalVariance *= g_globals.GlobalSpecularAAScale * g_globals.GlobalSpecularAAScale;
    ///***********************************************************************

    return surface;
}


#endif // #ifdef VA_RENDERINGSHARED_HLSL_INCLUDED