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

vec3 decodeDataForIBL(const vec4 data) {
    return data.rgb;
}

//------------------------------------------------------------------------------
// IBL prefiltered DFG term implementations
//------------------------------------------------------------------------------

vec3 PrefilteredDFG_LUT(float lod, float NoV) {
    // coord = sqrt(linear_roughness), which is the mapping used by cmgen.
    return g_DFGLookupTable.SampleLevel( g_samplerLinearClamp, float2(NoV, 1.0-lod), 0.0 ).rgb;
}

//------------------------------------------------------------------------------
// IBL environment BRDF dispatch
//------------------------------------------------------------------------------

vec3 prefilteredDFG(float perceptualRoughness, float NoV) {
    // PrefilteredDFG_LUT() takes a LOD, which is sqrt(roughness) = perceptualRoughness
    return PrefilteredDFG_LUT(perceptualRoughness, NoV);
}

//------------------------------------------------------------------------------
// IBL irradiance implementations
//------------------------------------------------------------------------------

vec3 Irradiance_SphericalHarmonics(const vec3 n, vaVector4 DiffuseSH[9] ) 
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

vec3 DiffuseIrradiance( const ShadingParams shading, vec3 n, bool useLocalIBL, bool useDistantIBL ) 
{
    float3 local = 0, distant = 0;

    // no longer needed; n and v are in worldspace and IBL cube is in worldspace too
    // n = mul( (float3x3)g_DistantIBL.WorldToIBLRotation, n );                // this should go out in the future

#if IBL_IRRADIANCE_SOURCE == IBL_IRRADIANCE_SH
    [branch] if( shading.IBL.UseLocal && useLocalIBL )
        local   = g_lighting.LocalIBL.PreExposedLuminance * Irradiance_SphericalHarmonics( ComputeIBLDirection( shading.Position, g_lighting.LocalIBL, n ), g_lighting.LocalIBL.DiffuseSH );
    [branch] if( shading.IBL.UseDistant && useDistantIBL )
        distant = g_lighting.DistantIBL.PreExposedLuminance * Irradiance_SphericalHarmonics( ComputeIBLDirection( shading.Position, g_lighting.DistantIBL, n ), g_lighting.DistantIBL.DiffuseSH );
#elif IBL_IRRADIANCE_SOURCE == IBL_IRRADIANCE_CUBEMAP
    #ifdef VA_RAYTRACING
        // not a huge loss sampling only MIP0 since these are very low res anyhow
        [branch] if( shading.IBL.UseLocal && useLocalIBL )
            local   = g_lighting.LocalIBL.PreExposedLuminance * g_LocalIBLIrradianceMap.SampleLevel( g_samplerLinearClamp, ComputeIBLDirection( shading.Position, g_lighting.LocalIBL, n ), 0 ).rgb;
        [branch] if( shading.IBL.UseDistant && useDistantIBL )
            distant = g_lighting.DistantIBL.PreExposedLuminance * g_DistantIBLIrradianceMap.SampleLevel( g_samplerLinearClamp, ComputeIBLDirection( shading.Position, g_lighting.DistantIBL, n ), 0 ).rgb;
    #else // ifdef VA_RAYTRACING
        [branch] if( shading.IBL.UseLocal && useLocalIBL )
            local   = g_lighting.LocalIBL.PreExposedLuminance * g_LocalIBLIrradianceMap.Sample( g_samplerLinearClamp, ComputeIBLDirection( shading.Position, g_lighting.LocalIBL, n ) ).rgb;
        [branch] if( shading.IBL.UseDistant && useDistantIBL )
            distant = g_lighting.DistantIBL.PreExposedLuminance * g_DistantIBLIrradianceMap.Sample( g_samplerLinearClamp, ComputeIBLDirection( shading.Position, g_lighting.DistantIBL, n ) ).rgb;
    #endif
#else
#error IBL_IRRADIANCE_SOURCE not correctly defined / supported
#endif
    
    return lerp( local, distant, shading.IBL.LocalToDistantK );
}


//------------------------------------------------------------------------------
// IBL specular
//------------------------------------------------------------------------------

vec3 prefilteredRadiance( const ShadingParams shading, vec3 r, float perceptualRoughness, bool useLocalIBL, bool useDistantIBL ) 
{
    // lod = lod_count * sqrt(roughness), which is the mapping used by cmgen
    // where roughness = perceptualRoughness^2
    // using all the mip levels requires seamless cubemap sampling
   
    // no longer needed; n and v are in worldspace and IBL cube is in worldspace too
    //r = normalize( mul( (float3x3)g_DistantIBL.WorldToIBLRotation, r ) );    // this should go out in the future

    float3 local = 0, distant = 0;

    [branch] if( shading.IBL.UseLocal && useLocalIBL )
    {
        float lod = min( g_lighting.LocalIBL.MaxReflMipLevel.x * perceptualRoughness, g_lighting.LocalIBL.ReflMipLevelClamp );
        local   = g_lighting.LocalIBL.PreExposedLuminance * decodeDataForIBL( g_LocalIBLReflectionsMap.SampleLevel( g_samplerLinearClamp, ComputeIBLDirection( shading.Position, g_lighting.LocalIBL, r ), lod ) );
    }
    [branch] if( shading.IBL.UseDistant && useDistantIBL )
    {
        float lod = min( g_lighting.DistantIBL.MaxReflMipLevel.x * perceptualRoughness, g_lighting.DistantIBL.ReflMipLevelClamp );
        distant = g_lighting.DistantIBL.PreExposedLuminance * decodeDataForIBL( g_DistantIBLReflectionsMap.SampleLevel( g_samplerLinearClamp, ComputeIBLDirection( shading.Position, g_lighting.DistantIBL, r ), lod ) );
    }

    return lerp( local, distant, shading.IBL.LocalToDistantK );
}

vec3 getSpecularDominantDirection(const vec3 n, const vec3 r, float roughness) {
    return mix(r, n, roughness * roughness);
}

vec3 specularDFG( const PixelParams pixel ) {
#if defined(SHADING_MODEL_CLOTH)
    return pixel.F0 * pixel.DFG.z;
#elif defined( SHADING_MODEL_SPECULAR_GLOSSINESS )
    return pixel.F0; // this seems to match better what the spec-gloss looks like in general
#else
    return mix(pixel.DFG.xxx, pixel.DFG.yyy, pixel.F0);
#endif
}

/**
 * Returns the reflected vector at the current shading point. The reflected vector
 * return by this function might be different from shading_reflected:
 * - For anisotropic material, we bend the reflection vector to simulate
 *   anisotropic indirect lighting
 * - The reflected vector may be modified to point towards the dominant specular
 *   direction to match reference renderings when the roughness increases
 */

vec3 getReflectedVector(const PixelParams pixel, const vec3 v, const vec3 n) {
#if defined(MATERIAL_HAS_ANISOTROPY)
    vec3  anisotropyDirection = pixel.Anisotropy >= 0.0 ? pixel.AnisotropicB : pixel.AnisotropicT;
    vec3  anisotropicTangent  = cross(anisotropyDirection, v);
    vec3  anisotropicNormal   = cross(anisotropicTangent, anisotropyDirection);
    float bendFactor          = abs(pixel.Anisotropy) * saturate(5.0 * pixel.PerceptualRoughness);
    vec3  bentNormal          = normalize(mix(n, anisotropicNormal, bendFactor));

    vec3 r = reflect(-v, bentNormal);
#else
    vec3 r = reflect(-v, n);
#endif
    return r;
}

vec3 getReflectedVector( const ShadingParams shading, const PixelParams pixel, const vec3 n ) {
#if defined(MATERIAL_HAS_ANISOTROPY)
    vec3 r = getReflectedVector( pixel, shading.View, n );
#else
    vec3 r = shading.Reflected;
#endif
    return getSpecularDominantDirection( n, r, pixel.Roughness );
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

vec3 importanceSamplingNdfDggx(vec2 u, float roughness) {
    // Importance sampling D_GGX
    float a2 = roughness * roughness;
    float phi = 2.0 * PI * u.x;
    float cosTheta2 = (1.0 - u.y) / (1.0 + (a2 - 1.0) * u.y);
    float cosTheta = sqrt(cosTheta2);
    float sinTheta = sqrt(1.0 - cosTheta2);
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

vec3 importanceSamplingVNdfDggx(vec2 u, float roughness, vec3 v) {
    // See: "A Simpler and Exact Sampling Routine for the GGX Distribution of Visible Normals", Eric Heitz
    float alpha = roughness;

    // stretch view
    v = normalize(vec3(alpha * v.x, alpha * v.y, v.z));

    // orthonormal basis
    vec3 up = abs(v.z) < 0.9999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, v));
    vec3 b = cross(t, v);

    // sample point with polar coordinates (r, phi)
    float a = 1.0 / (1.0 + v.z);
    float r = sqrt(u.x);
    float phi = (u.y < a) ? u.y / a * PI : PI + (u.y - a) / (1.0 - a) * PI;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi) * ((u.y < a) ? 1.0 : v.z);

    // compute normal
    vec3 h = p1 * t + p2 * b + sqrt(max(0.0, 1.0 - p1*p1 - p2*p2)) * v;

    // unstretch
    h = normalize(vec3(alpha * h.x, alpha * h.y, max(0.0, h.z)));
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

vec3 isEvaluateIBL( const PixelParams pixel, vec3 n, vec3 v, float NoV, bool useLocalIBL, bool useDistantIBL )
{
    if( !useDistantIBL )
        return vec3(0,0,0);
    // no longer needed; n and v are in worldspace and IBL cube is in worldspace too
    // // viewspace to worldspace
    // n = mul( (float3x3)g_DistantIBL.WorldToIBLRotation, n );
    // v = mul( (float3x3)g_DistantIBL.WorldToIBLRotation, v );

    // TODO: for a true anisotropic BRDF, we need a real tangent space
    vec3 up = abs(n.z) < 0.9999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);

    float3x3 tangentToWorld;
    tangentToWorld[0] = normalize(cross(up, n));
    tangentToWorld[1] = cross(n, tangentToWorld[0]);
    tangentToWorld[2] = n;

    float roughness = pixel.Roughness;
    float a2 = roughness * roughness;

    float2 iblMaxMipLevel = float2( g_DistantIBL.MaxReflMipLevel, g_DistantIBL.Pow2MaxReflMipLevel );
    const uint numSamples = uint(IBL_INTEGRATION_IMPORTANCE_SAMPLING_COUNT);
    const float invNumSamples = 1.0 / float(numSamples);

    float3 indirectSpecular = 0.0.xxx;
    for (uint i = 0u; i < numSamples; i++)
    {
        vec2 u = hammersley(i);
        vec3 h = mul( importanceSamplingNdfDggx(u, roughness), tangentToWorld );

        // Since anisotropy doesn't work with prefiltering, we use the same "faux" anisotropy
        // we do when we use the prefiltered cubemap
        vec3 l = getReflectedVector(pixel, v, h);

        // Compute this sample's contribution to the brdf
        float NoL = dot(n, l);
        if (NoL > 0.0) {
            float NoH = dot(n, h);
            float LoH = max(dot(l, h), 0.0);

            // PDF inverse (we must use D_GGX() here, which is used to generate samples)
            float ipdf = (4.0 * LoH) / (D_GGX(roughness, NoH, h) * NoH);

            float mipLevel = prefilteredImportanceSampling(ipdf, iblMaxMipLevel);

            // we use texture() instead of textureLod() to take advantage of mipmapping
            vec3 L = decodeDataForIBL( g_DistantIBLReflectionsMap.SampleLevel( g_samplerLinearClamp, l, min( mipLevel, g_DistantIBL.ReflMipLevelClamp ) ) );

            float D = distribution(roughness, NoH, h);
            float V = visibility(roughness, NoV, NoL);
            vec3  F = fresnel(pixel.F0, LoH);
            vec3 Fr = F * (D * V * NoL * ipdf * invNumSamples);

            indirectSpecular += (Fr * L);
        }
    }

    return indirectSpecular;
}

void isEvaluateClearCoatIBL(const PixelParams pixel, float specularAO, inout vec3 Fd, inout vec3 Fr, bool useLocalIBL, bool useDistantIBL) {
#if defined(MATERIAL_HAS_CLEAR_COAT)
#if defined(MATERIAL_HAS_NORMAL) || defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
    // We want to use the geometric normal for the clear coat layer
    float clearCoatNoV = clampNoV(dot(shading_clearCoatNormal, shading_view));
    vec3 clearCoatNormal = shading_clearCoatNormal;
#else
    float clearCoatNoV = shading_NoV;
    vec3 clearCoatNormal = shading_normal;
#endif
    // The clear coat layer assumes an IOR of 1.5 (4% reflectance)
    float Fc = F_Schlick(0.04, 1.0, clearCoatNoV) * pixel.clearCoat;
    float attenuation = 1.0 - Fc;
    Fd *= attenuation;
    Fr *= attenuation;

    PixelParams p;
    p.perceptualRoughness = pixel.clearCoatPerceptualRoughness;
    p.f0 = vec3(0.04);
    p.roughness = perceptualRoughnessToRoughness(p.perceptualRoughness);
    p.anisotropy = 0.0;

    vec3 clearCoatLobe = isEvaluateIBL(p, clearCoatNormal, shading_view, clearCoatNoV, useLocalIBL, useDistantIBL);
    Fr += clearCoatLobe * (specularAO * pixel.clearCoat);
#endif
}
#endif

//------------------------------------------------------------------------------
// IBL evaluation
//------------------------------------------------------------------------------

void evaluateClothIndirectDiffuseBRDF( const ShadingParams shading, const PixelParams pixel, inout float diffuse) {
#if defined(SHADING_MODEL_CLOTH)
#if defined(MATERIAL_HAS_SUBSURFACE_COLOR)
    // Simulate subsurface scattering with a wrap diffuse term
    diffuse *= Fd_Wrap(shading.NoV, 0.5);
#endif
#endif
}

void evaluateClearCoatIBL( const ShadingParams shading, const PixelParams pixel, float specularAO, inout vec3 Fd, inout vec3 Fr, bool useLocalIBL, bool useDistantIBL ) {
#if IBL_INTEGRATION_ALGORITHM == IBL_INTEGRATION_IMPORTANCE_SAMPLING
    isEvaluateClearCoatIBL(pixel, specularAO, Fd, Fr, useLocalIBL, useDistantIBL);
    return;
#endif

#if defined(MATERIAL_HAS_CLEAR_COAT)
#if defined(MATERIAL_HAS_NORMAL) || defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
    // We want to use the geometric normal for the clear coat layer
    float clearCoatNoV = clampNoV(dot(shading_clearCoatNormal, shading_view));
    vec3 clearCoatR = reflect(-shading_view, shading_clearCoatNormal);
#else
    float clearCoatNoV = shading_NoV;
    vec3 clearCoatR = shading_reflected;
#endif
    // The clear coat layer assumes an IOR of 1.5 (4% reflectance)
    float Fc = F_Schlick(0.04, 1.0, clearCoatNoV) * pixel.clearCoat;
    float attenuation = 1.0 - Fc;
    Fd *= attenuation;
    Fr *= attenuation;
    Fr += prefilteredRadiance( shading, clearCoatR, pixel.clearCoatPerceptualRoughness, useLocalIBL, useDistantIBL ) * (specularAO * Fc);
#endif
}

void evaluateSubsurfaceIBL( const ShadingParams shading, const PixelParams pixel, const vec3 diffuseIrradiance, inout vec3 Fd, inout vec3 Fr, bool useLocalIBL, bool useDistantIBL )
{
#if defined(SHADING_MODEL_SUBSURFACE)
    vec3 viewIndependent = diffuseIrradiance;
    vec3 viewDependent = prefilteredRadiance( shading, -shading.View, pixel.Roughness, 1.0 + pixel.Thickness, useLocalIBL, useDistantIBL );
    float attenuation = (1.0 - pixel.Thickness) / (2.0 * PI);
    Fd += pixel.SubsurfaceColor * (viewIndependent + viewDependent) * attenuation;
#elif defined(SHADING_MODEL_CLOTH) && defined(MATERIAL_HAS_SUBSURFACE_COLOR)
    Fd *= saturate(pixel.SubsurfaceColor + shading.NoV);
#endif
}

#if defined(HAS_REFRACTION)

struct Refraction {
    vec3 position;
    vec3 direction;
    float d;
};

void refractionSolidSphere(const PixelParams pixel,
    const vec3 n, vec3 r, out Refraction ray) {
    r = refract(r, n, pixel.etaIR);
    float NoR = dot(n, r);
    float d = pixel.thickness * -NoR;
    ray.position = vec3(shading_position + r * d);
    ray.d = d;
    vec3 n1 = normalize(NoR * r - n * 0.5);
    ray.direction = refract(r, n1,  pixel.etaRI);
}

void refractionSolidBox(const PixelParams pixel,
    const vec3 n, vec3 r, out Refraction ray) {
    vec3 rr = refract(r, n, pixel.etaIR);
    float NoR = dot(n, rr);
    float d = pixel.thickness / max(-NoR, 0.001);
    ray.position = vec3(shading_position + rr * d);
    ray.direction = r;
    ray.d = d;
#if REFRACTION_MODE == REFRACTION_MODE_CUBEMAP
    // fudge direction vector, so we see the offset due to the thickness of the object
    float envDistance = 10.0; // this should come from a ubo
    ray.direction = normalize((ray.position - shading_position) + ray.direction * envDistance);
#endif
}

void refractionThinSphere(const PixelParams pixel,
    const vec3 n, vec3 r, out Refraction ray) {
    float d = 0.0;
#if defined(MATERIAL_HAS_MICRO_THICKNESS)
    // note: we need the refracted ray to calculate the distance traveled
    // we could use shading_NoV, but we would lose the dependency on ior.
    vec3 rr = refract(r, n, pixel.etaIR);
    float NoR = dot(n, rr);
    d = pixel.uThickness / max(-NoR, 0.001);
    ray.position = vec3(shading_position + rr * d);
#else
    ray.position = vec3(shading_position);
#endif
    ray.direction = r;
    ray.d = d;
}

void applyRefraction(const PixelParams pixel,
    const vec3 n0, vec3 E, vec3 Fd, vec3 Fr,
    inout vec3 color, bool useLocalIBL, bool useDistantIBL) {

    Refraction ray;

#if REFRACTION_TYPE == REFRACTION_TYPE_SOLID
    refractionSolidSphere(pixel, n0, -shading_view, ray);
#elif REFRACTION_TYPE == REFRACTION_TYPE_THIN
    refractionThinSphere(pixel, n0, -shading_view, ray);
#else
#error "invalid REFRACTION_TYPE"
#endif

    /* compute transmission T */
#if defined(MATERIAL_HAS_ABSORPTION)
#if defined(MATERIAL_HAS_THICKNESS) || defined(MATERIAL_HAS_MICRO_THICKNESS)
    vec3 T = min(vec3(1.0), exp(-pixel.absorption * ray.d));
#else
    vec3 T = 1.0 - pixel.absorption;
#endif
#endif

    float perceptualRoughness = pixel.perceptualRoughnessUnclamped;
#if REFRACTION_TYPE == REFRACTION_TYPE_THIN
    // Roughness remaping for thin layers, see Burley 2012, "Physically-Based Shading at Disney"
    perceptualRoughness = saturate((0.65 * pixel.etaRI - 0.35) * perceptualRoughness);

    // For thin surfaces, the light will bounce off at the second interface in the direction of
    // the reflection, effectively adding to the specular, but this process will repeat itself.
    // Each time the ray exits the surface on the front side after the first bounce,
    // it's multiplied by E^2, and we get: E + E(1-E)^2 + E^3(1-E)^2 + ...
    // This infinite serie converges and is easy to simplify.
    // Note: we calculate these bounces only on a single component,
    // since it's a fairly subtle effect.
    E *= 1.0 + pixel.transmission * (1.0 - E.g) / (1.0 + E.g);
#endif

    /* sample the cubemap or screen-space */
#if REFRACTION_MODE == REFRACTION_MODE_CUBEMAP
    // when reading from the cubemap, we are not pre-exposed so we apply iblLuminance
    // which is not the case when we'll read from the screen-space buffer
    vec3 Ft = prefilteredRadiance( shading, ray.direction, perceptualRoughness, bool useLocalIBL, bool useDistantIBL ) * frameUniforms.iblLuminance;
#else
    // compute the point where the ray exits the medium, if needed
    vec4 p = vec4(frameUniforms.clipFromWorldMatrix * vec4(ray.position, 1.0));
    p.xy = uvToRenderTargetUV(p.xy * (0.5 / p.w) + 0.5);

    // perceptualRoughness to LOD
    // Empirical factor to compensate for the gaussian approximation of Dggx, chosen so
    // cubemap and screen-space modes match at perceptualRoughness 0.125
    float tweakedPerceptualRoughness = perceptualRoughness * 1.74;
    float lod = max(0.0, 2.0 * log2(tweakedPerceptualRoughness) + frameUniforms.refractionLodOffset);

    vec3 Ft = textureLod(light_ssr, p.xy, lod).rgb;
#endif

    /* fresnel from the first interface */
    Ft *= 1.0 - E;

    /* apply absorption */
#if defined(MATERIAL_HAS_ABSORPTION)
    Ft *= T;
#endif

    Fr *= frameUniforms.iblLuminance;
    Fd *= frameUniforms.iblLuminance;
    color.rgb += Fr + mix(Fd, Ft, pixel.transmission);
}
#endif

void combineDiffuseAndSpecular(const PixelParams pixel,
        const vec3 n, const vec3 E, const vec3 Fd, const vec3 Fr,
        inout float3 color ) {
#if defined(HAS_REFRACTION)
    applyRefraction(pixel, n, E, Fd, Fr, color);
#else
    color += Fd + Fr;
#endif
}

void evaluateIBL( const ShadingParams shading, const MaterialInputs material, const PixelParams pixel, inout float3 color, bool useLocalIBL, bool useDistantIBL )
{
    // Apply transform here if we wanted to rotate the IBL
    vec3 n = shading.Normal;

    float diffuseAO = shading.DiffuseAmbientOcclusion;
    float specularAO = computeSpecularAO( shading.NoV, diffuseAO, pixel.Roughness );

    // temp hack - I don't like that it gets all black <shrug>
    specularAO = saturate(specularAO) * 0.97 + 0.03;

    // specular layer
    vec3 Fr;
#if IBL_INTEGRATION_ALGORITHM == IBL_INTEGRATION_PREFILTERED_CUBEMAP
    vec3 E = specularDFG( pixel );
    vec3 r = getReflectedVector( shading, pixel, n );
    Fr = E * prefilteredRadiance( shading, r, pixel.PerceptualRoughness, useLocalIBL, useDistantIBL );
#elif IBL_INTEGRATION_ALGORITHM == IBL_INTEGRATION_IMPORTANCE_SAMPLING
    vec3 E = vec3(0.0.xxx); // TODO: fix for importance sampling
    Fr = isEvaluateIBL( pixel, shading.Normal, shading.View, shading.NoV, useLocalIBL, useDistantIBL );
#endif
    Fr *= singleBounceAO(specularAO) * pixel.EnergyCompensation;

    // diffuse layer
    float diffuseBRDF = singleBounceAO( diffuseAO ); // Fd_Lambert() is baked in the SH below
    evaluateClothIndirectDiffuseBRDF( shading, pixel, diffuseBRDF );

    vec3 diffuseIrradiance = DiffuseIrradiance( shading, n, useLocalIBL, useDistantIBL );
    vec3 Fd = pixel.DiffuseColor * diffuseIrradiance * (1.0 - E) * diffuseBRDF;

    // clear coat layer
    evaluateClearCoatIBL( shading, pixel, specularAO, Fd, Fr, useLocalIBL, useDistantIBL );

    // subsurface layer
    evaluateSubsurfaceIBL( shading, pixel, diffuseIrradiance, Fd, Fr, useLocalIBL, useDistantIBL );

    // extra ambient occlusion term
    multiBounceAO(diffuseAO, pixel.DiffuseColor, Fd);
    multiBounceSpecularAO(specularAO, pixel.F0, Fr);

    // Note: iblLuminance is already premultiplied by the exposure
    combineDiffuseAndSpecular(pixel, n, E, Fd, Fr, color);
}
