///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaTextureHelpers.h"

#include "Rendering/vaRenderDeviceContext.h"
#include "Rendering/Effects/vaPostProcess.h"
#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"
#include "Rendering/vaAssetPack.h"

#include "IntegratedExternals/vaImguiIntegration.h"
#include "IntegratedExternals/imgui/imgui_internal.h"

using namespace Vanilla;

void vaTexturePool::FillDesc( ItemDesc & desc, const shared_ptr< vaTexture > texture )
{
    desc.Flags              = texture->GetFlags();
    desc.AccessFlags        = texture->GetAccessFlags();
    desc.Type               = texture->GetType();
    desc.BindSupportFlags   = texture->GetBindSupportFlags();
    desc.ResourceFormat     = texture->GetResourceFormat();
    desc.SRVFormat          = texture->GetSRVFormat();
    desc.RTVFormat          = texture->GetRTVFormat();
    desc.DSVFormat          = texture->GetDSVFormat();
    desc.UAVFormat          = texture->GetUAVFormat();
    desc.SizeX              = texture->GetSizeX();
    desc.SizeY              = texture->GetSizeY();
    desc.SizeZ              = texture->GetSizeZ();
    desc.SampleCount        = texture->GetSampleCount();
    desc.MIPLevels          = texture->GetMipLevels();
}

shared_ptr< vaTexture > vaTexturePool::FindOrCreate2D( vaRenderDevice & device, vaResourceFormat format, int width, int height, int mipLevels, int arraySize, int sampleCount, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags, void * initialData, int initialDataRowPitch, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags )
{
    std::unique_lock lock( m_mutex );

    ItemDesc desc;
    desc.Type = vaTextureType::Texture2D;

    if( (srvFormat == vaResourceFormat::Automatic) && ((bindFlags & vaResourceBindSupportFlags::ShaderResource) != 0) )
        srvFormat = format;
    if( (rtvFormat == vaResourceFormat::Automatic) && ((bindFlags & vaResourceBindSupportFlags::RenderTarget) != 0) )
        rtvFormat = format;
    if( (dsvFormat == vaResourceFormat::Automatic) && ((bindFlags & vaResourceBindSupportFlags::DepthStencil) != 0) )
        dsvFormat = format;
    if( (uavFormat == vaResourceFormat::Automatic) && ((bindFlags & vaResourceBindSupportFlags::UnorderedAccess) != 0) )
        uavFormat = format;

    desc.Device             = &device;
    desc.Flags              = flags;
    desc.AccessFlags        = accessFlags;
    desc.BindSupportFlags   = bindFlags;
    desc.ResourceFormat     = format;
    desc.SRVFormat          = srvFormat;
    desc.RTVFormat          = rtvFormat;
    desc.DSVFormat          = dsvFormat;
    desc.UAVFormat          = uavFormat;
    desc.SizeX              = width;
    desc.SizeY              = height;
    desc.SizeZ              = arraySize;
    desc.SampleCount        = sampleCount;
    desc.MIPLevels          = mipLevels;

    shared_ptr< vaTexture > retTexture;

    auto it = m_items.find( desc );
    if( it != m_items.end( ) )
    {
        retTexture = it->second;
        m_items.erase( it );
    }
    else
    {
        retTexture = vaTexture::Create2D( device, format, width, height, mipLevels, arraySize, sampleCount, bindFlags, accessFlags, srvFormat, rtvFormat, dsvFormat, uavFormat, flags, vaTextureContentsType::GenericColor, initialData, initialDataRowPitch );

#ifdef _DEBUG
        ItemDesc testDesc;
        FillDesc( testDesc, retTexture );
        if( memcmp( &testDesc, &desc, sizeof(desc) ) )
        {
            // there's a mismatch in desc creation above and actual texture desc : you need to find and correct it; if it's platform/API dependent then add a platform dependent implementation
            assert( false );
        }
#endif
    }

    return retTexture;
}

void vaTexturePool::Release( const shared_ptr< vaTexture > texture )
{
    std::unique_lock lock( m_mutex );

    assert( texture != nullptr );
    if( texture == nullptr )
        return;

    ItemDesc desc;
    FillDesc( desc, texture );

    if( m_items.size( ) > m_maxPooledTextureCount )
    {
        //assert( false );
        // need to track memory use and start dropping 'oldest' (or just random one?) when over the limit
//        return;
        m_items.erase( m_items.begin() );
    }

    m_items.insert( std::make_pair( desc, texture ) );
}

void vaTexturePool::ClearAll( )
{ 
    m_items.clear(); 
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


vaTextureTools::vaTextureTools( vaRenderDevice & device ) //: vaRenderingModule( vaRenderingModuleParams(device) )
    :
    m_UIDrawShaderConstants( device ),
    m_UIDrawTexture2DPS( device ),
    m_UIDrawTexture2DArrayPS( device ),
    m_UIDrawTextureCubePS( device )
{
    {
        uint32 initialData = 0x00000000;
        m_textures[(int)CommonTextureName::Black1x1] = vaTexture::Create2D( device, vaResourceFormat::R8G8B8A8_UNORM, 1, 1, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericColor, &initialData, sizeof( initialData ) );
    }
    {
        uint32 initialData = 0xFFFFFFFF;
        m_textures[(int)CommonTextureName::White1x1] = vaTexture::Create2D( device, vaResourceFormat::R8G8B8A8_UNORM, 1, 1, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericColor, &initialData, sizeof( initialData ) );
    }
    {
        uint32 initialData[16*16];
        for( int y = 0; y < 16; y++ )
            for( int x = 0; x < 16; x++ )
            {
                initialData[ y * 16 + x ] = (((x+y)%2) == 0)?(0xFFFFFFFF):(0x00000000);
            }
        m_textures[(int)CommonTextureName::Checkerboard16x16] = vaTexture::Create2D( device, vaResourceFormat::R8G8B8A8_UNORM, 16, 16, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericColor, initialData, 16*sizeof( uint32 ) );
    }
    {
        m_textures[(int)CommonTextureName::Black1x1Cube] = vaTexture::Create2D( device, vaResourceFormat::R8G8B8A8_UNORM, 1, 1, 1, 6, 1, vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::Cubemap, vaTextureContentsType::GenericColor );

        uint32 initialData = 0x00000000;
        vaTextureSubresourceData faceSubRes = { &initialData, 4, 4 };
        assert( device.GetMainContext() != nullptr );
        m_textures[(int)CommonTextureName::Black1x1Cube]->UpdateSubresources( *device.GetMainContext(), 0, std::vector<vaTextureSubresourceData>{faceSubRes, faceSubRes, faceSubRes, faceSubRes, faceSubRes, faceSubRes } );
    }


    m_UIDrawTexture2DPS->CreateShaderFromFile(      "vaHelperTools.hlsl", "UIDrawTexture2DPS", vaShaderMacroContaner{}, false );
    m_UIDrawTexture2DArrayPS->CreateShaderFromFile( "vaHelperTools.hlsl", "UIDrawTexture2DArrayPS", vaShaderMacroContaner{}, false );
    m_UIDrawTextureCubePS->CreateShaderFromFile(    "vaHelperTools.hlsl", "UIDrawTextureCubePS", vaShaderMacroContaner{}, false );

    m_textures[(int)CommonTextureName::BlueNoise64x64x1_3spp] = vaTexture::CreateFromImageFile( device, "bluenoise_8bpc_RGB1_0.dds", vaTextureLoadFlags::PresumeDataIsLinear, vaResourceBindSupportFlags::ShaderResource, vaTextureContentsType::GenericLinear );

    device.e_AfterBeginFrame.AddWithToken( m_aliveToken, this, &vaTextureTools::OnBeginFrame );
}

void vaTextureTools::OnBeginFrame( vaRenderDevice & device, float deltaTime )
{
    vaRenderDeviceContext & renderContext = *device.GetMainContext(); renderContext; deltaTime;

    /*
    // how to import a 3D texture from 2D slices - I'll leave this code in for now...
    static bool kmh = false;
    if( !kmh )
    {
        kmh = true;

        m_textures[(int)CommonTextureName::BlueNoise64x64x64_2spp] = vaTexture::Create3D( device, vaResourceFormat::R8G8_UNORM, 64, 64, 64, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess, vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericLinear );

        for( int z = 0; z < 64; z++ )
        {
            string path = vaStringTools::Format( "C:/Work/INTC_SHARE/BlueNoise/3DAnd4DBlueNoiseTextures/Data/64_64_64/LDR_RG01_%d.png", z );
            shared_ptr<vaTexture> slice = vaTexture::CreateFromImageFile( device, path, vaTextureLoadFlags::PresumeDataIsLinear, vaResourceBindSupportFlags::ShaderResource, vaTextureContentsType::GenericLinear );

            device.GetPostProcess().CopySliceToTexture3D( renderContext, m_textures[(int)CommonTextureName::BlueNoise64x64x64_2spp], z, slice );
        }

        //assert( false ); // below not supported :(
        //m_textures[(int)CommonTextureName::BlueNoise64x64x64_2spp]->SaveToDDSFile( renderContext, L"C:/Work/INTC_SHARE/vanilla_GTAO/3dtex.dds" );
    }
    */
    
}

shared_ptr< vaTexture > vaTextureTools::GetCommonTexture( CommonTextureName textureName )
{
    int index = (int)textureName;
    if( index < 0 || index >= _countof(m_textures) )
        return nullptr;
    return m_textures[index];
}

void vaTextureTools::UIDrawImages( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs )
{
    if( m_UIDrawItems.size( ) == 0 )
        return;
    
    VA_TRACE_CPUGPU_SCOPE( UITextures, renderContext );

    vaGraphicsItem renderItem;

    renderContext.BeginGraphicsItems( renderOutputs, nullptr );

    renderContext.GetRenderDevice().FillFullscreenPassGraphicsItem(renderItem);

    renderItem.ConstantBuffers[TEXTURE_UI_DRAW_TOOL_BUFFERSLOT] = m_UIDrawShaderConstants;

    // m_UIDrawShaderConstants.SetToAPISlot( renderContext, TEXTURE_UI_DRAW_TOOL_BUFFERSLOT );

    for( int i = (int)m_UIDrawItems.size( )-1; i >= 0; i-- )
    {
        UITextureState & item = m_UIDrawItems[i];

        shared_ptr<vaTexture> texture = item.Texture.lock();
        if( texture == nullptr )
            continue;

        UITextureDrawShaderConstants consts; memset( &consts, 0, sizeof( consts ) );
        consts.ClipRect             = item.ClipRectangle;
        consts.DestinationRect      = item.Rectangle;
        consts.Alpha                = item.Alpha;
        consts.TextureArrayIndex    = item.ArrayIndex;
        consts.TextureMIPIndex      = item.MIPIndex;
        consts.ShowAlpha            = (item.ShowAlpha)?(1):(0);
        consts.ContentsType         = (int)texture->GetContentsType();

        m_UIDrawShaderConstants.Upload( renderContext, consts );

        // texture->SetToAPISlotSRV( renderContext, TEXTURE_UI_DRAW_TOOL_TEXTURE_SLOT0 );
        renderItem.ShaderResourceViews[TEXTURE_UI_DRAW_TOOL_TEXTURE_SLOT0] = texture;
        renderItem.BlendMode = vaBlendMode::AlphaBlend;

        if( texture->GetType() == vaTextureType::Texture2D && texture->GetArrayCount() == 1 && texture->GetSampleCount() == 1 )
            //renderContext.FullscreenPassDraw( *m_UIDrawTexture2DPS, vaBlendMode::AlphaBlend );
            renderItem.PixelShader = m_UIDrawTexture2DPS;
        else if( texture->GetArrayCount() > 1 )
        {
            if( ((texture->GetFlags() & vaTextureFlags::Cubemap) != 0) && ((texture->GetFlags() & vaTextureFlags::CubemapButArraySRV) == 0) )
                //renderContext.FullscreenPassDraw( *m_UIDrawTextureCubePS, vaBlendMode::AlphaBlend );
                renderItem.PixelShader = m_UIDrawTextureCubePS;
            else
                //renderContext.FullscreenPassDraw( *m_UIDrawTexture2DArrayPS, vaBlendMode::AlphaBlend );
                renderItem.PixelShader = m_UIDrawTexture2DArrayPS;
        }
        else
        {
            // not yet implemented
            assert( false );
        }

        renderContext.ExecuteItem( renderItem );

        if( item.ScheduledTask )
            item.InUse = true;

        if( item.InUse )
            item.InUse = false;
        else
            m_UIDrawItems.erase( m_UIDrawItems.begin() + i );
    }

    renderContext.EndItems();

    for( int i = (int)m_UIDrawItems.size( ) - 1; i >= 0; i-- )
    {
        UITextureState & item = m_UIDrawItems[i];
        if( item.ScheduledTask )
        {
            item.ScheduledTask( renderContext, item );
            item.ScheduledTask = nullptr;
        }
    }

}

bool vaTextureTools::UITickImGui( const shared_ptr<vaTexture> & texture )
{
    bool hadChanges = false;
    texture;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui::PushID( &(*texture) );

    // look up previous frame draws for persistence
    int stateIndexFound = -1;
    for( int i = 0; i < m_UIDrawItems.size(); i++ )
    {
        if( m_UIDrawItems[i].Texture.lock() == texture )
        {
            stateIndexFound = i;
            assert( !m_UIDrawItems[i].InUse );  // multiple UI elements using the same item - time to upgrade 'bool InUse' to 'uint64 userID' or similar for multi-item support
            break;
        }
    }
    if( stateIndexFound == -1 )
    {
        UITextureState newItem;
        newItem.Texture         = texture;
        m_UIDrawItems.push_back( newItem );
        stateIndexFound = (int)m_UIDrawItems.size()-1;
    }
    UITextureState & uiState = m_UIDrawItems[stateIndexFound];
    uiState.InUse = true;

    if( ImGui::Button( "Texture View", ImVec2( ImGui::GetContentRegionAvail( ).x, 0 ) ) )
    {
        uiState.FullscreenPopUp = !uiState.FullscreenPopUp;
        if( uiState.FullscreenPopUp )
            ImGui::OpenPopup( "Texture View" );
    }

    float columnSize = ImGui::GetContentRegionAvail( ).x * 2.0f / 4.0f;

    ImGui::SetNextWindowSize( ImVec2( ImGui::GetIO( ).DisplaySize.x * 0.8f, ImGui::GetIO( ).DisplaySize.y * 0.8f ), ImGuiCond_Always );
    bool popupOpen = ImGui::BeginPopupModal( "Texture View", &uiState.FullscreenPopUp );

    ImGui::Text( "Dimensions:" );
    ImGui::SameLine( columnSize );
    ImGui::Text( "%d x %d x %d, %d MIPs", texture->GetSizeX( ), texture->GetSizeY( ), texture->GetSizeZ( ), texture->GetMipLevels( ) );

    ImGui::Text( "Format (res/SRV):" );
    ImGui::SameLine( columnSize );
    ImGui::Text( "%s", vaResourceFormatHelpers::EnumToString( texture->GetResourceFormat( ) ).c_str( ), vaResourceFormatHelpers::EnumToString( texture->GetSRVFormat( ) ).c_str( ) );

    string contentsTypeInfo;
    switch( texture->GetContentsType( ) )
    {
    case vaTextureContentsType::GenericColor:           contentsTypeInfo = "GenericColor";              break;
    case vaTextureContentsType::GenericLinear:          contentsTypeInfo = "GenericLinear";             break;
    case vaTextureContentsType::NormalsXYZ_UNORM:       contentsTypeInfo = "NormalsXYZ_UNORM";          break;
    case vaTextureContentsType::NormalsXY_UNORM:        contentsTypeInfo = "NormalsXY_UNORM";           break;
    case vaTextureContentsType::NormalsWY_UNORM:        contentsTypeInfo = "NormalsWY_UNORM";           break;
    case vaTextureContentsType::SingleChannelLinearMask:contentsTypeInfo = "SingleChannelLinearMask";   break;
    case vaTextureContentsType::DepthBuffer:            contentsTypeInfo = "DepthBuffer";               break;
    case vaTextureContentsType::LinearDepth:            contentsTypeInfo = "LinearDepth";               break;
    default:                                            contentsTypeInfo = "Unknown";                   break;
    }
    ImGui::Text( "Contents type:" );
    ImGui::SameLine( columnSize );
    ImGui::Text( contentsTypeInfo.c_str( ) );
    if( texture->IsView( ) )
    {
        ImGui::Text( "View, MIP" );
        ImGui::SameLine( columnSize );
        ImGui::Text( "from %d, count %d", texture->GetViewedMipSlice(), texture->GetMipLevels() );
        ImGui::Text( "View, array" );
        ImGui::SameLine( columnSize );
        ImGui::Text( "from %d, count %d", texture->GetViewedArraySlice(), texture->GetArrayCount() );
    }

    // display texture here
    //if( enqueueImageDraw )
    {
        auto availSize = ImGui::GetContentRegionAvail( );
        availSize.y -= 40; // space for array index and mip controls
        float texAspect = (float)texture->GetSizeX() / (float)texture->GetSizeY();
        if( texAspect > availSize.x / availSize.y )
        {
            uiState.Rectangle.z = availSize.x;
            uiState.Rectangle.w = availSize.x / texAspect;
        }
        else
        {
            uiState.Rectangle.z = availSize.y / texAspect;
            uiState.Rectangle.w = availSize.y;
        }


        bool clicked = ImGui::Selectable( "##dummy", false, 0, ImVec2(uiState.Rectangle.z, uiState.Rectangle.w) );
        clicked;

        uiState.ClipRectangle.x = ImGui::GetCurrentWindowRead( )->ClipRect.Min.x;
        uiState.ClipRectangle.y = ImGui::GetCurrentWindowRead( )->ClipRect.Min.y;
        uiState.ClipRectangle.z = ImGui::GetCurrentWindowRead( )->ClipRect.Max.x - ImGui::GetCurrentWindowRead( )->ClipRect.Min.x;
        uiState.ClipRectangle.w = ImGui::GetCurrentWindowRead( )->ClipRect.Max.y - ImGui::GetCurrentWindowRead( )->ClipRect.Min.y;
        
        uiState.Rectangle.x = ImGui::GetItemRectMin().x;
        uiState.Rectangle.y = ImGui::GetItemRectMin().y;
        uiState.Rectangle.z = ImGui::GetItemRectSize( ).x;
        uiState.Rectangle.w = ImGui::GetItemRectSize( ).y;

        ImGui::Checkbox( "Show alpha", &uiState.ShowAlpha );

        if( texture->GetSizeZ( ) > 1 )
            ImGui::InputInt( "Array index", &uiState.ArrayIndex, 1 );
        uiState.ArrayIndex = vaMath::Clamp( uiState.ArrayIndex, 0, texture->GetArrayCount( )-1 );
        
        ImGui::InputInt( "MIP level index", &uiState.MIPIndex, 1 );
        uiState.MIPIndex = vaMath::Clamp( uiState.MIPIndex, 0, texture->GetMipLevels( ) - 1 );

        ImGui::Separator();
        if( ImGui::Button( "Create MIPs", { -1, 0 } ) )
        {
            hadChanges = true;

            assert( uiState.ScheduledTask == nullptr );
            uiState.ScheduledTask = [ ]( vaRenderDeviceContext& renderContext, UITextureState& item )
            {
                shared_ptr<vaTexture> texture = item.Texture.lock( );
                if( texture == nullptr )
                    return;

                auto newTexture = vaTexture::TryCreateMIPs( renderContext, texture );
                if( newTexture != nullptr )
                {
                    VA_LOG( "Texture MIPs generated (%d)", newTexture->GetMipLevels() );
                    // This is done so that all other assets or systems referencing the texture by the ID now point to the new one!
                    if( texture->GetParentAsset( ) != nullptr )
                        texture->GetParentAsset( )->ReplaceAssetResource( newTexture );
                    else
                        vaUIDObjectRegistrar::SwapIDs( texture, newTexture );
                    item.Texture = newTexture;
                }
                else
                {
                    VA_LOG_WARNING( "Unable to generate MIPs for the texture" );
                }
            };
        }
        if( ImGui::Button( "Compress", { -1, 0 } ) )
        {
            hadChanges = true;

            assert( uiState.ScheduledTask == nullptr );
            uiState.ScheduledTask = [ ]( vaRenderDeviceContext& , UITextureState& item )
            {
                shared_ptr<vaTexture> texture = item.Texture.lock( );
                if( texture == nullptr )
                    return;

                auto newTexture = texture->TryCompress();
                if( newTexture != nullptr )
                {
                    VA_LOG( "Texture compressed from %s to %s", vaResourceFormatHelpers::EnumToString( texture->GetResourceFormat() ).c_str(), vaResourceFormatHelpers::EnumToString( newTexture->GetResourceFormat() ).c_str() );

                    // This is done so that all other assets or systems referencing the texture by the ID now point to the new one!
                    if( texture->GetParentAsset( ) != nullptr )
                        texture->GetParentAsset( )->ReplaceAssetResource( newTexture );
                    else
                        vaUIDObjectRegistrar::SwapIDs( texture, newTexture );
                    item.Texture = newTexture;
                }
                else
                {
                    VA_LOG_WARNING( "Unable to compress the texture" );
                }
            };
        }
    }

    if( popupOpen )
    {
        if( !uiState.FullscreenPopUp )
            ImGui::CloseCurrentPopup( );

        ImGui::EndPopup();
    }
    uiState.FullscreenPopUp = ImGui::IsPopupOpen("Texture View");

    ImGui::PopID();
#endif

    return hadChanges;
}
