///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VA_STANDARD_SAMPLERS_HLSL_INCLUDED
#define VA_STANDARD_SAMPLERS_HLSL_INCLUDED

#ifndef VA_COMPILED_AS_SHADER_CODE
#error not intended to be included outside of HLSL!
#endif

#include "vaSharedTypes.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global sampler slots

SamplerComparisonState                  g_samplerLinearCmpSampler               : register( S_CONCATENATER( SHADERGLOBAL_SHADOWCMP_SAMPLERSLOT ) ); 

SamplerState                            g_samplerPointClamp                     : register( S_CONCATENATER( SHADERGLOBAL_POINTCLAMP_SAMPLERSLOT         ) ); 
SamplerState                            g_samplerPointWrap                      : register( S_CONCATENATER( SHADERGLOBAL_POINTWRAP_SAMPLERSLOT          ) ); 
SamplerState                            g_samplerLinearClamp                    : register( S_CONCATENATER( SHADERGLOBAL_LINEARCLAMP_SAMPLERSLOT        ) ); 
SamplerState                            g_samplerLinearWrap                     : register( S_CONCATENATER( SHADERGLOBAL_LINEARWRAP_SAMPLERSLOT         ) ); 
SamplerState                            g_samplerAnisotropicClamp               : register( S_CONCATENATER( SHADERGLOBAL_ANISOTROPICCLAMP_SAMPLERSLOT   ) ); 
SamplerState                            g_samplerAnisotropicWrap                : register( S_CONCATENATER( SHADERGLOBAL_ANISOTROPICWRAP_SAMPLERSLOT    ) ); 

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif // VA_STANDARD_SAMPLERS_HLSL_INCLUDED