///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if 0 // obsolete, left as placeholder

#include "vaGBuffer.h"

#include "Rendering/vaRenderDeviceContext.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#define GBUFFER_SLOT0   0

using namespace Vanilla;

vaGBuffer::vaGBuffer( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params ),
    vaUIPanel( "GBuffer", 0, false, vaUIPanel::DockLocation::DockedLeftBottom ),
    //m_pixelShader( params.RenderDevice ),
    m_debugDrawDepthPS( params.RenderDevice ),
    m_debugDrawDepthViewspaceLinearPS( params.RenderDevice ),
    m_debugDrawNormalMapPS( params.RenderDevice ),
    m_debugDrawAlbedoPS( params.RenderDevice ),
    m_debugDrawRadiancePS( params.RenderDevice )
{
    m_debugInfo             = "GBuffer (uninitialized - forgot to call RenderTick?)";
    m_debugSelectedTexture  = -1;

    m_resolution            = vaVector2i( 0, 0 );
    m_sampleCount           = 1;
    m_deferredEnabled       = false;

    m_shadersDirty          = true;
    m_shaderFileToUse       = L"vaGBuffer.hlsl";
}

vaGBuffer::~vaGBuffer( )
{

}

void vaGBuffer::UIPanelTick( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    struct TextureInfo
    {
        string                  Name;
        shared_ptr<vaTexture>   Texture;
    };

    std::vector< TextureInfo > textures;

    textures.push_back( { "Depth Buffer", m_depthBuffer } );
    textures.push_back( { "Depth Buffer Viewspace Linear", m_depthBufferViewspaceLinear } );
    textures.push_back( { "Normal Map", m_normalMap } );
    textures.push_back( { "Albedo", m_albedo } );
    textures.push_back( { "Radiance", m_radiance} );
    textures.push_back( { "OutputColor", m_outputColorView } );

    for( size_t i = 0; i < textures.size(); i++ )
    {
        if( ImGui::Selectable( textures[i].Name.c_str(), m_debugSelectedTexture == i ) )
        {
            if( m_debugSelectedTexture == i )
                m_debugSelectedTexture = -1;
            else
                m_debugSelectedTexture = (int)i;
        }
    }
#endif
}

bool vaGBuffer::ReCreateIfNeeded( shared_ptr<vaTexture> & inoutTex, int width, int height, vaResourceFormat format, bool needsUAV, int msaaSampleCount, float & inoutTotalSizeSum, vaResourceFormat fastClearFormat )
{
//    if( format == vaResourceFormat::Unknown )
//    {
//        inoutTex = nullptr;
//        return true;
//    }

    vaResourceBindSupportFlags bindFlags = vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource;
    if( needsUAV )
        bindFlags |= vaResourceBindSupportFlags::UnorderedAccess;

    if( (width == 0) || (height == 0) || (format == vaResourceFormat::Unknown ) )
    {
        inoutTex = nullptr;
        return false;
    }
    else
    {
        vaResourceFormat resourceFormat  = format;
        vaResourceFormat srvFormat       = format;
        vaResourceFormat rtvFormat       = format;
        vaResourceFormat dsvFormat       = vaResourceFormat::Unknown;
        vaResourceFormat uavFormat       = format;

        bool fastClearDepth = false;

        // handle special cases
        if( format == vaResourceFormat::D32_FLOAT )
        {
            bindFlags = (bindFlags & ~(vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess)) | vaResourceBindSupportFlags::DepthStencil;
            resourceFormat  = vaResourceFormat::R32_TYPELESS;
            srvFormat       = vaResourceFormat::R32_FLOAT;
            rtvFormat       = vaResourceFormat::Unknown;
            dsvFormat       = vaResourceFormat::D32_FLOAT;
            uavFormat       = vaResourceFormat::Unknown;
            fastClearDepth = true;
        }
        else if( format == vaResourceFormat::D24_UNORM_S8_UINT )
        {
            bindFlags = ( bindFlags & ~( vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess ) ) | vaResourceBindSupportFlags::DepthStencil;
            resourceFormat = vaResourceFormat::R24G8_TYPELESS;
            srvFormat = vaResourceFormat::R24_UNORM_X8_TYPELESS;
            rtvFormat = vaResourceFormat::Unknown;
            dsvFormat = vaResourceFormat::D24_UNORM_S8_UINT;
            uavFormat = vaResourceFormat::Unknown;
            fastClearDepth = true;
        }
        else if( format == vaResourceFormat::R8G8B8A8_UNORM_SRGB )
        {
            //resourceFormat  = vaResourceFormat::R8G8B8A8_TYPELESS;
            //srvFormat       = vaResourceFormat::R8G8B8A8_UNORM_SRGB;
            //rtvFormat       = vaResourceFormat::R8G8B8A8_UNORM_SRGB;
            //uavFormat       = vaResourceFormat::R8G8B8A8_UNORM;
            uavFormat  = vaResourceFormat::Unknown;
            bindFlags &= ~vaResourceBindSupportFlags::UnorderedAccess;
        }
        else if ( vaResourceFormatHelpers::IsTypeless(format) )
        {
            srvFormat  = vaResourceFormat::Unknown;
            rtvFormat  = vaResourceFormat::Unknown;
            uavFormat  = vaResourceFormat::Unknown;
            //bindFlags &= ~(vaResourceBindSupportFlags::ShaderResource|vaResourceBindSupportFlags::RenderTarget|vaResourceBindSupportFlags::UnorderedAccess);
        }

        if( !needsUAV ) uavFormat       = vaResourceFormat::Unknown;

        if( (inoutTex != nullptr) && (inoutTex->GetSizeX() == width) && (inoutTex->GetSizeY()==height) &&
            (inoutTex->GetResourceFormat()==resourceFormat) && (inoutTex->GetSRVFormat()==srvFormat) && (inoutTex->GetRTVFormat()==rtvFormat) &&
            (inoutTex->GetDSVFormat()==dsvFormat) && (inoutTex->GetUAVFormat()==uavFormat) && (inoutTex->GetSampleCount()==msaaSampleCount) )
            return false;

        if( fastClearFormat != vaResourceFormat::Unknown )
        {
            if( fastClearDepth )
                vaTexture::SetNextCreateFastClearDSV( fastClearFormat, 0.0f, 0 );
            else
                vaTexture::SetNextCreateFastClearRTV( fastClearFormat, vaVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
        }

        inoutTex = vaTexture::Create2D( GetRenderDevice(), resourceFormat, width, height, 1, 1, msaaSampleCount, bindFlags, vaResourceAccessFlags::Default, srvFormat, rtvFormat, dsvFormat, uavFormat );
    }
    inoutTotalSizeSum += width * height * vaResourceFormatHelpers::GetPixelSizeInBytes( format ) * msaaSampleCount;
    return true;
}

void vaGBuffer::UpdateResources( int width, int height, int msaaSampleCount, bool enableDeferred )
{
    assert( width > 0 );
    assert( height > 0 );

    m_resolution = vaVector2i( width, height );
    m_sampleCount = msaaSampleCount;
    m_deferredEnabled = enableDeferred;

    float totalSizeInMB = 0.0f;

    ReCreateIfNeeded( m_depthBuffer                 , width, height, m_formats.DepthBuffer,                   false, msaaSampleCount,    totalSizeInMB, m_formats.DepthBuffer);
    ReCreateIfNeeded( m_depthBufferViewspaceLinear  , width, height, m_formats.DepthBufferViewspaceLinear,    false, msaaSampleCount,    totalSizeInMB, m_formats.DepthBufferViewspaceLinear );
    ReCreateIfNeeded( m_radiance                    , width, height, m_formats.Radiance,                      m_formats.RadianceEnableUAV, msaaSampleCount,    totalSizeInMB, m_formats.Radiance );
    if( ReCreateIfNeeded( m_outputColorTypeless     , width, height, m_formats.OutputColorTypeless,           true, 1,                  totalSizeInMB, m_formats.OutputColorView ) )                  // output is not MSAA, but has UAV
    {
        m_outputColorIgnoreSRGBConvView = vaTexture::CreateView( m_outputColorTypeless, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess | vaResourceBindSupportFlags::ShaderResource, 
            m_formats.OutputColorIgnoreSRGBConvView, m_formats.OutputColorIgnoreSRGBConvView, vaResourceFormat::Unknown, m_formats.OutputColorIgnoreSRGBConvView );
        m_outputColorView = vaTexture::CreateView( m_outputColorTypeless, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource, 
            m_formats.OutputColorView, m_formats.OutputColorView, vaResourceFormat::Unknown, vaResourceFormat::Unknown );
        if( m_formats.OutputColorR32UINT_UAV == vaResourceFormat::R32_UINT )
            m_outputColorR32UINT_UAV = vaTexture::CreateView( m_outputColorTypeless, vaResourceBindSupportFlags::UnorderedAccess, vaResourceFormat::Unknown, vaResourceFormat::Unknown, vaResourceFormat::Unknown, m_formats.OutputColorR32UINT_UAV );
        else
            m_outputColorR32UINT_UAV = nullptr;
    }

    if( m_deferredEnabled )
    {
        ReCreateIfNeeded( m_normalMap                   , width, height, m_formats.NormalMap,                     false, msaaSampleCount,    totalSizeInMB, m_formats.NormalMap );
        ReCreateIfNeeded( m_albedo                      , width, height, m_formats.Albedo,                        false, msaaSampleCount,    totalSizeInMB, m_formats.Albedo );
    }
    else
    {
        m_normalMap = nullptr;
        m_albedo    = nullptr;
    }

    totalSizeInMB /= 1024 * 1024;

    m_debugInfo = vaStringTools::Format( "GBuffer (approx. %.2fMB) ", totalSizeInMB );
}

void vaGBuffer::UpdateShaders( )
{
    if( m_shadersDirty )
    {
        m_shadersDirty = false;
    
        m_debugDrawDepthPS->CompileFromFile(                m_shaderFileToUse, "ps_5_0", "DebugDrawDepthPS",                 m_staticShaderMacros, false );
        m_debugDrawDepthViewspaceLinearPS->CompileFromFile( m_shaderFileToUse, "ps_5_0", "DebugDrawDepthViewspaceLinearPS",  m_staticShaderMacros, false );
        m_debugDrawNormalMapPS->CompileFromFile(            m_shaderFileToUse, "ps_5_0", "DebugDrawNormalMapPS",             m_staticShaderMacros, false );
        m_debugDrawAlbedoPS->CompileFromFile(               m_shaderFileToUse, "ps_5_0", "DebugDrawAlbedoPS",                m_staticShaderMacros, false );
        m_debugDrawRadiancePS->CompileFromFile(             m_shaderFileToUse, "ps_5_0", "DebugDrawRadiancePS",              m_staticShaderMacros, false );
    }
}

void vaGBuffer::RenderDebugDraw( vaDrawAttributes & drawAttributes )
{
    assert( !m_shadersDirty );                              if( m_shadersDirty ) return;

    drawAttributes;

    assert( false ); // TODO: port to platform independent code

#if 0
    if( m_debugSelectedTexture == -1 )
        return;

    vaRenderDeviceContextDX11 * apiContext = drawContext.APIContext.SafeCast<vaRenderDeviceContextDX11*>( );
    ID3D11DeviceContext * dx11Context = apiContext->GetDXContext( );

    vaDirectXTools11::AssertSetToD3DContextAllShaderTypes( dx11Context, (ID3D11ShaderResourceView*)nullptr, GBUFFER_SLOT0 );

    if( m_debugSelectedTexture == 0 )
    {
        vaDirectXTools11::SetToD3DContextAllShaderTypes( dx11Context, m_depthBuffer->SafeCast<vaTextureDX11*>()->GetSRV(), GBUFFER_SLOT0 );
        apiContext->FullscreenPassDraw( *m_debugDrawDepthPS );
    }
    else if( m_debugSelectedTexture == 1 )
    {
        vaDirectXTools11::SetToD3DContextAllShaderTypes( dx11Context, m_depthBufferViewspaceLinear->SafeCast<vaTextureDX11*>( )->GetSRV( ), GBUFFER_SLOT0 );
        apiContext->FullscreenPassDraw( *m_debugDrawDepthViewspaceLinearPS );
    }
    else if( m_debugSelectedTexture == 2 )
    {
        vaDirectXTools11::SetToD3DContextAllShaderTypes( dx11Context, m_normalMap->SafeCast<vaTextureDX11*>( )->GetSRV( ), GBUFFER_SLOT0 );
        apiContext->FullscreenPassDraw( *m_debugDrawNormalMapPS );
    }
    else if( m_debugSelectedTexture == 3 )
    {
        vaDirectXTools11::SetToD3DContextAllShaderTypes( dx11Context, m_albedo->SafeCast<vaTextureDX11*>( )->GetSRV( ), GBUFFER_SLOT0 );
        apiContext->FullscreenPassDraw( *m_debugDrawAlbedoPS );
    }
    else if( m_debugSelectedTexture == 4 )
    {
        vaDirectXTools11::SetToD3DContextAllShaderTypes( dx11Context, m_radiance->SafeCast<vaTextureDX11*>( )->GetSRV( ), GBUFFER_SLOT0 );
        apiContext->FullscreenPassDraw( *m_debugDrawRadiancePS );
    }

    // Reset
    vaDirectXTools11::SetToD3DContextAllShaderTypes( dx11Context, (ID3D11ShaderResourceView*)nullptr, GBUFFER_SLOT0 );
#endif
}

/*
// draws provided depthTexture (can be the one obtained using GetDepthBuffer( )) into currently selected RT; relies on settings set in vaRenderGlobals and will assert and return without doing anything if those are not present
void vaGBuffer::DepthToViewspaceLinear( vaDrawAttributes & drawAttributes, vaTexture & depthTexture )
{
    assert( !m_shadersDirty );                              if( m_shadersDirty ) return;

    drawContext; depthTexture;

    assert( false ); // TODO: port to platform independent code

#if 0

    vaRenderDeviceContextDX11 * apiContext = drawContext.APIContext.SafeCast<vaRenderDeviceContextDX11*>( );
    ID3D11DeviceContext * dx11Context = apiContext->GetDXContext( );

    vaDirectXTools11::AssertSetToD3DContextAllShaderTypes( dx11Context, ( ID3D11ShaderResourceView* )nullptr, GBUFFER_SLOT0 );

    vaDirectXTools11::SetToD3DContextAllShaderTypes( dx11Context, depthTexture.SafeCast<vaTextureDX11*>( )->GetSRV( ), GBUFFER_SLOT0 );
    apiContext->FullscreenPassDraw( *m_depthToViewspaceLinearPS );

    // Reset
    vaDirectXTools11::SetToD3DContextAllShaderTypes( dx11Context, ( ID3D11ShaderResourceView* )nullptr, GBUFFER_SLOT0 );

        //vaGraphicsItem renderItem;
        //apiContext.FillFullscreenPassGraphicsItem( renderItem );
        //renderItem.ConstantBuffers[ IMAGE_COMPARE_TOOL_BUFFERSLOT ]         = m_constants.GetBuffer();
        //renderItem.ShaderResourceViews[ GBUFFER_SLOT0 ]                   = m_referenceTexture;
        //renderItem.ShaderResourceViews[ IMAGE_COMPARE_TOOL_TEXTURE_SLOT1 ]  = m_helperTexture;
        //renderItem.PixelShader          = m_visualizationPS;
        //apiContext.ExecuteSingleItem( renderItem );
#endif
}
*/

#endif // #if 0 // obsolete, left as placeholder