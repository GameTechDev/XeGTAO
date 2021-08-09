struct ShadingParams
{
    highp float3x3  TangentToWorld;     // TBN matrix   <- COPIED FROM SurfaceInteraction
    highp vec3      Position;           // position of the fragment in world space - same as vertex.WorldspacePos

    float3      ObjectPosition;     // object space position of the fragment in world space

    float3      View;               // normalized vector from the fragment to the eye
    float3      Normal;             // normalized transformed normal, in world space (if normalmap exists, this is a perturbed normal)
    float3      GeometricNormal;    // normalized geometric normal, in world space
    float3      Reflected;          // reflection of view about normal
    float       NoV;                // dot(normal, view), always strictly >= MIN_N_DOT_V

#if defined(MATERIAL_HAS_BENT_NORMAL)
    float3  	BentNormal;       	// normalized transformed normal, in world space
#endif

#if defined(MATERIAL_HAS_CLEAR_COAT)
    float3      ClearCoatNormal;    // normalized clear coat layer normal, in world space
#endif

//	highp float2 NormalizedViewportCoord;

#if defined(MATERIAL_HAS_EMISSIVE)
    float3      PrecomputedEmissive;
#endif

#if VA_RM_ACCEPTSHADOWS
    CubeShadowsParams
                CubeShadows;
#endif

    IBLParams   IBL;

    float       DiffuseAmbientOcclusion;        // min( ssao and material.AmbientOcclusion)

    float3      GetWorldGeometricNormalVector( ) { return GeometricNormal; }
};