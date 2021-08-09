///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Lukasz, Migas (Lukasz.Migas@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// See https://github.com/GameTechDev/TAA

#ifndef __INTEL_TAA_H__
#define __INTEL_TAA_H__

#define INTEL_TAA_NUM_THREADS_X 8
#define INTEL_TAA_NUM_THREADS_Y 8

#ifdef __cplusplus

#include <cmath>

namespace IntelTAA
{
    // cpp<->hlsl mapping
    struct Matrix4x4    { float           m[16];    };
    struct Vector4      { float           x,y,z,w;  };
    struct Vector3      { float           x,y,z;    };
    struct Vector2      { float           x,y;      };
    struct Vector2i     { int             x,y;      };
    struct Vector4i     { int             x,y,z,w;  };
    struct Vector4ui    { unsigned int    x,y,z,w;  };
    typedef unsigned int uint32;

#else // #ifdef __cplusplus

// cpp<->hlsl mapping
#define Matrix4x4       float4x4
#define Vector4         float4
#define Vector3         float3
#define Vector2         float2
#define Vector2i        int2
#define Vector4i        int4
#define Vector4ui       uint4
#define uint32          uint

#endif

// Constant buffer
struct FTAAResolve
{
    Vector4     Resolution;         //width, height, 1/width, 1/height
    Vector2     Jitter;
    uint32      FrameNumber;
    uint32      DebugFlags;         // AllowLongestVelocityVector | AllowNeighbourhoodSampling | AllowYCoCg | AllowVarianceClipping | AllowBicubicFilter | AllowDepthThreshold | MarkNoHistoryPixels
    float       LerpMul;
    float       LerpPow;
    float       VarClipGammaMin;    // (MIN_VARIANCE_GAMMA)
    float       VarClipGammaMax;    // (MAX_VARIANCE_GAMMA)
    float       PreExposureNewOverOld;
    float       Padding0;
    float       Padding1;
    float       Padding2;
};

#ifndef __cplusplus

    typedef float  fp32_t;
    typedef float2 fp32_t2;
    typedef float3 fp32_t3;
    typedef float4 fp32_t4;

    #if 0 == USE_FP16
    typedef float  fp16_t;
    typedef float2 fp16_t2;
    typedef float3 fp16_t3;
    typedef float4 fp16_t4;

    typedef int  i16_t;
    typedef int2 i16_t2;
    typedef int3 i16_t3;
    typedef int4 i16_t4;

    typedef uint  ui16_t;
    typedef uint2 ui16_t2;
    typedef uint3 ui16_t3;
    typedef uint4 ui16_t4;
    #else
    typedef float16_t  fp16_t;
    typedef float16_t2 fp16_t2;
    typedef float16_t3 fp16_t3;
    typedef float16_t4 fp16_t4;

    typedef int16_t  i16_t;
    typedef int16_t2 i16_t2;
    typedef int16_t3 i16_t3;
    typedef int16_t4 i16_t4;

    typedef uint16_t  ui16_t;
    typedef uint16_t2 ui16_t2;
    typedef uint16_t3 ui16_t3;
    typedef uint16_t4 ui16_t4;
    #endif

    // this is all that the effect requires
    struct TAAParams
    {
        FTAAResolve             CBData;

        // in current MiniEngine's format
        Texture2D<fp16_t3>      VelocityBuffer;
    
        // Current colour buffer - rgb used
        Texture2D<fp16_t3>      ColourTexture;
    
        // Stored temporal antialiased pixel - .a should be sufficient enough to handle weight stored as float [0.5f, 1.f)
        Texture2D<fp32_t4>      HistoryTexture;
    
        // Current linear depth buffer - used only when USE_DEPTH_THRESHOLD is set
        Texture2D<fp16_t>       DepthBuffer;
    
        // Previous linear frame depth buffer - used only when USE_DEPTH_THRESHOLD is set
        Texture2D<fp16_t>       PrvDepthBuffer;
    
        // Antialiased colour buffer (used in a next frame as the HistoryTexture)
        RWTexture2D<fp16_t4>    OutTexture;
    
        // Samplers
        SamplerState            MinMagLinearMipPointClamp;
        SamplerState            MinMagMipPointClamp;
    };

#endif // #ifndef __cplusplus




#ifdef __cplusplus
} // namespace
#endif // #ifdef __cplusplus

#endif // #ifndef __INTEL_TAA_H__