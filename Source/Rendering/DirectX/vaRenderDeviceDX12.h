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

#include "Rendering/vaRenderDevice.h"

#include "Core/System/vaMemoryStream.h"

#include "Rendering/DirectX/vaDirectXIncludes.h"
#include "Rendering/DirectX/vaDirectXTools.h"

#include "Core/vaConcurrency.h"
#include "Core/vaContainers.h"

// #include "Rendering/DirectX/vaDebugCanvas2DDX11.h"
// #include "Rendering/DirectX/vaDebugCanvas3DDX11.h"

#ifdef _DEBUG
#define VA_D3D12_USE_DEBUG_LAYER
//#define VA_D3D12_USE_DEBUG_LAYER_GPU_VALIDATION
//#define VA_D3D12_USE_DEBUG_LAYER_DRED
//#define VA_D3D12_FORCE_IMMEDIATE_SYNC               // useful to rule out suspected synchronization issues
#endif


namespace Vanilla
{
    class vaApplicationWin;
    class vaRenderBufferDX12;

    class vaRenderDeviceDX12 : public vaRenderDevice
    {
        // Dynamic persistent descriptor heap, allows single descriptor allocation/deallocation.
        class DescriptorHeap
        {
        private:
            vaRenderDeviceDX12* m_device            = nullptr;
            int                 m_capacity          = 0;
            int                 m_reservedCapacity  = 0;            // this amount is pre-allocated and used elsewhere (by TransientDescriptorAllocator for ex.)
            int                 m_allocatedCount    = 0;
            std::vector<int>    m_freed;

            D3D12_DESCRIPTOR_HEAP_DESC 
                                m_heapDesc          = { };
            ComPtr<ID3D12DescriptorHeap>
                                m_heap;
            uint32              m_descriptorSize    = 0;

            D3D12_CPU_DESCRIPTOR_HANDLE m_heapCPUStart = { 0 };
            D3D12_GPU_DESCRIPTOR_HANDLE m_heapGPUStart = { 0 };

            shared_ptr<vaPaddedObject<std::mutex>>   m_mutex;

        public:
            DescriptorHeap( )           { m_mutex = std::make_shared<vaPaddedObject<std::mutex>>(); }
            ~DescriptorHeap( );

            void                        Initialize( vaRenderDeviceDX12 & device, const D3D12_DESCRIPTOR_HEAP_DESC & desc, int reservedCapacity );

            bool                        Allocate( int & outIndex, D3D12_CPU_DESCRIPTOR_HANDLE & outCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE & outGPUHandle );
            void                        Release( int index );

            const ComPtr<ID3D12DescriptorHeap> &
                                        GetHeap( ) const            { return m_heap; }
            const D3D12_DESCRIPTOR_HEAP_DESC &
                                        GetDesc( ) const            { return m_heapDesc; }

            uint32                      GetDescriptorSize( ) const  { return m_descriptorSize; }

            D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandleForHeapStart( ) const { return m_heapCPUStart; }
            D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandleForHeapStart( ) const { return m_heapGPUStart; }
        };

        // Uses a circular (ring) buffer to allocate temporary (lasting for vaRenderDevice::c_BackbufferCount frames) descriptors, backed by
        // the storage from a DescriptorHeap (above).
        // This isn't thread-safe so for all use please pre-allocate.
        class TransientDescriptorAllocator
        {
            DescriptorHeap *            m_backingHeap           = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE m_backingHeapCPUStart   = {0};
            D3D12_GPU_DESCRIPTOR_HANDLE m_backingHeapGPUStart   = {0};
            uint32                      m_descriptorSize        = 0;

            int                         m_capacity              = 0;
            //vaRenderDevice::c_BackbufferCount

            int                         m_head                  = 0;            // the next empty space (last allocated+1)

            // can't roll over these indices
            int                         m_frameBarriers[vaRenderDevice::c_BackbufferCount];

            // if the alloc failed and we sync-ed, this is how far we got (index into m_frameBarriers)
            int                         m_syncAge               = 0;

        public:
            void                        Initialize( DescriptorHeap * backingHeap, int capacity );
            void                        Deinitialize( )                                             { m_backingHeap = nullptr; m_capacity = 0; }
            
            // Returns newly allocated descriptor index or -1 if allocation failed; if allocation failed, the caller needs to keep doing a NextFrame cycle, 
            // which requires a context Flush() and GPU sync.
            // 'size' must be smaller than m_capacity/2.
            int                         Allocate( int size );

            // This lets the allocator know that we've synced to the oldest frame and it can drop the oldest barrier!
            void                        NextFrame( );
            
            // This is for within-frame syncing - let's you free up / reuse incrementally from the oldest chunk by sync-ing to old frames
            int                         SyncAge( ) const                                            { return m_syncAge; }
            void                        SyncAgeIncrement( )                                         { m_syncAge++; }

            D3D12_CPU_DESCRIPTOR_HANDLE GetHeapCPUStart( ) const                                    { return m_backingHeapCPUStart; }
            D3D12_GPU_DESCRIPTOR_HANDLE GetHeapGPUStart( ) const                                    { return m_backingHeapGPUStart; }

            D3D12_CPU_DESCRIPTOR_HANDLE ComputeCPUHandle( int index )                               { return CD3DX12_CPU_DESCRIPTOR_HANDLE( m_backingHeapCPUStart, index, m_descriptorSize ); }
            D3D12_GPU_DESCRIPTOR_HANDLE ComputeGPUHandle( int index )                               { return CD3DX12_GPU_DESCRIPTOR_HANDLE( m_backingHeapGPUStart, index, m_descriptorSize ); }
        };

        static_assert( array_size( vaGraphicsItem::ShaderResourceViews ) == array_size( vaComputeItem::ShaderResourceViews ) );
        static_assert( array_size( vaGraphicsItem::ConstantBuffers ) == array_size( vaComputeItem::ConstantBuffers ) );
        //static_assert( vaRenderOutputs::c_maxUAVs == array_size( vaComputeItem::UnorderedAccessViews ) );

        struct DefaultRootSignatureParams
        {
            // Constants - these are all direct CBVs (set with SetGraphicsRootConstantBufferView for ex.) - global and per-draw.
            // These take 2 DWORDs space each in root signature.
            static const int            GlobalDirectCBVBase         = 0;
            static const int            GlobalDirectCBVSlotBase     = SHADERGLOBAL_CBV_SLOT_BASE;
            static const int            GlobalDirectCBVCount        = array_size(vaShaderItemGlobals::ConstantBuffers);
            static const int            PerDrawDirectCBVBase        = GlobalDirectCBVBase + GlobalDirectCBVCount;
            static const int            PerDrawDirectCBVSlotBase    = 0;
            static const int            PerDrawDirectCBVCount       = array_size(vaGraphicsItem::ConstantBuffers);

            // Global UAVs and SRVs - these are all one descriptor parameter with 3 ranges. 
            // This is all a single DWORD space in root signature.
            static const int            GlobalUAVSRVBase            = PerDrawDirectCBVBase+PerDrawDirectCBVCount;
            static const int            GlobalUAVSlotBase           = SHADERGLOBAL_UAV_SLOT_BASE;
            static const int            OutputsUAVSlotBase          = 0;
            static const int            GlobalSRVSlotBase           = SHADERGLOBAL_SRV_SLOT_BASE;
            static const int            GlobalSRVParamCount         = 1; //array_size(vaShaderItemGlobals::ShaderResourceViews);
            static const int            GlobalUAVSRVParamCount      = 1;
            // these are offsets within the descriptor heap above
            static const int            DescriptorOffsetGlobalUAV   = 0;
            static const int            DescriptorOffsetOutputsUAV  = DescriptorOffsetGlobalUAV+array_size(vaShaderItemGlobals::UnorderedAccessViews);
            static const int            DescriptorOffsetGlobalSRV   = DescriptorOffsetOutputsUAV+vaRenderOutputs::c_maxUAVs;
            static const int            GlobalUAVSRVRangeSize       = DescriptorOffsetGlobalSRV+array_size(vaShaderItemGlobals::ShaderResourceViews);

            // These are legacy (non-bindless) SRVs, 1 root parameter each for 1-item descriptor range for each SRV.
            // These take single DWORD space in root signature each ( array_size(vaGraphicsItem::ShaderResourceViews) )
            static const int            PerDrawSRVBase              = GlobalUAVSRVBase + GlobalUAVSRVParamCount;
            static const int            PerDrawSRVSlotBase          = 0;
            static const int            PerDrawSRVCount             = array_size(vaGraphicsItem::ShaderResourceViews);

            // Raytracing acceleration structure
            static const int            RaytracingStructDirectSRV   = PerDrawSRVBase + PerDrawSRVCount;
            // Instance index (used only during rasterization)
            static const int            InstanceIndexDirectUINT32   = RaytracingStructDirectSRV + 1;
            // Generic uint 'root constant' - useful when only 1 uint parameter needed for pixel/compute/raytracing shader (and also allows for constant folding - good for perf.)
            static const int            GenericRootConstDirectUINT32= InstanceIndexDirectUINT32 + 1;

            // Bindless descriptors (need 2 until SM6.6 comes along)
            static const int            Bindless1SRVBase            = GenericRootConstDirectUINT32 + 1;
            static const int            Bindless1SRVSlotBase        = 0;
            static const int            Bindless1SRVRegSpace        = 1;
            static const int            Bindless2SRVBase            = Bindless1SRVBase + 1;
            static const int            Bindless2SRVSlotBase        = 0;
            static const int            Bindless2SRVRegSpace        = 2;

            static const int            TotalParameters             = Bindless2SRVBase + 1;
        };

        typedef std::unordered_map<vaMemoryBuffer, vaGraphicsPSODX12*, vaPSOKeyDataHasher>      GraphicsPSOCacheType;
        typedef std::unordered_map<vaMemoryBuffer, vaComputePSODX12*, vaPSOKeyDataHasher>       ComputePSOCacheType;
        typedef std::unordered_map<vaMemoryBuffer, vaRaytracePSODX12*, vaPSOKeyDataHasher>      RaytracePSOCacheType;

        // these are very light thread-local caches
        typedef vaHashedCircularCache<vaMemoryBuffer, vaGraphicsPSODX12*, 16, 137, vaPSOKeyDataHasher>  LocalGraphicsPSOCacheType;
        typedef vaHashedCircularCache<vaMemoryBuffer, vaComputePSODX12*, 16, 137, vaPSOKeyDataHasher>   LocalComputePSOCacheType;
        typedef vaHashedCircularCache<vaMemoryBuffer, vaRaytracePSODX12*, 16, 137, vaPSOKeyDataHasher>  LocalRaytracePSOCacheType;

        static constexpr int                c_maxWorkers            = 128;

    private:
        string                              m_preferredAdapterNameID = "";

        ComPtr<IDXGIFactory5>               m_DXGIFactory;
        ComPtr<ID3D12Device6>               m_device;

        ComPtr<ID3D12CommandQueue>          m_commandQueue;

        ComPtr<IDXGISwapChain3>             m_swapChain;

        std::vector<DescriptorHeap>         m_defaultDescriptorHeaps;
        TransientDescriptorAllocator        m_transientDescAllocator;

        // static const int                    c_defaultTransientDescriptorHeapsPerFrame       = 32;
        // TransientGPUDescriptorHeap          m_defaultTransientDescriptorHeaps[c_defaultTransientDescriptorHeapsPerFrame][vaRenderDevice::c_BackbufferCount];     // SRV/CBV/UAV only - when we hit the per-frame limit, expand to a more dynamic heap allocation - TODO: future
        // int                                 m_defaultTransientDescriptorHeapsCurrentIndex   = 0;
         std::atomic_bool                    m_defaultDescriptorHeapsInitialized = false;

        static const int                    c_SwapChainBufferCount                          = vaRenderDevice::c_BackbufferCount+1;

        // this are temporary - should be replaced by vaTexture wrapper once that starts working
        //ComPtr<ID3D12Resource>             m_renderTargets[vaRenderDevice::c_BackbufferCount];
        //vaRenderTargetViewDX12             m_renderTargetViews[vaRenderDevice::c_BackbufferCount];
        std::vector<shared_ptr<vaTexture>>  m_renderTargets;

        // default root signatures
        ComPtr<ID3D12RootSignature>         m_defaultGraphicsRootSignature;
        ComPtr<ID3D12RootSignature>         m_defaultComputeRootSignature;
        // DefaultRootSignatureIndexRanges     m_defaultRootParamIndexRanges;
        // ExtendedRootSignatureIndexRanges    m_extendedRootParamIndexRanges;

        // synchronization objects
        uint32                              m_currentFrameFlipIndex         = 0;   // 0..vaRenderDevice::c_BackbufferCount-1, a.k.a. m_frameIndex
        uint32                              m_currentSwapChainBufferIndex   = 0;   // 0..swap chain count
        HANDLE                              m_fenceEvent;
        ComPtr<ID3D12Fence>                 m_fence;
        uint64                              m_fenceValues[vaRenderDevice::c_BackbufferCount];
        uint64                              m_lastFenceValue                = 0;   // we used to have fenceValues[vaRenderDevice::c_BackbufferCount] so you could sync to any frame but it's not used and causes confusion

        // not doing this now due to complexity caused by flushes 
        // D3D12_QUERY_DATA_PIPELINE_STATISTICS m_lastStats = {0};
        // ComPtr<ID3D12QueryHeap>             m_statsHeap;
        // shared_ptr<vaRenderBufferDX12>            m_statsReadbackBuffers[vaRenderDevice::c_BackbufferCount];

        HWND                                m_hwnd                  = 0;

        alignas(VA_ALIGN_PAD) char          m_padding0[VA_ALIGN_PAD];
        alignas(VA_ALIGN_PAD) std::mutex    m_beginFrameCallbacksMutex;
        alignas(VA_ALIGN_PAD) char          m_padding1[VA_ALIGN_PAD];
        std::atomic_bool                    m_beginFrameCallbacksExecuting = false;
        std::atomic_bool                    m_beginFrameCallbacksDisable = false;
        std::vector< std::function<void( vaRenderDeviceDX12 & device )> >
                                            m_beginFrameCallbacks;

        alignas(VA_ALIGN_PAD) char          m_padding2[VA_ALIGN_PAD];
        alignas(VA_ALIGN_PAD) std::mutex    m_GPUFrameFinishedCallbacksMutex;
        alignas(VA_ALIGN_PAD) char          m_padding3[VA_ALIGN_PAD];
        std::atomic_bool                    m_GPUFrameFinishedCallbacksExecuting = false;
        std::vector< std::function<void( vaRenderDeviceDX12 & device )> >
                                            m_GPUFrameFinishedCallbacks[vaRenderDevice::c_BackbufferCount];

#ifdef VA_IMGUI_INTEGRATION_ENABLED
        ComPtr<ID3D12DescriptorHeap>        m_imgui_SRVDescHeap = nullptr;
#endif

        vaConstantBufferViewDX12            m_nullCBV;
        vaShaderResourceViewDX12            m_nullSRV;
        vaUnorderedAccessViewDX12           m_nullUAV;
        vaUnorderedAccessViewDX12           m_nullBufferUAV;
        vaRenderTargetViewDX12              m_nullRTV;
        vaDepthStencilViewDX12              m_nullDSV;
        vaSamplerViewDX12                   m_nullSamplerView;

        // PSO cache - this might be a lot more efficient with a hash map (unordered_map) especially since the hash gets computed for the vaMemoryBuffer key anyway
        lc_shared_mutex<61>                 m_graphicsPSOCacheMutex;
        GraphicsPSOCacheType                m_graphicsPSOCache;
        vaMemoryBuffer                      m_graphicsPSOCacheCleanupLastKey;
        lc_shared_mutex<>                   m_computePSOCacheMutex;
        ComputePSOCacheType                 m_computePSOCache;
        vaMemoryBuffer                      m_computePSOCacheCleanupLastKey;
        lc_shared_mutex<>                   m_raytracePSOCacheMutex;
        RaytracePSOCacheType                m_raytracePSOCache;
        vaMemoryBuffer                      m_raytracePSOCacheCleanupLastKey;
        int                                 m_PSOCachesClearOrder = 0;          // used by PSOCachesClearUnusedTick to alternate clearing between frames

        //lc_shared_mutex<17>                 m_bindDefaultDescriptorHeapsMutex;                  // <- the way this is used right now, the mutex is unneccessary 

        double                              m_timeBeforeSync                    = 0.0;
        double                              m_timeSpanCPUFrame                  = 0.0;
        double                              m_timeSpanCPUGPUSync                = 0.0;
        double                              m_timeSpanCPUPresent                = 0.0;
        double                              m_timeSpanCPUGPUSyncStalls          = 0.0;          // these are unexpected stalls triggered by running out of transient heap space or etc.

        bool                                m_workersUseBundleCommandLists      = true;

    public:
        vaRenderDeviceDX12( const string & preferredAdapterNameID = "", const std::vector<wstring> & shaderSearchPaths = { vaCore::GetExecutableDirectory( ), vaCore::GetExecutableDirectory( ) + L"../Source/Rendering/Shaders" } );
        virtual ~vaRenderDeviceDX12( void );

    public:
        HWND                                GetHWND( ) { return m_hwnd; }

    private:
        bool                                Initialize( const std::vector<wstring> & shaderSearchPaths );
        void                                Deinitialize( );

    protected:
        virtual void                        CreateSwapChain( int width, int height, HWND hwnd, vaFullscreenState fullscreenState ) override;
        virtual bool                        ResizeSwapChain( int width, int height, vaFullscreenState fullscreenState ) override;             // returns true if actually resized
        virtual void                        SetWindowed( ) override;

        virtual bool                        IsSwapChainCreated( ) const                                                     { return m_swapChain != nullptr; }

        virtual void                        BeginFrame( float deltaTime );
        virtual void                        EndAndPresentFrame( int vsyncInterval = 0 );

    public:
        double                              GetTimeSpanCPUFrame( ) const override                                           { return m_timeSpanCPUFrame; }
        double                              GetTimeSpanCPUGPUSync( ) const override                                         { return m_timeSpanCPUGPUSync; }
        double                              GetTimeSpanCPUPresent( ) const override                                         { return m_timeSpanCPUPresent; }

        // TODO: rename to GetDX*
//        ID3D11DeviceContext *               GetImmediateRenderContext( ) const                                            { return m_deviceImmediateContext; }
        const ComPtr<ID3D12Device6> &       GetPlatformDevice( ) const                                                      { return m_device; }
        const ComPtr<ID3D12CommandQueue> &  GetCommandQueue( ) const                                                        { return m_commandQueue; }

        virtual vaShaderManager &           GetShaderManager( ) override                                                    { assert(false); return *((vaShaderManager *)nullptr); }

        static void                         RegisterModules( );

        uint32                              GetCurrentFrameFlipIndex( ) const                                               { return m_currentFrameFlipIndex; }
        virtual shared_ptr<vaTexture>       GetCurrentBackbufferTexture( ) const override                                   { return (m_currentSwapChainBufferIndex < m_renderTargets.size()) ? (m_renderTargets[m_currentSwapChainBufferIndex]):(nullptr); }

        // useful for any early loading, resource copying, etc - happens just after the main context command list is ready to receive commands but before any other rendering is done
        void                                ExecuteAtBeginFrame( const std::function<void( vaRenderDeviceDX12 & device )> & callback );

        // not sure if this is a good idea but seems so at this point - intended to schedule resource deletion after the current fence
        void                                ExecuteAfterCurrentGPUFrameDone( const std::function<void( vaRenderDeviceDX12 & device )> & callback );
        void                                ExecuteAfterCurrentGPUFrameDone( const std::vector< std::function<void( vaRenderDeviceDX12 & device )> > & callbacks );

        // safely releases the object only after all active command lists have been executed; it makes sure resourcePtr is the only
        // reference to the object, takes ownership and resets resourcePtr so no one can use it after
        template<typename T>
        void                                SafeReleaseAfterCurrentGPUFrameDone( ComPtr<T> & resourcePtr, bool assertOnNotUnique = true );

        const vaConstantBufferViewDX12 &    GetNullCBV        () const                                                      { return  m_nullCBV;       }
        const vaShaderResourceViewDX12 &    GetNullSRV        () const                                                      { return  m_nullSRV;       }
        const vaUnorderedAccessViewDX12&    GetNullUAV        () const                                                      { return  m_nullUAV;       }
        const vaUnorderedAccessViewDX12&    GetNullBufferUAV  () const                                                      { return  m_nullBufferUAV; }
        const vaRenderTargetViewDX12   &    GetNullRTV        () const                                                      { return  m_nullRTV;       }
        const vaDepthStencilViewDX12   &    GetNullDSV        () const                                                      { return  m_nullDSV;       }
        const vaSamplerViewDX12        &    GetNullSamplerView() const                                                      { return m_nullSamplerView;}

        // find in cache or create - warning, never store this pointer longer than a frame; it can get deleted after the GPU finishes with it.
        vaGraphicsPSODX12 *                 FindOrCreateGraphicsPipelineState( const vaGraphicsPSODescDX12 & psoDesc, LocalGraphicsPSOCacheType * localCache = nullptr );
        vaComputePSODX12 *                  FindOrCreateComputePipelineState( const vaComputePSODescDX12 & psoDesc, LocalComputePSOCacheType * localCache = nullptr );
        vaRaytracePSODX12 *                 FindOrCreateRaytracePipelineState( const vaRaytracePSODescDX12 & psoDesc, LocalRaytracePSOCacheType * localCache = nullptr );

        //
    private:
        friend class vaResourceViewDX12;
        friend class vaRenderDeviceContextDX12;
        friend class vaRenderDeviceContextBaseDX12;
        friend class vaRenderDeviceContextWorkerDX12;
        bool                                AllocatePersistentResourceView( D3D12_DESCRIPTOR_HEAP_TYPE type, int & outIndex, D3D12_CPU_DESCRIPTOR_HANDLE & outCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE & outGPUHandle );
        void                                ReleasePersistentResourceView( D3D12_DESCRIPTOR_HEAP_TYPE type, int index )     { GetDescriptorHeap( type )->Release( index ); }
        D3D12_GPU_DESCRIPTOR_HANDLE         GetBindlessDescHeapGPUHandle( )                                                 { return GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)->GetGPUDescriptorHandleForHeapStart(); }

        int                                 TransientDescHeapAllocate( int size );
        D3D12_CPU_DESCRIPTOR_HANDLE         TransientDescHeapGetCPUStart( ) const                                           { return m_transientDescAllocator.GetHeapCPUStart(); }
        D3D12_GPU_DESCRIPTOR_HANDLE         TransientDescHeapGetGPUStart( ) const                                           { return m_transientDescAllocator.GetHeapGPUStart(); }
        D3D12_CPU_DESCRIPTOR_HANDLE         TransientDescHeapComputeCPUHandle( int index )                                  { return m_transientDescAllocator.ComputeCPUHandle(index); }
        D3D12_GPU_DESCRIPTOR_HANDLE         TransientDescHeapComputeGPUHandle( int index )                                  { return m_transientDescAllocator.ComputeGPUHandle(index); }

        bool                                ExecuteBeginFrameCallbacks( );
        bool                                ExecuteGPUFrameFinishedCallbacks( bool oldestFrameOnly );                       // either oldest frame only or all frames (have to WaitForGPU before!)

        void                                CreateSwapChainRelatedObjects( );
        void                                ReleaseSwapChainRelatedObjects( );

        ID3D12RootSignature *               GetDefaultGraphicsRootSignature( ) const                                        { return m_defaultGraphicsRootSignature.Get(); }
        ID3D12RootSignature *               GetDefaultComputeRootSignature( ) const                                         { return m_defaultComputeRootSignature.Get();  }
        //const DefaultRootSignatureIndexRanges & GetDefaultRootParamIndexRanges( ) const                                     { return m_defaultRootParamIndexRanges; }

        // bool                                PrepareNextTransientDescriptorHeap( int itemsToBeRendered = 0 );
        void                                BindDefaultDescriptorHeaps( ID3D12GraphicsCommandList * commandList );

        void                                PSOCachesClearUnusedTick( );
        void                                PSOCachesClearAll( );

        friend class vaResourceViewDX12;
        friend class vaRenderDeviceContextDX12;
        friend class vaTextureDX12;
        friend class vaSceneRaytracingDX12;
        DescriptorHeap *                    GetDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE type );

        void                                DeviceRemovedHandler( );

        virtual void                        SetMultithreadingParams( int workerCount ) override;

    protected:
        virtual void                        ImGuiCreate( ) override;
        virtual void                        ImGuiDestroy( ) override;
        virtual void                        ImGuiNewFrame( ) override;
        // ImGui gets drawn into the main device context ( GetMainContext() ) - this is fixed for now but could be a parameter
        virtual void                        ImGuiEndFrameAndRender( const vaRenderOutputs & renderOutputs, vaRenderDeviceContext & renderContext ) override;

        virtual void                        StartShuttingDown( ) override;

    protected:
        virtual void                        UIMenuHandler( class vaApplicationBase& );

    public:
        virtual string                      GetAPIName( ) const override                                            { return StaticGetAPIName(); }
        static string                       StaticGetAPIName( )                                                     { return "DirectX12"; }
        static void                         StaticEnumerateAdapters( std::vector<pair<string, string>> & outAdapters );

    public:
        // will add a GPU fence, block until it is done and, optionally, execute all callbacks pooled by 'ExecuteAfterCurrentGPUFrameDone'
        void                                SyncGPU( bool executeAfterFrameDoneCallbacks );
        // will sync on previous frames; age can go from c_BackbufferCount to 0 where 0 means sync to current frame with a call SyncGPU(false)
        void                                SyncGPUFrame( int age );
    };

    //////////////////////////////////////////////////////////////////////////
    // Inline
    //////////////////////////////////////////////////////////////////////////


    inline bool vaRenderDeviceDX12::AllocatePersistentResourceView( D3D12_DESCRIPTOR_HEAP_TYPE type, int & outIndex, D3D12_CPU_DESCRIPTOR_HANDLE & outCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE & outGPUHandle )
    {
        DescriptorHeap * allocator = GetDescriptorHeap( type );

        if( allocator == nullptr )
            { outIndex = -1; assert( false ); }

        return allocator->Allocate( outIndex, outCPUHandle, outGPUHandle );
    }

//    inline bool vaRenderDeviceDX12::TransientGPUDescriptorHeap::Allocate( int numberOfDescriptors, int & outIndex )
//    {
//        assert( m_device != nullptr );
//#if 0
//        std::unique_lock mutexLock( m_mutex );
//
//        if( ( m_allocatedCount + numberOfDescriptors ) > m_capacity )
//        {
//            VA_ERROR( "Ran out of TransientGPUDescriptorHeap space - consider initializing with a bigger heap or fixing it 'properly' - check comments around vaRenderDevice::SyncAndFlush" );
//            assert( false );
//            return false;
//        }
//
//        // do the allocation
//        outIndex = m_allocatedCount;
//        m_allocatedCount += numberOfDescriptors;
//#else
//        int output = m_allocatedCount.fetch_add( numberOfDescriptors );
//        if( output + numberOfDescriptors > m_capacity )
//        {
//            VA_ERROR( "Ran out of TransientGPUDescriptorHeap space - consider initializing with a bigger heap or fixing it 'properly' - check comments around vaRenderDevice::SyncAndFlush" );
//            assert( false );
//            return false;
//        }
//        outIndex = output;
//#endif
//        return true;
//    }

    template<typename T>
    inline void vaRenderDeviceDX12::SafeReleaseAfterCurrentGPUFrameDone( ComPtr<T> & resourceComPtr, bool assertOnNotUnique )
    {
        if( resourceComPtr == nullptr )
            return;

        // Get native ptr
        IUnknown * resourcePtr = nullptr; 
        HRESULT hr; V( resourceComPtr.CopyTo( &resourcePtr ) );
        
        // Reset ComPtr and make sure we have the only reference (in resourcePtr)
        UINT refCount = resourceComPtr.Reset();
        if( assertOnNotUnique )
            { assert( refCount == 1 ); refCount; }

        // Let the resource be removed when we can guarantee GPU has finished using it
        ExecuteAfterCurrentGPUFrameDone( 
        [resourcePtr, assertOnNotUnique]( vaRenderDeviceDX12 & device )
            { 
                device;
                ULONG refCount = resourcePtr->Release();
                if( assertOnNotUnique ) 
                { assert( refCount == 0 ); refCount; }
            } );
    }

    inline vaRenderDeviceDX12 & AsDX12( vaRenderDevice & device )   { return *device.SafeCast<vaRenderDeviceDX12*>(); }
    inline vaRenderDeviceDX12 * AsDX12( vaRenderDevice * device )   { return device->SafeCast<vaRenderDeviceDX12*>(); }
}