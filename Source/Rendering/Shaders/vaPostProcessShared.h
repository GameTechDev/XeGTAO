///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VA_SHADER_SHARED_TYPES_POSTPROCESS_HLSL
#define VA_SHADER_SHARED_TYPES_POSTPROCESS_HLSL

#include "vaShaderCore.h"

#define POSTPROCESS_COMPARISONRESULTS_UAV_SLOT      0

#define POSTPROCESS_CONSTANTSBUFFERSLOT             0

#define POSTPROCESS_TEXTURE_SLOT0                   0
#define POSTPROCESS_TEXTURE_SLOT1                   1
#define POSTPROCESS_TEXTURE_SLOT2                   2

// these should be good enough for 8k x 4k textures with 8bit LDR data; for 10bit they will lack precision but still be usable; for more than that - rework is needed
#define POSTPROCESS_COMPARISONRESULTS_SIZE          (4096)
#define POSTPROCESS_COMPARISONRESULTS_FIXPOINT_MAX  (260100.0)

#define POSTPROCESS_BLUR_CONSTANTSBUFFERSLOT        1

#define POSTPROCESS_BLUR_TEXTURE_SLOT0              0
#define POSTPROCESS_BLUR_TEXTURE_SLOT1              1

#define POSTPROCESS_TONEMAP_CONSTANTSBUFFERSLOT     1

#define POSTPROCESS_TONEMAP_TEXTURE_SLOT0           0


#ifndef VA_COMPILED_AS_SHADER_CODE
namespace Vanilla
{
#endif

    // used in a generic way depending on the shader
    struct PostProcessConstants
    {
        vaVector4               Param1;
        vaVector4               Param2;
        vaVector4               Param3;
        vaVector4               Param4;
    };

    // all of this is unused at the moment
    struct PostProcessBlurConstants
    {
        vaVector2               PixelSize;
        float                   Factor0;
        float                   Dummy0;

        int                     GaussIterationCount;
        int                     Dummy1;
        int                     Dummy2;
        int                     Dummy3;
        vaVector4               GaussOffsetsWeights[1024];
    };

    struct PostProcessTonemapConstants
    {
        float                   DbgGammaTest;
        float                   Exposure;
        float                   WhiteLevel;
        float                   Saturation;

        vaVector2               ViewportPixelSize;      // .xy == 1.0 / ViewportSize.xy

        float                   Dummy0;                 // unused

        // just above values, pre-calculated for faster shader math
        float                   PreExposureMultiplier;
        float                   WhiteLevelSquared;

        float                   BloomMultiplier;      
        float                   BloomMinThresholdPE;    // renderCamera.BloomSettings().BloomMinThreshold * consts.PreExposureMultiplier;

        float                   BloomMaxClampPE;        // renderCamera.BloomSettings().BloomMaxClamp * consts.PreExposureMultiplier;

        vaVector2               FullResPixelSize;
        vaVector2               BloomSampleUVMul;
    };


#ifndef VA_COMPILED_AS_SHADER_CODE
} // namespace Vanilla
#endif


#ifdef VA_COMPILED_AS_SHADER_CODE

cbuffer PostProcessConstantsBuffer : register( B_CONCATENATER( POSTPROCESS_CONSTANTSBUFFERSLOT ) )
{
    PostProcessConstants                    g_postProcessConsts;
}

cbuffer PostProcessBlurConstantsBuffer : register( B_CONCATENATER( POSTPROCESS_BLUR_CONSTANTSBUFFERSLOT ) )
{
    PostProcessBlurConstants              g_postProcessBlurConsts;
}

cbuffer PostProcessTonemapConstantsBuffer : register( B_CONCATENATER( POSTPROCESS_TONEMAP_CONSTANTSBUFFERSLOT ) )
{
    PostProcessTonemapConstants              g_postProcessTonemapConsts;
}

#endif


#endif // VA_SHADER_SHARED_TYPES_POSTPROCESS_HLSL