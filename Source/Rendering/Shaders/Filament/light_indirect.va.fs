#ifndef VA_LIGHT_INDIRECT_INCLUDED
#define VA_LIGHT_INDIRECT_INCLUDED

//------------------------------------------------------------------------------
// Image based lighting configuration
//------------------------------------------------------------------------------

// Number of spherical harmonics bands (1, 2 or 3)
#define SPHERICAL_HARMONICS_BANDS           3

// moved to vaIBLShared.h
// // IBL integration algorithm
// #define IBL_INTEGRATION_PREFILTERED_CUBEMAP         0
// #define IBL_INTEGRATION_IMPORTANCE_SAMPLING         1
// 
// #define IBL_INTEGRATION_ALGORITHM                             IBL_INTEGRATION_PREFILTERED_CUBEMAP

#define IBL_INTEGRATION_IMPORTANCE_SAMPLING_COUNT   64

//------------------------------------------------------------------------------
// IBL utilities
//------------------------------------------------------------------------------

float3 decodeDataForIBL(const float4 data) {
    return data.rgb;
}

//------------------------------------------------------------------------------
// IBL irradiance implementations
//------------------------------------------------------------------------------

float3 Irradiance_SphericalHarmonics(const float3 n, vaVector4 DiffuseSH[9] ) 
{
    return max(
          DiffuseSH[0].xyz
#if SPHERICAL_HARMONICS_BANDS >= 2
        + DiffuseSH[1].xyz * (n.y).xxx
        + DiffuseSH[2].xyz * (n.z).xxx
        + DiffuseSH[3].xyz * (n.x).xxx
#endif
#if SPHERICAL_HARMONICS_BANDS >= 3
        + DiffuseSH[4].xyz * (n.y * n.x).xxx
        + DiffuseSH[5].xyz * (n.y * n.z).xxx
        + DiffuseSH[6].xyz * (3.0 * n.z * n.z - 1.0).xxx
        + DiffuseSH[7].xyz * (n.z * n.x).xxx
        + DiffuseSH[8].xyz * (n.x * n.x - n.y * n.y).xxx
#endif
        , 0.0.xxx);
}

//------------------------------------------------------------------------------
// IBL irradiance dispatch
//------------------------------------------------------------------------------

float3 DiffuseIrradiance( const MaterialInteraction materialSurface, float3 n, bool useLocalIBL, bool useDistantIBL ) 
{
    float3 local = 0, distant = 0;

    // no longer needed; n and v are in worldspace and IBL cube is in worldspace too
    // n = mul( (float3x3)g_DistantIBL.WorldToIBLRotation, n );                // this should go out in the future

#if IBL_IRRADIANCE_SOURCE == IBL_IRRADIANCE_SH
    [branch] if( materialSurface.IBL.UseLocal && useLocalIBL )
        local   = g_lighting.LocalIBL.PreExposedLuminance * Irradiance_SphericalHarmonics( ComputeIBLDirection( materialSurface.Position, g_lighting.LocalIBL, n ), g_lighting.LocalIBL.DiffuseSH );
    [branch] if( materialSurface.IBL.UseDistant && useDistantIBL )
        distant = g_lighting.DistantIBL.PreExposedLuminance * Irradiance_SphericalHarmonics( ComputeIBLDirection( materialSurface.Position, g_lighting.DistantIBL, n ), g_lighting.DistantIBL.DiffuseSH );
#elif IBL_IRRADIANCE_SOURCE == IBL_IRRADIANCE_CUBEMAP
    #ifdef VA_RAYTRACING
        // not a huge loss sampling only MIP0 since these are very low res anyhow
        [branch] if( materialSurface.IBL.UseLocal && useLocalIBL )
            local   = g_lighting.LocalIBL.PreExposedLuminance * g_LocalIBLIrradianceMap.SampleLevel( g_samplerLinearClamp, ComputeIBLDirection( materialSurface.Position, g_lighting.LocalIBL, n ), 0 ).rgb;
        [branch] if( materialSurface.IBL.UseDistant && useDistantIBL )
            distant = g_lighting.DistantIBL.PreExposedLuminance * g_DistantIBLIrradianceMap.SampleLevel( g_samplerLinearClamp, ComputeIBLDirection( materialSurface.Position, g_lighting.DistantIBL, n ), 0 ).rgb;
    #else // ifdef VA_RAYTRACING
        [branch] if( materialSurface.IBL.UseLocal && useLocalIBL )
            local   = g_lighting.LocalIBL.PreExposedLuminance * g_LocalIBLIrradianceMap.Sample( g_samplerLinearClamp, ComputeIBLDirection( materialSurface.Position, g_lighting.LocalIBL, n ) ).rgb;
        [branch] if( materialSurface.IBL.UseDistant && useDistantIBL )
            distant = g_lighting.DistantIBL.PreExposedLuminance * g_DistantIBLIrradianceMap.Sample( g_samplerLinearClamp, ComputeIBLDirection( materialSurface.Position, g_lighting.DistantIBL, n ) ).rgb;
    #endif
#else
#error IBL_IRRADIANCE_SOURCE not correctly defined / supported
#endif
    
    return lerp( local, distant, materialSurface.IBL.LocalToDistantK );
}


//------------------------------------------------------------------------------
// IBL specular
//------------------------------------------------------------------------------

float3 prefilteredRadiance( const MaterialInteraction materialSurface, float3 r, float perceptualRoughness, bool useLocalIBL, bool useDistantIBL ) 
{
    // lod = lod_count * sqrt(roughness), which is the mapping used by cmgen
    // where roughness = perceptualRoughness^2
    // using all the mip levels requires seamless cubemap sampling
   
    // no longer needed; n and v are in worldspace and IBL cube is in worldspace too
    //r = normalize( mul( (float3x3)g_DistantIBL.WorldToIBLRotation, r ) );    // this should go out in the future

    float3 local = 0, distant = 0;

    [branch] if( useLocalIBL )
    {
        float lod = min( g_lighting.LocalIBL.MaxReflMipLevel.x * perceptualRoughness, g_lighting.LocalIBL.ReflMipLevelClamp );
        local   = g_lighting.LocalIBL.PreExposedLuminance * decodeDataForIBL( g_LocalIBLReflectionsMap.SampleLevel( g_samplerLinearClamp, ComputeIBLDirection( materialSurface.Position, g_lighting.LocalIBL, r ), lod ) );
    }
    [branch] if( useDistantIBL )
    {
        float lod = min( g_lighting.DistantIBL.MaxReflMipLevel.x * perceptualRoughness, g_lighting.DistantIBL.ReflMipLevelClamp );
        distant = g_lighting.DistantIBL.PreExposedLuminance * decodeDataForIBL( g_DistantIBLReflectionsMap.SampleLevel( g_samplerLinearClamp, ComputeIBLDirection( materialSurface.Position, g_lighting.DistantIBL, r ), lod ) );
    }

    return lerp( local, distant, materialSurface.IBL.LocalToDistantK );
}

float3 getSpecularDominantDirection(const float3 n, const float3 r, float roughness) {
    return lerp(r, n, roughness * roughness);
}

float3 specularDFG( const MaterialInteraction materialSurface ) {
#if defined(SHADING_MODEL_CLOTH)
    return materialSurface.F0 * materialSurface.DFG.z;
#elif defined( SHADING_MODEL_SPECULAR_GLOSSINESS )
    return materialSurface.F0; // this seems to match better what the spec-gloss looks like in general
#else
    return lerp(materialSurface.DFG.xxx, materialSurface.DFG.yyy, materialSurface.F0);
#endif
}

/**
 * Returns the reflected vector at the current materialSurface point. The reflected vector
 * return by this function might be different from materialSurface.Reflected:
 * - For anisotropic material, we bend the reflection vector to simulate
 *   anisotropic indirect lighting
 * - The reflected vector may be modified to point towards the dominant specular
 *   direction to match reference renderings when the roughness increases
 */

float3 getReflectedVector(const MaterialInteraction materialSurface, const float3 v, const float3 n) {
#if defined(MATERIAL_HAS_ANISOTROPY)
    float3  anisotropyDirection = materialSurface.Anisotropy >= 0.0 ? materialSurface.AnisotropicB : materialSurface.AnisotropicT;
    float3  anisotropicTangent  = cross(anisotropyDirection, v);
    float3  anisotropicNormal   = cross(anisotropicTangent, anisotropyDirection);
    float bendFactor          = abs(materialSurface.Anisotropy) * saturate(5.0 * materialSurface.PerceptualRoughness);
    float3  bentNormal          = normalize(lerp(n, anisotropicNormal, bendFactor));

    float3 r = reflect(-v, bentNormal);
#else
    float3 r = reflect(-v, n);
#endif
    return r;
}

float3 getReflectedVector( const MaterialInteraction materialSurface, const float3 n ) {
#if defined(MATERIAL_HAS_ANISOTROPY)
    float3 r = getReflectedVector( materialSurface, materialSurface.View, n );
#else
    float3 r = materialSurface.Reflected;
#endif
    return getSpecularDominantDirection( n, r, materialSurface.Roughness );
}

//------------------------------------------------------------------------------
// Prefiltered importance sampling
//------------------------------------------------------------------------------

#if IBL_INTEGRATION_ALGORITHM == IBL_INTEGRATION_IMPORTANCE_SAMPLING
#error this is no longer kept up to date
vec2 hammersley(uint index) {
    // Compute Hammersley sequence
    // TODO: these should come from uniforms
    // TODO: we should do this with logical bit operations
    const uint numSamples = uint(IBL_INTEGRATION_IMPORTANCE_SAMPLING_COUNT);
    const uint numSampleBits = uint(log2(float(numSamples)));
    const float invNumSamples = 1.0 / float(numSamples);
    uint i = uint(index);
    uint t = i;
    uint bits = 0u;
    for (uint j = 0u; j < numSampleBits; j++) {
        bits = bits * 2u + (t - (2u * (t / 2u)));
        t /= 2u;
    }
    return vec2(float(i), float(bits)) * invNumSamples;
}

float3 importanceSamplingNdfDggx(vec2 u, float roughness) {
    // Importance sampling D_GGX
    float a2 = roughness * roughness;
    float phi = 2.0 * PI * u.x;
    float cosTheta2 = (1.0 - u.y) / (1.0 + (a2 - 1.0) * u.y);
    float cosTheta = sqrt(cosTheta2);
    float sinTheta = sqrt(1.0 - cosTheta2);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float3 importanceSamplingVNdfDggx(vec2 u, float roughness, float3 v) {
    // See: "A Simpler and Exact Sampling Routine for the GGX Distribution of Visible Normals", Eric Heitz
    float alpha = roughness;

    // stretch view
    v = normalize(float3(alpha * v.x, alpha * v.y, v.z));

    // orthonormal basis
    float3 up = abs(v.z) < 0.9999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(up, v));
    float3 b = cross(t, v);

    // sample point with polar coordinates (r, phi)
    float a = 1.0 / (1.0 + v.z);
    float r = sqrt(u.x);
    float phi = (u.y < a) ? u.y / a * PI : PI + (u.y - a) / (1.0 - a) * PI;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi) * ((u.y < a) ? 1.0 : v.z);

    // compute normal
    float3 h = p1 * t + p2 * b + sqrt(max(0.0, 1.0 - p1*p1 - p2*p2)) * v;

    // unstretch
    h = normalize(float3(alpha * h.x, alpha * h.y, max(0.0, h.z)));
    return h;
}

float prefilteredImportanceSampling(float ipdf, vec2 iblMaxMipLevel) {
    // See: "Real-time Shading with Filtered Importance Sampling", Jaroslav Krivanek
    // Prefiltering doesn't work with anisotropy
    const float numSamples = float(IBL_INTEGRATION_IMPORTANCE_SAMPLING_COUNT);
    const float invNumSamples = 1.0 / float(numSamples);
    const float dim = iblMaxMipLevel.y;
    const float omegaP = (4.0 * PI) / (6.0 * dim * dim);
    const float invOmegaP = 1.0 / omegaP;
    const float K = 4.0;
    float omegaS = invNumSamples * ipdf;
    float mipLevel = clamp(log2(K * omegaS * invOmegaP) * 0.5, 0.0, iblMaxMipLevel.x);
    return mipLevel;
}

float3 isEvaluateIBL( const MaterialInteraction materialSurface, float3 n, float3 v, float NoV, bool useLocalIBL, bool useDistantIBL )
{
    if( !useDistantIBL )
        return float3(0,0,0);
    // no longer needed; n and v are in worldspace and IBL cube is in worldspace too
    // // viewspace to worldspace
    // n = mul( (float3x3)g_DistantIBL.WorldToIBLRotation, n );
    // v = mul( (float3x3)g_DistantIBL.WorldToIBLRotation, v );

    // TODO: for a true anisotropic BRDF, we need a real tangent space
    float3 up = abs(n.z) < 0.9999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);

    float3x3 tangentToWorld;
    tangentToWorld[0] = normalize(cross(up, n));
    tangentToWorld[1] = cross(n, tangentToWorld[0]);
    tangentToWorld[2] = n;

    float roughness = materialSurface.Roughness;
    float a2 = roughness * roughness;

    float2 iblMaxMipLevel = float2( g_DistantIBL.MaxReflMipLevel, g_DistantIBL.Pow2MaxReflMipLevel );
    const uint numSamples = uint(IBL_INTEGRATION_IMPORTANCE_SAMPLING_COUNT);
    const float invNumSamples = 1.0 / float(numSamples);

    float3 indirectSpecular = 0.0.xxx;
    for (uint i = 0u; i < numSamples; i++)
    {
        vec2 u = hammersley(i);
        float3 h = mul( importanceSamplingNdfDggx(u, roughness), tangentToWorld );

        // Since anisotropy doesn't work with prefiltering, we use the same "faux" anisotropy
        // we do when we use the prefiltered cubemap
        float3 l = getReflectedVector(materialSurface, v, h);

        // Compute this sample's contribution to the brdf
        float NoL = dot(n, l);
        if (NoL > 0.0) {
            float NoH = dot(n, h);
            float LoH = max(dot(l, h), 0.0);

            // PDF inverse (we must use D_GGX() here, which is used to generate samples)
            float ipdf = (4.0 * LoH) / (D_GGX(roughness, NoH, h) * NoH);

            float mipLevel = prefilteredImportanceSampling(ipdf, iblMaxMipLevel);

            // we use texture() instead of textureLod() to take advantage of mipmapping
            float3 L = decodeDataForIBL( g_DistantIBLReflectionsMap.SampleLevel( g_samplerLinearClamp, l, min( mipLevel, g_DistantIBL.ReflMipLevelClamp ) ) );

            float D = distribution(roughness, NoH, h);
            float V = visibility(roughness, NoV, NoL);
            float3  F = fresnel(materialSurface.F0, LoH);
            float3 Fr = F * (D * V * NoL * ipdf * invNumSamples);

            indirectSpecular += (Fr * L);
        }
    }

    return indirectSpecular;
}

void isEvaluateClearCoatIBL( const MaterialInteraction materialSurface, float specularAO, inout float3 Fd, inout float3 Fr, bool useLocalIBL, bool useDistantIBL ) 
{
#if defined(MATERIAL_HAS_CLEAR_COAT)
    // We want to use the geometric normal for the clear coat layer
    float clearCoatNoV = clampNoV(dot(materialSurface.ClearCoatNormal, materialSurface.View));
    float3 clearCoatNormal = materialSurface.ClearCoatNormal;
    // The clear coat layer assumes an IOR of 1.5 (4% reflectance)
    float Fc = F_Schlick(0.04, 1.0, clearCoatNoV) * materialSurface.clearCoat;
    float attenuation = 1.0 - Fc;
    Fd *= attenuation;
    Fr *= attenuation;

    PixelParams p; <- copy to temporarily modify before sending on?
    p.perceptualRoughness = materialSurface.clearCoatPerceptualRoughness;
    p.f0 = float3(0.04);
    p.roughness = PerceptualRoughnessToRoughness(p.perceptualRoughness);
    p.anisotropy = 0.0;

    float3 clearCoatLobe = isEvaluateIBL(p, clearCoatNormal, materialSurface.View, clearCoatNoV, useLocalIBL, useDistantIBL);
    Fr += clearCoatLobe * (specularAO * materialSurface.clearCoat);
#endif
}
#endif

//------------------------------------------------------------------------------
// IBL evaluation
//------------------------------------------------------------------------------

void evaluateClothIndirectDiffuseBRDF( const MaterialInteraction materialSurface, inout float diffuse) {
#if defined(SHADING_MODEL_CLOTH)
#if defined(MATERIAL_HAS_SUBSURFACE_COLOR)
    // Simulate subsurface scattering with a wrap diffuse term
    diffuse *= Fd_Wrap(materialSurface.NoV, 0.5);
#endif
#endif
}

void evaluateClearCoatIBL( const MaterialInteraction materialSurface, float specularAO, inout float3 Fd, inout float3 Fr, bool useLocalIBL, bool useDistantIBL ) {
#if IBL_INTEGRATION_ALGORITHM == IBL_INTEGRATION_IMPORTANCE_SAMPLING
    isEvaluateClearCoatIBL(materialSurface, specularAO, Fd, Fr, useLocalIBL, useDistantIBL);
    return;
#endif

#if defined(MATERIAL_HAS_CLEAR_COAT)
    // We want to use the geometric normal for the clear coat layer
    float clearCoatNoV = clampNoV(dot(materialSurface.ClearCoatNormal, materialSurface.View));
    float3 clearCoatR = reflect(-materialSurface.View, materialSurface.ClearCoatNormal);
    // The clear coat layer assumes an IOR of 1.5 (4% reflectance)
    float Fc = F_Schlick(0.04, 1.0, clearCoatNoV) * materialSurface.clearCoat;
    float attenuation = 1.0 - Fc;
    Fd *= attenuation;
    Fr *= attenuation;
    Fr += prefilteredRadiance( materialSurface, clearCoatR, materialSurface.clearCoatPerceptualRoughness, useLocalIBL, useDistantIBL ) * (specularAO * Fc);
#endif
}

void evaluateSubsurfaceIBL( const MaterialInteraction materialSurface, const float3 diffuseIrradiance, inout float3 Fd, inout float3 Fr, bool useLocalIBL, bool useDistantIBL )
{
#if defined(SHADING_MODEL_SUBSURFACE)
    float3 viewIndependent = diffuseIrradiance;
    float3 viewDependent = prefilteredRadiance( materialSurface, -materialSurface.View, materialSurface.Roughness, 1.0 + materialSurface.Thickness, useLocalIBL, useDistantIBL );
    float attenuation = (1.0 - materialSurface.Thickness) / (2.0 * PI);
    Fd += materialSurface.SubsurfaceColor * (viewIndependent + viewDependent) * attenuation;
#elif defined(SHADING_MODEL_CLOTH) && defined(MATERIAL_HAS_SUBSURFACE_COLOR)
    Fd *= saturate(materialSurface.SubsurfaceColor + materialSurface.NoV);
#endif
}

#if defined(HAS_REFRACTION)

struct Refraction {
    float3 position;
    float3 direction;
    float d;
};

void refractionSolidSphere( const MaterialInteraction materialSurface, const float3 n, float3 r, out Refraction ray )
{
    r = refract(r, n, materialSurface.etaIR);
    float NoR = dot(n, r);
    float d = materialSurface.thickness * -NoR;
    ray.position = float3(materialSurface.Position + r * d);
    ray.d = d;
    float3 n1 = normalize(NoR * r - n * 0.5);
    ray.direction = refract(r, n1,  materialSurface.etaRI);
}

void refractionSolidBox( const MaterialInteraction materialSurface, const float3 n, float3 r, out Refraction ray )
{
    float3 rr = refract(r, n, materialSurface.etaIR);
    float NoR = dot(n, rr);
    float d = materialSurface.thickness / max(-NoR, 0.001);
    ray.position = float3(materialSurface.Position + rr * d);
    ray.direction = r;
    ray.d = d;
#if REFRACTION_MODE == REFRACTION_MODE_CUBEMAP
    // fudge direction vector, so we see the offset due to the thickness of the object
    float envDistance = 10.0; // this should come from a ubo
    ray.direction = normalize((ray.position - materialSurface.Position) + ray.direction * envDistance);
#endif
}

void refractionThinSphere( const MaterialInteraction materialSurface, const float3 n, float3 r, out Refraction ray) 
{
    float d = 0.0;
#if defined(MATERIAL_HAS_MICRO_THICKNESS)
    // note: we need the refracted ray to calculate the distance traveled
    // we could use materialSurface.NoV, but we would lose the dependency on ior.
    float3 rr = refract(r, n, materialSurface.etaIR);
    float NoR = dot(n, rr);
    d = materialSurface.uThickness / max(-NoR, 0.001);
    ray.position = float3(materialSurface.Position + rr * d);
#else
    ray.position = float3(materialSurface.Position);
#endif
    ray.direction = r;
    ray.d = d;
}

void applyRefraction( const MaterialInteraction materialSurface, const float3 n0, float3 E, float3 Fd, float3 Fr, inout float3 color, bool useLocalIBL, bool useDistantIBL) 
{
    Refraction ray;

#if REFRACTION_TYPE == REFRACTION_TYPE_SOLID
    refractionSolidSphere(materialSurface, n0, -materialSurface.View, ray);
#elif REFRACTION_TYPE == REFRACTION_TYPE_THIN
    refractionThinSphere(materialSurface, n0, -materialSurface.View, ray);
#else
#error "invalid REFRACTION_TYPE"
#endif

    /* compute transmission T */
#if defined(MATERIAL_HAS_ABSORPTION)
#if defined(MATERIAL_HAS_THICKNESS) || defined(MATERIAL_HAS_MICRO_THICKNESS)
    float3 T = min(float3(1.0), exp(-materialSurface.absorption * ray.d));
#else
    float3 T = 1.0 - materialSurface.absorption;
#endif
#endif

    float perceptualRoughness = materialSurface.perceptualRoughnessUnclamped;
#if REFRACTION_TYPE == REFRACTION_TYPE_THIN
    // Roughness remaping for thin layers, see Burley 2012, "Physically-Based Shading at Disney"
    perceptualRoughness = saturate((0.65 * materialSurface.etaRI - 0.35) * perceptualRoughness);

    // For thin surfaces, the light will bounce off at the second interface in the direction of
    // the reflection, effectively adding to the specular, but this process will repeat itself.
    // Each time the ray exits the surface on the front side after the first bounce,
    // it's multiplied by E^2, and we get: E + E(1-E)^2 + E^3(1-E)^2 + ...
    // This infinite serie converges and is easy to simplify.
    // Note: we calculate these bounces only on a single component,
    // since it's a fairly subtle effect.
    E *= 1.0 + materialSurface.transmission * (1.0 - E.g) / (1.0 + E.g);
#endif

    /* sample the cubemap or screen-space */
#if REFRACTION_MODE == REFRACTION_MODE_CUBEMAP
    // when reading from the cubemap, we are not pre-exposed so we apply iblLuminance
    // which is not the case when we'll read from the screen-space buffer
    float3 Ft = prefilteredRadiance( materialSurface, ray.direction, perceptualRoughness, bool useLocalIBL, bool useDistantIBL ) * frameUniforms.iblLuminance;
#else
    // compute the point where the ray exits the medium, if needed
    float4 p = float4(frameUniforms.clipFromWorldMatrix * float4(ray.position, 1.0));
    p.xy = uvToRenderTargetUV(p.xy * (0.5 / p.w) + 0.5);

    // perceptualRoughness to LOD
    // Empirical factor to compensate for the gaussian approximation of Dggx, chosen so
    // cubemap and screen-space modes match at perceptualRoughness 0.125
    float tweakedPerceptualRoughness = perceptualRoughness * 1.74;
    float lod = max(0.0, 2.0 * log2(tweakedPerceptualRoughness) + frameUniforms.refractionLodOffset);

    float3 Ft = textureLod(light_ssr, p.xy, lod).rgb;
#endif

    /* fresnel from the first interface */
    Ft *= 1.0 - E;

    /* apply absorption */
#if defined(MATERIAL_HAS_ABSORPTION)
    Ft *= T;
#endif

    Fr *= frameUniforms.iblLuminance;
    Fd *= frameUniforms.iblLuminance;
    color.rgb += Fr + lerp(Fd, Ft, materialSurface.transmission);
}
#endif

void combineDiffuseAndSpecular( const MaterialInteraction materialSurface, const float3 n, const float3 E, const float3 Fd, const float3 Fr, inout float3 color ) 
{
#if defined(HAS_REFRACTION)
    applyRefraction(materialSurface, n, E, Fd, Fr, color);
#else
    color += Fd + Fr;
#endif
}

void evaluateIBL( const MaterialInteraction materialSurface, inout float3 color, bool useLocalIBL, bool useDistantIBL )
{
    // Apply transform here if we wanted to rotate the IBL
    float3 normal       = materialSurface.Normal;
    float3 bentNormal   = materialSurface.BentNormal; //materialSurface.Normal;

    float diffuseAO = materialSurface.DiffuseAmbientOcclusion;
    float specularAO = computeSpecularAO( materialSurface, diffuseAO, materialSurface.Roughness );

    // specular layer
    float3 Fr;
#if IBL_INTEGRATION_ALGORITHM == IBL_INTEGRATION_PREFILTERED_CUBEMAP
    float3 E = specularDFG( materialSurface );
    float3 r = getReflectedVector( materialSurface, normal );
    Fr = E * prefilteredRadiance( materialSurface, r, materialSurface.PerceptualRoughness, useLocalIBL, useDistantIBL );
#elif IBL_INTEGRATION_ALGORITHM == IBL_INTEGRATION_IMPORTANCE_SAMPLING
    float3 E = float3(0.0.xxx); // TODO: fix for importance sampling
    Fr = isEvaluateIBL( materialSurface, normal, materialSurface.View, materialSurface.NoV, useLocalIBL, useDistantIBL );
#endif
    Fr *= singleBounceAO(specularAO) * materialSurface.EnergyCompensation;

    // diffuse layer
    float diffuseBRDF = singleBounceAO( diffuseAO ); // Fd_Lambert() is baked in the SH below
    evaluateClothIndirectDiffuseBRDF( materialSurface, diffuseBRDF );

    float3 diffuseIrradiance = DiffuseIrradiance( materialSurface, normalize(normal+bentNormal), useLocalIBL, useDistantIBL );
    float3 Fd = materialSurface.DiffuseColor * diffuseIrradiance * (1.0 - E) * diffuseBRDF;

    // clear coat layer
    evaluateClearCoatIBL( materialSurface, specularAO, Fd, Fr, useLocalIBL, useDistantIBL );

    // subsurface layer
    evaluateSubsurfaceIBL( materialSurface, diffuseIrradiance, Fd, Fr, useLocalIBL, useDistantIBL );

    // extra ambient occlusion term
    multiBounceAO(diffuseAO, materialSurface.DiffuseColor, Fd);
    multiBounceSpecularAO(specularAO, materialSurface.F0, Fr);

    // Note: iblLuminance is already premultiplied by the exposure
    combineDiffuseAndSpecular(materialSurface, normal, E, Fd, Fr, color);
}

#endif // VA_LIGHT_INDIRECT_INCLUDED