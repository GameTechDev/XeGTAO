//------------------------------------------------------------------------------
// Directional light evaluation
//------------------------------------------------------------------------------

#if !defined(TARGET_MOBILE)
#define SUN_AS_AREA_LIGHT
#endif

float3 sampleSunAreaLight( const ShadingParams shading, vec3 lightDirection, const float4 sunAreaLightParams ) 
{
//#if defined(SUN_AS_AREA_LIGHT)
    if (sunAreaLightParams.w < 0.0) 
        return lightDirection;
    else
    {
        // simulate sun as disc area light
        float LoR = dot(lightDirection, shading.Reflected);
        float d = sunAreaLightParams.x;
        highp vec3 s = shading.Reflected - LoR * lightDirection;
        return LoR < d ?
                normalize(lightDirection * d + normalize(s) * sunAreaLightParams.y) : shading.Reflected;
                //                                                               ^ hmmm is this correct?
    }
}
/*
LightParams getDirectionalLight( const ShadingParams shading, const ShaderLightDirectional lightIn ) 
{
    LightParams light;
    // note: lightColorIntensity.w is always premultiplied by the exposure
    light.ColorIntensity    = float4( lightIn.Color, lightIn.Intensity );
    light.L                 = sampleSunAreaLight( shading, -lightIn.Direction, lightIn.SunAreaLightParams );
    light.Attenuation       = 1.0;
    light.NoL               = saturate( dot( shading.Normal, light.L ) );

    return light;
}
*/

/*
void evaluateDirectionalLights( const ShadingParams shading, const MaterialInputs material, const PixelParams pixel, inout float3 diffuseColor, inout float3 specularColor) 
{
    [loop]
    for( uint i = 0; i < g_lighting.LightCountDirectional; i++ )
    {
        ShaderLightDirectional lightIn = g_lighting.LightsDirectional[i];
    
        LightParams light = getDirectionalLight( shading, lightIn );

#if defined(MATERIAL_CAN_SKIP_LIGHTING)
        if( light.NoL <= 0.0 ) 
            break;
#endif

        float visibility = 1.0;
        if( light.NoL > 0.0 ) 
        {
//#if defined(HAS_SHADOWING)
//        #error shadows for directional light not supported yet
//            visibility *= shadow(light_shadowMap, getLightSpacePosition());
//#endif
            // maybe not the best place to apply this
            visibility *= computeMicroShadowing( light.NoL, material.AmbientOcclusion );
        }
        surfaceShading( shading, pixel, light, visibility, diffuseColor, specularColor );
    }
}
*/