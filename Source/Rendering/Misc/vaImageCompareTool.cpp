///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaImageCompareTool.h"

#include "Core/vaInput.h"

#include "Core/System/vaFileTools.h"

#include "IntegratedExternals/vaImguiIntegration.h"

using namespace Vanilla;


vaImageCompareTool::vaImageCompareTool( const vaRenderingModuleParams & params ) : vaRenderingModule( params ),
    m_visualizationPS( params ),
    m_constants( params ),
    vaUIPanel( "CompareTool", -2, true, vaUIPanel::DockLocation::DockedLeftBottom )
{
    m_screenshotCapturePath = L"";
    m_saveReferenceScheduled = false;
    m_compareReferenceScheduled = false;
    m_referenceDDSTextureStoragePath = vaCore::GetExecutableDirectory() + L"reference.dds";
    m_referencePNGTextureStoragePath = vaCore::GetExecutableDirectory() + L"reference.png";
    m_initialized = false;

    m_visualizationPS->CreateShaderFromFile( "vaHelperTools.hlsl", "ImageCompareToolVisualizationPS", vaShaderMacroContaner{}, false );
}

vaImageCompareTool::~vaImageCompareTool( )
{

}

void vaImageCompareTool::SaveAsReference( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & colorInOut )
{
    // There is/was an issue in DX12 on some GPUs on Windows RS4 at some image sizes (for ex 2560x1600) when bind flags are different for otherwise identical texture the CopyResource will fail with
    // "D3D12 ERROR: ID3D12CommandList::CopyResource: Source and Destination resource must have the same size. But pSrcResource has resource size (16449536) and pDstResource has resource size (16384000)."
    // Might be an error in the debug layer.
    // Therefore always re-create the reference texture with identical bind flags when saving.

    const shared_ptr< vaTexture > & viewedOriginal = (!colorInOut->IsView())?(colorInOut):(colorInOut->GetViewedOriginal( ) );

    if( ( m_referenceTexture == nullptr ) || ( m_referenceTexture->GetSizeX( ) != colorInOut->GetSizeX( ) ) || ( m_referenceTexture->GetSizeY( ) != colorInOut->GetSizeY( ) ) || ( m_referenceTexture->GetResourceFormat( ) != colorInOut->GetSRVFormat( ) ) 
        || m_referenceTexture->GetBindSupportFlags() != viewedOriginal->GetBindSupportFlags() )
    {
        assert( colorInOut->GetType() == vaTextureType::Texture2D );
        assert( colorInOut->GetMipLevels() == 1 );
        assert( colorInOut->GetSizeZ() == 1 );
        assert( colorInOut->GetSampleCount() == 1 );

        auto bindFlags = viewedOriginal->GetBindSupportFlags();

        m_referenceTexture = vaTexture::Create2D( GetRenderDevice(), viewedOriginal->GetResourceFormat(), colorInOut->GetSizeX(), colorInOut->GetSizeY(), 1, 1, 1, bindFlags, 
            vaResourceAccessFlags::Default, colorInOut->GetSRVFormat(), viewedOriginal->GetRTVFormat(), viewedOriginal->GetDSVFormat(), viewedOriginal->GetUAVFormat() );
    }

    m_referenceTexture->CopyFrom( renderContext, colorInOut );
}

vaVector4 vaImageCompareTool::CompareWithReference( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & colorInOut )
{
    vaPostProcess & postProcess = GetRenderDevice( ).GetPostProcess( );
    // Bail if reference image not captured, or size/format mismatch - please capture a reference image first.
    if( ( m_referenceTexture == nullptr ) || ( m_referenceTexture->GetSizeX( ) != colorInOut->GetSizeX( ) ) || ( m_referenceTexture->GetSizeY( ) != colorInOut->GetSizeY( ) ) ) //|| ( m_referenceTexture->GetSRVFormat( ) != colorInOut->GetSRVFormat( ) ) )
        return vaVector4( -1, -1, -1, -1 );

    return postProcess.CompareImages( renderContext, m_referenceTexture, colorInOut );
}

void vaImageCompareTool::RenderTick( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & colorInOut )
{
    if( !m_initialized && m_referenceTexture == nullptr )
    {
        if( vaFileTools::FileExists( m_referenceDDSTextureStoragePath ) )
        {
            m_referenceTexture = vaTexture::CreateFromImageFile( GetRenderDevice(), m_referenceDDSTextureStoragePath );
            if( m_referenceTexture != nullptr )
                VA_LOG( L"CompareTool: Reference image loaded from " + m_referenceDDSTextureStoragePath );
        }
        else if( vaFileTools::FileExists( m_referencePNGTextureStoragePath ) )
        {
            m_referenceTexture = vaTexture::CreateFromImageFile( GetRenderDevice(), m_referencePNGTextureStoragePath );
            if( m_referenceTexture != nullptr )
                VA_LOG( L"CompareTool: Reference image loaded from " + m_referencePNGTextureStoragePath );
        }
        m_initialized = true;
    }

    if( m_helperTexture == nullptr || ( m_helperTexture->GetSizeX( ) != colorInOut->GetSizeX( ) ) || ( m_helperTexture->GetSizeY( ) != colorInOut->GetSizeY( ) ) || ( m_helperTexture->GetSRVFormat( ) != colorInOut->GetSRVFormat( ) ) )
    {
        m_helperTexture = vaTexture::Create2D( GetRenderDevice(), colorInOut->GetSRVFormat( ), colorInOut->GetSizeX( ), colorInOut->GetSizeY( ), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource,
            vaResourceAccessFlags::Default, colorInOut->GetSRVFormat( ) );
    }

    if( m_screenshotCapturePath != L"" )
    {
        shared_ptr<vaTexture> textureToSave = colorInOut;
        if( colorInOut->GetSRVFormat( ) == vaResourceFormat::R11G11B10_FLOAT || colorInOut->GetResourceFormat( ) == vaResourceFormat::R16G16B16A16_FLOAT )
        {
            VA_WARN( "CompareTool: Source image not in format supported for saving to .png, attempting to convert to closest compatible format. This will cause in slightly different image being saved." );
            textureToSave = vaTexture::Create2D( GetRenderDevice( ), vaResourceFormat::R8G8B8A8_UNORM_SRGB, colorInOut->GetSizeX( ), colorInOut->GetSizeY( ), colorInOut->GetSizeZ( ), 1, 1, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource );

            auto copyRect = vaVector4( 0, 0, (float)colorInOut->GetSizeX( ), (float)colorInOut->GetSizeY( ) );
            renderContext.StretchRect( textureToSave, colorInOut, copyRect, copyRect, false );
        }

        if( textureToSave->SaveToPNGFile( renderContext, m_screenshotCapturePath ) )
        {
            VA_LOG( L"CompareTool: Screenshot saved to " + m_screenshotCapturePath );
        }
        else
        {
            VA_LOG_ERROR( L"CompareTool: Error saving screenshot to " + m_screenshotCapturePath );
        }
        m_screenshotCapturePath = L"";
    }

    if( m_saveReferenceScheduled )
    {
        SaveAsReference( renderContext, colorInOut );
        m_saveReferenceScheduled = false;

        if( m_referenceTexture != nullptr )
        {
            VA_LOG( "CompareTool: Reference image captured" );

            shared_ptr<vaTexture> textureToSave = m_referenceTexture;

            // first save to raw .dds - this guarantees image is identical when loaded as a reference, but since it's not good for reading with other image tools, also save as .png
            if( textureToSave->SaveToDDSFile( renderContext, m_referenceDDSTextureStoragePath ) )
                VA_LOG( L"CompareTool: Reference DDS image saved to " + m_referenceDDSTextureStoragePath );
            else
                VA_LOG_ERROR( L"CompareTool: Error saving reference DDS to " + m_referenceDDSTextureStoragePath );

            // using PNG is not good as a reference due to potential conversion but it's easy to read from any other tool, so save as it like that also 
            if( m_referenceTexture->GetSRVFormat( ) == vaResourceFormat::R11G11B10_FLOAT || m_referenceTexture->GetResourceFormat( ) == vaResourceFormat::R16G16B16A16_FLOAT )
            {
                //VA_WARN( "CompareTool: Reference image not in format supported for saving to .png, attempting to convert to closest compatible format. This will cause comparison differences when loaded next time." );
                textureToSave = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R8G8B8A8_UNORM_SRGB, m_referenceTexture->GetSizeX(), m_referenceTexture->GetSizeY(), m_referenceTexture->GetSizeZ(), 1, 1, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource );

                // can't use CopyResource because it doesn't support all format conversions
                // textureToSave->CopyFrom( renderContext, m_referenceTexture );

                auto copyRect = vaVector4( 0, 0, (float)m_referenceTexture->GetSizeX( ), (float)m_referenceTexture->GetSizeY( ) );
                renderContext.StretchRect( textureToSave, m_referenceTexture, copyRect, copyRect, false );
            }
            if( textureToSave->SaveToPNGFile( renderContext, m_referencePNGTextureStoragePath ) )
                VA_LOG( L"CompareTool: Reference PNG image saved to " + m_referencePNGTextureStoragePath );
            else
                VA_LOG_ERROR( L"CompareTool: Error saving reference PNG to " + m_referencePNGTextureStoragePath );
        }
        else
        {
            VA_LOG_ERROR( "CompareTool: Error capturing reference image." );
        }
    }

    if( m_compareReferenceScheduled )
    {
        vaVector4 diff = CompareWithReference( renderContext, colorInOut );
        if( diff.x == -1 )
        {
            VA_LOG_ERROR( "CompareTool: Reference image not captured, or size/format mismatch - please capture a reference image first." );
        }
        else
        {
            VA_LOG_SUCCESS( "CompareTool: Comparing current screen image with saved reference: PSNR: %.3f (MSE: %f)", diff.y, diff.x );
        }
        m_compareReferenceScheduled = false;
    }

    if( m_visualizationType != vaImageCompareTool::VisType::None && m_referenceTexture != nullptr && m_helperTexture != nullptr )
    {
        if( !( ( m_referenceTexture == nullptr ) || ( m_referenceTexture->GetSizeX( ) != colorInOut->GetSizeX( ) ) || ( m_referenceTexture->GetSizeY( ) != colorInOut->GetSizeY( ) ) ) )// || ( m_referenceTexture->GetSRVFormat( ) != colorInOut->GetSRVFormat( ) ) ) )
        {
            m_helperTexture->CopyFrom( renderContext, colorInOut );

            ImageCompareToolShaderConstants consts; memset( &consts, 0, sizeof(consts) );
            consts.VisType = (int)m_visualizationType;
            m_constants.Upload( renderContext, consts );

            vaGraphicsItem renderItem;
            m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
            renderItem.ConstantBuffers[ IMAGE_COMPARE_TOOL_BUFFERSLOT ]         = m_constants;
            renderItem.ShaderResourceViews[ IMAGE_COMPARE_TOOL_TEXTURE_SLOT0 ]  = m_referenceTexture;
            renderItem.ShaderResourceViews[ IMAGE_COMPARE_TOOL_TEXTURE_SLOT1 ]  = m_helperTexture;
            renderItem.PixelShader          = m_visualizationPS;
            renderContext.ExecuteSingleItem( renderItem, vaRenderOutputs::FromRTDepth(colorInOut), nullptr );
        }
    }
}

void vaImageCompareTool::UIPanelTickAlways( vaApplicationBase & )
{
    if( m_referenceTexture != nullptr )
    {
        if( ( vaInputKeyboardBase::GetCurrent( ) != NULL ) && vaInputKeyboardBase::GetCurrent( )->IsKeyDown( KK_CONTROL ) )
        {
            if( vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( vaKeyboardKeys::KK_OEM_MINUS ) )
                m_visualizationType = vaImageCompareTool::VisType::None;
            else if( vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( vaKeyboardKeys::KK_OEM_PLUS ) )
                m_visualizationType = vaImageCompareTool::VisType::ShowReference;
            if( vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( vaKeyboardKeys::KK_OEM_4 ) )
                m_visualizationType = vaImageCompareTool::VisType::ShowDifference;
            else if( vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( vaKeyboardKeys::KK_OEM_6 ) )
                m_visualizationType = vaImageCompareTool::VisType::ShowDifferenceX10;
        }
    }
}

void vaImageCompareTool::UIPanelTick( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui::PushItemWidth( 120.0f );

    ImGui::BeginGroup( );
    m_saveReferenceScheduled = ImGui::Button( "Save ref" );
    ImGui::SameLine( );
    m_compareReferenceScheduled = ImGui::Button( "Compare with ref" );

    if( m_referenceTexture == nullptr )
    {
        ImGui::Text( "No reference captured/loaded!" );
    }
    else
    {
        std::vector<string> elements; 
        elements.push_back( "None" ); 
        elements.push_back( "ShowReference" ); 
        elements.push_back( "ShowDifference" ); 
        elements.push_back( "ShowDifferenceX10"); 
        elements.push_back( "ShowDifferenceX100" );

        ImGuiEx_Combo( "Visualization", (int&)m_visualizationType, elements );
    }

    if( ImGui::Button( "Save to screenshot_xxx.png" ) )
    {
        m_screenshotCapturePath = vaCore::GetExecutableDirectory() + vaStringTools::Format( L"screenshot%03d.png", m_screenshotCaptureCounter );
        m_screenshotCaptureCounter++;
    }

    ImGui::EndGroup( );

    ImGui::PopItemWidth( );
#endif
}
