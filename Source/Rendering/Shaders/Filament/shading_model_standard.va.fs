#if defined(MATERIAL_HAS_CLEAR_COAT)
float clearCoatLobe( const ShadingParams shading, const PixelParams pixel, const vec3 h, float NoH, float LoH, out float Fcc ) {

#if defined(MATERIAL_HAS_NORMAL) || defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
    // If the material has a normal map, we want to use the geometric normal
    // instead to avoid applying the normal map details to the clear coat layer
    float clearCoatNoH = saturate(dot(shading.ClearCoatNormal, h));
#else
    float clearCoatNoH = NoH;
#endif

    // clear coat specular lobe
    float D = distributionClearCoat(pixel.ClearCoatRoughness, clearCoatNoH, h);
    float V = visibilityClearCoat(LoH);
    float F = F_Schlick(0.04, 1.0, LoH) * pixel.ClearCoat; // fix IOR to 1.5

    Fcc = F;
    return D * V * F;
}
#endif

#if defined(MATERIAL_HAS_ANISOTROPY)
vec3 anisotropicLobe(const float3 Wo, const PixelParams pixel, const float3 lightDir, const vec3 h,
        float NoV, float NoL, float NoH, float LoH) {

    vec3 l = lightDir;
    vec3 t = pixel.AnisotropicT;
    vec3 b = pixel.AnisotropicB;
    vec3 v = Wo; // Wo - a.k.a. shading.View 

    float ToV = dot(t, v);
    float BoV = dot(b, v);
    float ToL = dot(t, l);
    float BoL = dot(b, l);
    float ToH = dot(t, h);
    float BoH = dot(b, h);

    // Anisotropic parameters: at and ab are the roughness along the tangent and bitangent
    // to simplify materials, we derive them from a single roughness parameter
    // Kulla 2017, "Revisiting Physically Based Shading at Imageworks"
    float at = max(pixel.Roughness * (1.0 + pixel.Anisotropy), MIN_ROUGHNESS);
    float ab = max(pixel.Roughness * (1.0 - pixel.Anisotropy), MIN_ROUGHNESS);

    // specular anisotropic BRDF
    float D = distributionAnisotropic(at, ab, ToH, BoH, NoH);
    float V = visibilityAnisotropic(pixel.Roughness, at, ab, ToV, BoV, ToL, BoL, NoV, NoL);
    vec3  F = fresnel(pixel.F0, LoH);

    return (D * V) * F;
}
#endif

vec3 isotropicLobe(const PixelParams pixel, const float3 lightDir, const vec3 h,
        float NoV, float NoL, float NoH, float LoH) {

    float D = distribution(pixel.Roughness, NoH, h);
    float V = visibility(pixel.Roughness, NoV, NoL);
    vec3  F = fresnel(pixel.F0, LoH);

    return (D * V) * F;
}

// Wo - a.k.a. shading.View 
vec3 specularLobe( const float3 Wo, const PixelParams pixel, const float3 lightDir, const vec3 h, float NoV, float NoL, float NoH, float LoH) 
{
#if defined(MATERIAL_HAS_ANISOTROPY)
    return anisotropicLobe(Wo, pixel, lightDir, h, NoV, NoL, NoH, LoH);
#else
    return isotropicLobe(pixel, lightDir, h, NoV, NoL, NoH, LoH);
#endif
}

vec3 diffuseLobe(const PixelParams pixel, float NoV, float NoL, float LoH) {
    return pixel.DiffuseColor * diffuse(pixel.Roughness, NoV, NoL, LoH);
}

/**
 * Evaluates lit materials with the standard shading model. This model comprises
 * of 2 BRDFs: an optional clear coat BRDF, and a regular surface BRDF.
 *
 * Surface BRDF
 * The surface BRDF uses a diffuse lobe and a specular lobe to render both
 * dielectrics and conductors. The specular lobe is based on the Cook-Torrance
 * micro-facet model (see brdf.fs for more details). In addition, the specular
 * can be either isotropic or anisotropic.
 *
 * Clear coat BRDF
 * The clear coat BRDF simulates a transparent, absorbing dielectric layer on
 * top of the surface. Its IOR is set to 1.5 (polyutherane) to simplify
 * our computations. This BRDF only contains a specular lobe and while based
 * on the Cook-Torrance microfacet model, it uses cheaper terms than the surface
 * BRDF's specular lobe (see brdf.fs).
 */
void surfaceShading( const ShadingParams shading, const PixelParams pixel, const float3 Wi, inout float3 inoutColor ) 
{
    vec3 h = normalize( shading.View + Wi );    // shading.View a.k.a. Wo

    float NoV = shading.NoV;
    float NoL = saturate( dot( shading.Normal, Wi ) );
    float NoH = saturate( dot( shading.Normal, h ) );
    float LoH = saturate( dot( Wi, h) );

    vec3 Fr = specularLobe( shading.View, pixel, Wi, h, NoV, NoL, NoH, LoH );
    vec3 Fd = diffuseLobe( pixel, NoV, NoL, LoH );
    Fr *= pixel.EnergyCompensation;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    // coment below is from original Filament implementation:
    // TODO: attenuate the diffuse lobe to avoid energy gain
    //
    // FS: Since we're monte carlo integrating over the lobe, I've attempted to compensate for the light that
    // doesn't come to play with the diffuse by reducing the incident light by the (dielectric only!) fresnel
    // effect. It feel wrong though (ignores roughness to begin with) but I don't have time & knowhow to dig
    // through further, so I'll leave some useful references for when I get back to it in the future:
    // * pbrt on the topic: http://www.pbr-book.org/3ed-2018/Reflection_Models/Fresnel_Incidence_Effects.html#
    // * there's a short note on "Moving Frostbite to PBR": https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf 
    // * above Frostbite's [Jen+01] is a reference to this (page 3): https://graphics.stanford.edu/papers/bssrdf/bssrdf.pdf
    {
        float f90 = saturate( pixel.DielectricF0 * 50.0 );     // same as "fresnel(pixel.DielectricF0, LoH).x", since pixel.DielectricF0 is scalar (grayscale)
        float frD = F_Schlick( pixel.DielectricF0, f90, LoH ); // same as "fresnel(pixel.DielectricF0, LoH).x", since pixel.DielectricF0 is scalar (grayscale)
        Fd *= 1-frD;
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(HAS_REFRACTION)
    #error not implemented
    Fd *= (1.0 - pixel.Transmission);
#endif

#if defined(MATERIAL_HAS_CLEAR_COAT)
    float Fcc;
    float clearCoat = clearCoatLobe(pixel, h, NoH, LoH, Fcc);
    float attenuation = 1.0 - Fcc;

#if defined(MATERIAL_HAS_NORMAL) || defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
    vec3 color = (Fd + Fr) * attenuation * NoL;

    // If the material has a normal map, we want to use the geometric normal
    // instead to avoid applying the normal map details to the clear coat layer
    float clearCoatNoL = saturate(dot(shading.ClearCoatNormal, light.L));
    color += clearCoat * clearCoatNoL;

    // Early exit to avoid the extra multiplication by NoL
    inoutColor += color;
    return;
#else
    float3 color    = (Fd + Fr) * attenuation + clearCoat;
#endif
#else
    // The energy compensation term is used to counteract the darkening effect
    // at high roughness
    float3 color    = Fd + Fr;
#endif

    inoutColor += color * NoL;
}

void surfaceShading( const ShadingParams shading, const PixelParams pixel, const LightParams light, float occlusion, inout float3 inoutColor ) 
{
    float3 color = 0;
    surfaceShading( shading, pixel, light.L, color );
    inoutColor += color * light.ColorIntensity.rgb * (light.ColorIntensity.w * light.Attenuation * occlusion);
}

#if defined(VA_RAYTRACING) 

//#define BSDFSAMPLE_F_VIZ_DEBUG

// This is a mix of pbrt's BSDFSample_f and 'surfaceShading' (above); unlike pbrt, coordinate system used here is 'world'
// Wi - sampled incident direction
// Multiple importance sampling might need a separate BxDFSample_pdf with given Wi just to get the pdf
// 'u' - 2D quasi-random samples; 'r' - 1D quasi-random sample
BSDFSample BSDFSample_f( const SurfaceInteraction surface, const ShadingParams shading, const PixelParams pixel, const float r, const float2 u ) 
{
    float3 Wi;
    float pdf;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    // importance sampling
    //
    // we're doing everything here in tangent space
    float3x3 shadingTangentToWorld = shading.TangentToWorld;
#if 1 // use (or not) texture normal tangent space
    shadingTangentToWorld[2] = shading.Normal;
    ReOrthogonalizeFrame( shadingTangentToWorld, false );
#endif
    float3 Wo = mul( shadingTangentToWorld, shading.View ); // a.k.a. Ve
    // We assume the Wo view vector is never coming 'from below' of the shading.Normal so z>0 in the shading tangent frame.
    // Otherwise, we have a problem.
    //
// roughness mess - will require additional work to adjust the tangent space (shadingTangentToWorld) with aniso rotation
#if defined(MATERIAL_HAS_ANISOTROPY)
    #error not implemented yet
#endif
    //
    // various (importance) sampling methods go here
#if 0   // << CLEANUP THIS << uniform hemisphere reference - use this as a reference for testing (will take ages to accumulate though and will have self-intersections - TODO: fix that same as below)
    Wi = SampleHemisphereUniform( u, pdf );
#elif 0 // << CLEANUP THIS << VNDF distribution
    Wi = SampleGGXVNDF( Wo, pixel.Roughness, pixel.Roughness, u, pdf );
#else   // mixed cosine weighted and VNDF: smaller - more suitable for specular; bigger - more suitable for diffuse
    
    // mixed 50%-50% cosine weighted and VNDF 
    // const float pc = 0.5;

    // much more powerful adaptive approach; TODO: read up on http://www.pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Reflection_Functions.html#FresnelBlend
    const float pc = saturate(sqrt(1.0-pixel.ReflectivityEstimate)); // this is adaptive to material and much more powerful

    float dummy;
    if( r < pc )
        Wi = SampleHemisphereCosineWeighted( u );
    else
    {
        Wi = SampleGGXVNDF( Wo, pixel.RoughnessUnclamped, pixel.RoughnessUnclamped, u, dummy ); // <- RoughnessUnclamped used only for picking the direction, not for computing pdf; hack, not sure how much if any bias it adds
    }

// prevent rays from going under the physical geometry triangle normal to avoid self-intersections
#if 1
    // TODO: read https://blogs.unity3d.com/2017/10/02/microfacet-based-normal-mapping-for-robust-monte-carlo-path-tracing/ and see
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
    float3 Tn = mul( shadingTangentToWorld, surface.TriangleNormal ); // we need the triangle normal in shading tangent space
    Wi = normalize( Wi + (max( 0, -dot(Wi, Tn) ) * ( 0.6 + Rand1D(r)*0.8 ) ) * Tn ); // push the Wi 'out' in the triangle normal direction if required, dithered
#endif

    float pdf1 = SampleHemisphereCosineWeightedPDF( Wi.z );
    float pdf2 = SampleGGXVNDF_PDF( Wo, Wi, pixel.Roughness, pixel.Roughness );
    pdf1 = max( 1e-16, pdf1 );  // things go haywire with too low pdfs and this doesn't seem to cause measurable energy loss, but there's probably a better standard way to deal with it
    pdf2 = max( 1e-16, pdf2 );  // things go haywire with too low pdfs and this doesn't seem to cause measurable energy loss, but there's probably a better standard way to deal with it
    pdf = lerp( pdf2, pdf1, pc );   // <- is this really correct? I struggle.

    // debug view of distribution choices
    //[branch] if( IsUnderCursorRange( (int2)surface.Position.xy, int2(1,1) ) )
    //    DebugDraw3DText( surface.WorldspacePos.xyz, float2( -10, 76 ), float4( 0.5, 0.5, 1 ,1 ), float4( pc, r, pdf1, pdf2 ) );

#endif

#ifdef BSDFSAMPLE_F_VIZ_DEBUG
    [branch] if( IsUnderCursorRange( (int2)surface.Position.xy, int2(1,1) ) )
    {
        DebugDraw3DText( surface.WorldspacePos.xyz, float2( -10, -48 ), float4(0.5,0.5,1,1), float4(Wo, 0) );
        DebugDraw3DText( surface.WorldspacePos.xyz, float2( -10, -34 ), float4(0.5,1,0.5,1), float4(Wi, pixel.Roughness) );
    }
#endif
    // go back to world space!
    Wi = normalize( mul( Wi, shadingTangentToWorld ) );
    // Wo = normalize( mul( Wo, shadingTangentToWorld ) ); // this is shading.View but with the adjustment so it's never coming under the surface!
    Wo = shading.View;
    //
    // debugging 
#ifdef BSDFSAMPLE_F_VIZ_DEBUG
    [branch] if( IsUnderCursorRange( (int2)surface.Position.xy, int2(1,1) ) )
    {
        float length = 0.3; float thickness = length * 0.01;
        surface.DebugDrawTangentSpace( length );
        DebugDraw3DArrow( surface.WorldspacePos, surface.WorldspacePos + Wi * length, thickness, float4( 1, 1, 1, 1 ) );
        DebugDraw3DCylinder( surface.RayOrigin( ), surface.WorldspacePos.xyz, thickness, thickness, float4(0.5,0.5,0,1) );
    }
#endif
    //////////////////////////////////////////////////////////////////////////////////////////////////////////

    BSDFSample ret;
    ret.F   = 0;
    ret.Wi  = Wi;
    ret.PDF = pdf;
    surfaceShading( shading, pixel, Wi, ret.F );
    
// //#if (VA_RM_SPECIAL_EMISSIVE_LIGHT != 0)
// //    ret.Specularness = 0.0;
// //#else
//     //float diffPart      = (Fd.r+Fd.g+Fd.b);
//     //float reflPart      = (Fr.r+Fr.g+Fr.b);
//     //ret.Specularness    = sq(saturate( reflPart / (diffPart+reflPart) ));
//     ret.Specularness    = pixel.ReflectivityEstimate;
//     ret.Specularness    *= saturate( pow(1-sqrt(pixel.Roughness-MIN_ROUGHNESS), 8) );
//     ret.Specularness    = saturate( ret.Specularness * 1.5 - 0.1 );
// //#endif

    //[branch] if( IsUnderCursorRange( (int2)surface.Position.xy, int2(1,1) ) )
    //{
    //    //DebugDraw3DText( surface.WorldspacePos.xyz, float2( -10, -74 ), float4( 1, 0.5, 0, 1 ), float4( diffPart, reflPart, ret.Specularness, 0 ) );
    //    //DebugDraw3DText( surface.WorldspacePos.xyz, float2( -10, -62 ), float4( 0, 1, 0, 1 ), float4( Fr*NoL/ret.PDF, ret.PDF ) );
    //    //DebugDraw3DText( surface.WorldspacePos.xyz, float2( -10, -48 ), float4( 0, 0.5, 1, 1 ), float4( Fd*NoL/ret.PDF, ret.PDF ) );
    //}

#ifdef BSDFSAMPLE_F_VIZ_DEBUG
    [branch] if( IsUnderCursorRange( (int2)surface.Position.xy, int2(1,1) ) )
    {
        DebugDraw3DText( surface.WorldspacePos.xyz, float2( -10, -62 ), float4(ret.F/ret.PDF,1), float4(ret.F, ret.PDF) );
        DebugDraw3DText( surface.WorldspacePos.xyz, float2( -10, -76 ), float4(ret.F/ret.PDF,1), float4(ret.F/ret.PDF, 0) );
    }
#endif

    return ret;
}

#endif // #if defined(VA_RAYTRACING) 