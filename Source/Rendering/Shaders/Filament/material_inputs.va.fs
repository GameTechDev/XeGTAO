// Decide if we can skip lighting when dot(n, l) <= 0.0
#if defined(SHADING_MODEL_CLOTH)
#if !defined(MATERIAL_HAS_SUBSURFACE_COLOR) && (VA_RM_SPECIAL_EMISSIVE_LIGHT == 0)
    #define MATERIAL_CAN_SKIP_LIGHTING
#endif
#elif defined(SHADING_MODEL_SUBSURFACE)
    // Cannot skip lighting
#else
#if (VA_RM_SPECIAL_EMISSIVE_LIGHT == 0)
    #define MATERIAL_CAN_SKIP_LIGHTING
#endif
#endif

struct MaterialInputs
{
    float4  BaseColor;                  // Diffuse color of a non-metallic object and/or specular color of a metallic object; .a is simply alpha (for alpha testing or as 1-opacity)

#if !defined(SHADING_MODEL_UNLIT)

#if !defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
    float   Roughness;                  // Defines the perceived smoothness (0.0) or roughness (1.0). It is sometimes called glossiness. Same as PixelParams::PerceptualRoughnessUnclamped
#endif
#if !defined(SHADING_MODEL_CLOTH) && !defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
    float   Metallic;                   // Defines whether a surface is dielectric (0.0, non-metal) or conductor (1.0, metal). Pure, unweathered surfaces are rare and will be either 0.0 or 1.0. Rust is not a conductor.
    float   Reflectance;
#endif
    float   AmbientOcclusion;           // Defines how much of the ambient light is accessible to a surface point. It is a per-pixel shadowing factor between 0.0 and 1.0. This is a static value in contrast to dynamic such as SSAO.
#if defined(MATERIAL_HAS_EMISSIVE)
    float3  EmissiveColor;              // Simulates additional light emitted by the surface. .rgb is linear color (but should be edited as a sRGB in the UI)
    float   EmissiveIntensity;          // Intensity for the Emissive: same logic as regular lights.
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
    vec3    SpecularColor;
    float   Glossiness;
#endif

#if defined(MATERIAL_HAS_NORMAL)
    float3  Normal;                     // Normal as loaded from material (usually normal map texture) - tangent space so +Z points outside of the surface.
#endif

#if defined(MATERIAL_HAS_CLEAR_COAT) && defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
    float3  ClearCoatNormal;            // Same as Normal except additionally defining a different normal for the clear coat!
#endif

#endif // #if !defined(SHADING_MODEL_UNLIT)

// #if defined(MATERIAL_HAS_POST_LIGHTING_COLOR)
//     float4  PostLightingColor;
// #endif

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
};

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
    material.EmissiveColor          = float3( 1.0, 1.0, 1.0 );
    material.EmissiveIntensity      = 0.0;
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

 #if defined(MATERIAL_HAS_NORMAL)
    material.Normal                 = float3( 0.0, 0.0, 1.0 );
#endif
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

    return material;
}

