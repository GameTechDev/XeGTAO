///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Lukasz, Migas (Lukasz.Migas@intel.com) - TAA code, Filip Strugar (filip.strugar@intel.com) - integration
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __TAA_SHARED_H__
#define __TAA_SHARED_H__

#include "vaShaderCore.h"

#include "IntelTAA.h"

#ifdef __cplusplus
using IntelTAA::FTAAResolve;
namespace Vanilla
{
#endif // #ifdef __cplusplus

    #define TAA_CONSTANTSBUFFERSLOT                 0

    #define MOTIONVECTORS_BLOCK_SIZE_X              8
    #define MOTIONVECTORS_BLOCK_SIZE_Y              8

    // used in a generic way depending on the shader
    struct TAAConstants
    {
        vaMatrix4x4             ReprojectionMatrix;
        FTAAResolve             Consts;
    };

#ifdef __cplusplus
}   // close the namespace
#endif // #ifdef __cplusplus


#endif // __TAA_SHARED_H__