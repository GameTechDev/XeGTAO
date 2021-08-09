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

struct VSOutput2D
{                                       
   float4 xPos            : SV_Position;
   float4 xColor          : COLOR;
   float4 xUV             : TEXCOORD0;  
   float2 xOrigScreenPos  : TEXCOORD1;  
};                                      

VSOutput2D VS_Canvas2D( const float4 xInPos : SV_Position, const float4 xInColor : COLOR, const float4 xUV : TEXCOORD0, const float2 xScreenPos : TEXCOORD1 )
{
   VSOutput2D output;
   output.xPos           = xInPos;
   output.xColor         = xInColor;
   output.xUV            = xUV;
   output.xOrigScreenPos = xScreenPos;
   return output;
}

float4 PS_Canvas2D( const VSOutput2D xInput ) : SV_Target
{
   return xInput.xColor;
}

struct VSOutput3D
{                                                                               
  float4 xPos       : SV_Position;                                                
  float3 xNormal    : NORMAL;
  float4 xColor     : TEXCOORD0;                                                    
};

VSOutput3D VS_Canvas3D( const float4 xInPos : SV_Position, const float4 xInColor : COLOR, const float3 xInNormal : NORMAL )
{                                                                               
   VSOutput3D output;                                                                
   output.xPos      = xInPos;                                                         
   output.xColor    = xInColor;                                                     
   output.xNormal   = xInNormal;
   return output;                                                                
}                                                                               

float4 PS_Canvas3D( const VSOutput3D xInput, const bool isFrontFace : SV_IsFrontFace/*, out float outDepth : SV_Depth*/ ) : SV_Target
{                                               
    float3 lightDir = normalize( float3( 0.5, 0.5, -1.0) );
    
    float3 col = xInput.xColor.xyz * (0.3 + 0.6 * saturate( 0.2 + dot( xInput.xNormal, -lightDir ) ) );
    float alpha = xInput.xColor.a;
    //outDepth = xInput.xPos.z / xInput.xPos.w;
    //if( xInput.xColor.a < 0.99 )
    //    outDepth = (3.4e+38F) / xInput.xPos.w;

    
    if( !isFrontFace )
        alpha = lerp( alpha, 0, alpha );

    return float4( col, alpha ); // + frac( float4( 0.001f * (float)g_globalShared.FrameCounter, 0, 0, 0 ) );
}