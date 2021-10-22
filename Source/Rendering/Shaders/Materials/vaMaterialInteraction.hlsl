///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "../vaRenderingShared.hlsl"

#include "../Filament/brdf.va.fs"

#ifndef VA_MATERIAL_INTERACTION_HLSL_INCLUDED
#define VA_MATERIAL_INTERACTION_HLSL_INCLUDED

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Common shared types


// Detailed material-related surface info at the rasterized pixel or ray hit (interpolated vertex + related stuff).
// Inspired by https://pbr-book.org/3ed-2018/Geometry_and_Transformations/Interactions but the abstraction was split into 
// geometry-specific part (GeometryInteraction), while the MaterialInteraction contains material-specific data.
struct MaterialInteraction
{
    MaterialInputs  Inputs;         // raw inputs
    // (The part below is what used to be ShadingParams in Filament)

    float3x3    TangentToWorld;     // TBN matrix   <- COPIED FROM GeometryInteraction
    float3      Position;           // position of the fragment in world space - same as vertex.WorldspacePos

    float3      ObjectPosition;     // object space position of the fragment in world space

    float3      View;               // normalized vector from the fragment to the eye
    float3      Normal;             // normalized transformed normal, in world space (if normalmap exists, this is a perturbed normal)
    float3      GeometricNormal;    // normalized geometric normal, in world space
    float3      Reflected;          // reflection of view about normal
    float       NoV;                // dot(normal, view), always strictly >= MIN_N_DOT_V

#if defined(MATERIAL_HAS_CLEAR_COAT)
    float3      ClearCoatNormal;    // normalized clear coat layer normal, in world space
#endif

#if defined(MATERIAL_HAS_EMISSIVE)
    float3      PrecomputedEmissive;
#endif

#if !defined(RAYTRACED_SHADOWS)
    CubeShadowsParams
        CubeShadows;
#endif

    IBLParams   IBL;

    float3  	BentNormal;       	        // normalized transformed normal, in world space
    float       DiffuseAmbientOcclusion;    // min( ssao and material.AmbientOcclusion)

    float3      GetWorldGeometricNormalVector( ) { return GeometricNormal; }

    // (The part below is what used to be PixelParams in Filament)

    float   BaseAlpha;                      // alpha for forward transparencies, blending for decals, etc: coming from BaseColor.a

    float3  DiffuseColor;
    float   PerceptualRoughness;
    float   PerceptualRoughnessUnclamped;   // (not clamped by MIN_PERCEPTUAL_ROUGHNESS)
    float3  F0;                             // Reflectance at normal incidence
    float   DielectricF0;                   // computeDielectricF0() * (1.0 - metallic) - used for diffuse energy conservation estimate
    float   Roughness;
    float   RoughnessUnclamped;             // (not clamped by MIN_ROUGHNESS)
    float3  DFG;
    float3  EnergyCompensation;

    float   ReflectivityEstimate;           // estimate of the surface reflectivity for purposes of picking the better pdf; it represents combined Metallic and Reflectance parts from the PBR material (but works for old materials too)

#if defined(MATERIAL_HAS_CLEAR_COAT)
    float   ClearCoat;
    float   ClearCoatPerceptualRoughness;
    float   ClearCoatRoughness;
#endif

#if defined(MATERIAL_HAS_SHEEN_COLOR)
    float3  SheenColor;
#if !defined(SHADING_MODEL_CLOTH)
    float   SheenRoughness;
    float   SheenPerceptualRoughness;
    float   SheenScaling;
    float   SheenDFG;
#endif
#endif

#if defined(MATERIAL_HAS_ANISOTROPY)
    float3  AnisotropicT;
    float3  AnisotropicB;
    float   Anisotropy;
#endif

#if defined(SHADING_MODEL_SUBSURFACE) || defined(HAS_REFRACTION)
    float   Thickness;
#endif
#if defined(SHADING_MODEL_SUBSURFACE)
    float3  SubsurfaceColor;
    float   SubsurfacePower;
#endif

#if defined(SHADING_MODEL_CLOTH) && defined(MATERIAL_HAS_SUBSURFACE_COLOR)
    float3  SubsurfaceColor;
#endif

#if defined(HAS_REFRACTION)
    float   EtaRI;
    float   EtaIR;
    float   Transmission;
    float   uThickness;
    float3  Absorption;
#endif

    static MaterialInteraction          Compute( const GeometryInteraction geometrySurface, const MaterialInputs materialInputs, const uint2 screenSamplingPos );
};

MaterialInteraction MaterialInteraction::Compute( const GeometryInteraction geometrySurface, const MaterialInputs materialInputs, const uint2 screenSamplingPos )
{
    MaterialInteraction materialSurface;

    materialSurface.Inputs          = materialInputs;

    // all these are just copies from surface, but keep them in for consistency with Filament
    materialSurface.Position        = geometrySurface.WorldspacePos;
    materialSurface.GeometricNormal = geometrySurface.WorldspaceNormal;
    materialSurface.View            = geometrySurface.View;   // normalized vector from the fragment to the eye

    materialSurface.Normal          = mul( materialInputs.Normal, geometrySurface.TangentToWorld() ); // this used to have 'normalize()' but it's unnecessary as inputs are supposed to be normalized

    // to disable normal
    // materialSurface.Normal         = materialSurface.GetWorldGeometricNormalVector();

#if 1   // just don't allow normal to be facing away but this is likely too hacky and related to https://blogs.unity3d.com/2017/10/02/microfacet-based-normal-mapping-for-robust-monte-carlo-path-tracing/ - something to fix int he future
    materialSurface.NoV             = dot( materialSurface.Normal, materialSurface.View );
    materialSurface.Normal          = normalize( materialSurface.Normal + max( 0, -materialSurface.NoV + MIN_N_DOT_V ) * materialSurface.View );
    materialSurface.NoV             = max( MIN_N_DOT_V, materialSurface.NoV );
#else
    materialSurface.NoV             = clampNoV( dot( materialSurface.Normal, materialSurface.View ) );
#endif

    // Compute MATERIAL tangent space (different from geometry tangent space)
    materialSurface.TangentToWorld      = geometrySurface.TangentToWorld( );
    materialSurface.TangentToWorld[2]   = materialSurface.Normal;
    ReOrthogonalizeFrame( materialSurface.TangentToWorld, false );

    materialSurface.Reflected       = reflect( -materialSurface.View, materialSurface.Normal );

#if defined(MATERIAL_HAS_CLEAR_COAT)
#if defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
    materialSurface.ClearCoatNormal = normalize( mul( materialInputs.ClearCoatNormal, geometrySurface.TangentToWorld() ) );
#else
    materialSurface.ClearCoatNormal = materialSurface.GetWorldGeometricNormalVector();
#endif
#error do the same as above with clearcoat normal - prevent it to be facing away from the view vector
#endif
    //#endif

#if defined(MATERIAL_HAS_EMISSIVE)
    // the way filament does it:
    // float emissiveIntensity = pow( 2.0, g_globals.EV100 + materialInputs.EmissiveIntensity - 3.0) * g_globals.PreExposureMultiplier;

    // vanilla does it a bit simpler - also clamp values below 0 and don't do anything in case of VA_RM_SPECIAL_EMISSIVE_LIGHT
    // <removed pre-exposure multiplier from here so it doesn't apply itself twice for the special emissive lights where it gets multiplied by the light intensity that was premultiplied already>
    materialSurface.PrecomputedEmissive = max( 0, materialInputs.EmissiveColorIntensity );
#endif

#if !defined(RAYTRACED_SHADOWS)
    materialSurface.CubeShadows     = ComputeCubeShadowsParams( geometrySurface.ObjectspaceNoise, geometrySurface.WorldspacePos.xyz );
#endif

    materialSurface.IBL             = ComputeIBLParams( geometrySurface.WorldspacePos.xyz, materialSurface.GeometricNormal, materialInputs.VA_RM_LOCALIBL_NORMALBIAS, materialInputs.VA_RM_LOCALIBL_BIAS );

    float ssaoTerm; 
    SampleSSAO( screenSamplingPos, materialSurface.Normal, ssaoTerm, materialSurface.BentNormal );

    materialSurface.DiffuseAmbientOcclusion = materialInputs.AmbientOcclusion;

#if !defined(VA_RAYTRACING) && (!VA_RM_TRANSPARENT || VA_RM_DECAL)   // only sample SSAO if we're not transparent, unless we're a decal (in which case we lay on opaque surface by definition, so SSAO is correct)
    materialSurface.DiffuseAmbientOcclusion = min( materialInputs.AmbientOcclusion, ssaoTerm );
#endif

    // // if we wanted to update the TangentToWorld (cotangent frame) with the normal loaded from normalmap, we can do this:
    // materialSurface.TangentToWorld[2] = materialSurface.Normal;
    // ReOrthogonalizeFrame( cotangentFrame, false );


    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // PixelParams section

    {
        float4 baseColor = materialInputs.BaseColor;

        materialSurface.BaseAlpha = baseColor.a;

#if defined(BLEND_MODE_FADE) && !defined(SHADING_MODEL_UNLIT)
#error not ported to vanilla yet
        // Since we work in premultiplied alpha mode, we need to un-premultiply
        // in fade mode so we can apply alpha to both the specular and diffuse
        // components at the end
        unpremultiply(baseColor);
#endif

#if defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
        // This is from KHR_materials_pbrSpecularGlossiness.
        float3 specularColor = materialInputs.SpecularColor;
        float metallic = computeMetallicFromSpecularColor(specularColor);

        materialSurface.DielectricF0 = 0;
        materialSurface.DiffuseColor = computeDiffuseColor(baseColor, metallic);
        materialSurface.F0 = specularColor;
#elif !defined(SHADING_MODEL_CLOTH)
#if defined(HAS_REFRACTION) && (!defined(MATERIAL_HAS_REFLECTANCE) && defined(MATERIAL_HAS_IOR))
        materialSurface.DiffuseColor = baseColor.rgb;
        // If refraction is enabled, and reflectance is not set in the material, but ior is,
        // then use it -- othterwise proceed as usual.
    todo: what to do with materialSurface.DielectricF0? it's the same as F0?
        materialSurface.F0 = float3(iorToF0(materialInputs.ior, 1.0));
#else
        materialSurface.DiffuseColor = computeDiffuseColor(baseColor, materialInputs.Metallic);

        // Assumes an interface from air to an IOR of 1.5 for dielectrics
        materialSurface.DielectricF0 = computeDielectricF0(materialInputs.Reflectance);
        materialSurface.F0 = computeF0(baseColor, materialInputs.Metallic, materialSurface.DielectricF0);
        materialSurface.DielectricF0 *= (1.0 - materialInputs.Metallic);
#endif
#else
        materialSurface.DiffuseColor = baseColor.rgb;
        materialSurface.F0 = materialInputs.SheenColor;
#if defined(MATERIAL_HAS_SUBSURFACE_COLOR)
        materialSurface.SubsurfaceColor = materialInputs.SubsurfaceColor;
#endif
#endif    


#if defined(HAS_REFRACTION)
        // Air's Index of refraction is 1.000277 at STP but everybody uses 1.0
        const float airIor = 1.0;
#if !defined(MATERIAL_HAS_IOR)
        // [common case] ior is not set in the material, deduce it from F0
        float materialor = f0ToIor(materialSurface.f0.g);
#else
        // if ior is set in the material, use it (can lead to unrealistic materials)
        float materialor = max(1.0, materialInputs.ior);
#endif
        materialSurface.etaIR = airIor / materialor;  // air -> material
        materialSurface.etaRI = materialor / airIor;  // material -> air
#if defined(MATERIAL_HAS_TRANSMISSION)
        materialSurface.transmission = saturate(materialInputs.transmission);
#else
        materialSurface.transmission = 1.0;
#endif
#if defined(MATERIAL_HAS_ABSORPTION)
#if defined(MATERIAL_HAS_THICKNESS) || defined(MATERIAL_HAS_MICRO_THICKNESS)
        materialSurface.absorption = max(float3(0.0), materialInputs.absorption);
#else
        materialSurface.absorption = saturate(materialInputs.absorption);
#endif
#else
        materialSurface.absorption = float3(0.0);
#endif
#if defined(MATERIAL_HAS_THICKNESS)
        materialSurface.thickness = max(0.0, materialInputs.thickness);
#endif
#if defined(MATERIAL_HAS_MICRO_THICKNESS) && (REFRACTION_TYPE == REFRACTION_TYPE_THIN)
        materialSurface.uThickness = max(0.0, materialInputs.microThickness);
#else
        materialSurface.uThickness = 0.0;
#endif
#endif    
    }

    {
#if defined(MATERIAL_HAS_CLEAR_COAT)
        materialSurface.ClearCoat = materialInputs.ClearCoat;

        // Clamp the clear coat roughness to avoid divisions by 0
        float clearCoatPerceptualRoughness = materialInputs.ClearCoatRoughness;
        clearCoatPerceptualRoughness =
            clamp(clearCoatPerceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);

#if defined(GEOMETRIC_SPECULAR_AA)
#if defined(GEOMETRIC_SPECULAR_AA_USE_NORMALMAPS)
        clearCoatPerceptualRoughness = normalFiltering( clearCoatPerceptualRoughness, materialSurface.ClearCoatNormal );
#else
        clearCoatPerceptualRoughness = normalFiltering( clearCoatPerceptualRoughness, materialSurface.GetWorldGeometricNormalVector() );
#endif
#endif
#if defined(GEOMETRIC_SPECULAR_AA_UNIFIED)
#if defined(GEOMETRIC_SPECULAR_AA) || defined(GEOMETRIC_SPECULAR_AA_USE_NORMALMAPS)
#error These defines are incompatible
#endif
        clearCoatPerceptualRoughness = normalFiltering( clearCoatPerceptualRoughness, geometrySurface.NormalVariance );
#endif

        materialSurface.clearCoatPerceptualRoughness = clearCoatPerceptualRoughness;
        materialSurface.clearCoatRoughness = PerceptualRoughnessToRoughness(clearCoatPerceptualRoughness);

#if defined(CLEAR_COAT_IOR_CHANGE)
        // The base layer's f0 is computed assuming an interface from air to an IOR
        // of 1.5, but the clear coat layer forms an interface from IOR 1.5 to IOR
        // 1.5. We recompute f0 by first computing its IOR, then reconverting to f0
        // by using the correct interface
        materialSurface.F0 = mix(materialSurface.F0, f0ClearCoatToSurface(materialSurface.F0), materialSurface.ClearCoat);
#endif
#endif    
    }

    {
#if defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
        float perceptualRoughness = computeRoughnessFromGlossiness(materialInputs.Glossiness);
#else
        float perceptualRoughness = materialInputs.Roughness;
#endif


        // Clamp the roughness to a minimum value to avoid divisions by 0 during lighting
        //perceptualRoughness = clamp(perceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);

#if defined(GEOMETRIC_SPECULAR_AA)
#if defined(GEOMETRIC_SPECULAR_AA_USE_NORMALMAPS)
        perceptualRoughness = normalFiltering( perceptualRoughness, materialSurface.Normal );
#else
        perceptualRoughness = normalFiltering( perceptualRoughness, materialSurface.GetWorldGeometricNormalVector() );
#endif
#endif
#if defined(GEOMETRIC_SPECULAR_AA_UNIFIED)
#if defined(GEOMETRIC_SPECULAR_AA) || defined(GEOMETRIC_SPECULAR_AA_USE_NORMALMAPS)
#error These defines are incompatible
#endif
        perceptualRoughness = normalFiltering( perceptualRoughness, geometrySurface.NormalVariance );
#endif

#if defined(MATERIAL_HAS_CLEAR_COAT) && defined(MATERIAL_HAS_CLEAR_COAT_ROUGHNESS)
        // This is a hack but it will do: the base layer must be at least as rough
        // as the clear coat layer to take into account possible diffusion by the
        // top layer
        float basePerceptualRoughness = max(perceptualRoughness, materialSurface.ClearCoatPerceptualRoughness);
        perceptualRoughness = mix(perceptualRoughness, basePerceptualRoughness, materialSurface.ClearCoat);
#endif

        materialSurface.PerceptualRoughnessUnclamped = perceptualRoughness;
        // Clamp the roughness to a minimum value to avoid divisions by 0 during lighting
        materialSurface.PerceptualRoughness = clamp(perceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
        // Remaps the roughness to a perceptually linear roughness (roughness^2)
        materialSurface.Roughness = PerceptualRoughnessToRoughness(materialSurface.PerceptualRoughness);
        materialSurface.RoughnessUnclamped = PerceptualRoughnessToRoughness(materialSurface.PerceptualRoughnessUnclamped);
    }

    {
#if defined(SHADING_MODEL_SUBSURFACE)
        materialSurface.SubsurfacePower = materialInputs.SubsurfacePower;
        materialSurface.SubsurfaceColor = materialInputs.SubsurfaceColor;
        materialSurface.Thickness = saturate(materialInputs.Thickness);
#endif
    }

    {
#if defined(MATERIAL_HAS_ANISOTROPY)
        float3 direction = materialInputs.AnisotropyDirection;
        materialSurface.Anisotropy = materialInputs.Anisotropy;
        materialSurface.AnisotropicT = normalize( mul( direction, geometrySurface.TangentToWorld ) ); <- or materialSurface.TangentToWorld? figure it out, might not matter
        materialSurface.AnisotropicB = normalize( cross( materialSurface.GetWorldGeometricNormalVector(), materialSurface.AnisotropicT) );
#endif
    }

    {
        // Pre-filtered DFG term used for image-based lighting
        materialSurface.DFG = prefilteredDFG( materialSurface.PerceptualRoughness, materialSurface.NoV );

#if !defined(SHADING_MODEL_CLOTH)
        // Energy compensation for multiple scattering in a microfacet model
        // See "Multiple-Scattering Microfacet BSDFs with the Smith Model"
        materialSurface.EnergyCompensation = 1.0 + materialSurface.F0 * (1.0 / materialSurface.DFG.y - 1.0);
#else
        materialSurface.EnergyCompensation = 1.0.xxx;
#endif
    }

    // Note, this makes sense with our diffuse+specular BRDF; it will be different for other types of BxDFs
    {
        float f0 =  (materialSurface.F0.r+materialSurface.F0.g+materialSurface.F0.b)                              ;
        float d =   (materialSurface.DiffuseColor.r+materialSurface.DiffuseColor.g+materialSurface.DiffuseColor.b);
        float f = F_Schlick( f0, 25*f0, materialSurface.NoV ); // why not 50x? this seemed like a better estimate for purposes of finding best adaptive sampling distribution <shrug>
        materialSurface.ReflectivityEstimate = saturate(f / (f+d));

        // and convert to final weight used to decide between cosine-weighted distribution (less than) or microfacet distribution (larger or equal)
        materialSurface.ReflectivityEstimate = saturate(sqrt(1.0-materialSurface.ReflectivityEstimate));
    }

    return materialSurface;
}


#endif // #ifdef VA_MATERIAL_INTERACTION_HLSL_INCLUDED