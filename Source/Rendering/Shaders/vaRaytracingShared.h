///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VA_RAYTRACING_SHARED_HLSL
#define VA_RAYTRACING_SHARED_HLSL

#include "vaSharedTypes.h"

#ifdef VA_COMPILED_AS_SHADER_CODE
#include "vaRenderMesh.hlsl"
#endif


#define VA_RAYTRACING_SHADER_CALLABLES_PERMATERIAL          1	// effectively, stride
#define VA_RAYTRACING_SHADER_CALLABLES_SHADE_OFFSET         0	// effectively, ID of the specific callable shader

// Miss shader-based API path to allow for callables that support TraceRay; use VA_RAYTRACING_SHADER_MISSCALLABLES_SHADE_OFFSET and null acceleration structure to invoke; see https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#callable-shaders
#define VA_RAYTRACING_SHADER_MISS_CALLABLES_SHADE_OFFSET    2	// effectively, ID of the specific callable shader  (first two are for vaRaytraceItem::Miss and MissSecondary)


// nice 32bit random primes from here: https://asecuritysite.com/encryption/random3?val=32
#define VA_RAYTRACING_HASH_SEED_AA                      0x09FFF95B
#define VA_RAYTRACING_HASH_SEED_DIR_INDIR_LIGHTING_1D   0x2FB8FF47      // these are 1D (choice) and 2D (sample) used by both direct and indirect lighting - this can be done because:
#define VA_RAYTRACING_HASH_SEED_DIR_INDIR_LIGHTING_2D   0x74DDDA53      // Turquin - From Ray to Path Tracing: "Note that you can and should reuse the same sample for light and material sampling at a given depth, since they are independent integral computations, merely combined together in a weighted sum by MIS."
#define VA_RAYTRACING_HASH_SEED_RUSSIAN_ROULETTE        0x1D6F5FC9
#define VA_RAYTRACING_HASH_SEED_LIGHTING_SPEC           0xD19ED69B      // used for tree traversal or etc
#define VA_RAYTRACING_HASH_SEED_PLACEHOLDER2            0xFBD0A37F
#define VA_RAYTRACING_HASH_SEED_PLACEHOLDER3            0xC6456085
#define VA_RAYTRACING_HASH_SEED_PLACEHOLDER4            0x8FCEC1EF

//#define VA_RAYTRACING_BOUCE_COUNT_MASK                  (0xFFFF)        // surely no more than 65535 bounces?
#define VA_RAYTRACING_FLAG_NOT_USED_AT_THE_MOMENT       (1 << 16)       // this is a visibility only ray - no closest hit shader; this flag serves dual purpose: miss shader clears it to indicate a miss
#define VA_RAYTRACING_FLAG_LAST_BOUNCE                  (1 << 17)       // 
#define VA_RAYTRACING_FLAG_PATH_REGULARIZATION          (1 << 18)
#define VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_VIZ          (1 << 19)
#define VA_RAYTRACING_FLAG_SHOW_DEBUG_LIGHT_VIZ         (1 << 20)
#define VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_DETAIL_VIZ   (1 << 21)
#define VA_RAYTRACING_FLAG_STOPPED                      (1 << 22)       // 

#ifndef VA_COMPILED_AS_SHADER_CODE
namespace Vanilla
{
#endif

    // used for individual path tracing rays or visibility rays
    struct ShaderMultiPassRayPayload
    {
        uint                    PathIndex;                  // a.k.a. path index; (1<31) used as a visibility flag
        float                   ConeSpreadAngle;
        float                   ConeWidth;      
    };

    // for visibility rays - not actually used at the moment to avoid shader complexity but it would be an easy optimization
    // WARNING: changing this at runtime requires C++ code rebuild due to sizeof() while setting up raytracing PSO
    struct ShaderRayPayloadGeneric
    {
        vaVector2i              PixelPos;                   // set by caller, useful for debugging or outputting
        float                   ConeSpreadAngle;            // set by caller, updated by callee: see RayTracingGems, Chapter 20 Texture Level of Detail Strategies for Real-Time Ray Tracing
        float                   ConeWidth;                  // set by caller, updated by callee: see RayTracingGems, Chapter 20 Texture Level of Detail Strategies for Real-Time Ray Tracing
        vaVector3               AccumulatedRadiance;        // initialized by caller, updated by callee
        uint                    HashSeed;                   // set by caller, updated on the way
        vaVector3               Beta;                       // initialized by caller, updated by callee; a.k.a. accumulatedBSDF - Beta *= BSDFSample::F / BSDFSample::PDF
        uint                    Flags;                      // Various VA_RAYTRACING_FLAG_* flags
        vaVector3               NextRayOrigin;              // fill in to continue path tracing or ignore
        int                     BounceIndex;                // each bounce adds one! (intentionally int)
        vaVector3               NextRayDirection;           // fill in to continue path tracing
        float                   AccumulatedRayTravel;       // sometimes useful
    };


    // this contains all that is needed to compute hit (and continue path tracing); it is a bit chunky but precision is needed on all of these
    struct ShaderGeometryHitPayload
    {
        vaVector3               RayDirLength;               // ray direction * ray length           ( WorldRayDirection( ) * RayTCurrent() )
        //vaVector3               RayOrigin;                  // ray origin                           ( WorldRayOrigin( ) )
        uint                    PrimitiveIndex;             // specifies the triangle               ( PrimitiveIndex() )
        vaVector2               Barycentrics;               // BuiltInTriangleIntersectionAttributes::barycentrics
        uint                    InstanceIndex;              // specifies the object instance        ( InstanceIndex()  )
        uint                    MaterialIndex;              // stored in InstanceID( )
        //uint                    SortKey;                    // how to sort path dispatch order <- moved to separate buffer
    };


#ifndef VA_COMPILED_AS_SHADER_CODE
}
#endif

#if defined(VA_COMPILED_AS_SHADER_CODE) || defined(__INTELLISENSE__)

#include "vaRenderingShared.hlsl"

// http://www.pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Reflection_Functions.html
struct BSDFSample
{
    float3  F;              // BSDF function evaluated along the Wi
    float3  Wi;             // light incident direction; quasi-random direction for continued path tracing, picked by the appropriate probability distribution
    float   PDF;            // pdf of the above Wi, measured with respect to solid angle
};

#if defined(VA_RAYTRACING) || defined(__INTELLISENSE__)

RaytracingAccelerationStructure g_raytracingScene   : register( T_CONCATENATER( SHADERGLOBAL_SRV_SLOT_RAYTRACING_ACCELERATION ), space0 );

void LoadHitSurfaceInteraction( /*const uint2 dispatchRaysIndex,*/ const float2 barycentrics, const uint instanceIndex, const uint primitiveIndex, const float3 rayDirLength, const float rayConeSpreadAngle, const float rayConeWidth, out ShaderInstanceConstants instanceConstants, out ShaderMeshConstants meshConstants, out ShaderMaterialConstants materialConstants, out SurfaceInteraction surface )
{
    instanceConstants = LoadInstanceConstants( instanceIndex );
    materialConstants = g_materialConstants[instanceConstants.MaterialGlobalIndex];
    meshConstants     = g_meshConstants[instanceConstants.MeshGlobalIndex];

#if 0 // structured doesn't work because bindless reasons
    uint indexOffset = primitiveIndex * 3;
    uint indexA = g_bindlessIndices[meshConstants.IndexBufferBindlessIndex][indexOffset+0];
    uint indexB = g_bindlessIndices[meshConstants.IndexBufferBindlessIndex][indexOffset+1];
    uint indexC = g_bindlessIndices[meshConstants.IndexBufferBindlessIndex][indexOffset+2];

    RenderMeshVertex vertA = g_bindlessVertices[meshConstants.VertexBufferBindlessIndex][indexA];
    RenderMeshVertex vertB = g_bindlessVertices[meshConstants.VertexBufferBindlessIndex][indexB];
    RenderMeshVertex vertC = g_bindlessVertices[meshConstants.VertexBufferBindlessIndex][indexC];
#else // ...so use byte address for now gah 
    uint indexByteOffset = primitiveIndex * 3 * 4;
    // load 3 indices
    uint3 indices = g_bindlessBAB[meshConstants.IndexBufferBindlessIndex].Load3(indexByteOffset);
    RenderMeshVertex vertA, vertB, vertC;
    // load 3 vertices
    vertA = RenderMeshManualVertexLoad( meshConstants.VertexBufferBindlessIndex, indices.x );
    vertB = RenderMeshManualVertexLoad( meshConstants.VertexBufferBindlessIndex, indices.y );
    vertC = RenderMeshManualVertexLoad( meshConstants.VertexBufferBindlessIndex, indices.z );
#endif
    // shade 3 vertices
    ShadedVertex interpA, interpB, interpC;
    interpA = RenderMeshVertexShader( vertA, instanceConstants );
    interpB = RenderMeshVertexShader( vertB, instanceConstants );
    interpC = RenderMeshVertexShader( vertC, instanceConstants );

    surface = SurfaceInteraction::ComputeAtRayHit( interpA, interpB, interpC, barycentrics, rayDirLength, rayConeSpreadAngle, rayConeWidth, meshConstants.FrontFaceIsClockwise );

#if 0 // debugging
    [branch] if( IsUnderCursorRange( dispatchRaysIndex, int2(1,1) ) )
    {
        DebugDraw3DLine( interpA.WorldspacePos, interpB.WorldspacePos, float4( 1, 0, 0, 1 ) );
        DebugDraw3DLine( interpB.WorldspacePos, interpC.WorldspacePos, float4( 0, 1, 0, 1 ) );
        DebugDraw3DLine( interpC.WorldspacePos, interpA.WorldspacePos, float4( 0, 0, 1, 1 ) );
        DebugDraw3DText( surface.WorldspacePos, float2( 0, 40 ), float4( 0.8, 0.8, 0.8, 1 ), surface.NormalVariance );
    }
#endif
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
// TODO: add near/far clip planes
inline void GenerateCameraRay( uint2 pixelCoords, float2 viewportSize, float2 subPixelJitter, out float3 origin, out float3 direction, out float coneSpreadAngle )
{
    // should we use 
    float2 xy           = pixelCoords + 0.5f + subPixelJitter; // center in the middle of the pixel.
    float2 screenPos    = xy / viewportSize * float2( 2.0, -2.0 ) + float2( -1.0, 1.0 );

    // Unproject the pixel coordinate into a ray. Could just use orthonormal camera basis - but this gives us any potential jitter applied to projection matrix too.
    float4 world        = mul( g_globals.ViewProjInv, float4(screenPos, 0, 1) );

    world.xyz /= world.w;
    origin = g_globals.CameraWorldPosition.xyz;
    direction = normalize(world.xyz - origin);

    coneSpreadAngle = g_globals.PixelFOVXY.x;
}

// This is the 'offset_ray' from listing 6-1, Ray Tracing Gems 1, Chapter 6, by Carsten Wächter and Nikolaus Binder, NVIDIA
// Normal points outward for rays exiting the surface, else is flipped.
float3 OffsetNextRayOrigin( const float3 p, const float3 n )
{
    //return p + n * 0.0005; <- the naive approach
    const float origin      = 1.0f / 32.0f;
    const float float_scale = 1.0f / 65536.0f;
    const float int_scale   = 256.0f;

    int3 of_i = { int_scale * n.x, int_scale * n.y, int_scale * n.z };

    float3  p_i = { asfloat(asint(p.x)+((p.x < 0) ? -of_i.x : of_i.x)),
                    asfloat(asint(p.y)+((p.y < 0) ? -of_i.y : of_i.y)),
                    asfloat(asint(p.z)+((p.z < 0) ? -of_i.z : of_i.z)) };

    return float3(  ( abs(p.x) < origin ) ? ( p.x + float_scale * n.x ) : (p_i.x) ,
                    ( abs(p.y) < origin ) ? ( p.y + float_scale * n.y ) : (p_i.y) ,
                    ( abs(p.z) < origin ) ? ( p.z + float_scale * n.z ) : (p_i.z) );
}

float3 RayDebugColor( uint recursionDepth )
{
    return GradientRainbow( frac( recursionDepth / 16.0 ) );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Various multi-dimensional sampling functions that take a canonical uniform variable(s) and transform them to a specific PDF
// See http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Transforming_between_Distributions.html for background and 
// "..\pbrt-v3\src\core\sampling.cpp" for code reference
//
// The optional 'pdf' return values are defined with respect to solid angle (not spherical coordinates). 
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations.html#SamplingaUnitDisk
float2 UniformSampleDisk( const float2 u )
{
    float r = sqrt(u[0]);
    float theta = 2 * VA_PI * u[1];
    return float2(r * cos(theta), r * sin(theta));
}
float2 ConcentricSampleDisk( const float2 u )
{
    // Map uniform random numbers to $[-1,1]^2$
    float2 uOffset = 2.f * u - float2(1, 1);

    // Handle degeneracy at the origin
    if (uOffset.x == 0 && uOffset.y == 0) return float2(0, 0);

    // Apply concentric mapping to point
    float theta, r;
    if( abs(uOffset.x) > abs(uOffset.y) )
    {
        r = uOffset.x;
        theta = (VA_PI/4.0) * (uOffset.y / uOffset.x);
    } 
    else 
    {
        r = uOffset.y;
        theta = (VA_PI/2.0) - (VA_PI/4.0) * (uOffset.x / uOffset.y);
    }
    return r * float2( cos(theta), sin(theta) );
}
//////////////////////////////////////////////////////////////////////////

// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations.html
float3 SampleHemisphereUniform( const float2 u )
{
    float z = u[0];
    float r = sqrt( max( 0.0, 1.0 - z * z) );
    float phi = 2 * VA_PI * u[1];
    return float3(r * cos(phi), r * sin(phi), z);
}
float SampleHemisphereUniformPDF( )
{
    return 1.0 / (2.0 * VA_PI);
}
float3 SampleHemisphereUniform( const float2 u, out float pdf )
{
    pdf = SampleHemisphereUniformPDF( );
    return SampleHemisphereUniform( u );
}
//////////////////////////////////////////////////////////////////////////

// http://www.rorydriscoll.com/2009/01/07/better-sampling/
float3 SampleHemisphereCosineWeighted( const float2 u )
{
    const float r = sqrt(u.x);
    const float theta = 2 * VA_PI * u.y;

    const float x = r * cos(theta);
    const float y = r * sin(theta);

    return float3(x, y, sqrt(max(0.0, 1.0 - u.x)));
    //return normalize( float3(x, y, fireflyFudge+sqrt(max(0.0, 1.0 - u.x)) ) );
}
float SampleHemisphereCosineWeightedPDF( float cosTheta )
{ 
    // // this fireflyFudge is energy conserving for diffuse materials (integrates to 1)
    // cosTheta = fireflyFudge + cosTheta * (1-fireflyFudge*2.0);
    return max( 0, cosTheta ) / VA_PI;
}
// Set fireflyFudge to [0, 0.2] as needed (default 0.05, min recommended 1e-4).
float3 SampleHemisphereCosineWeighted( const float2 u, out float pdf )
{
    float3 ret = SampleHemisphereCosineWeighted( u/*, fireflyFudge*/ );
    pdf = SampleHemisphereCosineWeightedPDF( ret.z/*, fireflyFudge*/ );
    return ret;
}
//////////////////////////////////////////////////////////////////////////

// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations.html#SamplingaCone
float3 SampleConeUniform( const float2 u, float cosThetaMax ) 
{
    float cosTheta = (1.0 - u[0]) + u[0] * cosThetaMax;
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = u[1] * 2 * VA_PI;
    return float3( cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta );
}
float SampleConeUniformPDF( float cosThetaMax )
{
    return 1.0 / (2.0 * VA_PI * (1.0 - cosThetaMax));
}
float3 SampleConeUniform( const float2 u, float cosThetaMax, out float pdf )
{
    pdf = SampleConeUniformPDF( cosThetaMax );
    return SampleConeUniform( u, cosThetaMax );
}
//////////////////////////////////////////////////////////////////////////

// See the original 'sampleGGXVNDF' in vaRenderingShader.hlsl!
// 'Ve' == 'Wo' (outgoing direction, a.k.a. view vector)
// The original outputs a microfacet normal with PDF D_Ve(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z 
// This version additionally converts the normal to reflected ray (Wi) and computes pdf as well so it can 
// fit into existing mix-material pipeline.
// An additional useful reference: 
//  * https://github.com/mmanzi/gradientdomain-mitsuba/blob/master/src/bsdfs/microfacet.h
// Set fireflyFudge to [0, 0.2] as needed (default 0.05, min recommended 1e-4).
float SampleGGXVNDF_PDF( float3 Wo, float3 Wi, float alpha_x, float alpha_y )
{
    float3 Ne = normalize( Wo + Wi );
    return VNDFDistributionPDF( Wo, Ne, alpha_x, alpha_y/* , fireflyFudge*/ );
}
float3 SampleGGXVNDF( float3 Wo, float alpha_x, float alpha_y, float2 u, out float pdf )
{
    // Section 3.2: transforming the view direction to the hemisphere configuration
    float3 Vh = normalize(float3(alpha_x * Wo.x, alpha_y * Wo.y, Wo.z));
    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1,0,0);
    float3 T2 = cross(Vh, T1);
    // Section 4.2: parameterization of the projected area
    float r = sqrt(u.x);	
    float phi = 2.0 * VA_PI * u.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s)*sqrt(1.0 - t1*t1) + s*t2;
    // Section 4.3: reprojection onto hemisphere
    float3 Nh = t1*T1 + t2*T2 + sqrt(max(0.0, 1.0 - t1*t1 - t2*t2))*Vh;
    // Section 3.4: transforming the normal back to the ellipsoid configuration
    float3 Ne = normalize( float3(alpha_x * Nh.x, alpha_y * Nh.y, /*fireflyFudge +*/ max( 0, Nh.z)) );

    // reflect view against the surface normal (http://jcgt.org/published/0007/04/01/paper.pdf Appendix B, Equation 16)
    float3 Wi = reflect( -Wo, Ne );

    // reflect view against the surface normal (http://jcgt.org/published/0007/04/01/paper.pdf Appendix B, Equations 17, 18)
    pdf = VNDFDistributionPDF( Wo, Ne, alpha_x, alpha_y/*, fireflyFudge*/ );
    //pdf = SampleGGXVNDF_PDF( Wo, Wi, alpha_x, alpha_y, fireflyFudge ); ^^above is same as this but less math

    return Wi;
}
//
#endif // #ifdef VA_RAYTRACING

#endif // #ifdef VA_COMPILED_AS_SHADER_CODE

#endif // VA_RAYTRACING_SHARED_HLSL