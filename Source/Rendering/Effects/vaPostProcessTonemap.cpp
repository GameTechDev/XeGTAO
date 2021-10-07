///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaPostProcessTonemap.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/vaShader.h"
#include "Rendering/vaRenderCamera.h"
#include "Rendering/vaTexture.h"
#include "Rendering/Effects/vaPostProcessBlur.h"
#include "Rendering/Shaders/vaSharedTypes.h"
#include "Rendering/Shaders/vaPostProcessShared.h"


using namespace Vanilla;

vaPostProcessTonemap::vaPostProcessTonemap( const vaRenderingModuleParams & params )
 : vaRenderingModule( params ),
    vaUIPanel( "Tonemap", 0, !VA_MINIMAL_UI_BOOL, vaUIPanel::DockLocation::DockedLeftBottom ),
    m_PSPassThrough( params ),
    m_PSTonemap( params ),
    m_PSTonemapWithLumaExport( params ),
    m_CSAvgLumHoriz( params ),
    m_CSAvgLumVert( params ),
    m_CSHalfResDownsampleAndAvgLum( params ),
    m_PSAddBloom( params ),
    m_CSDebugColorTest( params ),
    m_constantBuffer( vaConstantBuffer::Create<PostProcessTonemapConstants>( params.RenderDevice, "PostProcessTonemapConstants" ) )
{
    m_avgLuminance1x1 = vaTexture::Create2D( params.RenderDevice, vaResourceFormat::R32_FLOAT, 1, 1, 1, 1, 1, vaResourceBindSupportFlags::UnorderedAccess, vaResourceAccessFlags::Default );

    m_bloomBlur = params.RenderDevice.CreateModule<vaPostProcessBlur>();

    m_shadersDirty = true;
    
    // init to defaults (starts compiling shaders early)
    UpdateShaders( false );
}

vaPostProcessTonemap::~vaPostProcessTonemap( )
{
}

void vaPostProcessTonemap::UIPanelTick( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui::PushItemWidth( 120.0f );

    ImGui::Checkbox( "Gamma test", &m_dbgGammaTest );
    ImGui::Checkbox( "Dbg color test", &m_dbgColorTest );

    ImGui::PopItemWidth();
#endif
}

void vaPostProcessTonemap::UpdateConstants( vaRenderDeviceContext & renderContext, const vaRenderCamera & renderCamera, const std::shared_ptr<vaTexture> & preTonemapRadiance )
{
    // Constants
    {
        PostProcessTonemapConstants & consts = m_lastShaderConsts;
        consts.DbgGammaTest             = (m_dbgGammaTest)?(1.0f):(0.0f);
        consts.Exposure                 = renderCamera.ExposureSettings().Exposure;
        consts.WhiteLevel               = (renderCamera.TonemapSettings().UseModifiedReinhard)?(renderCamera.TonemapSettings().ModifiedReinhardWhiteLevel):( std::numeric_limits<float>::max() );       // modified Reinhard white level of FLT_MAX equals "regular" Reinhard
        consts.Saturation               = renderCamera.TonemapSettings().Saturation;

        consts.ViewportPixelSize        = vaVector2( 1.0f / (float)preTonemapRadiance->GetWidth(), 1.0f / (float)preTonemapRadiance->GetHeight() );

        consts.BloomMultiplier          = renderCamera.BloomSettings().BloomMultiplier;

        consts.FullResPixelSize         = vaVector2( 1.0f / (float)preTonemapRadiance->GetSizeX(), 1.0f / (float)preTonemapRadiance->GetSizeY() );
        consts.BloomSampleUVMul         = (m_halfResRadiance!=nullptr)?( vaVector2((float)m_halfResRadiance->GetSizeX() * 2.0f, (float)m_halfResRadiance->GetSizeY() * 2.0f) ):( vaVector2((float)preTonemapRadiance->GetSizeX(), (float)preTonemapRadiance->GetSizeY()) );
        consts.BloomSampleUVMul         = vaVector2( 1.0f / consts.BloomSampleUVMul.x, 1.0f / consts.BloomSampleUVMul.y );

        consts.PreExposureMultiplier    = renderCamera.GetPreExposureMultiplier( true );
        consts.WhiteLevelSquared        = consts.WhiteLevel * consts.WhiteLevel;

        consts.BloomMinThresholdPE      = renderCamera.BloomSettings().BloomMinThreshold * consts.PreExposureMultiplier;
        consts.BloomMaxClampPE          = renderCamera.BloomSettings().BloomMaxClamp * consts.PreExposureMultiplier;

        consts.Dummy0                   = 0.0f;

        m_constantBuffer->Upload( renderContext, consts );
    }
}

void vaPostProcessTonemap::UpdateShaders( bool waitCompileShaders )
{
    std::vector< pair< string, string > > newShaderMacros;

    // newShaderMacros.push_back( std::pair<std::string, std::string>( "SOME_MACRO_EXAMPLE", "" ) );

    if( newShaderMacros != m_staticShaderMacros )
    {
        m_staticShaderMacros = newShaderMacros;
        m_shadersDirty = true;
    }

    if( m_shadersDirty )
    {
        m_shadersDirty = false;

        m_PSPassThrough->CompileFromFile( "vaPostProcessTonemap.hlsl", "PSPassThrough", m_staticShaderMacros, false );
        m_PSTonemap->CompileFromFile( "vaPostProcessTonemap.hlsl", "PSTonemap", m_staticShaderMacros, false );
        m_PSTonemapWithLumaExport->CompileFromFile( "vaPostProcessTonemap.hlsl", "PSTonemapWithLumaExport", m_staticShaderMacros, false );

        m_CSAvgLumHoriz->CompileFromFile( "vaPostProcessTonemap.hlsl", "CSAvgLumHoriz", m_staticShaderMacros, false );
        m_CSAvgLumVert->CompileFromFile( "vaPostProcessTonemap.hlsl", "CSAvgLumVert", m_staticShaderMacros, false );

        m_CSHalfResDownsampleAndAvgLum->CompileFromFile( "vaPostProcessTonemap.hlsl", "CSHalfResDownsampleAndAvgLum", m_staticShaderMacros, false );
        m_PSAddBloom->CompileFromFile( "vaPostProcessTonemap.hlsl", "PSAddBloom", m_staticShaderMacros, false );

        m_CSDebugColorTest->CompileFromFile( "vaPostProcessTonemap.hlsl", "CSDebugColorTest", m_staticShaderMacros, false );
    }

    if( waitCompileShaders )
    {
        m_PSPassThrough->WaitFinishIfBackgroundCreateActive();
        m_PSTonemap->WaitFinishIfBackgroundCreateActive();
        m_PSTonemapWithLumaExport->WaitFinishIfBackgroundCreateActive();
        m_PSAddBloom->WaitFinishIfBackgroundCreateActive();
        m_CSAvgLumHoriz->WaitFinishIfBackgroundCreateActive();
        m_CSAvgLumVert->WaitFinishIfBackgroundCreateActive();
        m_CSHalfResDownsampleAndAvgLum->WaitFinishIfBackgroundCreateActive();
        m_PSAddBloom->WaitFinishIfBackgroundCreateActive();
        m_CSDebugColorTest->WaitFinishIfBackgroundCreateActive();
    }
}

vaDrawResultFlags vaPostProcessTonemap::TickAndApplyCameraPostProcess( vaRenderDeviceContext & renderContext, vaRenderCamera & renderCamera, const std::shared_ptr<vaTexture> & dstColor, const std::shared_ptr<vaTexture> & preTonemapRadiance, const AdditionalParams & additionalParams )
{
    vaDrawResultFlags renderResults = vaDrawResultFlags::None;
    const std::shared_ptr<vaTexture> & outExportLuma = additionalParams.OutExportLuma;

    VA_TRACE_CPUGPU_SCOPE( Tonemap, renderContext );

    // sorry, tonemapper no longer supports MSAA due to complexity 
    if( preTonemapRadiance->GetSampleCount() != 1 )
    { assert( false ); return vaDrawResultFlags::UnspecifiedError; }

    UpdateShaders( true );

    vaVector2i halfSize = vaVector2i( (preTonemapRadiance->GetSizeX() + 1) / 2, (preTonemapRadiance->GetSizeY() + 1) / 2 );
    if( (m_halfResRadiance == nullptr) || !( ( halfSize.x == m_halfResRadiance->GetSizeX() ) && ( halfSize.y == m_halfResRadiance->GetSizeY() ) && ( preTonemapRadiance->GetSRVFormat() == m_halfResRadiance->GetSRVFormat() ) ) )
        m_halfResRadiance = vaTexture::Create2D( GetRenderDevice(), preTonemapRadiance->GetSRVFormat(), halfSize.x, halfSize.y, 1, 1, 1, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess, vaResourceAccessFlags::Default );

    vaVector2i scratchSize = vaVector2i( (halfSize.x + 7)/8, (halfSize.y + 7)/8 );
    if( (m_avgLuminanceScratch == nullptr) || !( ( scratchSize.x == m_avgLuminanceScratch->GetSizeX() ) && ( scratchSize.y == m_avgLuminanceScratch->GetSizeY() ) ) )
        m_avgLuminanceScratch = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32_FLOAT, scratchSize.x, scratchSize.y, 1, 1, 1, vaResourceBindSupportFlags::UnorderedAccess );

    UpdateConstants( renderContext, renderCamera, preTonemapRadiance );

    if( m_dbgColorTest )
    {
        VA_TRACE_CPUGPU_SCOPE( GammaTest, renderContext );
        vaComputeItem computeItem; //computeItem.GlobalUAVBarrierBefore = false; computeItem.GlobalUAVBarrierAfter = false;
        computeItem.ConstantBuffers[POSTPROCESS_TONEMAP_CONSTANTSBUFFERSLOT] = m_constantBuffer;
        computeItem.ComputeShader = m_CSDebugColorTest;
        computeItem.SetDispatch( (preTonemapRadiance->GetWidth() + 8 - 1 ) / 8, (preTonemapRadiance->GetHeight() + 8 - 1 ) / 8, 1 );
        renderResults |= renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs(preTonemapRadiance), nullptr );
    }

    // Downsample to half x half - used for bloom
    {
        VA_TRACE_CPUGPU_SCOPE( Downsample, renderContext );
        vaComputeItem computeItem; //computeItem.GlobalUAVBarrierBefore = false; computeItem.GlobalUAVBarrierAfter = false;
        computeItem.ConstantBuffers[POSTPROCESS_TONEMAP_CONSTANTSBUFFERSLOT] = m_constantBuffer;
        computeItem.ShaderResourceViews[POSTPROCESS_TONEMAP_TEXTURE_SLOT0]   = preTonemapRadiance;
        computeItem.ComputeShader = m_CSHalfResDownsampleAndAvgLum;
        computeItem.SetDispatch( (m_halfResRadiance->GetWidth() + 8 - 1 ) / 8, (m_halfResRadiance->GetHeight() + 8 - 1 ) / 8, 1 );
        renderResults |= renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs(m_halfResRadiance, m_avgLuminanceScratch), nullptr );
    }

    if( !additionalParams.SkipCameraLuminanceUpdate )
    {
#if 0 // CPU-side avg luminance calculation to proof-test the GPU side
        static bool testCPUReduce = false;
        if( testCPUReduce )
        {
            auto GPUTex = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32G32B32A32_FLOAT, preTonemapRadiance->GetSizeX(), preTonemapRadiance->GetSizeY(), 1, 1, 1, vaResourceBindSupportFlags::RenderTarget );
            auto CPUTex = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32G32B32A32_FLOAT, preTonemapRadiance->GetSizeX(), preTonemapRadiance->GetSizeY(), 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead );
            renderContext.CopySRVToRTV( GPUTex, preTonemapRadiance );
            CPUTex->CopyFrom( renderContext, GPUTex );
            double avg = 0.0;
            auto calcLuminance = []( vaVector3 color ) -> float
            { return vaMath::Max( 0.000001f, vaVector3::Dot( color, vaVector3( 0.299f, 0.587f, 0.114f ) ) ); };
            float preExposureMultiplier = renderCamera.GetPreExposureMultiplier( true );

            if( CPUTex->TryMap( renderContext, vaResourceMapType::Read, false ) )
            {
                auto & mappedData = CPUTex->GetMappedData()[0];
                for( int x = 0; x < mappedData.SizeX; x++ )
                    for( int y = 0; y < mappedData.SizeY; y++ )
                    {
                        const vaVector3 & color = mappedData.PixelAt<vaVector4>( x, y ).AsVec3();
                        float luminance = calcLuminance( color );
                        luminance = luminance / preExposureMultiplier;
                        luminance = std::log( luminance );
                        avg += luminance;
                    }
                avg /= (float)(mappedData.SizeX*mappedData.SizeY);
                CPUTex->Unmap( renderContext );
            }
            int dbg = 0; dbg++;
        }
#endif

        vaComputeItem computeItem; //computeItem.GlobalUAVBarrierBefore = false; computeItem.GlobalUAVBarrierAfter = false;
        computeItem.ConstantBuffers[POSTPROCESS_TONEMAP_CONSTANTSBUFFERSLOT] = m_constantBuffer;
        {
            VA_TRACE_CPUGPU_SCOPE( AvgLumHoriz, renderContext );
            computeItem.ComputeShader = m_CSAvgLumHoriz;
            computeItem.SetDispatch( ( m_avgLuminanceScratch->GetWidth() + 64 - 1 ) / 64, 1, 1 );
            renderResults |= renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs( nullptr, m_avgLuminanceScratch, nullptr ), nullptr );
        }

        {
            VA_TRACE_CPUGPU_SCOPE( AvgLumVert, renderContext );
            computeItem.ComputeShader = m_CSAvgLumVert;
            computeItem.SetDispatch( 1, 1, 1 );
            renderResults |= renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs( nullptr, m_avgLuminanceScratch, m_avgLuminance1x1 ), nullptr );
        }

        if( renderResults == vaDrawResultFlags::None )
            renderCamera.UpdateLuminance( renderContext, m_avgLuminance1x1 );
    }

    if( renderCamera.BloomSettings().UseBloom && renderCamera.Settings().EnablePostProcess )
    {
        float bloomSize = renderCamera.BloomSettings().BloomSize * (float)((renderCamera.GetYFOVMain())?(preTonemapRadiance->GetSizeY()):(preTonemapRadiance->GetSizeX())) / 100.0f;

        VA_TRACE_CPUGPU_SCOPE( HalfResBlur, renderContext );

        // Blur
        renderResults |= m_bloomBlur->BlurToScratch( renderContext, m_halfResRadiance, bloomSize / 2.0f );
    }

    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ConstantBuffers[POSTPROCESS_TONEMAP_CONSTANTSBUFFERSLOT] = m_constantBuffer;

    // Apply bloom as a separate pass
    // TODO: combine into tone mapping / resolve (WARNING, make sure it still works if additionalParams.SkipTonemapper true)
    if( renderCamera.BloomSettings().UseBloom && renderCamera.Settings().EnablePostProcess )
    {
        VA_TRACE_CPUGPU_SCOPE( AddBloom, renderContext );

        renderItem.ShaderResourceViews[POSTPROCESS_TONEMAP_TEXTURE_SLOT0]   = m_bloomBlur->GetLastScratch();
        renderItem.PixelShader  = m_PSAddBloom;
        renderItem.BlendMode    = vaBlendMode::Additive;
        renderResults |= renderContext.ExecuteSingleItem( renderItem, vaRenderOutputs::FromRTDepth(preTonemapRadiance), nullptr );
        renderItem.BlendMode    = vaBlendMode::Opaque;
    }

    if( renderResults == vaDrawResultFlags::None )
    {
        VA_TRACE_CPUGPU_SCOPE( Apply, renderContext );

        UpdateConstants( renderContext, renderCamera, preTonemapRadiance );

        vaRenderOutputs renderOutputs = vaRenderOutputs::FromRTDepth( dstColor );

        m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
        renderItem.ConstantBuffers[POSTPROCESS_TONEMAP_CONSTANTSBUFFERSLOT] = m_constantBuffer;
        
        if( outExportLuma != nullptr )
            renderOutputs.UnorderedAccessViews[1]   = outExportLuma;

        renderItem.ShaderResourceViews[POSTPROCESS_TONEMAP_TEXTURE_SLOT0]   = preTonemapRadiance;

        // Apply tonemapping
        if( renderCamera.Settings().EnablePostProcess && !additionalParams.SkipTonemapper )
        {
            //renderContext.FullscreenPassDraw( *m_PSTonemap );
            renderItem.PixelShader  = (outExportLuma!=nullptr)?(m_PSTonemapWithLumaExport.get()):(m_PSTonemap.get());
            renderResults |= renderContext.ExecuteSingleItem( renderItem, renderOutputs, nullptr );
        }
        else
        {
            // Just copy the floating point source radiance into output color
            renderItem.ShaderResourceViews[POSTPROCESS_TONEMAP_TEXTURE_SLOT0]   = preTonemapRadiance;
            renderItem.PixelShader  = m_PSPassThrough.get();
            renderResults |= renderContext.ExecuteSingleItem( renderItem, renderOutputs, nullptr );
        }
    }
    return renderResults;
}