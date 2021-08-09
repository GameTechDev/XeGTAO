///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VA_SHADER_SHARED_TYPES_HELPERTOOLS_BLUR_HLSL
#define VA_SHADER_SHARED_TYPES_HELPERTOOLS_BLUR_HLSL

#include "vaShaderCore.h"

#ifndef VA_COMPILED_AS_SHADER_CODE
namespace Vanilla
{
#endif

    struct ImageCompareToolShaderConstants
    {
        int                     VisType;
        int                     Dummy1;
        int                     Dummy2;
        int                     Dummy3;
    };

    struct UITextureDrawShaderConstants
    {
        vaVector4               ClipRect;
        vaVector4               DestinationRect;
        float                   Alpha;
        int                     TextureArrayIndex;
        int                     TextureMIPIndex;
        int                     ContentsType;
        int                     ShowAlpha;
        int                     Padding0;
        int                     Padding1;
        int                     Padding2;
    };

#ifndef VA_COMPILED_AS_SHADER_CODE
} // namespace Vanilla
#endif

#define IMAGE_COMPARE_TOOL_BUFFERSLOT       1

#define IMAGE_COMPARE_TOOL_TEXTURE_SLOT0    0
#define IMAGE_COMPARE_TOOL_TEXTURE_SLOT1    1

#define TEXTURE_UI_DRAW_TOOL_BUFFERSLOT     1

#define TEXTURE_UI_DRAW_TOOL_TEXTURE_SLOT0  0

#ifdef VA_COMPILED_AS_SHADER_CODE

#endif

#ifndef VA_COMPILED_AS_SHADER_CODE
namespace Vanilla
{
#endif

    struct PrimitiveShapeRendererShaderConstants
    {
        //vaMatrix4x4         ShadowWorldViewProj;
        vaVector4           ColorMul;
    };

#ifndef VA_COMPILED_AS_SHADER_CODE
} // namespace Vanilla
#endif

#define PRIMITIVESHAPERENDERER_CONSTANTSBUFFERSLOT     1

#define PRIMITIVESHAPERENDERER_SHAPEINFO_SRV            0

#ifdef VA_COMPILED_AS_SHADER_CODE

cbuffer PrimitiveShapeRendererShaderConstantsBuffer : register( B_CONCATENATER( PRIMITIVESHAPERENDERER_CONSTANTSBUFFERSLOT ) )
{
    PrimitiveShapeRendererShaderConstants              g_PrimitiveShapeRendererConstants;
}

#endif
#endif // VA_SHADER_SHARED_TYPES_HELPERTOOLS_BLUR_HLSL