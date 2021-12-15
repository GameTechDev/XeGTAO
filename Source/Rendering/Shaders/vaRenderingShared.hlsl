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


float3 SphericalToCartesian( float azimuthAngle, float polarAngle )
{
    float3 outVector;
    outVector.x = sin( polarAngle ) * cos( azimuthAngle );
    outVector.y = sin( polarAngle ) * sin( azimuthAngle );
    outVector.z = cos( polarAngle );
    return outVector;
}

float IntersectRaySphere( float3 rayOrigin, float3 rayDir, float3 spherePos, float sphereRadius )
{
    float3 e = rayOrigin - spherePos;
    float3 d = rayDir;

    float a = 1.0;
    float b = 2.0 * dot(d,e);
    float c = dot(e,e) - sphereRadius*sphereRadius;

    float det = b*b - 4.0*a*c;

    if (det <= 0.0) {
        return 0.0f;
    }

    return (-b - sqrt(det)) / (2.0 * a);
}

//float3 NearestRaySphere( float3 rayOrigin, float3 rayDir, float3 spherePos, float sphereRadius )
//{
//    float3 surfaceToCenter = spherePos - rayOrigin;
//    float3 centerToRay = dot( surfaceToCenter, rayDir ) * rayDir - surfaceToCenter;
//    return surfaceToCenter + centerToRay * saturate( sphereRadius / length(centerToRay) );
//}

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

// same as above, just returning the matrix
float3x3 ComputeOrthonormalBasis( float3 n )
{
    float3x3 ret; ret[2] = n;
    ComputeOrthonormalBasis( n, ret[0], ret[1] );
    return ret;
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

float PerceptualRoughnessToRoughness(float perceptualRoughness) 
{
    return perceptualRoughness * perceptualRoughness;
}

float RoughnessToPerceptualRoughness(float roughness) 
{
    return sqrt(roughness);
}


// could be part of material properties itself?
#define GEOMETRIC_SPECULAR_AA_VARIANCE      (0.08)      // 0.02
#define GEOMETRIC_SPECULAR_AA_THRESHOLD     (0.003)     // 0.002

#if 0
float normalFiltering(float perceptualRoughness, const float3 worldNormal)
{
    // Kaplanyan 2016, "Stable specular highlights"
    // Tokuyoshi 2017, "Error Reduction and Simplification for Shading Anti-Aliasing"
    // Tokuyoshi and Kaplanyan 2019, "Improved Geometric Specular Antialiasing"

    // This implementation is meant for deferred rendering in the original paper but
    // we use it in forward rendering as well (as discussed in Tokuyoshi and Kaplanyan
    // 2019). The main reason is that the forward version requires an expensive transform
    // of the half vector by the tangent frame for every light. This is therefore an
    // approximation but it works well enough for our needs and provides an improvement
    // over our original implementation based on Vlachos 2015, "Advanced VR Rendering".

    float3 du = ddx( worldNormal ) * g_globals.GlobalSpecularAAScale.xxx;
    float3 dv = ddy( worldNormal ) * g_globals.GlobalSpecularAAScale.xxx;

    // this should maybe be part of material itself
    const float specularAntiAliasingVariance    = GEOMETRIC_SPECULAR_AA_VARIANCE;
    const float specularAntiAliasingThreshold   = GEOMETRIC_SPECULAR_AA_THRESHOLD;

    float variance = /*materialParams._*/specularAntiAliasingVariance * (dot(du, du) + dot(dv, dv));

    float roughness = PerceptualRoughnessToRoughness(perceptualRoughness);
    float kernelRoughness = min(2.0 * variance, /*materialParams._*/specularAntiAliasingThreshold);
    float squareRoughness = saturate(roughness * roughness + kernelRoughness);

    return RoughnessToPerceptualRoughness(sqrt(squareRoughness));
}
#endif

#if 1 //defined(GEOMETRIC_SPECULAR_AA_UNIFIED)
// Same as above but variance is pre-computed
float normalFiltering(float perceptualRoughness, float variance)
{
    // Kaplanyan 2016, "Stable specular highlights"
    // Tokuyoshi 2017, "Error Reduction and Simplification for Shading Anti-Aliasing"
    // Tokuyoshi and Kaplanyan 2019, "Improved Geometric Specular Antialiasing"

    // This implementation is meant for deferred rendering in the original paper but
    // we use it in forward rendering as well (as discussed in Tokuyoshi and Kaplanyan
    // 2019). The main reason is that the forward version requires an expensive transform
    // of the half vector by the tangent frame for every light. This is therefore an
    // approximation but it works well enough for our needs and provides an improvement
    // over our original implementation based on Vlachos 2015, "Advanced VR Rendering".

    // this should maybe be part of material itself
    const float specularAntiAliasingVariance    = GEOMETRIC_SPECULAR_AA_VARIANCE;
    const float specularAntiAliasingThreshold   = GEOMETRIC_SPECULAR_AA_THRESHOLD;

    variance = /*materialParams._*/specularAntiAliasingVariance * variance;

    float roughness = PerceptualRoughnessToRoughness(perceptualRoughness);
    float kernelRoughness = min(2.0 * variance, /*materialParams._*/specularAntiAliasingThreshold);
    float squareRoughness = saturate(roughness * roughness + kernelRoughness);

    return RoughnessToPerceptualRoughness(sqrt(squareRoughness));
}
#endif


// See https://www.pbr-book.org/3ed-2018/Reflection_Models/Specular_Reflection_and_Transmission#FresnelReflectance
//  * eta       : relative index of refraction: etaI / etaT
//  * cosThetaI : cos of angle between normal and incident medium ray direction
//  * f         : returned fresnel term
//  * cosThetaT : returned cos of angle between -normal and refracted direction (0 when it's total internal reflection)
void FresnelDielectric( float eta, float cosThetaI, out float f, out float cosThetaT )
{
    // unlike in pbr-book, cosThetaI is expected to always >= 0; otherwise do 'eta = 1/eta' and 'cosThetaI = -costThetaI' - see "Potentially swap indices of refraction"

    // compute cosThetaT using Snell’s law
    const float sinThetaTSq = Sq(eta) * max(0, 1 - Sq(cosThetaI));

    if( sinThetaTSq >= 1 )
    {
        f = 1;
        cosThetaT = 0;
    }
    else
    {
        cosThetaT = sqrt( max( 0, 1 - sinThetaTSq ) );

        float Rparl = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);
        float Rperp = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);
        f = (Sq(Rparl) + Sq(Rperp)) / 2.0;
    }
}
// shorter version of the above
float FresnelDielectric( float eta, float cosThetaI )
{
    float f, cosThetaT;
    FresnelDielectric( eta, cosThetaI, f, cosThetaT );
    return f;
}


/** Evaluates the Fresnel term using dieletric fresnel equations.
Based on http://www.pbr-book.org/3ed-2018/Reflection_Models/Specular_Reflection_and_Transmission.html

\param[in] eta Relative index of refraction (etaI / etaT).
\param[in] cosThetaI Cosine of angle between normal and incident direction.
\param[out] cosThetaT Cosine of angle between negative normal and transmitted direction (0 for total internal reflection).
\return Returns Fr(eta, cosThetaI).
*/
float FresnelDielectric( float eta, float cosThetaI, out float cosThetaT )
{
    if (cosThetaI < 0)
    {
        eta = 1 / eta;
        cosThetaI = -cosThetaI;
    }

    float sinThetaTSq = eta * eta * (1 - cosThetaI * cosThetaI);
    // Check for total internal reflection
    if (sinThetaTSq > 1)
    {
        cosThetaT = 0;
        return 1;
    }

    cosThetaT = sqrt(1 - sinThetaTSq); // No clamp needed

                                       // Note that at eta=1 and cosThetaI=0, we get cosThetaT=0 and NaN below.
                                       // It's important the framework clamps |cosThetaI| or eta to small epsilon.
    float Rs = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);
    float Rp = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);

    return 0.5 * (Rs * Rs + Rp * Rp);
}

// see https://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Importance_Sampling
float BalanceHeuristic(int nf, float fPdf, int ng, float gPdf) 
{
    float f = nf * fPdf;
    float g = ng * gPdf;
    return (f) / (f + g);
}
float PowerHeuristic(int nf, float fPdf, int ng, float gPdf) 
{
    float f = nf * fPdf;
    float g = ng * gPdf;
    return (f * f) / (f * f + g * g);
}


#endif // #ifdef VA_RENDERINGSHARED_HLSL_INCLUDED