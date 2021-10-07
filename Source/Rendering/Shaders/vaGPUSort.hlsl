///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Note, this sorter relies on AMD FidelityFX Parallel Sort, please refer to FFX_ParallelSort.h for details

#ifndef __VA_GPU_SORT_HLSL__
#define __VA_GPU_SORT_HLSL__

#ifndef VA_COMPILED_AS_SHADER_CODE
#error not intended to be included outside of HLSL!
#endif

#include "vaShared.hlsl"

#define FFX_HLSL
#include "FFX_ParallelSort.h"

cbuffer FFX_ParallelSortCBConstantsBuffer                         : register( b0 )
{
	FFX_ParallelSortCB   g_consts;
}

StructuredBuffer<uint>		SrcBuffer				: register(t0);			// The unsorted keys or scan data
StructuredBuffer<uint>      SrcPayload				: register(t1);			// The payload data

RWStructuredBuffer<uint>    ScratchBuffer			: register(u0);			// a.k.a SumTable - the sum table we will write sums to
RWStructuredBuffer<uint>    ReducedScratchBuffer    : register(u1);			// a.k.a. ReduceTable - the reduced sum table we will write sums to
						    
RWStructuredBuffer<uint>    DstBuffer				: register(u2);			// The sorted keys or prefixed data
RWStructuredBuffer<uint>    DstPayload				: register(u3);			// the sorted payload data


// FPS Count
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void FPS_Count(uint localID : SV_GroupThreadID, uint groupID : SV_GroupID)
{
	// Call the uint version of the count part of the algorithm
	FFX_ParallelSort_Count_uint( localID, groupID, g_consts, g_genericRootConst, SrcBuffer, ScratchBuffer );
}


// FPS Reduce
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void FPS_CountReduce(uint localID : SV_GroupThreadID, uint groupID : SV_GroupID)
{
	// Call the reduce part of the algorithm
	FFX_ParallelSort_ReduceCount( localID, groupID, g_consts, ScratchBuffer, ReducedScratchBuffer );
}

// FPS Scan
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void FPS_ScanPrefix(uint localID : SV_GroupThreadID, uint groupID : SV_GroupID)
{
	uint BaseIndex = FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE * groupID;
	FFX_ParallelSort_ScanPrefix( g_consts.NumScanValues, localID, groupID, 0, BaseIndex, false, g_consts, ReducedScratchBuffer, ReducedScratchBuffer,ReducedScratchBuffer );
}

// FPS ScanAdd
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void FPS_ScanAdd(uint localID : SV_GroupThreadID, uint groupID : SV_GroupID)
{
	// When doing adds, we need to access data differently because reduce 
	// has a more specialized access pattern to match optimized count
	// Access needs to be done similarly to reduce
	// Figure out what bin data we are reducing
	uint BinID = groupID / g_consts.NumReduceThreadgroupPerBin;
	uint BinOffset = BinID * g_consts.NumThreadGroups;

	// Get the base index for this thread group
	uint BaseIndex = (groupID % g_consts.NumReduceThreadgroupPerBin) * FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE;

	FFX_ParallelSort_ScanPrefix(g_consts.NumThreadGroups, localID, groupID, BinOffset, BaseIndex, true, g_consts, ScratchBuffer, ScratchBuffer, ReducedScratchBuffer);
}

// FPS Scatter
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void FPS_Scatter(uint localID : SV_GroupThreadID, uint groupID : SV_GroupID)
{
	FFX_ParallelSort_Scatter_uint(localID, groupID, g_consts, g_genericRootConst, SrcBuffer, DstBuffer, ScratchBuffer
#ifdef kRS_ValueCopy
		,SrcPayload, DstPayload
#endif // kRS_ValueCopy
	);
}


#endif // __VA_GPU_SORT_HLSL__