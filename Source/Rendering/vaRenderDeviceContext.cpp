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

#include "vaRenderDeviceContext.h"

#include "Core/System/vaFileTools.h"

#include "Rendering/vaShader.h"
#include "Rendering/vaRenderBuffers.h"
#include "Rendering/vaRenderMaterial.h"
#include "Rendering/vaSceneLighting.h"
#include "Rendering/vaSceneRaytracing.h"
#include "Rendering/vaRenderGlobals.h"

#include "Rendering/vaGPUTimer.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"

using namespace Vanilla;

vaRenderDeviceContext::vaRenderDeviceContext( vaRenderDevice & renderDevice, const shared_ptr<vaRenderDeviceContext> & master, int instanceIndex ) 
    : vaRenderingModule( renderDevice ), m_isWorkerContext( master != nullptr ), m_instanceIndex( instanceIndex ), m_master( master ), m_masterPtr( master.get() )
#ifdef VA_SCOPE_TRACE_ENABLED
    , m_frameBeginEndTraceStaticPart( "GPUFrame", false )
    , m_framePresentTraceStaticPart( "Present", false )
#endif
{ 
    if( !m_isWorkerContext )
        m_tracer = renderDevice.CreateModule< vaGPUContextTracer, vaGPUContextTracerParams >( *this );
}

vaRenderDeviceContext::~vaRenderDeviceContext( )
{
    assert( m_itemsStarted == vaRenderTypeFlags::None );
#ifdef VA_SCOPE_TRACE_ENABLED
    assert( m_frameBeginEndTrace == nullptr );
    assert( m_framePresentTrace == nullptr );
#endif
}

void vaRenderDeviceContext::BeginFrame( ) 
{ 
    assert( m_itemsStarted == vaRenderTypeFlags::None );

    if( m_tracer != nullptr )
    {
        m_tracer->BeginFrame( ); 
#ifdef VA_SCOPE_TRACE_ENABLED
        m_frameBeginEndTrace = new(&m_manualTraceStorage[0]) vaScopeTrace( m_frameBeginEndTraceStaticPart, this );
#endif
    }
}
void vaRenderDeviceContext::EndFrame( ) 
{
    m_currentOutputs.Reset();
    if( m_tracer != nullptr )
    {
#ifdef VA_SCOPE_TRACE_ENABLED
        m_framePresentTrace = new( &m_manualTraceStorage[sizeof(vaScopeTrace)] ) vaScopeTrace( m_framePresentTraceStaticPart, this );
#endif
    }
}

void vaRenderDeviceContext::PostPresent( )
{
    assert( m_itemsStarted == vaRenderTypeFlags::None );
    if( m_tracer != nullptr )
    {
#ifdef VA_SCOPE_TRACE_ENABLED
        m_framePresentTrace->~vaScopeTrace( ); m_framePresentTrace = nullptr;
        m_frameBeginEndTrace->~vaScopeTrace( ); m_frameBeginEndTrace = nullptr;
#endif
        m_tracer->EndFrame( );
    }
}

static void UpdateRenderItemGlobals( vaRenderDeviceContext & renderContext, vaShaderItemGlobals & shaderGlobals, const vaDrawAttributes * drawAttributes, vaRenderTypeFlags renderTypeFlags )
{
    //VA_TRACE_CPU_SCOPE( UpdateRenderItemGlobals );

    if( drawAttributes != nullptr )
        shaderGlobals = drawAttributes->BaseGlobals;

    // these two actually part-work without drawAttributes (at the moment)!
    renderContext.GetRenderDevice( ).GetMaterialManager( ).UpdateAndSetToGlobals( renderContext, shaderGlobals, drawAttributes );
    renderContext.GetRenderDevice( ).GetMeshManager( ).UpdateAndSetToGlobals( /*renderContext,*/ shaderGlobals/*, *drawAttributes*/ );

    if( drawAttributes == nullptr )
    { 
        // assert here means error: raytracing can't work without draw attributes
        assert( ( renderTypeFlags & vaRenderTypeFlags::Raytrace ) == 0 ); 
    }
    else
    {
        if( drawAttributes->Lighting != nullptr )
            drawAttributes->Lighting->UpdateAndSetToGlobals( renderContext, shaderGlobals, *drawAttributes );

        //assert( (drawAttributes->Raytracing != nullptr) == (( renderTypeFlags & vaRenderTypeFlags::Raytrace ) != 0) );  // setting up raytracing globals during Graphics calls is not tested and might not work! first test and then remove this assert
        if( ( renderTypeFlags & vaRenderTypeFlags::Raytrace ) != 0 )
            drawAttributes->Raytracing->UpdateAndSetToGlobals( renderContext, shaderGlobals, *drawAttributes );
    }

    renderContext.GetRenderDevice( ).GetRenderGlobals( ).UpdateAndSetToGlobals( renderContext, shaderGlobals, drawAttributes );
}

void vaRenderDeviceContext::BeginGraphicsItems( const vaRenderOutputs & renderOutputs, const vaDrawAttributes* drawAttributes )
{
    vaRenderTypeFlags renderTypeFlags = vaRenderTypeFlags::Graphics;

    vaShaderItemGlobals shaderGlobals;
    UpdateRenderItemGlobals( *this, shaderGlobals, drawAttributes, renderTypeFlags );

    BeginItems( renderTypeFlags, &renderOutputs, shaderGlobals );
}

void vaRenderDeviceContext::BeginComputeItems( const vaRenderOutputs & renderOutputs, const vaDrawAttributes * drawAttributes )
{
    vaRenderTypeFlags renderTypeFlags = vaRenderTypeFlags::Compute;

    vaShaderItemGlobals shaderGlobals;
    UpdateRenderItemGlobals( *this, shaderGlobals, drawAttributes, renderTypeFlags );

    BeginItems( renderTypeFlags, &renderOutputs, shaderGlobals );
}

void vaRenderDeviceContext::BeginRaytraceItems( const vaRenderOutputs & renderOutputs, const vaDrawAttributes* drawAttributes )
{
    vaRenderTypeFlags renderTypeFlags = vaRenderTypeFlags::Compute | vaRenderTypeFlags::Raytrace;

    vaShaderItemGlobals shaderGlobals;
    UpdateRenderItemGlobals( *this, shaderGlobals, drawAttributes, renderTypeFlags );

    BeginItems( renderTypeFlags, &renderOutputs, shaderGlobals );
}

vaDrawResultFlags vaRenderDeviceContext::ExecuteGraphicsItemsConcurrent( int itemCount, const vaRenderOutputs& renderOutputs, const vaDrawAttributes* drawAttributes, const GraphicsItemCallback & callback )
{
    vaDrawResultFlags ret = vaDrawResultFlags::None;
    int batchCount = (itemCount+c_maxItemsPerBeginEnd-1)/c_maxItemsPerBeginEnd;
    for( int batch = 0; batch < batchCount; batch++ )
    {
        const int batchItemFrom = batch * c_maxItemsPerBeginEnd; const int batchItemCount = std::min( itemCount-batchItemFrom, c_maxItemsPerBeginEnd );

        BeginGraphicsItems( renderOutputs, drawAttributes );

        for( int i = batchItemFrom; i < batchItemFrom+batchItemCount; i++ )
        {
            ret |= callback( i, *this );
        }

        EndItems();
    }

    return ret;
}


// useful for copying individual MIPs, in which case use Views created with vaTexture::CreateView
vaDrawResultFlags vaRenderDeviceContext::CopySRVToRTV( shared_ptr<vaTexture> destination, shared_ptr<vaTexture> source )
{
    assert( m_itemsStarted == vaRenderTypeFlags::None );
    assert( !IsWorker() );
    return GetRenderDevice( ).CopySRVToRTV( *this, destination, source );
}

vaDrawResultFlags vaRenderDeviceContext::StretchRect( const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture, const vaVector4 & _dstRect, const vaVector4 & srcRect, bool linearFilter, vaBlendMode blendMode, const vaVector4 & colorMul, const vaVector4 & colorAdd )
{
    assert( m_itemsStarted == vaRenderTypeFlags::None );
    assert( !IsWorker() );
    return GetRenderDevice( ).StretchRect( *this, dstTexture, srcTexture, _dstRect, srcRect, linearFilter, blendMode, colorMul, colorAdd );
}
