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

#include "vaGPUSort.h"

#include "Rendering/vaRenderDevice.h"
#include "Rendering/vaRenderDeviceContext.h"
#include "Rendering/vaRenderGlobals.h"
#include "Rendering/vaShader.h"
#include "Rendering/vaRenderBuffers.h"

#define FFX_PARALLELSORT_ENABLED
#ifdef FFX_PARALLELSORT_ENABLED

#define FFX_CPP
#include "Shaders/FFX_ParallelSort.h"

#endif

using namespace Vanilla;

vaGPUSort::vaGPUSort( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params )
{
#ifdef FFX_PARALLELSORT_ENABLED
    m_constantBuffer = vaConstantBuffer::Create<FFX_ParallelSortCB>( params.RenderDevice, "GPUSortConstants" );
#endif
}

vaGPUSort::~vaGPUSort( ) 
{ 
}

vaDrawResultFlags vaGPUSort::Sort( vaRenderDeviceContext & renderContext, const shared_ptr<vaRenderBuffer> & keys, const shared_ptr<vaRenderBuffer> & outSortedIndices, const uint32 keyCount, const uint32 maxKeyValue )
{
    vaDrawResultFlags drawResults = vaDrawResultFlags::None; renderContext; keys; outSortedIndices; keyCount; maxKeyValue;
#ifdef FFX_PARALLELSORT_ENABLED

    VA_TRACE_CPUGPU_SCOPE( Sort, renderContext );

    // Allocate the scratch buffers needed for radix sort
    uint32_t scratchBufferSize;
    uint32_t reducedScratchBufferSize;
    FFX_ParallelSort_CalculateScratchResourceSize( keyCount, scratchBufferSize, reducedScratchBufferSize );
    if( m_scratchBuffer == nullptr || m_scratchBuffer->GetElementCount( ) < scratchBufferSize 
    	|| m_reducedScratchBuffer == nullptr || m_reducedScratchBuffer->GetElementCount( ) < reducedScratchBufferSize 
        || m_scratchIndicesBuffer == nullptr || m_scratchIndicesBuffer->GetElementCount( ) < keyCount 
        )
    {
        m_scratchBuffer         = vaRenderBuffer::Create( GetRenderDevice(), scratchBufferSize, vaResourceFormat::R32_UINT, vaRenderBufferFlags::None, "ScratchBuffer", nullptr );
        m_reducedScratchBuffer  = vaRenderBuffer::Create( GetRenderDevice(), reducedScratchBufferSize, vaResourceFormat::R32_UINT, vaRenderBufferFlags::None, "ReducedScratchBuffer", nullptr );
        m_scratchIndicesBuffer  = vaRenderBuffer::Create( GetRenderDevice(), keyCount, vaResourceFormat::R32_UINT, vaRenderBufferFlags::None, "ScratchIndicesBuffer", nullptr );
        m_scratchKeysBuffer     = vaRenderBuffer::Create( GetRenderDevice(), keyCount, vaResourceFormat::R32_UINT, vaRenderBufferFlags::None, "ScratchKeysBuffer", nullptr );
    }

    // Create shaders if needed
    if( m_FPS_Count == nullptr )
    {
        std::vector<shared_ptr<vaShader>> allShaders;
        string shaderFileToUse = "vaGPUSort.hlsl";

        allShaders.push_back( m_FPS_Count       = vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "FPS_Count", {}, false ) );
        allShaders.push_back( m_FPS_CountReduce = vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "FPS_CountReduce", {}, false ) );
        allShaders.push_back( m_FPS_ScanPrefix  = vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "FPS_ScanPrefix", {}, false ) );
        allShaders.push_back( m_FPS_ScanAdd     = vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "FPS_ScanAdd", {}, false ) );
        allShaders.push_back( m_FPS_Scatter     = vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "FPS_Scatter", { { "kRS_ValueCopy", "1" }, {"kRS_InitValueAsIndex", "1"} }, false ) );

        // wait until shaders are compiled! this allows for parallel compilation but ensures all are compiled after this point
        for( auto sh : allShaders ) sh->WaitFinishIfBackgroundCreateActive();
    }

    // Update master constant buffer
    uint32_t NumThreadgroupsToRun;
    uint32_t NumReducedThreadgroupsToRun;
    {
        const uint maxNumThreadgroups = 800;

        FFX_ParallelSortCB consts;
        FFX_ParallelSort_SetConstantAndDispatchData( keyCount, maxNumThreadgroups, consts, NumThreadgroupsToRun, NumReducedThreadgroupsToRun);
        m_constantBuffer->Upload( renderContext, consts );
    }

    vaComputeItem computeItem;
    computeItem.ConstantBuffers[0] = m_constantBuffer;

    shared_ptr<vaRenderBuffer> ReadBufferInfo( keys ), WriteBufferInfo( m_scratchKeysBuffer );
    shared_ptr<vaRenderBuffer> ReadPayloadBufferInfo( outSortedIndices ), WritePayloadBufferInfo( m_scratchIndicesBuffer );

    uint32 bitsNeeded = vaMath::CeilLog2( maxKeyValue );
    bitsNeeded = std::max( 1U, bitsNeeded );

    uint32 bitsRounding = FFX_PARALLELSORT_SORT_BITS_PER_PASS * 2;  // it's possible to remove * 2 by inverting something with ping-ponging somehow possibly
    bitsNeeded = ((bitsNeeded + bitsRounding - 1) / bitsRounding)*bitsRounding;

    for( uint32 Shift = 0; Shift < bitsNeeded; Shift += FFX_PARALLELSORT_SORT_BITS_PER_PASS )
    {
        // TODO: all of the uav inputs/outputs could be made static for the pass, and we could switch to Begin/ExecuteItem/End which is faster

        computeItem.GenericRootConst = Shift;
        computeItem.GlobalUAVBarrierBefore = false;

        // source keys
        computeItem.ShaderResourceViews[0]  = ReadBufferInfo;
        computeItem.ShaderResourceViews[1]  = ReadPayloadBufferInfo;

        vaRenderOutputs uavInputsOutputs;
        uavInputsOutputs.UnorderedAccessViews[0] = m_scratchBuffer;
        uavInputsOutputs.UnorderedAccessViews[1] = m_reducedScratchBuffer;
        uavInputsOutputs.UnorderedAccessViews[2] = WriteBufferInfo;
        uavInputsOutputs.UnorderedAccessViews[3] = WritePayloadBufferInfo;

        renderContext.BeginComputeItems( uavInputsOutputs, nullptr );

        {
            //VA_TRACE_CPUGPU_SCOPE( Count, renderContext );
            computeItem.ComputeShader = m_FPS_Count;
            computeItem.SetDispatch( NumThreadgroupsToRun );
            drawResults |= renderContext.ExecuteItem( computeItem );
        }

        {
            //VA_TRACE_CPUGPU_SCOPE( CountReduce, renderContext );
            computeItem.ComputeShader = m_FPS_CountReduce;
            computeItem.SetDispatch( NumReducedThreadgroupsToRun );
            drawResults |= renderContext.ExecuteItem( computeItem );
        }
        
        {
            //VA_TRACE_CPUGPU_SCOPE( ScanPrefix, renderContext );
            assert(NumReducedThreadgroupsToRun < FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE && "Need to account for bigger reduced histogram scan");
            computeItem.ComputeShader = m_FPS_ScanPrefix;
            computeItem.SetDispatch( 1 );
            drawResults |= renderContext.ExecuteItem( computeItem );
        }

        {
            //VA_TRACE_CPUGPU_SCOPE( ScanAdd, renderContext );
            computeItem.ComputeShader = m_FPS_ScanAdd;
            computeItem.SetDispatch( NumReducedThreadgroupsToRun );
            drawResults |= renderContext.ExecuteItem( computeItem );
        }

        {
            //VA_TRACE_CPUGPU_SCOPE( Scatter, renderContext );
            computeItem.ComputeShader = m_FPS_Scatter;
            computeItem.SetDispatch( NumThreadgroupsToRun );
            drawResults |= renderContext.ExecuteItem( computeItem );
        }

        renderContext.EndItems( );

        std::swap(ReadBufferInfo, WriteBufferInfo);
        std::swap(ReadPayloadBufferInfo, WritePayloadBufferInfo);
    }
#else
    VA_ERROR( "vaGPUSort not enabled!" );
    drawResults = vaDrawResultFlags::UnspecifiedError;
#endif
    return drawResults;
}



