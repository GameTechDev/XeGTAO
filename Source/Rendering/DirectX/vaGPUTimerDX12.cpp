///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Rendering/DirectX/vaGPUTimerDX12.h"

#include "Rendering/DirectX/vaRenderDeviceContextDX12.h"
#include "Rendering/DirectX/vaRenderBuffersDX12.h"

#include <algorithm>

#ifdef VA_USE_PIX3
#define USE_PIX
#pragma warning ( push )
#pragma warning ( disable: 4100 )
#include <pix3.h>
#pragma warning ( pop )
#pragma comment(lib, "WinPixEventRuntime.lib")
#endif

// not included or enabled by default
// #define USE_RGP

#ifdef USE_RGP
#include <AMDDxExt\AmdPix3.h>
#endif


using namespace Vanilla;

vaGPUContextTracerDX12::vaGPUContextTracerDX12( const vaRenderingModuleParams& params ) : vaGPUContextTracer( vaSaferStaticCast< const vaGPUContextTracerParams &, const vaRenderingModuleParams &>( params ) )
{
    vaRenderDeviceDX12 & device = AsDX12( GetRenderDevice() );
    D3D12_QUERY_HEAP_DESC queryHeapDesc;
    queryHeapDesc.Count = vaGPUContextTracer::c_maxTraceCount * 2;
    queryHeapDesc.NodeMask = 0;
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    device.GetPlatformDevice( )->CreateQueryHeap( &queryHeapDesc, IID_PPV_ARGS( &m_queryHeap ) );
    m_queryHeap->SetName( L"vaGPUTimerManagerDX12_QueryHeap" );

    for( int i = 0; i < _countof( m_queryReadbackBuffers ); i++ )
    {
        m_queryReadbackBuffers[i] = vaRenderBuffer::Create<uint64>( m_renderDevice, vaGPUContextTracer::c_maxTraceCount * 2, vaRenderBufferFlags::Readback, nullptr, "GPUContextTracerReadback" );
        m_currentTraceIndex[i] = 0;
    }
}

vaGPUContextTracerDX12::~vaGPUContextTracerDX12( )
{

}

void vaGPUContextTracerDX12::BeginFrame( )
{
//#ifdef VA_USE_PIX3
//    PIXBeginEvent( AsDX12( m_renderContext.GetRenderDevice() ).GetCommandQueue().Get(), (UINT64)PIX_COLOR_INDEX( 1 ), "MainContextFrame" );
//#endif

    assert( m_recursionDepth == 0 );
    if( m_GPUTimestampFrequency == 0 )
    {
        uint64 CPUTimestampAtSync = 0;
        AsDX12( GetRenderDevice() ).GetCommandQueue()->GetTimestampFrequency( &m_GPUTimestampFrequency );
        AsDX12( GetRenderDevice() ).GetCommandQueue()->GetClockCalibration( &m_GPUTimestampAtSync, &CPUTimestampAtSync );

        m_CPUTimeAtSync = (CPUTimestampAtSync - vaCore::NativeAppStartTime()) / double(vaCore::NativeTimerFrequency());
    }

    assert( GetRenderDevice( ).IsRenderThread( ) );
    assert( !m_active );
    m_currentTraceIndex[m_currentBufferSet] = 0;
    m_active = true;

    //m_wholeFrameTraceHandle = Begin( "WholeFrame" );
}

void vaGPUContextTracerDX12::EndFrame( )
{
    assert( GetRenderDevice( ).IsRenderThread( ) );
    assert( m_active );
    //End( m_wholeFrameTraceHandle ); m_wholeFrameTraceHandle = -1;
    assert( m_recursionDepth == 0 );

    m_active = false;

    // ok, we've done submitting all queries for this frame; now issue the copy-to-buffer command so we can read it in the future
    AsDX12( m_renderContext ).GetCommandList( )->ResolveQueryData( m_queryHeap.Get( ), D3D12_QUERY_TYPE_TIMESTAMP, 0, m_currentTraceIndex[m_currentBufferSet] * 2, AsDX12(*m_queryReadbackBuffers[m_currentBufferSet]).GetResource( ), 0 );

    // ok, move to next
    m_currentBufferSet = ( m_currentBufferSet + 1 ) % c_bufferCount;

    // first get the old resolved data and update 
    if( m_currentTraceIndex[m_currentBufferSet] > 0 )   // only copy if there's anything to copy
    {
        // we should always be able to map this
        //if( m_queryReadbackBuffers[m_currentBufferSet]->Map( vaResourceMapType::Read ) )
        {
            uint64* data = reinterpret_cast<uint64*>( m_queryReadbackBuffers[m_currentBufferSet]->GetMappedData( ) );

            vaTracer::Entry entries[ c_maxTraceCount ];

            uint64 prevTimestamp = 0;
            const int entryCount    = m_currentTraceIndex[m_currentBufferSet];
            int outEntryCount = 0;
            for( int i = 0; i < entryCount; i++ )
            {
                uint64 beginTimestamp   = data[i*2+0];
                uint64 endTimestamp     = data[i*2+1];
                if(    (beginTimestamp == 0 )
                    || (endTimestamp == 0 )
                    // || (beginTimestamp == endTimestamp )         // these are totally legal so let them be
                    || (endTimestamp < beginTimestamp) 
                    || (prevTimestamp > beginTimestamp) 
                    || (beginTimestamp <= m_GPUTimestampAtSync)
                    )
                {
                    //assert( false );
                    continue;
                }

                prevTimestamp = beginTimestamp;

                vaTracer::Entry & entry = entries[outEntryCount];
                outEntryCount++;

                entry.Name      = m_traceEntries[i][m_currentBufferSet].Name;
                entry.Beginning = m_CPUTimeAtSync + ( beginTimestamp - m_GPUTimestampAtSync ) / double( m_GPUTimestampFrequency );
                entry.End       = m_CPUTimeAtSync + ( endTimestamp - m_GPUTimestampAtSync ) / double( m_GPUTimestampFrequency );
                entry.Depth     = m_traceEntries[i][m_currentBufferSet].Depth;
                entry.SubID     = m_traceEntries[i][m_currentBufferSet].SubID;
            }

            //m_queryReadbackBuffers[m_currentBufferSet]->Unmap(  );

            m_threadContext->BatchAddFrame( entries, outEntryCount );
        }
        //else
        //{
        //    VA_WARN( "Unable to map query resolve data" );
        //}

    }
    m_currentTraceIndex[m_currentBufferSet] = 0; // reset, free to start collecting again

//#ifdef VA_USE_PIX3
//    PIXEndEvent( AsDX12( m_renderContext ).GetCommandList( ).Get( ) ); // "MainContextFrame"
//#endif
}

int vaGPUContextTracerDX12::Begin( vaMappedString name, int subID ) 
{ 
    assert( m_recursionDepth >= 0 );
    assert( name != nullptr );

    int currentIndex = m_currentTraceIndex[m_currentBufferSet];
    assert( currentIndex < c_maxTraceCount );
    if( currentIndex >= c_maxTraceCount  )
        return -1;

    m_currentTraceIndex[m_currentBufferSet]++;  // increment for the future

    m_traceEntries[currentIndex][m_currentBufferSet] = { name, subID, m_recursionDepth };

#ifdef VA_USE_PIX3
    PIXBeginEvent( AsDX12( m_renderContext ).GetCommandList( ).Get(), PIX_COLOR_INDEX(subID % 0xFF), (const char*)name );
#endif

    AsDX12( m_renderContext ).GetCommandList( )->EndQuery( m_queryHeap.Get( ), D3D12_QUERY_TYPE_TIMESTAMP, currentIndex * 2 + 0 );

    m_recursionDepth++;

    return currentIndex;
}


void vaGPUContextTracerDX12::End( int currentIndex )
{ 
    assert( m_recursionDepth >= 0 );
    if( !( currentIndex >= 0 && currentIndex < c_maxTraceCount ) )
        { assert( false );  return; }
    m_recursionDepth--;

    AsDX12( m_renderContext ).GetCommandList( )->EndQuery( m_queryHeap.Get( ), D3D12_QUERY_TYPE_TIMESTAMP, currentIndex * 2 + 1 );

#ifdef VA_USE_PIX3
    PIXEndEvent( AsDX12( m_renderContext ).GetCommandList( ).Get( ) );
#endif
}
