///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VA_SHADER_IBLSHARED_H
#define VA_SHADER_IBLSHARED_H

#include "vaShaderCore.h"

#include "vaLightingShared.h"

#ifndef VA_COMPILED_AS_SHADER_CODE
namespace Vanilla
{
#endif
//
//    // all of this is unused at the moment
//    struct PostProcessBlurConstants
//    {
//        vaVector2               PixelSize;
//        float                   Factor0;
//        float                   Dummy0;
//
//        int                     GaussIterationCount;
//        int                     Dummy1;
//        int                     Dummy2;
//        int                     Dummy3;
//        vaVector4               GaussOffsetsWeights[1024];
//    };
//
#ifndef VA_COMPILED_AS_SHADER_CODE
} // namespace Vanilla
#endif

// Filtering defines
#define IBL_FILTER_UAV_SLOT                            0
#define IBL_FILTER_CUBE_FACES_ARRAY_UAV_SLOT           1

#define IBL_FILTER_CUBE_FACES_ARRAY_TEXTURE_SLOT       0
//#define IBL_FILTER_SCALING_FACTOR_K_TEXTURE_SLOT       1
#define IBL_FILTER_GENERIC_TEXTURE_SLOT_0              1
#define IBL_FILTER_CUBE_TEXTURE_SLOT                   2


#ifdef VA_COMPILED_AS_SHADER_CODE

// cbuffer PostProcessBlurConstantsBuffer : register( B_CONCATENATER( POSTPROCESS_BLUR_CONSTANTSBUFFERSLOT ) )
// {
//     PostProcessBlurConstants              g_postProcessBlurConsts;
// }

#endif


#endif // VA_SHADER_IBLSHARED_H