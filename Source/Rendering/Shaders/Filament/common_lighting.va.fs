
struct PixelParams 
{
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
    vec3  sheenColor;
#if !defined(SHADING_MODEL_CLOTH)
    float sheenRoughness;
    float sheenPerceptualRoughness;
    float sheenScaling;
    float sheenDFG;
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
    float EtaRI;
    float EtaIR;
    float Transmission;
    float uThickness;
    vec3  Absorption;
#endif
};

// a contact shadow approximation, totally not physically correct; a riff on "Chan 2018, "Material Advances in Call of Duty: WWII" and "The Technical Art of Uncharted 4" http://advances.realtimerendering.com/other/2016/naughty_dog/NaughtyDog_TechArt_Final.pdf (microshadowing)"
// TODO: figure it out with bent normals! see https://www.activision.com/cdn/research/siggraph_2018_opt.pdf
float computeMicroShadowing(float NoL, float ao) 
{
#if DIRECT_LIGHTING_AO_MICROSHADOWS
#if 0 // from the paper - different from Filament and looks wrong
    float aperture = 2.0 * ao * ao;
    return saturate( abs(NoL) + aperture - 1.0 );
#else // filament version
    float aperture = inversesqrt(1.0000001 - ao);
    NoL += 0.1; // when using bent normals, avoids overshadowing - bent normals are just approximation anyhow
    return saturate(NoL * aperture);
#endif
#else
    return 1;
#endif
}
