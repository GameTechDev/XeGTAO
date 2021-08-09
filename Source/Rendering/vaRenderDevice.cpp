///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Rendering/vaRenderDevice.h"
#include "Rendering/vaTextureHelpers.h"

#include "Rendering/vaRenderBuffers.h"

#include "vaAssetPack.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"

#include "Rendering/vaDebugCanvas.h"

#include "Rendering/vaTexture.h"

#include "Core/System/vaFileTools.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/vaRenderGlobals.h"

#include "Rendering/Effects/vaPostProcess.h"
#include "Rendering/Effects/vaPostProcessTonemap.h"
#include "Rendering/Effects/vaPostProcessBlur.h"
#include "Rendering/Effects/vaASSAOLite.h"
#include "Rendering/Effects/vaGTAO.h"
#include "Rendering/Effects/vaTAA.h"
#include "Rendering/Effects/vaSkybox.h"

#include "Rendering/vaPathTracer.h"

#include "Rendering/vaSceneRenderer.h"

#include "Core/vaProfiler.h"

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
#include "IntegratedExternals/vaTaskflowIntegration.h"
#endif

using namespace Vanilla;

void vaRenderOutputs::SetRenderTarget( const std::shared_ptr<vaTexture> & renderTarget, const std::shared_ptr<vaTexture> & depthStencil, bool updateViewport )
{
    //assert( &renderTarget != &RenderTargets[0] );
    for( int i = 0; i < vaRenderOutputs::c_maxRTs; i++ )     RenderTargets[i] = (i==0)?(renderTarget):(nullptr);
    for( int i = 0; i < vaRenderOutputs::c_maxUAVs; i++ )    UnorderedAccessViews[i] = nullptr;
    // for( int i = 0; i < vaRenderOutputs::c_maxUAVs; i++ )    UAVInitialCounts[i]    = (uint32)-1;
    RenderTargets[0] = renderTarget;
    DepthStencil = depthStencil;
    RenderTargetCount = renderTarget != nullptr;
    //UAVsStartSlot = 0;

    const std::shared_ptr<vaTexture>& anyRT = ( renderTarget != NULL ) ? ( renderTarget ) : ( depthStencil );

    if( updateViewport )
    {
        if( anyRT != nullptr )
        {
            assert( anyRT->GetType( ) == vaTextureType::Texture2D );   // others not supported (yet?)
            Viewport.X = 0;
            Viewport.Y = 0;
            Viewport.Width = anyRT->GetSizeX( );
            Viewport.Height = anyRT->GetSizeY( );
        }
        else
        {
            Viewport = vaViewport();
        }
    }

    if( renderTarget != NULL )
    {
        assert( ( renderTarget->GetBindSupportFlags( ) & vaResourceBindSupportFlags::RenderTarget ) != 0 );
    }
    if( depthStencil != NULL )
    {
        assert( ( depthStencil->GetBindSupportFlags( ) & vaResourceBindSupportFlags::DepthStencil ) != 0 );
    }
}

void vaRenderOutputs::SetUnorderedAccessViews( uint32 numUAVs, const std::shared_ptr<vaShaderResource>* UAVs, bool updateViewport )
{
    SetRenderTargetsAndUnorderedAccessViews( 0, nullptr, nullptr, numUAVs, UAVs, updateViewport );
}

void vaRenderOutputs::SetRenderTargetsAndUnorderedAccessViews( uint32 numRTs, const std::shared_ptr<vaTexture>* renderTargets, const std::shared_ptr<vaTexture>& depthStencil, uint32 numUAVs, const std::shared_ptr<vaShaderResource>* UAVsArg, bool updateViewport )
{
    assert( numRTs <= vaRenderOutputs::c_maxRTs );
    assert( numUAVs <= vaRenderOutputs::c_maxUAVs );
    this->RenderTargetCount = numRTs = vaMath::Min( numRTs, vaRenderOutputs::c_maxRTs );
    numUAVs = vaMath::Min( numUAVs, vaRenderOutputs::c_maxUAVs );

    for( size_t i = 0; i < vaRenderOutputs::c_maxRTs; i++ )
        this->RenderTargets[i] = ( i < this->RenderTargetCount ) ? ( renderTargets[i] ) : ( nullptr );
    for( size_t i = 0; i < vaRenderOutputs::c_maxUAVs; i++ )
    {
        this->UnorderedAccessViews[i] = ( i < numUAVs ) ? ( UAVsArg[i] ) : ( nullptr );
        //this->UAVInitialCounts[i] = ( ( i < this->UAVCount ) && ( UAVInitialCountsArg != nullptr ) ) ? ( UAVInitialCountsArg[i] ) : ( -1 );
    }
    this->DepthStencil = depthStencil;
    //this->UAVsStartSlot = UAVStartSlot;

    const vaFramePtr<vaTexture>& anyRT = ( this->RenderTargets[0] != NULL ) ? ( this->RenderTargets[0] ) : ( depthStencil );

    if( updateViewport )
    {
        if( anyRT != nullptr )
        {
            assert( anyRT->GetType( ) == vaTextureType::Texture2D );   // others not supported (yet?)
            Viewport.X = 0;
            Viewport.Y = 0;
            Viewport.Width = anyRT->GetSizeX( );
            Viewport.Height = anyRT->GetSizeY( );
        }
        else
        {
            Viewport = vaViewport( );
        }
    }

    for( size_t i = 0; i < this->RenderTargetCount; i++ )
    {
        if( this->RenderTargets[i] != nullptr )
        {
            assert( ( this->RenderTargets[i]->GetBindSupportFlags( ) & vaResourceBindSupportFlags::RenderTarget ) != 0 );
        }
    }
    for( size_t i = 0; i < numUAVs; i++ )
    {
        if( this->UnorderedAccessViews[i] != nullptr )
        {
            assert( ( this->UnorderedAccessViews[i]->GetBindSupportFlags( ) & vaResourceBindSupportFlags::UnorderedAccess ) != 0 );
        }
    }
    if( depthStencil != NULL )
    {
        assert( ( depthStencil->GetBindSupportFlags( ) & vaResourceBindSupportFlags::DepthStencil ) != 0 );
    }
}

void vaRenderOutputs::SetRenderTargets( uint32 numRTs, const std::shared_ptr<vaTexture>* renderTargets, const std::shared_ptr<vaTexture>& depthStencil, bool updateViewport )
{
    SetRenderTargetsAndUnorderedAccessViews( numRTs, renderTargets, depthStencil, 0, nullptr, updateViewport );
}

thread_local vaRenderDeviceThreadLocal vaRenderDevice::s_threadLocal;

void vaRenderDevice::RegisterModules( )
{
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaSceneRenderer );
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaRenderInstanceStorage );
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaRenderGlobals );
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaRenderMaterial );
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaPostProcess );
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaPostProcessTonemap );
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaPostProcessBlur );
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaASSAOLite );
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaGTAO );
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaTAA );
    //VA_RENDERING_MODULE_REGISTER_GENERIC( vaTextureTools );
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaRenderMaterialManager );
    // VA_RENDERING_MODULE_REGISTER_GENERIC( vaAssetPackManager );
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaSkybox );
    VA_RENDERING_MODULE_REGISTER_GENERIC( vaPathTracer );
}

vaRenderDevice::vaRenderDevice( )
{ 
    static bool modulesRegistered = false;
    if( !modulesRegistered )
    {
        modulesRegistered = true;
        RegisterModules( );
    }

    m_profilingEnabled = true; 

//    vaRenderingModuleRegistrar::CreateSingletonIfNotCreated( );

    s_threadLocal.RenderThread = true;

    vaUIManager::GetInstance( ).RegisterMenuItemHandler( "Rendering", m_aliveToken, std::bind( &vaRenderDevice::UIMenuHandler, this, std::placeholders::_1 ) );
}

void vaRenderDevice::UIMenuHandler( vaApplicationBase & application )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    if( ImGui::MenuItem( "Recompile shaders", "CTRL+R" ) )
    {
        vaShader::ReloadAll( );
    }
#endif
    m_renderGlobals->UIMenuHandler( application );
}

void vaRenderDevice::InitializeBase( )
{
    assert( IsRenderThread() );

    m_canvas2D  = shared_ptr< vaDebugCanvas2D >( new vaDebugCanvas2D( vaRenderingModuleParams( *this ) ) );
    m_canvas3D  = shared_ptr< vaDebugCanvas3D >( new vaDebugCanvas3D( vaRenderingModuleParams( *this ) ) );

    m_textureTools      = std::make_shared<vaTextureTools>( *this );
    m_renderMaterialManager = CreateModule<vaRenderMaterialManager>( );

    m_assetPackManager  = std::make_shared<vaAssetPackManager>( *this );

    m_renderMeshManager = CreateModule<vaRenderMeshManager>( );
    m_renderGlobals     = CreateModule<vaRenderGlobals>( );

    // fullscreen triangle stuff & related
    {
        m_PPConstants = CreateModule< vaConstantBuffer>( );
        m_PPConstants->Create( sizeof( PostProcessConstants ), nullptr, true, 0 );

        m_fsVertexShader                = CreateModule< vaVertexShader> ( );
        m_copyResourcePS                = CreateModule< vaPixelShader > ( );
        m_vertexShaderStretchRect       = CreateModule< vaVertexShader> ( );
        m_pixelShaderStretchRectLinear  = CreateModule< vaPixelShader > ( );
        m_pixelShaderStretchRectPoint   = CreateModule< vaPixelShader > ( );

        std::vector<vaVertexInputElementDesc> inputElements;
        inputElements.push_back( { "SV_Position", 0,    vaResourceFormat::R32G32B32A32_FLOAT,    0,  0, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
        inputElements.push_back( { "TEXCOORD", 0,       vaResourceFormat::R32G32_FLOAT,          0, 16, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );

        // full screen pass vertex shader
        {
            const char * pVSString = "void main( inout const float4 xPos : SV_Position, inout float2 UV : TEXCOORD0 ) { }";
            m_fsVertexShader->CreateShaderAndILFromBuffer( pVSString, "main", inputElements, vaShaderMacroContaner(), false );
        }

        // copy resource shader
        {
            string shaderCode = 
                "Texture2D g_source           : register( t0 );                 \n"
                "float4 main( in const float4 xPos : SV_Position ) : SV_Target  \n"
                "{                                                              \n"
                "   return g_source.Load( int3( xPos.xy, 0 ) );                 \n"
                "}                                                              \n";

            m_copyResourcePS->CreateShaderFromBuffer( shaderCode, "main", vaShaderMacroContaner(), false );
        }

        m_vertexShaderStretchRect->CreateShaderAndILFromFile(   "vaPostProcess.hlsl", "VSStretchRect", inputElements, { }, false );
        m_pixelShaderStretchRectLinear->CreateShaderFromFile(   "vaPostProcess.hlsl", "PSStretchRectLinear", { }, false );
        m_pixelShaderStretchRectPoint->CreateShaderFromFile(    "vaPostProcess.hlsl", "PSStretchRectPoint", { }, false );

        {
            // using one big triangle
            SimpleVertex fsVertices[3];
            float z = 0.0f;
            fsVertices[0] = SimpleVertex( -1.0f,  1.0f, z, 1.0f, 0.0f, 0.0f );
            fsVertices[1] = SimpleVertex( 3.0f,   1.0f, z, 1.0f, 2.0f, 0.0f );
            fsVertices[2] = SimpleVertex( -1.0f, -3.0f, z, 1.0f, 0.0f, 2.0f );

            m_fsVertexBufferZ0 = vaRenderBuffer::Create<SimpleVertex>( *this, countof( fsVertices ), vaRenderBufferFlags::VertexIndexBuffer, fsVertices, "FSVertexBufferZ0" );
        }

        {
            // using one big triangle
            SimpleVertex fsVertices[3];
            float z = 1.0f;
            fsVertices[0] = SimpleVertex( -1.0f,  1.0f, z, 1.0f, 0.0f, 0.0f );
            fsVertices[1] = SimpleVertex( 3.0f,   1.0f, z, 1.0f, 2.0f, 0.0f );
            fsVertices[2] = SimpleVertex( -1.0f, -3.0f, z, 1.0f, 0.0f, 2.0f );

            m_fsVertexBufferZ1 = vaRenderBuffer::Create<SimpleVertex>( *this, countof( fsVertices ), vaRenderBufferFlags::VertexIndexBuffer, fsVertices, "FSVertexBufferZ1" );
        }

        // this still lets all of them compile in parallel, just ensures they're done before leaving the function
        m_fsVertexShader->WaitFinishIfBackgroundCreateActive( );
        m_copyResourcePS->WaitFinishIfBackgroundCreateActive( );
        m_vertexShaderStretchRect->WaitFinishIfBackgroundCreateActive( );
        m_pixelShaderStretchRectLinear->WaitFinishIfBackgroundCreateActive( );
        m_pixelShaderStretchRectPoint->WaitFinishIfBackgroundCreateActive( );
    }
}

void vaRenderDevice::DeinitializeBase( )
{
    m_asyncBeginFrameCallbacks.InvokeAndDeactivate( *this, std::numeric_limits<float>::lowest() );

    assert( IsRenderThread() );
    m_renderGlobals                 = nullptr;
    m_PPConstants                   = nullptr;
    m_fsVertexShader                = nullptr;
    m_fsVertexBufferZ0              = nullptr;
    m_fsVertexBufferZ1              = nullptr;
    m_copyResourcePS                = nullptr;
    m_vertexShaderStretchRect       = nullptr;
    m_pixelShaderStretchRectLinear  = nullptr;
    m_pixelShaderStretchRectPoint   = nullptr;
    m_canvas2D                      = nullptr;
    m_canvas3D                      = nullptr;
    m_assetPackManager              = nullptr;
    m_textureTools                  = nullptr;
    m_postProcess                   = nullptr;
    m_renderMaterialManager         = nullptr;
    m_renderMeshManager             = nullptr;
    m_shaderManager                 = nullptr;
    m_currentBackbuffer.Reset();
    vaBackgroundTaskManager::GetInstancePtr()->ClearAndRestart();
}

vaRenderDevice::~vaRenderDevice( ) 
{ 
    assert( IsRenderThread() );
    assert( !m_frameStarted );
    assert( m_disabled );
    assert( m_renderGlobals == nullptr );   // forgot to call DeinitializeBase()?
}

void vaRenderDevice::ExecuteAsyncBeginFrameCallbacks( float deltaTime )
{
    m_asyncBeginFrameCallbacks.Invoke( *this, deltaTime );
}

vaTextureTools & vaRenderDevice::GetTextureTools( )
{
    assert( IsRenderThread() );
    assert( m_textureTools != nullptr );
    return *m_textureTools;
}

vaRenderMaterialManager & vaRenderDevice::GetMaterialManager( )
{
    assert( m_renderMaterialManager != nullptr );
    return *m_renderMaterialManager;
}

vaRenderMeshManager &     vaRenderDevice::GetMeshManager( )
{
    assert( m_renderMeshManager != nullptr );
    return *m_renderMeshManager;
}

vaAssetPackManager &     vaRenderDevice::GetAssetPackManager( )
{
    assert( m_assetPackManager != nullptr );
    return *m_assetPackManager;
}

vaRenderGlobals & vaRenderDevice::GetRenderGlobals( )
{
    assert( IsRenderThread() );
    assert( m_renderGlobals != nullptr );
    return *m_renderGlobals;
}

vaPostProcess & vaRenderDevice::GetPostProcess( )
{
    assert( IsRenderThread( ) );
    if( m_postProcess == nullptr )
        m_postProcess = CreateModule<vaPostProcess>();
    return *m_postProcess;
}

void vaRenderDevice::BeginFrame( float deltaTime )
{
    assert( !m_disabled );
    assert( IsRenderThread() );
    m_totalTime += deltaTime;
    m_lastDeltaTime = deltaTime;
    assert( !m_frameStarted );
    m_frameStarted = true;
    m_currentFrameIndex++;

#ifdef VA_IMGUI_INTEGRATION_ENABLED
    // for DX12 there's an empty frame done before creating the swapchain - ignore some of the checks during that
    if( m_swapChainTextureSize != vaVector2i(0, 0) && deltaTime > 0 )
    {
        assert( m_imguiFrameStarted );      // ImGui always has frame set so anyone can imgui anything at any time (if on the main thread)
    }
#endif

    // implementer's class responsibility
    // m_mainDeviceContext->BeginFrame();

    e_BeginFrame( deltaTime );

    m_currentBackbuffer.SetRenderTarget( GetCurrentBackbufferTexture( ), nullptr, true );
}

void vaRenderDevice::EndAndPresentFrame( int vsyncInterval )
{
    assert( !m_disabled );
    assert( IsRenderThread() );
    vsyncInterval;

    // implementer's class responsibility
    // m_mainDeviceContext->EndFrame( );

    assert( m_frameStarted );
    m_frameStarted = false;

#ifdef VA_IMGUI_INTEGRATION_ENABLED
    {
        VA_TRACE_CPU_SCOPE( ImGuiEndFrameNewFrame );

        if( m_imguiFrameStarted )
        {
            // if we haven't rendered anything, reset imgui to avoid any unnecessary accumulation
            ImGuiEndFrame( );
        }

        // // for DX12 there's an empty frame done before creating the swapchain - ignore some of the checks during that
        // if( m_swapChainTextureSize != vaVector2i(0, 0) )
        // {
        //     m_imguiFrameRendered = false;
        // }
    }
#endif
    m_currentBackbuffer.Reset();
}

void vaRenderDevice::FillFullscreenPassGraphicsItem( vaGraphicsItem & graphicsItem, bool zIs0 ) const
{
    assert( !m_disabled );

    // this should be thread-safe as long as the lifetime of the device is guaranteed
    graphicsItem.Topology         = vaPrimitiveTopology::TriangleList;
    graphicsItem.VertexShader     = GetFSVertexShader();
    graphicsItem.VertexBuffer     = (zIs0)?(GetFSVertexBufferZ0()):(GetFSVertexBufferZ1());
    graphicsItem.DrawType         = vaGraphicsItem::DrawType::DrawSimple;
    graphicsItem.DrawSimpleParams.VertexCount = 3;
}

namespace Vanilla
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    void ImSetBigClearSansRegular( ImFont * font );
    void ImSetBigClearSansBold(    ImFont * font );
#endif`
}

void vaRenderDevice::ImGuiCreate( )
{
    assert( IsRenderThread() );
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
        
    io.Fonts->AddFontDefault();

#if 0   // custom fonts 
    // this would be a good place for DPI scaling.
    float bigSizePixels = 26.0f;
    float displayOffset = -1.0f;

    ImFontConfig fontConfig;

    vaFileTools::EmbeddedFileData fontFileData;

    fontFileData = vaFileTools::EmbeddedFilesFind( "fonts:\\ClearSans-Regular.ttf" );
    if( fontFileData.HasContents( ) )
    {
        void * imguiBuffer = ImGui::MemAlloc( (int)fontFileData.MemStream->GetLength() );
        memcpy( imguiBuffer, fontFileData.MemStream->GetBuffer(), (int)fontFileData.MemStream->GetLength() );

        ImFont * font = io.Fonts->AddFontFromMemoryTTF( imguiBuffer, (int)fontFileData.MemStream->GetLength(), bigSizePixels, &fontConfig );
        font->DisplayOffset.y += displayOffset;   // Render 1 pixel down
        ImSetBigClearSansRegular( font );
    }

    fontFileData = vaFileTools::EmbeddedFilesFind( "fonts:\\ClearSans-Bold.ttf" );
    if( fontFileData.HasContents( ) )
    {
        void * imguiBuffer = ImGui::MemAlloc( (int)fontFileData.MemStream->GetLength() );
        memcpy( imguiBuffer, fontFileData.MemStream->GetBuffer(), (int)fontFileData.MemStream->GetLength() );

        ImFont * font = io.Fonts->AddFontFromMemoryTTF( imguiBuffer, (int)fontFileData.MemStream->GetLength(), bigSizePixels, &fontConfig );
        font->DisplayOffset.y += displayOffset;   // Render 1 pixel down
        ImSetBigClearSansBold( font );
    }
#endif

    // enable docking
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif
}

void vaRenderDevice::ImGuiDestroy( )
{
    assert( IsRenderThread() );
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui::DestroyContext();
#endif
}

void vaRenderDevice::ImGuiEndFrame( )
{
    assert( !m_disabled );

#ifdef VA_IMGUI_INTEGRATION_ENABLED
    assert( m_imguiFrameStarted );
    ImGui::EndFrame();
    m_imguiFrameStarted = false;
#endif
}

void vaRenderDevice::ImGuiRender( const vaRenderOutputs & renderOutputs, vaRenderDeviceContext & renderContext )
{
    assert( !m_disabled );

    renderContext;
    assert( m_frameStarted );
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGuiEndFrameAndRender( renderOutputs, renderContext );
#endif
}

// useful for copying individual MIPs, in which case use Views created with vaTexture::CreateView
vaDrawResultFlags vaRenderDevice::CopySRVToRTV( vaRenderDeviceContext & renderContext, shared_ptr<vaTexture> destination, shared_ptr<vaTexture> source )
{
    vaRenderOutputs scratchOutputs; 
    scratchOutputs.SetRenderTarget( destination, nullptr, true );

    if( destination == nullptr
        || destination->GetType( ) != source->GetType( )
        || destination->GetSizeX( ) != source->GetSizeX( )
        || destination->GetSizeY( ) != source->GetSizeY( )
        || destination->GetSizeZ( ) != source->GetSizeZ( )
        || destination->GetSampleCount( ) != source->GetSampleCount( )
        )
    {
        assert( false );
        VA_ERROR( "vaRenderDevice::CopySRVToRTV - not supported or incorrect parameters" );
        return vaDrawResultFlags::UnspecifiedError;
    }

    vaGraphicsItem renderItem;
    FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ShaderResourceViews[0] = source;
    renderItem.PixelShader = renderContext.GetRenderDevice( ).GetFSCopyResourcePS( );
    return renderContext.ExecuteSingleItem( renderItem, scratchOutputs, nullptr );
}
    
vaDrawResultFlags vaRenderDevice::StretchRect( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture, const vaVector4 & _dstRect, const vaVector4 & _srcRect, bool linearFilter, vaBlendMode blendMode, const vaVector4 & colorMul, const vaVector4 & colorAdd )
{
    VA_TRACE_CPUGPU_SCOPE( PP_StretchRect, renderContext );

    vaRenderOutputs scratchOutputs;
    scratchOutputs.SetRenderTarget( dstTexture, nullptr, true );

    vaVector4 dstRect = _dstRect;
    vaVector4 srcRect = _srcRect;

    if( dstRect == vaVector4::Zero )
    {
        if( dstTexture != nullptr )
            dstRect = { 0, 0, (float)dstTexture->GetSizeX( ), (float)dstTexture->GetSizeY( ) };
        else
        {
            assert( false );
        }
    }

    if( srcRect == vaVector4::Zero )
        srcRect = { 0, 0, (float)srcTexture->GetSizeX( ), (float)srcTexture->GetSizeY( ) };
    assert( dstRect != vaVector4::Zero );

    // not yet supported / tested
    assert( dstRect.x == 0 );
    assert( dstRect.y == 0 );

    vaVector2 dstPixSize = vaVector2( 1.0f / ( dstRect.z - dstRect.x ), 1.0f / ( dstRect.w - dstRect.y ) );

    vaVector2 srcPixSize = vaVector2( 1.0f / (float)srcTexture->GetSizeX( ), 1.0f / (float)srcTexture->GetSizeY( ) );

    PostProcessConstants consts;
    consts.Param1.x = dstPixSize.x * dstRect.x * 2.0f - 1.0f;
    consts.Param1.y = 1.0f - dstPixSize.y * dstRect.y * 2.0f;
    consts.Param1.z = dstPixSize.x * dstRect.z * 2.0f - 1.0f;
    consts.Param1.w = 1.0f - dstPixSize.y * dstRect.w * 2.0f;

    consts.Param2.x = srcPixSize.x * srcRect.x;
    consts.Param2.y = srcPixSize.y * srcRect.y;
    consts.Param2.z = srcPixSize.x * srcRect.z;
    consts.Param2.w = srcPixSize.y * srcRect.w;

    consts.Param3 = colorMul;
    consts.Param4 = colorAdd;

    //consts.Param2.x = (float)viewport.Width
    //consts.Param2 = dstRect;

    m_PPConstants->Upload( renderContext, &consts, sizeof(consts) );

    vaGraphicsItem renderItem;
    FillFullscreenPassGraphicsItem( renderItem );

    renderItem.ConstantBuffers[POSTPROCESS_CONSTANTSBUFFERSLOT] = m_PPConstants;
    renderItem.ShaderResourceViews[POSTPROCESS_TEXTURE_SLOT0] = srcTexture;

    renderItem.VertexShader = m_vertexShaderStretchRect;
    //renderItem.VertexShader->WaitFinishIfBackgroundCreateActive();
    renderItem.PixelShader = ( linearFilter ) ? ( m_pixelShaderStretchRectLinear ) : ( m_pixelShaderStretchRectPoint );
    //renderItem.PixelShader->WaitFinishIfBackgroundCreateActive();
    renderItem.BlendMode = blendMode;

    return renderContext.ExecuteSingleItem( renderItem, scratchOutputs, nullptr );
}

void vaRenderDevice::GetMultithreadingParams( int & outAvailableCPUThreads, int & outWorkerCount ) const
{
    outAvailableCPUThreads = 1;
#if 1 && defined(VA_TASKFLOW_INTEGRATION_ENABLED)   // enable multithreading / worker contexts
    outAvailableCPUThreads += (int)vaTF::Executor( ).num_workers( );
#endif
    outWorkerCount = m_multithreadedWorkerCount;
}