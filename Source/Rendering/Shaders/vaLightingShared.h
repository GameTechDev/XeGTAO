///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaSharedTypes.h"
#include "vaConversions.h"

#ifndef VA_LIGHTING_SHARED_H
#define VA_LIGHTING_SHARED_H


// IBL integration algorithm
#define IBL_INTEGRATION_PREFILTERED_CUBEMAP         0
#define IBL_INTEGRATION_IMPORTANCE_SAMPLING         1

#define IBL_INTEGRATION                             IBL_INTEGRATION_PREFILTERED_CUBEMAP

// IBL irradiance source
#define IBL_IRRADIANCE_SH                           0
#define IBL_IRRADIANCE_CUBEMAP                      1

#define IBL_IRRADIANCE_SOURCE                       IBL_IRRADIANCE_CUBEMAP

#ifdef VA_COMPILED_AS_SHADER_CODE
#define VA_SHADERCOMPATIBLE_REF
#else
#define VA_SHADERCOMPATIBLE_REF &
#endif

#ifndef VA_COMPILED_AS_SHADER_CODE
namespace Vanilla
{
#endif

//struct ShaderLightDirectional
//{
//    static const uint   MaxLights                       = 16;
//
//    vaVector3           Color;
//    float               Intensity;                      // premultiplied by exposure
//
//    vaVector3           Direction;
//    float               Dummy1;
//
//    vaVector4           SunAreaLightParams;             // special case Sun area light - if Sun.w is 0 then it's not the special case area light (sun)
//};

struct ShaderLightPoint
{
    static const uint   MaxPointLights                  = 128 * 1024;

    // can be compressed to R11G11B10_FLOAT
    vaVector3           Color;							// stored as linear, tools should show srgb though
    float               Intensity;                      // premultiplied by exposure

    vaVector3           Position;
    float               Range;                          // distance at which it is considered that it cannot effectively contribute any light (also used for shadows)

    // can be compressed to 32bit
    vaVector3           Direction;
    float               Size;                           // useful to avoid near-infinities for when close-to-point lights, should be set to biggest acceptable value. see https://youtu.be/wzIcjzKQ2BE?t=884
    // can be compressed to 8/16bit
    float               SpotInnerAngle;                 // angle from Direction below which the spot light has the full intensity (a.k.a. inner cone angle)
    float               SpotOuterAngle;                 // angle from Direction below which the spot light intensity starts dropping (a.k.a. outer cone angle)
    
    float               CubeShadowIndex;                // if used, index of cubemap shadow in the cubemap array texture; otherwise -1
    float               RTSizeModifier;                 // this is used to multiply .Size for RT shadow ray testing - it is temporary and just Size will be used once emissive materials start being done differently (independent from Size)
};

struct ShaderLightTreeNode
{
    // a lot of this could be compressible to fp16
    vaVector3           Center;                         // bounding sphere around Center with (Uncertainty)Radius that contains all nodes below
    float               UncertaintyRadius;              // bounding sphere around Center with (Uncertainty)Radius that contains all nodes below
    float               IntensitySum;                   // light intensity sum for all nodes below
    float               RangeAvg;                       // intensity-weighted avg of ShaderLightPoint::Range for all nodes below
    float               SizeAvg;                        // min of ShaderLightPoint::Size

    VA_INLINE void      SetDummy( )                     // set as bogus node - doesn't do anything, should return 0 by any weight functions
    {
        Center.x = 0; Center.y = 0; Center.z = 0;
        UncertaintyRadius = 0; IntensitySum = 0; RangeAvg = -1; SizeAvg = 1;
    }
    VA_INLINE bool      IsDummy( ) VA_CONST             { return RangeAvg == -1; }

    // This returns diffuse only weight; use MaterialLightWeight for more accurate material-specific weight
    VA_INLINE float     Weight( const vaVector3 VA_REFERENCE pos ) VA_CONST;

};

struct IBLProbeConstants
{
    vaMatrix4x4             WorldToGeometryProxy;               // used for parallax geometry proxy
    vaMatrix4x4             WorldToFadeoutProxy;                // used to transition from (if enabled) local to (if enabled) distant IBL regions
    vaVector4               DiffuseSH[9];

    uint                    Enabled;
    float                   PreExposedLuminance;
    float                   MaxReflMipLevel;
    float                   Pow2MaxReflMipLevel;                // = (float)(1 << (uint)MaxMipLevel)

    vaVector3               Position;                           // cubemap capture position
    float                   ReflMipLevelClamp;                  // either == to MaxReflMipLevel, or slightly lower to reduce impact of low resolution at the last cube MIP

    vaVector3               Extents;                            // a.k.a. size / 2
    float                   UseProxy;
};

struct ShaderLightingConstants
{
    static const int        MaxShadowCubes                  = 10;   // so the number of cube faces is x6 this - lots of RAM

    vaVector4               AmbientLightFromDistantIBL;         // a hack.

    // See vaFogSphere for descriptions
    vaVector3               FogCenter;
    int                     FogEnabled;

    vaVector3               FogColor;
    float                   FogRadiusInner;

    float                   FogRadiusOuter;
    float                   FogBlendCurvePow;
    float                   FogBlendMultiplier;
    float                   FogRange;                   // FogRadiusOuter - FogRadiusInner

    vaMatrix4x4             EnvmapRotation;             // ideally we shouldn't need this but at the moment we support this to simplify asset side...
    
    int                     EnvmapEnabled;
    float                   EnvmapMultiplier;
    uint                    Dummy1;
    uint                    Dummy2;

    uint                    Dummy0;
    float                   ShadowCubeDepthBiasScale;           // scaled by 1.0 / (float)m_shadowCubeResolution
    float                   ShadowCubeFilterKernelSize;         // scaled by 1.0 / (float)m_shadowCubeResolution
    float                   ShadowCubeFilterKernelSizeUnscaled; // same as above but not scaled by 1.0 / (float)m_shadowCubeResolution

    vaVector2               AOMapTexelSize;                     // one over texture resolution
    int                     AOMapEnabled;
    uint                    LightCountPoint;

    IBLProbeConstants       LocalIBL;
    IBLProbeConstants       DistantIBL;

    int                     LightTreeTotalElements;             // all levels together; should be LightTreeBottomLevelSize/2
    int                     LightTreeDepth;
    int                     LightTreeBottomLevelSize;
    int                     LightTreeBottomLevelOffset;
};

    VA_INLINE float ShaderLightRangeAttenuation( float distanceSquare, float range )
    {
        // falloff looks like this: https://www.desmos.com/calculator/uboytsdeyt
        float falloff = 1.0f / (range * range);
        float factor = distanceSquare * falloff;
        float smoothFactor = VA_SATURATE(1.0f - factor * factor * factor);
        return smoothFactor * smoothFactor;
    }

    VA_INLINE float ShaderLightDistanceAttenuation( float distance, float distanceSquare, float size )
    {   // from http://www.cemyuksel.com/research/pointlightattenuation/ 
        float sizeSquare = size*size;
        return 2.0f / (distanceSquare + sizeSquare + distance * sqrt(distanceSquare+sizeSquare) );
    }

    VA_INLINE float ShaderLightAttenuation( float distance, float range, float size )
    {
        float distanceSquare = distance*distance;
        return ShaderLightRangeAttenuation( distanceSquare, range ) * ShaderLightDistanceAttenuation( distance, distanceSquare, size );
    }

    VA_INLINE float ShaderLightTreeNode::Weight( const vaVector3 VA_REFERENCE pos ) VA_CONST
    {
        vaVector3 delta = Center - pos;
        float distance = VA_MAX( UncertaintyRadius, (VA_LENGTH(delta)-UncertaintyRadius) );    // this is distance to node sphere
        return IntensitySum * ShaderLightAttenuation( distance, RangeAvg, SizeAvg );
    };


#ifndef VA_COMPILED_AS_SHADER_CODE
} // namespace Vanilla
#endif


#endif // #ifndef VA_LIGHTING_SHARED_H
