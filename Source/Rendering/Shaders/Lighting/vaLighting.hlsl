///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VA_LIGHTING_HLSL
#define VA_LIGHTING_HLSL

#include "../vaShared.hlsl"
#include "vaLightingShared.h"

#include "../vaGeometryInteraction.hlsl"

#include "../PoissonDisks/vaPoissonDisk8.h"

// global settings
//#define VA_SKIP_LIGHTING_WITH_ZERO_N_DOT_L <- not implemented, enable and errors will pop where implementation needed


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-shaded-pixel intermediate data structures

// Used to transfer simple type of light (directional, point, spot, etc.) information to actual material surface
// lighting code. Originally from https://github.com/google/filament but modified.
struct LightParams
{
    float3  ColorIntensity;     // rgb, pre-exposed color.rgb * intensity.xxx
    float3  L;                  // light direction (worldspace) - perhaps should rename to 'Wi'
    float   Dist;               // length of L vector before it was normalized
    float   Attenuation;
    float   NoL;
};

#define VA_CUBE_SHADOW_TAP_COUNT (3)

// All cubemap shadows have same resolution & setup so they share a lot of the parameters
struct CubeShadowsParams
{
    float3      TapOffsets[VA_CUBE_SHADOW_TAP_COUNT];
    //    float       ViewDepth;
    //    float       Something;
};

struct IBLParams
{
    bool        UseLocal;               // local enabled
    bool        UseDistant;             // distant enabled

    float       LocalToDistantK;        // interpolation factor
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//TextureCube         g_EnvironmentMap            : register( T_CONCATENATER( SHADERGLOBAL_LIGHTING_ENVMAP_TEXTURESLOT_V ) );
TextureCubeArray    g_CubeShadowmapArray                : register( T_CONCATENATER( SHADERGLOBAL_LIGHTING_CUBE_SHADOW_TEXTURESLOT_V ) );

TextureCube         g_LocalIBLReflectionsMap            : register( T_CONCATENATER( LIGHTINGGLOBAL_LOCALIBL_REFROUGHMAP_TEXTURESLOT_V ) );
TextureCube         g_LocalIBLIrradianceMap             : register( T_CONCATENATER( LIGHTINGGLOBAL_LOCALIBL_IRRADIANCEMAP_TEXTURESLOT_V ) );

TextureCube         g_DistantIBLReflectionsMap          : register( T_CONCATENATER( LIGHTINGGLOBAL_DISTANTIBL_REFROUGHMAP_TEXTURESLOT_V ) );
TextureCube         g_DistantIBLIrradianceMap           : register( T_CONCATENATER( LIGHTINGGLOBAL_DISTANTIBL_IRRADIANCEMAP_TEXTURESLOT_V ) );

Texture2D<uint>     g_AOMap                             : register( T_CONCATENATER( SHADERGLOBAL_AOMAP_TEXTURESLOT_V ) );

StructuredBuffer<ShaderLightPoint> g_lightsPoint        : register( T_CONCATENATER( LIGHTINGGLOBAL_SIMPLELIGHTS_SLOT_V ) );
StructuredBuffer<ShaderLightTreeNode> g_lightTree       : register( T_CONCATENATER( LIGHTINGGLOBAL_LIGHT_TREE_SLOT_V ) );

cbuffer LightingConstantsBuffer                         : register( B_CONCATENATER( LIGHTINGGLOBAL_CONSTANTSBUFFERSLOT_V ) )
{
    ShaderLightingConstants     g_lighting;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared lighting

void SampleSSAO( const uint2 svpos, const float3 shadingNormal, out float aoVisibility, out float3 bentNormal )
{
    // start with default (unused) AO
    aoVisibility    = 1.0; 
    bentNormal      = shadingNormal;

#ifndef VA_RAYTRACING   // not currently connected in raytracing case although it will work for primary non-rays hitting opaque surface!
    
    if( g_lighting.AOMapEnabled == 1 )
    {
        // no filtering allowed as it could be packed bent normal
        uint value = g_AOMap[(uint2)svpos].x;
        aoVisibility = value / 255.0;
    } 
    else if( g_lighting.AOMapEnabled == 2 )
    {
        // no filtering allowed as it could be packed bent normal
        uint value = g_AOMap[(uint2)svpos].x;

        DecodeVisibilityBentNormal( value, aoVisibility, bentNormal );

        // viewspace to worldspace - makes sense to precalculate
        bentNormal = mul( (float3x3)g_globals.ViewInv, bentNormal );
    }

#endif


}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IBL
//

IBLParams ComputeIBLParams( const float3 worldspacePos, const float3 geometricNormal, float VA_RM_LOCALIBL_NORMALBIAS, float VA_RM_LOCALIBL_BIAS )
{
    // some global settings
    const float transitionArea              = 0.25;

    IBLParams ret;

    ret.UseLocal        = g_lighting.LocalIBL.Enabled;
    ret.UseDistant      = g_lighting.DistantIBL.Enabled;

    ret.LocalToDistantK = saturate( ret.UseDistant - ret.UseLocal );
    
    if( ret.UseLocal && g_lighting.LocalIBL.UseProxy )
    {
        float3 dist             = mul( g_lighting.LocalIBL.WorldToFadeoutProxy, float4( worldspacePos, 1 ) ).xyz;

        // this really is a bad hack
//#ifdef VA_RM_LOCALIBL_NORMALBIAS
        dist += mul( (float3x3)g_lighting.LocalIBL.WorldToFadeoutProxy, geometricNormal ) * VA_RM_LOCALIBL_NORMALBIAS;
//#endif
        dist     = abs( dist );

        float md = max( dist.x, max( dist.y, dist.z ) );

        float materialBasedBias = 0.0;
//#ifdef VA_RM_LOCALIBL_BIAS
        materialBasedBias = VA_RM_LOCALIBL_BIAS;
//#endif

        ret.LocalToDistantK     = saturate( 1 - (1-md) / transitionArea - materialBasedBias );
    }
    if( ret.LocalToDistantK < 0.001 )
        ret.UseDistant = 0;
    if( ret.LocalToDistantK > 0.999 )
        ret.UseLocal = 0;

    return ret;
}

float3 ComputeIBLDirection( const float3 worldspacePos, const IBLProbeConstants probe, float3 direction )
{
    [branch]
    if( probe.UseProxy )
    {
        // https://developer.valvesoftware.com/wiki/Parallax_Corrected_Cubemaps

        float3 ReflDirectionWS = direction;

        // Intersection with OBB convert to unit box space
        // Transform in local unit parallax cube space (scaled and rotated)
        float3 RayLS        = mul( (float3x3)probe.WorldToGeometryProxy, ReflDirectionWS );
        float3 PositionLS   = mul( probe.WorldToGeometryProxy, float4( worldspacePos, 1 ) ).xyz;

        float3 Unitary = float3(1.0f, 1.0f, 1.0f);
        float3 FirstPlaneIntersect  = (Unitary - PositionLS) / RayLS;
        float3 SecondPlaneIntersect = (-Unitary - PositionLS) / RayLS;
        float3 FurthestPlane = max(FirstPlaneIntersect, SecondPlaneIntersect);
        float Distance = min(FurthestPlane.x, min(FurthestPlane.y, FurthestPlane.z));

        // Use Distance in WS directly to recover intersection
        float3 IntersectPositionWS = worldspacePos + ReflDirectionWS * Distance;
        return IntersectPositionWS - probe.Position;
    }
    else
    {
        return direction;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shadows
//

CubeShadowsParams ComputeCubeShadowsParams( float noise, float3 worldspacePos )
{
    CubeShadowsParams ret;

    float angle = noise * (3.14159265 * 2.0);

    float ca = cos( angle );
    float sa = sin( angle );
    float scale = g_lighting.ShadowCubeFilterKernelSize;

    // make sure the kernel size is (roughly) independent of the number of taps!
    float refit = 1.0 / clamp( g_poissonDisk[VA_CUBE_SHADOW_TAP_COUNT-1].z, 0.25, 1.0 );

    // think of a better way to use gradient to scale shadow kernel
    // refit *= 1 + max( 0, max( length( ddx(worldspacePos.xyz) ), length( ddy(worldspacePos.xyz ) ) ) * 50.0 - 0.01 );

    scale *= refit;

    float2x2 RotScaleMatrix = float2x2( scale * ca, scale * -sa, scale * sa, scale * ca );

    [unroll]
    for( uint tap = 0; tap < VA_CUBE_SHADOW_TAP_COUNT; tap++ )
    {
        float2 rotatedOffset = mul( RotScaleMatrix, g_poissonDisk[tap].xy );
        ret.TapOffsets[tap] = float3( rotatedOffset, 1.0 + g_poissonDisk[tap].z * g_lighting.ShadowCubeFilterKernelSizeUnscaled * refit );
    }

    return ret;
}

float ComputeCubeShadow( CubeShadowsParams cubeShadowParams, float3 worldGeometryNormal, int cubeShadowIndex, float3 pixelToLightDir, float pixelToLightLength, float lightSize, float lightRange )
{
    // This is to get the slope between the shadow receiving geometry and the light (shadow) direction
    float NoL = dot(worldGeometryNormal,pixelToLightDir);
    //float slopeBias = tan(acos(NoL));
    float slopeBias = sqrt(1 - NoL*NoL) / NoL;  // same as above ^ but with no trigonometry functions
    slopeBias = min( 8.0, slopeBias );          // arbitrarily limit the slope bias, otherwise it can cause light bleeding artifact; 10, 12 are good values too

    // This is the base direction we're sampling the cubemap at
    float3 cubeDir = -pixelToLightDir;

    float3 absCubeDir = abs( cubeDir );
        
#if 0   // not needd for now
    uint longestComponentIndex = 2;
    if( (absCubeDir.z > absCubeDir.x) * (absCubeDir.z > absCubeDir.y) == 0 )
        longestComponentIndex = (absCubeDir.x > absCubeDir.y)?(0):(1);
    float longestComponent = absCubeDir[longestComponentIndex];
    float2 shorterComponents = float2( absCubeDir[(longestComponentIndex+1)%3], (longestComponentIndex+2)%3 );
#else
    float longestComponent = max( max( absCubeDir.x, absCubeDir.y), absCubeDir.z );
#endif

    float cubeDepth = longestComponent * pixelToLightLength;

    // Worldspace cube shadow pixel size: I think this is right? halfPixel / resolution * distance along face axis?
    // (use sqrt(2)/2==0.7071 instead because it is needed when the pixel 'cube' is projected diagonally or something like that)
    float worldspaceDepthBiasScale = g_lighting.ShadowCubeDepthBiasScale * cubeDepth;

    // TODO: maybe precompute these during light setup?
    const float a = lightSize / ( lightSize - lightRange );
    const float b = ( lightSize * lightRange ) / ( lightRange - lightSize );

    const float fixedAndSlopeBias = 1+slopeBias;

    // const float screenSpaceScaling = 1.0 + max( 0, cubeShadowParams.Something ) * 10.0;

#if 0   // manual compare
    // Apply the fixed and slope-based biases
    cubeDepth   -= (worldspaceDepthBiasScale * fixedAndSlopeBias);
    // Convert to cubemap projected depth (NDC z)
    float projZ = (cubeDepth * a + b) / ( cubeDepth );    // see PerspectiveFovLH; camera is using 'm_useReversedZ' so near/far are swapped

    float shadowmapCompVal = g_CubeShadowmapArray.SampleLevel( g_samplerPointClamp, float4( cubeDir, cubeShadowIndex ), 0.0 ).x;
    return( shadowmapCompVal < projZ );
#elif 0 // use cmp sampler, 1-tap
    worldspaceDepthBiasScale *= 1.5; // increase bias to cover 2x2 block from the LinearCmpSampler
    // Apply the fixed and slope-based biases
    cubeDepth   -= (worldspaceDepthBiasScale * fixedAndSlopeBias);
    // Convert to cubemap projected depth (NDC z)
    float projZ = (cubeDepth * a + b) / ( cubeDepth );    // see PerspectiveFovLH; camera is using 'm_useReversedZ' so near/far are swapped
    return g_CubeShadowmapArray.SampleCmpLevelZero( g_samplerLinearCmpSampler, float4( cubeDir, cubeShadowIndex ), projZ ).x;
#else   // use multi-tap with cmp sampler

    float3 jitterX, jitterY;
    ComputeOrthonormalBasis( cubeDir, jitterX, jitterY );
        
    // precompute bias basis before the loop
    const float tapProjZBase = (cubeDepth * a + b) / ( cubeDepth );
    const float cubeDepthBiased1 = cubeDepth-fixedAndSlopeBias*worldspaceDepthBiasScale;
    const float tapProjZBaseB1 = (cubeDepthBiased1 * a + b) / ( cubeDepthBiased1 ) - tapProjZBase;

    float shadowSum = 0.0;
    [unroll]
    for( uint tap = 0; tap < VA_CUBE_SHADOW_TAP_COUNT; tap++ )
    {
        // A LOT of this can be precomputed
        float2 tapOffset        = /*screenSpaceScaling **/ cubeShadowParams.TapOffsets[tap].xy;   //mul( cubeShadowParams.RotScaleMatrix, g_poissonDisk[tap].xy );
        float tapDepthBiasSize  = /*screenSpaceScaling **/ cubeShadowParams.TapOffsets[tap].z;    //(1.0 + g_poissonDisk[tap].z * g_lighting.ShadowCubeFilterKernelSizeUnscaled);

#if 0
        float tapDepth = cubeDepth - (tapDepthBiasSize * worldspaceDepthBiasScale * fixedAndSlopeBias);
        float tapProjZ = (tapDepth * a + b) / ( tapDepth );    // most of this could go outside of the loop as only the first part (tapDepth * a) changes enough
#else
        // using precomputed bias basis
        float tapProjZ = tapProjZBase + tapProjZBaseB1 * tapDepthBiasSize;
#endif

        float3 tapDir = normalize( cubeDir + jitterX * tapOffset.x + jitterY * tapOffset.y );
        float tapShadow = g_CubeShadowmapArray.SampleCmpLevelZero( g_samplerLinearCmpSampler, float4( tapDir, cubeShadowIndex ), tapProjZ ).x;
        //float tapShadow = g_CubeShadowmapArray.SampleLevel( g_samplerPointClamp, float4( tapDir, cubeShadowIndex ), 0.0 ).x < tapProjZ;

#define SHADOW_USE_NON_LINEAR_AVERAGE

#ifdef SHADOW_USE_NON_LINEAR_AVERAGE
        shadowSum += sqrt(tapShadow);
    }
    shadowSum /= (float)VA_CUBE_SHADOW_TAP_COUNT;
    return shadowSum * shadowSum;
#else
        shadowSum += tapShadow;
    }
    shadowSum /= (float)VA_CUBE_SHADOW_TAP_COUNT;
    return shadowSum;
#endif
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fog/atmospherics
//

float LightingGetFogK( float3 surfaceWorldspacePos )
{
    if( g_lighting.FogEnabled )
    {
        float dist = length( surfaceWorldspacePos - g_lighting.FogCenter);
        return g_lighting.FogBlendMultiplier * pow( saturate( (dist - g_lighting.FogRadiusInner) / g_lighting.FogRange), g_lighting.FogBlendCurvePow );
    }
    else
    {
        return 0.0;
    }
}

float3 LightingApplyFog( float3 surfaceWorldspacePos, float3 inColor )
{
    // this is just an easy place to insert any code for visual debugging :)
//    if( all( abs(surfaceWorldspacePos) < 2.0.xxx ) )
//    {
//        return g_test[2];
//        return float3( 1, 0, 0 );
//    }
//    else
    return lerp( inColor, g_lighting.FogColor, LightingGetFogK( surfaceWorldspacePos ) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared lighting

// Compute light params at surface location and normal that are required for further light processing; will not 
// compute visibility, AO or material response, just the "L(p, Wi)"
LightParams EvaluateLightAtSurface( const ShaderLightPoint lightIn, const float3 worldPos, const float3 surfaceNormal ) //  worldPos == surface.WorldspacePos; surfaceNormal == shading.Normal
{
    LightParams light;

    float3 posToLight       = lightIn.Position - worldPos;
    float distanceSquare    = dot( posToLight, posToLight );
    light.Dist              = sqrt( distanceSquare );
    light.L                 = posToLight / light.Dist;
    light.NoL               = saturate( dot( surfaceNormal, light.L ) );

#ifdef VA_SKIP_LIGHTING_WITH_ZERO_N_DOT_L
#error Set all to 0 and leave early if NoL is <=0
#endif 

    light.Attenuation       = ShaderLightRangeAttenuation( distanceSquare, lightIn.Range ) * ShaderLightDistanceAttenuation( light.Dist, distanceSquare, lightIn.Size );

    float intensityIn       = lightIn.Intensity  * g_globals.PreExposureMultiplier;
    light.ColorIntensity    = lightIn.Color * intensityIn;

    // spot attenuation is designed so that if InnerAngle is 0deg and OuterAngle is 90deg then it's a Lambertian emitter - "saturate( dot( lightIn.Direction, -light.L ) )"
    // all the math below is only there to 
    {
        float exitNoL = dot( lightIn.Direction, -light.L );
#if 1
        float angle = acos(exitNoL); //FastAcos( exitNoL ); // <- FastAcos speeds things up SIGNIFICANTLY - consider using in a fast path
        float spotAttenuation = saturate( (lightIn.SpotOuterAngle - angle) / (lightIn.SpotOuterAngle - lightIn.SpotInnerAngle) );
        light.Attenuation *= sin( spotAttenuation * (VA_PI * 0.5) );
#else   // this should also give same results with 0-90deg as Lambertian emitter but be potentially cheaper and allow for pre-compile; let's first think about it a bit
        float cosInner = 1-cos(lightIn.SpotInnerAngle);
        float cosOuter = 1-cos(lightIn.SpotOuterAngle);
        light.Attenuation *= 1-saturate( (1-exitNoL-cosInner) / (cosOuter-cosInner) );
#endif

        //light.Attenuation *= saturate( dot( lightIn.Direction, -light.L ) );

        // old
        // // squaring of spot attenuation is just for a softer outer->inner curve that I like more visually
        // light.Attenuation = spotAttenuation*spotAttenuation;
    }

    return light;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // #ifndef VA_LIGHTING_HLSL
