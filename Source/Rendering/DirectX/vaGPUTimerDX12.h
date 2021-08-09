///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Rendering/vaGPUTimer.h"

#include "Rendering/DirectX/vaDirectXIncludes.h"
#include "Rendering/DirectX/vaDirectXTools.h"

#include <vector>

namespace Vanilla
{
    class vaRenderBufferDX12;

    class vaGPUContextTracerDX12 : public vaGPUContextTracer
    {
    protected:
        static const int                            c_bufferCount           = vaRenderDevice::c_BackbufferCount + 1;

        struct TraceEntry
        {
            vaMappedString  Name;
            int             SubID;
            int             Depth;
        };

        TraceEntry                                  m_traceEntries[vaGPUContextTracer::c_maxTraceCount][c_bufferCount];

        ComPtr<ID3D12QueryHeap>                     m_queryHeap;
        shared_ptr<vaRenderBuffer>                     m_queryReadbackBuffers[c_bufferCount];

        int                                         m_currentTraceIndex[c_bufferCount];

        int                                         m_currentBufferSet      = 0;

        double                                      m_CPUTimeAtSync         = 0;
        uint64                                      m_GPUTimestampAtSync    = 0;
        uint64                                      m_GPUTimestampFrequency = 0;

        int                                         m_recursionDepth        = 0;

        //int                                         m_wholeFrameTraceHandle = -1;

    public:
        vaGPUContextTracerDX12( const vaRenderingModuleParams & params );
        virtual ~vaGPUContextTracerDX12( );

    protected:
        virtual void                                BeginFrame( ) override;
        virtual void                                EndFrame( ) override;

    public:
        virtual int                                 Begin( vaMappedString name, int subID ) override;
        virtual void                                End( int handle ) override;
    };

}
