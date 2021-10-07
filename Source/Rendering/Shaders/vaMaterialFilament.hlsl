///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Based on:    https://github.com/google/filament
//
// Filament is the best fast opensource PBR renderer (that I could find), including excellent documentation and 
// performance. The size and complexity of the Filament library itself is a bit out of scope of what Vanilla is 
// trying to do but I've integrated its PBR shaders and material definitions with as few changes as I could manage. 
// Original Filament shaders are included into the repository but only some are used directly - the rest are in as a 
// reference for enabling easier integration of any future Filament changes.
//
// There are many subtle differences in the implementation though, so beware.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaMaterialLoaders.hlsl"
#include "vaRenderingShared.hlsl"
#include "vaLighting.hlsl"
#include "vaRaytracingShared.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Set up various Filament-specific macros
// 
#if defined( VA_FILAMENT_STANDARD )
#elif defined( VA_FILAMENT_SUBSURFACE )
#define SHADING_MODEL_SUBSURFACE    // filament definition
#elif defined(VA_FILAMENT_CLOTH)
#define SHADING_MODEL_CLOTH         // filament definition
#elif defined( VA_FILAMENT_SPECGLOSS )
#define SHADING_MODEL_SPECULAR_GLOSSINESS
#else
#error Correct version of filament material model has to be set using one of the VA_FILAMENT_* macros
#endif
// Note: Filament SHADING_MODEL_UNLIT is never defined
//

#if VA_RM_ADVANCED_SPECULAR_SHADER
#define GEOMETRIC_SPECULAR_AA_UNIFIED               // this is a path that behaves similarly/same between rasterization and raytracing
// #define GEOMETRIC_SPECULAR_AA                    // <- disabled for now, in order to unify rasterization with raytracing; 
// #define GEOMETRIC_SPECULAR_AA_USE_NORMALMAPS     // <- disabled for now, in order to unify rasterization with raytracing; this should be handled properly with normal->roughness prefiltering anyway
//
#if 1 // this one is a lot more aggressive AA but it seems to be worth it
#define GEOMETRIC_SPECULAR_AA_VARIANCE      (0.08)
#define GEOMETRIC_SPECULAR_AA_THRESHOLD     (0.003)
#else
#define GEOMETRIC_SPECULAR_AA_VARIANCE      (0.02)
#define GEOMETRIC_SPECULAR_AA_THRESHOLD     (0.002)
#endif

#endif
//
// we just always enable these and hope they get compiled out when unused (seems to happen in testing so far)
#define MATERIAL_HAS_NORMAL                 1           // #if defined( VA_RM_HAS_INPUT_Normal )
#define MATERIAL_HAS_EMISSIVE               1           // #if defined( VA_RM_HAS_INPUT_EmissiveColor )
#if defined(MATERIAL_HAS_CLEAR_COAT)
#define MATERIAL_HAS_CLEAR_COAT_NORMAL      1
#endif
//
// 
// These are the defines that I haven't ported/connected yet!
//
// #define MATERIAL_HAS_ANISOTROPY         1
#define SPECULAR_AMBIENT_OCCLUSION      SPECULAR_AO_BENT_NORMALS  // SPECULAR_AO_OFF / SPECULAR_AO_SIMPLE / SPECULAR_AO_BENT_NORMALS
#define MULTI_BOUNCE_AMBIENT_OCCLUSION  1
#define DIRECT_LIGHTING_AO_MICROSHADOWS 1
// #define TARGET_MOBILE
//
// port Vanilla to Filament blend modes
#if VA_RM_TRANSPARENT
#define BLEND_MODE_TRANSPARENT
#elif VA_RM_ALPHATEST
#define BLEND_MODE_MASKED
#else
#define BLEND_MODE_OPAQUE
#endif
#if VA_RM_TRANSPARENT && VA_RM_ALPHATEST
#error this scenario has not been tested with filament (why use alpha testing and transparency at the same time? might be best to disable it in the UI if not needed)
#endif
//
// these are all Filament blend modes listed:
// #define BLEND_MODE_OPAQUE
// #define BLEND_MODE_MASKED
// #define BLEND_MODE_ADD
// #define BLEND_MODE_TRANSPARENT
// #define BLEND_MODE_MULTIPLY
// #define BLEND_MODE_SCREEN
// #define BLEND_MODE_FADE
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// This is where the Filament core gets included
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// GLSL <-> HLSL conversion helpers (this link was helpful: https://dench.flatlib.jp/opengl/glsl_hlsl)

// #if defined(TARGET_MOBILE)
// #define HIGHP highp
// #define MEDIUMP mediump
// #else
#define HIGHP
#define MEDIUMP
// #endif

#define highp
#define mediump

//#if !defined(TARGET_MOBILE) || defined(CODEGEN_TARGET_VULKAN_ENVIRONMENT)
//#define LAYOUT_LOCATION(x) layout(location = x)
//#else
#define LAYOUT_LOCATION(x)
//#endif

#define dFdx    ddx
#define dFdy    ddy

#define vec2    float2
#define vec3    float3
#define vec4    float4
#define mat3    float3x3
#define mat4    float4x4
#define mix     lerp

#define linear  _linear

#define inversesqrt rsqrt
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// includes of filament or ported filament headers!
//
#include "Filament\common_math.fs"
#include "Filament\brdf.va.fs"
#include "Filament\common_getters.va.fs"
#include "Filament\common_graphics.va.fs"
#include "Filament\common_lighting.va.fs"
#include "Filament\common_shading.va.fs"
#include "Filament\common_material.fs"
#include "Filament\ambient_occlusion.va.fs"
#include "Filament\conversion_functions.va.fs"
// "Filament\depth_main.fs"                     - handled in PS_DepthOnly
// "Filament\dithering.fs"                      - not needed ATM
// "Filament\fxaa.fs"                           - not needed ATM
// "Filament\inputs.fs"                         - using Vanilla one (SurfaceInteraction)
#include "Filament\material_inputs.va.fs"

#if defined(SHADING_MODEL_CLOTH)
#include "Filament\shading_model_cloth.va.fs"
#elif defined(SHADING_MODEL_SUBSURFACE)
#include "Filament\shading_model_subsurface.va.fs"
#else
#include "Filament\shading_model_standard.va.fs"
#endif

#include "Filament\light_directional.va.fs"
#include "Filament\light_indirect.va.fs"        // stubbed out for now
#include "Filament\light_punctual.va.fs"
#include "Filament\shading_parameters.va.fs"
// #include "Filament\getters.va.fs"			// not meaningful to port this - too many differences
// "Filament\main.fs"                           - entry points are in vaMaterialFilament.hlsl
// "Filament\post_process.fs"                   - not used here
#include "Filament\shading_lit.va.fs"
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Load from the actual material (textures, constants, etc.) and set some of the dependant filament defines!
MaterialInputs LoadMaterial( const in ShaderInstanceConstants instance, const SurfaceInteraction surface )
{
    const ShaderMaterialConstants materialConstants = g_materialConstants[ instance.MaterialGlobalIndex ];
    const RenderMaterialInputs materialInputs = LoadRenderMaterialInputs( surface, materialConstants );

    MaterialInputs material = InitMaterial();

#if defined( VA_RM_HAS_INPUT_BaseColor )
    material.BaseColor  = surface.Color.rgba * materialInputs.BaseColor;
#endif

#if defined( VA_RM_HAS_INPUT_Normal )
    // load normalmap (could be texture or static)
    material.Normal = materialInputs.Normal.xyz;
#endif

#if defined( VA_RM_HAS_INPUT_EmissiveColor )
    material.EmissiveColorIntensity *= materialInputs.EmissiveColor.xyz;
#endif

#if defined( VA_RM_HAS_INPUT_EmissiveIntensity )
    material.EmissiveColorIntensity *= materialInputs.EmissiveIntensity.x;
#endif

    material.EmissiveColorIntensity *= instance.EmissiveMultiplier;

#if defined( VA_RM_HAS_INPUT_Roughness ) && !defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
    material.Roughness = materialInputs.Roughness.x;
#endif

#if defined( VA_RM_HAS_INPUT_Metallic )
    material.Metallic = materialInputs.Metallic.x;
#endif

#if defined( VA_RM_HAS_INPUT_Reflectance )
    material.Reflectance = materialInputs.Reflectance.x;
#endif

#if defined( VA_RM_HAS_INPUT_AmbientOcclusion )
    material.AmbientOcclusion = materialInputs.AmbientOcclusion.x;
#endif

    // this just to support a different way of defining/overriding alpha (instead of using BaseColor)
#if defined( VA_RM_HAS_INPUT_Opacity )
    material.BaseColor.a *= materialInputs.Opacity.x;
#endif

#ifdef VA_RM_HAS_INPUT_SpecularColor
    material.SpecularColor  = materialInputs.SpecularColor.rgb;
#endif

#ifdef VA_RM_HAS_INPUT_Glossiness
    material.Glossiness = materialInputs.Glossiness.x;
#endif

    // InvGlossiness is basically roughness and it's weird but it's basically what Amazon Lumberyard Bistro dataset has in its textures so.. just go with it I guess?
#ifdef VA_RM_HAS_INPUT_InvGlossiness
    material.Glossiness = 1.0 - materialInputs.InvGlossiness.x;
#endif


    // this is no longer needed with the new system!
#if defined( VA_RM_HAS_INPUT_ARMHack )
#error this goes out!
    float3 ARMHack = materialInputs.ARMHack.rgb;
    material.AmbientOcclusion   = ARMHack.r;
    material.Roughness          = ARMHack.g;
    material.Metallic           = ARMHack.b;
#endif

#if VA_RM_ALPHATEST
    material.AlphaTestThreshold = materialConstants.AlphaTestThreshold;
#endif

    // these two are hacks and will go away in the future - they used to be macros so keeping the naming convention
    material.VA_RM_LOCALIBL_NORMALBIAS = materialConstants.VA_RM_LOCALIBL_NORMALBIAS;
    material.VA_RM_LOCALIBL_BIAS       = materialConstants.VA_RM_LOCALIBL_BIAS      ;

    return material;
}

bool AlphaDiscard( const MaterialInputs material )
{
#if VA_RM_ALPHATEST
    // maybe go for optional https://casual-effects.com/research/Wyman2017Hashed/Wyman2017Hashed.pdf
    return (material.BaseColor.a+g_globals.WireframePass) < material.AlphaTestThreshold;
#else
    return false;
#endif
}

float4 EvaluateMaterialAndLighting( const SurfaceInteraction surface, const ShadingParams shading, const PixelParams pixel, const MaterialInputs material )
{
    float3 color     = 0.0.xxx;

    [branch]
    if( shading.IBL.UseLocal || shading.IBL.UseDistant )
        evaluateIBL( shading, material, pixel, color, shading.IBL.UseLocal, shading.IBL.UseDistant );

//    // 'ambient light' - this is totally not PBR but left in here for testing purposes
//    color += material.BaseColor.rgb * g_lighting.AmbientLightIntensity.rgb;

// #if defined(HAS_DIRECTIONAL_LIGHTING)
//     evaluateDirectionalLights( shading, material, pixel, diffuseColor, specularColor );
// #endif

// #if defined(HAS_DYNAMIC_LIGHTING)
     evaluatePunctualLights( surface, shading, pixel, color );
// #endif

#if defined(MATERIAL_HAS_EMISSIVE)
    color.xyz += shading.PrecomputedEmissive * g_globals.PreExposureMultiplier;
#endif

    float transparency = computeDiffuseAlpha(material.BaseColor.a);

    // // we don't want alpha blending to fade out speculars (at least not completely)
    // specularColor /= max( 0.01, transparency );

    float4 finalColor = float4( color, transparency );

//
//#if defined(VA_FILAMENT_STANDARD)
//    float4 finalColor = float4( 1, 0, 0, 1 );
//#elif defined(VA_FILAMENT_SUBSURFACE)
//    float4 finalColor = float4( 0, 1, 0, 1 );
//#elif defined(VA_FILAMENT_CLOTH)
//    float4 finalColor = float4( 0, 0, 1, 1 );
//#else
//#error Correct version of filament material model has to be set using one of the VA_FILAMENT_* macros
//#endif
//
    return finalColor;
}