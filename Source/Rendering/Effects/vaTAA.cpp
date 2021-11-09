///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Lukasz, Migas (Lukasz.Migas@intel.com) - TAA code, Filip Strugar (filip.strugar@intel.com) - integration
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaTAA.h"

#include "Rendering/vaRenderDevice.h"
#include "Rendering/vaRenderGlobals.h"
#include "Rendering/Shaders/vaShaderCore.h"
#include "Rendering/vaShader.h"
#include "Rendering/vaRenderBuffers.h"
#include "Rendering/vaTexture.h"
#include "Rendering/Shaders/vaTAAShared.h"

#include "Core/vaInput.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/vaRenderDeviceContext.h"

#include "Rendering/vaTextureHelpers.h"


using namespace Vanilla;

vaTAA::vaTAA( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params ),
    vaUIPanel( "TAA", 10, !VA_MINIMAL_UI_BOOL, vaUIPanel::DockLocation::DockedLeftBottom ),
    m_CSTAA( params ),
    m_CSFinalApply( params ),
    m_constantBuffer( vaConstantBuffer::Create<TAAConstants>( params.RenderDevice, "TAAConstants" ) )
{ 
}

void vaTAA::UIPanelTick( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui::Text( "Settings:" );
    ImGui::InputFloat( "Global texture MIP offset", &m_globalMIPOffset );   m_globalMIPOffset    = vaMath::Clamp( m_globalMIPOffset, -4.0f, 4.0f );
    ImGui::InputFloat( "Lerp mul", &m_lerpMul );                            m_lerpMul            = vaMath::Clamp( m_lerpMul, 0.9f, 1.0f );
    ImGui::InputFloat( "Lerp power", &m_lerpPow );                          m_lerpPow            = vaMath::Clamp( m_lerpPow, 0.1f, 10.0f );
    ImGui::InputFloat2( "Variance gamma", &m_varianceGammaMinMax.x );       m_varianceGammaMinMax= vaVector2::Clamp( m_varianceGammaMinMax, {0,0}, {1000,1000} );
    ImGui::Checkbox( "Use FP16", &m_paramUseFP16 );
    ImGui::Checkbox( "Use TGSM", &m_paramUseTGSM );
    ImGui::Checkbox( "Use Depth Threshold", &m_paramUseDepthThreshold );
    ImGui::Checkbox( "Use YCoCg space", &m_paramUseYCoCgSpace );
    ImGui::Separator( );
    ImGui::Text( "Debugging and experimental switches:" );
    ImGui::Checkbox( "Show motion vectors", &m_debugShowMotionVectors );
    ImGui::Checkbox( "Show no history pixels", &m_paramShowNoHistoryPixels );
    ImGui::Checkbox( "Disable subpixel jitter", &m_debugDisableAAJitter );
#endif
}

bool vaTAA::UpdateTexturesAndShaders( int width, int height )
{
    bool hadChanges = false;
    std::vector< pair< string, string > > newShaderMacros;

    // global shader switches
    if( m_debugShowMotionVectors )
        newShaderMacros.push_back( std::pair<std::string, std::string>( "TAA_SHOW_MOTION_VECTORS", "" ) );

    newShaderMacros.push_back( std::pair<std::string, std::string>( "USE_DEBUG_COLOUR_NO_HISTORY",  m_paramShowNoHistoryPixels  ?"1":"0" ) );
    newShaderMacros.push_back( std::pair<std::string, std::string>( "USE_FP16",                     m_paramUseFP16              ?"1":"0" ) );
    newShaderMacros.push_back( std::pair<std::string, std::string>( "USE_TGSM",                     m_paramUseTGSM              ?"1":"0" ) );
    newShaderMacros.push_back( std::pair<std::string, std::string>( "USE_DEPTH_THRESHOLD",          m_paramUseDepthThreshold    ?"1":"0" ) );
    newShaderMacros.push_back( std::pair<std::string, std::string>( "USE_YCOCG_SPACE",              m_paramUseYCoCgSpace        ?"1":"0" ) );

    if( newShaderMacros != m_staticShaderMacros )
    {
        m_staticShaderMacros = newShaderMacros;
        m_shadersDirty = true;
    }

    if( m_shadersDirty )
    {
        m_shadersDirty = false;

        string shaderFileToUse = "vaTAA.hlsl";

        // to allow parallel background compilation but still ensure they're all compiled after this function
        std::vector<shared_ptr<vaShader>> allShaders;
        allShaders.push_back( m_CSTAA.get() );
        allShaders.push_back( m_CSFinalApply.get() );

        m_CSTAA->CompileFromFile( shaderFileToUse, "CSTAA", m_staticShaderMacros, false );
        m_CSFinalApply->CompileFromFile( shaderFileToUse, "CSFinalApply", m_staticShaderMacros, false );

        // wait until shaders are compiled! this allows for parallel compilation
        for( auto sh : allShaders ) sh->WaitFinishIfBackgroundCreateActive();

        hadChanges = true;
    }

    bool needsUpdate = false;

    needsUpdate |= (m_size.x != width) || (m_size.y != height);

    m_size.x    = width;
    m_size.y    = height;

    if( needsUpdate )
    {
        hadChanges = true;

        //const vaResourceFormat colorFormat = vaResourceFormat::R32G32B32A32_FLOAT;
        const vaResourceFormat colorFormat = vaResourceFormat::R16G16B16A16_FLOAT;

        m_debugImage        = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R11G11B10_FLOAT, m_size.x, m_size.y, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_debugImage        ->SetName( "TAA_DebugImage" );
        //m_workingVisibility = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R16_UNORM, m_size.x, m_size.y, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_depthPrevious     = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32_FLOAT, m_size.x, m_size.y, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_depthPrevious     ->SetName( "TAA_DepthPrevious" );
        m_history           = vaTexture::Create2D( GetRenderDevice(), colorFormat, m_size.x, m_size.y, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_history           ->SetName( "TAA_DepthHistoryA" );
        m_historyPrevious   = vaTexture::Create2D( GetRenderDevice(), colorFormat, m_size.x, m_size.y, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_historyPrevious   ->SetName( "TAA_DepthHistoryB" );
    }

    return hadChanges;
}

vaVector2 vaTAA::ComputeJitter( int64 frameIndex )
{
    assert( m_previousFrameIndex != frameIndex );   // using twice within the same frame? that is not intended/supported
    m_previousFrameIndex    = m_currentFrameIndex;
    m_currentFrameIndex     = frameIndex;

    const float* Offset = nullptr;
    float Scale = 1.0f;
    // following work of Vaidyanathan et all: https://software.intel.com/content/www/us/en/develop/articles/coarse-pixel-shading-with-temporal-supersampling.html
    static const float Halton23_16[ 16 ][ 2 ] = { { 0.0f, 0.0f }, { 0.5f, 0.333333f }, { 0.25f, 0.666667f }, { 0.75f, 0.111111f }, { 0.125f, 0.444444f }, { 0.625f, 0.777778f }, { 0.375f ,0.222222f }, { 0.875f ,0.555556f }, { 0.0625f, 0.888889f }, { 0.562500f, 0.037037f }, { 0.3125f, 0.37037f }, { 0.8125f, 0.703704f }, { 0.1875f, 0.148148f }, { 0.6875f, 0.481481f }, { 0.4375f ,0.814815f }, { 0.9375f ,0.259259f } };
    static const float BlueNoise_16[ 16 ][ 2 ] = { { 1.5f, 0.59375f }, { 1.21875f, 1.375f }, { 1.6875f, 1.90625f }, { 0.375f, 0.84375f }, { 1.125f, 1.875f }, { 0.71875f, 1.65625f }, { 1.9375f ,0.71875f }, { 0.65625f ,0.125f }, { 0.90625f, 0.9375f }, { 1.65625f, 1.4375f }, { 0.5f, 1.28125f }, { 0.21875f, 0.0625f }, { 1.843750f, 0.312500f }, { 1.09375f, 0.5625f }, { 0.0625f, 1.21875f }, { 0.28125f, 1.65625f } };

#if 1 // halton
    Offset = Halton23_16[ frameIndex % 16 ];
    static vaVector2 correctionOffset = {-55,0};
    if( correctionOffset.x == -55 )
    {
        correctionOffset = {0,0};
        for( int i = 0; i < 16; i++ )
            correctionOffset += vaVector2(Halton23_16[i][0], Halton23_16[i][1]);
        correctionOffset /= 16.0f;
        correctionOffset = vaVector2(0.5f, 0.5f) - correctionOffset;
    }
#endif

    m_previousJitter    = m_currentJitter;
    m_currentJitter     = { (Offset[0]+correctionOffset.x) * Scale - 0.5f, (Offset[1]+correctionOffset.y) * Scale - 0.5f };

    if( m_debugDisableAAJitter )
        m_currentJitter = {0,0};

    return m_currentJitter;
}

void vaTAA::UpdateConstants( vaRenderDeviceContext & renderContext, const vaCameraBase & cameraBase, const vaMatrix4x4 & reprojectionMatrix, const vaVector2 & cameraJitterDelta )
{
    TAAConstants consts;

    // the scene should have been rendered with the current jitter - if not, there's a mismatch somewhere
    assert( cameraBase.GetSubpixelOffset( ) == m_currentJitter ); cameraBase;

    // TODO: remove local jitter delta computation? since we're getting it from outside anyhow
    vaVector2 jitterDelta           = m_previousJitter - m_currentJitter;
    assert( jitterDelta == cameraJitterDelta ); cameraJitterDelta;

    consts.ReprojectionMatrix       = reprojectionMatrix;
    consts.Consts.Resolution        = { (float)m_size.x, (float)m_size.y, 1.0f / (float)m_size.x, 1.0f / (float)m_size.y };
    consts.Consts.Jitter            = { jitterDelta.x, jitterDelta.y };
    consts.Consts.FrameNumber       = m_currentFrameIndex % 2;
    consts.Consts.DebugFlags        = 0;
    consts.Consts.LerpMul           = m_lerpMul;
    consts.Consts.LerpPow           = m_lerpPow;
    consts.Consts.VarClipGammaMin   = m_varianceGammaMinMax.x;
    consts.Consts.VarClipGammaMax   = m_varianceGammaMinMax.y;
    consts.Consts.PreExposureNewOverOld = cameraBase.GetPreExposureMultiplier( true ) / m_historyPreviousPreExposureMul;

    //if( m_resetHistory )
    //{
    //    consts.Consts.LerpMul = 0.0f;
    //    m_resetHistory = false;
    //}

    m_constantBuffer->Upload( renderContext, consts );
}

vaDrawResultFlags vaTAA::Apply( vaRenderDeviceContext & renderContext, const vaCameraBase & cameraBase, const shared_ptr<vaTexture> & motionVectors, const shared_ptr<vaTexture> & viewspaceDepth, const shared_ptr<vaTexture> & inoutColor, const vaMatrix4x4 & reprojectionMatrix, const vaVector2 & cameraJitterDelta )
{
    assert( inoutColor->GetSize( ) == viewspaceDepth->GetSize( ) );
    assert( viewspaceDepth->GetSampleCount( ) == 1 ); // MSAA no longer supported!

    UpdateTexturesAndShaders( viewspaceDepth->GetSizeX( ), viewspaceDepth->GetSizeY( ) );

    assert( !m_shadersDirty ); if( m_shadersDirty ) return vaDrawResultFlags::UnspecifiedError;

    // TODO: reprojection matrix is now part of global attributes - no need to pass it as local constants
    UpdateConstants( renderContext, cameraBase, reprojectionMatrix, cameraJitterDelta );

    if( m_resetHistory )
    {
        m_depthPrevious->ClearUAV( renderContext, vaVector4(0,0,0,0) );
        m_historyPrevious->ClearUAV( renderContext, vaVector4(0,0,0,0) );
        m_historyPreviousPreExposureMul = 1.0f;
        m_resetHistory = false;
    }

    vaComputeItem computeItemBase;
    // UAV barriers not required in current setup because UAV<->SRV barriers are automatically inserted; this however will not hold
    // in case of future modifications so beware :)
    computeItemBase.GlobalUAVBarrierBefore  = false;
    computeItemBase.GlobalUAVBarrierAfter   = false;

    // these are used by all passes
    computeItemBase.ConstantBuffers[TAA_CONSTANTSBUFFERSLOT]    = m_constantBuffer;
    // computeItem.ShaderResourceViews[5]  = GetRenderDevice().GetTextureTools().GetCommonTexture( vaTextureTools::CommonTextureName::BlueNoise64x64x1_3spp );

    // needed only for shader debugging viz
    vaDrawAttributes drawAttributes(cameraBase); 
    drawAttributes.Settings.ReprojectionMatrix = reprojectionMatrix;

    {
        VA_TRACE_CPUGPU_SCOPE( MainTAA, renderContext );

        vaComputeItem computeItem = computeItemBase;
        computeItem.ComputeShader = m_CSTAA;

        // input SRVs
        computeItem.ShaderResourceViews[0] = motionVectors;   // a.k.a. velocity buffer
        computeItem.ShaderResourceViews[1] = inoutColor;
        computeItem.ShaderResourceViews[2] = m_historyPrevious;
        computeItem.ShaderResourceViews[3] = viewspaceDepth;
        computeItem.ShaderResourceViews[4] = m_depthPrevious;

        computeItem.SetDispatch( (m_size.x + INTEL_TAA_NUM_THREADS_X-1) / INTEL_TAA_NUM_THREADS_X, (m_size.y + INTEL_TAA_NUM_THREADS_Y-1) / INTEL_TAA_NUM_THREADS_Y, 1 );

        renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs( m_history, nullptr, m_debugImage ), &drawAttributes );
    }

    {
        VA_TRACE_CPUGPU_SCOPE( FinalApply, renderContext );

        vaComputeItem computeItem = computeItemBase;
        computeItem.ComputeShader = m_CSFinalApply;

        // input SRVs
        computeItem.ShaderResourceViews[2] = m_history;

        computeItem.SetDispatch( (m_size.x + INTEL_TAA_NUM_THREADS_X-1) / INTEL_TAA_NUM_THREADS_X, (m_size.y + INTEL_TAA_NUM_THREADS_Y-1) / INTEL_TAA_NUM_THREADS_Y, 1 );

        renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs( inoutColor, nullptr, m_debugImage ), &drawAttributes );
    }

    m_depthPrevious->CopyFrom( renderContext, viewspaceDepth );

    //std::swap( m_depthPrevious, m_depth );
    std::swap( m_historyPrevious, m_history );

    m_historyPreviousPreExposureMul = cameraBase.GetPreExposureMultiplier( true );

    return vaDrawResultFlags::None;
}
