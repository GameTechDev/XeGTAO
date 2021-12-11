///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// This is based on AMD FidelityFX Parallel Sort but modified, see vaGPUSort.h for explanation on modifications!

#ifndef __VA_GPU_SORT_HLSL__
#define __VA_GPU_SORT_HLSL__

#ifndef VA_COMPILED_AS_SHADER_CODE
#error not intended to be included outside of HLSL!
#endif

#include "vaShared.hlsl"

#define FFX_HLSL
#include "FFX_ParallelSort.h"

#if 1
cbuffer FFX_ParallelSortCBConstantsBuffer                         : register( b0 )
{
	FFX_ParallelSortCB   g_consts;
}
#else
StructuredBuffer<FFX_ParallelSortCB> g_constsArr    : register(t2);
static const FFX_ParallelSortCB g_consts = g_constsArr[0];
#endif

StructuredBuffer<uint>      SrcKeys                 : register(t0);			// The unsorted keys or scan data
StructuredBuffer<uint>      SrcIndices				: register(t1);			// The indices into keys that are to be sorted

RWStructuredBuffer<uint>    ScratchBuffer			: register(u0);			// a.k.a SumTable - the sum table we will write sums to
RWStructuredBuffer<uint>    ReducedScratchBuffer    : register(u1);			// a.k.a. ReduceTable - the reduced sum table we will write sums to
						    
RWStructuredBuffer<uint>    DstIndices				: register(u3);			// The indices into keys that are to be sorted

RWStructuredBuffer<FFX_ParallelSortCB> ConstsUAV            : register(u0);
RWStructuredBuffer<FFX_DispatchIndirectBuffer> DispIndUAV   : register(u1);

[numthreads(1, 1, 1)]
void CSSetupIndirect( uint index : SV_DispatchThreadID )
{
    const uint MaxThreadGroups = 800;

    //CBuffer[0].NumKeys = NumKeys; // the only thing we init on the cpp side
    const uint NumKeys = ConstsUAV[0].NumKeys;

    uint BlockSize = FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE;
    uint NumBlocks = (NumKeys + BlockSize - 1) / BlockSize;

    // Figure out data distribution
    uint NumThreadGroupsToRun = MaxThreadGroups;
    uint BlocksPerThreadGroup = (NumBlocks / NumThreadGroupsToRun);
    ConstsUAV[0].NumThreadGroupsWithAdditionalBlocks = NumBlocks % NumThreadGroupsToRun;

    if (NumBlocks < NumThreadGroupsToRun)
    {
        BlocksPerThreadGroup = 1;
        NumThreadGroupsToRun = NumBlocks;
        ConstsUAV[0].NumThreadGroupsWithAdditionalBlocks = 0;
    }

    ConstsUAV[0].NumThreadGroups = NumThreadGroupsToRun;
    ConstsUAV[0].NumBlocksPerThreadGroup = BlocksPerThreadGroup;

    // Calculate the number of thread groups to run for reduction (each thread group can process BlockSize number of entries)
    uint NumReducedThreadGroupsToRun = FFX_PARALLELSORT_SORT_BIN_COUNT * ((BlockSize > NumThreadGroupsToRun) ? 1 : (NumThreadGroupsToRun + BlockSize - 1) / BlockSize);
    ConstsUAV[0].NumReduceThreadgroupPerBin = NumReducedThreadGroupsToRun / FFX_PARALLELSORT_SORT_BIN_COUNT;
    ConstsUAV[0].NumScanValues = NumReducedThreadGroupsToRun;	// The number of reduce thread groups becomes our scan count (as each thread group writes out 1 value that needs scan prefix)

    ConstsUAV[0].NumThreadGroupsToRun = NumThreadGroupsToRun;
    ConstsUAV[0].NumReducedThreadGroupsToRun = NumReducedThreadGroupsToRun;

    // Setup dispatch arguments
    DispIndUAV[0].CountScatterArgs[0] = NumThreadGroupsToRun;
    DispIndUAV[0].CountScatterArgs[1] = 1;
    DispIndUAV[0].CountScatterArgs[2] = 1;
    DispIndUAV[0].CountScatterArgs[3] = 0;

    DispIndUAV[0].ReduceScanArgs[0] = NumReducedThreadGroupsToRun;
    DispIndUAV[0].ReduceScanArgs[1] = 1;
    DispIndUAV[0].ReduceScanArgs[2] = 1;
    DispIndUAV[0].ReduceScanArgs[3] = 0;
}

// FPS Count and FirstPassCount (see VA_GPUSORT_FIRST_PASS_INIT_INDICES)
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void CSCount(uint localID : SV_GroupThreadID, uint groupID : SV_GroupID)
{
	// Call the uint version of the count part of the algorithm
	FFX_ParallelSort_Count_uint( localID, groupID, g_consts, g_genericRootConst, SrcKeys, ScratchBuffer, SrcIndices );
}

// FPS Reduce
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void CSCountReduce(uint localID : SV_GroupThreadID, uint groupID : SV_GroupID)
{
	// Call the reduce part of the algorithm
	FFX_ParallelSort_ReduceCount( localID, groupID, g_consts, ScratchBuffer, ReducedScratchBuffer );
}

// FPS Scan
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void CSScanPrefix(uint localID : SV_GroupThreadID, uint groupID : SV_GroupID)
{
	uint BaseIndex = FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE * groupID;
	FFX_ParallelSort_ScanPrefix( g_consts.NumScanValues, localID, groupID, 0, BaseIndex, false, g_consts, ReducedScratchBuffer, ReducedScratchBuffer, ReducedScratchBuffer );
}
// FPS ScanAdd
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void CSScanAdd(uint localID : SV_GroupThreadID, uint groupID : SV_GroupID)
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

// FPS Scatter & FirstPassScatter (see VA_GPUSORT_FIRST_PASS_INIT_INDICES)
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void CSScatter(uint localID : SV_GroupThreadID, uint groupID : SV_GroupID)
{
	FFX_ParallelSort_Scatter_uint(localID, groupID, g_consts, g_genericRootConst, SrcKeys, /*DstBuffer,*/ ScratchBuffer, SrcIndices, DstIndices );
}


#endif // __VA_GPU_SORT_HLSL__