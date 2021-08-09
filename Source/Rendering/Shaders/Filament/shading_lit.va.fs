//------------------------------------------------------------------------------
// Lighting
//------------------------------------------------------------------------------

float computeDiffuseAlpha(float a)
{
#if defined(BLEND_MODE_TRANSPARENT) || defined(BLEND_MODE_FADE) || defined(BLEND_MODE_MASKED)
    return a;
#else
    return 1.0;
#endif
}

#if 0 // vanilla handles alpha testing itself
#if defined(BLEND_MODE_MASKED)
float computeMaskedAlpha(float a) {
    // Use derivatives to smooth alpha tested edges
    return (a - getMaskThreshold()) / max(fwidth(a), 1e-3) + 0.5;
}
#endif

void applyAlphaMask(inout vec4 baseColor) {
#if defined(BLEND_MODE_MASKED)
    baseColor.a = computeMaskedAlpha(baseColor.a);
    if (baseColor.a <= 0.0) {
        discard;
    }
#endif
}
#endif

#if defined(GEOMETRIC_SPECULAR_AA)
float normalFiltering(float perceptualRoughness, const vec3 worldNormal) {
    // Kaplanyan 2016, "Stable specular highlights"
    // Tokuyoshi 2017, "Error Reduction and Simplification for Shading Anti-Aliasing"
    // Tokuyoshi and Kaplanyan 2019, "Improved Geometric Specular Antialiasing"

    // This implementation is meant for deferred rendering in the original paper but
    // we use it in forward rendering as well (as discussed in Tokuyoshi and Kaplanyan
    // 2019). The main reason is that the forward version requires an expensive transform
    // of the half vector by the tangent frame for every light. This is therefore an
    // approximation but it works well enough for our needs and provides an improvement
    // over our original implementation based on Vlachos 2015, "Advanced VR Rendering".

    vec3 du = ddx( worldNormal ) * g_globals.GlobalSpecularAAScale.xxx;
    vec3 dv = ddy( worldNormal ) * g_globals.GlobalSpecularAAScale.xxx;
    
    // this should maybe be part of material itself
    const float specularAntiAliasingVariance    = GEOMETRIC_SPECULAR_AA_VARIANCE;
    const float specularAntiAliasingThreshold   = GEOMETRIC_SPECULAR_AA_THRESHOLD;

    float variance = /*materialParams._*/specularAntiAliasingVariance * (dot(du, du) + dot(dv, dv));

    float roughness = perceptualRoughnessToRoughness(perceptualRoughness);
    float kernelRoughness = min(2.0 * variance, /*materialParams._*/specularAntiAliasingThreshold);
    float squareRoughness = saturate(roughness * roughness + kernelRoughness);

    return roughnessToPerceptualRoughness(sqrt(squareRoughness));
}
#endif

#if defined(GEOMETRIC_SPECULAR_AA_UNIFIED)
// Same as above but variance is pre-computed
float normalFiltering(float perceptualRoughness, float variance) {
    // Kaplanyan 2016, "Stable specular highlights"
    // Tokuyoshi 2017, "Error Reduction and Simplification for Shading Anti-Aliasing"
    // Tokuyoshi and Kaplanyan 2019, "Improved Geometric Specular Antialiasing"

    // This implementation is meant for deferred rendering in the original paper but
    // we use it in forward rendering as well (as discussed in Tokuyoshi and Kaplanyan
    // 2019). The main reason is that the forward version requires an expensive transform
    // of the half vector by the tangent frame for every light. This is therefore an
    // approximation but it works well enough for our needs and provides an improvement
    // over our original implementation based on Vlachos 2015, "Advanced VR Rendering".

    // this should maybe be part of material itself
    const float specularAntiAliasingVariance    = GEOMETRIC_SPECULAR_AA_VARIANCE;
    const float specularAntiAliasingThreshold   = GEOMETRIC_SPECULAR_AA_THRESHOLD;

    variance = /*materialParams._*/specularAntiAliasingVariance * variance;

    float roughness = perceptualRoughnessToRoughness(perceptualRoughness);
    float kernelRoughness = min(2.0 * variance, /*materialParams._*/specularAntiAliasingThreshold);
    float squareRoughness = saturate(roughness * roughness + kernelRoughness);

    return roughnessToPerceptualRoughness(sqrt(squareRoughness));
}
#endif

/**
 * Computes all the parameters required to shade the current pixel/fragment.
 * These parameters are derived from the MaterialInputs structure computed
 * by the user's material code.
 *
 * This function is also responsible for discarding the fragment when alpha
 * testing fails.
 */
PixelParams ComputePixelParams(const SurfaceInteraction surface, const ShadingParams shading, const MaterialInputs material) // was getPixelParams
{
    // Since HLSL expects 'inout' to be fully initialized before leaving the function, compiler was complaining
    // when the multiple getters were used so we've just embedded them here manually. If someone has a better
    // idea please let me know :)
    PixelParams pixel;

    // getCommonPixelParams(shading, material, pixel);
    {
        vec4 baseColor = material.BaseColor;
        //applyAlphaMask(baseColor);

    #if defined(BLEND_MODE_FADE) && !defined(SHADING_MODEL_UNLIT)
    #error not ported to vanilla yet
        // Since we work in premultiplied alpha mode, we need to un-premultiply
        // in fade mode so we can apply alpha to both the specular and diffuse
        // components at the end
        unpremultiply(baseColor);
    #endif

    #if defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
        // This is from KHR_materials_pbrSpecularGlossiness.
        vec3 specularColor = material.SpecularColor;
        float metallic = computeMetallicFromSpecularColor(specularColor);

        pixel.DielectricF0 = 0;
        pixel.DiffuseColor = computeDiffuseColor(baseColor, metallic);
        pixel.F0 = specularColor;
    #elif !defined(SHADING_MODEL_CLOTH)
    #if defined(HAS_REFRACTION) && (!defined(MATERIAL_HAS_REFLECTANCE) && defined(MATERIAL_HAS_IOR))
        pixel.DiffuseColor = baseColor.rgb;
        // If refraction is enabled, and reflectance is not set in the material, but ior is,
        // then use it -- othterwise proceed as usual.
        todo: what to do with pixel.DielectricF0? it's the same as F0?
        pixel.F0 = vec3(iorToF0(material.ior, 1.0));
    #else
        pixel.DiffuseColor = computeDiffuseColor(baseColor, material.Metallic);

        // Assumes an interface from air to an IOR of 1.5 for dielectrics
        pixel.DielectricF0 = computeDielectricF0(material.Reflectance);
        pixel.F0 = computeF0(baseColor, material.Metallic, pixel.DielectricF0);
        pixel.DielectricF0 *= (1.0 - material.Metallic);
    #endif
    #else
        pixel.DiffuseColor = baseColor.rgb;
        pixel.F0 = material.SheenColor;
    #if defined(MATERIAL_HAS_SUBSURFACE_COLOR)
        pixel.SubsurfaceColor = material.SubsurfaceColor;
    #endif
    #endif    
    

#if defined(HAS_REFRACTION)
    // Air's Index of refraction is 1.000277 at STP but everybody uses 1.0
    const float airIor = 1.0;
#if !defined(MATERIAL_HAS_IOR)
    // [common case] ior is not set in the material, deduce it from F0
    float materialor = f0ToIor(pixel.f0.g);
#else
    // if ior is set in the material, use it (can lead to unrealistic materials)
    float materialor = max(1.0, material.ior);
#endif
    pixel.etaIR = airIor / materialor;  // air -> material
    pixel.etaRI = materialor / airIor;  // material -> air
#if defined(MATERIAL_HAS_TRANSMISSION)
    pixel.transmission = saturate(material.transmission);
#else
    pixel.transmission = 1.0;
#endif
#if defined(MATERIAL_HAS_ABSORPTION)
#if defined(MATERIAL_HAS_THICKNESS) || defined(MATERIAL_HAS_MICRO_THICKNESS)
    pixel.absorption = max(vec3(0.0), material.absorption);
#else
    pixel.absorption = saturate(material.absorption);
#endif
#else
    pixel.absorption = vec3(0.0);
#endif
#if defined(MATERIAL_HAS_THICKNESS)
    pixel.thickness = max(0.0, material.thickness);
#endif
#if defined(MATERIAL_HAS_MICRO_THICKNESS) && (REFRACTION_TYPE == REFRACTION_TYPE_THIN)
pixel.uThickness = max(0.0, material.microThickness);
#else
pixel.uThickness = 0.0;
#endif
#endif    
    }

    // getClearCoatPixelParams(shading, material, pixel);
    {
    #if defined(MATERIAL_HAS_CLEAR_COAT)
        pixel.ClearCoat = material.ClearCoat;

        // Clamp the clear coat roughness to avoid divisions by 0
        float clearCoatPerceptualRoughness = material.ClearCoatRoughness;
        clearCoatPerceptualRoughness =
                clamp(clearCoatPerceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);

    #if defined(GEOMETRIC_SPECULAR_AA)
        #if defined(GEOMETRIC_SPECULAR_AA_USE_NORMALMAPS)
            clearCoatPerceptualRoughness = normalFiltering( clearCoatPerceptualRoughness, shading.ClearCoatNormal );
        #else
            clearCoatPerceptualRoughness = normalFiltering( clearCoatPerceptualRoughness, shading.GetWorldGeometricNormalVector() );
        #endif
    #endif
    #if defined(GEOMETRIC_SPECULAR_AA_UNIFIED)
        #if defined(GEOMETRIC_SPECULAR_AA) || defined(GEOMETRIC_SPECULAR_AA_USE_NORMALMAPS)
            #error These defines are incompatible
        #endif
        clearCoatPerceptualRoughness = normalFiltering( clearCoatPerceptualRoughness, surface.NormalVariance );
    #endif

        pixel.clearCoatPerceptualRoughness = clearCoatPerceptualRoughness;
        pixel.clearCoatRoughness = perceptualRoughnessToRoughness(clearCoatPerceptualRoughness);

    #if defined(CLEAR_COAT_IOR_CHANGE)
        // The base layer's f0 is computed assuming an interface from air to an IOR
        // of 1.5, but the clear coat layer forms an interface from IOR 1.5 to IOR
        // 1.5. We recompute f0 by first computing its IOR, then reconverting to f0
        // by using the correct interface
        pixel.F0 = mix(pixel.F0, f0ClearCoatToSurface(pixel.F0), pixel.ClearCoat);
    #endif
    #endif    
    }

    //getRoughnessPixelParams(shading, material, pixel);
    {
    #if defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
        float perceptualRoughness = computeRoughnessFromGlossiness(material.Glossiness);
    #else
        float perceptualRoughness = material.Roughness;
    #endif
   

        // Clamp the roughness to a minimum value to avoid divisions by 0 during lighting
        //perceptualRoughness = clamp(perceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);

    #if defined(GEOMETRIC_SPECULAR_AA)
        #if defined(GEOMETRIC_SPECULAR_AA_USE_NORMALMAPS)
            perceptualRoughness = normalFiltering( perceptualRoughness, shading.Normal );
        #else
            perceptualRoughness = normalFiltering( perceptualRoughness, shading.GetWorldGeometricNormalVector() );
        #endif
    #endif
    #if defined(GEOMETRIC_SPECULAR_AA_UNIFIED)
        #if defined(GEOMETRIC_SPECULAR_AA) || defined(GEOMETRIC_SPECULAR_AA_USE_NORMALMAPS)
            #error These defines are incompatible
        #endif
        perceptualRoughness = normalFiltering( perceptualRoughness, surface.NormalVariance );
    #endif

    #if defined(MATERIAL_HAS_CLEAR_COAT) && defined(MATERIAL_HAS_CLEAR_COAT_ROUGHNESS)
        // This is a hack but it will do: the base layer must be at least as rough
        // as the clear coat layer to take into account possible diffusion by the
        // top layer
        float basePerceptualRoughness = max(perceptualRoughness, pixel.ClearCoatPerceptualRoughness);
        perceptualRoughness = mix(perceptualRoughness, basePerceptualRoughness, pixel.ClearCoat);
    #endif

        pixel.PerceptualRoughnessUnclamped = perceptualRoughness;
        // Clamp the roughness to a minimum value to avoid divisions by 0 during lighting
        pixel.PerceptualRoughness = clamp(perceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
        // Remaps the roughness to a perceptually linear roughness (roughness^2)
        pixel.Roughness = perceptualRoughnessToRoughness(pixel.PerceptualRoughness);
        pixel.RoughnessUnclamped = perceptualRoughnessToRoughness(pixel.PerceptualRoughnessUnclamped);
    }

    // getSubsurfacePixelParams(shading, material, pixel);
    {
    #if defined(SHADING_MODEL_SUBSURFACE)
        pixel.SubsurfacePower = material.SubsurfacePower;
        pixel.SubsurfaceColor = material.SubsurfaceColor;
        pixel.Thickness = saturate(material.Thickness);
    #endif
    }

    //getAnisotropyPixelParams(shading, material, pixel);
    {
    #if defined(MATERIAL_HAS_ANISOTROPY)
        vec3 direction = material.AnisotropyDirection;
        pixel.Anisotropy = material.Anisotropy;
        pixel.AnisotropicT = normalize( mul( direction, shading.TangentToWorld ) );
        pixel.AnisotropicB = normalize( cross( shading.GetWorldGeometricNormalVector(), pixel.AnisotropicT) );
    #endif
    }

    //getEnergyCompensationPixelParams(shading, pixel);
    {
    // Pre-filtered DFG term used for image-based lighting
    pixel.DFG = prefilteredDFG( pixel.PerceptualRoughness, shading.NoV );

#if !defined(SHADING_MODEL_CLOTH)
    // Energy compensation for multiple scattering in a microfacet model
    // See "Multiple-Scattering Microfacet BSDFs with the Smith Model"
    pixel.EnergyCompensation = 1.0 + pixel.F0 * (1.0 / pixel.DFG.y - 1.0);
#else
    pixel.EnergyCompensation = 1.0.xxx;
#endif
    }

    {
        float f0 =  (pixel.F0.r+pixel.F0.g+pixel.F0.b)                              ;
        float d =   (pixel.DiffuseColor.r+pixel.DiffuseColor.g+pixel.DiffuseColor.b);
        float f = F_Schlick( f0, 25*f0, shading.NoV ); // why not 50x? this seemed like a better estimate for purposes of finding best adaptive sampling distribution <shrug>
        pixel.ReflectivityEstimate = saturate(f / (f+d));
    }

    return pixel;
}

#if 0

/**
 * This function evaluates all lights one by one:
 * - Image based lights (IBL)
 * - Directional lights
 * - Punctual lights
 *
 * Area lights are currently not supported.
 *
 * Returns a pre-exposed HDR RGBA color in linear space.
 */
vec4 evaluateLights(const MaterialInputs material) {
    PixelParams pixel;
    getPixelParams(material, pixel);

    // Ideally we would keep the diffuse and specular components separate
    // until the very end but it costs more ALUs on mobile. The gains are
    // currently not worth the extra operations
    vec3 color = vec3(0.0);

    // We always evaluate the IBL as not having one is going to be uncommon,
    // it also saves 1 shader variant
    evaluateIBL(material, pixel, color, true, true);

#if defined(HAS_DIRECTIONAL_LIGHTING)
    evaluateDirectionalLight(material, pixel, color);
#endif

#if defined(HAS_DYNAMIC_LIGHTING)
    evaluatePunctualLights(pixel, color);
#endif

#if defined(BLEND_MODE_FADE) && !defined(SHADING_MODEL_UNLIT)
    // In fade mode we un-premultiply baseColor early on, so we need to
    // premultiply again at the end (affects diffuse and specular lighting)
    color *= material.baseColor.a;
#endif

    return vec4(color, computeDiffuseAlpha(material.baseColor.a));
}

void addEmissive(const MaterialInputs material, inout vec4 color) {
#if defined(MATERIAL_HAS_EMISSIVE)
    // The emissive property applies independently of the shading model
    // It is defined as a color + exposure compensation
    highp vec4 emissive = material.emissive;
    highp float attenuation = computePreExposedIntensity(
            pow(2.0, frameUniforms.ev100 + emissive.w - 3.0), frameUniforms.exposure);
    color.rgb += emissive.rgb * attenuation;
#endif
}

/**
 * Evaluate lit materials. The actual shading model used to do so is defined
 * by the function surfaceShading() found in shading_model_*.fs.
 *
 * Returns a pre-exposed HDR RGBA color in linear space.
 */
vec4 evaluateMaterial(const MaterialInputs material) {
    vec4 color = evaluateLights(material);
    addEmissive(material, color);
    return color;
}

#endif