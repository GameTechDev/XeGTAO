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

#include "Core/vaCoreIncludes.h"

#include "Rendering/vaRenderingIncludes.h"

#include <vector>

namespace Vanilla
{
    struct vaGPUContextTracerParams : vaRenderingModuleParams
    {
        vaRenderDeviceContext & RenderDeviceContext;
        vaGPUContextTracerParams( vaRenderDevice & renderDevice, vaRenderDeviceContext & renderDeviceContext );
    };

    class vaGPUContextTracer : public vaRenderingModule
    {
    public:
        static const int                            c_maxTraceCount     = 8192;     // max traces per frame
        static constexpr const char *               c_threadNamePrefix  = "!!GPU";

    protected:
        vaRenderDeviceContext &                     m_renderContext;

        bool                                        m_active            = false;
        shared_ptr<vaTracer::ThreadContext>         m_threadContext     = nullptr;

    protected:

    protected:
        vaGPUContextTracer( const vaGPUContextTracerParams & params );
    public:
        virtual ~vaGPUContextTracer( ) { }

    public:
        virtual int                                 Begin( vaMappedString name, int subID ) = 0;
        // enable this if you wish, but it's costly - few tens per frame is ok, more - not sure
        // int                                         Begin( const string & name, int subID )            { return Begin( vaCore::MapString( name ), int subID ); }
        virtual void                                End( int handle )                       = 0;

    protected:
        friend class vaRenderDeviceContext;
        virtual void                                BeginFrame( )                           = 0;
        virtual void                                EndFrame( )                             = 0;
    };
}
