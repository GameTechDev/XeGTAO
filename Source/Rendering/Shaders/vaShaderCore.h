///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VA_SHADER_CORE_H
#define VA_SHADER_CORE_H

#ifndef VA_COMPILED_AS_SHADER_CODE

namespace Vanilla
{
#define VA_SATURATE     vaMath::Saturate
#define VA_MIN          vaComponentMin
#define VA_MAX          vaComponentMax
#define VA_LENGTH       vaLength
#define VA_INLINE       inline
#define VA_REFERENCE    &
#define VA_CONST        const

}

#else

#define VA_SATURATE     saturate
#define VA_MIN          min
#define VA_MAX          max
#define VA_LENGTH       length
#define VA_INLINE       
#define VA_REFERENCE    
#define VA_CONST

#endif

#ifndef VA_COMPILED_AS_SHADER_CODE

#include "Core/vaCoreIncludes.h"

#else

// Vanilla-specific; this include gets intercepted and macros are provided through it to allow for some macro
// shenanigans that don't work through the normal macro string pairs (like #include macros!).
#include "MagicMacrosMagicFile.h"

// Vanilla defaults to column-major matrices in shaders because that is the DXC default with no arguments, and it
// seems to be more common* in general. (*AFAIK)
// This is in contrast to the C++ side, which is row-major, so the ordering of matrix operations in shaders needs
// to be inverted, which is fine, for ex., "projectedPos = mul( g_globals.ViewProj, worldspacePos )"
// One nice side-effect is that it's easy to drop the 4th column for 4x3 matrix (on c++ side), which becomes 3x4 
// (on the shader side), which is useful for reducing memory traffic for affine transforms.
// See https://github.com/microsoft/DirectXShaderCompiler/blob/master/docs/SPIR-V.rst#appendix-a-matrix-representation 
// and http://www.mindcontrol.org/~hplus/graphics/matrix-layout.html for more detail.

#define vaMatrix4x4 column_major float4x4
#define vaMatrix4x3 column_major float3x4
#define vaMatrix3x3 column_major float3x3
#define vaVector4   float4
#define vaVector3   float3
#define vaVector2   float2
#define vaVector2i  int2
#define vaVector2ui uint2
#define vaVector4i  int4
#define vaVector4ui uint4

#define CONCATENATE_HELPER(a, b) a##b
#define CONCATENATE(a, b) CONCATENATE_HELPER(a, b)

#define B_CONCATENATER(x) CONCATENATE(b,x)
#define S_CONCATENATER(x) CONCATENATE(s,x)
#define T_CONCATENATER(x) CONCATENATE(t,x)
#define U_CONCATENATER(x) CONCATENATE(u,x)

#define ShaderMin( x, y )        min( x, y )

#endif

#endif // VA_SHADER_CORE_H