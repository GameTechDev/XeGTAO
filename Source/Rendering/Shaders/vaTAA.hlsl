///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Lukasz, Migas (Lukasz.Migas@intel.com) - TAA code, Filip Strugar (filip.strugar@intel.com) - integration
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaShared.hlsl"

//#ifndef __INTELLISENSE__    // avoids some pesky intellisense errors
#include "vaTAAShared.h"
//#endif

#include "vaTonemappers.hlsli"

#include "IntelTAA.hlsli"

cbuffer TAAConstantsBuffer : register( B_CONCATENATER( TAA_CONSTANTSBUFFERSLOT ) )
{
    TAAConstants        g_TAAConstants;
}

Texture2D<float>            g_sourceDepth           : register( t0 );   // source (clip space) depth (in our case NDC too?)

RWTexture2D<float4>         g_outputDbgImage        : register( u2 );

//RWTexture2D<float4>         g_outputFinal           : register( u0 );
//Texture2D<float4>           g_sourceMotionVectors   : register( t2 );


Texture2D<fp16_t3>          g_velocityBuffer    : register( t0 );
// Current colour buffer - rgb used
Texture2D<fp16_t3>          g_colourTexture     : register( t1 );
// Stored temporal antialiased pixel - .a should be sufficient enough to handle weight stored as float [0.5f, 1.f)
Texture2D<fp32_t4>          g_historyTexture    : register( t2 );
// Current linear depth buffer - used only when USE_DEPTH_THRESHOLD is set
Texture2D<fp16_t>           g_depthBuffer       : register( t3 );
// Previous linear frame depth buffer - used only when USE_DEPTH_THRESHOLD is set
Texture2D<fp16_t>           g_prvDepthBuffer    : register( t4 );
// Antialiased colour buffer (used in a next frame as the HistoryTexture)
RWTexture2D<fp16_t4>        g_outTexture        : register( u0 );


[numthreads(INTEL_TAA_NUM_THREADS_X, INTEL_TAA_NUM_THREADS_Y, 1)]
void CSTAA( uint3 inDispatchIdx : SV_DispatchThreadID, uint3 inGroupID : SV_GroupID, uint3 inGroupThreadID : SV_GroupThreadID )
{
    uint2 pixCoord  = inDispatchIdx.xy;

    float2 clipXY   = pixCoord + 0.5f;

    TAAParams params;
    params.CBData                       = g_TAAConstants.Consts;
    params.VelocityBuffer               = g_velocityBuffer;
    params.ColourTexture                = g_colourTexture;
    params.HistoryTexture               = g_historyTexture;
    params.DepthBuffer                  = g_depthBuffer;
    params.PrvDepthBuffer               = g_prvDepthBuffer;
    params.OutTexture                   = g_outTexture;
    params.MinMagLinearMipPointClamp    = g_samplerLinearClamp;
    params.MinMagMipPointClamp          = g_samplerPointClamp;

    //    float4 color    = float4( g_colourTexture.Load( uint3( pixCoord, 0 ) ), 1 );
    //    float4 history  = g_historyTexture.Load( uint3( pixCoord, 0 ) );
    //    float depth     = g_depthBuffer.Load( uint3( pixCoord, 0 ) );
    //    float depthPrev = g_prvDepthBuffer.Load( uint3( pixCoord, 0 ) );
    //

#ifdef TAA_SHOW_MOTION_VECTORS
    bool dbg1 = all( (pixCoord.xy+1) % 100 == 0.xx );
    bool dbg2 = all( (pixCoord.xy+49) % 100 == 0.xx );
    if( dbg1 || dbg2 )
    {
        float3 velocity = params.VelocityBuffer.Load( uint3( pixCoord, 0 ) );
        DebugDraw2DLine( clipXY, clipXY + velocity.xy, float4( dbg1, dbg2, 0, 1 ) );

        if( all( (pixCoord.xy+1) % 400 == 0 ) )
            DebugDraw2DText( clipXY, float4( 1, 0.5, 1, 1 ), float4( velocity, g_depthBuffer.Load( uint3( pixCoord, 0 ) ) ) );
    }
#endif
    //    g_outTexture[ pixCoord ] = depth - depthPrev;
#if 0 //TAA_SHOW_MOTION_VECTORS
    g_outputDbgImage[ pixCoord ] = float4( params.VelocityBuffer.Load( uint3( pixCoord, 0 ) ) * 0.01 + 0.5, 1 );

    [branch] if( IsUnderCursorRange(pixCoord, int2(1, 1)) )
    {
        DebugDraw2DText( pixCoord, float4( 1, 0.5, 1, 1 ), float4( params.CBData.Jitter, 0, 0 ) );
    }
#endif

    IntelTAA( inDispatchIdx, inGroupID, inGroupThreadID, params );
}

[numthreads(INTEL_TAA_NUM_THREADS_X, INTEL_TAA_NUM_THREADS_Y, 1)]
void CSFinalApply( uint2 dispatchThreadID : SV_DispatchThreadID )
{
    uint2 pixCoord = dispatchThreadID.xy;

    g_outTexture[pixCoord]  = fp16_t4( TAAInverseTonemap( g_historyTexture.Load( uint3( pixCoord, 0 ) ).rgb ), 1 );
}