///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __VA_SKY_HLSL__
#define __VA_SKY_HLSL__

#include "vaSharedTypes.h"

#define SKY_CONSTANTSBUFFERSLOT                    0

// The rest below is shader only code
#ifndef __cplusplus

#include "vaShared.hlsl"

cbuffer SimpleSkyConstantsBuffer                    : register( B_CONCATENATER( SKY_CONSTANTSBUFFERSLOT ) )
{
    SimpleSkyConstants                      g_simpleSkyGlobal;
}

struct VSOutput
{
   float4 Position      : SV_Position;
   float4 PositionProj  : TEXCOORD0;
};


VSOutput SimpleSkyboxVS( const float4 inPos : SV_Position )
{
   VSOutput ret;

   ret.Position = inPos;
   ret.PositionProj = inPos; //-mul( ProjToWorld, xOut.m_xPosition ).xyz;	

   return ret;
}

float4 SimpleSkyboxPS( const VSOutput input ) : SV_Target
{
   float4 skyDirection = mul( g_simpleSkyGlobal.ProjToWorld, input.PositionProj ).xyzw;
   skyDirection *= 1 / skyDirection.wwww;
   skyDirection.xyz = normalize( skyDirection.xyz );

   ////////////////////////////////////////////////////////////////////////////////////////////////
   // this is a quick ad-hoc noise algorithm used to dither the gradient, to mask the 
   // banding on these awful monitors
   // FS_2015: lol - this comment is from around 2005 - LCD dispays those days were apparently really bad :)
   float noise = frac( dot( sin( skyDirection.xyz * float3( 533, 599, 411 ) ) * 10, float3( 1.0, 1.0, 1.0 ) ) );
   float noiseAdd = (noise - 0.5) * 0.1;
   // noiseAdd = 0.0; // to disable noise, just uncomment this
   //
   float noiseMul = 1 - noiseAdd;
   ////////////////////////////////////////////////////////////////////////////////////////////////
   
   // Horizon

   float horizonK = 1 - dot( skyDirection.xyz, float3( 0, 0, 1 ) );
   horizonK = saturate( pow( abs( horizonK ), g_simpleSkyGlobal.SkyColorLowPow ) * g_simpleSkyGlobal.SkyColorLowMul );
   horizonK *= noiseMul;

   float4 finalColor = lerp( g_simpleSkyGlobal.SkyColorHigh, g_simpleSkyGlobal.SkyColorLow, horizonK );

   // Sun

   float dirToSun = saturate( dot( skyDirection.xyz, g_simpleSkyGlobal.SunDir.xyz ) / 2.0 + 0.5 );

   float sunPrimary = clamp( pow( dirToSun, g_simpleSkyGlobal.SunColorPrimaryPow ) * g_simpleSkyGlobal.SunColorPrimaryMul, 0.0f, 1.0f );
   sunPrimary *= noiseMul;

   finalColor += g_simpleSkyGlobal.SunColorPrimary * sunPrimary;

   float sunSecondary = clamp( pow( dirToSun, g_simpleSkyGlobal.SunColorSecondaryPow ) * g_simpleSkyGlobal.SunColorSecondaryMul, 0.0f, 1.0f );
   sunSecondary *= noiseMul;

   finalColor += g_simpleSkyGlobal.SunColorSecondary * sunSecondary;
   
   return float4( finalColor.xyz, 1 );
}

#endif // #ifndef __cplusplus

#endif // #ifndef __VA_SKY_HLSL__