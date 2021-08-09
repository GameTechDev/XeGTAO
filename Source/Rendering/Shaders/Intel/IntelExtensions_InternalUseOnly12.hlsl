//-----------------------------------------------------------------------------
// File: IntelExtensions_InternalUseOnly12.hlsl
//
// Desc: HLSL extensions available on Intel processor graphic platforms
//       Extensions defined in this file should be used only internally.
//
// Copyright (c) Intel Corporation 2020. All rights reserved.
//-----------------------------------------------------------------------------

#include "IntelExtensions12.hlsl"

//
// Define extension opcodes for internal usage (no enums in HLSL)
//

#define INTEL_EXT_GRADIENT_AND_BROADCAST    23

float IntelExt_GradientAndBroadcastScalar(float color)
{
    uint opcode = g_IntelExt.IncrementCounter();
    g_IntelExt[opcode].opcode = INTEL_EXT_GRADIENT_AND_BROADCAST;
    g_IntelExt[opcode].src0f.x = color.x;

	return g_IntelExt[opcode].dst0f.x;
}

float4 IntelExt_GradientAndBroadcastVec4(float4 color)
{
    return float4(
        IntelExt_GradientAndBroadcastScalar(color.x),
        IntelExt_GradientAndBroadcastScalar(color.y),
        IntelExt_GradientAndBroadcastScalar(color.z),
        IntelExt_GradientAndBroadcastScalar(color.w)
    );
}

float3 IntelExt_GradientAndBroadcastVec3(float3 color)
{
    return float3(
        IntelExt_GradientAndBroadcastScalar(color.x),
        IntelExt_GradientAndBroadcastScalar(color.y),
        IntelExt_GradientAndBroadcastScalar(color.z)
    );
}

float2 IntelExt_GradientAndBroadcastVec2(float2 color)
{
    return float2(
        IntelExt_GradientAndBroadcastScalar(color.x),
        IntelExt_GradientAndBroadcastScalar(color.y)
    );
}
