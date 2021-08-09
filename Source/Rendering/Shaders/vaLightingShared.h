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
    float               Size;                           // distance from which to start attenuating or compute umbra/penumbra/antumbra / compute specular (making this into a 'sphere' light) - useful to avoid near-infinities for when close-to-point lights
    // can be compressed to 8/16bit
    float               SpotInnerAngle;                 // angle from Direction below which the spot light has the full intensity (a.k.a. inner cone angle)
    float               SpotOuterAngle;                 // angle from Direction below which the spot light intensity starts dropping (a.k.a. outer cone angle)
    
    float               CubeShadowIndex;                // if used, index of cubemap shadow in the cubemap array texture; otherwise -1
    float               RTSizeModifier;                 // this is used to multiply .Size for RT shadow ray testing - it is temporary and just Size will be used once emissive materials start being done differently (independent from Size)
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

    vaVector4               AmbientLightIntensity;                  // TODO: remove this

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


    // ShaderLightDirectional  LightsDirectional[ShaderLightDirectional::MaxLights];
    
    //ShaderLightPoint        LightsPoint[ShaderLightPoint::MaxLights];
    //ShaderLightPoint        LightsSpotAndPoint[ShaderLightPoint::MaxLights];

    //ShaderLightPoint        LightsSimplePoint[ShaderLightPoint::MaxSimpleLights];

    IBLProbeConstants       LocalIBL;
    IBLProbeConstants       DistantIBL;

    int                     SLC_TLASLeafStartIndex;
    float                   SLC_ErrorLimit;
    float                   SLC_SceneRadius;
    int                     SLC_Dummy2;

    //
    // vaVector4               ShadowCubes[MaxShadowCubes];    // .xyz is cube center and .w is unused at the moment
};

#ifndef VA_COMPILED_AS_SHADER_CODE
} // namespace Vanilla
#endif


#endif // #ifndef VA_LIGHTING_SHARED_H
