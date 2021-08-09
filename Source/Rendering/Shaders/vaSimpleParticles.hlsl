///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaShared.hlsl"

// #define VA_MATERIAL_BASIC_DISABLE_NEAR_PERPENDICULAR_TO_EYE_NORMAL_FILTERING

#if 0

#include "vaMaterialBasic.hlsl"

// must be the same as in vaSimpleParticles.cpp used for vertex shader!
struct GenericBillboardSpriteVertex
{
    float4 Position_CreationID  : SV_Position;
    float4 Color                : COLOR;
    float4 Transform2D          : TEXCOORD0;
};

Texture2D           g_viewspaceDepth              : register( T_CONCATENATER( SIMPLE_PARTICLES_VIEWSPACE_DEPTH ) );

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vertex/geometry shading below
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// pass-through vertex shader provided externally (vaSimpleParticles.cpp)

void FillRemainingOutputs( inout ShadedVertex spriteVert, float4 color, float2 spriteUV )
{
    spriteVert.Color            = color;
    spriteVert.WorldspaceNormal = normalize( -spriteVert.WorldspacePos );
    spriteVert.Texcoord01.xy    = spriteUV;
    spriteVert.Texcoord01.zw    = ((spriteVert.Position.xy / spriteVert.Position.w + float2( 1.0, -1.0) ) * float2( 0.5, -0.5 ) );// * g_globals.ViewportPixelSize.xy;
    spriteVert.ObjectspacePos   = input[i].ObjectspacePos;
}

[maxvertexcount(8)]
void SimpleParticleGS( point GenericBillboardSpriteVertex inputArr[1],  inout TriangleStream<ShadedVertex> triStream, in uint primitiveID : SV_PrimitiveID )
{
    GenericBillboardSpriteVertex input = inputArr[0];

    ShaderInstanceConstants instance = LoadInstanceConstants( g_instanceIndex.InstanceIndex );

    float3 worldspacePos    = mul( instance.World, float4( input.Position_CreationID.xyz, 1) ).xyz;

    // this doesn't work for raytracing
    const float viewDepth   = dot( worldspacePos.xyz - g_globals.CameraWorldPosition.xyz - g_globals.WorldBase.xyz, g_globals.CameraDirection.xyz );

    // this clipping is handy for performance reasons but a gradual fadeout when getting closer to clipspace is probably a good idea
    if( viewDepth < 0.0 )
        return;

    // uint uintCreationID  = asuint(input.Position_CreationID.w);
    // uint tempInt = uintCreationID ^ ( ( uintCreationID ^ 0x85ebca6b ) >> 13 ) * 0xc2b2ae3;
    // float perInstanceNoise = frac( tempInt / 4096.0 );

    // float3 viewDir      = normalize( viewspacePos );

    float3 dirRight     = float3( 1.0f, 0.0f, 0.0f );
    float3 dirBottom    = float3( 0.0f, 1.0f, 0.0f );

    float2x2 rotScale   = float2x2( input.Transform2D.x, input.Transform2D.y, input.Transform2D.z, input.Transform2D.w );

    // rotate & scale in viewspace
    dirRight.xy         = mul( rotScale, dirRight.xy );
    dirBottom.xy        = mul( rotScale, dirBottom.xy );

    // convert to worldspace
    dirRight            = mul( (float3x3)g_globals.ViewInv, dirRight );
    dirBottom           = mul( (float3x3)g_globals.ViewInv, dirBottom );

    ShadedVertex spriteTris[4];

    float size = length( dirRight + dirBottom );

    spriteTris[0].WorldspacePos  = float4( worldspacePos.xyz - dirRight - dirBottom, size );
    spriteTris[1].WorldspacePos  = float4( worldspacePos.xyz + dirRight - dirBottom, size );
    spriteTris[2].WorldspacePos  = float4( worldspacePos.xyz - dirRight + dirBottom, size );
    spriteTris[3].WorldspacePos  = float4( worldspacePos.xyz + dirRight + dirBottom, size );
 
    spriteTris[0].Position      = mul( g_globals.ViewProj, float4( spriteTris[0].WorldspacePos.xyz, 1.0 ) );
    spriteTris[1].Position      = mul( g_globals.ViewProj, float4( spriteTris[1].WorldspacePos.xyz, 1.0 ) );
    spriteTris[2].Position      = mul( g_globals.ViewProj, float4( spriteTris[2].WorldspacePos.xyz, 1.0 ) );
    spriteTris[3].Position      = mul( g_globals.ViewProj, float4( spriteTris[3].WorldspacePos.xyz, 1.0 ) );

    // do all the subsequent shading math with the WorldBase for precision purposes
    spriteTris[0].WorldspacePos.xyz -= g_globals.WorldBase.xyz;
    spriteTris[1].WorldspacePos.xyz -= g_globals.WorldBase.xyz;
    spriteTris[2].WorldspacePos.xyz -= g_globals.WorldBase.xyz;
    spriteTris[3].WorldspacePos.xyz -= g_globals.WorldBase.xyz;

    // ************************************************************************************************************************************
    // we can do culling here - just create bounding box from above positions.xyz/positions.w and see if it intersect clipping cube
    // ************************************************************************************************************************************

    FillRemainingOutputs( spriteTris[0], input.Color, float2( 0.0, 0.0 ) );
    FillRemainingOutputs( spriteTris[1], input.Color, float2( 1.0, 0.0 ) );
    FillRemainingOutputs( spriteTris[2], input.Color, float2( 0.0, 1.0 ) );
    FillRemainingOutputs( spriteTris[3], input.Color, float2( 1.0, 1.0 ) );

    for( int i = 0; i < 4; i++ )
        triStream.Append( spriteTris[i] );

    triStream.RestartStrip( );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pixel shading section below
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ParticleMaterialAccumulateLights( const in float3 worldspacePos, const in SurfaceValues material, const in float particleThickness, inout float3 diffuseAccum, inout float3 reflectedAccum )
{
    const float3 viewDir        = -normalize( g_globals.CameraWorldPosition.xyz - worldspacePos.xyz ); // normalized vector from the pixel to the eye (actually from eye to the pixel but this code is going to get cleaned anyway so ignore it)
    const float3 materialNormal = material.CotangentFrame[2];

    // Only apply lights above this brightness - TODO: should be based on current exposure/tonemapping settings!
    const float minViableLightThreshold = 1e-3f;

    uint i;
    float diffTerm, specTerm;

    float2 cubeJitterRots[3];
    float cubeJitterLocations[3];
    {
        float angle = material.Noise * (3.1415 * 2.0); //g_globals.TimeFmod3600;
        float scale = g_lighting.CubeShadowDirJitterOffset;
        // 3 taps so 120° angle
        float sa120 = 0.86602540378443864676372317075294;
        float ca120 = -0.5;
        cubeJitterRots[0] = float2( cos( angle ), sin( angle ) );
        cubeJitterRots[1] = float2( cubeJitterRots[0].x * ca120 - cubeJitterRots[0].y * sa120, cubeJitterRots[0].x * sa120 + cubeJitterRots[0].y * ca120 );
        cubeJitterRots[2] = float2( cubeJitterRots[1].x * ca120 - cubeJitterRots[1].y * sa120, cubeJitterRots[1].x * sa120 + cubeJitterRots[1].y * ca120 );
        cubeJitterRots[0] *= scale * 0.75;  // with different scaling we get corkscrew-like sampling points
        cubeJitterRots[1] *= scale * 1.25;  // with different scaling we get corkscrew-like sampling points
        cubeJitterRots[2] *= scale * 2.5;   // with different scaling we get corkscrew-like sampling points
        cubeJitterLocations[0] = (material.Noise);
        cubeJitterLocations[1] = frac(material.Noise+0.33);
        cubeJitterLocations[2] = frac(material.Noise+0.67);
    }

    // directional lights
    [loop]
    for( i = 0; i < g_lighting.LightCountDirectional; i++ )
    {
        ShaderLightDirectional light = g_lighting.LightsDirectional[i];

        diffTerm = saturate( dot( materialNormal, -light.Direction ) );

//make absolutely sure this isn't used unless SubsurfaceScatterHack is defined, 'cause it's a bit horrible (it also prevent's the early-out branching below)
#ifdef VA_RM_INPUT_LOAD_SubsurfaceScatterHack
        diffTerm = lerp( max( diffTerm, 0 ), 1, material.SubsurfaceScatterHack );
#endif

        [branch]    // early out, facing away from the light - both optimization and correctness but there should be a smoother transition to reduce aliasing
        if( diffTerm > 0 )
        {
#if VA_RM_ADVANCED_SPECULAR_SHADER
            ComputeGGXSpecularBRDF( viewDir, material.CotangentFrame, -light.Direction, material.Roughness, specTerm );
#else
            ComputePhongSpecularBRDF( viewDir, materialNormal, -light.Direction, material.SpecularPow, specTerm );
#endif
            diffuseAccum    += diffTerm * (light.Intensity * light.Color);
            reflectedAccum  += specTerm * (light.Intensity * light.Color);
        }
    }

    // point & spot lights combined
    [loop]
    for( i = 0; i < g_lighting.LightCountSpotAndPoint; i++ )
    {
        ShaderLightPoint light = g_lighting.LightsSpotAndPoint[i];

        float3 pixelToLight = light.Position - worldspacePos;
        float pixelToLightLength = length( pixelToLight );

        if( pixelToLightLength > light.Range )
            continue;

        pixelToLight /= pixelToLightLength;

        diffTerm = saturate( dot( materialNormal, pixelToLight ) );

//make absolutely sure this isn't used unless SubsurfaceScatterHack is defined, 'cause it's a bit horrible (it also prevent's the early-out branching below)
#ifdef VA_RM_INPUT_LOAD_SubsurfaceScatterHack
        diffTerm = lerp( max( diffTerm, 0 ), 1, material.SubsurfaceScatterHack );
#endif

#if VA_RM_SPECIAL_EMISSIVE_LIGHT
        // only add emissive within light sphere, and then scale with light itself; this is to allow emissive materials to be 'controlled' by
        // the light - useful for models that represent light emitters (lamps, etc.)
        if( pixelToLightLength < light.Size )
            diffuseAccum  += material.Emissive.rgb * (light.Intensity * light.Color);
#endif

        // // debugging shadows
        // if( light.CubeShadowIndex >= 0 )
        // {
        //     // do shadow map stuff
        //     float3 cubeShadowDir = -normalize( mul( (float3x3)g_globals.ViewInv, pixelToLight ) );
        //     float value = g_CubeShadowmapArray.Sample( g_samplerPointClamp, float4( cubeShadowDir, light.CubeShadowIndex ) ).x;
        //     // return float4( GradientHeatMap( frac(length(viewspacePos)) ), 1.0 );
        //     // return float4( GradientHeatMap( frac(pixelToLightLength) ), 1.0 );
        //     return float4( GradientHeatMap( frac(value) ), 1.0 );
        // }

        [branch]    // early out, facing away from the light
        if( diffTerm > 0 )
        {
            // const float earlyOutAttenuationThreshold = minViableLightThreshold / light.IntensityLength;

            float attenuationSqrt = max( light.Size, pixelToLightLength );
            float attenuation = 1.0 / (attenuationSqrt*attenuationSqrt);

            // spot light attenuation (if spot light) - yeah these could be 2 loops but that would be messy
            //[branch]
            //if( i < g_lighting.LightCountSpotOnly )
            //{ 
                float angle = acos( dot( light.Direction, -pixelToLight ) );
                float spotAttenuation = saturate( (light.SpotOuterAngle - angle) / (light.SpotOuterAngle - light.SpotInnerAngle) );
        
                // squaring of spot attenuation is just for a softer outer->inner curve that I like more visually
                attenuation *= spotAttenuation*spotAttenuation;
            //}
            
#if VA_RM_ACCEPTSHADOWS
            [branch]
            if( light.CubeShadowIndex >= 0 )
            {
                //[branch]
                //if( attenuation > earlyOutAttenuationThreshold )
                //{
                    // do shadow map stuff
                    float3 cubeShadowDir = -pixelToLight; //-normalize( mul( (float3x3)g_globals.ViewInv, pixelToLight ) );

                    float3 pixelToLight2 = light.Position - worldspacePos - viewDir * particleThickness;
                    float3 cubeShadowDir2 = -pixelToLight2; //-normalize( mul( (float3x3)g_globals.ViewInv, pixelToLight2 ) );

                    float distToLightCompVal = sqrt(pixelToLightLength / light.Range);
                #if 0   // manual compare
                    float shadowmapCompVal = g_CubeShadowmapArray.Sample( g_samplerPointClamp, float4( cubeShadowDir, light.CubeShadowIndex ) ).x;

                    if( shadowmapCompVal < distToLightCompVal )
                        attenuation *= 0;
                #elif 0 // use cmp sampler, 1-tap
                    attenuation *= g_CubeShadowmapArray.SampleCmp( g_samplerLinearCmpSampler, float4( cubeShadowDir, light.CubeShadowIndex ), distToLightCompVal ).x;
                #else   // use multi-tap with cmp sampler
                    float3 shadows;

                    float3 jitterX, jitterY;
                    ComputeOrthonormalBasis( cubeShadowDir, jitterX, jitterY );
                    
                    [unroll]
                    for( uint jittI = 0; jittI < 3; jittI++ )
                    {
                        float3 jitterOffset = jitterX * cubeJitterRots[jittI].x + jitterY * cubeJitterRots[jittI].y;
#if 0
                        float3 cubeShadowDirJ = cubeShadowDir;
#else
                        float3 cubeShadowDirJ = lerp( cubeShadowDir, cubeShadowDir2, cubeJitterLocations[jittI] );
#endif
                        shadows[jittI] = g_CubeShadowmapArray.SampleCmp( g_samplerLinearCmpSampler, float4( normalize(cubeShadowDirJ + jitterOffset), light.CubeShadowIndex ), distToLightCompVal ).x;
                    }
                    #if 0 // blend in sqrt space trick
                        shadows.xyz = sqrt( shadows.xyz );
                        float shadow = dot( shadows.xyz, 1.0 / 3.0 );
                        shadow = (shadow * shadow);
                        attenuation *= shadow;
                    #else
                        attenuation *= dot( shadows.xyz, 1.0 / 3.0 );
                    #endif
                #endif
                //}
            }
#endif

            //[branch]
            //if( attenuation > earlyOutAttenuationThreshold )
            {

#if VA_RM_ADVANCED_SPECULAR_SHADER
                ComputeGGXSpecularBRDF( viewDir, material.CotangentFrame, pixelToLight, material.Roughness, specTerm );
#else
                ComputePhongSpecularBRDF( viewDir, materialNormal, pixelToLight, material.SpecularPow, specTerm );
#endif

                diffuseAccum    += (attenuation * diffTerm) * (light.Intensity * light.Color);
                reflectedAccum  += (attenuation * specTerm) * (light.Intensity * light.Color);
            }
        }
    }

#if VA_RM_ADVANCED_SPECULAR_SHADER
    reflectedAccum *= 0.05;   // ugly hack to make 'advanced specular' roughly match Phong in intensity
    //return float4( frac( material.Roughness ).xxx, 1 );
#endif
}

float3 ParticleMaterialFinalizeLight( float3 worldspacePos, SurfaceValues material, float3 diffuseAccum )
{
    const bool transparent = true;

    // add albedo color to diffusely reflected light
    diffuseAccum = diffuseAccum * material.Albedo;

    // start calculating final colour
    float3 lightAccum       = 0;

    lightAccum  += material.Albedo.rgb * g_lighting.AmbientLightIntensity.rgb;
    lightAccum  += diffuseAccum;

    // these are not diminished by alpha so "un-alpha" them here (should probably use premult alpha blending mode and multiply above by alpha instead)
    float reflectedFactor = 1.0;
    if( transparent )
        reflectedFactor /= max( 0.001, material.Opacity );
    lightAccum  += material.Emissive.rgb * reflectedFactor * g_globals.PreExposureMultiplier;

    lightAccum  = max( 0, lightAccum );

    return lightAccum;
}

float4 ParticleColor( const ShadedVertex interpolants, SurfaceValues material, const float particleThickness )
{
    float3 diffuseAccum     = 0.0f;
    float3 reflectedAccum   = 0.0f;

    ParticleMaterialAccumulateLights( interpolants.WorldspacePos.xyz, material, particleThickness, diffuseAccum, reflectedAccum );

    // not needed for particles
    // BasicMaterialAccumulateReflections( interpolants.worldspacePos.xyz, material, reflectedAccum );

#if VA_RM_SPECIAL_EMISSIVE_LIGHT
    material.Emissive.rgb = 0;
#endif

    float3 lightAccum = ParticleMaterialFinalizeLight( interpolants.WorldspacePos.xyz, material, diffuseAccum );

    lightAccum  = LightingApplyFog( interpolants.WorldspacePos.xyz, lightAccum );

    return float4( lightAccum, material.Opacity );
}

// inverseTransmittance is Alpha
void CalcParticleVolumeParams( const in ShadedVertex interpolants, const in float viewspaceDepth, const float inAlpha, const float translucencyMultiplier, out float oneMinusTransmittance, out float outParticleThickness )
{
    const float pixViewDepth   = dot( interpolants.WorldspacePos.xyz - g_globals.CameraWorldPosition.xyz, g_globals.CameraDirection.xyz );

    float particleSize      = interpolants.WorldspacePos.w;

    float alphaCombined     = saturate( inAlpha );
    
//#define SIMPLE_PARTICLES_FANCY_VOLUME_COMPUTE
#ifdef SIMPLE_PARTICLES_FANCY_VOLUME_COMPUTE
    // For translucent media with constant absorption we can use Beer's Law to approximate translucence " T(d) = exp( -k * d ) " where d is depth travelled, and k is absorption function.
    // In this function we do this in reverse, having to calculate approximate thickness from the particle max thicknes (particleSize) and transucence given by alpha (obtained, 
    // for example, from an artist-generated texture).
    //
    // The absorptionConstant controls the approximation of the thickness (the smaller the absorptionConstant is, the more correct it becomes, and the larger it is, 
    // the more linear is the relationship to interpolants alpha)
    // use "plot -log(1-x*(1-e^(-1/z))) * z, x = 0.0 to 1.0, y = 0.0 to 1.0, z = 0.3" to plot it in wolfram alpha
    const float absorptionConstant              = 1.0 / 0.6; // the lower the constant, the more logarithmy-like curve is
    const float maxAlphaForRelativeThicknessOne = 1.0 - exp( -1.0 * absorptionConstant );

    float relativeThickness     = saturate( -log( 1.0 - alphaCombined * maxAlphaForRelativeThicknessOne ) * absorptionConstant );
#else
    float relativeThickness     = alphaCombined;
#endif

#ifdef SIMPLE_PARTICLES_VOLUMETRIC_DEPTH
    relativeThickness   = saturate( min( relativeThickness, 
        min( 
                pixViewDepth - particleSize * 0.5,       // this is to fade against the near clip plane (assumed to be at 0 heh) - 0.5 is a fudge to not fade that quickly
                (viewspaceDepth - pixViewDepth)          // this is to fade against the depth
        ) / (particleSize*1.5) ) );                                     // convert to [0...1] relativeThickness format; the *1.5 is a fudge to make it fade out even sooner
    
    // have not measured any perf benefit                                                                    
    // if( relativeThickness <= 0.0 )
    //     discard;
#endif
#ifdef SIMPLE_PARTICLES_FANCY_VOLUME_COMPUTE
    float transmittance         = 1 - ( 1 - exp( -relativeThickness / absorptionConstant ) ) / maxAlphaForRelativeThicknessOne;
#else
    float transmittance         = 1 - relativeThickness;
#endif

    oneMinusTransmittance       = 1.0 - pow( transmittance, translucencyMultiplier );
    outParticleThickness        = relativeThickness * particleSize;
}



float4 SimpleParticlePS( const in ShadedVertex interpolants, const bool isFrontFace : SV_IsFrontFace ) : SV_Target
{
    const float pixViewDepth   = dot( interpolants.WorldspacePos.xyz - g_globals.CameraWorldPosition.xyz, g_globals.CameraDirection.xyz );

    // this no longer works with VRS as we're using half depth buffer...
    //float viewspaceDepth = g_viewspaceDepth.Load( int3( interpolants.Position.xy, 0 ) ).x;
    // ...but the uv-based approach works!
    float viewspaceDepth = g_viewspaceDepth.Sample( g_samplerPointClamp, interpolants.Texcoord01.zw ).x;

   if( saturate(viewspaceDepth - pixViewDepth) + g_globals.WireframePass == 0 )
       discard;

    SurfaceValues material = FillBasicMaterialValues( LoadInstanceConstants( g_instanceIndex.InstanceIndex ), interpolants, isFrontFace, true );

    float outAlpha, particleThickness;

    CalcParticleVolumeParams( interpolants, viewspaceDepth, material.Opacity, 1.0, outAlpha, particleThickness );
    material.Opacity = outAlpha;

#ifdef SIMPLE_PARTICLES_NO_LIGHTING
    // hacky
    float3 colorOut = material.Albedo;
    colorOut *= g_globals.PreExposureMultiplier;
    colorOut.rgb = colorOut.rgb * instance.EmissiveAdd.a + instance.EmissiveAdd.rgb;
    return float4( colorOut, material.Opacity );
#else
    float particleSize  = interpolants.WorldspacePos.w;
    particleThickness   += particleSize * 0.4; // a fudge, adds more noise / better spread to shadows if used, otherwise no difference
    float4 finalColor = ParticleColor( interpolants, material, particleThickness );
    
    // this adds UI highlights and other similar stuff
    finalColor.rgb = finalColor.rgb * instance.EmissiveAdd.a + instance.EmissiveAdd.rgb;
    
    // finalColor.r = interpolants.Texcoord01.z; //interpolants.Position.x / 1600.0;
    // finalColor.g = interpolants.Texcoord01.w; //interpolants.Position.y / 900.0;
    // finalColor.b = 0;
    // finalColor.a = 1;

    return finalColor;
#endif
}

#endif