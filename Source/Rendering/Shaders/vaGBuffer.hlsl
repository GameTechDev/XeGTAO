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

#if 0 // obsolete, left as placeholder

Texture2D           g_textureSlot0              : register(t0);

float4 DebugDrawDepthPS( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    float v = g_textureSlot0.Load( int3( int2(Position.xy), 0 ) ).x;
    return float4( frac(v), frac(v*10.0), frac(v*100.0), 1.0 );
}

float4 DebugDrawDepthViewspaceLinearPS( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    float v = g_textureSlot0.Load( int3( int2(Position.xy), 0 ) ).x;
    //return float4( v.xxx/100.0, 1.0 );
    return float4( frac(v), frac(v/10.0), frac(v/100.0), 1.0 );
}

float4 DebugDrawNormalMapPS( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    float3 v = g_textureSlot0.Load( int3( int2(Position.xy), 0 ) ).xyz;
    
    v = GBufferDecodeNormal( v );

    return float4( DisplayNormalSRGB(v), 1.0 );
}

float4 DebugDrawAlbedoPS( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    float3 v = g_textureSlot0.Load( int3( int2(Position.xy), 0 ) ).xyz;
    return float4( v, 1.0 );
}

float4 DebugDrawRadiancePS( const in float4 Position : SV_Position, const in float2 Texcoord : TEXCOORD0 ) : SV_Target
{
    float3 v = g_textureSlot0.Load( int3( int2(Position.xy), 0 ) ).xyz;
    return float4( v, 1.0 );
}

#endif