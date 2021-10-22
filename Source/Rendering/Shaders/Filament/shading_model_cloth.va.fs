/**
 * Evaluates lit materials with the cloth shading model. Similar to the standard
 * model, the cloth shading model is based on a Cook-Torrance microfacet model.
 * Its distribution and visibility terms are however very different to take into
 * account the softer apperance of many types of cloth. Some highly reflecting
 * fabrics like satin or leather should use the standard model instead.
 *
 * This shading model optionally models subsurface scattering events. The
 * computation of these events is not physically based but can add necessary
 * details to a material.
 */
void surfaceShading( const MaterialInteraction materialSurface, const LightParams light, float occlusion, inout float3 inoutColor )
{
    vec3 h = normalize( materialSurface.View + light.L );
    float NoL = light.NoL;
    float NoH = saturate( dot( materialSurface.Normal, h ) );
    float LoH = saturate( dot( light.L, h ) );

    // specular BRDF
    float D = distributionCloth( materialSurface.Roughness, NoH );
    float V = visibilityCloth( materialSurface.NoV, NoL );
    vec3  F = materialSurface.F0;
    // Ignore materialSurface.energyCompensation since we use a different BRDF here
    vec3 Fr = (D * V) * F;

    // diffuse BRDF
    float _diffuse = diffuse( materialSurface.Roughness, materialSurface.NoV, NoL, LoH );
#if defined(MATERIAL_HAS_SUBSURFACE_COLOR)
    // Energy conservative wrap diffuse to simulate subsurface scattering
    _diffuse *= Fd_Wrap( dot( materialSurface.Normal, light.L), 0.5 );
#endif

    // We do not multiply the diffuse term by the Fresnel term as discussed in
    // Neubelt and Pettineo 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"
    // The effect is fairly subtle and not deemed worth the cost for mobile
    vec3 Fd = _diffuse * materialSurface.DiffuseColor;

#if defined(MATERIAL_HAS_SUBSURFACE_COLOR)
    // Cheap subsurface scatter
    Fd *= saturate( materialSurface.SubsurfaceColor + NoL );
    // We need to apply NoL separately to the specular lobe since we already took
    // it into account in the diffuse lobe
    vec3 color = Fd + Fr * NoL;
    color *= light.ColorIntensity.rgb * (light.Attenuation * occlusion);
#else
    vec3 color = Fd + Fr;
    color *= light.ColorIntensity.rgb * (light.Attenuation * NoL * occlusion);
#endif
    inoutColor += color;
}
