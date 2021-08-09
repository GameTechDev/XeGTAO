///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XeGTAO is based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion", 
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
// 
// Implementation:  Filip Strugar (filip.strugar@intel.com), Steve Mccalla <stephen.mccalla@intel.com>         (\_/)
// Version:         1.00                                                                                      (='.'=)
// Details:         https://github.com/GameTechDev/XeGTAO                                                     (")_(")
//
// Version history: see XeGTAO.h
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaShared.hlsl"
#include "vaNoise.hlsl"
#include "vaRaytracingShared.h"

#ifndef __INTELLISENSE__    // avoids some pesky intellisense errors
#include "XeGTAO.h"
#endif

cbuffer GTAOConstantBuffer                      : register( b0 )
{
    GTAOConstants               g_GTAOConsts;
}

RWStructuredBuffer<ReferenceRTAOConstants>
                                g_inoutConsts           : register( u0 );
#define g_refRTAOConsts g_inoutConsts[0]

RWTexture2D<float>              g_outputRTAO            : register( u1 );
RWTexture2D<float4>             g_outputDbgTexture      : register( u2 );
RWTexture2D<float4>             g_outputNormalsDepth    : register( u3 );

void CommitPixel( float3 color )    // color.x holds AO; if color.a == 1, also dump AO out as a debugging viz value
{
    // Read current accumulation, if any
    float prevAO = (g_refRTAOConsts.AccumulatedFrames==0)?(0):(g_outputRTAO[DispatchRaysIndex().xy]);

    prevAO += color.x;

    // .z == 1 is a flag indicating that we shouldn't write AO as debug output because something else was written there
    if( color.z == 1 )
    {
        float accumFrameCount = min( g_refRTAOConsts.AccumulatedFrames+1, g_refRTAOConsts.AccumulateFrameMax );
        g_outputDbgTexture[DispatchRaysIndex().xy]  = float4( (prevAO/accumFrameCount).xxx, 1 );
    }

    // we could skip the whole raytracing thing here because we've accumulated as much as we wanted, but that would
    // break 3D debugging
    [branch] if( g_refRTAOConsts.AccumulatedFrames >= g_refRTAOConsts.AccumulateFrameMax )
        return;

    // Write the raytraced color to the output texture.
    g_outputRTAO[DispatchRaysIndex().xy] = prevAO;

}

[shader("raygeneration")]
void AORaygen()
{
    float3 rayDir;
    float3 rayOrigin;
    float rayConeSpreadAngle;
    float2 subPixelJitter = float2(0,0); // no AA

    // Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
    GenerateCameraRay(DispatchRaysIndex().xy, subPixelJitter, rayOrigin, rayDir, rayConeSpreadAngle);

    // seed based on current pixel - no need for hash, it gets hashed first thing in the ray processing
    uint  hashSeed = Hash32( DispatchRaysIndex().x );
    hashSeed = Hash32Combine( hashSeed, DispatchRaysIndex().y );
    hashSeed = Hash32( hashSeed );

    ShaderPathTracerRayPayload rayPayload;
    rayPayload.DispatchRaysIndex    = DispatchRaysIndex().xy;
    rayPayload.ConeSpreadAngle      = rayConeSpreadAngle;
    rayPayload.ConeWidth            = 0;                        // it's 0 at pinhole camera focal point
    rayPayload.AccumulatedRadiance  = float3( 1, 0, 1 );        // diffuse AO goes into .x, .z used for debugging (if 0, dump AO to debug, otherwise nothing)
    rayPayload.HashSeed             = hashSeed;
    rayPayload.Beta                 = float3( 0, 0, 0 );        // this is unused for AO
    rayPayload.Flags                = 0;
    rayPayload.NextRayOrigin        = rayOrigin;
    rayPayload.NextRayDirection     = rayDir;
    rayPayload.BounceCount          = -1;
    rayPayload.AccumulatedRayTravel = 0;
    rayPayload.LastSpecularness     = 1.0;
    rayPayload.PathSpecularness     = 1.0;
    rayPayload.MaxRoughness         = 0.0;

    // Trace the camera ray first (alternative to figuring out the starting point from depth & normal)
    RayDesc nextRay;
    nextRay.Origin      = rayPayload.NextRayOrigin;
    nextRay.Direction   = rayPayload.NextRayDirection;
    nextRay.TMin        = 0.0;
    nextRay.TMax        = 1000000.0;
    
    rayPayload.NextRayDirection = float3( 0, 0, 0 );    // set to 0 so that by default there will be no more bounces unless requested (so miss shader can be empty for ex.)

    const uint missShaderIndex      = 0;    // normal (primary) miss shader
    TraceRay( g_raytracingScene, RAY_FLAG_NONE/*RAY_FLAG_CULL_BACK_FACING_TRIANGLES*/, ~0, 0, 0, missShaderIndex, nextRay, rayPayload );

    // reset to 0, we're only counting from here
    rayPayload.AccumulatedRayTravel = 0;
    rayPayload.BounceCount          = 0;

    while( rayPayload.BounceCount < g_refRTAOConsts.MaxBounces && rayPayload.AccumulatedRayTravel < g_refRTAOConsts.TotalRaysLength && length( rayPayload.NextRayDirection ) > 0 ) 
    {
        nextRay.Origin      = rayPayload.NextRayOrigin;
        nextRay.Direction   = rayPayload.NextRayDirection;
        nextRay.TMin        = 0;
        nextRay.TMax        = g_refRTAOConsts.TotalRaysLength - rayPayload.AccumulatedRayTravel;

        rayPayload.NextRayDirection = float3( 0, 0, 0 );    // set to 0 so that by default there will be no more bounces unless requested (so miss shader can be empty for ex.)

        TraceRay( g_raytracingScene, RAY_FLAG_NONE/*RAY_FLAG_CULL_BACK_FACING_TRIANGLES*/, ~0, 0, 0, missShaderIndex, nextRay, rayPayload );
        float remainingDistance = g_refRTAOConsts.TotalRaysLength - rayPayload.AccumulatedRayTravel;

        // We've bounced against something and have further to travel
        if( remainingDistance > 0 )
        {
            // Apply Lambertian scattering - cosine term is baked into sampling distribution.
            // 
            // // We fade out albedo to 1 over the FallofRange (for ex. last 10%) distance; this reduces the noise on objects that are 
            // // entering/leaving the 'near-field AO' range. This does not matter nearly as much for the reference version (as we use many 
            // // samples per pixel) but it does matter for the inferenced version where stability with only few samples is important - so 
            // // we do it in the reference version too, to avoid forcing ML to learn it.
            // float falloff = 1 - saturate( (remainingDistance / g_refRTAOConsts.TotalRaysLength) / g_refRTAOConsts.FalloffRange );
            // float albedo  = lerp(g_refRTAOConsts.Albedo, 1, falloff);

            rayPayload.AccumulatedRadiance.x *= g_refRTAOConsts.Albedo; //albedo;
        }
        rayPayload.BounceCount++;
    };

    CommitPixel( rayPayload.AccumulatedRadiance );
}

[shader("closesthit")]
void AOClosestHit( inout ShaderPathTracerRayPayload rayPayload, in BuiltInTriangleIntersectionAttributes attr )
{
    ShaderInstanceConstants     instanceConstants;
    ShaderMeshConstants         meshConstants;
    ShaderMaterialConstants     materialConstants;
    SurfaceInteraction          surface;

    LoadHitSurfaceInteraction( rayPayload.DispatchRaysIndex.xy, attr.barycentrics, InstanceIndex(), PrimitiveIndex(), WorldRayDirection(), RayTCurrent(), rayPayload.ConeSpreadAngle, rayPayload.ConeWidth, instanceConstants, meshConstants, materialConstants, surface );

#ifdef DEBUG_DRAW_RAYS
    [branch] if( rayPayload.BounceCount == -1 && IsUnderCursorRange( rayPayload.DispatchRaysIndex.xy, int2(1,1) ) )
    {
        float4 rayDebugColor = float4( GradientRainbow( rayPayload.BounceCount / 10.0 ), 1 );
        DebugDraw3DCylinder( WorldRayOrigin( ), surface.WorldspacePos.xyz, 
           rayPayload.ConeWidth * 0.5, surface.RayConeWidth * 0.5, rayDebugColor );
        DebugDraw3DSphere( surface.WorldspacePos.xyz, surface.RayConeWidth, float4( 0, 0.5, 0, 0.8 ) );

        g_outputDbgTexture[rayPayload.DispatchRaysIndex] = float4( 1, 0, 0, 1 );
        rayPayload.AccumulatedRadiance.z = 0;
    
        surface.DebugDrawTangentSpace( 0.3 );
        // if( IsCursorClicked() )
        //     surface.DebugText( );
    }
#endif

    // In the future try using shading normal as well
    const float3x3 tangentToWorld = surface.TangentToWorld( );

    if( rayPayload.BounceCount == -1 )
    {
        // bounce 0 needs to dump out AO training/inferencing data
        float4 normalsDepth;
        normalsDepth.xyz = tangentToWorld[2];   // output normal
        normalsDepth.w   = -dot( surface.View*surface.ViewDistance, g_globals.CameraDirection.xyz );
        g_outputNormalsDepth[DispatchRaysIndex().xy] = normalsDepth;
    }

    // hash seed above is identical for identical paths; incremental sampleIndex gives us a good low discrepancy sampling for accumulation
    // for anything like adaptive sampling this needs to be changed to adaptive sample index
    float2 u = LDSample2D( g_refRTAOConsts.AccumulatedFrames, rayPayload.HashSeed );
    // !!advance hash after use to decorrelate!!
    rayPayload.HashSeed = Hash32( rayPayload.HashSeed );

    float3 dir = SampleHemisphereCosineWeighted( u );   // figure out where to send pseudo-random rays; cosine weighting baked in for implicit lambertian
    dir = normalize( mul( dir, tangentToWorld ) );      // tangent to world space - we should actually skip or mirror if it's tilted such that it's going to hit the same triangle 

    // update for next ray
    rayPayload.NextRayOrigin    = OffsetNextRayOrigin( surface.WorldspacePos.xyz, surface.TriangleNormal ); // compute new ray start position with the offset to avoid self-intersection
    rayPayload.NextRayDirection = dir;
    rayPayload.ConeSpreadAngle  = surface.RayConeSpreadAngle;
    rayPayload.ConeWidth        = surface.RayConeWidth;
    rayPayload.AccumulatedRayTravel += RayTCurrent();

#if 0 // various debug viz
    rayPayload.AccumulatedRadiance.z = 0; // <- this tells the dispatch thread not to output AO as debug viz 

#if 1
    //g_outputDbgTexture[DispatchRaysIndex().xy] = float4( isFrontFace.xxx, 1 );
    //g_outputDbgTexture[DispatchRaysIndex().xy] = float4( surface.Texcoord01.xy, 0, 1 );
    //g_outputDbgTexture[DispatchRaysIndex().xy] = float4( DisplayNormalSRGB( normalsDepth.xyz ), 1 ); //float4( frac(surface.WorldspacePos.xyz), 1 );
    //g_outputDbgTexture[DispatchRaysIndex().xy] = float4( DisplayNormalSRGB( rightedTriangleNormal ), 1 ); //float4( frac(surface.WorldspacePos.xyz), 1 );
    g_outputDbgTexture[DispatchRaysIndex().xy] = float4( DisplayNormalSRGB( tangentToWorld[2] ), 1 ); //float4( frac(surface.WorldspacePos.xyz), 1 );
#endif
#if 0
    float rand = Hash2DScreen( rayPayload.DispatchRaysIndex.xy );
    if( rand > 0.99 )
        g_outputDbgTexture[DispatchRaysIndex().xy]  = float4( 0, 0, 0, 1 ); //float4( DisplayNormalSRGB( normalsDepth.xyz ), 1 );
    else
        g_outputDbgTexture[DispatchRaysIndex().xy]  = float4( 1, 1, 1, 1 ); 
#endif
#endif

}

[shader("miss")]
void AOMiss(inout ShaderPathTracerRayPayload rayPayload)
{
#ifdef DEBUG_DRAW_RAYS
    float4 rayDebugColor = float4( GradientRainbow( rayPayload.BounceCount / 10.0 ), 0.3 );
    [branch] if( rayPayload.BounceCount == 0 && IsUnderCursorRange( rayPayload.DispatchRaysIndex.xy, int2(1,1) ) )
    {
        float approxEndConeWidth = rayPayload.ConeWidth*0.5 * (1+rayPayload.ConeSpreadAngle * RayTCurrent());
        DebugDraw3DCylinder( WorldRayOrigin(), WorldRayOrigin() + WorldRayDirection() * RayTCurrent(), rayPayload.ConeWidth*0.5, approxEndConeWidth, rayDebugColor );
        DebugDraw3DSphere( WorldRayOrigin() + WorldRayDirection() /** RayTCurrent()*/, /*approxEndConeWidth*/ 0.01, float4( 1.0, 0.0, 0.0, 0.8 ) );
    }
#endif

    if( rayPayload.BounceCount == -1 )
    {
        // bounce 0 needs to dump out AO training/inferencing data, even if it's sky!
        g_outputNormalsDepth[DispatchRaysIndex().xy] = float4( 0, 0, -1, 100001.0 );
    }

    rayPayload.AccumulatedRayTravel += RayTCurrent();
}


