///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Material interface design
//
// Each material definition resides in a shader file in \Source\Rendering\Shaders\Materials\vaXXX.hlsl, for example,
// \Source\Rendering\Shaders\Materials\vaStandardPBR.hlsl. The XXX part of the name will match values in 
// vaRenderMaterialManager::MaterialClasses, so it's easy to cross reference.
// 
// While each material shader can define all of the entry points for each shader type manually, this is not the 
// preferred approach as it can lead to significant code duplication.
//
// The preferred approach is for the material definition files to expose MaterialXXX functions required by the
// rasterizer and/or path tracer, which will then get called by the external code.
// 
// Material interface:
// 
//  Load/setup:
//   * Material_Load        - with a given ShaderInstanceConstants and GeometryInteraction, load all raw material data 
//                            (sample textures, etc.), returning MaterialInputs
//   * Material_Surface     - with a given GeometryInteraction (interpolated geometry ray hit / rasterization point), 
//                            and MaterialInputs, pre-compute and return MaterialSurface (which contains everything 
//                            about material on the GeometrySurface)
//
//  Shared:
//   * Material_AlphaTest   - with a given raw GeometryInteraction and MaterialInputs and <>, return bool alpha test 
//                            value for the point
//   * Material_BxDF        - compute BxDF for given outgoing (view) vector Wo, incoming (light) vector Wi and 
//                            surface/material info
// 
//  Path tracing:
//   * Material_BxDFSample  - generate a Wi sample vector and pdf, given low discrepancy sampler(s) and surface/material info
//   ( not currently used / available ) * Material_BxDFPDF      - compute a sampling PDF of given Wo, Wi pair
//   * Material_WeighLight  - compute weight of the light proxy (luminance estimate)
// 
//  Rasterization:
//   * Material_Forward     - do complete forward shading
//  
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../vaShaderCore.h"

#include "../vaGeometryInteraction.hlsl"

#ifdef VA_RAYTRACING
#include "../vaPathTracerShared.h"
#endif 

#include "../Lighting/vaLighting.hlsl"

// this is shared for rasterized and raytraced paths so nice single place to do comparison
float4 DebugDisplay( const uint2 pixelPos, const GeometryInteraction geometrySurface, const ShaderInstanceConstants instanceConstants, const MaterialInputs material, const ShaderMeshConstants meshConstants, const MaterialInteraction materialSurface )
{
    //    [branch] if( IsUnderCursor( geometrySurface.Position.xy ) )
    //        DebugDraw2DText( geometrySurface.Position.xy + float2( 0, 20 ), float4( 0.8, 0.8, 0.8, 1 ), float4( VA_RM_SHADER_ID, geometrySurface.Position.xy * g_globals.ViewportPixelSize.xy, 0 ) );

    // *** Debug geometrySurface! ***
    // return float4( geometrySurface.Position.xy * g_globals.ViewportPixelSize.xy, 0 , 1.0 );
    // return float4( frac( geometrySurface.Position.z * 1000.0 ), frac( geometrySurface.Position.w * 1000.0 ), 0, 1 );
    // return float4( geometrySurface.Color.rgb, 1 );
    // return float4( geometrySurface.Color.aaa, 1 );
    // return float4( frac( geometrySurface.WorldspacePos.xyz ), 1 );
    // return float4( frac( mul( g_globals.View, float4( geometrySurface.WorldspacePos.xyz, 1 ) ) ).zzz, 1 ); - viewspace depth
    // return float4( DisplayNormalSRGB( geometrySurface.WorldspaceNormal.xyz ), 1 );
    // return float4( DisplayNormalSRGB( geometrySurface.TriangleNormal ), 1 );
    // return float4( frac(geometrySurface.Texcoord01.xy), 0, 1 );
    // return float4( 10*geometrySurface.RayConeWidth, 5*geometrySurface.RayConeWidthProjected, geometrySurface.RayConeWidthProjected/geometrySurface.RayConeWidth*0.5-1, 1 );

    // return float4( DisplayNormalSRGB( material.Normal ), 1.0 ); // looks ok
    // return float4( DisplayNormalSRGB( materialSurface.TangentToWorld[0] ), 1.0 ); // looks ok
    // return float4( DisplayNormalSRGB( materialSurface.TangentToWorld[1] ), 1.0 ); // looks ok
    // return float4( DisplayNormalSRGB( materialSurface.TangentToWorld[2] ), 1.0 ); // looks ok
    // [branch] if( IsUnderCursor( geometrySurface.Position.xy ) )
    //     DebugDraw3DText( geometrySurface.WorldspacePos.xyz, float2(0,-20), float4( 1, 0, 0, 1 ), float4( material.Normal, 0 ) );
    //return float4( DisplayNormalSRGB( materialSurface.Normal ), 1.0 ); // looks ok
    //return float4( DisplayNormalSRGB( materialSurface.BentNormal ), 1.0 ); // looks ok
    // return float4( DisplayNormalSRGB( materialSurface.Reflected ), 1.0 ); // I suppose it could be ok
    // return float4( pow( abs( materialSurface.NoV ), 2.2 ).xxx * 0.1, 1.0 ); // I suppose it could be ok as well
    // return float4( geometrySurface.ObjectspaceNoise.xxx, 1.0 ); // looks ok
    // return float4( material.AmbientOcclusion.xxx, 1.0 ); // looks ok
    // return float4( materialSurface.DiffuseAmbientOcclusion.xxx, 1.0 ); // looks ok
    // return float4( materialSurface.IBL.LocalToDistantK.xxx, 1 );

    // [branch] if( IsUnderCursor( geometrySurface.Position.xy ) )
    //     DebugDraw3DLine( geometrySurface.WorldspacePos.xyz, geometrySurface.WorldspacePos.xyz + materialSurface.Normal, float4( 0, 1, 0, 1 ) );
    // [branch] if( IsUnderCursor( geometrySurface.Position.xy + float2(-1,0) ) )
    //     DebugDraw3DLine( geometrySurface.WorldspacePos.xyz, geometrySurface.WorldspacePos.xyz + materialSurface.Normal, float4( 0, 0, 1, 1 ) );

    // return float4( materialSurface.DiffuseColor, 1.0 ); // looks ok
    // return float4( materialSurface.PerceptualRoughness.xxx, 1.0 ); // no idea if it's ok yet
    // return float4( materialSurface.F0, 1.0 ); // I guess it's cool?
    // return float4( materialSurface.Roughness.xxx, 1.0 ); // no idea if it's ok yet
    // return float4( materialSurface.DFG, 1.0 ); // no idea if it's ok yet
    // return float4( materialSurface.EnergyCompensation, 1.0 ); // no idea if it's ok yet

#if 0
    [branch] if( IsUnderCursor( pixelPos ) )
    {
        //SampleSSAO( const uint2 svpos, const float3 shadingNormal, out float aoVisibility, out float3 bentNormal )
        //float3 dbgWorldViewNorm = mul((float3x3)g_globals.ViewInv, viewspaceNormal).xyz;
        //float3 dbgWorldBentNorm = mul((float3x3)g_globals.ViewInv, bentNormal).xyz;
        //DebugDraw3DSphereCone( geometrySurface.WorldspacePos, materialSurface.Normal, 0.3, VA_PI*0.5 - acos(saturate(materialSurface.DiffuseAmbientOcclusion)), float4( 0.2, 0.2, 0.2, 0.5 ) );
        DebugDraw3DArrow( geometrySurface.WorldspacePos.xyz, geometrySurface.WorldspacePos.xyz + materialSurface.Normal * 0.5, 0.01, float4( 1, 0, 0, 0.5 ) );
        DebugDraw3DSphereCone( geometrySurface.WorldspacePos, materialSurface.BentNormal, 0.3, VA_PI*0.5 - acos(saturate(materialSurface.DiffuseAmbientOcclusion)), float4( 0.0, 1.0, 0.0, 0.7 ) );
        DebugDraw2DText( pixelPos + float2( 0, 20 ), float4( 0.8, 0.8, 0.8, 1 ), g_genericRootConst );
    }
#endif

    return float4( 0, 0, 0, 0 );
}

#ifndef VA_RAYTRACING

// depth only pre-pass (also used for shadow maps)
void PS_DepthOnly( const in ShadedVertex inVertex, const in float4 position : SV_Position, const in uint isFrontFace : SV_IsFrontFace )
{
    GeometryInteraction geometrySurface = GeometryInteraction::Compute( inVertex, isFrontFace );
#if VA_RM_ALPHATEST
    const ShaderInstanceConstants instanceConstants = LoadInstanceConstants( g_instanceIndex.InstanceIndex );
    MaterialInputs material = Material_Load( instanceConstants, geometrySurface );
    if( Material_AlphaTest( geometrySurface, material ) )
        discard;
#endif
}

// a more complex depth pre-pass that also outputs normals
void PS_RichPrepass( const in ShadedVertex inVertex, const in float4 position : SV_Position, uint isFrontFace : SV_IsFrontFace, 
    out uint outPackedNormals : SV_Target0, out float4 outMotionVectors : SV_Target1, out float outViewspaceDepth : SV_Target2 )
{
    GeometryInteraction geometrySurface = GeometryInteraction::Compute( inVertex, isFrontFace );
    const ShaderInstanceConstants instanceConstants = LoadInstanceConstants( g_instanceIndex.InstanceIndex );
    MaterialInputs materialInputs = Material_Load( instanceConstants, geometrySurface );

#if VA_RM_ALPHATEST
    if( Material_AlphaTest( geometrySurface, materialInputs ) )
        discard;
#endif

    MaterialInteraction materialSurface = Material_Surface( geometrySurface, materialInputs, (uint2)position.xy );

    // get worldspace geometry normal and convert to viewspace because this is what we consume later
    // const float3x3 tangentToWorld = geometrySurface.TangentToWorld( );
#if 1 // output normals in viewspace (otherwise worldspace)
    float3 outNormal = mul( (float3x3)g_globals.View, materialSurface.Normal );
#else // or worldspace!
    float3 outNormal = materialSurface.Normal;
#endif
    outPackedNormals.x = FLOAT3_to_R11G11B10_UNORM( saturate( outNormal.xyz * 0.5 + 0.5 ) );

    
#if 1   // also output motion vectors and viewspace depth
    //float depthNDC  = g_sourceDepth.Load( int3(pixCoord, 0)/*, offset*/).x;
    //float depth     = NDCToViewDepth( depthNDC );

    //float4 projectedPos = mul( g_globals.ViewProj, float4( geometrySurface.WorldspacePos.xyz, 1.0 ) );
    float depthNDC  = position.z; //    same as: projectedPos.z/projectedPos.w;
    float depth     = position.w; //    same as: projectedPos.w;                // also same as: dot( geometrySurface.WorldspacePos.xyz - g_globals.CameraWorldPosition.xyz, g_globals.CameraDirection.xyz );

#if 0   // without vertex motion
    float4 reprojectedPos = mul( g_globals.ReprojectionMatrix, float4( ScreenToNDCSpaceXY( position.xy ), depthNDC, 1 ) );
#else   // with vertex motion
    float4 projectedPrevPos = mul( g_globals.ViewProj, float4( geometrySurface.PreviousWorldspacePos.xyz, 1.0 ) );
    float4 reprojectedPos = mul( g_globals.ReprojectionMatrix, float4( projectedPrevPos.xy/projectedPrevPos.w, projectedPrevPos.z/projectedPrevPos.w, 1 ) );
#endif

    reprojectedPos.xyz /= reprojectedPos.w;
    float reprojectedDepth = NDCToViewDepth( reprojectedPos.z );

    // reduce 16bit precision issues - push the older frame ever so slightly into foreground
    reprojectedDepth *= 0.99999;

    float3 delta;
    delta.xy = NDCToScreenSpaceXY( reprojectedPos.xy ) - position.xy;
    delta.z = reprojectedDepth - depth;

    // de-jitter! not sure if this is the best way to do it for everything, but it's required for TAA
    delta.xy -= g_globals.CameraJitterDelta;

    outViewspaceDepth   = depth;
    outMotionVectors    = float4( delta.xyz, 0 ); //(uint)(frac( clip ) * 10000);

#endif

#if 0
    [branch] if( IsUnderCursorRange( position.xy, int2(1,1) ) )
        geometrySurface.DebugDrawTangentSpace( 0.3 );
#endif
}

// standard forward render
[earlydepthstencil]
float4 PS_Forward( const in ShadedVertex inVertex, const in float4 position : SV_Position, uint isFrontFace : SV_IsFrontFace/*, float3 baryWeights : SV_Barycentrics*/ ) : SV_Target
{
    GeometryInteraction geometrySurface = GeometryInteraction::Compute( inVertex, isFrontFace );

    const ShaderInstanceConstants instanceConstants = LoadInstanceConstants( g_instanceIndex.InstanceIndex );

    MaterialInputs materialInputs = Material_Load( instanceConstants, geometrySurface );

    // if running with depth pre-pass zbuffer and 'equal' depth test, no need to alpha-discard here!
#if VA_RM_ALPHATEST && !defined(VA_NO_ALPHA_TEST_IN_MAIN_DRAW)
    if( Material_AlphaTest( geometrySurface, material ) )
        discard;
#endif

    ShaderMeshConstants meshConstants     = g_meshConstants[instanceConstants.MeshGlobalIndex];

    // make all albedo white
    // material.BaseColor.rgb = 1.xxx;

    // after alpha-test
    ReportCursorInfo( instanceConstants, (int2)position.xy, geometrySurface.WorldspacePos ); // provides info for mouse right click context menu!

    // material.Normal = float3( 0, 0, 1 );
    MaterialInteraction materialSurface = Material_Surface( geometrySurface, materialInputs, (uint2)position.xy );

    float4 finalColor = Material_Forward( geometrySurface, materialSurface );

    finalColor.rgb = LightingApplyFog( geometrySurface.WorldspacePos.xyz, finalColor.rgb );

    // this adds UI highlights and other similar stuff
    float4 emissiveAdd = Unpack_R10G10B10FLOAT_A2_UNORM( instanceConstants.EmissiveAddPacked );
    finalColor.rgb = finalColor.rgb * emissiveAdd.a + emissiveAdd.rgb;

    finalColor.rgb = lerp( finalColor.rgb, float3( 0, 0.5, 0 ), g_globals.WireframePass );

    float4 debugColor = DebugDisplay( (uint2)position.xy, geometrySurface, instanceConstants, materialInputs, meshConstants, materialSurface );

    // [branch] if( IsUnderCursor( geometrySurface.Position.xy + float2( 1,0) ) )
    //     DebugDraw3DLine( geometrySurface.WorldspacePos.xyz, geometrySurface.WorldspacePos.xyz + materialSurface.Normal, float4( 1, 0, 0, 1 ) );

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

#define ENABLE_DEBUG_DRAW_RAYS

[shader("anyhit")]
void AnyHitAlphaTest( inout ShaderMultiPassRayPayload rayPayload, BuiltInTriangleIntersectionAttributes attr )
{
#if VA_RM_ALPHATEST
    ShaderInstanceConstants     instanceConstants;
    ShaderMeshConstants         meshConstants;
    ShaderMaterialConstants     materialConstants;
    GeometryInteraction         geometrySurface;

    LoadHitSurfaceInteraction( attr.barycentrics, InstanceIndex( ), PrimitiveIndex( ), WorldRayDirection( ) * RayTCurrent( ), rayPayload.ConeSpreadAngle, rayPayload.ConeWidth, instanceConstants, meshConstants, materialConstants, geometrySurface );

    //callablePayload.Color = float4( frac( geometrySurface.WorldspacePos.xyz ), 1 );

//    if( all( frac( geometrySurface.WorldspacePos.xyz ) < 0.5 ) )
//        IgnoreHit( );

//    if( any( ((callablePayload.DispatchRaysIndex.xy + VA_RM_SHADER_ID) % 4) < 2 ) )
//        IgnoreHit( );

    MaterialInputs material = Material_Load( instanceConstants, geometrySurface );
    if( Material_AlphaTest( geometrySurface, material ) )
        IgnoreHit( );
#endif    
}

#if VA_USE_RAY_SORTING

[shader("miss")]    // using "miss callables" hack - see vaRaytraceItem::MaterialMissCallable and 
void PathTraceSurfaceResponse( inout ShaderMultiPassRayPayload rayPayloadLocal )
{
    ShaderGeometryHitPayload geometryHitPayload = g_pathGeometryHitPayloads[rayPayloadLocal.PathIndex];

#else

[shader("closesthit")]
void PathTraceClosestHit( inout ShaderMultiPassRayPayload rayPayloadLocal, in BuiltInTriangleIntersectionAttributes attr )
{
    ShaderGeometryHitPayload geometryHitPayload;
    //geometryHitPayload.RayOrigin        = WorldRayOrigin( );
    geometryHitPayload.InstanceIndex    = InstanceIndex( );
    geometryHitPayload.RayDirLength     = WorldRayDirection( ) * RayTCurrent( );
    geometryHitPayload.PrimitiveIndex   = PrimitiveIndex( );
    geometryHitPayload.Barycentrics     = attr.barycentrics;
    geometryHitPayload.MaterialIndex    = InstanceID( );
    // g_pathSortKeys[rayPayloadLocal.PathIndex] = geometryHitPayload.MaterialIndex; // no sorting in this path

#endif

//#if VA_RM_TRANSPARENT
//    return; //IgnoreHit( ); // for now :) THIS IS ALSO DISABLED ON THE C++ side, look for 'instanceGlobal.Material->IsTransparent()' in vaSceneRaytracingDX12.cpp
//#endif
    
    ShaderPathPayload pathPayload = g_pathPayloads[ rayPayloadLocal.PathIndex ];

    const uint sampleIndex = g_pathTracerConsts.SampleIndex();

    ShaderInstanceConstants     instanceConstants;
    ShaderMeshConstants         meshConstants;
    ShaderMaterialConstants     materialConstants;
    GeometryInteraction         geometrySurface;

    LoadHitSurfaceInteraction( /*pathPayload.DispatchRaysIndex.xy,*/ geometryHitPayload.Barycentrics, geometryHitPayload.InstanceIndex, geometryHitPayload.PrimitiveIndex, geometryHitPayload.RayDirLength, pathPayload.ConeSpreadAngle, pathPayload.ConeWidth, instanceConstants, meshConstants, materialConstants, geometrySurface );
    
    bool debugDrawRays              = false;
    bool debugDrawDirectLighting    = false;
    bool debugDrawRayDetails        = false;
#ifdef ENABLE_DEBUG_DRAW_RAYS
    debugDrawRays           = (pathPayload.Flags & VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_VIZ) != 0;
    debugDrawDirectLighting = debugDrawRays && ((pathPayload.Flags & VA_PATH_TRACER_FLAG_SHOW_DEBUG_LIGHT_VIZ) != 0);
    debugDrawRayDetails     = (pathPayload.Flags & VA_PATH_TRACER_FLAG_SHOW_DEBUG_PATH_DETAIL_VIZ) != 0;
#endif
    if( debugDrawRays )
    {
        float3 approxRayOrigin = geometrySurface.WorldspacePos.xyz - geometryHitPayload.RayDirLength;   // can't use WorldRayOrigin in the miss shader path!
        float4 rayDebugColor = float4( GradientRainbow( pathPayload.BounceIndex / 6.0 ), 0.4 );
        DebugDraw3DCylinder( approxRayOrigin, /*callablePayload.RayOrigin + callablePayload.RayDir + callablePayload.RayLength*/ geometrySurface.WorldspacePos.xyz, 
            pathPayload.ConeWidth * 0.5, geometrySurface.RayConeWidth * 0.5, rayDebugColor );
        DebugDraw3DSphere( geometrySurface.WorldspacePos.xyz, geometrySurface.RayConeWidth, float4( 0, 0, 0, 0.7 ) );
    }
    if( debugDrawRayDetails )
    {
        float4 rayDebugColor = float4( GradientRainbow( pathPayload.BounceIndex / 6.0 ), 1 );
        geometrySurface.DebugDrawTangentSpace( sqrt(geometrySurface.ViewDistance) * 0.1 );
        //DebugDraw3DText( geometrySurface.WorldspacePos.xyz, float2( -10, -16 ), lerp( rayDebugColor, float4(1,1,1,0.4), 0.5 ), rayPayload.BounceIndex );
    }

    MaterialInputs      materialInputs  = Material_Load( instanceConstants, geometrySurface );
    MaterialInteraction materialSurface = Material_Surface( geometrySurface, materialInputs, uint2(0,0) );

    // "poor man's path regularization"
    if( (pathPayload.Flags & VA_PATH_TRACER_FLAG_PATH_REGULARIZATION) != 0 )
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
        pathPayload.MaxRoughness = max( pow(pathPayload.MaxRoughness, bounceMod), materialSurface.Roughness );  // get the biggest roughnes on the path so far
        pathPayload.MaxRoughness = min( pathPayload.MaxRoughness, tauAlpha );                                   // reduce the limit so it never goes above 'tau alpha' (see above paper)
        materialSurface.Roughness = max( pathPayload.MaxRoughness, materialSurface.Roughness );                 // limit the current roughnes to above MaxRoughness    

        //if( DebugOnce() )
        //    DebugText( tauAlpha );
    }

    // ambient lighting - totally not PBR but left in for testing purposes
    // rayPayload.AccumulatedRadiance  += rayPayload.Beta * materialInputs.BaseColor.rgb * g_lighting.AmbientLightIntensity.rgb;

    // decorrelate sampling for unrelated effects
    const uint hashSeedDirectIndirect1D = Hash32Combine( pathPayload.HashSeed, VA_PATH_TRACER_HASH_SEED_DIR_INDIR_LIGHTING_1D );
    const uint hashSeedDirectIndirect2D = Hash32Combine( pathPayload.HashSeed, VA_PATH_TRACER_HASH_SEED_DIR_INDIR_LIGHTING_2D );

    // let's use the same for indirect and direct lighting since they're used for non-correlated stuff
    const float  ldSample1D = LDSample1D( sampleIndex, hashSeedDirectIndirect1D );
    const float2 ldSample2D = LDSample2D( sampleIndex, hashSeedDirectIndirect2D );

    bool lastBounce = (pathPayload.Flags & VA_PATH_TRACER_FLAG_LAST_BOUNCE) != 0;

    if( debugDrawRays )
        DebugDraw3DText( geometrySurface.WorldspacePos.xyz, float2( -10, 16 ), float4(lastBounce?1:0,1,1,1), pathPayload.BounceIndex );
    
    // if( !geometrySurface.IsFrontFace ) //dot( geometrySurface.TriangleNormal, geometrySurface.View ) < 0 )
    //     pathPayload.AccumulatedRadiance.g += 0.1;

    // O M G. I've been chasing this one for the whole day. Looks like a compiler bug. The + 1e-37 is a workaround for it.
    // TODO:    report this. It's been hard to replicate in a small example though.
    // UPDATE:  issue gone with v1.6.2104 - is it really fixed? leaving this in - we'll find out
    const float3 prevBeta = pathPayload.Beta;// + 1e-37; 
    const float prevLastSpecularness = pathPayload.LastSpecularness;
    pathPayload.LastSpecularness = 0;
    bool refracted = false; // useful for a couple of things like picking the next ray offset

    float3 nextRayDirection     = {0,0,0};

    [branch]
    if( !lastBounce )
    {
        BSDFSample bsdfSample;

        // importance sample
        Material_BxDFSample( geometrySurface, materialSurface, ldSample1D, ldSample2D, bsdfSample.Wi, bsdfSample.PDF, refracted );

        // debugging 
#ifdef BSDFSAMPLE_F_VIZ_DEBUG
        [branch] if( IsUnderCursorRange( (int2)geometrySurface.Position.xy, int2(1,1) ) )
        {
            float length = 0.3; float thickness = length * 0.01;
            geometrySurface.DebugDrawTangentSpace( length );
            DebugDraw3DArrow( geometrySurface.WorldspacePos, geometrySurface.WorldspacePos + Wi * length, thickness, float4( 1, 1, 1, 1 ) );
            DebugDraw3DCylinder( geometrySurface.RayOrigin( ), geometrySurface.WorldspacePos.xyz, thickness, thickness, float4(0.5,0.5,0,1) );
        }
#endif
        // evaluate sample
        bsdfSample.F = Material_BxDF( materialSurface, bsdfSample.Wi, refracted );

#ifdef BSDFSAMPLE_F_VIZ_DEBUG
        [branch] if( IsUnderCursorRange( (int2)geometrySurface.Position.xy, int2(1,1) ) )
        {
            DebugDraw3DText( geometrySurface.WorldspacePos.xyz, float2( -10, -62 ), float4(ret.F/ret.PDF,1), float4(ret.F, ret.PDF) );
            DebugDraw3DText( geometrySurface.WorldspacePos.xyz, float2( -10, -76 ), float4(ret.F/ret.PDF,1), float4(ret.F/ret.PDF, 0) );
        }
#endif

        // update for next ray
        nextRayDirection            = bsdfSample.Wi;
        pathPayload.ConeSpreadAngle = geometrySurface.RayConeSpreadAngle;
        pathPayload.ConeWidth       = geometrySurface.RayConeWidth;

        pathPayload.Beta             *= bsdfSample.F / bsdfSample.PDF;

        // this maps PDF to specularness - very rough intuition, not worked out but it looks ok-ish. Need to come back and re-evaluate. Depends on MAX_ROUGHNESS and many other things.
        const float k = 4 * VA_PI;
        // const float p = <- how many steradians does the pixel subtend? how to define specularity? 
        pathPayload.LastSpecularness = saturate( bsdfSample.PDF / (k+bsdfSample.PDF) + 0.002 );

        if( false && debugDrawRayDetails )
        {
            DebugDraw2DText( float2( 10, 100 * (pathPayload.BounceIndex ) +  0), float4(1,0.0,0,1), float4( pathPayload.BounceIndex, ldSample1D, ldSample2D.x, ldSample2D.y ) );
            DebugDraw2DText( float2( 10, 100 * (pathPayload.BounceIndex ) + 15), float4(1,0.2,0,1), float4( geometrySurface.WorldspacePos.xyz, materialSurface.Normal.z ) );
            DebugDraw2DText( float2( 10, 100 * (pathPayload.BounceIndex ) + 30), float4(1,0.4,0,1), float4( bsdfSample.F, bsdfSample.PDF ) );
            DebugDraw2DText( float2( 10, 100 * (pathPayload.BounceIndex ) + 45), float4(1,0.6,0,1), float4( bsdfSample.Wi, sampleIndex ) );
        }
        //
        // add some kind of russian roulette here
        // if( length(pathPayload.Beta) < 0.001 )
        //     pathPayload.NextRayDirection = float3(0,0,0);
    }

    // compute new ray start position with the offset to avoid self-intersection; see function for docs
    // (also used for direct lighting visibility rays because why not, it's so good)
    const float3 nextRayOrigin  = OffsetNextRayOrigin( geometrySurface.WorldspacePos.xyz, (refracted)?(-geometrySurface.TriangleNormal):(geometrySurface.TriangleNormal) ); // compute new ray start position with the offset to avoid self-intersection

#if !VA_USE_RAY_SORTING
    pathPayload.NextRayOrigin    = nextRayOrigin;
    pathPayload.NextRayDirection = nextRayDirection;
#endif

    // Emissive + direct lighting
    // Note: we scale emissive by prev specularness and direct lighting by current specularness to avoid double-lighting, since we always have 
    // double (parallel) geometry and analytical lighting representation. 
    {
        float3 lightRadiance            = {0,0,0};

        SamplePointLightsDirectRT( geometrySurface, materialSurface, nextRayOrigin, sampleIndex, pathPayload.HashSeed, ldSample1D, ldSample2D, lightRadiance, pathPayload, debugDrawDirectLighting );
        
        // pathPayload.LastSpecularness is 'current specularness' which determines how much emissive we'll take in the next bounce.
        // If this is the last bounce, it is 0 so we get (1.0-0.0) - all the light!
        lightRadiance *= saturate(1-pathPayload.LastSpecularness);

        // not sure what to do with this...
        [branch] if( (materialSurface.IBL.UseLocal || materialSurface.IBL.UseDistant) && lastBounce )  // last bounce samples skybox 
        {
            evaluateIBL( materialSurface, lightRadiance, materialSurface.IBL.UseLocal, materialSurface.IBL.UseDistant );
        }

        float3 emissiveRadiance = {0,0,0};
#if defined(MATERIAL_HAS_EMISSIVE)
        {
            // by convention, in Vanilla we either integrate emissive light via 'Next Event Estimation' (https://developer.nvidia.com/blog/conquering-noisy-images-in-ray-tracing-with-next-event-estimation/)
            // for diffuse bounces, in which case pathPayload.LastSpecularness is low/zero, so direct emissive below is mostly ignored; and for highly specular bounces
            // 'pathPayload.LastSpecularness' high/one, which means it wasn't captured by NEE so we add it here:
            emissiveRadiance = materialSurface.PrecomputedEmissive * g_globals.PreExposureMultiplier * saturate( prevLastSpecularness );
        }
#endif

        // the pathPayload.PathSpecularness here is before it gets updated, so it matches prevBeta
        pathPayload.AccumulatedRadiance  += RadianceCombineAndFireflyClamp( pathPayload.PathSpecularness, prevBeta, lightRadiance+emissiveRadiance );

        //if( debugDrawRays )
        //{
        //    //DebugText( float4( pathPayload.AccumulatedRadiance, pathPayload.BounceIndex ) );
        //    //DebugText( float4( lightRadiance, pathPayload.BounceIndex ) );
        //}
    }

    pathPayload.PathSpecularness *= pathPayload.LastSpecularness;

    if( debugDrawRays )
    {
#if defined( VA_FILAMENT_STANDARD )
        //DebugDraw3DText( geometrySurface.WorldspacePos.xyz, float2( -10, 150 ), float4(1,1,1,1), float4( materialInputs.Metallic, materialInputs.Roughness, 0, 0 ) );
#endif
    }
    if( debugDrawRayDetails )
    {
        DebugDraw3DText( geometrySurface.WorldspacePos.xyz, float2( -10, -16 ), float4(0,1,1,1.0), pathPayload.PathSpecularness ); // prevPathSpecularness
    }

    //pathPayload.AccumulatedRadiance = prevPathSpecularness;
    //pathPayload.AccumulatedRadiance = prevSpecularness;

    if( pathPayload.BounceIndex == 0 )
    {
        // provides info for mouse right click context menu!
        ReportCursorInfo( instanceConstants, pathPayload.PixelPos.xy, geometrySurface.WorldspacePos );

        // debugging viz stuff
        if( (uint)g_pathTracerConsts.DebugViewType >= (uint)PathTracerDebugViewType::SurfacePropsBegin && (uint)g_pathTracerConsts.DebugViewType <= (uint)PathTracerDebugViewType::SurfacePropsEnd )
        {
            pathPayload.AccumulatedRadiance = 0;
            if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::GeometryTexcoord0        )
                pathPayload.AccumulatedRadiance = float3(geometrySurface.Texcoord01.xy, 0);
            if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::GeometryNormalNonInterpolated        )
                pathPayload.AccumulatedRadiance = DisplayNormalSRGB( geometrySurface.TriangleNormal );
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::GeometryNormalInterpolated      )
                pathPayload.AccumulatedRadiance = DisplayNormalSRGB( geometrySurface.WorldspaceNormal.xyz );
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::GeometryTangentInterpolated     )
                pathPayload.AccumulatedRadiance = DisplayNormalSRGB( geometrySurface.WorldspaceTangent.xyz );
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::GeometryBitangentInterpolated   )
                pathPayload.AccumulatedRadiance = DisplayNormalSRGB( geometrySurface.WorldspaceBitangent.xyz );
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::ShadingNormal                   )
                pathPayload.AccumulatedRadiance = DisplayNormalSRGB( materialSurface.Normal.xyz );
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::MaterialBaseColor               )
                pathPayload.AccumulatedRadiance = materialInputs.BaseColor.xyz;
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::MaterialBaseColorAlpha          )
                pathPayload.AccumulatedRadiance = materialInputs.BaseColor.aaa;
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::MaterialEmissive                )
                pathPayload.AccumulatedRadiance = materialInputs.EmissiveColorIntensity;
#if defined( VA_FILAMENT_STANDARD )
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::MaterialMetalness               )
                pathPayload.AccumulatedRadiance = materialInputs.Metallic.xxx;
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::MaterialReflectance             )
                pathPayload.AccumulatedRadiance = materialInputs.Reflectance.xxx;
#endif
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::MaterialRoughness               )
                pathPayload.AccumulatedRadiance = materialSurface.PerceptualRoughness;
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::MaterialAmbientOcclusion        )
                pathPayload.AccumulatedRadiance = materialInputs.AmbientOcclusion.xxx;
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::ReflectivityEstimate        )
                pathPayload.AccumulatedRadiance = materialSurface.ReflectivityEstimate;
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::MaterialID                      )
                pathPayload.AccumulatedRadiance = float3( asfloat(instanceConstants.MaterialGlobalIndex), 0, 0 );
            else if( (uint)g_pathTracerConsts.DebugViewType == (uint)PathTracerDebugViewType::ShaderID                        )
                pathPayload.AccumulatedRadiance = float3( asfloat((uint)VA_RM_SHADER_ID), 0, 0 );
        }
    }

    /*
    // these can go to the vaPathTracer.hlsl itself!
    // Apply fog to both for now - not a long term solution :)
    diffuseColor.rgb    = LightingApplyFog( geometrySurface.WorldspacePos.xyz, diffuseColor.rgb );
    specularColor.rgb   = LightingApplyFog( geometrySurface.WorldspacePos.xyz, specularColor.rgb );

    // This adds UI highlights and other similar stuff - add to diffuse (and has ability to cancel out diffuse and specular)
    float4 emissiveAdd  = Unpack_R10G10B10FLOAT_A2_UNORM( instanceConstants.EmissiveAddPacked );
    emissiveAdd         = lerp( emissiveAdd, float4( 0, 0.5, 0, 0 ), g_globals.WireframePass ); // I honestly don't know why is wireframe even supported in raytracing when it doesn't work but hey...
    diffuseColor.rgb    = diffuseColor.rgb * emissiveAdd.a + emissiveAdd.rgb;
    specularColor.rgb   = specularColor.rgb * emissiveAdd.a;

    // Useful for debugging stuff
    float4 debugColor   = DebugDisplay( geometrySurface, instanceConstants, materialInputs, meshConstants, materialSurface );
    diffuseColor.rgb    = lerp( diffuseColor.rgb , debugColor.rgb, debugColor.a );
    specularColor.rgb   = lerp( specularColor.rgb, 0, debugColor.a );

    pathPayload.AccumulatedRadiance_Diff += diffuseColor;
    pathPayload.AccumulatedRadiance_Spec += specularColor;*/

    float3 auxAlbedo = saturate( materialSurface.DiffuseColor + materialSurface.F0 ); // the best I could do for now (effectively: lerp(albedo, float3(1,1,1), metallic ) )
    PathTracerOutputAUX( pathPayload.PixelPos, pathPayload, geometrySurface.WorldspacePos.xyz, auxAlbedo, materialSurface.Normal, 
        ComputeScreenMotionVectors( pathPayload.PixelPos + float2(0.5, 0.5), geometrySurface.WorldspacePos, geometrySurface.PreviousWorldspacePos, float2(0,0) ).xy );

    if( lastBounce )
    {
        // we're done!
        PathTracerCommit( rayPayloadLocal.PathIndex, pathPayload );
    }
    else
    {
        // advance these two parts of the payload - we're about to bounce
        pathPayload.BounceIndex++;
        pathPayload.HashSeed = Hash32( pathPayload.HashSeed );
        pathPayload.Flags |= (pathPayload.BounceIndex == g_pathTracerConsts.MaxBounces)?(VA_PATH_TRACER_FLAG_LAST_BOUNCE):(0);

        g_pathPayloads[ rayPayloadLocal.PathIndex ] = pathPayload;

#if VA_USE_RAY_SORTING
        // this gets sent through the path tracing API
        ShaderMultiPassRayPayload rayPayloadLocalNew;
        rayPayloadLocalNew.PathIndex       = rayPayloadLocal.PathIndex;
        rayPayloadLocalNew.ConeSpreadAngle = pathPayload.ConeSpreadAngle;
        rayPayloadLocalNew.ConeWidth       = pathPayload.ConeWidth;

        RayDesc nextRay;
        nextRay.Origin      = nextRayOrigin;
        nextRay.Direction   = nextRayDirection;
        nextRay.TMin        = 0.0;
        nextRay.TMax        = 1000000.0;

        const uint missShaderIndex      = 0;    // normal (primary) miss shader
        TraceRay( g_raytracingScene, RAY_FLAG_NONE/*RAY_FLAG_CULL_BACK_FACING_TRIANGLES*/, ~0, 0, 0, missShaderIndex, nextRay, rayPayloadLocalNew );
#endif
    }
}

#endif // #ifndef VA_RAYTRACING