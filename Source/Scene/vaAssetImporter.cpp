///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaAssetImporter.h"

#include "Core/System/vaMemoryStream.h"
#include "Core/System/vaFileTools.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"
#include "Rendering/vaDebugCanvas.h"

#include "IntegratedExternals/vaImguiIntegration.h"

using namespace Vanilla;

bool LoadFileContents_Assimp( const string & path, vaAssetImporter::ImporterContext & parameters );

bool LoadFileContents_cgltf( const string & path, vaAssetImporter::ImporterContext & parameters );

vaAssetImporter::vaAssetImporter( vaRenderDevice & device ) : 
    vaUIPanel( "Asset Importer", 0,
#ifdef VA_ASSIMP_INTEGRATION_ENABLED
        true
#else
        false
#endif
        , vaUIPanel::DockLocation::DockedRight
        ),
    m_device( device )
{
}
vaAssetImporter::~vaAssetImporter( )
{
    Clear();
}


bool vaAssetImporter::LoadFileContents( const string & path, ImporterContext & importerContext )
{
    string filename;
    string ext;
    string textureSearchPath;
    vaFileTools::SplitPath( path.c_str( ), &textureSearchPath, &filename, &ext );
    ext = vaStringTools::ToLower( ext );
    if( ext == ".gltf" )
    {
        return LoadFileContents_cgltf( path, importerContext );
    }
    else
    {
        return LoadFileContents_Assimp( path, importerContext );
    }

    //        VA_WARN( "vaAssetImporter::LoadFileContents - don't know how to parse '%s' file type!", ext.c_str( ) );
    return false;
}

vaAssetImporter::ImporterContext::~ImporterContext( )
{
    if( AssetPack != nullptr )
        Device.GetAssetPackManager().UnloadPack( AssetPack );
}

void vaAssetImporter::Clear( )
{
    if( m_importerTask != nullptr )
    {
        if( !vaBackgroundTaskManager::GetInstance( ).IsFinished( m_importerTask ) )
        {
            m_importerContext->Abort();
            vaBackgroundTaskManager::GetInstance( ).WaitUntilFinished( m_importerTask );
        }
        m_importerTask = nullptr;
    }
    m_importerContext = nullptr;
    m_readyToImport = true;
}

void vaAssetImporter::UIPanelTickAlways( vaApplicationBase & )
{
    // these shouldn't ever appear anywhere unless we draw them
    if( GetAssetPack( ) != nullptr )
        GetAssetPack( )->UIPanelSetVisible( false );
    if( GetScene( ) != nullptr )
        GetScene( )->UIPanelSetVisible( false );
}

void vaAssetImporter::Draw3DUI( vaDebugCanvas3D& canvas3D ) const
{
    canvas3D.DrawAxis( vaVector3( 0, 0, 0 ), 10000.0f, NULL, 0.3f );

    for( float gridStep = 1.0f; gridStep <= 1000; gridStep *= 10 )
    {
        int gridCount = 10;
        for( int i = -gridCount; i <= gridCount; i++ )
        {
            canvas3D.DrawLine( { i * gridStep, -gridCount * gridStep, 0.0f }, { i * gridStep, +gridCount * gridStep, 0.0f }, 0x80000000 );
            canvas3D.DrawLine( { -gridCount * gridStep, i * gridStep, 0.0f }, { +gridCount * gridStep, i * gridStep, 0.0f }, 0x80000000 );
        }
    }
}

void vaAssetImporter::UIPanelTick( vaApplicationBase & application )
{
    application;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
#ifdef VA_ASSIMP_INTEGRATION_ENABLED

    if( m_importerContext != nullptr && m_importerContext->Scene != nullptr &&
        ImGui::Button( "Set preview IBL", { -1, 0 } ) )
    {
        string file = vaFileTools::OpenFileDialog( "", vaCore::GetExecutableDirectoryNarrow( ) );
        if( file != "" )
        {
            assert( false );
            // m_importerContext->Scene->DistantIBL( ).SetImportFilePath( file );
        }
    }
    ImGui::Separator( );


    // importing assets UI
    if( m_readyToImport )
    {
        assert( m_importerContext == nullptr );

        ImGui::Text( "Importer options" );

        ImGui::Indent( );
        ImGui::Text( "Base transformation (applied to everything):" );

        ImGui::InputFloat3( "Base rotate yaw pitch roll", &m_settings.BaseRotateYawPitchRoll.x );
        vaVector3::Clamp( m_settings.BaseRotateYawPitchRoll, vaVector3( -180.0f, -180.0f, -180.0f ), vaVector3( 180.0f, 180.0f, 180.0f ) );
        if( ImGui::IsItemHovered() ) 
            ImGui::SetTooltip( "Yaw around the +Z (up) axis, a pitch around the +Y (right) axis, and a roll around the +X (forward) axis." );

        ImGui::InputFloat3( "Base scale", &m_settings.BaseTransformScaling.x );
        ImGui::InputFloat3( "Base offset", &m_settings.BaseTransformOffset.x );

        //ImGui::InputFloat3( "Base scaling", &m_uiContext.ImportingPopupBaseScaling.x );
        //ImGui::InputFloat3( "Base translation", &m_uiContext.ImportingPopupBaseTranslation.x );
        ImGui::Separator( );
        ImGui::Checkbox( "Assimp: Force (re-)generate normals", &m_settings.AIForceGenerateNormals );
        // ImGui::Checkbox( "Assimp: generate normals (if missing)", &m_settings.AIGenerateNormalsIfNeeded );
        ImGui::Checkbox( "Assimp: Generate smooth normals (if generating)", &m_settings.AIGenerateSmoothNormalsIfGenerating );
        ImGui::Checkbox( "Assimp: SplitLargeMeshes", &m_settings.AISplitLargeMeshes );
        ImGui::Checkbox( "Assimp: FindInstances", &m_settings.AIFindInstances );
        ImGui::Checkbox( "Assimp: OptimizeMeshes", &m_settings.AIOptimizeMeshes );
        ImGui::Checkbox( "Assimp: OptimizeGraph", &m_settings.AIOptimizeGraph );
        ImGui::Checkbox( "Assimp: Flip UVs", &m_settings.AIFLipUVs );
        ImGui::Separator( );
        ImGui::Checkbox( "Textures: GenerateMIPs", &m_settings.TextureGenerateMIPs );
        ImGui::Separator( );
        ImGui::InputText( "AssetNamePrefix", &m_settings.AssetNamePrefix );
        //ImGui::Checkbox( "Regenerate tangents/bitangents",      &m_uiContext.ImportingRegenerateTangents );
        ImGui::Separator( );
        ImGui::Text( "Predefined lighting (if no imported lights)" );
        ImGui::InputText( "Envmap", &m_settings.DefaultDistantIBL, ImGuiInputTextFlags_AutoSelectAll );
        ImGui::SameLine( );
        if( ImGui::Button( "...###DefaultDistantIBLEllipsis" ) )
        {
            string fileName = vaFileTools::OpenFileDialog( m_settings.DefaultDistantIBL, vaCore::GetExecutableDirectoryNarrow( ) );
            if( fileName != "" )
                m_settings.DefaultDistantIBL = fileName;
        }
        //ImGui::Checkbox( "Directional light", &m_settings.AddDefaultLightDirectional );
        //if( m_settings.AddDefaultLightDirectional )
        //{
        //    VA_GENERIC_RAII_SCOPE( ImGui::Indent();, ImGui::Unindent(); );
        //    vaVector3 colorSRGB = vaVector3::LinearToSRGB( m_settings.DefaultLightDirectionalColor );
        //    if( ImGui::ColorEdit3( "Color", &colorSRGB.x, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float ) )
        //        m_settings.DefaultLightDirectionalColor = vaVector3::SRGBToLinear( colorSRGB );
        //    ImGui::InputFloat( "Intensity", &m_settings.DefaultLightDirectionalIntensity );
        //    if( ImGui::InputFloat3( "Direction", &m_settings.DefaultLightDirectionalDir.x, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue ) )
        //        m_settings.DefaultLightDirectionalDir = m_settings.DefaultLightDirectionalDir.Normalized();
        //}
        ImGui::Separator( );
        ImGui::Unindent( );

        ImGui::Separator( );
        ImGui::InputText( "Input file", &m_inputFile, ImGuiInputTextFlags_AutoSelectAll );
        ImGui::SameLine( );
        if( ImGui::Button( "...###InputFileEllipsis" ) )
        {
            string fileName = vaFileTools::OpenFileDialog( m_inputFile, vaCore::GetExecutableDirectoryNarrow( ) );
            if( fileName != "" )
                m_inputFile = fileName;
        }
        ImGui::Separator( );

        if( vaFileTools::FileExists( m_inputFile ) )
        {
            if( ImGui::Button( "RUN IMPORTER", ImVec2( -1.0f, 0.0f ) ) )
            {
                assert( m_importerTask == nullptr );
                m_readyToImport = false;

                string fileName;
                vaFileTools::SplitPath( m_inputFile, nullptr, &fileName, nullptr );

                shared_ptr<vaAssetPack>     assetPack   = m_device.GetAssetPackManager( ).CreatePack( fileName + "_AssetPack" );

                if( assetPack == nullptr )
                {
                    Clear();
                    return;
                }

                shared_ptr<vaScene>         scene = std::make_shared<vaScene>( fileName ); //+ "_Scene" );

                if( m_settings.DefaultDistantIBL != ""  )
                {
                    entt::entity lightingParent = scene->CreateEntity( "Default Lighting (not imported)" );

                    if( m_settings.DefaultDistantIBL != "" )
                    {
                        Scene::DistantIBLProbe probe;
                        probe.Enabled = true;
                        probe.SetImportFilePath( m_settings.DefaultDistantIBL );
                        entt::entity probeEntity = scene->CreateEntity( "DistantIBLProbe", vaMatrix4x4::FromTranslation( probe.Position ), lightingParent );
                        scene->Registry( ).emplace<Scene::DistantIBLProbe>( probeEntity, probe );
                    }
                    // if( m_settings.AddDefaultLightDirectional  )
                    // {
                    //     Scene::LightDirectional lightDirectional;
                    // 
                    //     vaMatrix3x3 rot = vaMatrix3x3::Identity;
                    //     rot.Row( 0 ) = m_settings.DefaultLightDirectionalDir;
                    //     vaVector3::ComputeOrthonormalBasis( rot.Row( 0 ), rot.Row( 1 ), rot.Row( 2 ) );
                    //     entt::entity lightEntity = scene->CreateEntity( "DirectionalLight", vaMatrix4x4::FromRotationTranslation( rot, {0,0,0} ), lightingParent );
                    //     auto & newLight         = scene->Registry().emplace<Scene::LightDirectional>( lightEntity );
                    //     newLight.Color          = m_settings.DefaultLightDirectionalColor;
                    //     newLight.Intensity      = m_settings.DefaultLightDirectionalIntensity;
                    // }
                }

                vaMatrix4x4 baseTransform
                    = vaMatrix4x4::Scaling( m_settings.BaseTransformScaling ) 
                    * vaMatrix4x4::FromYawPitchRoll( vaMath::DegreeToRadian( m_settings.BaseRotateYawPitchRoll.x ), vaMath::DegreeToRadian( m_settings.BaseRotateYawPitchRoll.y ), vaMath::DegreeToRadian( m_settings.BaseRotateYawPitchRoll.z ) ) 
                    * vaMatrix4x4::Translation( m_settings.BaseTransformOffset );

                m_importerContext = std::make_shared<ImporterContext>( m_device, m_inputFile, assetPack, scene, m_settings, baseTransform );

                auto importerLambda = [ importerContext = m_importerContext ] ( vaBackgroundTaskManager::TaskContext & ) -> bool 
                {
                    // vaThreading::SetSyncedWithMainThread( );
                    vaAssetImporter::LoadFileContents( importerContext->FileName, *importerContext );
                    return true;
                };
                m_importerTask = vaBackgroundTaskManager::GetInstance().Spawn( "", vaBackgroundTaskManager::SpawnFlags::None, importerLambda );
            }
        }
        else
            ImGui::Text( "Select input file!" );
    }
    if( !m_readyToImport && m_importerContext != nullptr )
    {
        if( m_importerTask != nullptr && !vaBackgroundTaskManager::GetInstance().IsFinished( m_importerTask ) )
        {
            ImGui::ProgressBar( m_importerContext->GetProgress() );
            if( ImGui::Button( "Abort!", ImVec2( -1.0f, 0.0f ) ) )
            {
                m_importerContext->AddLog( "Aborting...\n" );
                m_importerContext->Abort( );
            }
        }
        else
        {
            ImGui::Text( "Import finished, log:" );
        }

        string logText = m_importerContext->GetLog();
        // how to scroll to bottom? no idea, see if these were resolved:
        // https://github.com/ocornut/imgui/issues/2072
        // https://github.com/ocornut/imgui/issues/1972
        // ImGui::InputTextMultiline( "Importer log", &logText, ImVec2(-1.0f, ImGui::GetTextLineHeight() * 8), ImGuiInputTextFlags_ReadOnly );
        ImGui::BeginChild( "Child1", ImVec2( -1, ImGui::GetTextLineHeight() * 8 ), true, ImGuiWindowFlags_HorizontalScrollbar );
        ImGui::Text( logText.c_str() );
        ImGui::SetScrollHereY( 1.0f );
        ImGui::EndChild();

        ImGui::Separator();

        if( vaBackgroundTaskManager::GetInstance().IsFinished( m_importerTask ) )
        {
            // if( ImGui::BeginChild( "ImporterDataStuff", ImVec2( 0.0f, 0.0f ), true ) )
            // {
            if( ImGui::Button( "Clear all imported data", ImVec2( -1.0f, 0.0f ) ) )
            {
                Clear( );
            }
            ImGui::Separator( );
            ImGui::Text( "Imported data:" );
            ImGui::Separator();
            if( GetAssetPack() == nullptr )
            {
                ImGui::Text( "Assets will appear here after importing" );
            }
            else
            {
                GetAssetPack()->UIPanelTickCollapsable( application, false, true );
            }
            ImGui::Separator();
            if( GetScene() == nullptr )
            {
                ImGui::Text( "Scene will appear here after importing" );
            }
            else
            {
                GetScene()->UIPanelTickCollapsable( application, false, true );
            }
        }
        //}
        // ImGui::End( );
    }
#else
    ImGui::Text("VA_ASSIMP_INTEGRATION_ENABLED not defined!");
#endif
#endif // #ifdef VA_IMGUI_INTEGRATION_ENABLED
}
