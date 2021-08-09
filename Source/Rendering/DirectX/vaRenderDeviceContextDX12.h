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

#include "Rendering/DirectX/vaRenderDeviceDX12.h"
#include "Rendering/DirectX/vaDirectXIncludes.h"
#include "Rendering/DirectX/vaDirectXTools.h"
#include "Rendering/DirectX/vaShaderDX12.h"

#include "Rendering/vaRenderingIncludes.h"

// search thread (worker) local PSO cache first to avoid shared_mutex
#define VA_DX12_USE_LOCAL_PSO_CACHE

namespace Vanilla
{
    // In DX12 case vaRenderDeviceContext encapsulates ID3D12CommandAllocator, ID3D12GraphicsCommandList, viewports and render target stuff
    // There's one "main" context for use from the main thread (vaThreading::IsMainThread) that is also used for buffer copies/updates,
    // UI and similar.
    //
    // vaRenderDeviceContextBaseDX12    - most common stuff
    // vaRenderDeviceContextDX12        - fully featured context, at this point only single-threaded support (main/render thread only)
    // vaRenderDeviceContextComputeDX12 - (future, not implemented) async compute context (main/render thread only)
    // vaRenderDeviceContextWorkerDX12  - used by the vaRenderDeviceContextDX12 to provide multithreaded capability
    //
    class vaRenderDeviceContextDX12;
    //
    class vaRenderDeviceContextBaseDX12 : public vaRenderDeviceContext
    {
    public:

    protected:
        bool const                          m_useBundles;

        vaRenderDeviceDX12 &                m_deviceDX12;

        ComPtr<ID3D12CommandAllocator>      m_commandAllocators[vaRenderDevice::c_BackbufferCount];

        ComPtr<ID3D12GraphicsCommandList5>  m_commandList;
        bool                                m_commandListReady                  = false;

        // avoid overloading the driver - don't change states if they're the same
        D3D_PRIMITIVE_TOPOLOGY              m_currentTopology                   = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        D3D12_SHADING_RATE                  m_currentShadingRate                = D3D12_SHADING_RATE_1X1;
        vaFramePtr<vaShaderResource>        m_currentVertexBuffer               = nullptr;
        vaFramePtr<vaShaderResource>        m_currentIndexBuffer                = nullptr;
        ID3D12PipelineState *               m_currentPSO                        = nullptr;

        // when there's reuse possible between draw calls, avoid re-filling the whole structure from 0
        vaGraphicsPSODescDX12               m_scratchPSODesc;

        // applies to both render and compute items
        const int                           c_flushAfterItemCount               = vaRenderDeviceContext::c_maxItemsPerBeginEnd;
        int                                 m_itemsSubmittedAfterLastExecute    = 0;

        vaRenderDeviceDX12::LocalGraphicsPSOCacheType
                                            m_localGraphicsPSOCache;

        // this is the one that's set as a root parameter
        int                                 m_nextTransientDesc_Globals         = -1;
        // these are just offsets of the ^above^, used for descriptor copying
        int                                 m_nextTransientDesc_GlobalUAVs      = -1;           // size is array_size(vaShaderItemGlobals::UnorderedAccessViews)
        int                                 m_nextTransientDesc_OutputsUAVs     = -1;           // size is vaRenderOutputs::c_maxUAVs
        int                                 m_nextTransientDesc_GlobalSRVs      = -1;           // size is array_size(vaShaderItemGlobals::ShaderResourceViews)

        /// TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP 
        class vaSceneRaytracingDX12 *       m_currentSceneRaytracing            = nullptr;
        /// TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP 

    protected:
                                            vaRenderDeviceContextBaseDX12( vaRenderDevice & device, const shared_ptr<vaRenderDeviceContextDX12> & master, int instanceIndex, bool useBundles );
        virtual                             ~vaRenderDeviceContextBaseDX12( );

    public:
        virtual void                        BeginGraphicsItems( const vaRenderOutputs & , const vaDrawAttributes * ) override { assert( false ); }
        virtual void                        BeginComputeItems( const vaRenderOutputs & , const vaDrawAttributes * ) override { assert( false ); }
        virtual void                        BeginRaytraceItems( const vaRenderOutputs &, const vaDrawAttributes * ) override { assert( false ); }

        virtual vaDrawResultFlags           ExecuteItem( const vaGraphicsItem & renderItem, vaExecuteItemFlags flags ) override;
        virtual vaDrawResultFlags           ExecuteItem( const vaComputeItem & renderItem, vaExecuteItemFlags flags ) override;
        virtual vaDrawResultFlags           ExecuteItem( const vaRaytraceItem & raytraceItem, vaExecuteItemFlags flags ) override;

        const ComPtr<ID3D12GraphicsCommandList5> & 
                                            GetCommandList( ) const                         { return m_commandList; }

        // This binds descriptor heaps, root signatures, viewports, scissor rects and render targets; useful if any external code messes with them
        void                                BindDefaultStates( );

        virtual void                        ExecuteAfterCurrentGPUFrameDone( const std::function<void( vaRenderDeviceDX12& device )>& callback ) { m_deviceDX12.ExecuteAfterCurrentGPUFrameDone( callback ); }
        
        vaRenderDeviceContextDX12 *         GetMasterDX12( ) const;

        // virtual std::pair< vaRenderDeviceDX12::TransientGPUDescriptorHeap *, int >
        //                                     AllocateSRVUAVHeapDescriptors( int numberOfDescriptors ); 

    protected:
        // Resource management (command allocators)
        virtual void                        BeginFrame( ) override;
        virtual void                        EndFrame( ) override;

        void                                ResetAndInitializeCommandList( int currentFrame );

        bool                                CommandListReady( ) const                       { return m_commandListReady; }

        // this can trigger Flush and GPUSync!
        virtual void                        PreAllocateTransientDescriptors( )              { assert( false ); }

        virtual void                        CommitOutputs( const vaRenderOutputs & outputs );
        virtual void                        CommitGlobals( vaRenderTypeFlags typeFlags, const vaShaderItemGlobals & shaderGlobals );
                void                        CommitTransientDescriptors( );

        // This if for use outside of BeginItems when one needs to be called to ensure D3D12 views are properly set and resources transitioned for
        // external rendering (like imgui)
        virtual void                        CommitOutputsRaw( vaRenderTypeFlags typeFlags, const vaRenderOutputs & outputs );

    public:
        // // Any time you access the internal command list (m_commandList/GetCommandList) and transition any resources manually, you 
        // // have to reset the outputs cache. If changing any other states, then you must reset everything with BindDefaultStates()
        // virtual void                        ResetCachedOutputs( ) = 0;
        void                                ResetCachedOutputs( )  { m_currentOutputs.Reset( ); }
    };

    // Worker context
    class vaRenderDeviceContextWorkerDX12 : public vaRenderDeviceContextBaseDX12
    {
    private:
        std::vector< std::function<void( vaRenderDeviceDX12& device )> >
                                        m_localGPUFrameFinishedCallbacks;

        // vaRenderDeviceDX12::TransientGPUDescriptorHeap *
        //                                     m_preAllocatedSRVUAVHeap        = nullptr;
        // int                                 m_preAllocatedSRVUAVHeapBase    = 0;
        // int                                 m_preAllocatedSRVUAVHeapCount   = 0;

        vaShaderItemGlobals                     m_deferredGlobals;
        bool                                    m_hasGlobals                = false;

        // bool                                m_globalsSet                    = false;        // stuff coming from vaShaderItemGlobals, if any
        // UINT                                m_globalRootDescTableParamIndex = 0;
        // D3D12_GPU_DESCRIPTOR_HANDLE         m_globalRootDescTableBaseDesc   = {0};
        // D3D12_GPU_VIRTUAL_ADDRESS           m_globalConstantBuffers[SHADERGLOBAL_CBV_SLOT_COUNT];


    protected:
        friend class vaRenderDeviceDX12;
        vaRenderDeviceContextWorkerDX12( vaRenderDevice & device, int instanceIndex, const shared_ptr<vaRenderDeviceContextDX12> & master, bool useBundles ) : vaRenderDeviceContextBaseDX12( device, master, instanceIndex, useBundles ) { }
    public:
        virtual                             ~vaRenderDeviceContextWorkerDX12( ) { assert( m_localGPUFrameFinishedCallbacks.size() == 0 ); }

    public:
        virtual vaRenderTypeFlags           GetSupportFlags( ) const override       { return vaRenderTypeFlags::Graphics | vaRenderTypeFlags::Compute; }

        // todo: if this becomes too expensive, consider executing it in a multithreaded fashion at the EndFrame
        // (might require change of interface or at least a way to warn people)
        virtual void                        ExecuteAfterCurrentGPUFrameDone( const std::function<void( vaRenderDeviceDX12& device )>& callback ) { m_localGPUFrameFinishedCallbacks.push_back( callback ); }

    protected:
        friend class vaRenderDeviceContextDX12;
        // Resource management (command allocators)
        virtual void                        BeginFrame( ) override;
        virtual void                        EndFrame( ) override;

        virtual void                        CommitOutputs( const vaRenderOutputs & outputs ) override;

        void                                DeferredSetGlobals( const vaShaderItemGlobals & globals )           { assert( !m_hasGlobals ); m_deferredGlobals = globals; m_hasGlobals = true;}

        // void                                SetGlobalRootDescTable( UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor )   { m_globalsSet = true; m_globalRootDescTableParamIndex = RootParameterIndex; m_globalRootDescTableBaseDesc = BaseDescriptor; }
        // void                                SetGlobalConstantBuffer( int index, const D3D12_GPU_VIRTUAL_ADDRESS & gpuAddress )              { m_globalConstantBuffers[index] = gpuAddress; }

        void                                PreWorkPrepareMainThread( int workItemCount );
        void                                PreWorkPrepareWorkerThread( int workItemCount );
        void                                PostWorkCleanupWorkerThread( );
        void                                PostWorkCleanupMainThread( );

        // virtual std::pair< vaRenderDeviceDX12::TransientGPUDescriptorHeap*, int >
        //                                     AllocateSRVUAVHeapDescriptors( int numberOfDescriptors ) override;

    public:
        // // incorrect use of a Worker - there are no cached outputs because there can be no outputs associated
        // virtual void                        ResetCachedOutputs( ) override { assert( false ); }
    };

    // Main context - there can be only one per device
    class vaRenderDeviceContextDX12 : public vaRenderDeviceContextBaseDX12
    {
    private:

        struct ResourceStateTransitionItem
        {
            int                                     WorkerIndex;
            D3D12_RESOURCE_STATES                   Target;
            uint32                                  SubResIndex;
        };
        std::unordered_map<vaFramePtr<vaShaderResourceDX12>, ResourceStateTransitionItem>
                                            m_resourceTransitionQueue;
        alignas( VA_ALIGN_PAD ) char        m_padding0[VA_ALIGN_PAD];
        alignas( VA_ALIGN_PAD ) std::mutex  m_resourceTransitionQueueMutex;
        alignas( VA_ALIGN_PAD ) char        m_padding1[VA_ALIGN_PAD];

        std::vector<shared_ptr<vaRenderDeviceContextWorkerDX12>>
                                            m_workers;
        std::vector<vaDrawResultFlags>      m_workerDrawResults;
        // global flag used internally to know how to redirect calls - a bit ugly but easier to manage at the moment this way
        int                                 m_workersActive = 0;

        bool                                m_workersUseBundles = false;

    protected:
        friend class vaRenderDeviceDX12;
        friend class vaRenderDeviceContextWorkerDX12;
                                            vaRenderDeviceContextDX12( vaRenderDevice & device, int instanceIndex ) : vaRenderDeviceContextBaseDX12(device, nullptr, instanceIndex, false) { }
    public:
        virtual                             ~vaRenderDeviceContextDX12( ) { assert( m_workersActive == 0 ); }

    public:
        virtual void                        BeginGraphicsItems( const vaRenderOutputs & renderOutputs, const vaDrawAttributes * drawAttributes ) override { return vaRenderDeviceContext::BeginGraphicsItems( renderOutputs, drawAttributes ); }
        virtual void                        BeginComputeItems( const vaRenderOutputs & renderOutputs, const vaDrawAttributes* drawAttributes ) override { assert( m_workersActive == 0 ); return vaRenderDeviceContext::BeginComputeItems( renderOutputs, drawAttributes ); }
        virtual void                        BeginRaytraceItems( const vaRenderOutputs&, const vaDrawAttributes* ) override;

        virtual void                        EndItems( ) override;
        virtual vaRenderTypeFlags           GetSupportFlags( ) const override { return vaRenderTypeFlags::Graphics | vaRenderTypeFlags::Compute; }

        void                                QueueResourceStateTransition( const vaFramePtr<vaShaderResourceDX12> & resource, int workerIndex, D3D12_RESOURCE_STATES target, uint32 subResIndex = -1 );

        // Executes the command list on the main queue. Cannot be called between BeginItems/EndItems
        void                                Flush( );

    protected:
        void                                ExecuteCommandList( );
        virtual void                        BeginFrame( ) override;
        virtual void                        EndFrame( ) override;
        virtual void                        PostPresent( ) override;

        void                                SetWorkers( const std::vector<shared_ptr<vaRenderDeviceContextWorkerDX12>> & workers, bool workersUseBundles );
        int                                 GetWorkerCount( ) const { return (int)m_workers.size(); }

        virtual void                        PreAllocateTransientDescriptors( ) override; 

    protected:
        virtual vaDrawResultFlags           ExecuteGraphicsItemsConcurrent( int itemCount, const vaRenderOutputs & renderOutputs, const vaDrawAttributes * drawAttributes, const GraphicsItemCallback & callback ) override;

        virtual void                        BeginItems( vaRenderTypeFlags typeFlags, const vaRenderOutputs* renderOutputs, const vaShaderItemGlobals& shaderGlobals ) override;

    public:
        // // Any time you access the internal command list (m_commandList/GetCommandList) and transition any resources manually, you 
        // // have to reset the outputs cache. If changing any other states, then you must reset everything with BindDefaultStates()
        // virtual void                        ResetCachedOutputs( ) override { m_currentOutputs.Reset( ); }
    };

    inline vaRenderDeviceContextDX12 * vaRenderDeviceContextBaseDX12::GetMasterDX12( ) const 
    { 
        vaRenderDeviceContext* master = GetMaster( ); 
        if( master == nullptr ) 
            return nullptr; 
        return static_cast<vaRenderDeviceContextDX12*>( master ); 
    }


    inline vaRenderDeviceContextBaseDX12 &  AsDX12( vaRenderDeviceContext & renderContext )   { return *renderContext.SafeCast<vaRenderDeviceContextBaseDX12*>(); }
    inline vaRenderDeviceContextBaseDX12 *  AsDX12( vaRenderDeviceContext * renderContext )   { return renderContext->SafeCast<vaRenderDeviceContextBaseDX12*>(); }

    inline vaRenderDeviceContextDX12 & AsFullDX12( vaRenderDeviceContext& renderContext ) { assert( !renderContext.IsWorker() ); return *renderContext.SafeCast<vaRenderDeviceContextDX12*>( ); }
    inline vaRenderDeviceContextDX12 * AsFullDX12( vaRenderDeviceContext* renderContext ) { assert( !renderContext->IsWorker() ); return renderContext->SafeCast<vaRenderDeviceContextDX12*>( ); }
}