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
    //m_constantBuffer = vaConstantBuffer::Create<FFX_ParallelSortCB>( params.RenderDevice, "GPUSortConstants" );
    m_constantBuffer = vaRenderBuffer::Create<FFX_ParallelSortCB>( params.RenderDevice, 1, vaRenderBufferFlags::ConstantBuffer, "GPUSortConstants" );
    m_dispatchIndirectBuffer = vaRenderBuffer::Create<FFX_DispatchIndirectBuffer>( params.RenderDevice, 1, vaRenderBufferFlags::ConstantBuffer, "GPUSortDispatchIndirect" );
#endif
}

vaGPUSort::~vaGPUSort( ) 
{ 
}

//#define DETAILED_GPU_PROFILING

vaDrawResultFlags vaGPUSort::Sort( vaRenderDeviceContext & renderContext, const shared_ptr<vaRenderBuffer> & keys, const shared_ptr<vaRenderBuffer> & sortedIndices, bool resetIndices, const uint32 keyCount, const uint32 maxKeyValue, const shared_ptr<vaRenderBuffer> & actualNumberOfKeysToSort, const uint32 actualNumberOfKeysToSortByteOffset )
{
    vaDrawResultFlags drawResults = vaDrawResultFlags::None; renderContext; keys; keyCount; maxKeyValue;
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
    }

    // Create shaders if needed
    if( m_CSSetupIndirect == nullptr )
    {
        std::vector<shared_ptr<vaShader>> allShaders;
        string shaderFileToUse = "vaGPUSort.hlsl";

        allShaders.push_back( m_CSSetupIndirect   = vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "CSSetupIndirect", {}, false ) ) ;
        allShaders.push_back( m_CSCount           = vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "CSCount", {}, false ) );
        allShaders.push_back( m_CSCountReduce     = vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "CSCountReduce", {}, false ) );
        allShaders.push_back( m_CSScanPrefix      = vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "CSScanPrefix", {}, false ) );
        allShaders.push_back( m_CSScanAdd         = vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "CSScanAdd", {}, false ) );
        allShaders.push_back( m_CSScatter         = vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "CSScatter", {}, false ) );
        allShaders.push_back( m_CSFirstPassCount  = vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "CSCount", { { "VA_GPUSORT_FIRST_PASS_INIT_INDICES", "" } }, false ) );
        allShaders.push_back( m_CSFirstPassScatter= vaComputeShader::CreateFromFile( GetRenderDevice(), shaderFileToUse, "CSScatter", { { "VA_GPUSORT_FIRST_PASS_INIT_INDICES", "" } }, false ) );

        // wait until shaders are compiled! this allows for parallel compilation but ensures all are compiled after this point
        for( auto sh : allShaders ) sh->WaitFinishIfBackgroundCreateActive();
    }

    // Was: "Update master constant buffer"; Now it's only for validation/asserting.
    {
        uint32_t NumThreadgroupsToRun;
        uint32_t NumReducedThreadgroupsToRun;
        const uint maxNumThreadgroups = 800;
        
        FFX_ParallelSortCB consts;
        FFX_ParallelSort_SetConstantAndDispatchData( keyCount, maxNumThreadgroups, consts, NumThreadgroupsToRun, NumReducedThreadgroupsToRun);
        //m_constantBuffer->Upload( renderContext, consts );
        //m_constantBuffer->UploadSingle( renderContext, consts, 0 );
        assert(NumReducedThreadgroupsToRun < FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE && "Need to account for bigger reduced histogram scan");
    }

    {
        // key count goes here
        if( actualNumberOfKeysToSort != nullptr )
            m_constantBuffer->CopyFrom( renderContext, *actualNumberOfKeysToSort, 0, actualNumberOfKeysToSortByteOffset, 4 );
        else
            m_constantBuffer->Upload( renderContext, &keyCount, 0, 4 );

        vaComputeItem computeItem;
        computeItem.ComputeShader = m_CSSetupIndirect;
        computeItem.SetDispatch( 1 );
        renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs(m_constantBuffer, m_dispatchIndirectBuffer), nullptr );
    }

    vaComputeItem computeItem;
    computeItem.ConstantBuffers[0] = m_constantBuffer;
    //computeItem.ShaderResourceViews[2] = m_constantBuffer;

    // for ping-ponging
    shared_ptr<vaRenderBuffer> ReadIndicesBufferInfo( sortedIndices ), WriteIndicesBufferInfo( m_scratchIndicesBuffer );

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
        computeItem.ShaderResourceViews[0]  = keys;
        computeItem.ShaderResourceViews[1]  = ReadIndicesBufferInfo;

        vaRenderOutputs uavInputsOutputs;
        uavInputsOutputs.UnorderedAccessViews[0] = m_scratchBuffer;
        uavInputsOutputs.UnorderedAccessViews[1] = m_reducedScratchBuffer;
        uavInputsOutputs.UnorderedAccessViews[2] = nullptr;//WriteBufferInfo;
        uavInputsOutputs.UnorderedAccessViews[3] = WriteIndicesBufferInfo;

        renderContext.BeginComputeItems( uavInputsOutputs, nullptr );

        {
#ifdef DETAILED_GPU_PROFILING
            VA_TRACE_CPUGPU_SCOPE( Count, renderContext );
#endif
            computeItem.ComputeShader = (Shift==0 && resetIndices)?m_CSFirstPassCount:m_CSCount;
            //computeItem.SetDispatch( NumThreadgroupsToRun );
            computeItem.SetDispatchIndirect( m_dispatchIndirectBuffer, 0*4 );
            drawResults |= renderContext.ExecuteItem( computeItem );
        }

        {
#ifdef DETAILED_GPU_PROFILING
            VA_TRACE_CPUGPU_SCOPE( CountReduce, renderContext );
#endif
            computeItem.ComputeShader = m_CSCountReduce;
            //computeItem.SetDispatch( NumReducedThreadgroupsToRun );
            computeItem.SetDispatchIndirect( m_dispatchIndirectBuffer, 4*4 );
            drawResults |= renderContext.ExecuteItem( computeItem );
        }
        
        {
#ifdef DETAILED_GPU_PROFILING
            VA_TRACE_CPUGPU_SCOPE( ScanPrefix, renderContext );
#endif
            computeItem.ComputeShader = m_CSScanPrefix;
            computeItem.SetDispatch( 1 );
            drawResults |= renderContext.ExecuteItem( computeItem );
        }

        {
#ifdef DETAILED_GPU_PROFILING
            VA_TRACE_CPUGPU_SCOPE( ScanAdd, renderContext );
#endif
            computeItem.ComputeShader = m_CSScanAdd;
            //computeItem.SetDispatch( NumReducedThreadgroupsToRun );
            computeItem.SetDispatchIndirect( m_dispatchIndirectBuffer, 4*4 );
            drawResults |= renderContext.ExecuteItem( computeItem );
        }

        {
#ifdef DETAILED_GPU_PROFILING
            VA_TRACE_CPUGPU_SCOPE( Scatter, renderContext );
#endif
            computeItem.ComputeShader = (Shift==0 && resetIndices)?m_CSFirstPassScatter:m_CSScatter;
            //computeItem.SetDispatch( NumThreadgroupsToRun );
            computeItem.SetDispatchIndirect( m_dispatchIndirectBuffer, 0*4 );
            drawResults |= renderContext.ExecuteItem( computeItem );
        }

        renderContext.EndItems( );

        // ping-pong
        std::swap(ReadIndicesBufferInfo, WriteIndicesBufferInfo);
    }
#else
    VA_ERROR( "vaGPUSort not enabled!" );
    drawResults = vaDrawResultFlags::UnspecifiedError;
#endif

    static bool validate = false;
    shared_ptr<vaRenderBuffer> downloadBufferIndices  = nullptr;
    shared_ptr<vaRenderBuffer> downloadBufferKeys     = nullptr;
    if( validate )
    {
        uint maxPaths = (uint)sortedIndices->GetElementCount();
        downloadBufferIndices  = vaRenderBuffer::Create( GetRenderDevice(), maxPaths, vaResourceFormat::R32_UINT, vaRenderBufferFlags::Readback, "ValidationDownloadIndices" );
        downloadBufferKeys     = vaRenderBuffer::Create( GetRenderDevice(), maxPaths+1, vaResourceFormat::R32_UINT, vaRenderBufferFlags::Readback, "ValidationDownloadKeys" );

        // must copy keys here before they get scrambled below in the old version
        downloadBufferKeys   ->CopyFrom( renderContext, *keys, 0, 0 );
        downloadBufferIndices->CopyFrom( renderContext, *sortedIndices, 0, 0 );

        renderContext.Flush( );                     // submit all work on render context including the copies above
        renderContext.GetRenderDevice().SyncGPU( ); // sync (wait for the GPU to finish with it all before we can read)!

        uint * indexArr = reinterpret_cast<uint32*>( downloadBufferIndices->GetMappedData( ) );
        uint * keyArr = reinterpret_cast<uint32*>( downloadBufferKeys->GetMappedData( ) );
        uint smallestKey = 0;
        
        // path tracer debugging
        // uint totalActivePaths = keyArr[maxPaths]; // this is where we store currently active path count - just a convenient place, not a key
        // totalActivePaths;

        // uint removed = 0;
        // for( uint i = 0; i < maxPaths; i++ )
        // {
        //     if( keyArr[i] == 0xFFFFFFFF )
        //         removed++;
        // }

        for( uint i = 0; i < maxPaths; i++ )
        {
            uint key = keyArr[indexArr[i]];
            // this ensures indices are sorted from min to max key
            assert( smallestKey <= key );       
            smallestKey = key;

            // path tracer debugging
            // // this ensures inactive paths are correctly counted
            // if( key == 0xFFFFFFFF )
            // { assert( i >= totalActivePaths ); }
            // if( i>= totalActivePaths )
            // { assert( key == 0xFFFFFFFF ); }
        }
    }

    return drawResults;
}



