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

#include "../vaGeometryInteraction.hlsl"
#ifdef VA_RAYTRACING
#include "../vaPathTracerShared.h"
#endif 
#include "../Lighting/vaLighting.hlsl"

#ifndef VA_FILAMENT_SPECGLOSS
#define VA_FILAMENT_STANDARD
#endif

#include "vaMaterialShared.hlsli"

#include "../Filament/brdf.va.fs"
#include "../Filament/ambient_occlusion.va.fs"

#include "../Filament/shading_model_standard.va.fs"
#include "../Filament/light_indirect.va.fs"

// just return default defined in vaMaterialShared.hlsli
MaterialInputs Material_Load( const ShaderInstanceConstants instance, const GeometryInteraction geometrySurface )
{
    return LoadMaterial( instance, geometrySurface );
}

// just return default defined in vaMaterialShared.hlsli
MaterialInteraction Material_Surface( const GeometryInteraction geometrySurface, const MaterialInputs materialInputs, const uint2 screenSamplingPos )
{
    return MaterialInteraction::Compute( geometrySurface, materialInputs, screenSamplingPos );
}

bool Material_AlphaTest( const GeometryInteraction geometrySurface, const MaterialInputs material )
{
#if VA_RM_ALPHATEST
    // maybe go for optional https://casual-effects.com/research/Wyman2017Hashed/Wyman2017Hashed.pdf
    return (material.BaseColor.a+g_globals.WireframePass) < material.AlphaTestThreshold;
#else
    return false;
#endif
}

// float3 getSpecularDominantDirArea( float3 N, float3 R, float NdotV, float roughness )
// {
//    // Simple linear approximation
//    float lerpFactor = (1 - roughness);
//    return normalize ( lerp (N , R , lerpFactor )) ;
//}

// hack we use to avoid Dirac delta functions
static const float c_perfectSpecularPDF = 1000000.0f;

// See surfaceshading from Filament's shading_model_standard.fs for more info.
// At the moment clear coat is not implemented/enabled.
// 
float3 Material_BxDF( const MaterialInteraction _materialSurface, const float3 Wi, const float sphereLightDistance, const float sphereLightRadius, const bool refracted )
{
#if VA_RM_TRANSPARENT
    if( refracted )
        return float3( 1, 1, 1 ) * c_perfectSpecularPDF;
#endif

    MaterialInteraction materialSurface = _materialSurface;

    // spherical area light
    float3 specWi = Wi; 
    float specAreaLightNormalization = 1.0;

#if 1 // enable spherical area light
    if( sphereLightRadius > 0 )
    {
        // 'Specular D Modification' (https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf)
        // materialSurface.Roughness = Sq( saturate( sqrt(materialSurface.Roughness) + (sphereLightSize / (sphereLightDistance) ) ) );

        // "Closest representative point" approximation, pg 15+, https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
        // TODO: Better specular dominant dir, search for getSpecularDominantDir, https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
        // TODO: Smoothed Representative Point, https://www.dropbox.com/s/mx0ub7t3j0b46bo/sig2015_approx_models_PBR_notes_DRAFT.pdf?dl=0

        float3 specularDominantDir = materialSurface.Reflected; // upgrade to better representative point

        float3 surfaceToLight = Wi * sphereLightDistance;
        float3 centerToRay = dot( surfaceToLight, specularDominantDir ) * specularDominantDir - surfaceToLight;
        float3 closestPoint = surfaceToLight + centerToRay * saturate( sphereLightRadius / length(centerToRay) );
        specWi = normalize( closestPoint );

        specAreaLightNormalization = pow( materialSurface.Roughness / clamp(materialSurface.Roughness + sphereLightRadius / (2.0 * sphereLightDistance), 0.0, 1.0), 2.0);
    }
#endif

    // materialSurface.View a.k.a. Wo
    float3 diffH = normalize( materialSurface.View + Wi );          
    float3 specH = normalize( materialSurface.View + specWi );

    float NoV = materialSurface.NoV;
    float diffNoL = saturate( dot( materialSurface.Normal, Wi ) );
    float specNoL = saturate( dot( materialSurface.Normal, specWi ) );
    float diffNoH = saturate( dot( materialSurface.Normal, diffH ) );
    float specNoH = saturate( dot( materialSurface.Normal, specH ) );
    float diffLoH = saturate( dot( Wi, diffH ) );
    float specLoH = saturate( dot( specWi, specH ) );

    float3 Fr = specularLobe( materialSurface.View, materialSurface, specWi, specH, NoV, specNoL, specNoH, specLoH );
    float3 Fd = diffuseLobe( materialSurface, NoV, diffNoL, diffLoH );
    Fr *= materialSurface.EnergyCompensation * specAreaLightNormalization;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    // coment below is from original Filament implementation:
    // TODO: attenuate the diffuse lobe to avoid energy gain
    //
    // FS: Since we're monte carlo integrating over the lobe, I've attempted to compensate for the light that
    // doesn't come to play with the diffuse by reducing the incident light by the (dielectric only!) fresnel
    // effect. It feels wrong though (ignores roughness to begin with) but I don't have time & knowhow to dig
    // through further, so I'll leave some useful references for when I get back to it in the future:
    // * pbrt on the topic: http://www.pbr-book.org/3ed-2018/Reflection_Models/Fresnel_Incidence_Effects.html#
    // * there's a short note on "Moving Frostbite to PBR": https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf 
    // * above Frostbite's [Jen+01] is a reference to this (page 3): https://graphics.stanford.edu/papers/bssrdf/bssrdf.pdf
    {
        float f90 = saturate( materialSurface.DielectricF0 * 50.0 );     // same as "fresnel(materialSurface.DielectricF0, LoH).x", since materialSurface.DielectricF0 is scalar (grayscale)
        float frD = F_Schlick( materialSurface.DielectricF0, f90, diffLoH ); // same as "fresnel(materialSurface.DielectricF0, LoH).x", since materialSurface.DielectricF0 is scalar (grayscale)
        Fd *= 1-frD;
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(HAS_REFRACTION)
#error not implemented
    Fd *= (1.0 - materialSurface.Transmission);
#endif

#if defined(MATERIAL_HAS_CLEAR_COAT)
    float Fcc;
    float clearCoat = clearCoatLobe(materialSurface, specH, NoH, LoH, Fcc);
    float attenuation = 1.0 - Fcc;

    float3 color = (Fd + Fr) * attenuation * NoL;

    // If the material has a normal map, we want to use the geometric normal
    // instead to avoid applying the normal map details to the clear coat layer
    float clearCoatNoL = saturate(dot(materialSurface.ClearCoatNormal, light.L));
    color += clearCoat * clearCoatNoL;

    // Early exit to avoid the extra multiplication by NoL
    return color;
#else
    // The energy compensation term is used to counteract the darkening effect
    // at high roughness
    float3 color    = Fd + Fr;
#endif

    color *= saturate( dot( materialSurface.Normal, Wi ) );   // geometry cosine term - perhaps pull it out outside of this

    return color;
}
float3 Material_BxDF( const MaterialInteraction materialSurface, const float3 Wi, const bool refracted )
{
    const float sphereLightDistance = 1e32f;
    const float sphereLightSize     = 0.0f;
    return Material_BxDF( materialSurface, Wi, sphereLightDistance, sphereLightSize, refracted );
}

float Material_WeighLight( const ShaderLightTreeNode lightNode, const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface )
{
    vaVector3 delta = lightNode.Center - geometrySurface.WorldspacePos;
    float deltaLength = length(delta);

    const float uncertaintyDistanceHeuristic = 0.28;
    float distance = VA_MAX( lightNode.UncertaintyRadius*uncertaintyDistanceHeuristic, deltaLength-lightNode.UncertaintyRadius*uncertaintyDistanceHeuristic );    // this is distance to somewhere inside node sphere - this is our best guess
    float lightAttenuation = ShaderLightAttenuation( distance, lightNode.RangeAvg, lightNode.SizeAvg );

#if 0 // a test!
    return lightAttenuation;
#endif 

#if 0   // incident light comes from center
    vaVector3 Wi = delta / deltaLength;
#else   // incident light comes from center offsetted by radius in the direction of the materialSurface normal (point on uncertainty sphere)
    vaVector3 Wi = normalize(delta + materialSurface.Normal * lightNode.UncertaintyRadius);
#endif

#if 0   // simplest distance-only based weight

    float retVal = lightNode.IntensitySum * lightAttenuation;

#elif 1 // approx material response based weight

    const float NoLFudgeHeuristic = 0.003;   // this allows for some very limited positive weights for lighting coming under the surface and accounts for other approximations

    float3 h = normalize( materialSurface.View + Wi );    // materialSurface.View a.k.a. Wo
    float NoV = materialSurface.NoV;
    float NoL = saturate( saturate( dot( materialSurface.Normal, Wi ) ) + NoLFudgeHeuristic );
    float NoH = saturate( dot( materialSurface.Normal, h ) );
    float LoH = saturate( dot( Wi, h) );

    float Fr = approxSpecularLobeLum( materialSurface, Wi, h, NoV, NoL, NoH, LoH );
    float Fd = approxDiffuseLobeLum( materialSurface, NoV, NoL, LoH );

    float brdfLuminance = CalcLuminance(Fr+Fd);
    const float brdfBiasHeuristic = 0.66;   // I honestly have no idea why this helps but it does
    float retVal = pow( brdfLuminance, brdfBiasHeuristic ) * lightNode.IntensitySum * lightAttenuation * NoL;

#else   // use full-fat material surface response to get the weight - actually works worse than the above due to lack of fudges :)

    float3 color = 0;
    surfaceShading( materialSurface, Wi, color );
    float retVal = CalcLuminance( color ) * lightNode.IntensitySum * lightAttenuation;

#endif
    return max( 0, retVal );
}

#ifdef VA_RAYTRACING    // just to speed up compilation of non-raytracing stuff

float Material_BxDF_PDF_Tangent( const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface, const float3 Wo, const float3 Wi, const bool refracted )
{
    float pdf1 = SampleHemisphereCosineWeightedPDF( Wi.z );
    float pdf2 = SampleGGXVNDF_PDF( Wo, Wi, materialSurface.Roughness, materialSurface.Roughness );
    pdf1 = max( 1e-16, pdf1 );  // things go haywire with too low pdfs and this doesn't seem to cause measurable energy loss, but there's probably a better standard way to deal with it
    pdf2 = max( 1e-16, pdf2 );  // things go haywire with too low pdfs and this doesn't seem to cause measurable energy loss, but there's probably a better standard way to deal with it
    return lerp( pdf2, pdf1, materialSurface.ReflectivityEstimate );
}

float Material_BxDF_PDF_World( const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface, float3 Wo, float3 Wi, const bool refracted )
{
    const float3x3 shadingTangentToWorld = materialSurface.TangentToWorld;
    Wo = mul( shadingTangentToWorld, Wo );
    Wi = mul( shadingTangentToWorld, Wi );
    return Material_BxDF_PDF_Tangent( geometrySurface, materialSurface, Wo, Wi, refracted );
}

// Figure out a good next sample direction for the BxDF using importance sampling; takes one 1D and 1 2D low discrepancy samples, 
// returns 'Wi' (light incoming direction / next path direction) IN TANGENT SPACE
void Material_BxDFSample( const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface, float r, const float2 u, out float3 Wi, out float pdf, out bool refracted )
{
    // we're doing everything here in tangent space, so convert
    const float3x3 shadingTangentToWorld = materialSurface.TangentToWorld;

    float3 Wo = mul( shadingTangentToWorld, materialSurface.View ); // a.k.a. Ve

    // We assume the Wo view vector is never coming 'from below' of the materialSurface.Normal so z>0 in the shading tangent frame.
    // Otherwise, we have a problem.

    // this is a placeholder code until I get proper double-sided materials
#if VA_RM_TRANSPARENT   // single sided air<->material -> thin material -> material<->air supported only

    // https://www.pbr-book.org/3ed-2018/Reflection_Models/Specular_Reflection_and_Transmission#
    const float etaI = 1.00029f;
    const float etaT = materialSurface.Inputs.IndexOfRefraction;
    float eta = etaI/etaT;

    float cosThetaI = Wo.z;
    float F = FresnelDielectric( eta, cosThetaI );

    float reflectPDF = F;
    if( r > reflectPDF )
    {
        pdf = c_perfectSpecularPDF;

        Wi = refract( -Wo, float3( 0, 0, -1 ), eta );
        
        // and go back material-> air with a hack to simulate thickness - total hack, all of this
        Wi = refract( Wi, float3( 0, 0, 1 ), 0.97/eta );

        // We must go back from tangent space to world space now
        Wi = normalize( mul( Wi, shadingTangentToWorld ) );

        refracted = true;
        return;
    }
    // recycle random number while preserving sample's low discrepancy
    r = saturate(r/reflectPDF);
#endif
    refracted = false;

    // anisotropic roughness mess - will require additional work to adjust the tangent space (shadingTangentToWorld) with aniso rotation
#if defined(MATERIAL_HAS_ANISOTROPY)
#error not implemented yet
#endif
    //
    // various (importance) sampling methods go here
#if 0   // << CLEANUP THIS << uniform hemisphere reference - use this as a reference for testing (will take ages to accumulate though and will have self-intersections - TODO: fix that same as below)
    Wi = SampleHemisphereUniform( u, pdf );
#elif 0 // << CLEANUP THIS << VNDF distribution
    Wi = SampleGGXVNDF( Wo, materialSurface.Roughness, materialSurface.Roughness, u, pdf );
#else   // mixed cosine weighted and VNDF: smaller - more suitable for specular; bigger - more suitable for diffuse

    // mixed 50%-50% cosine weighted and VNDF 
    // const float pc = 0.5;

    // much more powerful adaptive approach; TODO: read up on http://www.pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Reflection_Functions.html#FresnelBlend and chapter excercises
    if( r < materialSurface.ReflectivityEstimate )
        Wi = SampleHemisphereCosineWeighted( u );
    else
    {
        float dummy;
        Wi = SampleGGXVNDF( Wo, materialSurface.RoughnessUnclamped, materialSurface.RoughnessUnclamped, u, dummy ); // <- RoughnessUnclamped used only for picking the direction, not for computing pdf; hack, not sure how much if any bias it adds
    }

    // prevent rays from going under the physical geometry triangle normal to avoid self-intersections
#if 1
    // TODO: read https://blogs.unity3d.com/2017/10/02/microfacet-based-normal-mapping-for-robust-monte-carlo-path-tracing/ and  Unbiased VNDF Sampling for Backfacing Shading Normals: https://yusuketokuyoshi.com/papers/2021/Unbiased_VNDF_Sampling_for_Backfacing_Shading_Normals.pdf
    // if we can integrate that, it's almost certainly smarter than this hack. No time for that at the moment. There's also
    // the snapping of the normal that happens in shading_parameters.va.fs to attend to.
    //
    // Details of below: we snap the Wi (incident light direction - a.k.a. next ray to trace on the path) to the geometry 
    // (non-interpolated) triangle normal (it is now a slightly different sampling distribution). If we just snap Wi to the 
    // triangle plane then we're creating a new distribution that oversamples along just the plane - this is bad and causes
    // visible artifacts that show underlying geometry.
    // Instead, we first push it a fixed 60% (0.6) and additional random 0-80% [0, 0.8] towards the triangle plane. So the
    // next ray has 50% chance that it will self-intersect again (but at much lower angle from the triangle plane, which
    // sometimes just lets it travel along the triangle enough to escape) and it has a 50% chance to bounce away in a
    // somewhat random direction, which, while technically incorrect, gets smoothed out by Monte Carlo integration :)
    // We're lucky that the self-intersection usually does not cause penetration due to robust offset handling because
    // it pushes the ray starting point along the triangle normal just enough to avoid numerical precision issues.
    float3 Tn = mul( shadingTangentToWorld, geometrySurface.TriangleNormal ); // we need the triangle normal in shading tangent space
    Wi = normalize( Wi + (max( 0, -dot(Wi, Tn) ) * ( 0.6 + Rand1D(r)*0.8 ) ) * Tn ); // push the Wi 'out' in the triangle normal direction if required, dithered
#endif

    pdf = Material_BxDF_PDF_Tangent( geometrySurface, materialSurface, Wo, Wi, refracted );

#endif
                                                                      // Debugging if needed
#ifdef BSDFSAMPLE_F_VIZ_DEBUG
    [branch] if( IsUnderCursorRange( (int2)geometrySurface.Position.xy, int2(1,1) ) )
    {
        DebugDraw3DText( geometrySurface.WorldspacePos.xyz, float2( -10, -48 ), float4(0.5,0.5,1,1), float4(Wo, 0) );
        DebugDraw3DText( geometrySurface.WorldspacePos.xyz, float2( -10, -34 ), float4(0.5,1,0.5,1), float4(Wi, materialSurface.Roughness) );
    }
#endif

    // We must go back to world space now
    Wi = normalize( mul( Wi, shadingTangentToWorld ) );

    // just a test
    // pdf = Material_BxDF_PDF_World( geometrySurface, materialSurface, materialSurface.View, Wi, refracted );
}

#endif // #ifdef VA_RAYTRACING


// this will use Material_BxDF, Material_WeighLight and others defined above!
#include "../Lighting/vaPointLight.hlsli"

float4 Material_Forward( const GeometryInteraction geometrySurface, const MaterialInteraction materialSurface, const bool debugPixel )
{
    float3 color     = 0.0.xxx;

    [branch]
    if( materialSurface.IBL.UseLocal || materialSurface.IBL.UseDistant )
        evaluateIBL( materialSurface, color, materialSurface.IBL.UseLocal, materialSurface.IBL.UseDistant );

    //    // 'ambient light' - this is totally not PBR but left in here for testing purposes
    //    color += material.BaseColor.rgb * g_lighting.AmbientLightIntensity.rgb;

    // #if defined(HAS_DIRECTIONAL_LIGHTING)
    //     evaluateDirectionalLights( materialSurface, material, diffuseColor, specularColor );
    // #endif

    // #if defined(HAS_DYNAMIC_LIGHTING)
    EvaluatePointLightsForward( geometrySurface, materialSurface, color, debugPixel );
    // #endif

#if defined(MATERIAL_HAS_EMISSIVE)
    color.xyz += materialSurface.PrecomputedEmissive * g_globals.PreExposureMultiplier;
#endif

    float transparency = materialSurface.BaseAlpha;

    // // we don't want alpha blending to fade out speculars (at least not completely)
    // specularColor /= max( 0.01, transparency );

    float4 finalColor = float4( color, transparency );

    return finalColor;
}

// this is where the entry points and all the glue connecting to vaRenderMaterial.h/cpp is
#include "vaRenderMaterial.hlsl"