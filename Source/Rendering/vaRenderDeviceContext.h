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

#include "Rendering/vaRendering.h"
#include "Rendering/vaRenderDevice.h"

#include "Rendering/vaTexture.h"

#include "Rendering/vaRenderBuffers.h"
#include "Rendering/Shaders/vaPostProcessShared.h"

namespace Vanilla
{
    class vaRenderDevice;
    class vaGPUContextTracer;
    
    enum class vaRenderTypeFlags : uint32
    {
        None                        = 0,
        Graphics                    = ( 1 << 0 ),
        Compute                     = ( 1 << 1 ),
        Raytrace                    = ( 1 << 2 ),
    };
    BITFLAG_ENUM_CLASS_HELPER( vaRenderTypeFlags );

    enum class vaExecuteItemFlags : uint32
    {
        None                            = 0,
        ShadersUnchanged                = ( 1 << 0 ),       // this means all shaders are the same as in the previous ExecuteItem call - this is purely an optimization flag; if shaders did change the behaviour is undefined
    };
    BITFLAG_ENUM_CLASS_HELPER( vaExecuteItemFlags );


    // vaRenderDeviceContext is used to get/set current render targets and access rendering API stuff like contexts, etc.
    class vaRenderDeviceContext : public vaRenderingModule
    {
    protected:

        // (optionally) captured at BeginItems and released at EndItems
        // vaDrawAttributes *                m_currentSceneDrawContext                           = nullptr;
        vaRenderTypeFlags                   m_itemsStarted                  = vaRenderTypeFlags::None;
        // vaShaderItemGlobals                 m_currentShaderItemGlobals;

        shared_ptr<vaGPUContextTracer>      m_tracer;

        const bool                          m_isWorkerContext;          // these are not for direct use, used to encapsulate functionality for multithreading
        const int                           m_instanceIndex;            // each device can have multiple contexts and each has unique index from 0 to vaRenderDevice::GetContextCount()-1

        // how many items can be handled between BeginGraphicsItems/EndGraphicsItems; ExecuteGraphicsItemsConcurrent does not have this limit and will automatically split up the work 
        static const int                    c_maxItemsPerBeginEnd       = 131072;

#ifdef VA_SCOPE_TRACE_ENABLED
        char                                m_manualTraceStorage[sizeof(vaScopeTrace)*2];
        vaScopeTraceStaticPart              m_frameBeginEndTraceStaticPart;
        vaScopeTrace *                      m_frameBeginEndTrace = nullptr;
        vaScopeTraceStaticPart              m_framePresentTraceStaticPart;
        vaScopeTrace *                      m_framePresentTrace = nullptr;
#endif

        weak_ptr<vaRenderDeviceContext> const    
                                            m_master;
        vaRenderDeviceContext* const        m_masterPtr;    // direct pointer to above - debug build will confirm it's valid in GetMaster

        vaRenderOutputs                     m_currentOutputs;

    protected:

    protected:
        vaRenderDeviceContext( vaRenderDevice & renderDevice, const shared_ptr<vaRenderDeviceContext> & master, int instanceIndex );
    
    public:
        virtual ~vaRenderDeviceContext( );
        //
    public:
        // these are not for direct use, used to encapsulate functionality for multithreading
        // a worker context is spawned by the master context in certain use cases
        bool                                IsWorker( ) const           { return m_isWorkerContext; }
        // a worker context must be able to return its master context; all others should always return nullptr
        vaRenderDeviceContext *             GetMaster( ) const          { if( IsWorker() ) { assert( m_master.lock().get() == m_masterPtr ); } return m_masterPtr; }

        // each device can have multiple contexts and each has unique index from 0 to vaRenderDevice::GetContextCount()-1
        int                                 GetInstanceIndex( ) const   { return m_instanceIndex; }

        virtual void                        BeginFrame( );
        virtual void                        EndFrame( )  ;
        virtual void                        PostPresent( );             // called after EndFrame, and after Present was performed

        // platform-independent way of drawing items; it is immediate and begin/end items is there mostly for state caching; 
        // when present, 'drawAttributes' parameter represents global states like lighting; these are same for all items between BeginItems/EndItems
        virtual void                        BeginGraphicsItems( const vaRenderOutputs & renderOutputs, const vaDrawAttributes * drawAttributes );
        virtual void                        BeginComputeItems( const vaRenderOutputs & renderOutputs, const vaDrawAttributes * drawAttributes );
        virtual void                        BeginRaytraceItems( const vaRenderOutputs & renderOutputs, const vaDrawAttributes * drawAttributes );
        virtual vaDrawResultFlags           ExecuteItem( const vaGraphicsItem & renderItem, vaExecuteItemFlags flags = vaExecuteItemFlags::None ) = 0;
        virtual vaDrawResultFlags           ExecuteItem( const vaComputeItem & computeItem, vaExecuteItemFlags flags = vaExecuteItemFlags::None ) = 0;
        virtual vaDrawResultFlags           ExecuteItem( const vaRaytraceItem & raytraceItem, vaExecuteItemFlags flags = vaExecuteItemFlags::None ) = 0;
        virtual void                        EndItems( )                                                                                     { assert( m_itemsStarted != vaRenderTypeFlags::None ); m_itemsStarted = vaRenderTypeFlags::None; }
        vaDrawResultFlags                   ExecuteSingleItem( const vaGraphicsItem & renderItem, const vaRenderOutputs & renderOutputs, const vaDrawAttributes * drawAttributes );
        vaDrawResultFlags                   ExecuteSingleItem( const vaComputeItem & computeItem, const vaRenderOutputs & renderOutputs, const vaDrawAttributes * drawAttributes );
        vaDrawResultFlags                   ExecuteSingleItem( const vaRaytraceItem & raytraceItem, const vaRenderOutputs & renderOutputs, const vaDrawAttributes * drawAttributes );

        // way to draw a number of items in a multithreaded way; ordering is maintained; if threading not implemented, default will work just with BeginGraphicsItems/ExecuteItem/EndItems
        typedef std::function<vaDrawResultFlags( int index, vaRenderDeviceContext & renderContext )> GraphicsItemCallback;
        virtual vaDrawResultFlags           ExecuteGraphicsItemsConcurrent( int itemCount, const vaRenderOutputs & renderOutputs, const vaDrawAttributes * drawAttributes, const GraphicsItemCallback & callback );

        virtual vaRenderTypeFlags           GetSupportFlags( ) const                                                                        { return vaRenderTypeFlags::Graphics | vaRenderTypeFlags::Compute | (GetRenderDevice().GetCapabilities().Raytracing.Supported?(vaRenderTypeFlags::Raytrace):(vaRenderTypeFlags::None)); }
        virtual vaRenderTypeFlags           GetStartedFlags( ) const                                                                        { return m_itemsStarted; }

        //// Helper to fill in vaGraphicsItem with most common elements needed to render a fullscreen quad (or triangle, whatever) - vertex shader, vertex buffer, etc
        //void                                FillFullscreenPassGraphrItem( vaGraphicsItem & renderItem, bool zIs0 = true ) const             { GetRenderDevice().FillFullscreenPassGraphicsItem( renderItem, zIs0 ); }

        inline const shared_ptr<vaGPUContextTracer>& GetTracer( ) const { return m_tracer; }

    protected:
        virtual void                        BeginItems( vaRenderTypeFlags typeFlags, const vaRenderOutputs * , const vaShaderItemGlobals &  )            { assert( m_itemsStarted == vaRenderTypeFlags::None ); assert( typeFlags != vaRenderTypeFlags::None ); m_itemsStarted = typeFlags; };


    public:
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // useful for copying individual MIPs, in which case use Views created with vaTexture::CreateView
        virtual vaDrawResultFlags           CopySRVToRTV( shared_ptr<vaTexture> destination, shared_ptr<vaTexture> source );
        //
        // Copies srcTexture into dstTexture with stretching using requested filter and blend modes.
        virtual vaDrawResultFlags           StretchRect( const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture, const vaVector4 & dstRect = {0,0,0,0}, const vaVector4 & srcRect = {0,0,0,0}, bool linearFilter = true, vaBlendMode blendMode = vaBlendMode::Opaque, const vaVector4 & colorMul = vaVector4( 1.0f, 1.0f, 1.0f, 1.0f ), const vaVector4 & colorAdd = vaVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    };

    inline vaDrawResultFlags vaRenderDeviceContext::ExecuteSingleItem( const vaGraphicsItem& renderItem, const vaRenderOutputs& renderOutputs, const vaDrawAttributes* drawAttributes ) 
    { 
        assert( m_itemsStarted == vaRenderTypeFlags::None ); 
        assert( !IsWorker() );
        BeginGraphicsItems( renderOutputs, drawAttributes ); 
        vaDrawResultFlags drawResults = ExecuteItem( renderItem ); 
        EndItems( ); 
        assert( m_itemsStarted == vaRenderTypeFlags::None ); 
        return drawResults; 
    }
    inline vaDrawResultFlags vaRenderDeviceContext::ExecuteSingleItem( const vaComputeItem& computeItem, const vaRenderOutputs & renderOutputs, const vaDrawAttributes* drawAttributes ) 
    { 
        assert( m_itemsStarted == vaRenderTypeFlags::None ); 
        assert( !IsWorker() );
        BeginComputeItems( renderOutputs, drawAttributes ); 
        vaDrawResultFlags drawResults = ExecuteItem( computeItem ); 
        EndItems( ); 
        assert( m_itemsStarted == vaRenderTypeFlags::None ); 
        return drawResults; 
    }
    inline vaDrawResultFlags vaRenderDeviceContext::ExecuteSingleItem( const vaRaytraceItem& raytraceItem, const vaRenderOutputs & renderOutputs, const vaDrawAttributes* drawAttributes ) 
    { 
        assert( m_itemsStarted == vaRenderTypeFlags::None ); 
        assert( !IsWorker() );
        BeginRaytraceItems( renderOutputs, drawAttributes ); 
        vaDrawResultFlags drawResults = ExecuteItem( raytraceItem ); 
        EndItems( ); 
        assert( m_itemsStarted == vaRenderTypeFlags::None ); 
        return drawResults; 
    }


}