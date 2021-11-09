///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VA_RENDER_MESH_HLSL
#define VA_RENDER_MESH_HLSL

#include "vaGeometryInteraction.hlsl"

// This is the "unpacked" vaRenderMesh::StandardVertex
struct RenderMeshVertex
{
    float4 Position             : SV_Position;
    float4 Color                : COLOR;
    float4 Normal               : NORMAL;
    float4 Texcoord01           : TEXCOORD0;
};

RenderMeshVertex RenderMeshManualVertexLoad( const uint vertexBufferBindlessIndex, const uint index )
{
    const uint SizeOfStandardVertex = 48;
    RenderMeshVertex ret;

    const uint baseAddress = index * SizeOfStandardVertex;
    uint4 poscolRaw = g_bindlessBAB[vertexBufferBindlessIndex].Load4( baseAddress + 0 );
    ret.Position    = float4( asfloat( poscolRaw.xyz ), 1 );
    ret.Color       = R8G8B8A8_UNORM_to_FLOAT4( poscolRaw.w );
    ret.Normal      = asfloat( g_bindlessBAB[vertexBufferBindlessIndex].Load4( baseAddress + 16 ) );
    ret.Texcoord01  = asfloat( g_bindlessBAB[vertexBufferBindlessIndex].Load4( baseAddress + 32 ) );

    return ret;
}

ShadedVertex RenderMeshVertexShader( const RenderMeshVertex input, const ShaderInstanceConstants instanceConstants )
{
    ShadedVertex ret;

    ret.ObjectspacePos      = input.Position.xyz;

    //ret.Color                   = input.Color;
    ret.Texcoord01          = input.Texcoord01;
    // ret.Texcoord23          = float4( 0, 0, 0, 0 );

    ret.WorldspacePos.xyz    = mul( instanceConstants.World, float4( input.Position.xyz, 1 ) );
    ret.WorldspaceNormal.xyz = normalize( mul( (float3x3)instanceConstants.NormalWorld, input.Normal.xyz ).xyz );

    ret.PreviousWorldspacePos.xyz = mul( instanceConstants.PreviousWorld, float4( input.Position.xyz, 1 ) );

    // do all the subsequent shading math with the WorldBase for precision purposes
    ret.WorldspacePos.xyz -= g_globals.WorldBase.xyz;
    ret.PreviousWorldspacePos.xyz -= g_globals.PreviousWorldBase.xyz;

    // hijack this for highlighting and similar stuff
    ret.Color               = input.Color;

#ifdef VA_ENABLE_MANUAL_BARYCENTRICS
    ret.Barycentrics        = float3( 0, 0, 0 );
#endif

    return ret;
}

#ifndef VA_RAYTRACING

#if 0 // this version manually reads vertices instead of using vertex fixed function pipeline via the layouts - for testing only, it's slower
ShadedVertex VS_Standard( uint vertID : SV_VertexID )
{
    const ShaderInstanceConstants instanceConstants = LoadInstanceConstants( g_instanceIndex.InstanceIndex );
    const ShaderMeshConstants meshConstants         = g_meshConstants[instanceConstants.MeshGlobalIndex];
    return RenderMeshVertexShader( RenderMeshManualVertexLoad( meshConstants.VertexBufferBindlessIndex, vertID ), instanceConstants );
}
#else
ShadedVertex VS_Standard( const in RenderMeshVertex input, uint vertID : SV_VertexID, out float4 position : SV_Position )
{
    ShadedVertex a = RenderMeshVertexShader( input, LoadInstanceConstants( g_instanceIndex.InstanceIndex ) );

#if 1   // default (fastest) path
    position = mul( g_globals.ViewProj, float4( a.WorldspacePos.xyz, 1.0 ) );
    return a;
#else // test the standard vertex shader inputs vs the manual vertex inputs
    const ShaderInstanceConstants instanceConstants = LoadInstanceConstants( g_instanceIndex.InstanceIndex );
    const ShaderMeshConstants meshConstants         = g_meshConstants[instanceConstants.MeshGlobalIndex];
    ShadedVertex b = RenderMeshVertexShader( RenderMeshManualVertexLoad( meshConstants.VertexBufferBindlessIndex, vertID ), instanceConstants );

    // // just show a line up from every 0-th vertex 
    // [branch] if( vertID == 0 )
    //     DebugDraw3DLine( b.WorldspacePos.xyz, b.WorldspacePos.xyz + float3( 0, 0, 1 ), float4( 0.5, 5.0, 0.5, 0.9 ) );
   
    //a.Color.a += 0.0001;
    if( any( a.Position != b.Position ) ||
        any( a.Color            != b.Color            ) ||
        any( a.WorldspacePos    != b.WorldspacePos    ) ||
        any( a.WorldspaceNormal != b.WorldspaceNormal ) ||
        any( a.Texcoord01       != b.Texcoord01       ) )
        b.Color.x = 0;

    position = mul( g_globals.ViewProj, float4( b.WorldspacePos.xyz, 1.0 ) );
    return b;
#endif
}
#endif

#if defined(VA_ENABLE_MANUAL_BARYCENTRICS) && !defined(VA_ENABLE_PASSTHROUGH_GS)
#error manual barycentrics require custom GS below
#endif

#ifdef VA_ENABLE_PASSTHROUGH_GS

// // Per-vertex data passed to the rasterizer.
// struct GeometryShaderOutput
// {
//     min16float4 pos     : SV_POSITION;
//     min16float3 color   : COLOR0;
//     uint        rtvId   : SV_RenderTargetArrayIndex;
// };


// This geometry shader is a pass-through that leaves the geometry unmodified 
// and sets the render target array index.
[maxvertexcount(3)]
void GS_Standard(triangle ShadedVertex input[3], inout TriangleStream<ShadedVertex> outStream) <- this needs reworking w.r.t. sv_position, perhaps a new struct
{
    ShadedVertex output;
    [unroll(3)]
    for (int i = 0; i < 3; ++i)
    {
        output.Position        = input[i].Position;
        output.Color           = input[i].Color;
        output.WorldspacePos   = input[i].WorldspacePos;
        output.WorldspaceNormal= input[i].WorldspaceNormal;
        output.Texcoord01      = input[i].Texcoord01;

        output.ObjectspacePos  = input[i].ObjectspacePos;

#ifdef VA_ENABLE_MANUAL_BARYCENTRICS
        output.Barycentrics     = float3( i==0, i==1, i==2 );
#endif

        outStream.Append(output);
    }
}

#endif // VA_ENABLE_PASSTHROUGH_GS

#endif // VA_RAYTRACING

#endif // VA_RENDER_MESH_HLSL