///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Based on:    https://github.com/google/filament
//
// Filament is the best opensource PBR renderer (that I could find), including excellent documentation and 
// performance. The size and complexity of the Filament library itself is a bit out of scope of what Vanilla is 
// intended to provide so it's not used directly but its PBR shaders and material  definitions are integrated into 
// Vanilla (mostly) as they are. Original Filament shaders are included into the repository but only some are 
// used directly - the rest are in as a reference for enabling easier integration of any future Filament changes.
//
// There are subtle differences in the implementation though, so beware.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaShaderCore.h"

#include "vaRenderingShared.hlsl"

#ifdef VA_RAYTRACING
#include "vaPathTracerShared.h"
#endif 

#include "vaLighting.hlsl"
#include "vaMaterialLoaders.hlsl"
#include "vaMaterialFilament.hlsl"

// this is shared for rasterized and raytraced paths so nice single place to do comparison
float4 DebugDisplay( const SurfaceInteraction surface, const ShaderInstanceConstants instanceConstants, const MaterialInputs material, const ShaderMeshConstants meshConstants, const ShadingParams shading, const PixelParams pixel )
{
    //    [branch] if( IsUnderCursor( surface.Position.xy ) )
    //        DebugDraw2DText( surface.Position.xy + float2( 0, 20 ), float4( 0.8, 0.8, 0.8, 1 ), float4( VA_RM_SHADER_ID, surface.Position.xy * g_globals.ViewportPixelSize.xy, 0 ) );

    // *** Debug surface! ***
    // return float4( surface.Position.xy * g_globals.ViewportPixelSize.xy, 0 , 1.0 );
    // return float4( frac( surface.Position.z * 1000.0 ), frac( surface.Position.w * 1000.0 ), 0, 1 );
    // return float4( surface.Color.rgb, 1 );
    // return float4( surface.Color.aaa, 1 );
    // return float4( frac( surface.WorldspacePos.xyz ), 1 );
    // return float4( frac( mul( g_globals.View, float4( surface.WorldspacePos.xyz, 1 ) ) ).zzz, 1 ); - viewspace depth
    // return float4( DisplayNormalSRGB( surface.WorldspaceNormal.xyz ), 1 );
    // return float4( DisplayNormalSRGB( surface.TriangleNormal ), 1 );
    // return float4( frac(surface.Texcoord01.xy), 0, 1 );
    // return float4( 10*surface.RayConeWidth, 5*surface.RayConeWidthProjected, surface.RayConeWidthProjected/surface.RayConeWidth*0.5-1, 1 );

    // return float4( DisplayNormalSRGB( material.Normal ), 1.0 ); // looks ok
    // return float4( DisplayNormalSRGB( shading.TangentToWorld[0] ), 1.0 ); // looks ok
    // return float4( DisplayNormalSRGB( shading.TangentToWorld[1] ), 1.0 ); // looks ok
    // return float4( DisplayNormalSRGB( shading.TangentToWorld[2] ), 1.0 ); // looks ok
    // [branch] if( IsUnderCursor( surface.Position.xy ) )
    //     DebugDraw3DText( surface.WorldspacePos.xyz, float2(0,-20), float4( 1, 0, 0, 1 ), float4( material.Normal, 0 ) );
    // return float4( DisplayNormalSRGB( shading.Normal ), 1.0 ); // looks ok
    // return float4( DisplayNormalSRGB( shading.Reflected ), 1.0 ); // I suppose it could be ok
    // return float4( pow( abs( shading.NoV ), 2.2 ).xxx * 0.1, 1.0 ); // I suppose it could be ok as well
    // return float4( surface.ObjectspaceNoise.xxx, 1.0 ); // looks ok
    // return float4( material.AmbientOcclusion.xxx, 1.0 ); // looks ok
    // return float4( shading.CombinedAmbientOcclusion.xxx, 1.0 ); // looks ok
    // return float4( shading.IBL.LocalToDistantK.xxx, 1 );

    // [branch] if( IsUnderCursor( surface.Position.xy ) )
    //     DebugDraw3DLine( surface.WorldspacePos.xyz, surface.WorldspacePos.xyz + shading.Normal, float4( 0, 1, 0, 1 ) );
    // [branch] if( IsUnderCursor( surface.Position.xy + float2(-1,0) ) )
    //     DebugDraw3DLine( surface.WorldspacePos.xyz, surface.WorldspacePos.xyz + shading.Normal, float4( 0, 0, 1, 1 ) );

    // return float4( pixel.DiffuseColor, 1.0 ); // looks ok
    // return float4( pixel.PerceptualRoughness.xxx, 1.0 ); // no idea if it's ok yet
    // return float4( pixel.F0, 1.0 ); // I guess it's cool?
    // return float4( pixel.Roughness.xxx, 1.0 ); // no idea if it's ok yet
    // return float4( pixel.DFG, 1.0 ); // no idea if it's ok yet
    // return float4( pixel.EnergyCompensation, 1.0 ); // no idea if it's ok yet

    return float4( 0, 0, 0, 0 );
}

#ifndef VA_RAYTRACING

// depth only pre-pass (also used for shadow maps)
void PS_DepthOnly( const in ShadedVertex inVertex, const in float4 position : SV_Position, const in uint isFrontFace : SV_IsFrontFace )
{
    SurfaceInteraction surface = SurfaceInteraction::Compute( inVertex, isFrontFace );
#if VA_RM_ALPHATEST
    const ShaderInstanceConstants instanceConstants = LoadInstanceConstants( g_instanceIndex.InstanceIndex );
    MaterialInputs material = LoadMaterial( instanceConstants, surface );
    if( AlphaDiscard( material ) )
        discard;
#endif
}

// a more complex depth pre-pass that also outputs normals
void PS_RichPrepass( const in ShadedVertex inVertex, const in float4 position : SV_Position, uint isFrontFace : SV_IsFrontFace, out uint outData0 : SV_Target0 )
{
    SurfaceInteraction surface = SurfaceInteraction::Compute( inVertex, isFrontFace );
#if VA_RM_ALPHATEST
    const ShaderInstanceConstants instanceConstants = LoadInstanceConstants( g_instanceIndex.InstanceIndex );
    MaterialInputs material = LoadMaterial( instanceConstants, surface );
    if( AlphaDiscard( material ) )
        discard;
#endif
    // get worldspace geometry normal and convert to viewspace because this is what we consume later
    const float3x3 tangentToWorld = surface.TangentToWorld( );
#if 1 // output normals in viewspace (otherwise worldspace)
    float3 outNormal = mul( (float3x3)g_globals.View, tangentToWorld[2] );
#else // or worldspace!
    float3 outNormal = tangentToWorld[2];
#endif
    outData0.x = Pack_R11G11B10_UNORM( saturate( outNormal.xyz * 0.5 + 0.5 ) );

#if 0
    [branch] if( IsUnderCursorRange( position.xy, int2(1,1) ) )
        surface.DebugDrawTangentSpace( 0.3 );
#endif
}

// standard forward render
[earlydepthstencil]
float4 PS_Forward( const in ShadedVertex inVertex, const in float4 position : SV_Position, uint isFrontFace : SV_IsFrontFace/*, float3 baryWeights : SV_Barycentrics*/ ) : SV_Target
{
    SurfaceInteraction surface = SurfaceInteraction::Compute( inVertex, isFrontFace );

    const ShaderInstanceConstants instanceConstants = LoadInstanceConstants( g_instanceIndex.InstanceIndex );

    MaterialInputs material = LoadMaterial( instanceConstants, surface );

    // if running with depth pre-pass zbuffer and 'equal' depth test, no need to alpha-discard here!
#if VA_RM_ALPHATEST && !defined(VA_NO_ALPHA_TEST_IN_MAIN_DRAW)
    if( AlphaDiscard( material ) )
        discard;
#endif

    ShaderMeshConstants meshConstants     = g_meshConstants[instanceConstants.MeshGlobalIndex];

    // make all albedo white
    // material.BaseColor.rgb = 1.xxx;

    // after alpha-test
    ReportCursorInfo( instanceConstants, (int2)position.xy, surface.WorldspacePos ); // provides info for mouse right click context menu!

    // material.Normal = float3( 0, 0, 1 );
    ShadingParams shading = ComputeShadingParams( surface, material, SampleAO( position.xy ) );

    PixelParams pixel = ComputePixelParams( surface, shading, material );

    float4 finalColor = EvaluateMaterialAndLighting( surface, shading, pixel, material );


    finalColor.rgb = LightingApplyFog( surface.WorldspacePos.xyz, finalColor.rgb );

    // this adds UI highlights and other similar stuff
    float4 emissiveAdd = Unpack_R10G10B10FLOAT_A2_UNORM( instanceConstants.EmissiveAddPacked );
    finalColor.rgb = finalColor.rgb * emissiveAdd.a + emissiveAdd.rgb;

    finalColor.rgb = lerp( finalColor.rgb, float3( 0, 0.5, 0 ), g_globals.WireframePass );

    float4 debugColor = DebugDisplay( surface, instanceConstants, material, meshConstants, shading, pixel );

    // [branch] if( IsUnderCursor( surface.Position.xy + float2( 1,0) ) )
    //     DebugDraw3DLine( surface.WorldspacePos.xyz, surface.WorldspacePos.xyz + shading.Normal, float4( 1, 0, 0, 1 ) );

//    #if VA_RM_TRANSPARENT
//    if( g_globals.AlphaTAAHackEnabled )
//    {
//        float depth = g_globalDepthTexture.Load( int3( position.xy, 0 ) ).x;
//        float depthL = g_globalDepthTexture.Load( int3( position.xy, 0 ), int2(-1,0) ).x;
//        float depthT = g_globalDepthTexture.Load( int3( position.xy, 0 ), int2(1,0) ).x;
//        float depthR = g_globalDepthTexture.Load( int3( position.xy, 0 ), int2(0,-1) ).x;
//        float depthB = g_globalDepthTexture.Load( int3( position.xy, 0 ), int2(0,1) ).x;
//        depth = max( depth, max( max( depthT, depthL ), max( depthR, depthB ) ) );
//        if( depth > (position.z) )
//            discard;
//    }
//    #endif

    return float4( HDRClamp( lerp( finalColor.rgb, debugColor.rgb, debugColor.a ) ), finalColor.a );
}

#else // #ifndef VA_RAYTRACING

[shader("anyhit")]
void AnyHitAlphaTest( inout ShaderRayPayloadBase rayPayload, BuiltInTriangleIntersectionAttributes attr )
{
#if VA_RM_ALPHATEST
    ShaderInstanceConstants     instanceConstants;
    ShaderMeshConstants         meshConstants;
    ShaderMaterialConstants     materialConstants;
    SurfaceInteraction          surface;

    LoadHitSurfaceInteraction( rayPayload.DispatchRaysIndex.xy, attr.barycentrics, InstanceIndex(), PrimitiveIndex(), WorldRayDirection(), RayTCurrent(), rayPayload.ConeSpreadAngle, rayPayload.ConeWidth, instanceConstants, meshConstants, materialConstants, surface );

    //callablePayload.Color = float4( frac( surface.WorldspacePos.xyz ), 1 );

//    if( all( frac( surface.WorldspacePos.xyz ) < 0.5 ) )
//        IgnoreHit( );

//    if( any( ((callablePayload.DispatchRaysIndex.xy + VA_RM_SHADER_ID) % 4) < 2 ) )
//        IgnoreHit( );

    MaterialInputs material = LoadMaterial( instanceConstants, surface );
    if( AlphaDiscard( material ) )
        IgnoreHit( );
#endif    
}

[shader("callable")]
void CallableExample( inout ShaderCallablePayload callablePayload )
{
}

#define ENABLE_DEBUG_DRAW_RAYS

[shader("closesthit")]
void ClosestHitPathTrace( inout ShaderPathTracerRayPayload rayPayload, in BuiltInTriangleIntersectionAttributes attr )
{
#if VA_RM_TRANSPARENT
    return; //IgnoreHit( ); // for now :) THIS IS ALSO DISABLED ON THE C++ side, look for 'instanceGlobal.Material->IsTransparent()' in vaSceneRaytracingDX12.cpp
#endif

    const uint sampleIndex = g_pathTracerConsts.SampleIndex();

    ShaderInstanceConstants     instanceConstants;
    ShaderMeshConstants         meshConstants;
    ShaderMaterialConstants     materialConstants;
    SurfaceInteraction          surface;

    LoadHitSurfaceInteraction( rayPayload.DispatchRaysIndex.xy, attr.barycentrics, InstanceIndex(), PrimitiveIndex(), WorldRayDirection(), RayTCurrent(), rayPayload.ConeSpreadAngle, rayPayload.ConeWidth, instanceConstants, meshConstants, materialConstants, surface );
    
    bool debugDrawRays              = false;
    bool debugDrawDirectLighting    = false;
    bool debugDrawRayDetails        = false;
#ifdef ENABLE_DEBUG_DRAW_RAYS
    debugDrawRays           = (rayPayload.Flags & VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_VIZ) != 0;
    debugDrawDirectLighting = (rayPayload.Flags & VA_RAYTRACING_FLAG_SHOW_DEBUG_LIGHT_VIZ) != 0;
    debugDrawRayDetails     = (rayPayload.Flags & VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_DETAIL_VIZ) != 0;
#endif
    if( debugDrawRays )
    {
        float4 rayDebugColor = float4( GradientRainbow( rayPayload.BounceCount / 6.0 ), 0.4 );
        DebugDraw3DCylinder( WorldRayOrigin( ), /*callablePayload.RayOrigin + callablePayload.RayDir + callablePayload.RayLength*/ surface.WorldspacePos.xyz, 
            rayPayload.ConeWidth * 0.5, surface.RayConeWidth * 0.5, rayDebugColor );
        DebugDraw3DSphere( surface.WorldspacePos.xyz, surface.RayConeWidth, float4( 0, 0, 0, 0.7 ) );
    }
    if( debugDrawRayDetails )
    {
        float4 rayDebugColor = float4( GradientRainbow( rayPayload.BounceCount / 6.0 ), 1 );
        surface.DebugDrawTangentSpace( sqrt(surface.ViewDistance) * 0.1 );
        //DebugDraw3DText( surface.WorldspacePos.xyz, float2( -10, -16 ), lerp( rayDebugColor, float4(1,1,1,0.4), 0.5 ), rayPayload.BounceCount );
    }

    const float3x3 tangentToWorld = surface.TangentToWorld( );

    MaterialInputs material = LoadMaterial( instanceConstants, surface );
    ShadingParams shading   = ComputeShadingParams( surface, material, 1 );
    PixelParams pixel       = ComputePixelParams( surface, shading, material );

    // "poor man's path regularization"
    if( (rayPayload.Flags & VA_RAYTRACING_FLAG_PATH_REGULARIZATION) != 0 )
    {
        // TODO: for a proper solution see 
        //  - paper: https://www2.in.tu-clausthal.de/~cgstore/publications/2019_Jendersie_brdfregularization.pdf
        //  - presentation: https://www2.in.tu-clausthal.de/~cgstore/publications/2019_Jendersie_brdfregularization_presentation.pdf
        // main points:
        //  * for a given "tau" (BSDF threshold) parameter, the upper bound BSDF "is basically proportional to 1 by pi alpha square" so the roughness should be max( roughness, sqrt(1/tau) )
        //  * if using MIS, weights must be corrected
        //  * Bias Reduction: Path Diffusion (pg 17+ of the talk): 'Modify tau with the tangential standard deviation at vertex k'
        //  * Bias Reduction: Sampler Quality (pg 19+ of the talk)
        const float bounceMod = 0.85;    // if < 1 it increases roughness with every bounce
        const float tau = 16.0;         // see above paper on what 'tau' is
        const float tauAlpha = sqrt( 1 / (tau*VA_PI) );
        rayPayload.MaxRoughness = max( pow(rayPayload.MaxRoughness, bounceMod), pixel.Roughness );  // get the biggest roughnes on the path so far
        rayPayload.MaxRoughness = min( rayPayload.MaxRoughness, tauAlpha );                         // reduce the limit so it never goes above 'tau alpha' (see above paper)
        pixel.Roughness = max( rayPayload.MaxRoughness, pixel.Roughness );                          // limit the current pixel (material) roughnes to above MaxRoughness    

        //if( DebugOnce() )
        //    DebugText( tauAlpha );
    }

    // ambient lighting - totally not PBR but left in for testing purposes
    // rayPayload.AccumulatedRadiance  += rayPayload.Beta * material.BaseColor.rgb * g_lighting.AmbientLightIntensity.rgb;

    // decorrelate sampling for unrelated effects
    const uint hashSeedDirectIndirect1D = Hash32Combine( rayPayload.HashSeed, VA_RAYTRACING_HASH_SEED_DIR_INDIR_LIGHTING_1D );
    const uint hashSeedDirectIndirect2D = Hash32Combine( rayPayload.HashSeed, VA_RAYTRACING_HASH_SEED_DIR_INDIR_LIGHTING_2D );

    // let's use the same for indirect and direct lighting since they're used for non-correlated stuff
    const float  ldSample1D = LDSample1D( sampleIndex, hashSeedDirectIndirect1D );
    const float2 ldSample2D = LDSample2D( sampleIndex, hashSeedDirectIndirect2D );

    bool lastBounce = (rayPayload.Flags & VA_RAYTRACING_FLAG_LAST_BOUNCE) != 0;

    // compute new ray start position with the offset to avoid self-intersection; see function for docs
    // (also used for direct lighting visibility rays because why not, it's so good)
    const float3 nextRayOrigin  = OffsetNextRayOrigin( surface.WorldspacePos.xyz, surface.TriangleNormal ); // compute new ray start position with the offset to avoid self-intersection
    float3 nextRayDirection     = {0,0,0};

    // output depth if needed
    if( rayPayload.BounceCount == 0 && sampleIndex == 0 )
        g_viewspaceDepth[rayPayload.DispatchRaysIndex] = clamp( dot( g_globals.CameraDirection.xyz, nextRayOrigin - g_globals.CameraWorldPosition.xyz ), g_globals.CameraNearFar.x, g_globals.CameraNearFar.y );

    // O M G. I've been chasing this one for the whole day. Looks like a compiler bug. The + 1e-37 is a workaround for it.
    // TODO:    report this. It's been hard to replicate in a small example though.
    // UPDATE:  issue gone with v1.6.2104 - is it really fixed? leaving this in - we'll find out
    const float3 prevBeta   = rayPayload.Beta;// + 1e-37; 
    float prevSpecularness  = rayPayload.LastSpecularness;// + 1e-37;
    rayPayload.LastSpecularness = 0;
    float prevPathSpecularness = rayPayload.PathSpecularness;

    [branch]
    if( !lastBounce )
    {
        BSDFSample bsdf = BSDFSample_f( surface, shading, pixel, ldSample1D, ldSample2D );
        bsdf.PDF = max( 1e-16, bsdf.PDF ); // one could do a brutal max( 0.05, bsdf.PDF ) here if taking very, very few samples (lighting requires it too)

        // update for next ray
        nextRayDirection                = bsdf.Wi;
        rayPayload.ConeSpreadAngle      = surface.RayConeSpreadAngle;
        rayPayload.ConeWidth            = surface.RayConeWidth;
        rayPayload.AccumulatedRayTravel += RayTCurrent();

        rayPayload.Beta             *= bsdf.F / bsdf.PDF;

        // this maps PDF to specularness - very rough intuition, not worked out but it looks ok-ish. Need to come back and re-evaluate. Depends on MAX_ROUGHNESS and many other things.
        const float k = 4 * VA_PI;
        // const float p = <- how many steradians does the pixel subtend? how to define specularity? 
        rayPayload.LastSpecularness = saturate( bsdf.PDF / (k+bsdf.PDF) + 0.002 );

        if( false && debugDrawRayDetails )
        {
            DebugDraw2DText( float2( 10, 100 * (rayPayload.BounceCount ) +  0), float4(1,0.0,0,1), float4( rayPayload.BounceCount, ldSample1D, ldSample2D.x, ldSample2D.y ) );
            DebugDraw2DText( float2( 10, 100 * (rayPayload.BounceCount ) + 15), float4(1,0.2,0,1), float4( surface.WorldspacePos.xyz, shading.Normal.z ) );
            DebugDraw2DText( float2( 10, 100 * (rayPayload.BounceCount ) + 30), float4(1,0.4,0,1), float4( bsdf.F, bsdf.PDF ) );
            DebugDraw2DText( float2( 10, 100 * (rayPayload.BounceCount ) + 45), float4(1,0.6,0,1), float4( bsdf.Wi, sampleIndex ) );
        }
        //
        // add some kind of russian roulette here
        // if( length(rayPayload.Beta) < 0.001 )
        //     rayPayload.NextRayDirection = float3(0,0,0);
    }

    rayPayload.PathSpecularness *= rayPayload.LastSpecularness;

//    else
//    {
//        if( debugDrawRays )
//        {
//            DebugDraw2DText( float2( 10, 100 * (rayPayload.BounceCount ) +  0), float4(1,0.0,0,1), float4( rayPayload.BounceCount, 0, 0, 0 ) );
//            DebugDraw2DText( float2( 10, 100 * (rayPayload.BounceCount ) + 15), float4(1,0.2,0,1), float4( surface.WorldspacePos.xyz, shading.Normal.z ) );
//        }
//    }

    rayPayload.NextRayOrigin    = nextRayOrigin;
    rayPayload.NextRayDirection = nextRayDirection;

    // direct lighting
    {
        // http://www.pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Light_Sources.html
        //"Consider a diffuse surface illuminated by a small spherical area light source (Figure 14.7): sampling directions using the BSDF’s 
        // sampling distribution is likely to be very inefficient because the light is only visible along a small cone of directions from the 
        // point. A much better approach is to instead use a sampling distribution that is based on the light source. For example, the sampling 
        // routine could choose from among only those directions where the sphere is potentially visible."

        float3 lightRadiance            = {0,0,0};    // this radiance is coming from analytical lights (that might or might not have mesh representation)
        float3 specialEmissiveRadiance  = {0,0,0};    // this radiance is coming from scene representations of lights (i.e. light bulb mesh) - show this on very specular bounces instead of analytical lights
        evaluatePunctualLightsRT( surface, shading, pixel, nextRayOrigin, ldSample1D, ldSample2D, lightRadiance, specialEmissiveRadiance, rayPayload, debugDrawDirectLighting );

        // if we are just a pass-through surface (i.e. mirror), there's no point adding direct lighting as long as the lights themselves are represented as geometry
        lightRadiance *= saturate(1-rayPayload.LastSpecularness*rayPayload.LastSpecularness);
        // if the previous hit was just a specular bounce, show scene representations of lights (we want light meshes to be visible in the mirror!)
        lightRadiance += specialEmissiveRadiance * saturate( prevSpecularness*prevSpecularness );

        // emissive lighting (to be refactored)
#if defined(MATERIAL_HAS_EMISSIVE) && (VA_RM_SPECIAL_EMISSIVE_LIGHT == 0)
        lightRadiance.xyz += shading.PrecomputedEmissive * g_globals.PreExposureMultiplier;
#endif

        // no IBL - ha! okay maybe we'll have the distant one as direct lighting once importance sampling for env maps works? we'll see.
        // [branch] if( shading.IBL.UseLocal || shading.IBL.UseDistant )
        // {
        //     evaluateIBL( shading, material, pixel, lightRadiance, lastBounce, lastBounce );
        // }

        // Add up
        const float fireflyRadianceClamp = 2000.0;
        rayPayload.AccumulatedRadiance  += min( fireflyRadianceClamp * prevPathSpecularness, prevBeta * lightRadiance );
        //if( debugDrawRays )
        //{
        //    //DebugText( float4( rayPayload.AccumulatedRadiance, rayPayload.BounceCount ) );
        //    //DebugText( float4( lightRadiance, rayPayload.BounceCount ) );
        //}
    }

    if( debugDrawRays )
    {
#if defined( VA_FILAMENT_STANDARD )
        //DebugDraw3DText( surface.WorldspacePos.xyz, float2( -10, 150 ), float4(1,1,1,1), float4( material.Metallic, material.Roughness, 0, 0 ) );
#endif
    }
    if( debugDrawRayDetails )
    {
        DebugDraw3DText( surface.WorldspacePos.xyz, float2( -10, -16 ), float4(0,1,1,1.0), rayPayload.PathSpecularness ); // prevPathSpecularness
    }

    //rayPayload.AccumulatedRadiance = prevPathSpecularness;
    //rayPayload.AccumulatedRadiance = prevSpecularness;

    if( rayPayload.BounceCount == 0 )
    {
        // provides info for mouse right click context menu!
        ReportCursorInfo( instanceConstants, rayPayload.DispatchRaysIndex.xy, surface.WorldspacePos );

        // debugging viz stuff
        if( rayPayload.DebugViewType >= (uint)ShaderDebugViewType::SurfacePropsBegin && rayPayload.DebugViewType <= (uint)ShaderDebugViewType::SurfacePropsEnd )
        {
            rayPayload.AccumulatedRadiance = 0;
            if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::GeometryTexcoord0        )
                rayPayload.AccumulatedRadiance = float3(surface.Texcoord01.xy, 0);
            if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::GeometryNormalNonInterpolated        )
                rayPayload.AccumulatedRadiance = DisplayNormalSRGB( surface.TriangleNormal );
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::GeometryNormalInterpolated      )
                rayPayload.AccumulatedRadiance = DisplayNormalSRGB( surface.WorldspaceNormal.xyz );
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::GeometryTangentInterpolated     )
                rayPayload.AccumulatedRadiance = DisplayNormalSRGB( surface.WorldspaceTangent.xyz );
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::GeometryBitangentInterpolated   )
                rayPayload.AccumulatedRadiance = DisplayNormalSRGB( surface.WorldspaceBitangent.xyz );
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::ShadingNormal                   )
                rayPayload.AccumulatedRadiance = DisplayNormalSRGB( shading.Normal.xyz );
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::MaterialBaseColor               )
                rayPayload.AccumulatedRadiance = material.BaseColor.xyz;
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::MaterialBaseColorAlpha          )
                rayPayload.AccumulatedRadiance = material.BaseColor.aaa;
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::MaterialEmissive                )
                rayPayload.AccumulatedRadiance = material.EmissiveColor.rgb * material.EmissiveIntensity.xxx;
#if defined( VA_FILAMENT_STANDARD )
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::MaterialMetalness               )
                rayPayload.AccumulatedRadiance = material.Metallic.xxx;
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::MaterialReflectance             )
                rayPayload.AccumulatedRadiance = material.Reflectance.xxx;
#endif
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::MaterialRoughness               )
                rayPayload.AccumulatedRadiance = pixel.PerceptualRoughness;
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::MaterialAmbientOcclusion        )
                rayPayload.AccumulatedRadiance = material.AmbientOcclusion.xxx;
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::ReflectivityEstimate        )
                rayPayload.AccumulatedRadiance = pixel.ReflectivityEstimate;
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::MaterialID                      )
                rayPayload.AccumulatedRadiance = float3( asfloat(instanceConstants.MaterialGlobalIndex), 0, 0 );
            else if( rayPayload.DebugViewType == (uint)ShaderDebugViewType::ShaderID                        )
                rayPayload.AccumulatedRadiance = float3( asfloat((uint)VA_RM_SHADER_ID), 0, 0 );
        }
    }


    /*
    // these can go to the vaPathTracer.hlsl itself!
    // Apply fog to both for now - not a long term solution :)
    diffuseColor.rgb    = LightingApplyFog( surface.WorldspacePos.xyz, diffuseColor.rgb );
    specularColor.rgb   = LightingApplyFog( surface.WorldspacePos.xyz, specularColor.rgb );

    // This adds UI highlights and other similar stuff - add to diffuse (and has ability to cancel out diffuse and specular)
    float4 emissiveAdd  = Unpack_R10G10B10FLOAT_A2_UNORM( instanceConstants.EmissiveAddPacked );
    emissiveAdd         = lerp( emissiveAdd, float4( 0, 0.5, 0, 0 ), g_globals.WireframePass ); // I honestly don't know why is wireframe even supported in raytracing when it doesn't work but hey...
    diffuseColor.rgb    = diffuseColor.rgb * emissiveAdd.a + emissiveAdd.rgb;
    specularColor.rgb   = specularColor.rgb * emissiveAdd.a;

    // Useful for debugging stuff
    float4 debugColor   = DebugDisplay( surface, instanceConstants, material, meshConstants, shading, pixel );
    diffuseColor.rgb    = lerp( diffuseColor.rgb , debugColor.rgb, debugColor.a );
    specularColor.rgb   = lerp( specularColor.rgb, 0, debugColor.a );

    rayPayload.AccumulatedRadiance_Diff += diffuseColor;
    rayPayload.AccumulatedRadiance_Spec += specularColor;*/
}

#endif // #ifndef VA_RAYTRACING