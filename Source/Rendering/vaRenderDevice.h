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
#include "Core/vaEvent.h"

#include "Rendering/vaRendering.h"

namespace Vanilla
{
    class vaDebugCanvas2D;
    class vaDebugCanvas3D;

    class vaTexture;
    struct vaRenderOutputs;
    class vaRenderDeviceContext;

    class vaConstantBuffer;

    struct vaGraphicsItem;
    struct vaDrawAttributes;

    class vaTextureTools;
    class vaRenderMaterialManager;
    class vaRenderMeshManager;
    class vaAssetPackManager;
    class vaPostProcess;

    class vaShaderManager;

    class vaVertexShader;
    class vaPixelShader;
    class vaRenderBuffer;
    
    class vaRenderGlobals;
    class vaRenderDevice;
    class vaRenderDeviceContext;

    // used only for graphics items
    struct vaRenderOutputs
    {
        static const uint32             c_maxRTs            = 8;
        static const uint32             c_maxUAVs           = 8;

        vaViewport                      Viewport            = vaViewport( 0, 0 );

        vaFramePtr<vaTexture>           RenderTargets[c_maxRTs];
        vaFramePtr<vaShaderResource>    UnorderedAccessViews[c_maxUAVs];
        //uint32                          UAVInitialCounts[c_maxUAVs];    // these need to go out, should not support them anymore to reduce complexity
        vaFramePtr<vaTexture>           DepthStencil        = nullptr;

        uint32                          RenderTargetCount   = 0;
        //uint32                          UAVsStartSlot       = 0;
        //uint32                          UAVCount            = 0;

        vaRenderOutputs( )
        {
            //for( int i = 0; i < countof( UAVInitialCounts ); i++ )
            //    UAVInitialCounts[i] = (uint32)-1;
        }

        bool operator == ( const vaRenderOutputs & other ) const
        {
            if( !(Viewport == other.Viewport && RenderTargetCount == other.RenderTargetCount /*&& UAVsStartSlot == other.UAVsStartSlot && UAVCount == other.UAVCount*/) )
                return false;
            for( uint32 i = 0; i < RenderTargetCount; i++ )
                if( RenderTargets[i] != other.RenderTargets[i] )
                    return false;
            for( uint32 i = 0; i < c_maxUAVs; i++ )
                if( UnorderedAccessViews[i] != other.UnorderedAccessViews[i] )
                    return false;
            if( DepthStencil != other.DepthStencil )
                return false;
            return true;
        }
        bool operator != ( const vaRenderOutputs & other ) const { return !( *this == other ); }

        const vaFramePtr<vaTexture>         GetRenderTarget( ) const { return RenderTargets[0]; }

        // All these SetRenderTargetXXX below are simply helpers for setting m_outputsState - equivalent to filling the RenderOutputsState and doing SetOutputs (although more optimal)
        void                                SetRenderTarget( const std::shared_ptr<vaTexture>& renderTarget, const std::shared_ptr<vaTexture>& depthStencil, bool updateViewport );

        void                                SetRenderTargets( uint32 numRTs, const std::shared_ptr<vaTexture>* renderTargets, const std::shared_ptr<vaTexture>& depthStencil, bool updateViewport );

        void                                SetRenderTargetsAndUnorderedAccessViews( uint32 numRTs, const std::shared_ptr<vaTexture>* renderTargets, const std::shared_ptr<vaTexture>& depthStencil,
            uint32 numUAVs, const std::shared_ptr<vaShaderResource>* UAVs, bool updateViewport );

        void                                SetUnorderedAccessViews( uint32 numUAVs, const std::shared_ptr<vaShaderResource>* UAVs, bool updateViewport );

        void                                Reset( ) { *this = vaRenderOutputs( ); }

        void                                Validate( ) const;

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // static initializers
        static vaRenderOutputs              FromRTDepth( const std::shared_ptr<vaTexture> & renderTarget, const std::shared_ptr<vaTexture> & depthStencil = nullptr, bool updateViewport = true )
        {
            vaRenderOutputs ret;
            ret.SetRenderTarget( renderTarget, depthStencil, updateViewport );
            return ret;
        }
        static vaRenderOutputs              FromRTDepth( const std::vector< std::shared_ptr<vaTexture> > renderTargets, const std::shared_ptr<vaTexture> & depthStencil = nullptr, bool updateViewport = true )
        {
            vaRenderOutputs ret; assert( renderTargets.size() <= vaRenderOutputs::c_maxRTs ); uint32 RTCount = std::min( vaRenderOutputs::c_maxRTs, (uint32)renderTargets.size() );
            ret.SetRenderTargets( RTCount, renderTargets.data(), depthStencil, updateViewport );
            return ret;
        }
        //template < typename _Container >
        //static vaRenderOutputs              FromUAVs( const _Container & UAVs )
        //{
        //    vaRenderOutputs ret;
        //    ret.SetUnorderedAccessViews( (int)UAVs.size(), UAVs.data(), false );
        //    return ret;
        //}
        template < size_t size >
        static vaRenderOutputs              FromUAVs( const std::array< const std::shared_ptr<vaShaderResource>, size > & UAVs )
        {
            vaRenderOutputs ret;
            ret.SetUnorderedAccessViews( (uint32)UAVs.size(), &UAVs[0], false );
            return ret;
        }
        static vaRenderOutputs              FromUAVs( const std::shared_ptr<vaShaderResource> & UAV0 )
        {
            return FromUAVs( std::array<const std::shared_ptr<vaShaderResource>, 1>{UAV0} );
        }
        static vaRenderOutputs              FromUAVs( const std::shared_ptr<vaShaderResource> & UAV0, const std::shared_ptr<vaShaderResource> & UAV1 )
        {
            return FromUAVs( std::array<const std::shared_ptr<vaShaderResource>,2>{UAV0, UAV1} );
        }
        static vaRenderOutputs              FromUAVs( const std::shared_ptr<vaShaderResource> & UAV0, const std::shared_ptr<vaShaderResource> & UAV1, const std::shared_ptr<vaShaderResource> & UAV2 )
        {
            return FromUAVs( std::array<const std::shared_ptr<vaShaderResource>, 3>{UAV0, UAV1, UAV2} );
        }
        static vaRenderOutputs              FromUAVs( const std::shared_ptr<vaShaderResource> & UAV0, const std::shared_ptr<vaShaderResource> & UAV1, const std::shared_ptr<vaShaderResource> & UAV2, const std::shared_ptr<vaShaderResource> & UAV3 )
        {
            return FromUAVs( std::array<const std::shared_ptr<vaShaderResource>, 4>{UAV0, UAV1, UAV2, UAV3} );
        }
        static vaRenderOutputs              FromUAVs( const std::shared_ptr<vaShaderResource> & UAV0, const std::shared_ptr<vaShaderResource> & UAV1, const std::shared_ptr<vaShaderResource> & UAV2, const std::shared_ptr<vaShaderResource> & UAV3, const std::shared_ptr<vaShaderResource> & UAV4 )
        {
            return FromUAVs( std::array<const std::shared_ptr<vaShaderResource>, 5>{UAV0, UAV1, UAV2, UAV3, UAV4} );
        }
        static vaRenderOutputs              FromRTDepthUAVs( uint32 numRTs, const std::shared_ptr<vaTexture> * renderTargets, const std::shared_ptr<vaTexture> & depthStencil,
                                                            uint32 numUAVs, const std::shared_ptr<vaShaderResource>* UAVs, bool updateViewport )
        {
            vaRenderOutputs ret;
            ret.SetRenderTargetsAndUnorderedAccessViews( numRTs, renderTargets, depthStencil, numUAVs, UAVs, updateViewport );
            return ret;
        }
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    };

    struct vaRenderDeviceCapabilities
    {
        struct _VariableShadingRate
        {
            bool        Tier1                                                   = false;
            bool        Tier2                                                   = false;
            bool        AdditionalShadingRatesSupported                         = false;        // Indicates whether 2x4, 4x2, and 4x4 coarse pixel sizes are supported for single-sampled rendering; and whether coarse pixel size 2x4 is supported for 2x MSAA.
            bool        PerPrimitiveShadingRateSupportedWithViewportIndexing    = false;        // Indicates whether the per-provoking-vertex (also known as per-primitive) rate can be used with more than one viewport. If so, then, in that case, that rate can be used when SV_ViewportIndex is written to.
            uint        ShadingRateImageTileSize                                = 32;
        }                           VariableShadingRate;

        struct _Raytracing
        {
            bool        Supported                                               = false;
        }                           Raytracing;

        struct _Other
        {
            bool        BarycentricsSupported                                   = false;
        }                           Other;
    };

    struct vaRenderDeviceThreadLocal
    {
        bool        RenderThread          = false;
        bool        RenderThreadSynced    = false;        // MainThread or guaranteed not to run in parallel with MainThread
    };

    class vaRenderDevice
    {
        vaThreadSpecificAsyncCallbackQueue< vaRenderDevice &, float >
                                                m_asyncBeginFrameCallbacks;

    public:
        vaEvent<void(vaRenderDevice &)>         e_DeviceFullyInitialized;
        vaEvent<void()>                         e_DeviceAboutToBeDestroyed;
        
        // this happens at the beginning of the frame but before the main context can be used
        vaEvent<void(float deltaTime)>          e_BeginFrame;
        
        // this happens at the beginning of the frame, after everything is initialized and main context can be used
        vaEvent<void(vaRenderDevice & device, float deltaTime)>          
                                                e_AfterBeginFrame;

        // this happens just before the end of the frame, while the device and main context are still usable
        vaEvent<void( vaRenderDevice & device )>
                                                e_BeforeEndFrame;

    protected:
        struct SimpleVertex
        {
            float   Position[4];
            float   UV[2];

            SimpleVertex( ) {};
            SimpleVertex( float px, float py, float pz, float pw, float uvx, float uvy ) { Position[0] = px; Position[1] = py; Position[2] = pz; Position[3] = pw; UV[0] = uvx; UV[1] = uvy; }
        };
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Needed for a couple of general utility functions - they used to live in vaPostProcess but since they're used frequently, creating the whole 
        // vaPostProcess instance for them sounds just too troublesome, so placing them here
        shared_ptr< vaConstantBuffer >          m_PPConstants;
        shared_ptr<vaVertexShader>              m_fsVertexShader;  
        shared_ptr<vaRenderBuffer>              m_fsVertexBufferZ0;             // TODO: use trick from the link to avoid using vbuffers at all: https://www.reddit.com/r/gamedev/comments/2j17wk/a_slightly_faster_bufferless_vertex_shader_trick/
        shared_ptr<vaRenderBuffer>              m_fsVertexBufferZ1;             // TODO: use trick from the link to avoid using vbuffers at all: https://www.reddit.com/r/gamedev/comments/2j17wk/a_slightly_faster_bufferless_vertex_shader_trick/
        shared_ptr<vaPixelShader>               m_copyResourcePS;  
        shared_ptr<vaVertexShader>              m_vertexShaderStretchRect;
        shared_ptr<vaPixelShader>               m_pixelShaderStretchRectLinear;
        shared_ptr<vaPixelShader>               m_pixelShaderStretchRectPoint;
        
        // UAV clears workaround - use CSs instead of regular API because of buggy drivers (and, honestly, the API is for this is crap: https://www.gamedev.net/forums/topic/672063-d3d12-clearunorderedaccessviewfloat-fails/)
        shared_ptr<vaComputeShader>             m_CSClearUAV_Buff_1U ;
        shared_ptr<vaComputeShader>             m_CSClearUAV_Buff_4U ;
        shared_ptr<vaComputeShader>             m_CSClearUAV_Tex1D_1F;
        shared_ptr<vaComputeShader>             m_CSClearUAV_Tex1D_4F;
        shared_ptr<vaComputeShader>             m_CSClearUAV_Tex1D_1U;
        shared_ptr<vaComputeShader>             m_CSClearUAV_Tex1D_4U;
        shared_ptr<vaComputeShader>             m_CSClearUAV_Tex2D_1F;
        shared_ptr<vaComputeShader>             m_CSClearUAV_Tex2D_4F;
        shared_ptr<vaComputeShader>             m_CSClearUAV_Tex2D_1U;
        shared_ptr<vaComputeShader>             m_CSClearUAV_Tex2D_4U;

    protected:

        std::atomic_int64_t                     m_currentFrameIndex            = 0;
        //int64                                   m_currentFrameIndex            = 0;

        shared_ptr<vaDebugCanvas2D>             m_canvas2D;
        shared_ptr<vaDebugCanvas3D>             m_canvas3D;

        shared_ptr<vaRenderDeviceContext>       m_mainDeviceContext;

        bool                                    m_profilingEnabled;

        // maybe do https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic2 for the future...
        shared_ptr<vaTextureTools>              m_textureTools;
        shared_ptr<vaRenderGlobals>             m_renderGlobals;
        shared_ptr<vaRenderMaterialManager>     m_renderMaterialManager;
        shared_ptr<vaRenderMeshManager>         m_renderMeshManager;
        shared_ptr<vaAssetPackManager>          m_assetPackManager;
        shared_ptr<vaShaderManager>             m_shaderManager;
        shared_ptr<vaPostProcess>               m_postProcess;

        vaVector2i                              m_swapChainTextureSize      = { 0, 0 };
        string                                  m_adapterNameShort;
        uint                                    m_adapterVendorID;
        string                                  m_adapterNameID;             // adapterNameID is a mix of .Description and [.SubSysId] that uniquely identifies the current graphics device on the system
        int32                                   m_adapterLUIDHigh           = 0;
        uint32                                  m_adapterLUIDLow            = 0;


        double                                  m_totalTime                 = 0.0;
        float                                   m_lastDeltaTime             = 0.0f;
        bool                                    m_frameStarted              = false;

        bool                                    m_imguiFrameStarted         = false;

        // a lot of the vaRenderDevice functionality is locked to the thread that created the object
        std::atomic<std::thread::id>            m_threadID                  = std::this_thread::get_id();

        vaFullscreenState                       m_fullscreenState           = vaFullscreenState::Unknown;
        
        // set when window is destroyed, presents and additional rendering is no longer possible but device is still not destroyed
        bool                                    m_disabled                  = false;

        // set when properly initialized
        bool                                    m_valid                     = false;

        vaRenderDeviceCapabilities              m_caps;

        static thread_local vaRenderDeviceThreadLocal  s_threadLocal;

        vaRenderOutputs                         m_currentBackbuffer;

        int                                     m_nonWorkerRenderContextCount = 0;

        shared_ptr<void> const                  m_aliveToken                = std::make_shared<int>(42);    // this is only used to track object lifetime for callbacks and etc.

        int                                     m_multithreadedWorkerCount  = 1;

    public:
        static const int                        c_BackbufferCount           = 2;

    public:
        vaRenderDevice( );
        virtual ~vaRenderDevice( );

    protected:
        void                                InitializeBase( );
        void                                DeinitializeBase( );
        void                                ExecuteAsyncBeginFrameCallbacks( float deltaTime );

    public:
        virtual void                        CreateSwapChain( int width, int height, HWND hwnd, vaFullscreenState fullscreenState )  = 0;
        virtual bool                        ResizeSwapChain( int width, int height, vaFullscreenState fullscreenState )             = 0;    // return true if actually resized (for further handling)

        virtual void                        StartShuttingDown( )                                                        { m_disabled = true; }

        const vaRenderDeviceCapabilities &  GetCapabilities( ) const                                                    { return m_caps; }

        const vaVector2i &                  GetSwapChainTextureSize( ) const { return m_swapChainTextureSize; }

        const vaRenderOutputs &             GetCurrentBackbuffer( ) const                                               { assert(m_frameStarted); return m_currentBackbuffer; }
        virtual shared_ptr<vaTexture>       GetCurrentBackbufferTexture( ) const                                        = 0;

        virtual bool                        IsSwapChainCreated( ) const                                                 = 0;
        virtual void                        SetWindowed( )                                                              = 0;    // disable fullscreen
        virtual vaFullscreenState           GetFullscreenState( ) const                                                 { return m_fullscreenState; }

        virtual void                        BeginFrame( float deltaTime )              ;
        virtual void                        EndAndPresentFrame( int vsyncInterval = 0 );

        // this is a TODO for the future, for cases where a temporary per-frame resource (such as descriptors) gets exhausted due to huge per-frame
        // rendering requirements (for ex., doing supersampling or machine learning).
        // This should automatically get called by vaRenderDeviceContext::EndItems and in other places, and be capable of ending the frame (no UI!)
        // and re-starting it, with everything set up as it was. Should work in our out of BeginItems/EndItems scope.
        // virtual void                        SyncAndFlush( ) = 0;

        bool                                IsValid( ) const                                                            { return m_valid; }
        
        bool                                IsCreationThread( ) const                                                   { return m_threadID == std::this_thread::get_id(); }
        //bool                                IsRenderThread( ) const                                                     { return s_threadLocal.RenderThread || s_threadLocal.RenderThreadSynced; }
        bool                                IsFrameStarted( ) const                                                     { return m_frameStarted; }

        double                              GetTotalTime( ) const                                                       { return m_totalTime; }
        int64                               GetCurrentFrameIndex( ) const                                               { return m_currentFrameIndex; }

        virtual double                      GetTimeSpanCPUFrame( ) const                                                { return 0; }
        virtual double                      GetTimeSpanCPUGPUSync( ) const                                              { return 0; }
        virtual double                      GetTimeSpanCPUPresent( ) const                                              { return 0; }

        vaRenderDeviceContext *             GetMainContext( ) const                                                     { assert( IsRenderThread() ); return m_mainDeviceContext.get(); }
        int                                 GetTotalContextCount( ) const                                               { return m_nonWorkerRenderContextCount + m_multithreadedWorkerCount; }
                
        vaDebugCanvas2D &                   GetCanvas2D( )                                                              { return *m_canvas2D; }
        vaDebugCanvas3D &                   GetCanvas3D( )                                                              { return *m_canvas3D; }


        bool                                IsProfilingEnabled( )                                                       { return m_profilingEnabled; }

        const string &                      GetAdapterNameShort( ) const                                                { return m_adapterNameShort; }
        const string &                      GetAdapterNameID( ) const                                                   { return m_adapterNameID; }
        uint                                GetAdapterVendorID( ) const                                                 { return m_adapterVendorID; }
        virtual string                      GetAPIName( ) const                                                         = 0;

        const void                          GetAdapterLUID( int32 & highPart, uint32 & lowPart )                        { highPart = m_adapterLUIDHigh; lowPart = m_adapterLUIDLow; }

        // Fullscreen 
        const shared_ptr<vaVertexShader> &  GetFSVertexShader( ) const                                                  { return m_fsVertexShader;   }
        const shared_ptr<vaRenderBuffer> &  GetFSVertexBufferZ0( ) const                                                { return m_fsVertexBufferZ0; }
        const shared_ptr<vaRenderBuffer> &  GetFSVertexBufferZ1( ) const                                                { return m_fsVertexBufferZ1; }
        const shared_ptr<vaPixelShader>  &  GetFSCopyResourcePS( ) const                                                { return m_copyResourcePS;   }
        void                                FillFullscreenPassGraphicsItem( vaGraphicsItem & graphicsItem, bool zIs0 = true ) const;

        void                                GetMultithreadingParams( int & outAvailableCPUThreads, int & outWorkerCount ) const;
        virtual void                        SetMultithreadingParams( int workerCount )                                  { workerCount; }

        virtual void                        SyncGPU( )                                                                  = 0;

    private:
        friend vaApplicationBase;
        // ImGui gets drawn into the main device context ( GetMainContext() ) - this is fixed for now but could be a parameter
        void                                ImGuiRender( const vaRenderOutputs & renderOutputs, vaRenderDeviceContext & renderContext );

    public:
        // Few notes The rules for async callback are as follows:
        //  1.) callbacks can be added into the queue from any thread
        //  2.) if it is added from the render thread and you call .wait() before it executed, it will deadlock
        //  3.) otherwise feel free to .get()/.wait() on the future
        //  4.) if device gets destroyed with some callbacks enqueued, they will get called during destruction but with deltaTime set to std::numeric_limits<float>::lowest() and no more callbacks will be allowed to get added
        std::future<bool>                   AsyncInvokeAtBeginFrame( std::function<bool( vaRenderDevice &, float deltaTime )> && callback )  { return m_asyncBeginFrameCallbacks.Enqueue( std::forward<decltype(callback)>(callback) ); }

    public:
        // This one's if you're inheriting and modifying vaRenderingModuleParams
        template<typename ModuleType, typename ParamsType = vaRenderingModuleParams, typename... Args>
        inline std::shared_ptr<ModuleType>  CreateModule( Args & ... args )
        {
            return std::shared_ptr<ModuleType>( vaRenderingModuleRegistrar::CreateModuleTyped<ModuleType>( typeid( ModuleType ).name( ), ParamsType( *this, args... ) ) );
        }
        // This one's directly just using the default vaRenderingModuleParams
        template<typename ModuleType>
        inline std::shared_ptr<ModuleType>  CreateModule( void * userParam0 )
        {
            return std::shared_ptr<ModuleType>( vaRenderingModuleRegistrar::CreateModuleTyped<ModuleType>( typeid( ModuleType ).name( ), vaRenderingModuleParams( *this, userParam0 ) ) );
        }
        // This one's directly just using the default vaRenderingModuleParams
        template<typename ModuleType>
        inline std::shared_ptr<ModuleType>  CreateModule( void * userParam0, void * userParam1 )
        {
            return std::shared_ptr<ModuleType>( vaRenderingModuleRegistrar::CreateModuleTyped<ModuleType>( typeid( ModuleType ).name( ), vaRenderingModuleParams( *this, userParam0, userParam1 ) ) );
        }

    private:
        void                                RegisterModules( );

    protected:
        virtual void                        ImGuiCreate( );
        virtual void                        ImGuiDestroy( );
        virtual void                        ImGuiNewFrame( ) = 0;
        virtual void                        ImGuiEndFrame( );
        // ImGui gets drawn into the main device context ( GetMainContext() ) - this is fixed for now but could be a parameter
        virtual void                        ImGuiEndFrameAndRender( const vaRenderOutputs & renderOutputs, vaRenderDeviceContext & renderContext ) = 0;

    protected:
        virtual void                        UIMenuHandler( class vaApplicationBase & );

    public:
        // These are essentially API dependencies - they require graphics API to be initialized so there's no point setting them up separately
        vaTextureTools &                    GetTextureTools( );
        vaRenderMaterialManager &           GetMaterialManager( );
        vaRenderMeshManager &               GetMeshManager( );
        vaAssetPackManager &                GetAssetPackManager( );
        virtual vaShaderManager &           GetShaderManager( )                                                         = 0;
        vaRenderGlobals &                   GetRenderGlobals( );
        vaPostProcess &                     GetPostProcess( );

    public:
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // useful for copying individual MIPs, in which case use Views created with vaTexture::CreateView
        virtual vaDrawResultFlags           CopySRVToRTV( vaRenderDeviceContext & renderContext, shared_ptr<vaTexture> destination, shared_ptr<vaTexture> source );
        //
        // Copies srcTexture into dstTexture with stretching using requested filter and blend modes. Backups current render target and restores it after.
        virtual vaDrawResultFlags           StretchRect( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture, const vaVector4 & dstRect = {0,0,0,0}, const vaVector4 & srcRect = {0,0,0,0}, bool linearFilter = true, vaBlendMode blendMode = vaBlendMode::Opaque, const vaVector4 & colorMul = vaVector4( 1.0f, 1.0f, 1.0f, 1.0f ), const vaVector4 & colorAdd = vaVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
        //
        // "Manual" UAV clears - INCOMPLETE (please add where needed)
        vaDrawResultFlags                   ClearUAV( vaRenderDeviceContext & renderContext, const shared_ptr<vaRenderBuffer> & buffer, const vaVector4ui & clearValue );
        vaDrawResultFlags                   ClearUAV( vaRenderDeviceContext & renderContext, const shared_ptr<vaRenderBuffer> & buffer, uint32 clearValue );
        vaDrawResultFlags                   ClearUAV( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & texture, float clearValue );
        vaDrawResultFlags                   ClearUAV( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & texture, const vaVector4 & clearValue );
        vaDrawResultFlags                   ClearUAV( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & texture, uint clearValue );
        vaDrawResultFlags                   ClearUAV( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & texture, const vaVector4ui & clearValue );
    private:
        vaDrawResultFlags                   ClearTextureUAVGeneric( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & texture, const shared_ptr<vaComputeShader> & computeShader, PostProcessConstants clearValue );
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    public:
        // changing
        static const vaRenderDeviceThreadLocal & ThreadLocal( )                                                         { return s_threadLocal; }
        static void                         SetSyncedWithRenderThread( )                                                { s_threadLocal.RenderThreadSynced = true; }
        static bool                         IsRenderThread( )                                                           { return s_threadLocal.RenderThread || s_threadLocal.RenderThreadSynced; }


    public:
        template< typename CastToType >
        CastToType                          SafeCast( )                                                                 
        {
#ifdef _DEBUG
            CastToType ret = dynamic_cast< CastToType >( this );
            assert( ret != NULL );
            return ret;
#else
            return static_cast< CastToType >( this );
#endif
        }

        // ID3D11Device *             GetPlatformDevice( ) const                                  { m_device; }
    };
}