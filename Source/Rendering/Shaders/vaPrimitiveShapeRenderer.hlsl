///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaSharedTypes_PrimitiveShapeRenderer.h"

#include "vaSimpleShadowMap.hlsl"

StructuredBuffer<uint>      g_shapeInfo : register( T_CONCATENATER( PRIMITIVESHAPERENDERER_SHAPEINFO_SRV ) );

struct VertexTransformed
{
    float4 Position             : SV_Position;
    float4 Color                : COLOR;
    //float4 ViewspacePos         : TEXCOORD0;
    //float4 ViewspaceNormal      : NORMAL0;
    //float4 ViewspaceTangent     : NORMAL1;
    //float4 ViewspaceBitangent   : NORMAL2;
    //float4 Texcoord0            : TEXCOORD1;
};


VertexTransformed VSMain( const in uint2 inData : SV_Position, const in uint vertID : SV_VertexID )
{
    uint shapeInfoOffset    = inData.x;
    uint vertexSpecificInfo = inData.y;

    // always stored first
    uint shapeType  = g_shapeInfo[ shapeInfoOffset ];   shapeInfoOffset++;

    // always stored second 
    float4 color;
    color.x = asfloat(g_shapeInfo[shapeInfoOffset]);  shapeInfoOffset++;
    color.y = asfloat(g_shapeInfo[shapeInfoOffset]);  shapeInfoOffset++;
    color.z = asfloat(g_shapeInfo[shapeInfoOffset]);  shapeInfoOffset++;
    color.w = asfloat(g_shapeInfo[shapeInfoOffset]);  shapeInfoOffset++;

    color *= g_PrimitiveShapeRendererConstants.ColorMul;

    // now figure out individual vertex position for various types
    float3 position         = float3( 0, 0, 0 );

    // just a simple triangle
    if( shapeType == 1 )
    {
        uint localOffset = shapeInfoOffset + vertexSpecificInfo;
        position.x = asfloat(g_shapeInfo[localOffset+0]);
        position.y = asfloat(g_shapeInfo[localOffset+1]);
        position.z = asfloat(g_shapeInfo[localOffset+2]);
    }
    else
    {
        float4x4 localTransform = { 
            asfloat(g_shapeInfo[shapeInfoOffset+ 0]), asfloat(g_shapeInfo[shapeInfoOffset+ 4]), asfloat(g_shapeInfo[shapeInfoOffset+ 8]), asfloat(g_shapeInfo[shapeInfoOffset+12]), 
            asfloat(g_shapeInfo[shapeInfoOffset+ 1]), asfloat(g_shapeInfo[shapeInfoOffset+ 5]), asfloat(g_shapeInfo[shapeInfoOffset+ 9]), asfloat(g_shapeInfo[shapeInfoOffset+13]), 
            asfloat(g_shapeInfo[shapeInfoOffset+ 2]), asfloat(g_shapeInfo[shapeInfoOffset+ 6]), asfloat(g_shapeInfo[shapeInfoOffset+10]), asfloat(g_shapeInfo[shapeInfoOffset+14]), 
            asfloat(g_shapeInfo[shapeInfoOffset+ 3]), asfloat(g_shapeInfo[shapeInfoOffset+ 7]), asfloat(g_shapeInfo[shapeInfoOffset+11]), asfloat(g_shapeInfo[shapeInfoOffset+15]) };
            
        shapeInfoOffset += 16;

        // cylinder
        if( shapeType == 2 )
        {
            // decode basic params
            float height            = asfloat(g_shapeInfo[shapeInfoOffset+0]);
            float radiusBottom      = asfloat(g_shapeInfo[shapeInfoOffset+1]);
            float radiusTop         = asfloat(g_shapeInfo[shapeInfoOffset+2]);
            float tessellation      = asfloat(g_shapeInfo[shapeInfoOffset+3]);

            // see EncodeCylinderVertex()
            uint topFlag            = (vertexSpecificInfo >> 31);
            uint awayFromCenterAxis = (vertexSpecificInfo >> 30) & 1;
            uint intAngle           = vertexSpecificInfo & ((1<<30)-1);

            float angle = (float)(intAngle / tessellation) * 2.0 * 3.14159265;

            float r = lerp( radiusBottom, radiusTop, topFlag ) * float(awayFromCenterAxis);

            position = float3( cos( angle ) * r, sin( angle ) * r, height * float(topFlag) - height * 0.5 );
        }

        position = mul( localTransform, float4( position.xyz, 1) ).xyz;
    }

    VertexTransformed ret;

    ret.Color                   = color;

    ret.Position                = mul( g_globals.ViewProj, float4( position, 1.0 ) );

    return ret;
}


float4 PSMain( const in VertexTransformed input ) : SV_Target
{
    return input.Color;
}
