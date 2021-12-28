///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaPostProcessShared.h"

#if defined( VA_POSTPROCESS_CLEAR_UAV_TEX1D_1F ) // 1D textures 
RWTexture1D<float>          g_clearTarget   : register( u0 );
[numthreads(64, 1, 1)] void CSClearUAV_Tex1D_1F( uint dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[min(dispatchThreadID, g_postProcessConsts.Bounds.x)] = g_postProcessConsts.Bounds.x; }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX1D_4F )
RWTexture1D<float4>         g_clearTarget   : register( u0 );
[numthreads(64, 1, 1)] void CSClearUAV_Tex1D_4F( uint dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[min(dispatchThreadID, g_postProcessConsts.Bounds.x)] = g_postProcessConsts.Bounds.xyzw; }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX1D_1U )
RWTexture1D<uint>          g_clearTarget   : register( u0 );
[numthreads(64, 1, 1)] void CSClearUAV_Tex1D_1U( uint dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[min(dispatchThreadID, g_postProcessConsts.Bounds.x)] = asuint(g_postProcessConsts.Bounds.x); }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX1D_4U )
RWTexture1D<uint4>         g_clearTarget   : register( u0 );
[numthreads(64, 1, 1)] void CSClearUAV_Tex1D_4U( uint dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[min(dispatchThreadID, g_postProcessConsts.Bounds.x)] = asuint(g_postProcessConsts.Bounds.xyzw); }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX2D_1F ) // 2D textures 
RWTexture2D<float>          g_clearTarget   : register( u0 );
[numthreads(8, 8, 1)] void CSClearUAV_Tex2D_1F( uint2 dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[min(dispatchThreadID, g_postProcessConsts.Bounds.xy)] = g_postProcessConsts.Bounds.x; }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX2D_4F )
RWTexture2D<float4>         g_clearTarget   : register( u0 );
[numthreads(8, 8, 1)] void CSClearUAV_Tex2D_4F( uint2 dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[min(dispatchThreadID, g_postProcessConsts.Bounds.xy)] = g_postProcessConsts.Bounds.xyzw; }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX2D_1U )
RWTexture2D<uint>          g_clearTarget   : register( u0 );
[numthreads(8, 8, 1)] void CSClearUAV_Tex2D_1U( uint2 dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[min(dispatchThreadID, g_postProcessConsts.Bounds.xy)] = asuint(g_postProcessConsts.Bounds.x); }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_TEX2D_4U )
RWTexture2D<uint4>         g_clearTarget   : register( u0 );
[numthreads(8, 8, 1)] void CSClearUAV_Tex2D_4U( uint2 dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[min(dispatchThreadID, g_postProcessConsts.Bounds.xy)] = asuint(g_postProcessConsts.Bounds.xyzw); }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_BUFF_1U ) // buffers
RWBuffer<uint>              g_clearTarget   : register( u0 );
[numthreads(64, 1, 1)] void CSClearUAV_Buff_1U( uint dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[min(dispatchThreadID, g_postProcessConsts.Bounds.x)] = asuint(g_postProcessConsts.Bounds.x); }
#elif defined( VA_POSTPROCESS_CLEAR_UAV_BUFF_4U )
RWBuffer<uint4>             g_clearTarget   : register( u0 );
[numthreads(64, 1, 1)] void CSClearUAV_Buff_4U( uint dispatchThreadID : SV_DispatchThreadID ) { g_clearTarget[min(dispatchThreadID, g_postProcessConsts.Bounds.x)] = asuint(g_postProcessConsts.Bounds.xyzw); }
#endif