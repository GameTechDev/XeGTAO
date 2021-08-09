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


using namespace Vanilla;

vaGPUContextTracerParams::vaGPUContextTracerParams( vaRenderDevice & renderDevice, vaRenderDeviceContext & renderDeviceContext ) : RenderDeviceContext( renderDeviceContext ), vaRenderingModuleParams( renderDevice ) { }

vaGPUContextTracer::vaGPUContextTracer( const vaGPUContextTracerParams & params )
    : vaRenderingModule( params ),
    m_renderContext( params.RenderDeviceContext )
{
    m_threadContext = vaTracer::CreateVirtualThreadContext( ( string(c_threadNamePrefix) + " " + GetRenderDevice( ).GetAdapterNameShort( ) + "' main context" ).c_str( ), true );
}
