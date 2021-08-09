

// This provides functionality of Filament 'computeShadingParams' and 'prepareMaterial' in Vanilla; see shading_parameters.fs for 
// original code & comments

ShadingParams ComputeShadingParams( const SurfaceInteraction surface, const MaterialInputs material, float ssao )
{
    ShadingParams shading;

    // all these are just copies from surface, but keep them in for consistency with Filament
    shading.Position        = surface.WorldspacePos;
    shading.GeometricNormal = surface.WorldspaceNormal;
    shading.TangentToWorld  = surface.TangentToWorld( );
    shading.View            = surface.View;   // normalized vector from the fragment to the eye

//#if defined(HAS_ATTRIBUTE_TANGENTS)
#if defined(MATERIAL_HAS_NORMAL)
    shading.Normal          = mul( material.Normal, shading.TangentToWorld ); // this used to have 'normalize()' but it's unnecessary as inputs are supposed to be normalized
#else
    shading.Normal          = shading.GetWorldGeometricNormalVector();
#endif
    
#if 1   // just don't allow normal to be facing away but this is likely too hacky and related to https://blogs.unity3d.com/2017/10/02/microfacet-based-normal-mapping-for-robust-monte-carlo-path-tracing/ - something to fix int he future
    shading.NoV             = dot( shading.Normal, shading.View );
    shading.Normal          = normalize( shading.Normal + max( 0, -shading.NoV + MIN_N_DOT_V ) * shading.View );
    shading.NoV             = max( MIN_N_DOT_V, shading.NoV );
#else
    shading.NoV             = clampNoV( dot( shading.Normal, shading.View ) );
#endif

    shading.Reflected       = reflect( -shading.View, shading.Normal );

#if defined(MATERIAL_HAS_CLEAR_COAT)
#if defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
    shading.ClearCoatNormal = normalize( mul( material.ClearCoatNormal, shading.TangentToWorld ) ); //normalize(shading_tangentToWorld * material.clearCoatNormal);
#else
    shading.ClearCoatNormal = shading.GetWorldGeometricNormalVector();
#endif
#error do the same as above with clearcoat normal - prevent it to be facing away from the view vector
#endif
//#endif

#if defined(MATERIAL_HAS_EMISSIVE)
    // the way filament does it:
    // float emissiveIntensity = pow( 2.0, g_globals.EV100 + material.EmissiveIntensity - 3.0) * g_globals.PreExposureMultiplier;
    
    // vanilla does it a bit simpler - also clamp values below 0 and don't do anything in case of VA_RM_SPECIAL_EMISSIVE_LIGHT
    // <removed pre-exposure multiplier from here so it doesn't apply itself twice for the special emissive lights where it gets multiplied by the light intensity that was premultiplied already>
    float emissiveIntensity = max( 0, material.EmissiveIntensity ); // * g_globals.PreExposureMultiplier; // pow( 2.0, g_globals.EV100 + material.EmissiveIntensity - 3.0) * g_globals.PreExposureMultiplier;
    shading.PrecomputedEmissive = material.EmissiveColor * material.EmissiveIntensity.xxx;
#endif

#if VA_RM_ACCEPTSHADOWS
    shading.CubeShadows     = ComputeCubeShadowsParams( surface.ObjectspaceNoise, surface.WorldspacePos.xyz );
#endif

    shading.IBL             = ComputeIBLParams( surface.ObjectspaceNoise, surface.WorldspacePos.xyz, shading.GeometricNormal, material.VA_RM_LOCALIBL_NORMALBIAS, material.VA_RM_LOCALIBL_BIAS );

#if !VA_RM_TRANSPARENT || VA_RM_DECAL   // only sample SSAO if we're not transparent, unless we're a decal (in which case we lay on opaque surface by definition, so SSAO is correct)
    shading.DiffuseAmbientOcclusion = min( material.AmbientOcclusion, ssao );
#else
    shading.DiffuseAmbientOcclusion = material.AmbientOcclusion;
#endif

    // // if we wanted to update the TangentToWorld (cotangent frame) with the normal loaded from normalmap, we can do this:
    // shading.TangentToWorld[2] = shading.Normal;
    // ReOrthogonalizeFrame( cotangentFrame, false );

    return shading;
}
