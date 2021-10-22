#if defined(MATERIAL_HAS_CLEAR_COAT)
float clearCoatLobe( const MaterialInteraction materialSurface, const float3 h, float NoH, float LoH, out float Fcc ) {

    // If the material has a normal map, we want to use the geometric normal
    // instead to avoid applying the normal map details to the clear coat layer
    float clearCoatNoH = saturate(dot(materialSurface.ClearCoatNormal, h));

    // clear coat specular lobe
    float D = distributionClearCoat(materialSurface.ClearCoatRoughness, clearCoatNoH, h);
    float V = visibilityClearCoat(LoH);
    float F = F_Schlick(0.04, 1.0, LoH) * materialSurface.ClearCoat; // fix IOR to 1.5

    Fcc = F;
    return D * V * F;
}
#endif

#if defined(MATERIAL_HAS_ANISOTROPY)
float3 anisotropicLobe(const float3 Wo, const MaterialInteraction materialSurface, const float3 lightDir, const float3 h,
        float NoV, float NoL, float NoH, float LoH) {

    float3 l = lightDir;
    float3 t = materialSurface.AnisotropicT;
    float3 b = materialSurface.AnisotropicB;
    float3 v = Wo; // Wo - a.k.a. materialSurface.View 

    float ToV = dot(t, v);
    float BoV = dot(b, v);
    float ToL = dot(t, l);
    float BoL = dot(b, l);
    float ToH = dot(t, h);
    float BoH = dot(b, h);

    // Anisotropic parameters: at and ab are the roughness along the tangent and bitangent
    // to simplify materials, we derive them from a single roughness parameter
    // Kulla 2017, "Revisiting Physically Based Shading at Imageworks"
    float at = max(materialSurface.Roughness * (1.0 + materialSurface.Anisotropy), MIN_ROUGHNESS);
    float ab = max(materialSurface.Roughness * (1.0 - materialSurface.Anisotropy), MIN_ROUGHNESS);

    // specular anisotropic BRDF
    float D = distributionAnisotropic(at, ab, ToH, BoH, NoH);
    float V = visibilityAnisotropic(materialSurface.Roughness, at, ab, ToV, BoV, ToL, BoL, NoV, NoL);
    float3  F = fresnel(materialSurface.F0, LoH);

    return (D * V) * F;
}
#endif

float3 isotropicLobe( const MaterialInteraction materialSurface, const float3 lightDir, const float3 h, float NoV, float NoL, float NoH, float LoH) 
{

    float D = distribution(materialSurface.Roughness, NoH, h);
    float V = visibility(materialSurface.Roughness, NoV, NoL);
    float3  F = fresnel(materialSurface.F0, LoH);

    return (D * V) * F;
}

// Wo - a.k.a. materialSurface.View 
float3 specularLobe( const float3 Wo, const MaterialInteraction materialSurface, const float3 lightDir, const float3 h, float NoV, float NoL, float NoH, float LoH) 
{
#if defined(MATERIAL_HAS_ANISOTROPY)
    return anisotropicLobe(Wo, materialSurface, lightDir, h, NoV, NoL, NoH, LoH);
#else
    return isotropicLobe(materialSurface, lightDir, h, NoV, NoL, NoH, LoH);
#endif
}

float3 diffuseLobe(const MaterialInteraction materialSurface, float NoV, float NoL, float LoH) {
    return materialSurface.DiffuseColor * diffuse(materialSurface.Roughness, NoV, NoL, LoH);
}

float approxSpecularLobeLum(const MaterialInteraction materialSurface, const float3 lightDir, const float3 h, float NoV, float NoL, float NoH, float LoH) 
{
    // TODO: CalcLuminance - could be precomputed for materialSurface.F0
    float D = D_GGX(materialSurface.Roughness, NoH, h);
    float V = V_SmithGGXCorrelated_Fast(materialSurface.Roughness, NoV, NoL);
    float F = F_Schlick(CalcLuminance(materialSurface.F0), LoH);
    return (D * V) * F;
}
float approxDiffuseLobeLum(const MaterialInteraction materialSurface, float NoV, float NoL, float LoH) 
{
    // TODO: CalcLuminance - could be precomputed for materialSurface.DiffuseColor
    return CalcLuminance(materialSurface.DiffuseColor) * Fd_Lambert();
}
