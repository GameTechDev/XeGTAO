///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// A lot of the stuff here is based on: https://github.com/google/filament (which is awesome)
//
// Filament is the best compact opensource PBR renderer (that I could find), including excellent documentation and 
// performance. The size and complexity of the Filament library itself is a bit out of scope of what Vanilla is 
// intended to provide, and Vanilla's shift to path-tracing means Filament isn't used directly but a lot of its
// shaders and structures are inherited.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../Lighting/vaLighting.hlsl"

#include "vaMaterialLoaders.hlsl"

#if defined( VA_FILAMENT_SPECGLOSS )
#define SHADING_MODEL_SPECULAR_GLOSSINESS
#endif

#if 1
#define GEOMETRIC_SPECULAR_AA_UNIFIED                   // this is a path that behaves similarly/same between rasterization and raytracing
#endif

#define MIN_PERCEPTUAL_ROUGHNESS 0.06       // <- used to default to 0.45 but various artifacts can be seen up to 0.08 in fp32 too; 0.06 is a good tradeoff
#define MIN_ROUGHNESS            (MIN_PERCEPTUAL_ROUGHNESS*MIN_PERCEPTUAL_ROUGHNESS)

//
// we just always enable these and hope they get compiled out when unused (seems to happen in testing so far)
#define MATERIAL_HAS_EMISSIVE               1           // #if defined( VA_RM_HAS_INPUT_EmissiveColor )
#if defined(MATERIAL_HAS_CLEAR_COAT)
#define MATERIAL_HAS_CLEAR_COAT_NORMAL      1
#endif
//
// #define MATERIAL_HAS_ANISOTROPY         1
#define SPECULAR_AMBIENT_OCCLUSION          SPECULAR_AO_BENT_NORMALS  // SPECULAR_AO_OFF / SPECULAR_AO_SIMPLE / SPECULAR_AO_BENT_NORMALS
#define MULTI_BOUNCE_AMBIENT_OCCLUSION      1
#define DIRECT_LIGHTING_AO_MICROSHADOWS     1
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define MIN_N_DOT_V 1e-4

float clampNoV(float NoV) {
    // Neubelt and Pettineo 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"
    return max(NoV, MIN_N_DOT_V);
}

float3 computeDiffuseColor(const float4 baseColor, float metallic) {
    return baseColor.rgb * (1.0 - metallic);
}

float3 computeF0(const float4 baseColor, float metallic, float reflectance) {
    return baseColor.rgb * metallic + (reflectance * (1.0 - metallic));
}

float computeDielectricF0(float reflectance) {
    return 0.16 * reflectance * reflectance;
}

float computeMetallicFromSpecularColor(const float3 specularColor) {
    return Max3(specularColor);
}

float computeRoughnessFromGlossiness(float glossiness) {
    return 1.0 - glossiness;
}

float iorToF0(float transmittedIor, float incidentIor) {
    return Sq((transmittedIor - incidentIor) / (transmittedIor + incidentIor));
}

float f0ToIor(float f0) {
    float r = sqrt(f0);
    return (1.0 + r) / (1.0 - r);
}

float3 f0ClearCoatToSurface(const float3 f0) {
    // Approximation of iorTof0(f0ToIor(f0), 1.5)
    // This assumes that the clear coat layer has an IOR of 1.5
#if FILAMENT_QUALITY == FILAMENT_QUALITY_LOW
    return saturate(f0 * (f0 * 0.526868 + 0.529324) - 0.0482256);
#else
    return saturate(f0 * (f0 * (0.941892 - 0.263008 * f0) + 0.346479) - 0.0285998);
#endif
}

//------------------------------------------------------------------------------
// IBL prefiltered DFG term implementations
//------------------------------------------------------------------------------

float3 PrefilteredDFG_LUT(float lod, float NoV) {
    // coord = sqrt(linear_roughness), which is the mapping used by cmgen.
    return g_DFGLookupTable.SampleLevel( g_samplerLinearClamp, float2(NoV, 1.0-lod), 0.0 ).rgb;
}

//------------------------------------------------------------------------------
// IBL environment BRDF dispatch
//------------------------------------------------------------------------------

float3 prefilteredDFG(float perceptualRoughness, float NoV) {
    // PrefilteredDFG_LUT() takes a LOD, which is sqrt(roughness) = perceptualRoughness
    return PrefilteredDFG_LUT(perceptualRoughness, NoV);
}


// These are the raw material inputs at the shaded point, as loaded from textures (or other material nodes) but
// not yet precomputed into a surface interaction (MaterialInteraction)
struct MaterialInputs
{
    float4  BaseColor;                  // Diffuse color of a non-metallic object and/or specular color of a metallic object; .a is simply alpha (for alpha testing or as 1-opacity)

#if !defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
    float   Roughness;                  // Defines the perceived smoothness (0.0) or roughness (1.0). It is sometimes called glossiness. Same as PixelParams::PerceptualRoughnessUnclamped
#endif
#if !defined(SHADING_MODEL_CLOTH) && !defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
    float   Metallic;                   // Defines whether a surface is dielectric (0.0, non-metal) or conductor (1.0, metal). Pure, unweathered surfaces are rare and will be either 0.0 or 1.0. Rust is not a conductor.
    float   Reflectance;
#endif
    float   AmbientOcclusion;           // Defines how much of the ambient light is accessible to a surface point. It is a per-pixel shadowing factor between 0.0 and 1.0. This is a static value in contrast to dynamic such as SSAO.
#if defined(MATERIAL_HAS_EMISSIVE)
    float3  EmissiveColorIntensity;     // Simulates additional light emitted by the surface. .rgb is linear color (but should be edited as a sRGB in the UI) multiplied by Intensity
#endif

    float   ClearCoat;                  // Strength of the clear coat layer on top of a base dielectric or conductor layer. The clear coat layer will commonly be set to 0.0 or 1.0. This layer has a fixed index of refraction of 1.5.
    float   ClearCoatRoughness;         // Defines the perceived smoothness (0.0) or roughness (1.0) of the clear coat layer. It is sometimes called glossiness. This may affect the roughness of the base layer

    float   Anisotropy;                 // Defines whether the material appearance is directionally dependent, that is isotropic (0.0) or anisotropic (1.0). Brushed metals are anisotropic. Values can be negative to change the orientation of the specular reflections.
    float3  AnisotropyDirection;        // The anisotropyDirection property defines the direction of the surface at a given point and thus control the shape of the specular highlights. It is specified as vector of 3 values that usually come from a texture, encoding the directions local to the surface.

#if defined(SHADING_MODEL_SUBSURFACE) || defined(HAS_REFRACTION)
    float   Thickness;
#endif
#if defined(SHADING_MODEL_SUBSURFACE)
    float   SubsurfacePower;            // 
    float3  SubsurfaceColor;            // 
#endif

#if defined(SHADING_MODEL_CLOTH)
    float3  SheenColor;                 // Specular tint to create two-tone specular fabrics (defaults to sqrt(baseColor))
#if defined(MATERIAL_HAS_SUBSURFACE_COLOR)
    float3  SubsurfaceColor;            // Tint for the diffuse color after scattering and absorption through the material.
#endif
#endif

#if defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
    float3  SpecularColor;
    float   Glossiness;
#endif

    float3  Normal;                     // Normal as loaded from material (usually normal map texture) - tangent space so +Z points outside of the surface.

#if defined(MATERIAL_HAS_CLEAR_COAT) && defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
    float3  ClearCoatNormal;            // Same as Normal except additionally defining a different normal for the clear coat!
#endif

#if defined(HAS_REFRACTION)
#if defined(MATERIAL_HAS_ABSORPTION)
    float3  Absorption;
#endif
#if defined(MATERIAL_HAS_TRANSMISSION)
    float   Transmission;
#endif
#if defined(MATERIAL_HAS_IOR)
    float   IoR;
#endif
#if defined(MATERIAL_HAS_MICRO_THICKNESS) && (REFRACTION_TYPE == REFRACTION_TYPE_THIN)
    float   MicroThickness;
#endif
#endif

#if VA_RM_ALPHATEST
    float   AlphaTestThreshold;
#endif

    // these two are hacks and will go away in the future - they used to be macros so keeping the naming convention
    float   VA_RM_LOCALIBL_NORMALBIAS;
    float   VA_RM_LOCALIBL_BIAS      ;


    float   IndexOfRefraction;          // handling separately from Filament's refraction implementation (we're probably going to remove filament one)
};

#include "vaMaterialInteraction.hlsl"

MaterialInputs InitMaterial() 
{
    MaterialInputs material;

    material.BaseColor              = float4( 1.0, 1.0, 1.0, 1.0 );

#if !defined(SHADING_MODEL_SPECULAR_GLOSSINESS) 
    material.Roughness              = 1.0;
#endif

#if !defined(SHADING_MODEL_CLOTH) && !defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
    material.Metallic               = 0.0;
    material.Reflectance            = 0.35;
#endif

    material.AmbientOcclusion       = 1.0;

#if defined(MATERIAL_HAS_EMISSIVE)
    material.EmissiveColorIntensity = float3( 1.0, 1.0, 1.0 );
#endif

#if defined(MATERIAL_HAS_CLEAR_COAT)
    material.ClearCoat              = 1.0;
    material.ClearCoatRoughness     = 0.0;
#endif

#if defined(MATERIAL_HAS_ANISOTROPY)
    material.Anisotropy             = 0.0;
    material.AnisotropyDirection    = float3( 1.0, 0.0, 0.0 );
#endif

#if defined(SHADING_MODEL_SUBSURFACE) || defined(HAS_REFRACTION)
    material.Thickness              = 0.5;
#endif
#if defined(SHADING_MODEL_SUBSURFACE)
    material.SubsurfacePower        = 12.234;
    material.SubsurfaceColor        = float3( 1.0, 1.0, 1.0 );
#endif

#if defined(SHADING_MODEL_CLOTH)
    material.SheenColor             = sqrt( material.BaseColor.rgb );
#if defined(MATERIAL_HAS_SUBSURFACE_COLOR)
    material.SubsurfaceColor        = float3( 0.0, 0.0, 0.0 );
#endif
#endif

#if defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
    material.Glossiness             = 0.0;
    material.SpecularColor          = float3( 0.0, 0.0, 0.0 );
#endif

    material.Normal                 = float3( 0.0, 0.0, 1.0 );
#if defined(MATERIAL_HAS_CLEAR_COAT) && defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
    material.ClearCoatNormal        = float3( 0.0, 0.0, 1.0 );
#endif

    //#if defined(MATERIAL_HAS_POST_LIGHTING_COLOR)
    //    material.postLightingColor = vec4(0.0);
    //#endif

#if defined(HAS_REFRACTION)
#if defined(MATERIAL_HAS_ABSORPTION)
    material.Absorption             = float3(0.0);
#endif
#if defined(MATERIAL_HAS_TRANSMISSION)
    material.Transmission           = 1.0;
#endif
#if defined(MATERIAL_HAS_IOR)
    material.IoR                    = 1.5;
#endif
#if defined(MATERIAL_HAS_MICRO_THICKNESS) && (REFRACTION_TYPE == REFRACTION_TYPE_THIN)
    material.MicroThickness         = 0.0;
#endif
#endif

#if VA_RM_ALPHATEST
    material.AlphaTestThreshold     = 0.5;
#endif

    // these two are hacks and will go away in the future - they used to be macros so keeping the naming convention
    material.VA_RM_LOCALIBL_NORMALBIAS = 0.0;
    material.VA_RM_LOCALIBL_BIAS       = 0.0;

    material.IndexOfRefraction      = 1.0;

    return material;
}


// Load from the actual material (textures, constants, etc.) and set some of the dependant filament defines!
MaterialInputs LoadMaterial( const in ShaderInstanceConstants instance, const GeometryInteraction geometrySurface )
{
    const ShaderMaterialConstants materialConstants = g_materialConstants[ instance.MaterialGlobalIndex ];
    const RenderMaterialInputs materialInputs = LoadRenderMaterialInputs( geometrySurface, materialConstants );

    MaterialInputs material = InitMaterial();

#if defined( VA_RM_HAS_INPUT_BaseColor )
    material.BaseColor  = geometrySurface.Color.rgba * materialInputs.BaseColor;
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

    material.IndexOfRefraction         = materialConstants.IndexOfRefraction;

    return material;
}

