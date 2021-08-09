///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaTextureReductionTestTool.h"

#include "Rendering/vaTextureHelpers.h"

#include "Rendering/vaAssetPack.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"

using namespace Vanilla;

// not maintained, needs update

#ifdef VA_ENABLE_TEXTURE_REDUCTION_TOOL

bool vaTextureReductionTestTool::s_supportedByApp                = false;

static shared_ptr<vaMemoryStream> SaveCamera( const vaRenderCamera & camera )
{
    shared_ptr<vaMemoryStream> retVal = std::make_shared<vaMemoryStream>( (int64)0 );
    camera.Save( *retVal );
    retVal->WriteValue( camera.Settings() );
    return retVal;
}

static void LoadCamera( vaRenderCamera & camera, vaMemoryStream & memStream )
{
    memStream.Seek(0);
    camera.Load( memStream );
    memStream.ReadValue( camera.Settings( ) );
}

vaTextureReductionTestTool::vaTextureReductionTestTool( const vector<TestItemType> & textures, vector<shared_ptr<vaAssetTexture>> textureAssets )
{ 
//    assert( vaRenderingCore::IsInitialized() );
    m_textures = textures;
    m_textureAssets = textureAssets;
    assert( m_textureAssets.size() == m_textures.size() );
    
    m_popupJustOpened   = true;
    //m_logDataNextFrame  = false;

    ResetData( );

    vaFileStream fileIn;
    if( fileIn.Open( vaCore::GetExecutableDirectory( ) + L"TextureReductionTestTool.somekindofstate", FileCreationMode::Open ) )
    {
        int32 count = 0;
        fileIn.ReadValue<int32>( count );
        count = vaMath::Min( count, c_cameraSlotCount );
        for( int i = 0; i < count; i++ )
        {
            int32 buffSize = 0;
            fileIn.ReadValue<int32>( buffSize );
            if( buffSize == 0 )
            {
                m_cameraSlots[i] = nullptr;
            }
            else
            {
                byte * buff = new byte[ buffSize ];
                if( fileIn.Read( buff, buffSize ) )
                {
                    m_cameraSlots[i] = std::make_shared< vaMemoryStream >( 0, buffSize );
                    m_cameraSlots[i]->Write( buff, buffSize );
                }
                else
                {
                    assert( false );
                }
                delete[] buff;
            }
        }
        for( int i = count; i < c_cameraSlotCount; i++ )
            m_cameraSlots[i] = nullptr;
    }

}

vaTextureReductionTestTool::~vaTextureReductionTestTool( )
{
    ResetTextureOverrides();

    vaFileStream fileOut;
    if( fileOut.Open( vaCore::GetExecutableDirectory( ) + L"TextureReductionTestTool.somekindofstate", FileCreationMode::Create ) )
    {
        int32 count = c_cameraSlotCount;
        fileOut.WriteValue<int32>( count );
        for( int i = 0; i < count; i++ )
        {
            if( m_cameraSlots[i] == nullptr )
            {
                fileOut.WriteValue<int32>( 0 );
            }
            else
            { 
                fileOut.WriteValue<int32>( (int32)m_cameraSlots[i]->GetLength() );

                fileOut.Write( m_cameraSlots[i]->GetBuffer(), m_cameraSlots[i]->GetLength( ) );
            }
        }
    }
}

void vaTextureReductionTestTool::SaveAsReference( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & colorBuffer )
{
    if( ( m_referenceTexture == nullptr ) || ( m_referenceTexture->GetSizeX( ) != colorBuffer->GetSizeX( ) ) || ( m_referenceTexture->GetSizeY( ) != colorBuffer->GetSizeY( ) ) || ( m_referenceTexture->GetResourceFormat( ) != colorBuffer->GetResourceFormat( ) ) )
    {
        assert( colorBuffer->GetType( ) == vaTextureType::Texture2D );
        assert( colorBuffer->GetMipLevels( ) == 1 );
        assert( colorBuffer->GetSizeZ( ) == 1 );
        assert( colorBuffer->GetSampleCount( ) == 1 );
        m_referenceTexture = vaTexture::Create2D( renderContext.GetRenderDevice(), colorBuffer->GetResourceFormat( ), colorBuffer->GetSizeX( ), colorBuffer->GetSizeY( ), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource,
            vaResourceAccessFlags::Default, colorBuffer->GetSRVFormat( ) );
    }

    m_referenceTexture->CopyFrom( renderContext, colorBuffer );
}

void vaTextureReductionTestTool::ResetTextureOverrides( )
{
    if( m_currentlyOverriddenTexture != nullptr )
    {
        m_currentlyOverriddenTexture->SetOverrideView( nullptr );
        m_currentlyOverriddenTexture = nullptr;
        m_currentOverrideView = nullptr;
    }

    if( m_overrideAll )
    {
        m_overrideAll = false;
        for( int i = 0; i < (int)m_textures.size( ); i++ )
            m_textures[i].first->SetOverrideView( nullptr );
    }
}

void vaTextureReductionTestTool::TickCPU( const shared_ptr<vaRenderCamera> & camera )
{
    if( !m_enabled )
    {
        m_cameraSlotSelectedIndex = -1;
    }

    // no custom camera slot selected and backup available? reset camera!
    if( m_cameraSlotSelectedIndex == -1 && m_userCameraBackup != nullptr )
    {
        LoadCamera( *camera, *m_userCameraBackup );
        m_userCameraBackup = nullptr;
    }
    if( m_cameraSlotSelectedIndex != -1 && m_cameraSlots[m_cameraSlotSelectedIndex] != nullptr )
    {
        if( m_userCameraBackup == nullptr )
        {
            m_userCameraBackup = SaveCamera( *camera );
        }
        LoadCamera( *camera, *m_cameraSlots[m_cameraSlotSelectedIndex] );
    }

    /*
    if( m_runningTests )
    {
        // we've just started, capture the tonemap state
        if( m_currentTexture == -1 )
        {
            m_userTonemapSettings = tonemapper->Settings();
        }
        else
        {
            // revert to captured reference tonemap state
            tonemapper->Settings() = m_userTonemapSettings;

            // have to disable auto exposure, otherwise image comparison makes no sense
            tonemapper->Settings().UseAutoExposure = false;

            m_restoreTonemappingAfterTests = true;
        }
    }
    else if( m_restoreTonemappingAfterTests )
    {
        // revert to captured reference tonemap state
        tonemapper->Settings( ) = m_userTonemapSettings;
        m_restoreTonemappingAfterTests = false;
    }
    */

}

void vaTextureReductionTestTool::TickGPU( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & colorBuffer )
{
    vaPostProcess & postProcess = renderContext.GetRenderDevice( ).GetPostProcess( );

    renderContext;
    colorBuffer;

    if( m_downscaleTextureButtonClicks == 0 )
    {
        assert( !m_runningTests );
        DownscaleAll( renderContext );
        return;
    }

    if( !m_runningTests )
        return;

    // we've just started - init and loop
    if( m_currentTexture == -1 )
    {
        m_currentTexture    = 0;
        m_currentCamera     = 0;
        m_cameraSlotSelectedIndex = 0;
        m_texturesMaxFoundReduction[m_currentTexture] = vaMath::Clamp( m_maxLevelsToDrop, 0, m_textures[m_currentTexture].first->GetMipLevels( ) - 3 );
        m_currentSearchReductionCount = 0;
        ResetTextureOverrides( );
        return;
    }
    if( m_currentTexture == (int)m_textures.size( ) )
    {
        ResetTextureOverrides( );
        m_runningTests              = false;
        m_currentTexture            = -1;
        m_currentCamera             = -1;
        m_currentSearchReductionCount  = -1;
        return;
    }

    // requested camera different from current? set & wait a frame to get up-to-date camera
    if( m_cameraSlotSelectedIndex != m_currentCamera )
    {
        assert( false );
        // need to switch camera for next frame
        m_cameraSlotSelectedIndex = m_currentCamera;
        m_currentSearchReductionCount = 0;

        if( m_currentCamera == 0 )
        {
            // we've just started? start with max and work down
            m_texturesMaxFoundReduction[m_currentTexture] = vaMath::Clamp( m_maxLevelsToDrop, 0, m_textures[m_currentTexture].first->GetMipLevels( ) - 3 );
        }
        return;
    }

    bool timeToEndThisCamera = ( m_currentSearchReductionCount > m_texturesMaxFoundReduction[m_currentTexture] );
        
    if( !timeToEndThisCamera )
    {
        // first pass - just save the reference
        if( m_currentSearchReductionCount == 0 )
        {
            assert( m_currentlyOverriddenTexture == nullptr );
            // camera correctly set, capture reference
            SaveAsReference( renderContext, colorBuffer );
        }
        else
        {
            static bool compareInSRGB = true;
            vaVector4 val = postProcess.CompareImages( renderContext, m_referenceTexture, colorBuffer, compareInSRGB );
            // m_resultsMSR[m_currentTextLoopStep].push_back( val.x );
            // m_resultsPSNR[m_currentTextLoopStep].push_back( val.y );
            if( val.y < m_targetPSNRThreshold )
            {
                timeToEndThisCamera = true;
//                m_referenceTexture->SaveToPNGFile( renderContext, vaCore::GetExecutableDirectory() + L"!!A.png" );
//                colorBuffer->SaveToPNGFile( renderContext, vaCore::GetExecutableDirectory() + L"!!B.png" );
            }
        }
        
        if( !timeToEndThisCamera )
        {
            m_currentSearchReductionCount++;

            // set next reduction level (MIP)
            // assert( m_currentlyOverriddenTexture == nullptr );
            m_currentlyOverriddenTexture = m_textures[m_currentTexture].first;
            m_currentOverrideView = vaTexture::CreateView( m_currentlyOverriddenTexture,
                vaResourceBindSupportFlags::ShaderResource, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None,
                m_currentSearchReductionCount );
            m_currentlyOverriddenTexture->SetOverrideView( m_currentOverrideView );
        }
    }
        
    if( timeToEndThisCamera )
    {
        m_texturesMaxFoundReduction[m_currentTexture] = vaMath::Min( m_texturesMaxFoundReduction[m_currentTexture], m_currentSearchReductionCount-1 );
        
        // reached the end of this camera - reset
        ResetTextureOverrides( );
        m_currentSearchReductionCount = 0;
            
        // step to next existing camera
        do
        {
            m_currentCamera++;
        } while( m_currentCamera < c_cameraSlotCount && m_cameraSlots[m_currentCamera] == nullptr );
        m_cameraSlotSelectedIndex = m_currentCamera;
        if( m_currentCamera == c_cameraSlotCount )
        {
            // next texture
            m_currentTexture++;
            if( m_currentTexture == (int)m_textures.size( ) )
            {
                // finished all? exit
                m_runningTests = false;
                m_currentTexture = -1;
                m_currentCamera = -1;
                m_cameraSlotSelectedIndex = -1;
                return;
            }
            else
            {
                // run next
                m_currentCamera = 0;
                m_cameraSlotSelectedIndex = 0;
                m_texturesMaxFoundReduction[m_currentTexture] = vaMath::Clamp( m_maxLevelsToDrop, 0, m_textures[m_currentTexture].first->GetMipLevels( ) - 3 );
                m_currentSearchReductionCount = 0;
                ResetTextureOverrides( );
            }
        }
            
    }

}

void vaTextureReductionTestTool::OverrideAllWithCurrentStates( )
{
    assert( !m_overrideAll );

    m_overrideAll = true;
    for( int i = 0; i < (int)m_textures.size(); i++ )
    {
        auto overrideTex = vaTexture::CreateView( m_textures[i].first,
            vaResourceBindSupportFlags::ShaderResource, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None,
            m_texturesMaxFoundReduction[i] );
        m_textures[i].first->SetOverrideView( overrideTex );
    }
}

void vaTextureReductionTestTool::ResetData( )
{
    assert( !m_runningTests );

    ResetTextureOverrides();

    m_texturesMaxFoundReduction.clear();
    m_texturesMaxFoundReduction.resize( m_textures.size( ), 0 );

    m_texturesSorted.clear( );
    m_texturesSorted.resize( m_textures.size( ), 0 );
    for( size_t i = 0; i < m_textures.size( ); i++ )
        m_texturesSorted[i] = (int)i;

    m_runningTests      = false;
    m_currentTexture    = -1;
    m_currentCamera     = -1;

    //m_resultsPSNR.clear();
    //m_resultsMSR.clear();
    //m_avgMSR.clear();
    //m_avgPSNR.clear();
    //
    //m_resultsPSNR.resize( m_textures.size( ) );
    //m_resultsMSR.resize( m_textures.size( ) );
    //m_avgMSR.resize( m_textures.size( ) );
    //m_avgPSNR.resize( m_textures.size( ) );
    //m_texturesSortedByAveragePSNR.resize( m_textures.size( ) );
    //
    //for( size_t i = 0; i < m_textures.size( ); i++ )
    //{
    //    m_texturesSortedByAveragePSNR[i] = (int)i;
    //}
    //
    //m_testLoopsDone         = 0;
    //m_currentTextLoopStep   = -1;
}

//void vaTextureReductionTestTool::ComputeAveragesAndSort( )
//{
//    assert( !m_runningTests );
//    assert( m_currentTextLoopStep == -1 );
//
//    for( size_t i = 0; i < m_textures.size( ); i++ )
//        m_avgMSR[i] = 0.0f;
//
//    for( int j = 0; j < m_testLoopsDone; j++ )
//        for( size_t i = 0; i < m_textures.size( ); i++ )
//            m_avgMSR[i] += m_resultsMSR[i][j] / (float)m_testLoopsDone;
//
//    for( size_t i = 0; i < m_textures.size( ); i++ )
//    {
//        // Mean Squared Error to Peak Signal-to-Noise Ratio, assuming MAX is 1.0 (color values from 0 to 1); https://en.wikipedia.org/wiki/Peak_signal-to-noise_ratio
//        m_avgPSNR[i] = 10.0f * (float)log10( 1.0 / m_avgMSR[i] );
//    }
//
//    std::sort( m_texturesSortedByAveragePSNR.begin( ), m_texturesSortedByAveragePSNR.end( ), 
//        [ this ]( int a, int b ) { return m_avgPSNR[b] > m_avgPSNR[a]; } );
//}

void vaTextureReductionTestTool::ResetCamera( const shared_ptr<vaRenderCamera> & camera )
{
    if( m_userCameraBackup != nullptr )
    {
        LoadCamera( *camera, *m_userCameraBackup );
    }
}

void vaTextureReductionTestTool::DownscaleAll( vaRenderDeviceContext & renderContext )
{
    ResetTextureOverrides( );

    VA_LOG( "vaTextureReductionTestTool::DownscaleAll starting..." );

    for( int i = 0; i < m_textures.size( ); i++ )
    {
        if( m_texturesMaxFoundReduction[i] == 0 )
        {
            VA_LOG( "  texture '%s' - skipping.", m_textures[i].second.c_str() );
            continue;
        }

        if( m_textures[i].second == "paris_curtain_01b_diff" )
        {
            int dbg = 0;
            dbg++;
        }
        
        shared_ptr<vaTexture> newTexture = shared_ptr<vaTexture>( m_textures[i].first->CreateLowerResFromMIPs( renderContext, m_texturesMaxFoundReduction[i] ) );
        if( newTexture != nullptr )
        {
            VA_LOG( "  texture '%s' - downscaling from (%d, %d, %d) to (%d, %d, %d).", m_textures[i].second.c_str( ), 
                m_textures[i].first->GetSizeX(), m_textures[i].first->GetSizeY(), m_textures[i].first->GetSizeZ(),
                newTexture->GetSizeX(), newTexture->GetSizeY(), newTexture->GetSizeZ() );

            m_textureAssets[i]->ReplaceTexture( newTexture );
            m_textures[i].first = newTexture;
        }
    }

    ResetData( );
    m_downscaleTextureButtonClicks = 2;

    VA_LOG( "vaTextureReductionTestTool::DownscaleAll finished." );
}

void vaTextureReductionTestTool::TickUI( vaRenderDevice & device, const shared_ptr<vaRenderCamera> & camera, bool hasMouse )
{
    device; camera;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    if( !m_enabled )
        return;

    if( !hasMouse )
    {
        m_popupJustOpened = true;
        return;
    }

    const char * popupName = "vaTextureReductionTestTool";

    if( m_popupJustOpened )
        ImGui::OpenPopup( popupName );
    
    if( ImGui::BeginPopupModal( popupName ) )
    {
        int texCount = (int)m_textures.size();
        ImGui::Text( "Test the visual impact of reducing texture resolution - current test list has %d textures", texCount );
        if( m_popupJustOpened )
        {
            ImGui::SetKeyboardFocusHere( );
            m_popupJustOpened = false;
            m_downscaleTextureButtonClicks = 2;
        }

        bool byPSNR = true;
        byPSNR;

        vector<int> & orderVector = m_texturesSorted;

        ImGui::Text( "List of camera views to compare images from (click to save current, hover to see):" );
        bool resetSel = !m_runningTests;
        for( int i = 0; i < c_cameraSlotCount; i++ )
        {
            string id = vaStringTools::Format( "Slot%2d", i );
            vaVector4 col = (m_cameraSlots[i] == nullptr)?( vaVector4( 0.2f, 0.2f, 0.2f, 0.8f ) ) : ( vaVector4( 0.0f, 0.6f, 0.0f, 0.8f ) );
            if( i == m_cameraSlotSelectedIndex ) col = vaVector4( 0.0f, 0.0f, 0.6f, 0.8f );

            ImGui::PushStyleColor( ImGuiCol_Button,         ImFromVA( col ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered,  ImFromVA( col + vaVector4( 0.2f, 0.2f, 0.2f, 0.2f ) ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive,   ImFromVA( col + vaVector4( 0.4f, 0.4f, 0.4f, 0.2f ) ) );
            bool clicked = ImGui::Button( id.c_str() );
            ImGui::PopStyleColor( 3 );

            if( !m_overrideAll && !m_runningTests )
            {
                if( clicked )
                {
                    if( m_cameraSlotSelectedIndex == -1 )
                    { 
                        m_cameraSlots[i] = SaveCamera( *camera );
                    }
                    else
                    {
                        assert( m_userCameraBackup != nullptr );
                        m_cameraSlots[i] = std::make_shared<vaMemoryStream>( *m_userCameraBackup );
                    }
                }
                if( ImGui::IsItemHovered( ) && m_cameraSlots[i] != nullptr )
                {
                    m_cameraSlotSelectedIndex = i;
                    resetSel = false;
                }
            }
            if( i != 19 )
                ImGui::SameLine( );
        }
        ImGui::NewLine();
        if( resetSel )
            m_cameraSlotSelectedIndex = -1;

        if( ImGui::BeginChild( "TextureList", ImVec2( 1200, 500 ), true ) )
        {
            //float availableWidth = ImGui::GetContentRegionAvail( ).x - ImGui::GetStyle().ScrollbarSize;

            int columnCount = 2;

            ImGui::Columns( columnCount, "TextureListColumns", true );

            //float firstColumnWidth  = ImGui::CalcTextSize( "ThisStringShouldBeSimilarToMaxExpectedTextureNameLengthButNamesCanBePrettyLong" ).x;
            //availableWidth -= firstColumnWidth;
            //ImGui::SetColumnWidth( 0, firstColumnWidth );
            //for( int i = 1; i < columnCount; i++ )
            //    ImGui::SetColumnWidth( i, (float)( int(availableWidth) / (columnCount) ) );

            // Column titles
            ImGui::Separator( );
            ImGui::Text( "Texture name" );  ImGui::NextColumn();
            ImGui::Text( "Levels to drop and still stay over threshold" );      ImGui::NextColumn();
            ImGui::Separator();

            for( size_t i = 0; i < m_textures.size(); i++ )
            {
                ImGui::PushID( (int)i );
                //ImGui::Text( m_textures[orderVector[i]].second.c_str() );
                if( ImGui::Selectable( m_textures[orderVector[i]].second.c_str( ), m_textures[orderVector[i]].first == m_currentlyOverriddenTexture ) )
                {
                    if( !m_runningTests && !m_overrideAll )
                    {
                        if( m_textures[orderVector[i]].first == m_currentlyOverriddenTexture )
                        {
                            ResetTextureOverrides();
                        }
                        else
                        {
                            ResetTextureOverrides( );
                            m_currentlyOverriddenTexture = m_textures[orderVector[i]].first;
                            m_currentOverrideView = device.GetTextureTools().GetCommonTexture( vaTextureTools::CommonTextureName::White1x1 );
                            m_currentlyOverriddenTexture->SetOverrideView( m_currentOverrideView );
                        }
                    }
                }
                ImGui::PopID();
            }
            ImGui::NextColumn();
            for( size_t i = 0; i < m_textures.size( ); i++ )
            {
                ImGui::Text( "%d", m_texturesMaxFoundReduction[orderVector[i]] );
            }
        }
        ImGui::EndChild( );

        //m_logDataNextFrame = false;

        if( !m_runningTests && m_overrideAll && ImGui::Button( "Stop overriding all textures based on currently found reductions" ) )
        {
            ResetTextureOverrides( );
            m_downscaleTextureButtonClicks = 2;
        }

        if( !m_overrideAll )
        {
            if(!m_runningTests )
            {
                ImGui::InputFloat( "Target PSNR threshold", &m_targetPSNRThreshold, 1.0f );
                m_targetPSNRThreshold = vaMath::Clamp( m_targetPSNRThreshold, 10.0f, 90.0f );
                ImGui::InputInt( "Max levels to drop", &m_maxLevelsToDrop, 1 );
                m_maxLevelsToDrop = vaMath::Clamp( m_maxLevelsToDrop, 1, 15 );

                vaVector4 col = vaVector4( 0.0f, 0.0f, 0.4f, 1.0f );
                ImGui::PushStyleColor( ImGuiCol_Button, ImFromVA( col ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImFromVA( col + vaVector4( 0.2f, 0.2f, 0.2f, 0.2f ) ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImFromVA( col + vaVector4( 0.4f, 0.4f, 0.4f, 0.2f ) ) );
                if( ImGui::Button( "Run tests" ) )
                {
                    m_runningTests = true;
                    assert( m_currentTexture == -1 );
                    assert( m_currentCamera == -1 );
                    ResetTextureOverrides();
                    m_downscaleTextureButtonClicks = 2;
                }
                ImGui::PopStyleColor( 3 );

                ImGui::SameLine( );
                if( ImGui::Button( "Log current data" ) )
                {
                    //m_logDataNextFrame = true;
                    VA_LOG( "----------------------------------------------------------------------------------------------------------------------------------------" );
                    VA_LOG( "vaTextureReductionTestTool output:" );
                    VA_LOG( "Using target PSNR threshold of no less than %.1f", m_targetPSNRThreshold );
                    VA_LOG( "Index, Texture name, Max found reduction" );
                    for( size_t i = 0; i < m_textures.size( ); i++ )
                    {
                        VA_LOG( "%d, %s, %d", i, m_textures[orderVector[i]].second.c_str(), m_texturesMaxFoundReduction[orderVector[i]] );
                    }
                    VA_LOG( "----------------------------------------------------------------------------------------------------------------------------------------" );
                    m_downscaleTextureButtonClicks = 2;
                }
                ImGui::SameLine( );
                if( ImGui::Button( "Clear current data" ) )
                {
                    ResetData();
                    m_downscaleTextureButtonClicks = 2;
                }
                ImGui::SameLine( );

                if( !m_runningTests && !m_overrideAll && ImGui::Button( "Preview currently found reductions" ) )
                {
                    ResetTextureOverrides( );
                    OverrideAllWithCurrentStates( );
                    m_downscaleTextureButtonClicks = 2;
                }
                ImGui::SameLine( );

                col = vaVector4( 0.4f, 0.0f, 0.0f, 1.0f );
                ImGui::PushStyleColor( ImGuiCol_Button, ImFromVA( col ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImFromVA( col + vaVector4( 0.2f, 0.2f, 0.2f, 0.2f ) ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImFromVA( col + vaVector4( 0.4f, 0.4f, 0.4f, 0.2f ) ) );

                m_downscaleTextureButtonClicks = vaMath::Clamp( m_downscaleTextureButtonClicks, 0, 2 );
                const char * downscaleButtonTexts[3] = { "Are you really really sure?", "Are you sure?", "Downscale all textures based on currently found reductions" };
                if( ImGui::Button( downscaleButtonTexts[m_downscaleTextureButtonClicks] ) )
                {
                    m_downscaleTextureButtonClicks--;
                }
                m_downscaleTextureButtonClicks = vaMath::Clamp( m_downscaleTextureButtonClicks, 0, 2 );
                ImGui::PopStyleColor( 3 );

                ImGui::SameLine( ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Close tool").x - ImGui::GetStyle().FramePadding.x * 2.0f );
                if( ImGui::Button( "Close tool" ) )
                {
                    ImGui::CloseCurrentPopup( );
                    m_enabled = false;
                }
            }
            else
            {
                ImGui::Button( "<Please wait, running tests>", ImVec2( ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x * 2.0f, 0.0f ) );
            }
        }
        ImGui::EndPopup( );
    }
#endif
}

#endif