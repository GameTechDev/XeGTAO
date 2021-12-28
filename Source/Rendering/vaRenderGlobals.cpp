///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Rendering/vaRenderGlobals.h"

#include "Rendering/vaRenderDeviceContext.h"

#include "Rendering/vaShader.h"

#include "Rendering/vaRenderBuffers.h"

#include "Rendering/vaTexture.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Core/vaInput.h"
#include "Core/vaApplicationBase.h"

#include "Scene/vaScene.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"
#include "Rendering/vaAssetPack.h"

#include "Rendering/vaDebugCanvas.h"

#include "Rendering/vaSceneRaytracing.h"


using namespace Vanilla;

/////////////////////////////////////////////////////////////////////////////
// this is for right/middle click context menu
struct UIContextItem
{
    weak_ptr<vaScene>           Scene;
    entt::entity                Entity  = entt::null;
    weak_ptr<struct vaAssetRenderMesh>
                                RenderMeshAsset;
    weak_ptr<struct vaAssetRenderMaterial>
                                RenderMaterialAsset;
    vaVector3                   WorldspacePos;
    float                       ViewspaceDepth;
};
static int                          s_renderGlobalInstances = 0;   
static bool                         s_ui_contextReset = false;
//static vaVector2i                   s_ui_contextViewportPos;
//static vaVector3                    s_ui_contextWorldPos;
//static float                        s_ui_contextViewspaceDepth;
static std::vector<UIContextItem>   s_ui_contextItems;
/////////////////////////////////////////////////////////////////////////////


vaRenderGlobals::vaRenderGlobals( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params ),
    vaUIPanel( "RenderDebug", 0, !VA_MINIMAL_UI_BOOL, vaUIPanel::DockLocation::DockedLeftBottom ),
    m_constantBuffer( vaConstantBuffer::Create<ShaderGlobalConstants>( params.RenderDevice, "ShaderGlobalConstants" ) )
    //m_genericDataCaptureCS( params ),
    //m_cursorCaptureCS( params )
{ 
//    assert( vaRenderingCore::IsInitialized() );

    for( int i = 0; i < _countof( m_genericDataCaptureGPUTextures ); i++ )
    {
        m_genericDataCaptureGPUTextures[i] = vaTexture::Create2D( GetRenderDevice( ), vaResourceFormat::R32_UINT, SHADERGLOBAL_GENERICDATACAPTURE_COLUMNS, SHADERGLOBAL_GENERICDATACAPTURE_ROWS+1, 1, 1, 1, vaResourceBindSupportFlags::UnorderedAccess );
        m_genericDataCaptureCPUTextures[i] = vaTexture::Create2D( GetRenderDevice( ), vaResourceFormat::R32_UINT, SHADERGLOBAL_GENERICDATACAPTURE_COLUMNS, SHADERGLOBAL_GENERICDATACAPTURE_ROWS+1, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead | vaResourceAccessFlags::CPUReadManuallySynced );
        m_genericDataCaptureCPUTexturesHasData[i] = false;
    }

    for( int i = 0; i < _countof( m_shaderFeedbackStaticGPU ); i++ )
    {
        m_shaderFeedbackStaticGPU[i] = vaRenderBuffer::Create<ShaderFeedbackStatic>( GetRenderDevice(), 1, vaRenderBufferFlags::None, "ShaderFeedbackStatic" );
        m_shaderFeedbackStaticCPU[i] = vaRenderBuffer::Create<ShaderFeedbackStatic>( GetRenderDevice(), 1, vaRenderBufferFlags::Readback, "ShaderFeedbackStaticReadback" );
        m_shaderFeedbackStaticCPUHasData[i] = false;
        m_shaderFeedbackDynamicGPU[i] = vaRenderBuffer::Create<ShaderFeedbackDynamic>( GetRenderDevice( ), ShaderFeedbackDynamic::MaxItems, vaRenderBufferFlags::None, "ShaderFeedbackDynamic" );
        m_shaderFeedbackDynamicCPU[i] = vaRenderBuffer::Create<ShaderFeedbackDynamic>( GetRenderDevice( ), ShaderFeedbackDynamic::MaxItems, vaRenderBufferFlags::Readback, "ShaderFeedbackDynamicReadback" );
    }

    // for( int i = 0; i < _countof(m_cursorCaptureGPUTextures); i++ )
    // {
    //     m_cursorCaptureGPUTextures[i] = vaTexture::Create1D( GetRenderDevice(), vaResourceFormat::R32_UINT, SHADERGLOBAL_CURSORINFO_TOTALSIZE, 1, 1, vaResourceBindSupportFlags::UnorderedAccess );
    //     m_cursorCaptureCPUTextures[i] = vaTexture::Create1D( GetRenderDevice(), vaResourceFormat::R32_UINT, SHADERGLOBAL_CURSORINFO_TOTALSIZE, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead | vaResourceAccessFlags::CPUReadManuallySynced );
    //     m_cursorCaptureCPUTexturesHasData[i] = false;
    // }

    m_debugDrawDepth            = false;
    m_debugDrawNormalsFromDepth = false;

    //m_genericDataCaptureCS->CompileFromFile( L"vaHelperTools.hlsl", "cs_5_0", "GenericDataCaptureMSCS", { pair< string, string >( "VA_UPDATE_GENERIC_DATA_CAPTURE_SPECIFIC", "" ) }, false );
    //m_cursorCaptureCS->CompileFromFile( L"vaHelperTools.hlsl", "cs_5_0", "CursorCaptureCS", { pair< string, string >( "VA_UPDATE_3D_CURSOR_SPECIFIC", "" ) }, false );

    params.RenderDevice.e_BeforeEndFrame.AddWithToken( m_aliveToken, [ thisPtr = this ]( vaRenderDevice & device )
    {
        thisPtr->DigestGenericDataCapture( *device.GetMainContext() );
    }
    );

    vaUIManager::GetInstance().e_BeforeDrawUI.AddWithToken( m_aliveToken, [ thisPtr = this ]( vaRenderDeviceContext & deviceContext )
    {
        thisPtr->ProcessShaderFeedback( deviceContext );
    }
    );

    //vaUIManager::GetInstance( ).RegisterMenuItemHandler( "Rendering", m_aliveToken, std::bind( &vaRenderGlobals::UIMenuHandler, this, std::placeholders::_1 ) );

    s_renderGlobalInstances++;
}

vaRenderGlobals::~vaRenderGlobals( )
{
    s_renderGlobalInstances--;
    if( s_renderGlobalInstances == 0 )
        std::swap( s_ui_contextItems, std::vector<UIContextItem>{} );
}

void vaRenderGlobals::UpdateShaderConstants( vaRenderDeviceContext & renderContext, const vaDrawAttributes * drawAttributes )
{
    ShaderGlobalConstants consts;
    memset( &consts, 0, sizeof(consts) );

    if( drawAttributes != nullptr )
    {
        const vaCameraBase & camera = drawAttributes->Camera;
        const vaViewport & viewport = drawAttributes->Camera.GetViewport();

        consts.View                             = drawAttributes->Camera.GetViewMatrix( );
        consts.ViewInv                          = drawAttributes->Camera.GetInvViewMatrix( );
        consts.Proj                             = drawAttributes->Camera.GetProjMatrix( );

        if( (drawAttributes->RenderFlagsAttrib & vaDrawAttributes::RenderFlags::SetZOffsettedProjMatrix) != 0 )
            consts.Proj = drawAttributes->Camera.ComputeZOffsettedProjMatrix( 1.0002f, 0.0002f );

        consts.ViewProj                         = consts.View * consts.Proj;
        consts.ProjInv                          = consts.Proj.Inversed();
        consts.ViewProjInv                      = consts.ViewProj.Inversed();

        consts.WorldBase                        = vaVector4( drawAttributes->Settings.WorldBase, 0.0f );
        assert( consts.WorldBase == vaVector4(0,0,0,0) ); // Nowadays for WorldBase to work it also needs PreviousWorldBase and correct fix in shaders - see below
        consts.PreviousWorldBase                = consts.WorldBase; //vaVector4( drawAttributes->Settings.PreviousWorldBase, 0.0f );

        consts.CameraDirection                  = vaVector4( drawAttributes->Camera.GetDirection().Normalized(), 0.0f );
        consts.CameraRightVector                = vaVector4( drawAttributes->Camera.GetRightVector().Normalized(), 0.0f );
        consts.CameraUpVector                   = vaVector4( drawAttributes->Camera.GetUpVector().Normalized(), 0.0f );
        consts.CameraWorldPosition              = vaVector4( drawAttributes->Camera.GetPosition()-drawAttributes->Settings.WorldBase, 0.0f );
        consts.CameraSubpixelOffset             = vaVector4( drawAttributes->Camera.GetSubpixelOffset(), 0.0f, 0.0f );

        {
            consts.ViewportSize                 = vaVector2( (float)viewport.Width, (float)viewport.Height );
            consts.ViewportPixelSize            = vaVector2( 1.0f / (float)viewport.Width, 1.0f / (float)viewport.Height );
            consts.ViewportHalfSize             = vaVector2( (float)viewport.Width*0.5f, (float)viewport.Height*0.5f );
            consts.ViewportPixel2xSize          = vaVector2( 2.0f / (float)viewport.Width, 2.0f / (float)viewport.Height );

            float depthLinearizeMul             = -consts.Proj.m[3][2];         // float depthLinearizeMul = ( clipFar * clipNear ) / ( clipFar - clipNear );
            float depthLinearizeAdd             = consts.Proj.m[2][2];          // float depthLinearizeAdd = clipFar / ( clipFar - clipNear );
            // correct the handedness issue. need to make sure this below is correct, but I think it is.
            if( depthLinearizeMul * depthLinearizeAdd < 0 )
                depthLinearizeAdd = -depthLinearizeAdd;
            consts.DepthUnpackConsts            = vaVector2( depthLinearizeMul, depthLinearizeAdd );

            float tanHalfFOVY                   = 1.0f / consts.Proj.m[1][1];    // = tanf( drawAttributes->Camera.GetYFOV( ) * 0.5f );
            float tanHalfFOVX                   = 1.0F / consts.Proj.m[0][0];    // = tanHalfFOVY * drawAttributes->Camera.GetAspect( );
            consts.CameraTanHalfFOV     = vaVector2( tanHalfFOVX, tanHalfFOVY );

            float clipNear  = drawAttributes->Camera.GetNearPlaneDistance();
            float clipFar   = drawAttributes->Camera.GetFarPlaneDistance();
            consts.CameraNearFar        = vaVector2( clipNear, clipFar );

            consts.FOVXY                        = { camera.GetXFOV(), camera.GetYFOV() };
            consts.PixelFOVXY                   = { camera.GetXFOV() / (float)viewport.Width, camera.GetYFOV() / viewport.Height };
        }

        consts.Noise = drawAttributes->Settings.Noise;

        consts.GenericDataCollectEnabled        = drawAttributes->Settings.GenericDataCollect ? 1 : 0;

        consts.TransparencyPass                 = 0.0f; //( ( flags & vaDrawAttributes::RenderFlags::TransparencyPass) != 0 )?( 1.0f ) : ( 0.0f );
        consts.WireframePass                    = ( ( drawAttributes->RenderFlagsAttrib & vaDrawAttributes::RenderFlags::DebugWireframePass) != 0 )?( 1.0f ) : ( 0.0f );
    
        consts.GlobalMIPOffset                  = drawAttributes->Settings.MIPOffset;
        consts.GlobalSpecularAAScale            = drawAttributes->Settings.SpecularAAScale;
        consts.GlobalSpecialEmissiveScale       = drawAttributes->Settings.SpecialEmissiveScale;
    
        consts.EV100                            = drawAttributes->Camera.GetEV100( true );
        consts.PreExposureMultiplier            = drawAttributes->Camera.GetPreExposureMultiplier( true );
        consts.HDRClamp                         = drawAttributes->Camera.GetHDRClamp( );// * consts.PreExposureMultiplier;


        if( drawAttributes->Raytracing == nullptr )
        {
            consts.RaytracingMIPOffset          = 0;
            //consts.RaytracingMIPSlopeModifier   = 0;
        }
        else
        {
            auto & rtSettings = drawAttributes->Raytracing->Settings();
            consts.RaytracingMIPOffset          = rtSettings.MIPOffset;
            //consts.RaytracingMIPSlopeModifier   = rtSettings.MIPSlopeModifier;
        }
        consts.ReprojectionMatrix               = drawAttributes->Settings.ReprojectionMatrix;
        consts.CameraJitterDelta                = drawAttributes->Settings.CameraJitterDelta;
    }
    // this default is only correct if the viewport is full window
    vaVector2i cursorPos = ( drawAttributes != nullptr && drawAttributes->Settings.CursorViewportPos != vaVector2i(-1, -1) )?( drawAttributes->Settings.CursorViewportPos ):( 
        (vaInputMouseBase::GetCurrent( ) != nullptr )? (vaInputMouseBase::GetCurrent( )->GetCursorClientPosDirect( )) : (vaVector2i(-1,-1) ) );
    consts.CursorViewportPosition           = (vaVector2)cursorPos + vaVector2{ 0.5, 0.5f };
    consts.CursorHoverItemCaptureEnabled    = (drawAttributes != nullptr && drawAttributes->Settings.CursorHoverInfoCollect)?(1):(0);   // or should it be 1 when there's no draw attributes?
    consts.CursorKeyClicked                 = ( vaInputMouseBase::GetCurrent( ) != nullptr && vaInputMouseBase::GetCurrent( )->IsKeyClicked( MK_Left ) )?( 1<<0 ):(0);

    consts.AlphaTAAHackEnabled              = GetRenderDevice().GetMaterialManager().GetAlphaTAAHackEnabled( )?(1):(0);
    
    consts.FrameIndexMod64                  = GetRenderDevice().GetCurrentFrameIndex() % 64;

    double totalTime = GetRenderDevice().GetTotalTime();
    consts.TimeFract                        = (float)fmod( totalTime, 1.0 );
    consts.TimeFmod3600                     = (float)fmod( totalTime, 360.0 );
    consts.SinTime2Pi                       = (float)sin( totalTime * 2.0 * VA_PI );
    consts.SinTime1Pi                       = (float)sin( totalTime * VA_PI );

    m_constantBuffer->Upload( renderContext, consts );
}

void vaRenderGlobals::UpdateAndSetToGlobals( vaRenderDeviceContext & renderContext, vaShaderItemGlobals & shaderItemGlobals, const vaDrawAttributes * drawAttributes )
{
    UpdateShaderConstants( renderContext, drawAttributes );

    assert( shaderItemGlobals.ConstantBuffers[ SHADERGLOBAL_CONSTANTSBUFFERSLOT ] == nullptr );
    shaderItemGlobals.ConstantBuffers[ SHADERGLOBAL_CONSTANTSBUFFERSLOT ] = m_constantBuffer;

    if( drawAttributes != nullptr && drawAttributes->Settings.GenericDataCollect )
    {
        int currentWriteIndex = GetRenderDevice( ).GetCurrentFrameIndex( ) % _countof( m_genericDataCaptureGPUTextures );
        if( m_genericDataCaptureStarted < GetRenderDevice( ).GetCurrentFrameIndex( ) )
        {
            // re-think this - can't use ClearUAV because it triggers BeginComputeItems, but we're already in BeginGraphicsItems here and recursion is not allowed
            // perhaps just m_genericDataCaptureGPUTextures[currentWriteIndex]->UpdateSubresources( ) to 0 ? only counter value needs cleaning anyhow? maybe redesign the whole thing a bit?
            assert( false );
            // renderContext.ClearUAV( m_genericDataCaptureGPUTextures[currentWriteIndex], 0u );
            m_genericDataCaptureStarted = GetRenderDevice( ).GetCurrentFrameIndex( );
        }

        assert( shaderItemGlobals.UnorderedAccessViews[SHADERGLOBAL_GENERIC_OUTPUT_DATA_UAV_SLOT] == nullptr );
        shaderItemGlobals.UnorderedAccessViews[SHADERGLOBAL_GENERIC_OUTPUT_DATA_UAV_SLOT] = m_genericDataCaptureGPUTextures[currentWriteIndex];
    }
    else
    {
        assert( shaderItemGlobals.UnorderedAccessViews[SHADERGLOBAL_GENERIC_OUTPUT_DATA_UAV_SLOT] == nullptr );
        shaderItemGlobals.UnorderedAccessViews[SHADERGLOBAL_GENERIC_OUTPUT_DATA_UAV_SLOT] = nullptr;
    }

    int currentWriteIndex = GetRenderDevice( ).GetCurrentFrameIndex( ) % _countof( m_shaderFeedbackStaticGPU );

    // Only clear before first time it's used this frame!
    if( m_shaderFeedbackStartedFrame < GetRenderDevice( ).GetCurrentFrameIndex( ) )
    {
        ShaderFeedbackStatic initStatic; memset( &initStatic, 0, sizeof(initStatic) );
        m_shaderFeedbackStaticGPU[currentWriteIndex]->UploadSingle<ShaderFeedbackStatic>( renderContext, initStatic, 0 );
        m_shaderFeedbackStartedFrame = GetRenderDevice( ).GetCurrentFrameIndex( );
    }
    assert( shaderItemGlobals.UnorderedAccessViews[SHADERGLOBAL_SHADER_FEEDBACK_STATIC_UAV_SLOT] == nullptr );
    shaderItemGlobals.UnorderedAccessViews[SHADERGLOBAL_SHADER_FEEDBACK_STATIC_UAV_SLOT] = m_shaderFeedbackStaticGPU[currentWriteIndex];
    assert( shaderItemGlobals.UnorderedAccessViews[SHADERGLOBAL_SHADER_FEEDBACK_DYNAMIC_UAV_SLOT] == nullptr );
    shaderItemGlobals.UnorderedAccessViews[SHADERGLOBAL_SHADER_FEEDBACK_DYNAMIC_UAV_SLOT] = m_shaderFeedbackDynamicGPU[currentWriteIndex];
}

void vaRenderGlobals::ProcessShaderFeedback( vaRenderDeviceContext& renderContext )
{
    VA_TRACE_CPUGPU_SCOPE( ProcessShaderFeedback, renderContext );

    assert( m_shaderFeedbackStartedFrame <= GetRenderDevice( ).GetCurrentFrameIndex( ) && m_shaderFeedbackProcessedFrame < GetRenderDevice( ).GetCurrentFrameIndex( ) );
    if( m_shaderFeedbackProcessedFrame >= GetRenderDevice( ).GetCurrentFrameIndex( ) )
        return;
    m_shaderFeedbackProcessedFrame = GetRenderDevice( ).GetCurrentFrameIndex( );

    // 1.) get data from first ready CPU resource
    int oldestWriteIndex = GetRenderDevice( ).GetCurrentFrameIndex( ) % _countof( m_shaderFeedbackStaticGPU );
    if( m_shaderFeedbackStaticCPUHasData[oldestWriteIndex] )
    {
        m_shaderFeedbackStaticCPU[oldestWriteIndex]->Readback( m_shaderFeedbackLastCapture );
        m_shaderFeedbackLastCapture.CursorHoverInfoCounter  = std::min( m_shaderFeedbackLastCapture.CursorHoverInfoCounter, ShaderFeedbackStatic::MaxCursorHoverInfoItems );
        m_shaderFeedbackLastCapture.DynamicItemCounter      = std::min( m_shaderFeedbackLastCapture.DynamicItemCounter, ShaderFeedbackDynamic::MaxItems );
        if( m_shaderFeedbackLastCapture.DynamicItemCounter > 0 )
        {
            //m_shaderFeedbackDynamicCPU[oldestWriteIndex]->Map( vaResourceMapType::Read );
            DigestShaderFeedbackInfo( reinterpret_cast<ShaderFeedbackDynamic*>(m_shaderFeedbackDynamicCPU[oldestWriteIndex]->GetMappedData()), m_shaderFeedbackLastCapture.DynamicItemCounter );
            //m_shaderFeedbackDynamicCPU[oldestWriteIndex]->Unmap( );
        }
        else
            DigestShaderFeedbackInfo( nullptr, 0 );
        m_shaderFeedbackStaticCPUHasData[oldestWriteIndex] = false;
    }
    else
        m_shaderFeedbackLastCapture = {};

    // 2.) enqueue resource GPU->CPU copy if we had any new data this frame
    if( m_shaderFeedbackStartedFrame == GetRenderDevice( ).GetCurrentFrameIndex( ) )
    {
        int currentWriteIndex = GetRenderDevice( ).GetCurrentFrameIndex( ) % _countof( m_shaderFeedbackStaticGPU );
        m_shaderFeedbackStaticCPU[currentWriteIndex]->CopyFrom( renderContext, *m_shaderFeedbackStaticGPU[currentWriteIndex], 0, 0, m_shaderFeedbackStaticGPU[currentWriteIndex]->GetDataSize() );
        m_shaderFeedbackDynamicCPU[currentWriteIndex]->CopyFrom( renderContext, *m_shaderFeedbackDynamicGPU[currentWriteIndex], 0, 0, m_shaderFeedbackDynamicGPU[currentWriteIndex]->GetDataSize() );
        m_shaderFeedbackStaticCPUHasData[currentWriteIndex] = true;
    }

    // we must work on the main context due to mapping limitations
    assert( &renderContext == GetRenderDevice( ).GetMainContext( ) );

}

void vaRenderGlobals::DigestGenericDataCapture( vaRenderDeviceContext & renderContext )
{
    VA_TRACE_CPUGPU_SCOPE( DigestGenericDataCapture, renderContext );

    assert( m_genericDataCaptureStarted <= GetRenderDevice( ).GetCurrentFrameIndex( ) && m_genericDataCaptureFinalized < GetRenderDevice( ).GetCurrentFrameIndex( ) );
    if( m_genericDataCaptureFinalized >= GetRenderDevice( ).GetCurrentFrameIndex( ) )
        return;
    m_genericDataCaptureFinalized = GetRenderDevice( ).GetCurrentFrameIndex( );


    // 1.) get data from first ready CPU resource
    int oldestWriteIndex = GetRenderDevice( ).GetCurrentFrameIndex( ) % _countof( m_genericDataCaptureGPUTextures );
    const shared_ptr<vaTexture>& readTex = m_genericDataCaptureCPUTextures[oldestWriteIndex];
    if( m_genericDataCaptureCPUTexturesHasData[oldestWriteIndex] )
    {
        if( readTex->TryMap( renderContext, vaResourceMapType::Read, false ) )
        {
            uint32 itemCount = readTex->GetMappedData()[0].PixelAt<uint32>( 0, 0 );

            itemCount = std::min( itemCount, (uint32)SHADERGLOBAL_GENERICDATACAPTURE_ROWS );

            m_genericDataCaptured.HasData = true;
            m_genericDataCaptured.NumRows = itemCount;

            vaTextureMappedSubresource & subRes = readTex->GetMappedData()[0];
            for( uint32 r = 0; r < itemCount; r++ )
                for( uint32 c = 0; c < SHADERGLOBAL_GENERICDATACAPTURE_COLUMNS; c++ )
                {
                    float a = subRes.PixelAt<float>( c, r+1 );
                    m_genericDataCaptured.Data[r][c] = a;
                }

            readTex->Unmap( renderContext );
        }
        else
        {
            VA_LOG_ERROR( "Couldn't read 3d cursor buffer info!" );
        }
        m_genericDataCaptureCPUTexturesHasData[oldestWriteIndex] = false;
    }
    else
    {
        m_genericDataCaptured.Reset( );
    }

    // 2.) enqueue resource GPU->CPU copy if we had any new data this frame
    int currentWriteIndex = GetRenderDevice( ).GetCurrentFrameIndex( ) % _countof( m_genericDataCaptureGPUTextures );
    if( m_genericDataCaptureStarted == GetRenderDevice( ).GetCurrentFrameIndex( ) )
    { 
        const shared_ptr<vaTexture> & src = m_genericDataCaptureGPUTextures[currentWriteIndex];
        const shared_ptr<vaTexture> & dst = m_genericDataCaptureCPUTextures[currentWriteIndex];
        dst->CopyFrom( renderContext, src );
        m_genericDataCaptureCPUTexturesHasData[currentWriteIndex] = true;
    }

    // we must work on the main context due to mapping limitations
    assert( &renderContext == GetRenderDevice( ).GetMainContext( ) );
}

void vaRenderGlobals::DigestShaderFeedbackInfo( ShaderFeedbackDynamic dynamicItems[], int dynamicItemCount )
{
    m_cursorHoverInfoItems.clear( );
    
    auto & lastCapture = m_shaderFeedbackLastCapture;
    for( int i = 0; i < lastCapture.CursorHoverInfoCounter; i++ )
    {
        bool skip = false;
        for( int j = 0; j < (int)m_cursorHoverInfoItems.size(); j++ )
        {
            // resolve duplicates - just use the closest one
            if( lastCapture.CursorHoverInfoItems[i].OriginInfo == m_cursorHoverInfoItems[j].OriginInfo )
            {
                if( lastCapture.CursorHoverInfoItems[i].ViewspaceDepth < m_cursorHoverInfoItems[j].ViewspaceDepth )
                    m_cursorHoverInfoItems[j] = lastCapture.CursorHoverInfoItems[i];
                skip = true;
            }
        }
        if( skip )
            continue;
        else
            m_cursorHoverInfoItems.push_back( lastCapture.CursorHoverInfoItems[i] );
    }

    if( !m_freezeDebugDrawItems )
    {
        m_debugDrawItems.clear();
        for( int i = 0; i < dynamicItemCount; i++ )
            m_debugDrawItems.push_back( dynamicItems[i] );
    }

    auto & canvas2D = vaDebugCanvas2D::GetInstance(); canvas2D;
    auto & canvas3D = vaDebugCanvas3D::GetInstance(); canvas3D;

    for( ShaderFeedbackDynamic & item : m_debugDrawItems )
    {
        vaVector4ui & ref1ui = reinterpret_cast<vaVector4ui &>(item.Ref1);

        vaVector4 color = vaVector4::Saturate(item.Color);

        uint shadowColor = vaVector4( 0, 0, 0, color.w ).ToBGRA();

        // a switch would be nice
        if( item.Type == ShaderFeedbackDynamic::Type_LogTextNewLine )
            VA_LOG( "Shader: <newline>", ref1ui.x  );                // just do an empty newline instead?
        else if( item.Type == ShaderFeedbackDynamic::Type_LogTextUINT )
            VA_LOG( "Shader: UINT: %u", ref1ui.x  );
        else if( item.Type == ShaderFeedbackDynamic::Type_LogTextUINT4 )
            VA_LOG( "Shader: UINT4: %u, %u, %u, %u", ref1ui.x, ref1ui.y, ref1ui.z, ref1ui.w );
        else if( item.Type == ShaderFeedbackDynamic::Type_LogTextFLT )
            VA_LOG( "Shader: FLOAT: %.10f", item.Ref1.x );
        else if( item.Type == ShaderFeedbackDynamic::Type_LogTextFLT2 )
            VA_LOG( "Shader: FLOAT4: %.10f, %.10f", item.Ref1.x, item.Ref1.y );
        else if( item.Type == ShaderFeedbackDynamic::Type_LogTextFLT3 )
            VA_LOG( "Shader: FLOAT4: %.10f, %.10f, %.10f", item.Ref1.x, item.Ref1.y, item.Ref1.z );
        else if( item.Type == ShaderFeedbackDynamic::Type_LogTextFLT4 )
            VA_LOG( "Shader: FLOAT4: %.10f, %.10f, %.10f, %.10f", item.Ref1.x, item.Ref1.y, item.Ref1.z, item.Ref1.w );
        else if( item.Type == ShaderFeedbackDynamic::Type_2DLine )
            canvas2D.DrawLine( item.Ref0.AsVec2( ), item.Ref1.AsVec2( ), color.ToBGRA() );
        else if( item.Type == ShaderFeedbackDynamic::Type_2DCircle )
            canvas2D.DrawCircle( item.Ref0.AsVec2( ), item.Ref0.z, color.ToBGRA( ) );
        else if( item.Type == ShaderFeedbackDynamic::Type_2DRectangle )
            canvas2D.DrawRectangle( item.Ref0.AsVec2( ), item.Ref1.AsVec2( ), color.ToBGRA( ) );
        else if( item.Type == ShaderFeedbackDynamic::Type_2DTextUINT )
            canvas2D.DrawText( item.Ref0.x, item.Ref0.y, color.ToBGRA( ), shadowColor, "%u", ref1ui.x );
        else if( item.Type == ShaderFeedbackDynamic::Type_2DTextUINT4 )
            canvas2D.DrawText( item.Ref0.x, item.Ref0.y, color.ToBGRA( ), shadowColor, "%u, %u, %u, %u", ref1ui.x, ref1ui.y, ref1ui.z, ref1ui.w );
        else if( item.Type == ShaderFeedbackDynamic::Type_2DTextFLT )
            canvas2D.DrawText( item.Ref0.x, item.Ref0.y, color.ToBGRA( ), shadowColor, "%f", item.Ref1.x );
        else if( item.Type == ShaderFeedbackDynamic::Type_2DTextFLT4 )
            canvas2D.DrawText( item.Ref0.x, item.Ref0.y, color.ToBGRA( ), shadowColor, "%f, %f, %f, %f", item.Ref1.x, item.Ref1.y, item.Ref1.z, item.Ref1.w );
        else if( item.Type == ShaderFeedbackDynamic::Type_3DTextUINT )
            canvas2D.DrawText3D( canvas3D.GetLastCamera( ), item.Ref0.AsVec3(), {item.Param1, item.Param2}, color.ToBGRA( ), shadowColor, "%u", ref1ui.x );
        else if( item.Type == ShaderFeedbackDynamic::Type_3DTextUINT4 )
            canvas2D.DrawText3D( canvas3D.GetLastCamera( ), item.Ref0.AsVec3(), {item.Param1, item.Param2}, color.ToBGRA( ), shadowColor, "%u, %u, %u, %u", ref1ui.x, ref1ui.y, ref1ui.z, ref1ui.w );
        else if( item.Type == ShaderFeedbackDynamic::Type_3DTextFLT )
            canvas2D.DrawText3D( canvas3D.GetLastCamera( ), item.Ref0.AsVec3(), {item.Param1, item.Param2}, color.ToBGRA( ), shadowColor, "%f", item.Ref1.x );
        else if( item.Type == ShaderFeedbackDynamic::Type_3DTextFLT4 )
            canvas2D.DrawText3D( canvas3D.GetLastCamera( ), item.Ref0.AsVec3(), {item.Param1, item.Param2}, color.ToBGRA( ), shadowColor, "%f, %f, %f, %f", item.Ref1.x, item.Ref1.y, item.Ref1.z, item.Ref1.w );
        else if( item.Type == ShaderFeedbackDynamic::Type_3DLine )
            canvas3D.DrawLine( item.Ref0.AsVec3( ), item.Ref1.AsVec3( ), color.ToBGRA( ) );
        else if( item.Type == ShaderFeedbackDynamic::Type_3DSphere )
            canvas3D.DrawSphere( item.Ref0.AsVec3( ), item.Ref0.w, 0, color.ToBGRA( ) );
        else if( item.Type == ShaderFeedbackDynamic::Type_3DBox )
            canvas3D.DrawBox( item.Ref0.AsVec3( ), item.Ref1.AsVec3(), 0, color.ToBGRA( ) );
        else if( item.Type == ShaderFeedbackDynamic::Type_3DCylinder )
            canvas3D.DrawCylinder( item.Ref0.AsVec3( ), item.Ref1.AsVec3( ), item.Ref0.w, item.Ref1.w, 0, color.ToBGRA( ) );
        else if( item.Type == ShaderFeedbackDynamic::Type_3DArrow )
            canvas3D.DrawArrow( item.Ref0.AsVec3( ), item.Ref1.AsVec3( ), item.Ref0.w, 0, vaVector4( vaVector3::Saturate(color.AsVec3()*0.7f), color.w ).ToBGRA( ), vaVector4::Saturate(color*1.3f).ToBGRA( ) );
        else if( item.Type == ShaderFeedbackDynamic::Type_3DSphereCone )
            canvas3D.DrawSphereCone( item.Ref0.AsVec3( ), item.Ref1.AsVec3( ), item.Ref0.w, item.Ref1.w, 0, color.ToBGRA( ) );
        else if( item.Type == ShaderFeedbackDynamic::Type_3DLightViz )
            canvas3D.DrawLightViz( item.Ref0.AsVec3( ), item.Ref1.AsVec3( ), item.Ref0.w, item.Ref1.w, item.Param1, item.Param2, color.AsVec3() );
    }

    if( lastCapture.AssertFlag > 0 )
    {
        VA_WARN( "A shader has called DebugAssert with %u and %f parameters. (TODO: add shader identifier)", lastCapture.AssertPayloadUINT, lastCapture.AssertPayloadFLOAT );
        // assert( false ); // <- probably not needed, can't miss the above?
    }
}


void vaRenderGlobals::UIPanelTick( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    //ImGui::Text( "TODO (future): " );
    //ImGui::Text( "      [ ] draw triangle, pos, color" );
    //ImGui::Text( "      [ ] draw cone, pos, color" );
    if( ImGui::CollapsingHeader( "CursorHoverInfo" ) )
    {
        ImGui::Text( "CursorHoverInfo count: %d", m_cursorHoverInfoItems.size() );
    }
    //if( m_shaderFeedbackLastCapture.DebugUINT4Items > 0 )
    //{
    //    {
    //        ImGui::Text( "Debug UINT4s: " );
    //        VA_GENERIC_RAII_SCOPE( ImGui::Indent( ); , ImGui::Unindent( ); );
    //        for( int i = 0; i < m_shaderFeedbackLastCapture.DebugUINT4Counter; i++ )
    //        {
    //            auto & v = m_shaderFeedbackLastCapture.DebugUINT4Items[i];
    //            ImGui::Text( "%d: %u, %u, %u, %u", i, v.x, v.y, v.z, v.w );
    //        }
    //    }
    //    {
    //        ImGui::Text( "Debug FLOAT4s: " );
    //        VA_GENERIC_RAII_SCOPE( ImGui::Indent( );, ImGui::Unindent( ); );
    //        for( int i = 0; i < m_shaderFeedbackLastCapture.DebugFLOAT4Counter; i++ )
    //        {
    //            auto& v = m_shaderFeedbackLastCapture.DebugFLOAT4Items[i];
    //            ImGui::Text( "%d: %f, %f, %f, %f", i, v.x, v.y, v.z, v.w );
    //        }
    //    }
    //    ImGui::Text( "Dynamic debug items: %d", m_shaderFeedbackLastCapture.DynamicItemCounter );
    //}
#endif
}

void vaRenderGlobals::UIPanelTickAlways( vaApplicationBase& application )
{
    application;

#ifdef VA_IMGUI_INTEGRATION_ENABLED

    // we need this so we don't get mixed up with some other 
    VA_GENERIC_RAII_SCOPE( ImGui::PushID( this ); , ImGui::PopID( ); );

    const char* popupName = "RightClick3DContextMenu";
    if( !ImGui::IsPopupOpen( popupName ) )
    {
        //s_ui_contextViewportPos     = { std::numeric_limits<int>::min( ), std::numeric_limits<int>::min( ) };
        //s_ui_contextWorldPos        = { std::numeric_limits<float>::quiet_NaN( ), std::numeric_limits<float>::quiet_NaN( ), std::numeric_limits<float>::quiet_NaN( ) };
        //s_ui_contextViewspaceDepth  = std::numeric_limits<float>::quiet_NaN( );
        s_ui_contextReset = false;
        s_ui_contextItems.clear( );
    }

    bool openOrUpdateContext = false;
    if( !ImGui::IsWindowHovered( ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem ) )
    {
        if( ImGui::IsMouseClicked( 0 ) || ImGui::IsMouseClicked( 2 ) )
            s_ui_contextReset = true;
        if( ImGui::IsMouseClicked( 1 ) )
            openOrUpdateContext = true;
    }

    if( s_ui_contextReset )
    {
        if( ImGui::BeginPopup( popupName ) )
        {
            ImGui::CloseCurrentPopup( );
            ImGui::EndPopup( );
        }
        s_ui_contextReset = false;
    }

    if( m_enableContextMenu && openOrUpdateContext )
    {
        s_ui_contextItems.clear( );

        const auto & hoverInfoItems = m_cursorHoverInfoItems;
        if( hoverInfoItems.size() > 0 )
        {
            s_ui_contextReset = false;
            // s_ui_contextViewportPos = hoverInfo.ViewportPos;
            // s_ui_contextWorldPos = hoverInfo.WorldspacePos;
            // s_ui_contextViewspaceDepth = hoverInfo.ViewspaceDepth;

            auto & assetPackManager = GetRenderDevice( ).GetAssetPackManager( );

            for( int i = 0; i < (int)hoverInfoItems.size( ); i++ )
            {
                const auto& inItem = hoverInfoItems[i];
                UIContextItem outItem;

                outItem.Scene   = vaScene::FindByRuntimeID( inItem.OriginInfo.SceneID );

                outItem.Entity  = entt::null;
                auto scene = outItem.Scene.lock();
                if( scene != nullptr )
                    m_uiLastScene = outItem.Scene;

                if( scene != nullptr && scene->Registry().valid( entt::entity(inItem.OriginInfo.EntityID) ) )
                    outItem.Entity  = entt::entity(inItem.OriginInfo.EntityID);

                if( inItem.OriginInfo.MeshAssetID != DrawOriginInfo::NullSceneRuntimeID )
                    outItem.RenderMeshAsset = std::dynamic_pointer_cast<vaAssetRenderMesh, vaAsset>( assetPackManager.FindAsset( (uint64)inItem.OriginInfo.MeshAssetID ) );

                if( inItem.OriginInfo.MaterialAssetID != DrawOriginInfo::NullSceneRuntimeID )
                    outItem.RenderMaterialAsset = std::dynamic_pointer_cast<vaAssetRenderMaterial, vaAsset>( assetPackManager.FindAsset( (uint64)inItem.OriginInfo.MaterialAssetID ) );

                outItem.WorldspacePos = inItem.WorldspacePos;
                outItem.ViewspaceDepth = inItem.ViewspaceDepth;

                s_ui_contextItems.push_back( outItem );
            }

            ImGui::OpenPopup( popupName );
        }
    }

    if( ImGui::BeginPopup( popupName ) )
    {
        assert( m_enableContextMenu );

        VA_GENERIC_RAII_SCOPE( , ImGui::EndPopup( ); );

        // if( s_ui_contextItems.size( ) > 0 )
        // { 
        //     ImGui::Text( "Pixel {%d, %d}, depth: %.3f", s_ui_contextViewportPos.x, s_ui_contextViewportPos.y, s_ui_contextViewspaceDepth );
        //     ImGui::Text( "World pos: {%.3f, %.3f, %.3f}", s_ui_contextWorldPos.x, s_ui_contextWorldPos.y, s_ui_contextWorldPos.z );
        // }
        // ImGui::Separator( );

        //ImGui::Text( "Stuff under cursor: " );
        string indexstr;
        for( int i = 0; i < (int)s_ui_contextItems.size( ); i++ )
        {
            auto& item = s_ui_contextItems[i]; indexstr = vaStringTools::Format( "%d:", i );
            ImGui::Text( indexstr.c_str() );
            ImGui::SameLine();
            ImGui::TextColored( ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), " world pos: {%.3f, %.3f, %.3f}, view depth: %.3f", item.WorldspacePos.x, item.WorldspacePos.y, item.WorldspacePos.z, item.ViewspaceDepth );

            auto scene = item.Scene.lock();
            if( scene == nullptr )
            { assert( false ); continue; }

            if( ImGuiEx_SameLineSmallButtons( indexstr.c_str(), {"[marker]"}, {{false}}, false, {"Set scene helper marker to this position"} ) == 0 )
            {
                scene->UISetMarker( vaMatrix4x4::FromTranslation( item.WorldspacePos ) );
                ImGui::CloseCurrentPopup( );
            }

            VA_GENERIC_RAII_SCOPE( ImGui::Indent( ); , ImGui::Unindent( ); );
            VA_GENERIC_RAII_SCOPE( ImGui::PushID( i ); , ImGui::PopID( ); );

            if( item.Entity == entt::null )
            {
                ImGui::MenuItem( "Entity:    null/unknown", nullptr, false, false );
                //if( ImGui::BeginMenu( "Entity:    null/unknown", false ) )
                //    ImGui::EndMenu( );
            }
            else
            {
                string info = vaStringTools::Format( "Entity:    %s", Scene::GetNameAndID( scene->Registry( ), item.Entity ).c_str( ) );
                if( ImGui::BeginMenu( info.c_str( ), true ) )
                {
                    if( ImGui::MenuItem( "Highlight in scene view", nullptr, false, true ) )
                    {
                        scene->UIHighlight( item.Entity );
                        ImGui::CloseCurrentPopup( );
                    }
                    if( ImGui::MenuItem( "Open properties", nullptr, false, true ) )
                    {
                        scene->UIOpenProperties( item.Entity );
                        ImGui::CloseCurrentPopup( );
                    }
                    ImGui::EndMenu( );
                }
            }

            auto assetMenuInfo = [ & ]( const char* name, const shared_ptr<vaAsset>& asset )
            {
                if( asset == nullptr )
                {
                    ImGui::MenuItem( ( string( name ) + "null/unknown" ).c_str( ), nullptr, false, false );
                    //if( ImGui::BeginMenu( (string( name ) + "null/unknown").c_str(), false ) )
                    //    ImGui::EndMenu( );
                }
                else
                {
                    string info = name + asset->Name( );
                    if( ImGui::BeginMenu( info.c_str( ), true ) )
                    {
                        if( ImGui::MenuItem( "Highlight in asset pack", nullptr, false, true ) )
                        {
                            asset->UIHighlight( );
                            ImGui::CloseCurrentPopup( );
                        }
                        if( ImGui::MenuItem( "Open properties", nullptr, false, true ) )
                        {
                            asset->UIOpenProperties( );
                            ImGui::CloseCurrentPopup( );
                        }
                        ImGui::EndMenu( );
                    }
                }
            };

            assetMenuInfo( "Material:  ", item.RenderMaterialAsset.lock( ) );
            assetMenuInfo( "Mesh:      ", item.RenderMeshAsset.lock( ) );

            ImGui::Separator( );
        }
        if( s_ui_contextItems.size( ) == 0 )
        {
            ImGui::Text( "(no items of interest - possibly just skybox/background)" );
            ImGui::Separator( );
        }
        auto scene = m_uiLastScene.lock();
        ImGui::Text( "Scene helper marker: " );
        int buttonPress = ImGuiEx_SameLineSmallButtons( indexstr.c_str(), {"[unset]", "[set to camera]"}, {scene == nullptr || scene->UIGetMarker() == vaMatrix4x4::Degenerate, scene == nullptr}, false, {"Unset scene helper marker", "Set scene helper marker to camera"} );
        if( scene != nullptr && buttonPress == 0 )
            scene->UISetMarker( vaMatrix4x4::Degenerate );
        if( scene != nullptr && buttonPress == 1 )
            scene->UISetMarker( application.GetUICamera().GetWorldMatrix() );


        ImGui::TextColored( ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ), "Use middle mouse button or Ctrl+Enter to switch camera mode" );
    }
#endif

    if( ( vaInputKeyboardBase::GetCurrent( ) != NULL ) && vaInputKeyboardBase::GetCurrent( )->IsKeyDown( KK_CONTROL ) && vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( ( vaKeyboardKeys )'F' ) )
        m_freezeDebugDrawItems = !m_freezeDebugDrawItems;
}

void vaRenderGlobals::UIMenuHandler( vaApplicationBase & )
{
    ImGui::MenuItem( "Freeze debug draw items", "CTRL+F", &m_freezeDebugDrawItems );
}