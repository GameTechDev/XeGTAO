///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



#ifndef VA_MATERIAL_LOADERS_HLSL
#define VA_MATERIAL_LOADERS_HLSL

#include "vaShared.hlsl"
#include "vaRenderMesh.hlsl"

// this is now the default - if you need it to be different please rework
#define VA_NO_ALPHA_TEST_IN_MAIN_DRAW

Texture2D           g_DFGLookupTable            : register( T_CONCATENATER( SHADERGLOBAL_MATERIAL_DFG_LOOKUPTABLE_TEXTURESLOT_V ) );

// this will declare textures for the current material
#ifdef VA_RM_TEXTURE_DECLARATIONS
VA_RM_TEXTURE_DECLARATIONS;
#else
#error Expected VA_RM_TEXTURE_DECLARATIONS to be defined
#endif

// This is what all the vaRenderMaterial::m_new_inputSlots get converted into...
struct RenderMaterialInputs
{
#ifdef VA_RM_INPUTS_DECLARATIONS
    VA_RM_INPUTS_DECLARATIONS
#else
#error Expected VA_RM_INPUTS_DECLARATIONS to be defined
#endif
};

float4 RMSampleTexture2D( const in SurfaceInteraction surface, uint bindlessIndex, SamplerState sampler, uniform const int UVIndex )
{
    float2 coords = ( UVIndex == 0 )?( surface.Texcoord01.xy ):( surface.Texcoord01.zw );

#ifdef VA_RAYTRACING
    // Add final (texture-specific) LOD factor
    float2 dims;
    g_bindlessTex2D[bindlessIndex].GetDimensions( dims.x, dims.y );
    float texBaseLOD = log2( max(dims.x, dims.y) );
    return g_bindlessTex2D[bindlessIndex].SampleLevel( sampler, coords, texBaseLOD + surface.BaseLODTex0 + g_globals.GlobalMIPOffset );
    //return g_bindlessTex2D[bindlessIndex].SampleGrad( sampler, coords, gradsX, gradsY );
#else
    return g_bindlessTex2D[bindlessIndex].SampleBias( sampler, coords, g_globals.GlobalMIPOffset );
#endif
}


RenderMaterialInputs LoadRenderMaterialInputs( const in SurfaceInteraction surface, const in ShaderMaterialConstants materialConstants )
{
    RenderMaterialInputs inputs;

    const uint g_BindlessSRVIndices[16] = (uint[16])materialConstants.BindlessSRVIndicesPacked;

#ifdef VA_RM_NODES_DECLARATIONS
    VA_RM_NODES_DECLARATIONS;
#else
#error Expected VA_RM_NODES_DECLARATIONS to be defined
#endif

// just an example of a custom texture loader hack - not used for anything anymore but interesting
// #ifdef ASTEROID_SHADER_HACKS
//     BaseColorTexture = float4( 0, 1, 0, 1 ); //g_RMTexture00.SampleBias( g_samplerPointWrap, surface.Texcoord01.xy, g_globals.GlobalMIPOffset ); 
// 
//     // Triplanar projection
//     float3 blendWeights = abs(normalize(surface.ObjectPosition));
//     float3 uvw = surface.ObjectPosition * 0.5f + 0.5f;
//     // Tighten up the blending zone
//     blendWeights = saturate((blendWeights - 0.2f) * 7.0f);
//     blendWeights /= (blendWeights.x + blendWeights.y + blendWeights.z).xxx;
// 
//     float3 coords1 = float3(uvw.yz, 0);
//     float3 coords2 = float3(uvw.zx, 1);
//     float3 coords3 = float3(uvw.xy, 2);
// 
//     // TODO: Should really branch out zero'd weight ones, but FXC is being a pain
//     // and forward substituting the above and then refusing to compile "divergent"
//     // coordinates...
//     float3 detailTex = 0.0f;
//     detailTex += blendWeights.x * g_RMTexture00.SampleBias( g_samplerPointWrap, coords1.xy, g_globals.GlobalMIPOffset ).xyz;
//     detailTex += blendWeights.y * g_RMTexture00.SampleBias( g_samplerPointWrap, coords2.xy, g_globals.GlobalMIPOffset ).xyz;
//     detailTex += blendWeights.z * g_RMTexture00.SampleBias( g_samplerPointWrap, coords3.xy, g_globals.GlobalMIPOffset ).xyz;
//     
//     BaseColorTexture = float4( detailTex, 1 );
// #endif


#ifdef VA_RM_INPUTS_LOADING
    VA_RM_INPUTS_LOADING;
#else
#error Expected VA_RM_INPUTS_LOADING to be defined
#endif

    return inputs;
}

// predefined input macros:
//
// VA_RM_HAS_INPUT_#name                            : defined and 1 if material input slot with the 'name' exists


#endif // VA_MATERIAL_LOADERS_HLSL
