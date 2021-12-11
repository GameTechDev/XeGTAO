///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Vanilla.h"

#include "Core/System/vaFileTools.h"
#include "Core/vaProfiler.h"

#include "Rendering/vaGPUTimer.h"

#include "Rendering/vaDebugCanvas.h"
#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"
#include "Rendering/vaAssetPack.h"
#include "Rendering/vaRenderGlobals.h"
#include "Rendering/Effects/vaASSAOLite.h"
#include "Rendering/Effects/vaGTAO.h"

#include "IntegratedExternals/vaImguiIntegration.h"
#include "Scene/vaAssetImporter.h"

#include "Scene/vaScene.h"

#include "Core/System/vaMemoryStream.h"

#include "Rendering/vaSceneRenderer.h"
#include "Rendering/vaSceneMainRenderView.h"

#include "Scene/vaCameraControllers.h"

#include "Rendering/vaRenderDevice.h"
#include "Rendering/Misc/vaZoomTool.h"
#include "Rendering/Misc/vaImageCompareTool.h"

#include "Rendering/vaPathTracer.h"

//#include "Rendering/Misc/vaTextureReductionTestTool.h"

#include <iomanip>
#include <sstream> // stringstream
#include <fstream>


using namespace Vanilla;

static pair<string, vaApplicationLoopFunction>      g_workspaces[128];
static int                                          g_currentWorkspace  = 21;
static int                                          g_workspaceCount    = 0;
static int                                          g_nextWorkspace     = g_currentWorkspace;

static const char *                                 c_cameraPresetsRootEntityName = "PresetCameras";
static string CamIndexToName( int index ) { return vaStringTools::Format( "Cam%d", index ); };

void InitWorkspaces( );

static void Dispatcher( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static shared_ptr<int> aliveToken = nullptr;
    if( applicationState == vaApplicationState::Initializing )
    {
        aliveToken = std::make_shared<int>(42);
        vaUIManager::GetInstance().RegisterMenuItemHandler( "Workspaces", aliveToken, [] ( vaApplicationBase & )
        {
#ifdef VA_IMGUI_INTEGRATION_ENABLED
            for( int i = 0; i < g_workspaceCount; i++ )
            {
                bool selected = i == g_currentWorkspace;
                if( ImGui::MenuItem( g_workspaces[i].first.c_str(), "", &selected ) )
                {
                    g_nextWorkspace = i;
                }
            }
#endif
        } );
    } else if( applicationState == vaApplicationState::ShuttingDown )
    {
        aliveToken = nullptr;
        // vaUIManager::GetInstance().UnregisterMenuItemHandler( "Workspaces" );  <- not really needed, the aliveToken will make sure it is never called and in case
        // it gets immediately re-added,  
    }

    g_currentWorkspace = vaMath::Clamp( g_currentWorkspace, 0, (int)g_workspaceCount-1 );
    g_nextWorkspace = vaMath::Clamp( g_nextWorkspace, 0, (int)g_workspaceCount-1 );

    g_workspaces[g_currentWorkspace].second( renderDevice, application, deltaTime, applicationState );

    // perform workload switch
    if( applicationState == vaApplicationState::Running && g_currentWorkspace != g_nextWorkspace )
    {
        g_workspaces[g_currentWorkspace].second( renderDevice, application, std::numeric_limits<float>::lowest(), vaApplicationState::ShuttingDown );
        g_currentWorkspace = g_nextWorkspace;
        g_workspaces[g_currentWorkspace].second( renderDevice, application, std::numeric_limits<float>::lowest(), vaApplicationState::Initializing );
        g_workspaces[g_currentWorkspace].second( renderDevice, application, deltaTime, applicationState );
    }

    if( applicationState == vaApplicationState::Running && string(VA_APP_TITLE)=="" )
        application.SetWindowTitle( g_workspaces[g_currentWorkspace].first, true );
}

int APIENTRY _tWinMain( HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPTSTR lpCmdLine, int nCmdShow )
{
    InitWorkspaces();

    // do we have a saved workspace index?
    {
#ifndef VA_GTAO_SAMPLE
        std::ifstream fileRead( ( vaCore::GetExecutableDirectory( ) + L"workspace.txt" ) );
        string textLine;
        if( fileRead.is_open( ) )
            fileRead >> g_currentWorkspace;
#endif
        g_currentWorkspace = vaMath::Clamp( g_currentWorkspace, 0, (int)g_workspaceCount );
        g_nextWorkspace = g_currentWorkspace;
    }

    // testing base64
    // {
    //     auto data = vaFileTools::LoadMemoryStream( vaCore::GetExecutableDirectory( ) + L"Media\\test_normalmap.dds" );
    //     if( data != nullptr )
    //     {
    //         string b64 = vaStringTools::Base64Encode( data->GetBuffer(), data->GetLength() );
    //         vaFileTools::WriteText( vaCore::GetExecutableDirectoryNarrow( ) + "Media\\test_normalmap.dds.base64", b64 );
    // 
    //         auto b64dec = vaStringTools::Base64Decode( b64 );
    //         vaFileTools::WriteBuffer( vaCore::GetExecutableDirectoryNarrow( ) + "Media\\test_normalmap.dds.base64.dds", b64dec->GetBuffer(), b64dec->GetLength() );
    //     }
    // }


    {
        VA_GENERIC_RAII_SCOPE( vaCore::Initialize( );, vaCore::Deinitialize( ); );


        vaApplicationWin::Settings settings( VA_APP_TITLE, lpCmdLine, nCmdShow );
        
        // settings.Vsync = true;

        vaApplicationWin::Run( settings, Dispatcher );

        // save current workload index!
#ifndef VA_GTAO_SAMPLE
        {
            std::ofstream fileWrite( (vaCore::GetExecutableDirectory() + L"workspace.txt") );
            if( fileWrite.is_open() )
                fileWrite << g_currentWorkspace;
        }
#endif
    }
    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// vanilla scene + importer sections below
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template< bool ImporterMode >
void VanillaSceneTemplate( vaRenderDevice& renderDevice, vaApplicationBase& application, float deltaTime, vaApplicationState applicationState )
{
    renderDevice; deltaTime;
    static shared_ptr<VanillaSample> vanillaSample;
    if( applicationState == vaApplicationState::Initializing )
    {
        vanillaSample = std::make_shared<VanillaSample>( application.GetRenderDevice( ), application, ImporterMode );
        application.Event_Tick.Add( vanillaSample, &VanillaSample::OnTick );    // <- this 'takes over' the tick!
        application.Event_BeforeStopped.Add( vanillaSample, &VanillaSample::OnBeforeStopped );
        application.Event_SerializeSettings.Add( vanillaSample, &VanillaSample::OnSerializeSettings );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        vanillaSample = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );
    // stuff gets handled by the VanillaSample::OnTick :)
}

void VanillaScene( vaRenderDevice& renderDevice, vaApplicationBase& application, float deltaTime, vaApplicationState applicationState )
{
    return VanillaSceneTemplate<false>( renderDevice, application, deltaTime, applicationState );
}

void VanillaAssetImporter( vaRenderDevice& renderDevice, vaApplicationBase& application, float deltaTime, vaApplicationState applicationState )
{
    return VanillaSceneTemplate<true>( renderDevice, application, deltaTime, applicationState );
}

static string CameraFileName( int index )
{
    string fileName = vaCore::GetExecutableDirectoryNarrow( ) + "last";
    if( index != -1 ) 
        fileName += vaStringTools::Format( "_%d", index );
    fileName += ".camerastate";
    return fileName;
}

entt::entity MakeSphereLight( vaRenderDevice & renderDevice, vaScene & scene, const string & name, const vaVector3 & position, float size, const vaVector3 & color, const float intensity, entt::entity parentEntity )
{
    const vaGUID & unitSphereMeshID     = renderDevice.GetMeshManager().UnitSphere()->UIDObject_GetUID();
    const vaGUID & emissiveMaterialID   = renderDevice.GetMaterialManager().GetDefaultEmissiveLightMaterial()->UIDObject_GetUID();

    entt::entity lightEntity = scene.CreateEntity( name, vaMatrix4x4::FromScaleRotationTranslation( {size, size, size}, vaMatrix3x3::Identity, position ), parentEntity, 
        unitSphereMeshID, emissiveMaterialID );

    auto & newLight         = scene.Registry().emplace<Scene::LightPoint>( lightEntity );
    newLight.Color          = color;
    newLight.Intensity      = intensity;
    newLight.FadeFactor     = 1.0f;
    newLight.Radius         = 1.0f;
    newLight.Range          = 5000.0f;
    newLight.SpotInnerAngle = 0.0f;
    newLight.SpotOuterAngle = 0.0f;
    newLight.CastShadows    = false;

    Scene::EmissiveMaterialDriver & emissiveDriver = scene.Registry().emplace<Scene::EmissiveMaterialDriver>( lightEntity );
    emissiveDriver.ReferenceLightEntity    = Scene::EntityReference( scene.Registry(), lightEntity );
    emissiveDriver.AssumeUniformUnitSphere = true;
    
    return lightEntity;
}

entt::entity MakeConnectingRod( vaRenderDevice & renderDevice, vaScene & scene, const string & name, const vaVector3 & posFrom, const vaVector3 & posTo, float radius, entt::entity parentEntity )
{
    const vaGUID & cylinderMeshID       = renderDevice.GetMeshManager().UnitCylinder(0)->UIDObject_GetUID();
    vaGUID materialID   = "32f9a23c-6730-411a-86a8-d343aace8784"; // <- xxx_streetlight_metal from Bistro, or just use default // renderDevice.GetMaterialManager().GetDefaultMaterial()->UIDObject_GetUID();

    vaVector3 diff = posTo-posFrom;
    vaMatrix4x4 transform = vaMatrix4x4::Translation(0, 0, 0.5f) * vaMatrix4x4::Scaling( radius, radius, diff.Length() ) * vaMatrix4x4( vaMatrix3x3::RotateFromTo( vaVector3( 0, 0, 1 ), (diff).Normalized() ) );
    transform = transform * vaMatrix4x4::Translation(posFrom);

    entt::entity entity = scene.CreateEntity( name, transform, parentEntity, cylinderMeshID, materialID );

    return entity;
}

VanillaSample::VanillaSample( vaRenderDevice & renderDevice, vaApplicationBase & applicationBase, bool importerMode ) 
    : 
    vaRenderingModule( renderDevice ),
    m_application( applicationBase ),
    m_assetImporter( (importerMode)?(shared_ptr<vaAssetImporter>( new vaAssetImporter(m_renderDevice) )):(nullptr) ),
    vaUIPanel( "Vanilla", -100, true, vaUIPanel::DockLocation::DockedLeft, "", vaVector2( 500, 750 ) )
{
    // //////////////////////////////////////////////////////////////////////////
    // // TEMP TEMP TEMP TEMP
    // this doesn't work here renderDevice.GetShaderManager().RegisterShaderSearchPath( vaCore::GetExecutableDirectory() + L"../Source/IntegratedExternals/lightcuts/shaders/" );
    // //////////////////////////////////////////////////////////////////////////

    m_sceneRenderer = renderDevice.CreateModule<vaSceneRenderer>( );
    m_sceneMainView = m_sceneRenderer->CreateMainView( );
    m_sceneMainView->SetCursorHoverInfoEnabled(true);

    m_sceneMainView->Camera()->SetPosition( vaVector3( 4.3f, 29.2f, 14.2f ) );
    m_sceneMainView->Camera()->SetOrientationLookAt( vaVector3( 6.5f, 0.0f, 8.7f ) );

    m_cameraFreeFlightController    = std::shared_ptr<vaCameraControllerFreeFlight>( new vaCameraControllerFreeFlight() );
    m_cameraFreeFlightController->SetMoveWhileNotCaptured( false );

    m_cameraFlythroughController    = std::make_shared<vaCameraControllerFlythrough>();
    const float keyTimeStep = 8.0f;
    float keyTime = 0.0f;
    // search for HACKY_FLYTHROUGH_RECORDER on how to 'record' these if need be 
    float defaultDoFRange = 0.25f;
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( -15.027f, -3.197f, 2.179f ),   vaQuaternion( 0.480f, 0.519f, 0.519f, 0.480f ),     keyTime,    13.5f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 0
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( -8.101f, 2.689f, 1.289f ),     vaQuaternion( 0.564f, 0.427f, 0.427f, 0.564f ),     keyTime,     3.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 8
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( -4.239f, 4.076f, 1.621f ),     vaQuaternion( 0.626f, 0.329f, 0.329f, 0.626f ),     keyTime,     6.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 16
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 2.922f, 4.273f, 1.820f ),      vaQuaternion( 0.660f, 0.255f, 0.255f, -0.660f ),     keyTime,     8.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 24
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 4.922f, 4.273f, 1.820f ),      vaQuaternion( 0.660f, 0.255f, 0.255f, -0.660f ),     keyTime,     8.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 24
    //m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 7.658f, 4.902f, 1.616f ),      vaQuaternion( 0.703f, 0.078f, 0.078f, 0.703f ),     keyTime,     6.5f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 40
    //m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 8.318f, 3.589f, 2.072f ),      vaQuaternion( 0.886f, -0.331f, -0.114f, 0.304f ),   keyTime,    14.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 48
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 8.396f, 3.647f, 2.072f ),      vaQuaternion( 0.615f, 0.262f, 0.291f, 0.684f ),     keyTime,     3.0f, defaultDoFRange ) ); keyTime+=keyTimeStep*1.5f;   // 56
    //m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 9.750f, 0.866f, 2.131f ),      vaQuaternion( 0.747f, -0.131f, -0.113f, 0.642f ),   keyTime,     3.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 64
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 11.496f, -0.826f, 2.429f ),    vaQuaternion( 0.602f, -0.510f, -0.397f, 0.468f ),   keyTime,    10.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 72
    //m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 10.943f, -1.467f, 2.883f ),    vaQuaternion( 0.704f, 0.183f, 0.173f, 0.664f ),     keyTime,     1.2f, 1.8f*defaultDoFRange ) ); keyTime+=keyTimeStep;   // 80
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 7.312f, -3.135f, 2.869f ),     vaQuaternion( 0.692f, 0.159f, 0.158f, 0.686f ),     keyTime,     1.5f, 2.0f*defaultDoFRange ) ); keyTime+=keyTimeStep;   // 88
    //m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 7.559f, -3.795f, 2.027f ),     vaQuaternion( 0.695f, 0.116f, 0.117f, 0.700f ),     keyTime,     1.0f, 1.8f*defaultDoFRange ) ); keyTime+=keyTimeStep;   // 96
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 6.359f, -4.580f, 1.856f ),     vaQuaternion( 0.749f, -0.320f, -0.228f, 0.533f ),   keyTime,     4.0f, 1.2f*defaultDoFRange ) ); keyTime+=keyTimeStep;   // 104
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 5.105f, -6.682f, 0.937f ),     vaQuaternion( 0.559f, -0.421f, -0.429f, 0.570f ),   keyTime,     2.0f, 1.2f*defaultDoFRange ) ); keyTime+=keyTimeStep;   // 112
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 3.612f, -5.566f, 1.724f ),     vaQuaternion( 0.771f, -0.024f, -0.020f, 0.636f ),   keyTime,     2.0f, 1.2f*defaultDoFRange ) ); keyTime+=keyTimeStep;   // 120
    //m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 2.977f, -5.532f, 1.757f ),     vaQuaternion( 0.698f, -0.313f, -0.263f, 0.587f ),   keyTime,    12.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 128
    //m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 1.206f, -1.865f, 1.757f ),     vaQuaternion( 0.701f, -0.204f, -0.191f, 0.657f ),   keyTime,     2.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 136
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 0.105f, -1.202f, 1.969f ),     vaQuaternion( 0.539f, 0.558f, 0.453f, 0.439f ),     keyTime,     9.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 144
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( -6.314f, -1.144f, 1.417f ),    vaQuaternion( 0.385f, 0.672f, 0.549f, 0.314f ),     keyTime,    13.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 152
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( -15.027f, -3.197f, 2.179f ),   vaQuaternion( 0.480f, 0.519f, 0.519f, 0.480f ), keyTime+0.01f,  13.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 160
    m_cameraFlythroughController->SetFixedUp( true );

    // camera settings
    {
        auto & exposureSettings = m_sceneMainView->Camera()->ExposureSettings();
        auto & tonemapSettings  = m_sceneMainView->Camera()->TonemapSettings();
        auto & bloomSettings    = m_sceneMainView->Camera()->BloomSettings();
        exposureSettings.ExposureCompensation       = -0.4f;
        exposureSettings.UseAutoExposure            = true;     // disable for easier before/after comparisons
        exposureSettings.Exposure                   = 0.0f;
        exposureSettings.AutoExposureKeyValue       = 0.5f;
        exposureSettings.ExposureMax                = 4.5f;
        exposureSettings.ExposureMin                = -4.5f;
        //exposureSettings.AutoExposureAdaptationSpeed = std::numeric_limits<float>::infinity();   // for testing purposes we're setting this to infinity
        
        tonemapSettings;
        //tonemapSettings.UseTonemapping              = true;        // for debugging using values it's easier if it's disabled
        
        bloomSettings.UseBloom                      = true;
        bloomSettings.BloomSize                     = 0.40f;
        bloomSettings.BloomMultiplier               = 0.03f;
        bloomSettings.BloomMinThreshold             = 0.0008f;
        bloomSettings.BloomMaxClamp                 = 5.0f;
    }

    if( m_sceneMainView->ASSAO() != nullptr )
    {
        auto & ssaoSettings = m_sceneMainView->ASSAO()->Settings();
        ssaoSettings.Radius                         = 0.58f;
        ssaoSettings.ShadowMultiplier               = 0.61f;
        ssaoSettings.ShadowPower                    = 2.5f;
        ssaoSettings.QualityLevel                   = 1;
        ssaoSettings.BlurPassCount                  = 1;
        ssaoSettings.DetailShadowStrength           = 2.5f;
    #if 0 // drop to low quality for more perf
        ssaoSettings.QualityLevel                   = 0;
        ssaoSettings.ShadowMultiplier               = 0.4f;
    #endif
    }

    {
        vaFileStream fileIn;
        if( fileIn.Open( CameraFileName(-1), FileCreationMode::Open ) )
        {
            m_sceneMainView->Camera()->Load( fileIn );
        } else if( fileIn.Open( vaCore::GetExecutableDirectory( ) + L"default.camerastate", FileCreationMode::Open ) )
        {
            m_sceneMainView->Camera()->Load( fileIn );
        }
    }
    m_sceneMainView->Camera()->AttachController( m_cameraFreeFlightController );

    m_lastDeltaTime     = 0.0f;

#if !defined( VA_SAMPLE_BUILD_FOR_LAB ) && !defined( VA_SAMPLE_DEMO_BUILD )
    m_zoomTool          = std::make_shared<vaZoomTool>( GetRenderDevice() );
    m_imageCompareTool  = std::make_shared<vaImageCompareTool>(GetRenderDevice());
#endif

    m_currentScene      = nullptr;

    LoadAssetsAndScenes();
}

VanillaSample::~VanillaSample( )
{ 
    if( m_assetImporter != nullptr ) 
        m_assetImporter->Clear();
    GetRenderDevice().GetAssetPackManager().UnloadAllPacks();
}

bool VanillaSample::LoadCamera( int index )
{
    if( m_sceneMainView->Camera()->Load( CameraFileName(index) ) )
    {
        m_sceneMainView->Camera()->AttachController( m_cameraFreeFlightController );
        return true;
    }
    return false;
}

void VanillaSample::SaveCamera( int index )
{
    m_sceneMainView->Camera()->Save( CameraFileName(index) );
}

// temporary camera backup/restore (if changing with scripts or whatever)
void VanillaSample::BackupCamera( )
{
    assert( m_cameraBackup == nullptr ); // overwriting the existing backup? that's not good
    m_cameraBackup = std::make_shared<vaMemoryStream>();
    m_sceneMainView->Camera()->Save( *m_cameraBackup );
}
bool VanillaSample::RestoreCamera( )
{
    if( m_cameraBackup != nullptr )
    {
        m_cameraBackup->Seek( 0 );
        m_sceneMainView->Camera()->Load( *m_cameraBackup );
        m_cameraBackup = nullptr;
        return true;
    }
    return false;
}

void VanillaSample::LoadAssetsAndScenes( )
{
    // this loads and initializes asset pack manager - and at the moment loads assets
    GetRenderDevice().GetAssetPackManager( ).LoadPacks( "*", true );  // these should be loaded automatically by scenes that need them but for now just load all in the asset folder

    if( m_assetImporter != nullptr )
        return;

    auto sceneFiles = vaFileTools::FindFiles( vaCore::GetMediaRootDirectoryNarrow(), "*.vaScene", false );

    for( string sceneFilePath : sceneFiles )
    {
        string justFile; string justExt;
        vaFileTools::SplitPath( sceneFilePath, nullptr, &justFile, &justExt );

        m_scenesInFolder.push_back( justFile ); //+ justExt );
    }

#ifdef VA_ENABLE_TEXTURE_REDUCTION_TOOL
    vaTextureReductionTestTool::SetSupportedByApp();
#endif
}

void VanillaSample::OnBeforeStopped( )
{
#ifdef VA_ENABLE_TEXTURE_REDUCTION_TOOL
    if( vaTextureReductionTestTool::GetInstancePtr() != nullptr )
    {
        vaTextureReductionTestTool::GetInstance().ResetCamera( m_camera );
        delete vaTextureReductionTestTool::GetInstancePtr();
    }
#endif

#if 1 || defined( _DEBUG )
    RestoreCamera( );       // if scripts were messing with it 
    SaveCamera( );
#endif

    m_sceneRenderer = nullptr;
    m_sceneMainView = nullptr;
}

void VanillaSample::OnTick( float deltaTime )
{
    VA_TRACE_CPU_SCOPE( OnTick );

    if( !m_hasTicked )
    {
        m_hasTicked = true;
        UIPanelSetFocusNextFrame( );
    }

    auto currentBackbufferTexture = m_renderDevice.GetCurrentBackbufferTexture( );
    if( currentBackbufferTexture == nullptr )
    {
        // probably can't create backbuffer or something similar - let's chillax until that starts working
        vaThreading::Sleep(10);
        return;
    }

    m_settings.Validate( );

    m_lastSettings = m_settings;

#if 0 // enable global geometry shader and barycentrics
    if (GetRenderDevice().GetCapabilities().Other.BarycentricsSupported == false) {
        GetRenderDevice().GetMaterialManager().SetGlobalShaderMacros({ { "VA_ENABLE_PASSTHROUGH_GS", "" }, { "VA_ENABLE_MANUAL_BARYCENTRICS", "" } });
        GetRenderDevice().GetMaterialManager().SetGlobalGSOverride(true);
    }
#endif

    bool freezeMotionAndInput = false;

#ifdef VA_ENABLE_TEXTURE_REDUCTION_TOOL
    if( vaTextureReductionTestTool::GetInstancePtr( ) != nullptr && vaTextureReductionTestTool::GetInstance( ).IsRunningTests( ) )
        freezeMotionAndInput = true;
#endif

    // handle camera presets
    {
        // if user takes control, disable using preset camera
        if( vaInputMouseBase::GetCurrent( )->IsCaptured( ) )
            m_presetCameraSelectedIndex = -1;

        // if camera preset selected, load it (and back up current)
        if( m_presetCameraSelectedIndex != -1 && m_presetCameras[m_presetCameraSelectedIndex] != nullptr )
        {
            // we had nothing selected before - great, save to backup
            if( !HasCameraBackup() )
                BackupCamera();

            m_presetCameras[m_presetCameraSelectedIndex]->Seek( 0 );
            auto controller = m_sceneMainView->Camera()->GetAttachedController( );
            m_sceneMainView->Camera()->AttachController( nullptr ); // have to remove/attach controller so it resets itself and doesn't just continue moving the way it wants
            m_sceneMainView->Camera()->Load( *m_presetCameras[m_presetCameraSelectedIndex] );
        }

        // if no longer using preset, restore previous user camera, if any
        if( m_presetCameraSelectedIndex == -1 )
        {
            // deselected - restore backup
            RestoreCamera( );
        }
    }

    // seting up camera controllers
    {
        std::shared_ptr<vaCameraControllerBase> wantedCameraController = ( freezeMotionAndInput ) ? ( nullptr ) : ( m_cameraFreeFlightController );

        if( m_cameraFlythroughPlay )
            wantedCameraController = m_cameraFlythroughController;

        if( m_presetCameraSelectedIndex != -1 )
            wantedCameraController = nullptr;

        if( m_sceneMainView->Camera()->GetAttachedController( ) != wantedCameraController )
            m_sceneMainView->Camera()->AttachController( wantedCameraController );
    }

    {
        const float minValidDelta = 0.0005f;
        if( deltaTime < minValidDelta )         // things just not correct when the framerate is so high
        {
            VA_LOG_WARNING( "frame delta time too small, clamping" );
            deltaTime = minValidDelta;
        }
        const float maxValidDelta = 0.3f;
        if( deltaTime > maxValidDelta )         // things just not correct when the framerate is so low
        {
            // VA_LOG_WARNING( "frame delta time too large, clamping" );
            deltaTime = maxValidDelta;
        }

        if( freezeMotionAndInput )
            deltaTime = 0.0f;

        m_lastDeltaTime = deltaTime;
    }

#ifdef VA_ENABLE_TEXTURE_REDUCTION_TOOL
    if( vaTextureReductionTestTool::GetInstancePtr( ) != nullptr )
    {
        auto controller = m_sceneMainView->Camera()->GetAttachedController( );
        m_sceneMainView->Camera()->AttachController( nullptr );
        vaTextureReductionTestTool::GetInstance( ).TickCPU( m_sceneMainView->Camera() );
        m_sceneMainView->Camera()->AttachController( controller );   // <- this actually updates the controller to current camera values so it doesn't override us in Tick()
        //m_camera->Tick( 0.0f, false );

        if( !vaTextureReductionTestTool::GetInstance( ).IsEnabled( ) )
            delete vaTextureReductionTestTool::GetInstancePtr( );
    }
#endif

    // camera must have the correct viewport set
    m_sceneMainView->Camera()->SetViewport( vaViewport( currentBackbufferTexture->GetWidth(), currentBackbufferTexture->GetHeight() ) );
    m_sceneMainView->Camera()->SetYFOV( m_settings.CameraYFov );

    // camera needs to get 'ticked' so it can 'tick' the controllers and update its states
    m_sceneMainView->Camera()->Tick( deltaTime, m_application.HasFocus( ) && !freezeMotionAndInput );

    // Custom importer viz stuff
#if 1
    if( m_assetImporter != nullptr )
        m_assetImporter->Draw3DUI( GetRenderDevice( ).GetCanvas3D( ) );
#endif

    // tick UI before scene because some of the scene UI doesn't want to happen during scene async (at the moment)
    m_application.TickUI( *m_sceneMainView->Camera( ) );

    // Scene stuff
    {
        auto prevScene = m_currentScene;

        if( m_assetImporter == nullptr )
        {
            auto it = std::find( m_scenesInFolder.begin(), m_scenesInFolder.end(), m_settings.CurrentSceneName );
            int nextSceneIndex = -1;
            if( it == m_scenesInFolder.end() )
                nextSceneIndex = (m_scenesInFolder.size()>0)?(0):(-1);
            else
                nextSceneIndex = (int)(it - m_scenesInFolder.begin());
            if( m_currentSceneIndex != nextSceneIndex )
            {
                m_currentSceneIndex = nextSceneIndex;
                if( m_currentSceneIndex == -1 )
                    m_currentScene = nullptr;
                else
                {
                    m_currentScene = vaScene::Create( );
                    m_currentScene->LoadJSON( vaCore::GetMediaRootDirectoryNarrow( ) + m_scenesInFolder[m_currentSceneIndex] + ".vaScene" );
                }
            }
        }
        else
        {
            m_currentScene = m_assetImporter->GetScene();
        }

        if( prevScene != m_currentScene )
        {
            m_sceneRenderer->SetScene( m_currentScene );
            m_presetCamerasDirty = true;
        }

        InteractiveBistroTick( deltaTime, prevScene != m_currentScene );

        // LOAD FROM SCENE

        if( m_presetCamerasDirty )  // changed, reset & reload
        {
            m_presetCamerasDirty = false;

            for( int i = 0; i < c_presetCameraCount; i++ ) m_presetCameras[i] = nullptr; // reset all
            m_presetCameraSelectedIndex = -1; // reset current index
            auto presetsRoot = Scene::FindFirstByName( m_currentScene->Registry( ), c_cameraPresetsRootEntityName, entt::null, false );
            if( presetsRoot != entt::null )
            {
                Scene::VisitChildren( m_currentScene->Registry( ), presetsRoot, [ & ]( entt::entity entity )
                {
                    const Scene::Name* name = m_currentScene->Registry( ).try_get<Scene::Name>( entity );
                    const Scene::RenderCamera* cameraCom = m_currentScene->Registry( ).try_get<Scene::RenderCamera>( entity );
                    if( name == nullptr || cameraCom == nullptr )
                        return;

                    // wow is this 'atoi' ugly :D
                    for( int i = 0; i < c_presetCameraCount; i++ )
                    {
                        string id = CamIndexToName( i );
                        if( id == *name )
                            m_presetCameras[i] = cameraCom->Data;
                    }

                } );
            }
        }

        if( m_currentScene != nullptr )
            m_currentScene->TickBegin( deltaTime, m_application.GetCurrentTickIndex() );
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Custom keyboard/mouse inputs
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if( !freezeMotionAndInput && m_application.HasFocus( ) && !vaInputMouseBase::GetCurrent( )->IsCaptured( ) 
#ifdef VA_IMGUI_INTEGRATION_ENABLED
        && !ImGui::GetIO().WantTextInput 
#endif
        )
    {
        static float notificationStopTimeout = 0.0f;
        notificationStopTimeout += deltaTime;

        vaInputKeyboardBase & keyboard = *vaInputKeyboardBase::GetCurrent( );
        if( keyboard.IsKeyDown( vaKeyboardKeys::KK_LEFT ) || keyboard.IsKeyDown( vaKeyboardKeys::KK_RIGHT ) || keyboard.IsKeyDown( vaKeyboardKeys::KK_UP ) || keyboard.IsKeyDown( vaKeyboardKeys::KK_DOWN ) ||
            keyboard.IsKeyDown( ( vaKeyboardKeys )'W' ) || keyboard.IsKeyDown( ( vaKeyboardKeys )'S' ) || keyboard.IsKeyDown( ( vaKeyboardKeys )'A' ) ||
            keyboard.IsKeyDown( ( vaKeyboardKeys )'D' ) || keyboard.IsKeyDown( ( vaKeyboardKeys )'Q' ) || keyboard.IsKeyDown( ( vaKeyboardKeys )'E' ) )
        {
            if( notificationStopTimeout > 3.0f )
            {
                notificationStopTimeout = 0.0f;
                vaLog::GetInstance().Add( vaVector4( 1.0f, 0.0f, 0.0f, 1.0f ), L"To switch into free flight (move&rotate) mode, use mouse middle click or Ctrl+Enter." );
            }
        }
#ifdef VA_IMGUI_INTEGRATION_ENABLED
        if( !ImGui::GetIO().WantCaptureMouse )
#endif
            if( m_zoomTool != nullptr ) 
                m_zoomTool->HandleMouseInputs( *vaInputMouseBase::GetCurrent() );
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef VA_ENABLE_TEXTURE_REDUCTION_TOOL
    if( vaTextureReductionTestTool::GetInstancePtr( ) != nullptr )
    {
        vaTextureReductionTestTool::GetInstance( ).TickUI( GetRenderDevice(), m_sceneMainView->Camera(), !vaInputMouseBase::GetCurrent()->IsCaptured() );
    }
#endif

    // Do the rendering tick and present 
    {
        VA_TRACE_CPU_SCOPE( RenderingSection );

        // Asset importer can trigger some scene calls from a vaRenderDevice::BeginFrame callback (don't ask) and for that it has to have finished any async stuff
        if( m_assetImporter != nullptr && m_assetImporter->GetScene( ) && m_assetImporter->GetScene( )->IsTicking( ) )
            m_assetImporter->GetScene( )->TickEnd( );

        GetRenderDevice().BeginFrame( deltaTime );

        vaDrawResultFlags drawResults = RenderTick( deltaTime );

        m_allLoadedPrecomputedAndStable = ( drawResults == vaDrawResultFlags::None ); // && m_shadowsStable && m_IBLsStable;

        if( !m_allLoadedPrecomputedAndStable )
        {
            // no need to run at fastest FPS when we're probably still loading/streaming stuff
            vaThreading::Sleep( 30 );
        }


        // update and draw imgui
        m_application.DrawUI( *GetRenderDevice().GetMainContext( ), GetRenderDevice().GetCurrentBackbuffer( ), m_sceneMainView->GetOutputDepth() );

        GetRenderDevice().EndAndPresentFrame( (m_application.GetVsync())?(1):(0) );
    }

    // End of frame; time to stop any async scene processing!
    if( m_currentScene != nullptr && m_currentScene->IsTicking( ) )
        m_currentScene->TickEnd( );
}

vaDrawResultFlags VanillaSample::RenderTick( float deltaTime )
{
    VA_TRACE_CPU_SCOPE( VanillaSample_RenderTick );

    auto currentBackbufferTexture = m_renderDevice.GetCurrentBackbufferTexture( );
    if( currentBackbufferTexture == nullptr )
        return vaDrawResultFlags::UnspecifiedError;

    // this is "comparer stuff" and the main render target stuff
    vaViewport mainViewport( currentBackbufferTexture->GetWidth(), currentBackbufferTexture->GetHeight() );
    assert( m_sceneMainView->Camera()->GetViewport() == mainViewport );

    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    drawResults = m_sceneRenderer->RenderTick( deltaTime, m_application.GetCurrentTickIndex() );

    vaRenderDeviceContext & renderContext = *GetRenderDevice( ).GetMainContext( );

    const shared_ptr<vaTexture> & finalColor     = m_sceneMainView->GetOutputColor();
//    const shared_ptr<vaTexture> & finalDepth     = m_sceneMainView->GetTextureDepth();

#if 0 // just testing something
    static bool debugPostProcessThingie = false;
    ImGui::Checkbox( "DEBUG THINGIE!", &debugPostProcessThingie );
    if( debugPostProcessThingie )
        GetRenderDevice( ).GetPostProcess( ).GenericCPUImageProcess( renderContext, finalColor );
#endif 

    // this is perfectly cromulent
    if( finalColor == nullptr )
    {
        currentBackbufferTexture->ClearRTV( renderContext, {0.5f, 0.5f, 0.5f, 1.0f} );
        return vaDrawResultFlags::None;
    }

    // tick benchmark/testing scripts before any image tools
    m_currentFrameTexture = finalColor;
    m_miniScript.TickScript( m_lastDeltaTime );
    m_currentFrameTexture = nullptr;

    // various helper tools - at one point these should go and become part of the base app but for now let's just stick them in here
    {
        if( drawResults == vaDrawResultFlags::None && m_imageCompareTool != nullptr )
            m_imageCompareTool->RenderTick( renderContext, finalColor );

        if( m_zoomTool != nullptr )
            m_zoomTool->Draw( renderContext, finalColor );
    }

#ifdef VA_ENABLE_TEXTURE_REDUCTION_TOOL
    if( vaTextureReductionTestTool::GetInstancePtr( ) != nullptr && ( drawResults == vaDrawResultFlags::None ) && m_shadowsStable && m_IBLsStable )
    {
        vaTextureReductionTestTool::GetInstance( ).TickGPU( mainContext, finalOutColor );
    }
#endif

    // Final apply to screen (redundant copy for now, leftover from expanded screen thing)
    {
        VA_TRACE_CPUGPU_SCOPE( FinalApply, renderContext );

        GetRenderDevice().StretchRect( renderContext, currentBackbufferTexture, finalColor, vaVector4( ( float )0.0f, ( float )0.0f, (float)mainViewport.Width, (float)mainViewport.Height ), vaVector4( 0.0f, 0.0f, (float)mainViewport.Width, (float)mainViewport.Height ), false );
        //mainViewport = mainViewportBackup;
    }

    return drawResults;
}

void VanillaSample::OnSerializeSettings( vaXMLSerializer & serializer )
{
    m_settings.Serialize( serializer );

    m_lastSettings = m_settings;
}

const shared_ptr<vaRenderCamera> & VanillaSample::Camera( )
{ 
    return m_sceneMainView->Camera(); 
}

void VanillaSample::UIPanelTick( vaApplicationBase & application )
{
    application;
    //ImGui::Checkbox( "Texturing disabled", &m_debugTexturingDisabled );

#ifdef VA_IMGUI_INTEGRATION_ENABLED

    if( m_miniScript.IsActive( ) )
    {
        ImGui::Text( "SCRIPT ACTIVE" );
        ImGui::Text( "" );
        m_miniScript.TickUI();
        return;
    }
   
    if( m_assetImporter != nullptr )
    {
        // ImGui::Text("IMPORTER MODE ACTIVE");
        m_assetImporter->UIPanelSetVisible( false );
        ((vaUIPanel&)*m_assetImporter).UIPanelTick( application );

        return;
    }

#if !defined( VA_SAMPLE_BUILD_FOR_LAB ) && !defined( VA_SAMPLE_DEMO_BUILD )

#ifndef VA_GTAO_SAMPLE
    ImGui::Text( "Scene files in %s", vaCore::GetMediaRootDirectoryNarrow().c_str() );
    if( m_scenesInFolder.size() > 0 )
    {
        int currentSceneIndex = std::max( 0, m_currentSceneIndex );
        if( ImGuiEx_Combo( "Scene", currentSceneIndex, m_scenesInFolder ) )
        {
            //imguiStateStorage->SetInt( displayTypeID, displayTypeIndex );
        }
        m_settings.CurrentSceneName = m_scenesInFolder[currentSceneIndex];

        InteractiveBistroUI( application );
    }
    else
    {
        ImGui::Text( "   no vaScene files found!" );
    }

    ImGui::Separator( );

    //ImGui::Checkbox( "Use depth pre-pass", &m_settings.DepthPrePass );
    //if( ImGui::CollapsingHeader( "Main scene render view", ImGuiTreeNodeFlags_DefaultOpen ) )
#endif
        m_sceneMainView->UITick( application );

    ImGui::Separator();

#if 0 // reducing UI clutter
    {
        ImGui::Separator();
        ImGui::Indent();

        float yfov = m_settings.CameraYFov / ( VA_PIf ) * 180.0f;
        ImGui::InputFloat( "Camera Y FOV", &yfov, 5.0f, 0.0f, 1 );
        m_settings.CameraYFov = vaMath::Clamp( yfov, 20.0f, 140.0f ) * ( VA_PIf ) / 180.0f;
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Camera Y field of view" );
                    
        ImGui::Checkbox( "Z-Prepass", &m_settings.ZPrePass );
        ImGui::Unindent();
    }
#endif

    if( !IsAllLoadedPrecomputedAndStable( ) )
    {
        ImGui::Separator( );
        ImGui::NewLine( );
        ImGui::Text( "Asset/shader still loading or compiling" );
        ImGui::NewLine( );
        ImGui::Separator( );
    }
    else
    // Benchmarking/scripting
    // if( m_settings.SceneChoice == VanillaSample::SceneSelectionType::LumberyardBistro )
#endif // !defined(VA_SAMPLE_BUILD_FOR_LAB) && !defined(VA_SAMPLE_DEMO_BUILD)
    {
        ScriptedTests( application );
    }

    if( !m_allLoadedPrecomputedAndStable )
    {
        ImGui::TextColored( {1.0f, 0.3f, 0.3f, 1.0f}, "Scene/assets loading (or asset/shader errors)" );
        ImGui::Separator( );
    }

#endif
}

namespace Vanilla
{
    class AutoBenchTool
    {
        VanillaSample &                         m_parent;
        vaMiniScriptInterface &                 m_scriptInterface;

        wstring                                 m_reportDir;
        wstring                                 m_reportName;
        std::vector<std::vector<string>>        m_reportCSV;
        string                                  m_reportTXT;

        // backups
        bool                                    m_backupVSync;
        vaRenderCamera::AllSettings             m_backupCameraSettings;
        vaMemoryStream                          m_backupCameraStorage;
        VanillaSample::VanillaSampleSettings    m_backupSettings;
        // vaDepthOfField::DoFSettings             m_backupDoFSettings;
        float                                   m_backupFlythroughCameraTime;
        float                                   m_backupFlythroughCameraSpeed;
        bool                                    m_backupFlythroughCameraEnabled;

        string                                  m_statusInfo = "-";
        bool                                    m_shouldStop = false;
        bool                                    m_divertTracerOutput = false;

    public:
        AutoBenchTool( VanillaSample& parent, vaMiniScriptInterface& scriptInterface, bool ensureVisualDeterminism, bool writeReport, bool divertTracerOutput );
        ~AutoBenchTool( );

        void                                    ReportAddRowValues( const std::vector<string>& row ) { m_reportCSV.push_back( row ); FlushRowValues( ); }
        void                                    ReportAddText( const string& text ) { m_reportTXT += text; }
        wstring                                 ReportGetDir( ) { return m_reportDir; }

        void                                    SetUIStatusInfo( const string& statusInfo ) { m_statusInfo = statusInfo; }
        bool                                    GetShouldStop( ) const { return m_shouldStop; }

    private:
        void                                    FlushRowValues( );
    };

    class AutoTuneTool
    {
    public:
        enum class Stage
        {
            NotStarted,
            GTCapture,          // capture ground truth for all cameras
            Search,             // compare current settings vs ground truth
            Finished
        };

        struct Setting
        {
            string      Name;       // only for a nice(r) report
            float *     Value;
            float       RangeMin;   // inclusive min
            float       RangeMax;   // inclusive max
        };

    private:
        VanillaSample &                         m_parent;
        vaMiniScriptInterface &                 m_scriptInterface;

        VanillaSample::VanillaSampleSettings    m_backupSampleSettings;
        vaSceneMainRenderView::RenderSettings   m_backupMainViewSettings;

        string                                  m_statusInfo        = "-";
        bool                                    m_shouldStop        = false;

        std::vector<shared_ptr<vaTexture>>      m_capturedGroundTruths;
        std::vector<float>                      m_measuredMSEs;

        Stage                                   m_stage             = Stage::NotStarted;

        // given a setting RangeMin/RangeMax, how many steps (inclusive) to test for each setting
        const uint32                            m_stepsPerSetting;
        const uint32                            m_narrowingPasses;
        int32                                   m_currentTestStep   = -1;
        int32                                   m_totalTestSteps    = -1;
        int32                                   m_remainingNarrowingPasses = -1;
        std::vector<Setting>                    m_settings;

        // these are per-pass best scores
        int32                                   m_bestScoreTestStep = -1;
        float                                   m_bestScoreMSE      = VA_FLOAT_HIGHEST;
        
        // these are global best scores
        float                                   m_bestTotalScoreMSE = VA_FLOAT_HIGHEST;
        std::vector<float>                      m_bestTotalScoreSettings;       // actual best scores
        std::vector<float>                      m_bestTotalScoreMSEs;           // used just for reporting

    public:
        AutoTuneTool( VanillaSample & parent, vaMiniScriptInterface & scriptInterface, int stepsPerSetting = 7, int narrowingPasses = 5 );
        ~AutoTuneTool( );

        void                                    SetUIStatusInfo( const string & statusInfo )    { m_statusInfo = statusInfo; }

        // rangeMin/rangeMax are inclusive
        void                                    AddSearchSetting( const string & name, float * settingAddr, float rangeMin, float rangeMax );

        // current inputs
        Stage                                   Stage( ) const                                  { return m_stage; }

        // returns true to continue looping, false when done
        bool                                    Tick( bool inputsReady, float inputsReadyProgress );

        void                                    PrintCurrentBestSettings( );

    private:
        void                                    OnStartSearch( );
        void                                    SetSettings( int stepIndex, bool verbose, bool logRangesOnly );
    };
}

void VanillaSample::ScriptedGTAOAutoTune( vaApplicationBase & application )
{
    application;

    if( ImGui::Button( "GTAO auto-tune" ) )
    {
        m_miniScript.Start( [ thisPtr = this ]( vaMiniScriptInterface & msi )
        {
            // this sets up some globals and also backups all the sample settings
            AutoTuneTool autoTune( *thisPtr, msi, 10, 6 );

            thisPtr->MainRenderView( )->Settings( ).AAType          = vaAAType::None;   // disables AA
            thisPtr->MainRenderView( )->Settings( ).AOOption        = 2;        // sets GTAO
            thisPtr->MainRenderView( )->Settings( ).DebugShowAO     = true;     // we're comparing vs AO output only
            thisPtr->MainRenderView( )->Settings( ).RenderPath      = vaRenderType::Rasterization;
            thisPtr->MainRenderView( )->Settings( ).ShowWireframe   = false;

            shared_ptr<vaGTAO> gtao = thisPtr->MainRenderView( )->GTAO( );
            auto oldSettings        = gtao->Settings( );
            auto & activeSettings   = gtao->Settings( );

            // add the settings to search
            autoTune.AddSearchSetting( "RadiusMultiplier",          &activeSettings.RadiusMultiplier        , 0.9f, 2.0f );
            autoTune.AddSearchSetting( "FalloffRange",              &activeSettings.FalloffRange            , 0.0f, 0.95f );
            //autoTune.AddSearchSetting( "SampleDistributionPower",   &activeSettings.SampleDistributionPower , 0.8f, 2.5f );
            //autoTune.AddSearchSetting( "ThinOccluderCompensation",  &activeSettings.ThinOccluderCompensation    , 0.0f, 0.4f );
            //autoTune.AddSearchSetting( "FinalValuePower",           &activeSettings.FinalValuePower         , 0.8f, 3.0f );

            VA_LOG( "Starting GTAO auto-tune..." );

            const int cameraCount = thisPtr->PresetCameraCount( );
            thisPtr->PresetCameraIndex( ) = -1;

            while( true ) 
            { 
                bool inputsReady = false;
                float inputsReadyProgress = 1.0f;

                // Request stuff
                if( autoTune.Stage( ) == AutoTuneTool::Stage::GTCapture )
                {
                    gtao->ReferenceRTAOEnabled( )       = true;
                }
                else if( autoTune.Stage( ) == AutoTuneTool::Stage::Search )
                {
                    gtao->ReferenceRTAOEnabled( )       = false;
                    gtao->Settings().QualityLevel       = 2;
                    gtao->Settings().DenoisePasses      = 2;
                    inputsReady = true;
                }

                // Tick frame
                if( !msi.YieldExecution( ) )
                {
                    gtao->ReferenceRTAOEnabled( )       = false;
                    return;
                }

                if( autoTune.Stage( ) == AutoTuneTool::Stage::GTCapture )
                {
                    inputsReady = gtao->ReferenceRTAOSampleCount( ) == gtao->ReferenceRTAOSampleGoal( );
                    inputsReadyProgress = gtao->ReferenceRTAOSampleCount( ) / (float)gtao->ReferenceRTAOSampleGoal( );
                }
                else if( autoTune.Stage( ) == AutoTuneTool::Stage::Search )
                {
                }

                // Tick autotune
                if( !autoTune.Tick( inputsReady, inputsReadyProgress ) )
                    break;
            }
        } );
    }

    ImGui::Separator( );

    if( ImGui::Button( "ASSAO auto-tune" ) )
    {
        m_miniScript.Start( [ thisPtr = this ]( vaMiniScriptInterface & msi )
        {
            // this sets up some globals and also backups all the sample settings
            AutoTuneTool autoTune( *thisPtr, msi, 10, 6 );

            thisPtr->MainRenderView( )->Settings( ).AAType          = vaAAType::None;        // disables AA
            thisPtr->MainRenderView( )->Settings( ).AOOption        = 1;        // sets ASSAO
            thisPtr->MainRenderView( )->Settings( ).DebugShowAO     = true;     // we're comparing vs AO output only
            thisPtr->MainRenderView( )->Settings( ).RenderPath      = vaRenderType::Rasterization;
            thisPtr->MainRenderView( )->Settings( ).ShowWireframe   = false;

            shared_ptr<vaASSAOLite> assao = thisPtr->MainRenderView( )->ASSAO( );
            shared_ptr<vaGTAO> rgtao = thisPtr->MainRenderView( )->GTAO( );
            auto oldSettings        = assao->Settings( );
            auto & activeSettings   = assao->Settings( );

            // // add the settings to search
            autoTune.AddSearchSetting( "Radius",            &activeSettings.Radius              , 0.1f, 2.0f );
            autoTune.AddSearchSetting( "ShadowMultiplier",  &activeSettings.ShadowMultiplier    , 0.5f, 2.5f );
            autoTune.AddSearchSetting( "ShadowPower",       &activeSettings.ShadowPower         , 0.5f, 2.5f );


            VA_LOG( "Starting ASSAO auto-tune..." );

            const int cameraCount = thisPtr->PresetCameraCount( );
            thisPtr->PresetCameraIndex( ) = -1;

            while( true ) 
            { 
                bool inputsReady = false;
                float inputsReadyProgress = 1.0f;

                // Request stuff
                if( autoTune.Stage( ) == AutoTuneTool::Stage::GTCapture )
                {
                    thisPtr->MainRenderView( )->Settings( ).AOOption        = 2;        // sets GTAO (for reference)
                    rgtao->ReferenceRTAOEnabled( )       = true;
                }
                else if( autoTune.Stage( ) == AutoTuneTool::Stage::Search )
                {
                    thisPtr->MainRenderView( )->Settings( ).AOOption        = 1;        // sets ASSAO
                    rgtao->ReferenceRTAOEnabled( )       = false;
                    inputsReady = true;
                }

                // Tick frame
                if( !msi.YieldExecution( ) )
                {
                    rgtao->ReferenceRTAOEnabled( )       = false;
                    return;
                }

                if( autoTune.Stage( ) == AutoTuneTool::Stage::GTCapture )
                {
                    inputsReady = rgtao->ReferenceRTAOSampleCount( ) == rgtao->ReferenceRTAOSampleGoal( );
                    inputsReadyProgress = rgtao->ReferenceRTAOSampleCount( ) / (float)rgtao->ReferenceRTAOSampleGoal( );
                }
                else if( autoTune.Stage( ) == AutoTuneTool::Stage::Search )
                {
                }

                // Tick autotune
                if( !autoTune.Tick( inputsReady, inputsReadyProgress ) )
                    break;
            }
        } );
    }
}

void VanillaSample::ScriptedAutoBench( vaApplicationBase & application )
{
    application;
    if( !application.IsFullscreen() )
        ImGui::TextColored( {1.0f, 0.3f, 0.3f, 1.0f}, "!! app not fullscreen !!" );

    VA_GENERIC_RAII_SCOPE( ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV( (float)vaMath::Frac(application.GetTimeFromStart()*0.3), 0.6f, 0.6f));, ImGui::PopStyleColor(); );

    if( ImGui::Button( "!!RUN BENCHMARK!!", {-1, 0} ) )
    {
        m_miniScript.Start( [ thisPtr = this] (vaMiniScriptInterface & msi)
        {
            // this sets up some globals and also backups all the sample settings
            AutoBenchTool autobench( *thisPtr, msi, false, true, true );

            // animation stuff
            const float c_framePerSecond    = 10;
            const float c_frameDeltaTime    = 1.0f / (float)c_framePerSecond;
            const float c_totalTime         = thisPtr->GetFlythroughCameraController()->GetTotalTime();
            const int   c_totalFrameCount   = (int)(c_totalTime / c_frameDeltaTime);
            thisPtr->SetFlythroughCameraEnabled( true );
            thisPtr->GetFlythroughCameraController( )->SetPlaySpeed( 0.0f );

            // defaults
            thisPtr->MainRenderView( )->Settings( ).AAType          = vaAAType::TAA;        // doesn't matter, just make it consistent
            thisPtr->MainRenderView( )->Settings( ).DebugShowAO     = false;                // will disturb profiling
            thisPtr->MainRenderView( )->Settings( ).RenderPath      = vaRenderType::Rasterization;
            thisPtr->MainRenderView( )->Settings( ).ShowWireframe   = false;

            shared_ptr<vaASSAOLite> assao = thisPtr->MainRenderView( )->ASSAO( );
            shared_ptr<vaGTAO> gtao = thisPtr->MainRenderView( )->GTAO( );

            // info
            std::vector<string> columnHeadersRow;
            {
                autobench.ReportAddText( "\r\nPerformance testing of XeGTAO\r\n" );

                columnHeadersRow.push_back( "" );
                //
                //columnHeadersRow.push_back( "Disabled" );       // 0
                //columnHeadersRow.push_back( "ASSAO Medium" );   // 1
                //columnHeadersRow.push_back( "XeGTAO Low" );     // 2
                //columnHeadersRow.push_back( "XeGTAO Default" ); // 3
            }

            auto tracerView = std::make_shared<vaTracerView>( );

            // autobench.ReportAddText( "\r\n" );  

            autobench.ReportAddRowValues(columnHeadersRow);

            const char * aoTypes[] = { "No AO", "ASSAO Medium", "XeGTAO Low FP32", "XeGTAO High FP32", "XeGTAO Low FP16", "XeGTAO High FP16" };
            const int aoTypePassCount = (countof(aoTypes)*2+1);
            for( int aoTypePass = 0; aoTypePass < aoTypePassCount; aoTypePass++ )
            {
                int aoType = aoTypePass % countof(aoTypes);
                autobench.ReportAddText( vaStringTools::Format( "\r\nAO type: %s\r\n", aoTypes[aoType] ) );

                switch( aoType )
                {
                case 0: thisPtr->MainRenderView( )->Settings( ).AOOption        = 0;                    // sets nothing
                    break;
                case 1: thisPtr->MainRenderView( )->Settings( ).AOOption        = 1;                    // sets ASSAO medium
                    assao->Settings( ).QualityLevel = 1;
                    break;
                case 2: thisPtr->MainRenderView( )->Settings( ).AOOption        = 2;                    // sets XeGTAO low FP32
                    gtao->Settings( ).QualityLevel  = 0;
                    gtao->Use16bitMath( )           = false;
                    break;
                case 3: thisPtr->MainRenderView( )->Settings( ).AOOption        = 2;                    // sets XeGTAO high FP32
                    gtao->Settings( ).QualityLevel  = 2;
                    gtao->Use16bitMath( )           = false;
                    break;
                case 4: thisPtr->MainRenderView( )->Settings( ).AOOption        = 2;                    // sets XeGTAO low FP16
                    gtao->Settings( ).QualityLevel  = 0;
                    gtao->Use16bitMath( )           = true;
                    break;
                case 5: thisPtr->MainRenderView( )->Settings( ).AOOption        = 2;                    // sets XeGTAO high FP16
                    gtao->Settings( ).QualityLevel  = 2;
                    gtao->Use16bitMath( )           = true;
                    break;
                default: assert( false );
                }

                std::vector<string> reportRowAvgTime, reportRowAvgTimeAO;
                reportRowAvgTime.push_back( "Frame total (ms)" );
                reportRowAvgTimeAO.push_back( "AO only (ms)" );

                // do an empty run first
                bool warmupPass = true;
                for( int rpt = 0; rpt < 1; rpt++ )
                {
                    // thisPtr->SetVRSOption( rpt );

                    string status = "running pass " + std::to_string(aoTypePass) + " of " + std::to_string(aoTypePassCount) + " ";
                    status += aoTypes[aoType];
                    if( warmupPass )
                        status += " (warmup pass)";

                    autobench.SetUIStatusInfo( status + ", preparing..."  );

                    thisPtr->GetFlythroughCameraController( )->SetPlayTime( 0 );

                    // wait until IsAllLoadedPrecomputedAndStable and then run 4 more loops - this ensures the numbers from the vaProfiler 
                    // are not from previous test case.
                    int startupLoops = 3;
                    do
                    { 
                        if( !thisPtr->IsAllLoadedPrecomputedAndStable( ) )
                            startupLoops = 3;
                        startupLoops--;
                        if( !msi.YieldExecutionFor( 1 ) || autobench.GetShouldStop() )
                            return;
                    }
                    while( startupLoops > 0 );


                    using namespace std::chrono;
                    high_resolution_clock::time_point t1 = high_resolution_clock::now();

                    //float totalTime = 0.0f;
                    float totalTimeInAO = 0.0f;
                    float totalTimeInAOP0 = -1.0f;
                    float totalTimeInAOP1 = -1.0f;
                    float totalTimeInAOP2 = -1.0f;

                    if( !warmupPass )
                        tracerView->ConnectToThreadContext( string(vaGPUContextTracer::c_threadNamePrefix) + "*", VA_FLOAT_HIGHEST );

                    // ok let's go
                    for( int testFrame = 0; testFrame < c_totalFrameCount; testFrame++ )
                    {
                        autobench.SetUIStatusInfo( status + ", " + vaStringTools::Format( "%.1f%%", (float)testFrame/(float)(c_totalFrameCount-1)*100.0f ) );

                        thisPtr->GetFlythroughCameraController( )->SetPlayTime( testFrame * c_frameDeltaTime );
                        if( !msi.YieldExecution( ) || autobench.GetShouldStop() )
                            return;

                        // totalTime += msi.GetDeltaTime();
                        if( warmupPass )
                            testFrame += 3;
                    }

                    if( !warmupPass )
                    {
                        tracerView->Disconnect();
                        const vaTracerView::Node * node = tracerView->FindNodeRecursive( "XeGTAO" );
                        if( node == nullptr ) 
                            node = tracerView->FindNodeRecursive( "ASSAO" );
                        else
                        {
                            const vaTracerView::Node * nodeP0 = node->FindRecursive( "PrefilterDepths" );
                            const vaTracerView::Node * nodeP1 = node->FindRecursive( "MainPass" );
                            const vaTracerView::Node * nodeP2 = node->FindRecursive( "Denoise" );
                            totalTimeInAOP0 = (nodeP0 == nullptr)?(-1.0f):((float)nodeP0->TimeTotal);
                            totalTimeInAOP1 = (nodeP1 == nullptr)?(-1.0f):((float)nodeP1->TimeTotal);
                            totalTimeInAOP2 = (nodeP2 == nullptr)?(-1.0f):((float)nodeP2->TimeTotal);
                        }
                        if( node != nullptr )
                        {
                            totalTimeInAO = (float)node->TimeTotal;
                            assert( node->Instances == c_totalFrameCount );
                        }
                    }

                    high_resolution_clock::time_point t2 = high_resolution_clock::now();
                    float totalTime = (float)duration_cast<duration<double>>(t2 - t1).count();

                    if( warmupPass )
                    {
                        rpt--; //restarts at 0
                        warmupPass = false;
                    }
                    else
                    {
                        reportRowAvgTime.push_back( vaStringTools::Format("%.3f", totalTime * 1000.0f / (float)c_totalFrameCount) );
                        if( totalTimeInAOP0 != -1 && totalTimeInAOP1 != -1 && totalTimeInAOP2 != -1 )
                            reportRowAvgTimeAO.push_back( vaStringTools::Format("%.3f, , %.3f, %.3f, %.3f", totalTimeInAO * 1000.0f / (float)c_totalFrameCount, totalTimeInAOP0 * 1000.0f / (float)c_totalFrameCount, totalTimeInAOP1 * 1000.0f / (float)c_totalFrameCount, totalTimeInAOP2 * 1000.0f / (float)c_totalFrameCount ) );
                        else
                            reportRowAvgTimeAO.push_back( vaStringTools::Format("%.3f", totalTimeInAO * 1000.0f / (float)c_totalFrameCount) );
                    }
                }
                autobench.ReportAddRowValues( reportRowAvgTime );
                autobench.ReportAddRowValues( reportRowAvgTimeAO );
            }
            autobench.ReportAddText( "\r\n" );
    } );        
    }
}

void VanillaSample::ScriptedDemo( vaApplicationBase & application )
{
    application;
    //if( !application.IsFullscreen() )
    //    ImGui::TextColored( {1.0f, 0.3f, 0.3f, 1.0f}, "!! app not fullscreen !!" );

    // bool effectEnabled = m_sceneMainView->Settings().AOOption == 3;
    // ImGui::Checkbox( "Enable XeGTAO", &effectEnabled );
    // if( effectEnabled )
    //     m_sceneMainView->Settings().AOOption = 3;
    // else
    //     m_sceneMainView->Settings().AOOption = 0;

    //VA_GENERIC_RAII_SCOPE( ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV( (float)vaMath::Frac(application.GetTimeFromStart()*0.3), 0.6f, 0.6f));, ImGui::PopStyleColor(); );

    if( ImGui::Button( "RUN FLYTHROUGH ('Esc' to stop)", {-1, 0} ) )
    {
        m_miniScript.Start( [ thisPtr = this, &application ] (vaMiniScriptInterface & msi)
        {
            // this sets up some globals and also backups all the sample settings
            AutoBenchTool autobench( *thisPtr, msi, false, false, false );

            // animation stuff
            autobench.SetUIStatusInfo( "Playing flythrough; hit 'F1' to show/hide UI, 'Esc' to stop" );

            vaUIManager::GetInstance( ).SetVisible( false );
            application.SetVsync( true );

            thisPtr->SetFlythroughCameraEnabled( true );
            thisPtr->GetFlythroughCameraController( )->SetPlaySpeed( 1.0f );

            thisPtr->GetFlythroughCameraController( )->SetPlayTime( 0 );


            while( true )
            {
                //autobench.SetUIStatusInfo( status + ", " + vaStringTools::Format( "%.1f%%", (float)testFrame/(float)(c_totalFrameCount-1)*100.0f ) );

                //thisPtr->GetFlythroughCameraController( )->SetPlayTime( testFrame * c_frameDeltaTime );
                if( !msi.YieldExecution( ) || autobench.GetShouldStop() || application.GetInputKeyboard()->IsKeyDown(KK_ESCAPE) )
                {
                    vaUIManager::GetInstance( ).SetVisible( true );
                    return;
                }

                // // totalTime += msi.GetDeltaTime();
                // if( warmupPass )
                //     testFrame += 3;
            }
        } );        
    }

    if( ImGui::Button( "Record flythrough at 30fps ('Esc' to stop)", {-1, 0} ) )
    {
        m_miniScript.Start( [ thisPtr = this, &application ] (vaMiniScriptInterface & msi)
        {
            // this sets up some globals and also backups all the sample settings
            AutoBenchTool autobench( *thisPtr, msi, true, true, false );

            // animation stuff
            autobench.SetUIStatusInfo( "Recording flythrough; hit 'F1' to show/hide UI, 'Esc' to stop" );

            //vaUIManager::GetInstance( ).SetVisible( false );
            application.SetVsync( true );

            const float c_framePerSecond = 30;
            const float c_frameDeltaTime = 1.0f / (float)c_framePerSecond;
            const float c_totalTime = thisPtr->GetFlythroughCameraController( )->GetTotalTime( );
            const int   c_totalFrameCount = (int)( c_totalTime / c_frameDeltaTime );

            thisPtr->SetFlythroughCameraEnabled( true );
            thisPtr->GetFlythroughCameraController( )->SetPlaySpeed( 0.0f );

            // info
            autobench.ReportAddText( "\r\nRecording a flythrough with current settings\r\n" );

            // let temporal stuff stabilize
            for( int frameCounter = 0; frameCounter < 30; frameCounter++ )
            {
                autobench.SetUIStatusInfo( "warmup, " + vaStringTools::Format( "%.1f%%", (float)frameCounter/(float)(30-1)*100.0f ) );

                thisPtr->GetFlythroughCameraController( )->SetPlayTime( 0 );
                if( !msi.YieldExecution( ) || autobench.GetShouldStop() || application.GetInputKeyboard()->IsKeyDown(KK_ESCAPE) ) { vaUIManager::GetInstance( ).SetVisible( true ); return; }
            }


            int width   = thisPtr->CurrentFrameTexture( )->GetWidth( ); // /2;
            int height  = thisPtr->CurrentFrameTexture( )->GetHeight( ); // /2;
            shared_ptr<vaTexture> captureTexture = vaTexture::Create2D( thisPtr->GetRenderDevice(), vaResourceFormat::R8G8B8A8_UNORM_SRGB, width, height, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget );

            for( int frameCounter = 0; frameCounter < c_totalFrameCount; frameCounter++ )
            {
                thisPtr->GetFlythroughCameraController( )->SetPlayTime( frameCounter * c_frameDeltaTime );

                autobench.SetUIStatusInfo( "capturing, " + vaStringTools::Format( "%.1f%%", (float)frameCounter/(float)(c_totalFrameCount-1)*100.0f ) );

                if( !msi.YieldExecution( ) || autobench.GetShouldStop() || application.GetInputKeyboard()->IsKeyDown(KK_ESCAPE) ) { vaUIManager::GetInstance( ).SetVisible( true ); return; }

                if( thisPtr->m_sceneMainView->Settings( ).RenderPath == vaRenderType::PathTracing && thisPtr->m_sceneMainView->PathTracer( ) != nullptr )
                {
                    while( !thisPtr->m_sceneMainView->PathTracer( )->FullyAccumulated() )
                    {
                        if( !msi.YieldExecution( ) || autobench.GetShouldStop() || application.GetInputKeyboard()->IsKeyDown(KK_ESCAPE) ) { vaUIManager::GetInstance( ).SetVisible( true ); return; }
                    }
                }

                wstring folderName = autobench.ReportGetDir( );
                vaFileTools::EnsureDirectoryExists( folderName );

                thisPtr->GetRenderDevice( ).GetMainContext( )->StretchRect( captureTexture, thisPtr->CurrentFrameTexture( ), {0,0,0,0} );

                // thisPtr->CurrentFrameTexture( )->SaveToPNGFile( *thisPtr->GetRenderDevice( ).GetMainContext( ),
                //     folderName + vaStringTools::Format( L"frame_%05d.png", testFrame ) );

                captureTexture->SaveToPNGFile( *thisPtr->GetRenderDevice( ).GetMainContext( ),
                    folderName + vaStringTools::Format( L"frame_%05d.png", frameCounter ) );            
            }

            // for conversion to mpeg one option is to download ffmpeg and then do 
            string conversionInfo = vaStringTools::Format( "To convert to mpeg download ffmpeg and then do 'ffmpeg -r %d -f image2 -s %dx%d -i frame_%%05d.png -vcodec libx264 -crf 13 -pix_fmt yuv420p outputvideo.mp4' ",
                (int)c_framePerSecond, width, height );

            autobench.ReportAddText( "\r\n"+ conversionInfo +"\r\n" ); 

            VA_LOG_SUCCESS( conversionInfo );

        } );        
    }
}

void VanillaSample::ScriptedCameras( vaApplicationBase & application )
{
    application;

    if( m_currentScene != nullptr )
    {
        if( ImGui::CollapsingHeader( "Preset cameras in scene"/*, ImGuiTreeNodeFlags_DefaultOpen*/ ) )
        {
            ImGui::TextWrapped( "Use '0'-'9' keys to set to use preset camera and Ctrl+'0'..'9' to save current camera as a preset" );

            // Display buttons
            for( int i = 0; i < c_presetCameraCount; i++ )
            {
                string id = CamIndexToName( i );
                vaVector4 col = (m_presetCameras[i] == nullptr)?( vaVector4( 0.2f, 0.2f, 0.2f, 0.8f ) ) : ( vaVector4( 0.0f, 0.6f, 0.0f, 0.8f ) );
                if( i == m_presetCameraSelectedIndex ) col = vaVector4( 0.0f, 0.0f, 0.6f, 0.8f );

                ImGui::PushStyleColor( ImGuiCol_Button,         ImFromVA( col ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered,  ImFromVA( col + vaVector4( 0.2f, 0.2f, 0.2f, 0.2f ) ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonActive,   ImFromVA( col + vaVector4( 0.4f, 0.4f, 0.4f, 0.2f ) ) );
                bool clicked = ImGui::Button( id.c_str() );
                ImGui::PopStyleColor( 3 );
                clicked;

                if( clicked )
                {
                    if( m_presetCameraSelectedIndex != i && m_presetCameras[i] != nullptr )
                        m_presetCameraSelectedIndex = i;
                    else
                        m_presetCameraSelectedIndex = -1;
                }
                if( i != c_presetCameraCount-1 && (i%5)!=4 )
                    ImGui::SameLine( );
            }

            // Handle keyboard inputs
            if( !ImGui::GetIO( ).WantCaptureKeyboard && (vaInputKeyboard::GetCurrent( ) != nullptr) ) 
            {
                int numkeyPressed = -1;
                for( int i = 0; i <= 9; i++ )
                    numkeyPressed = ( vaInputKeyboard::GetCurrent( )->IsKeyClicked( (vaKeyboardKeys)('0'+i) ) ) ? ( i ) : ( numkeyPressed );
                if( numkeyPressed >= c_presetCameraCount )
                    numkeyPressed = -1;

                if( vaInputKeyboard::GetCurrent( )->IsKeyDownOrClicked( KK_LCONTROL ) && ( numkeyPressed != -1 ) && m_presetCameraSelectedIndex == -1 )
                {
                    m_presetCameras[numkeyPressed] = std::make_shared<vaMemoryStream>();
                    m_sceneMainView->Camera()->Save( *m_presetCameras[numkeyPressed] );
                }

                if( !vaInputKeyboard::GetCurrent( )->IsKeyDownOrClicked( KK_LCONTROL ) && ( numkeyPressed != -1 ) )
                {
                    if( m_presetCameraSelectedIndex != numkeyPressed && m_presetCameras[numkeyPressed] != nullptr )
                        m_presetCameraSelectedIndex = numkeyPressed;
                    else
                        m_presetCameraSelectedIndex = -1;
                }
            }

            // SAVE TO SCENE
            if( ImGui::Button( "Save changes to current .vaScene file", {-1, 0} ) )
            {
                // first cleanup old presets in the scene, if any
                auto presetsRoot = Scene::FindFirstByName( m_currentScene->Registry(), c_cameraPresetsRootEntityName, entt::null, false );
                if( presetsRoot != entt::null )
                    m_currentScene->DestroyEntity( presetsRoot, true );

                presetsRoot = m_currentScene->CreateEntity( c_cameraPresetsRootEntityName );

                // store presets (not actually using entity transforms)
                for( int i = 0; i < c_presetCameraCount; i++ )
                {
                    if( m_presetCameras[i] == nullptr )
                        continue;
                    string id = CamIndexToName( i );
                    auto cameraEnt = m_currentScene->CreateEntity( id, vaMatrix4x4::Identity, presetsRoot );
                    Scene::RenderCamera & cameraComp = m_currentScene->Registry().emplace<Scene::RenderCamera>( cameraEnt );
                    cameraComp.Data = m_presetCameras[i];
                }

                if( m_currentScene->SaveJSON( m_currentScene->LastJSONFilePath() ) )
                    VA_LOG_SUCCESS( "Saved scene successfully to '%s'", m_currentScene->LastJSONFilePath().c_str() );
                else
                    VA_LOG_WARNING( "Unable to save scene to '%s'", m_currentScene->LastJSONFilePath().c_str() );
            }
        }
    }

}

void VanillaSample::ScriptedTests( vaApplicationBase & application )
{
    application;

// #if defined( VA_GTAO_SAMPLE ) && !defined( VA_SAMPLE_BUILD_FOR_LAB )
//     static bool letsgooutbutavoidunreachablecodewarning = true; if( letsgooutbutavoidunreachablecodewarning ) return;
// #endif

    assert( !m_miniScript.IsActive() );
    if( m_miniScript.IsActive() )
        return;

    bool isDebug = false; isDebug;
#ifdef _DEBUG
    isDebug = true;
#endif

#if !defined( VA_SAMPLE_BUILD_FOR_LAB ) && !defined( VA_SAMPLE_DEMO_BUILD )
    ImGuiTreeNodeFlags headerFlags = 0;
    // headerFlags |= ImGuiTreeNodeFlags_Framed;
    // headerFlags |= ImGuiTreeNodeFlags_DefaultOpen;
    if( !ImGui::CollapsingHeader( "Scripts and stuff", headerFlags ) )
        return;

    ScriptedCameras( application );

    ImGui::Separator( );

    ScriptedGTAOAutoTune( application );

    ImGui::Separator( );
#endif

#if 0
    // for conversion to mpeg one option is to download ffmpeg and then do 'ffmpeg -r 60 -f image2 -s 1920x1080 -i frame_%05d.png -vcodec libx264 -crf 13  -pix_fmt yuv420p outputvideo.mp4'
    if( ImGui::Button( "Record a flythrough (current settings)" ) )
    {
        m_miniScript.Start( [ thisPtr = this ]( vaMiniScriptInterface& msi )
        {
            auto userSettings = thisPtr->Settings( );

            // this sets up some globals and also backups all the sample settings
            AutoBenchTool autobench( *thisPtr, msi, false, true );

            thisPtr->Settings( ) = userSettings;

            // for the flythrough recording, use the user settings

            // animation stuff
            const float c_framePerSecond = 60;
            const float c_frameDeltaTime = 1.0f / (float)c_framePerSecond;
            const float c_totalTime = thisPtr->GetFlythroughCameraController( )->GetTotalTime( );
            const int   c_totalFrameCount = (int)( c_totalTime / c_frameDeltaTime );
            thisPtr->SetFlythroughCameraEnabled( true );
            thisPtr->GetFlythroughCameraController( )->SetPlaySpeed( 0.0f );

            // info
            autobench.ReportAddText( "\r\nRecording a flythrough with current settings\r\n" );

            autobench.ReportAddText( "\r\n" );

            const char* scString[] = { "LOW", "MEDIUM", "HIGH" };
            autobench.ReportAddText( vaStringTools::Format( "\r\nShading complexity: %s\r\n", scString[thisPtr->Settings().ShadingComplexity] ) );

            string status = "running ";

            autobench.SetUIStatusInfo( status + ", preparing..." );

            thisPtr->GetFlythroughCameraController( )->SetPlayTime( 0 );

            int width = thisPtr->CurrentFrameTexture( )->GetWidth( )/2;
            int height = thisPtr->CurrentFrameTexture( )->GetHeight( )/2;
            shared_ptr<vaTexture> lowResTexture = vaTexture::Create2D( thisPtr->GetRenderDevice(), vaResourceFormat::R8G8B8A8_UNORM_SRGB, width, height, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget );

            // wait until IsAllLoadedPrecomputedAndStable and then run 4 more loops - this ensures the numbers from the vaProfiler 
            // are not from previous test case.
            int startupLoops = 3;
            do
            {
                if( !thisPtr->IsAllLoadedPrecomputedAndStable( ) )
                    startupLoops = 3;
                startupLoops--;
                if( !msi.YieldExecutionFor( 1 ) || autobench.GetShouldStop( ) )
                    return;
            } while( startupLoops > 0 );

            bool warmupPass = true;
            for( int loop = 0; loop < 2; loop++, warmupPass = false )
            {
                // ok let's go
                for( int testFrame = 0; testFrame < c_totalFrameCount; testFrame++ )
                {
                    autobench.SetUIStatusInfo( status + ", " + vaStringTools::Format( "%.1f%%", (float)testFrame / (float)( c_totalFrameCount - 1 ) * 100.0f ) );

                    thisPtr->GetFlythroughCameraController( )->SetPlayTime( testFrame * c_frameDeltaTime );
                    if( !msi.YieldExecution( ) || autobench.GetShouldStop( ) )
                        return;

                    if( warmupPass )
                        testFrame += 10;
                    else
                    {
                        wstring folderName = autobench.ReportGetDir( );
                        vaFileTools::EnsureDirectoryExists( folderName );

                        thisPtr->GetRenderDevice( ).GetMainContext( )->StretchRect( lowResTexture, thisPtr->CurrentFrameTexture( ), {0,0,0,0} );

                        // thisPtr->CurrentFrameTexture( )->SaveToPNGFile( *thisPtr->GetRenderDevice( ).GetMainContext( ),
                        //     folderName + vaStringTools::Format( L"frame_%05d.png", testFrame ) );

                        lowResTexture->SaveToPNGFile( *thisPtr->GetRenderDevice( ).GetMainContext( ),
                            folderName + vaStringTools::Format( L"hframe_%05d.png", testFrame ) );
                    }
                }
            }

            autobench.ReportAddText( "\r\n" );
        } );
    }
    // for conversion to mpeg one option is to download ffmpeg and then do 'ffmpeg -r 60 -f image2 -s 1920x1080 -i frame_%05d.png -vcodec libx264 -crf 13  -pix_fmt yuv420p outputvideo.mp4'
    ImGui::Separator( );

#endif

#if defined( VA_SAMPLE_BUILD_FOR_LAB )
    if( isDebug )
        ImGui::Text( "Perf analysis doesn't work in debug builds" );
    else
        ScriptedAutoBench( application );
#endif

//#if defined( VA_SAMPLE_DEMO_BUILD )
    ScriptedDemo( application );
//#endif

}

AutoBenchTool::AutoBenchTool( VanillaSample & parent, vaMiniScriptInterface & scriptInterface, bool ensureVisualDeterminism, bool writeReport, bool divertTracerOutput ) 
    : m_parent( parent ), m_scriptInterface( scriptInterface ), m_backupCameraStorage( (int64)0, (int64)1024 ), m_divertTracerOutput( divertTracerOutput )
{ 
    // must call this so we can call any stuff allowed only on the main thread
    vaThreading::SetSyncedWithMainThread();
    // must call this so we can call any rendering functions that are allowed only on the render thread
    vaRenderDevice::SetSyncedWithRenderThread();

    m_backupSettings                = m_parent.Settings();
    //m_backupDoFSettings             = m_parent.DoFSettings();
    m_parent.Camera()->Save( m_backupCameraStorage ); m_backupCameraStorage.Seek(0);
    m_backupCameraSettings          = m_parent.Camera()->Settings();
    m_backupFlythroughCameraTime    = m_parent.GetFlythroughCameraController( )->GetPlayTime();
    m_backupFlythroughCameraSpeed   = m_parent.GetFlythroughCameraController( )->GetPlaySpeed();
    m_backupFlythroughCameraEnabled = m_parent.GetFlythroughCameraEnabled( );

    m_backupVSync = m_parent.GetApplication().GetVsync( );
    m_parent.GetApplication().SetVsync( false );

    // disable so we can do our own views
    if( m_divertTracerOutput )
        vaTracer::SetTracerViewingUIEnabled( false );

    // display script UI
    m_scriptInterface.SetUICallback( [&statusInfo = m_statusInfo, &shouldStop = m_shouldStop] 
    { 
        ImGui::TextColored( {0.3f, 0.3f, 1.0f, 1.0f}, "Script running, status:" );
        ImGui::Indent();
        ImGui::TextWrapped( statusInfo.c_str() );
        ImGui::Unindent();
        if( ImGui::Button("STOP SCRIPT") )
            shouldStop = true;
        ImGui::Separator();
    } );

    // use default settings
    m_parent.Settings().CameraYFov                      = 55.0f / 180.0f * VA_PIf;
    //m_parent.Settings().DepthPrePass                    = true;
    // m_parent.Settings().DisableTransparencies           = false;
    // m_parent.Settings().VisualizeVRS                    = false;
    // m_parent.Settings().DoFDrivenVRSTransitionOffset    = 0.05f;
    // m_parent.Settings().DoFDrivenVRSMaxRate             = 3;
    // m_parent.Settings().DoFFocalLength                  = 2.0f;
    // m_parent.Settings().DoFRange                        = 0.3f;
    // m_parent.Settings().EnableGradientFilterExtension   = false;


    // initialize report dir and start it
    if( writeReport )
    {
        assert( m_reportDir == L"" );
        assert( m_reportCSV.size() == 0 );

        m_reportDir = vaCore::GetExecutableDirectory();

        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::wstringstream ss;
    #pragma warning ( suppress : 4996 )
        ss << std::put_time(std::localtime(&in_time_t), L"%Y%m%d_%H%M%S");
        m_reportDir += L"AutoBench\\" + ss.str() + L"\\";

        m_reportName = ss.str();

        vaFileTools::DeleteDirectory( m_reportDir );
        vaFileTools::EnsureDirectoryExists( m_reportDir );

        // add system info
        string info = "System info:  " + vaCore::GetCPUIDName() + ", " + m_parent.GetRenderDevice().GetAdapterNameShort( );
        info += "\r\nAPI:  " + m_parent.GetRenderDevice().GetAPIName() + "\r\n";
        ReportAddText( info );

        ReportAddText( vaStringTools::Format("Resolution:   %d x %d\r\n", m_parent.GetApplication().GetWindowClientAreaSize().x, m_parent.GetApplication().GetWindowClientAreaSize().y ) );
        ReportAddText( vaStringTools::Format("Vsync:        ") + ((m_parent.GetApplication().GetSettings().Vsync)?("!!ON!!"):("OFF")) + "\r\n" );

        string fullscreenState;
        switch( m_parent.GetApplication().GetFullscreenState() )
        {
        case ( vaFullscreenState::Windowed ):               fullscreenState = "Windowed"; break;
        case ( vaFullscreenState::Fullscreen ):             fullscreenState = "Fullscreen"; break;
        case ( vaFullscreenState::FullscreenBorderless ):   fullscreenState = "Fullscreen Borderless"; break;
        case ( vaFullscreenState::Unknown ) : 
        default : fullscreenState = "Unknown";
            break;
        }

        ReportAddText( "Fullscreen:   " + fullscreenState + "\r\n" );
        ReportAddText( "" );
    }

    // determinism stuff
    m_parent.SetRequireDeterminism( ensureVisualDeterminism );
    m_parent.Camera()->Settings().ExposureSettings.DefaultAvgLuminanceMinWhenDataNotAvailable = 0.00251505827f;
    m_parent.Camera()->Settings().ExposureSettings.DefaultAvgLuminanceMaxWhenDataNotAvailable = 0.00251505827f;
    m_parent.Camera()->Settings().ExposureSettings.AutoExposureAdaptationSpeed = std::numeric_limits<float>::infinity();
}

AutoBenchTool::~AutoBenchTool( )
{
    m_parent.PresetCameraIndex( ) = -1;

    m_parent.Settings()                                 = m_backupSettings;
    //m_parent.DoFSettings()                              = m_backupDoFSettings;
    m_parent.Camera()->Load( m_backupCameraStorage );
    m_parent.Camera()->Settings()                       = m_backupCameraSettings;
    m_parent.GetFlythroughCameraController( )->SetPlayTime( m_backupFlythroughCameraTime );
    m_parent.GetFlythroughCameraController( )->SetPlaySpeed( m_backupFlythroughCameraSpeed );
    m_parent.SetFlythroughCameraEnabled( m_backupFlythroughCameraEnabled );
    m_parent.SetRequireDeterminism( false );

    if( m_divertTracerOutput )
        vaTracer::SetTracerViewingUIEnabled( true );

    m_scriptInterface.SetUICallback( nullptr );

    m_parent.GetApplication().SetVsync( m_backupVSync );

    // report finish
    if( m_reportDir != L"" && m_reportTXT != "" )
    {
        // {
        //     vaFileStream outFile;
        //     outFile.Open( m_reportDir + m_reportName + L"_info.txt", (false)?(FileCreationMode::Append):(FileCreationMode::Create) );
        //     outFile.WriteTXT( m_reportTXT );
        // }

        FlushRowValues();

        if( !m_shouldStop )
        {
            vaFileStream outFile;
            outFile.Open( m_reportDir + m_reportName + L"_results.csv", (false)?(FileCreationMode::Append):(FileCreationMode::Create) );
            outFile.WriteTXT( m_reportTXT );
            outFile.WriteTXT( "\r\n" );
            VA_LOG( L"Report written to '%s'", m_reportDir.c_str() );
        }
        else
        {
            VA_WARN( L"Script stopped, no report written out!" );
        }
    }
}

void    AutoBenchTool::FlushRowValues( )
{
    for( int i = 0; i < m_reportCSV.size( ); i++ )
    {
        std::vector<string> row = m_reportCSV[i];
        string rowText;
        for( int j = 0; j < row.size( ); j++ )
        {
            rowText += row[j] + ", ";
        }
        m_reportTXT += rowText + "\r\n";
    }
    m_reportCSV.clear();
}

AutoTuneTool::AutoTuneTool( VanillaSample & parent, vaMiniScriptInterface & scriptInterface, int stepsPerSetting, int narrowingPasses ) 
    : m_parent( parent ), m_scriptInterface( scriptInterface ), m_stepsPerSetting( stepsPerSetting ), m_narrowingPasses( narrowingPasses ), m_remainingNarrowingPasses( narrowingPasses )
{                                                               
    // must call this so we can call any stuff allowed only on the main thread
    vaThreading::SetSyncedWithMainThread();
    // must call this so we can call any rendering functions that are allowed only on the render thread
    vaRenderDevice::SetSyncedWithRenderThread();

    m_backupSampleSettings          = m_parent.Settings();
    m_backupMainViewSettings        = m_parent.MainRenderView( )->Settings( );

    // display script UI
    m_scriptInterface.SetUICallback( [&statusInfo = m_statusInfo, &shouldStop = m_shouldStop] 
    { 
        ImGui::TextColored( {0.3f, 0.3f, 1.0f, 1.0f}, "auto-tune running, status:" );
        ImGui::Indent();
        ImGui::TextWrapped( statusInfo.c_str() );
        ImGui::Unindent();
        if( ImGui::Button("STOP SCRIPT") )
            shouldStop = true;
        ImGui::Separator();
    } );

    // use default settings
    m_parent.Settings().CameraYFov                      = 55.0f / 180.0f * VA_PIf;

    // determinism stuff
    m_parent.SetRequireDeterminism( true );
    m_parent.Camera()->Settings().ExposureSettings.DefaultAvgLuminanceMinWhenDataNotAvailable = 0.00251505827f;
    m_parent.Camera()->Settings().ExposureSettings.DefaultAvgLuminanceMaxWhenDataNotAvailable = 0.00251505827f;
    m_parent.Camera()->Settings().ExposureSettings.AutoExposureAdaptationSpeed = std::numeric_limits<float>::infinity();

    m_capturedGroundTruths.resize( m_parent.PresetCameraCount() );
    m_measuredMSEs.resize( m_parent.PresetCameraCount(), 0 );
}

AutoTuneTool::~AutoTuneTool( )
{
    m_parent.PresetCameraIndex( )   = -1;

    m_parent.Settings()                         = m_backupSampleSettings;
    m_parent.MainRenderView( )->Settings( )     = m_backupMainViewSettings;

    //m_parent.DoFSettings()                              = m_backupDoFSettings;
    // m_parent.Camera()->Load( m_backupCameraStorage );
    // m_parent.Camera()->Settings()                       = m_backupCameraSettings;
    m_parent.SetRequireDeterminism( false );

    m_scriptInterface.SetUICallback( nullptr );
}

bool AutoTuneTool::Tick( bool inputsReady, float inputsReadyProgress )
{
    shared_ptr<vaTexture> capturedFrame = inputsReady?(m_parent.CurrentFrameTexture( )):(nullptr);

    vaRenderDeviceContext & renderContext = *m_parent.GetRenderDevice().GetMainContext();

    auto moveToNextCamera = [ &parent = m_parent ]( )
    {
        do 
        {
            parent.PresetCameraIndex()++;
        } while( parent.PresetCameraIndex() < parent.PresetCameraCount( ) && !parent.HasPresetCamera(parent.PresetCameraIndex()) );

        if( parent.PresetCameraIndex( ) >= parent.PresetCameraCount( ) )
        {
            //VA_LOG( "No more preset cameras!" );
            return false;
        }

        //VA_LOG( "Moving to next preset camera: %d", parent.PresetCameraIndex() );
        return true;
    };

    if( m_stage == Stage::NotStarted )
    {
        assert( m_parent.PresetCameraIndex() == -1 );
        if( !moveToNextCamera( ) )
        {
            m_stage = Stage::Finished;
            VA_LOG_ERROR( "No cameras set up? Ending the script!" );
        }
        else
            m_stage = Stage::GTCapture;
        SetUIStatusInfo( "starting" );
    }
    else if( m_stage == Stage::GTCapture )
    {
        if( capturedFrame != nullptr )
        {
            assert( m_parent.PresetCameraCount() != -1 );

            int cameraIndex = m_parent.PresetCameraIndex();
            assert( m_capturedGroundTruths[cameraIndex] == nullptr );
            m_capturedGroundTruths[cameraIndex] = vaTexture::Create2D( m_parent.GetRenderDevice(), vaResourceFormat::R16G16B16A16_FLOAT, capturedFrame->GetWidth(), capturedFrame->GetHeight(), 1, 1, 1, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );

            renderContext.CopySRVToRTV( m_capturedGroundTruths[cameraIndex], capturedFrame );

            if( !moveToNextCamera( ) )
            {
                // all m_capturedGroundTruths[] are captured by now - start next stage
                assert( m_capturedGroundTruths.size() == m_measuredMSEs.size() );
                m_stage = Stage::Search;
                m_parent.PresetCameraIndex() = -1;
                if( !moveToNextCamera( ) )
                    { assert( false ); VA_LOG_ERROR ( "auto-tune error while trying to move to the Search stage" ); m_stage = Stage::Finished; }
                OnStartSearch( );
            }
        }
        SetUIStatusInfo( vaStringTools::Format( "Capturing reference, camera %d of %d, progress: %.1f%%", m_parent.PresetCameraIndex()+1, m_parent.PresetCameraCount(), inputsReadyProgress*100 ) );
    }
    else if( m_stage == Stage::Search )
    {
        if( capturedFrame != nullptr )
        {
            int cameraIndex = m_parent.PresetCameraIndex();

            assert( cameraIndex != -1 && m_parent.HasPresetCamera(cameraIndex) && m_capturedGroundTruths[cameraIndex] != nullptr );

            vaVector4 compRes = m_parent.GetRenderDevice().GetPostProcess().CompareImages( renderContext, m_capturedGroundTruths[cameraIndex], capturedFrame, true );
            m_measuredMSEs[ cameraIndex ] = compRes.x;

            // debug-dump images for validation
#if 0
            m_capturedGroundTruths[cameraIndex]->SaveToDDSFile( renderContext, vaStringTools::Format( L"C:\\temp\\refframe_%05d.dds", cameraIndex ) );
            capturedFrame->SaveToDDSFile( renderContext, vaStringTools::Format( L"C:\\temp\\compframe_%05d_%.1f.dds", cameraIndex, compRes.y ) );
#endif

            SetUIStatusInfo( vaStringTools::Format( "Searching, progress: %.1f%%, remaining passes: %d", m_currentTestStep/float(m_totalTestSteps-1) * 100.0f, m_remainingNarrowingPasses ) );

            if( !moveToNextCamera( ) )
            {
                assert( m_capturedGroundTruths.size() == m_measuredMSEs.size() );
                float averageMSE = 0.0f;
                float totalCount = 0.0f;
                for( size_t i = 0; i < m_measuredMSEs.size( ); i++ )
                {
                    if( m_capturedGroundTruths[i] == nullptr )
                        continue;
                    averageMSE += m_measuredMSEs[i];
                    totalCount += 1;
                }
                averageMSE /= totalCount;

                if( averageMSE < m_bestScoreMSE )
                { 
                    m_bestScoreMSE      = averageMSE;
                    m_bestScoreTestStep = m_currentTestStep;

                    if( m_bestScoreMSE < m_bestTotalScoreMSE )
                    {
                        m_bestTotalScoreMSE = m_bestScoreMSE;
                        m_bestTotalScoreMSEs= m_measuredMSEs;
                        
                        if( m_bestTotalScoreSettings.size() == 0 )
                            m_bestTotalScoreSettings.resize( m_settings.size() );

                        for( int i = 0; i < m_bestTotalScoreSettings.size(); i++ )
                            m_bestTotalScoreSettings[i] = *m_settings[i].Value;
                        VA_LOG( "Found better PSNR (%.2f) with settings: ", vaMath::PSNR( m_bestScoreMSE, 1.0f ), m_bestScoreTestStep );
                        PrintCurrentBestSettings( );
                    }
                }

                m_currentTestStep++;
                if( m_currentTestStep >= m_totalTestSteps )
                {
                    VA_LOG( "\nAuto-tune pass finished, best values found in this pass: " );
                    SetSettings( m_bestScoreTestStep, true, false );
                    m_remainingNarrowingPasses--;
                    if( m_remainingNarrowingPasses == 0 )
                    {
                        m_stage = Stage::Finished;

                        if( m_bestScoreTestStep == -1 )
                        {
                            VA_LOG_ERROR( "\nauto-tune search finished, nothing found, error in setup." );
                        }
                        else
                        {
                            VA_LOG_SUCCESS( "\nAuto-tune search finished! Best combined PSNR: %.2f", vaMath::PSNR( m_bestTotalScoreMSE, 1.0f ) ); // Best combined PSNR (%.2f) found at setting index %d (last pass only)\n", vaMath::PSNR( m_bestScoreMSE, 1.0f ), m_bestScoreTestStep );

                            VA_LOG( "Best found settings: " );
                            PrintCurrentBestSettings( );
                            for( int i = 0; i < m_bestTotalScoreSettings.size(); i++ )
                                *m_settings[i].Value = m_bestTotalScoreSettings[i];

                            VA_LOG_SUCCESS( "Printing individual per-camera best-found PSNRs:" );
                            for( size_t i = 0; i < m_bestTotalScoreMSEs.size( ); i++ )
                            {
                                if( m_capturedGroundTruths[i] == nullptr )
                                    VA_LOG( "  %d : <null>", i );
                                else
                                    VA_LOG( "  %d : %.2f", i, vaMath::PSNR( m_bestTotalScoreMSEs[i], 1.0f ) );
                            }
                        }
                        VA_LOG( "" );
                    }
                    else
                    {
                        // restart, but narrow the ranges:
                        for( size_t i = 0; i < m_settings.size( ); i++ )
                        {
                            float step = (m_settings[i].RangeMax - m_settings[i].RangeMin) / (float)(m_stepsPerSetting-1);
                            m_settings[i].RangeMin = std::max( m_settings[i].RangeMin, vaMath::Lerp( m_settings[i].RangeMin, *m_settings[i].Value-step, 0.6f ) );
                            m_settings[i].RangeMax = std::min( m_settings[i].RangeMax, vaMath::Lerp( m_settings[i].RangeMax, *m_settings[i].Value+step, 0.6f ) );
                        }
                        VA_LOG( "Search ranges narrowed! Resetting the best found scores" );
                        m_bestScoreTestStep = -1;
                        m_bestScoreMSE      = VA_FLOAT_HIGHEST;
                        m_currentTestStep   = 0;
                        SetSettings( m_currentTestStep, true, true );
                        VA_LOG( "Starting another pass... " );
                    }
                }
                
                if( m_stage != Stage::Finished )
                {
                    // start a new loop
                    m_parent.PresetCameraIndex() = -1;
                    if( !moveToNextCamera( ) )
                    { assert( false ); VA_LOG_ERROR ( "Auto-tune error while trying to move to the Search stage" ); m_stage = Stage::Finished; }
                    SetSettings( m_currentTestStep, false, false );
                }
            }
        }
    }

    return m_stage != AutoTuneTool::Stage::Finished && !m_shouldStop;
}

void AutoTuneTool::OnStartSearch( )
{
    assert( m_totalTestSteps == -1 && m_currentTestStep == -1 );
    assert( m_settings.size() > 0 );

    // might become variable in the future
    m_totalTestSteps = m_stepsPerSetting;
    for( size_t i = 1; i < m_settings.size( ); i++ )
        m_totalTestSteps *= m_stepsPerSetting;
    m_currentTestStep = 0;
    SetSettings( m_currentTestStep, false, false );
}

void AutoTuneTool::SetSettings( int stepIndex, bool verbose, bool logRangesOnly )
{
    assert( m_totalTestSteps >= 0 && stepIndex < m_totalTestSteps );
    assert( m_settings.size() > 0 );
    if( m_settings.size() == 0 )
        return;

    for( int i = (int)m_settings.size()-1; i >= 0; i-- )
    {
        int current = stepIndex % m_stepsPerSetting;
        stepIndex /= m_stepsPerSetting;
        
        float lerpK = vaMath::Saturate( current / (float)(m_stepsPerSetting-1) );
        *m_settings[i].Value = vaMath::Lerp( m_settings[i].RangeMin, m_settings[i].RangeMax, lerpK );
    }
    if( verbose )
    {
        VA_LOG( "Printing settings and search ranges for the current pass: " );
        for( size_t i = 0; i < m_settings.size( ); i++ )
        {
            if( !logRangesOnly )
                VA_LOG( "  %s : %.3f (search range from %.3f to %.3f)", m_settings[i].Name.c_str(), *m_settings[i].Value, m_settings[i].RangeMin, m_settings[i].RangeMax );
            else
                VA_LOG( "  %s : (new search range from %.3f to %.3f)", m_settings[i].Name.c_str(), m_settings[i].RangeMin, m_settings[i].RangeMax );
        }
        VA_LOG( "" );
    }
    assert( stepIndex == 0 );
//        m_bestScoreTestStep
//        m_bestScoreMSE     
}

void AutoTuneTool::PrintCurrentBestSettings( )
{
    //VA_LOG( "Printing best found settings (new prototype work): " );
    for( size_t i = 0; i < m_settings.size( ); i++ )
        VA_LOG( "  %s : %.3f", m_settings[i].Name.c_str(), m_bestTotalScoreSettings[i] );
    VA_LOG( "" );
}

void AutoTuneTool::AddSearchSetting( const string & name, float * settingAddr, float rangeMin, float rangeMax )
{
    assert( m_stage == Stage::NotStarted );

    m_settings.push_back( {name, settingAddr, rangeMin, rangeMax} );
}

struct InteractiveBistroContext
{
    vaRenderDevice &                        RenderDevice;       // needed for creating render objects <shrug>
    bool                                    EnableMoveObjs      = true;
    bool                                    EnableSwingLight    = true;
    bool                                    EnableAdvanceTime   = false;
    double                                  AnimTime            = 0;
    entt::entity                            CeilingFan          = entt::null;
    entt::entity                            Spaceship           = entt::null;
    entt::entity                            SpaceshipLL         = entt::null;
    entt::entity                            SpaceshipLR         = entt::null;
    entt::entity                            CeilingLight        = entt::null;
    entt::entity                            StatueLightParent   = entt::null;
    
    entt::entity                            AllLightsParent     = entt::null;

    InteractiveBistroContext( vaRenderDevice & renderDevice ) : RenderDevice(renderDevice)  { }
};

void VanillaSample::InteractiveBistroUI( vaApplicationBase & application )
{
    if( m_interactiveBistroContext == nullptr )
        return;
    InteractiveBistroContext & context = *reinterpret_cast<InteractiveBistroContext*>( m_interactiveBistroContext.get() );

    application;
    int enabledLights = m_sceneRenderer->GetLighting()->GetLastLightCount();
    if( ImGui::CollapsingHeader( ("Interactive Bistro (" + std::to_string(enabledLights) + ")###Interactive Bistro").c_str() ) )
    {
        VA_GENERIC_RAII_SCOPE( ImGui::Indent();, ImGui::Unindent(); );
        ImGui::Checkbox("Anim time advance", &context.EnableAdvanceTime );
        ImGui::InputDouble( "Anim time", &context.AnimTime );
        ImGui::Separator();
        ImGui::Checkbox( "Move some objects", &context.EnableMoveObjs );
        ImGui::Checkbox( "Swing the big light", &context.EnableSwingLight );
        ImGui::Separator();
        if( context.AllLightsParent != entt::null && ImGui::CollapsingHeader( "Light switches" ) )
        {
            ImGui::Text( "Total enabled lights: %d", m_sceneRenderer->GetLighting()->GetLastLightCount() );
            struct SPBI
            {
                string          Name;
                entt::entity    Entity;
                bool            Enabled;
            };
            std::vector<SPBI> SwitchableLights;
            Scene::VisitChildren( m_currentScene->CRegistry( ), context.AllLightsParent, [ & ]( entt::entity child )
            {
                string nameID   = Scene::GetIDString( m_currentScene->CRegistry( ), child );
                string name     = Scene::GetName( m_currentScene->CRegistry( ), child );
                bool switchedOn = !m_currentScene->CRegistry( ).any_of<Scene::DisableLightingRecursiveTag>( child );
                if( ImGui::Checkbox( ( name + "###" + nameID ).c_str( ), &switchedOn ) )
                {
                    if( switchedOn )
                        m_currentScene->Registry( ).remove<Scene::DisableLightingRecursiveTag>( child );
                    else
                        m_currentScene->Registry( ).emplace_or_replace<Scene::DisableLightingRecursiveTag>( child );
                }
            } );
        }
        //ImGui::Checkbox( "Animate 
        //ImGui::Separator();
    }
}

void VanillaSample::InteractiveBistroTick( float deltaTime, bool sceneChanged )
{
    if( sceneChanged )
    {
        m_interactiveBistroContext = nullptr;

        entt::entity ceilingFan = Scene::FindFirstByName( m_currentScene->CRegistry(), "ceiling_fan_1_rotate_pivot", entt::null, true );
        
        if( ceilingFan == entt::null )
            return;

        m_interactiveBistroContext = std::make_shared<InteractiveBistroContext>( GetRenderDevice() );
        InteractiveBistroContext & context = *reinterpret_cast<InteractiveBistroContext*>( m_interactiveBistroContext.get() );

        context.CeilingFan          = ceilingFan;
        context.StatueLightParent   = Scene::FindFirstByName( m_currentScene->CRegistry(), "InteriorStatueLights", entt::null, true );
        context.Spaceship           = Scene::FindFirstByName( m_currentScene->CRegistry(), "SpaceFighter_2_animated", entt::null, true );
        context.SpaceshipLL         = Scene::FindFirstByName( m_currentScene->CRegistry(), "light_left", context.Spaceship, true );
        context.SpaceshipLR         = Scene::FindFirstByName( m_currentScene->CRegistry(), "light_right", context.Spaceship, true );
        context.CeilingLight        = Scene::FindFirstByName( m_currentScene->CRegistry(), "ceiling_lamp_03_rotate_pivot", entt::null, true );
        context.AllLightsParent     = Scene::FindFirstByName( m_currentScene->CRegistry(), "Lights", entt::null, true );

        if( context.StatueLightParent != entt::null )
        {
            Scene::TagDestroyChildren( m_currentScene->Registry(), context.StatueLightParent, true );
            Scene::DestroyTagged(m_currentScene->Registry());

#ifdef VA_GTAO_SAMPLE
            const int       count = 15;
            const float     intensity = 0.1f;
            const float     size   = 0.03f;
#else
            const int       count = 500;
            const float     intensity = 0.02f;
            const float     size   = 0.012f;
#endif
            const float     angleOffset = 0.0f;
            const float     totalSwirls = 4.0f;
            const float     radius = 0.32f;
            const float     height = 1.3f;
            for( int i = 0; i < count; i++ )
            {
                float angle = angleOffset + i/float(count-1)*totalSwirls * 2.0f * VA_PIf;

                vaVector3 pos = { radius * cos(angle), radius * sin(angle), height * i/float(count-1) };
                vaVector3 col = vaColor::HSV2RGB( { i/float(count-1), 0.95f, 1.0f } );
                //vaStringTools::Format("light_%04d", i)   // vaVector3::RandomNormal(rand).ComponentAbs( );
                MakeSphereLight( GetRenderDevice(), *m_currentScene, vaStringTools::Format("l_%04d", i), pos, size, col, intensity, context.StatueLightParent );
            }
        }

        m_currentScene->RegisterSimpleScript( "StringLights", m_interactiveBistroContext, [&context] ( vaScene & scene, const string & typeName, entt::entity entity, struct Scene::SimpleScript & script, float deltaTime, int64 )
        {   deltaTime; script;
            if( typeName != "StringLights" ) { assert( false ); return; }

            if( Scene::FindFirstByName( scene.CRegistry(), "Generated", entity ) != entt::null )
                return; // already generated, bug out!

            entt::entity guidesEntity = Scene::FindFirstByName( scene.CRegistry(), "Guides", entity );
            if( guidesEntity == entt::null )
                return;
            entt::entity genEntity = scene.CreateEntity( "Generated", vaMatrix4x4::Identity, entity );
            scene.Registry().emplace<Scene::SerializationSkipTag>( genEntity );

            typedef std::pair<string, vaVector3> SuspensionPointType;
            std::vector<SuspensionPointType> suspensionPoints;
            Scene::VisitChildren( scene.CRegistry(), guidesEntity, [&]( entt::entity entity )
            { 
                suspensionPoints.push_back( {Scene::GetName( scene.CRegistry(), entity ), Scene::GetWorldTransform( scene.CRegistry(), entity ).GetTranslation()} );
            } );
            std::sort( suspensionPoints.begin( ), suspensionPoints.end( ), [ ]( const SuspensionPointType& left, const SuspensionPointType& right ) { return left.first < right.first; } );

            if( suspensionPoints.size() < 2 )
            { scene.CreateEntity( "Not enough Guides :)", vaMatrix4x4::Identity, genEntity ); return; }

            int totalLights = -1;
            if( sscanf( script.Parameters.c_str(), "%d", &totalLights ) != 1)
                totalLights = -1;
            if( totalLights <= 0 || totalLights > 65535 )
            { scene.CreateEntity( "Error parsing params", vaMatrix4x4::Identity, genEntity ); return; }

#ifdef VA_GTAO_SAMPLE
            totalLights /= 10;
            const float lightIntensity = 0.015f;
            const float lightSize   = 0.035f;
#else
            const float lightIntensity = 0.0015f;
            const float lightSize   = 0.025f;
#endif

            const float ropeSlack = 0.15f;
            int remainingLights = totalLights;
            int createdLights = 0;

            const float colorLoops  = 5.0f;

            for( int i = 0; i < suspensionPoints.size( )-1; i++ )
            {
                int segmentLights = (int)(remainingLights / (float)(suspensionPoints.size( )-1-i) + 0.5f);
                vaVector3 posFrom = suspensionPoints[i].second;
                vaVector3 posTo = suspensionPoints[i+1].second;
                vaVector3 lightPosPrev;
                for( int j = 0; j < segmentLights; j++ )
                {
                    vaVector3 lightPos = vaVector3::Lerp( posFrom, posTo, (j + 0.5f) / (float)(segmentLights) );
                    vaVector3 lightCol = vaColor::HSV2RGB( { vaMath::Frac( createdLights/float(totalLights-1) * colorLoops ), 1.0f, 1.0f } );
                    lightPos.z -= (posTo-posFrom).Length() * ropeSlack * std::sinf( (j + 0.5f) / (float)(segmentLights) * VA_PIf );
                    //vaStringTools::Format("light_%04d", i)   // vaVector3::RandomNormal(rand).ComponentAbs( );
                    MakeSphereLight( context.RenderDevice, scene, vaStringTools::Format("l_%02di_%04d", i,j), lightPos, lightSize, lightCol, lightIntensity, genEntity );
                    if( j == 0 )                MakeConnectingRod( context.RenderDevice, scene, "rb", posFrom, lightPos, lightSize * 0.15f, genEntity );
                    if( j > 0 )                 MakeConnectingRod( context.RenderDevice, scene, "rc", lightPosPrev, lightPos, lightSize * 0.15f, genEntity );
                    if( j == segmentLights-1 )  MakeConnectingRod( context.RenderDevice, scene, "re", lightPos, posTo, lightSize * 0.15f, genEntity );
                    lightPosPrev = lightPos;
                    createdLights++;
                    remainingLights--;
                }
            }
        }
        );

    }

    if( m_interactiveBistroContext == nullptr )
        return;

    InteractiveBistroContext & context = *reinterpret_cast<InteractiveBistroContext*>( m_interactiveBistroContext.get() );

    if( context.EnableAdvanceTime )
        context.AnimTime += deltaTime;

    entt::registry & registry = m_currentScene->Registry( );

    if( context.EnableMoveObjs )
    {
        if( context.CeilingFan != entt::null )
        {
            Scene::TransformLocal * trans = registry.try_get<Scene::TransformLocal>( context.CeilingFan );
            if( trans != nullptr ) 
            {
                *trans = vaMatrix4x4::RotationZ( (float)vaMath::Frac(context.AnimTime*0.1f) * VA_PIf * 2.0f );
                Scene::SetTransformDirtyRecursive( registry, context.CeilingFan );
            }
        }
        if( context.Spaceship != entt::null )
        {
            Scene::TransformLocal * trans = registry.try_get<Scene::TransformLocal>( context.Spaceship );
            if( trans != nullptr ) 
            {
                const float stage1 = 6.0f;
                const float stage2 = 8.0f;
                const float stage3 = 14.0f;
                float animTime = (float)fmod( context.AnimTime, stage3 );
                if( animTime < stage1 )
                {
                    float localTime = (animTime)/stage1;
                    *trans = vaMatrix4x4::Translation( {0, -1500.0f * localTime, 0.0f } );
                } else if( animTime < stage2 )
                { 
                    float localTime = (animTime-stage1) / (stage2-stage1);
                    *trans = vaMatrix4x4::RotationZ( localTime * VA_PIf ) * vaMatrix4x4::Translation( vaMath::Lerp( vaVector3( 0, -1500.0f, 0.0f ), vaVector3( 230, -1500.0f, -180 ), localTime ) );
                }else if( animTime < stage3 )
                { 
                    float localTime = (animTime-stage2) / (stage3-stage2);
                    *trans = vaMatrix4x4::RotationZ( 1.0 * VA_PIf ) * vaMatrix4x4::Translation( vaMath::Lerp( vaVector3( 230, -1500.0f, -180.0f ), vaVector3( 230, 0, -180.0f ), localTime ) );
                }

                Scene::SetTransformDirtyRecursive( registry, context.Spaceship );
            }
        }
        if( context.SpaceshipLL != entt::null && context.SpaceshipLR != entt::null )
        {
            Scene::LightPoint * lightL = registry.try_get<Scene::LightPoint>( context.SpaceshipLL );
            Scene::LightPoint * lightR = registry.try_get<Scene::LightPoint>( context.SpaceshipLR );
            if( lightL != nullptr && lightR != nullptr )
            {
                lightL->FadeFactor = (fmod( context.AnimTime, 1.0 ) > 0.8f)?(1.0f):(0.0f);
                lightR->FadeFactor = (fmod( context.AnimTime, 1.0 ) > 0.8f)?(1.0f):(0.0f);
            }
        }
    }
    if( context.EnableSwingLight && context.CeilingLight != entt::null )
    {
        Scene::TransformLocal * trans = registry.try_get<Scene::TransformLocal>( context.CeilingLight );
        if( trans != nullptr ) 
        {
            *trans = vaMatrix4x4::RotationAxis( vaVector3( 0.5f, 0.7f, 0.0f ).Normalized(), sin( (float)vaMath::Frac(context.AnimTime*0.15f) * VA_PIf * 2.0f ) * VA_PIf * 0.4f );
            Scene::SetTransformDirtyRecursive( registry, context.CeilingLight );
        }
    }
}


#define WORKSPACE( name )                                                                                                               \
    void name( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState );  \
    g_workspaces[g_workspaceCount++] = pair<string, vaApplicationLoopFunction>( #name, name );                                          \
    assert( g_workspaceCount < countof(g_workspaces) );

void InitWorkspaces( )
{
#ifndef VA_GTAO_SAMPLE
    WORKSPACE( Sample00_BlueScreen );
    WORKSPACE( Sample01_FullscreenPass );
    WORKSPACE( Sample02_JustATriangle );
    WORKSPACE( Sample03_TexturedTriangle );
    WORKSPACE( Sample04_ConstantBuffer );
    WORKSPACE( Sample05_RenderToTexture );
    WORKSPACE( Sample06_RenderToTextureCS );
    WORKSPACE( Sample07_TextureUpload );
    WORKSPACE( Sample08_TextureDownload );
    WORKSPACE( Sample09_SavingScreenshot );
    WORKSPACE( Sample10_Skybox );
    WORKSPACE( Sample11_Basic3DMesh );
    WORKSPACE( Sample12_PostProcess );
    WORKSPACE( Sample13_Tonemap );
    WORKSPACE( Sample14_SSAO );
    WORKSPACE( Sample15_BasicScene );
    WORKSPACE( Sample16_Particles );
    WORKSPACE( Sample17_PoissonDiskGenerator );
    WORKSPACE( Sample18_Burley2020Scrambling );

    //WORKSPACE( Workspace00_PBR );
    WORKSPACE( Workspace00_Scene );
    WORKSPACE( Workspace01_Asteroids );
#endif

    WORKSPACE( VanillaScene );
#ifndef VA_GTAO_SAMPLE
    WORKSPACE( VanillaAssetImporter );
#endif
};

