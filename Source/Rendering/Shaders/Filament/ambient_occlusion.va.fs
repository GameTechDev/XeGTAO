//------------------------------------------------------------------------------
// Ambient occlusion configuration
//------------------------------------------------------------------------------

// Diffuse BRDFs
#define SPECULAR_AO_OFF             0
#define SPECULAR_AO_SIMPLE          1
#define SPECULAR_AO_BENT_NORMALS    2

//------------------------------------------------------------------------------
// Ambient occlusion helpers
//------------------------------------------------------------------------------

// in Vanilla this is passed as a parameter and mixed with material.AmbientOcclusion into materialSurface.DiffuseAmbientOcclusion
// float evaluateSSAO() {
// }

float SpecularAO_Lagarde(float NoV, float visibility, float roughness) {
    // Lagarde and de Rousiers 2014, "Moving Frostbite to PBR"
    return saturate(pow(NoV + visibility, exp2(-16.0 * roughness - 1.0)) - 1.0 + visibility);
}

#if 1 //defined(MATERIAL_HAS_BENT_NORMAL)
float sphericalCapsIntersection(float cosCap1, float cosCap2, float cosDistance) {
    // Oat and Sander 2007, "Ambient Aperture Lighting"
    // Approximation mentioned by Jimenez et al. 2016
    float r1 = FastAcosPositive(cosCap1);
    float r2 = FastAcosPositive(cosCap2);
    float d  = FastAcos(cosDistance);

    // We work with cosine angles, replace the original paper's use of
    // cos(min(r1, r2)) with max(cosCap1, cosCap2)
    // We also remove a multiplication by 2 * PI to simplify the computation
    // since we divide by 2 * PI in computeBentSpecularAO()

    if (min(r1, r2) <= max(r1, r2) - d) {
        return 1.0 - max(cosCap1, cosCap2);
    } else if (r1 + r2 <= d) {
        return 0.0;
    }

    float delta = abs(r1 - r2);
    float x = 1.0 - saturate((d - delta) / max(r1 + r2 - delta, 1e-4));
    // simplified smoothstep()
    float area = Sq(x) * (-2.0 * x + 3.0);
    return area * (1.0 - max(cosCap1, cosCap2));
}
#endif

// This function could (should?) be implemented as a 3D LUT instead, but we need to save samplers
float SpecularAO_Cones( const MaterialInteraction materialSurface, float visibility, float roughness ) 
{
#if 1 //defined(MATERIAL_HAS_BENT_NORMAL)
    // Jimenez et al. 2016, "Practical Realtime Strategies for Accurate Indirect Occlusion"

    // aperture from ambient occlusion
    float cosAv = sqrt(1.0 - visibility);
    // aperture from roughness, log(10) / log(2) = 3.321928
    float cosAs = exp2(-3.321928 * Sq(roughness));
    // angle betwen bent normal and reflection direction
    float cosB  = dot(materialSurface.BentNormal, materialSurface.Reflected);

    // Remove the 2 * PI term from the denominator, it cancels out the same term from
    // sphericalCapsIntersection()
    float ao = sphericalCapsIntersection(cosAv, cosAs, cosB) / (1.0 - cosAs);
    // Smoothly kill specular AO when entering the perceptual roughness range [0.1..0.3]
    // Without this, specular AO can remove all reflections, which looks bad on metals
    return lerp(1.0, ao, smoothstep(0.01, 0.09, roughness));
#else
    return SpecularAO_Lagarde(materialSurface.NoV, visibility, roughness);
#endif
}

/**
 * Computes a specular occlusion term from the ambient occlusion term.
 */
float computeSpecularAO(const MaterialInteraction materialSurface, float visibility, float roughness) {
#if SPECULAR_AMBIENT_OCCLUSION == SPECULAR_AO_SIMPLE
    return SpecularAO_Lagarde(materialSurface.NoV, visibility, roughness);
#elif SPECULAR_AMBIENT_OCCLUSION == SPECULAR_AO_BENT_NORMALS
    return SpecularAO_Cones(materialSurface, visibility, roughness);
#else
    return 1.0;
#endif
}

#if MULTI_BOUNCE_AMBIENT_OCCLUSION == 1
/**
 * Returns a color ambient occlusion based on a pre-computed visibility term.
 * The albedo term is meant to be the diffuse color or f0 for the diffuse and
 * specular terms respectively.
 */
float3 gtaoMultiBounce(float visibility, const float3 albedo) {
    // Jimenez et al. 2016, "Practical Realtime Strategies for Accurate Indirect Occlusion"
    float3 a =  2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c =  2.7552 * albedo + 0.6903;

    return max(float3(visibility.xxx), ((visibility * a + b) * visibility + c) * visibility);
}
#endif

void multiBounceAO(float visibility, const float3 albedo, inout float3 color) {
#if MULTI_BOUNCE_AMBIENT_OCCLUSION == 1
    color *= gtaoMultiBounce(visibility, albedo);
#endif
}

void multiBounceSpecularAO(float visibility, const float3 albedo, inout float3 color) {
#if MULTI_BOUNCE_AMBIENT_OCCLUSION == 1 && SPECULAR_AMBIENT_OCCLUSION != SPECULAR_AO_OFF
    color *= gtaoMultiBounce(visibility, albedo);
#endif
}

float singleBounceAO(float visibility) {
#if MULTI_BOUNCE_AMBIENT_OCCLUSION == 1
    return 1.0;
#else
    return visibility;
#endif
}

// a contact shadow approximation, totally not physically correct; a riff on "Chan 2018, "Material Advances in Call of Duty: WWII" and "The Technical Art of Uncharted 4" http://advances.realtimerendering.com/other/2016/naughty_dog/NaughtyDog_TechArt_Final.pdf (microshadowing)"
// TODO: figure it out with bent normals! see https://www.activision.com/cdn/research/siggraph_2018_opt.pdf
float computeMicroShadowing(float NoL, float ao) 
{
#if DIRECT_LIGHTING_AO_MICROSHADOWS
#if 0 // from the paper - different from Filament and looks wrong
    float aperture = 2.0 * ao * ao;
    return saturate( abs(NoL) + aperture - 1.0 );
#else // filament version
    float aperture = rsqrt(1.0000001 - ao);
    NoL += 0.1; // when using bent normals, avoids overshadowing - bent normals are just approximation anyhow
    return saturate(NoL * aperture);
#endif
#else
    return 1;
#endif
}
