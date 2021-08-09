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

#include "Scene/vaCameraControllers.h"

#include "Rendering/vaRenderDevice.h"
#include "Rendering/Misc/vaZoomTool.h"
#include "Rendering/Misc/vaImageCompareTool.h"

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

#pragma warning ( suppress: 4505 ) // unreferenced local function has been removed
static void AddLumberyardTestLights( vaScene & scene, const vaGUID & unitSphereMeshID )
{
    scene; unitSphereMeshID;
#ifdef LIGHT_CUTS_EXPERIMENTATION

    std::vector<vaVector3> list;
    
    list.push_back( vaVector3( 10.716f, -0.433f, 3.553f ) );
    list.push_back( vaVector3( 10.405f, -0.597f, 3.532f ) );
    list.push_back( vaVector3( 10.286f, -0.660f, 3.524f ) );
    list.push_back( vaVector3( 10.166f, -0.724f, 3.516f ) );
    list.push_back( vaVector3( 10.086f, -0.766f, 3.511f ) );
    list.push_back( vaVector3( 9.968f, -0.829f, 3.503f ) );
    list.push_back( vaVector3( 9.851f, -0.891f, 3.495f ) );
    list.push_back( vaVector3( 9.770f, -0.934f, 3.489f ) );
    list.push_back( vaVector3( 9.654f, -0.995f, 3.482f ) );
    list.push_back( vaVector3( 9.538f, -1.057f, 3.474f ) );
    list.push_back( vaVector3( 9.458f, -1.099f, 3.469f ) );
    list.push_back( vaVector3( 9.342f, -1.161f, 3.461f ) );
    list.push_back( vaVector3( 9.227f, -1.221f, 3.453f ) );
    list.push_back( vaVector3( 9.113f, -1.282f, 3.445f ) );
    list.push_back( vaVector3( 9.001f, -1.341f, 3.438f ) );
    list.push_back( vaVector3( 8.888f, -1.401f, 3.430f ) );
    list.push_back( vaVector3( 8.811f, -1.442f, 3.425f ) );
    list.push_back( vaVector3( 8.699f, -1.501f, 3.418f ) );
    list.push_back( vaVector3( 8.585f, -1.561f, 3.410f ) );
    list.push_back( vaVector3( 8.471f, -1.622f, 3.402f ) );
    list.push_back( vaVector3( 8.360f, -1.681f, 3.395f ) );
    list.push_back( vaVector3( 8.249f, -1.740f, 3.388f ) );
    list.push_back( vaVector3( 8.104f, -1.816f, 3.378f ) );
    list.push_back( vaVector3( 8.029f, -1.856f, 3.373f ) );
    list.push_back( vaVector3( 7.919f, -1.915f, 3.365f ) );
    list.push_back( vaVector3( 7.807f, -1.974f, 3.358f ) );
    list.push_back( vaVector3( 7.699f, -2.031f, 3.351f ) );
    list.push_back( vaVector3( 7.591f, -2.089f, 3.343f ) );
    list.push_back( vaVector3( 7.483f, -2.145f, 3.336f ) );
    list.push_back( vaVector3( 7.376f, -2.202f, 3.329f ) );
    list.push_back( vaVector3( 7.270f, -2.258f, 3.322f ) );
    list.push_back( vaVector3( 7.131f, -2.332f, 3.313f ) );
    list.push_back( vaVector3( 7.026f, -2.388f, 3.306f ) );
    list.push_back( vaVector3( 6.919f, -2.444f, 3.298f ) );
    list.push_back( vaVector3( 6.815f, -2.500f, 3.291f ) );
    list.push_back( vaVector3( 6.710f, -2.555f, 3.284f ) );
    list.push_back( vaVector3( 6.604f, -2.611f, 3.277f ) );
    list.push_back( vaVector3( 6.434f, -2.701f, 3.266f ) );
    list.push_back( vaVector3( 6.363f, -2.739f, 3.261f ) );
    list.push_back( vaVector3( 6.292f, -2.777f, 3.256f ) );
    list.push_back( vaVector3( 6.222f, -2.814f, 3.252f ) );
    list.push_back( vaVector3( 6.153f, -2.850f, 3.247f ) );
    list.push_back( vaVector3( 6.052f, -2.904f, 3.240f ) );
    list.push_back( vaVector3( 6.004f, -3.061f, 3.233f ) );
    list.push_back( vaVector3( 5.957f, -3.214f, 3.227f ) );
    list.push_back( vaVector3( 5.910f, -3.367f, 3.220f ) );
    list.push_back( vaVector3( 5.863f, -3.521f, 3.213f ) );
    list.push_back( vaVector3( 5.815f, -3.676f, 3.207f ) );
    list.push_back( vaVector3( 5.768f, -3.830f, 3.200f ) );
    list.push_back( vaVector3( 5.705f, -3.950f, 3.193f ) );
    list.push_back( vaVector3( 5.567f, -3.938f, 3.186f ) );
    list.push_back( vaVector3( 5.415f, -3.891f, 3.180f ) );
    list.push_back( vaVector3( 5.261f, -3.843f, 3.173f ) );
    list.push_back( vaVector3( 5.110f, -3.797f, 3.166f ) );
    list.push_back( vaVector3( 4.908f, -3.734f, 3.157f ) );
    list.push_back( vaVector3( 4.756f, -3.687f, 3.151f ) );
    list.push_back( vaVector3( 4.638f, -3.704f, 3.144f ) );
    list.push_back( vaVector3( 4.554f, -3.789f, 3.137f ) );
    list.push_back( vaVector3( 4.492f, -3.991f, 3.129f ) );
    list.push_back( vaVector3( 4.394f, -4.043f, 3.122f ) );
    list.push_back( vaVector3( 4.295f, -4.096f, 3.115f ) );
    list.push_back( vaVector3( 4.234f, -4.293f, 3.107f ) );
    list.push_back( vaVector3( 4.188f, -4.443f, 3.100f ) );
    list.push_back( vaVector3( 4.142f, -4.594f, 3.094f ) );
    list.push_back( vaVector3( 4.097f, -4.743f, 3.087f ) );
    list.push_back( vaVector3( 4.065f, -4.847f, 3.083f ) );
    list.push_back( vaVector3( 4.019f, -4.997f, 3.076f ) );
    list.push_back( vaVector3( 3.974f, -5.144f, 3.069f ) );
    list.push_back( vaVector3( 3.928f, -5.294f, 3.063f ) );
    list.push_back( vaVector3( 3.882f, -5.443f, 3.056f ) );
    list.push_back( vaVector3( 3.837f, -5.590f, 3.050f ) );
    list.push_back( vaVector3( 3.778f, -5.782f, 3.042f ) );
    list.push_back( vaVector3( 3.734f, -5.927f, 3.035f ) );
    list.push_back( vaVector3( 3.689f, -6.072f, 3.029f ) );
    list.push_back( vaVector3( 3.630f, -6.265f, 3.020f ) );
    list.push_back( vaVector3( 3.587f, -6.407f, 3.014f ) );
    list.push_back( vaVector3( 3.543f, -6.550f, 3.008f ) );
    list.push_back( vaVector3( 3.439f, -6.646f, 3.000f ) );
    list.push_back( vaVector3( 3.254f, -6.589f, 2.992f ) );
    list.push_back( vaVector3( 3.115f, -6.546f, 2.986f ) );
    list.push_back( vaVector3( 2.929f, -6.489f, 2.978f ) );
    list.push_back( vaVector3( 2.801f, -6.400f, 2.973f ) );
    list.push_back( vaVector3( 2.752f, -6.307f, 2.973f ) );
    list.push_back( vaVector3( 2.687f, -6.184f, 2.973f ) );
    list.push_back( vaVector3( 2.638f, -6.092f, 2.973f ) );
    list.push_back( vaVector3( 2.589f, -6.000f, 2.973f ) );
    list.push_back( vaVector3( 2.525f, -5.879f, 2.973f ) );
    list.push_back( vaVector3( 2.476f, -5.787f, 2.973f ) );
    list.push_back( vaVector3( 2.427f, -5.694f, 2.973f ) );
    list.push_back( vaVector3( 2.363f, -5.573f, 2.973f ) );
    list.push_back( vaVector3( 2.313f, -5.478f, 2.973f ) );
    list.push_back( vaVector3( 2.261f, -5.380f, 2.973f ) );
    list.push_back( vaVector3( 2.195f, -5.257f, 2.973f ) );
    list.push_back( vaVector3( 2.146f, -5.163f, 2.973f ) );
    list.push_back( vaVector3( 2.096f, -5.069f, 2.973f ) );
    list.push_back( vaVector3( 2.046f, -4.975f, 2.973f ) );
    list.push_back( vaVector3( 1.982f, -4.854f, 2.973f ) );
    list.push_back( vaVector3( 1.933f, -4.761f, 2.973f ) );
    list.push_back( vaVector3( 1.868f, -4.638f, 2.973f ) );
    list.push_back( vaVector3( 1.818f, -4.545f, 2.973f ) );
    list.push_back( vaVector3( 1.753f, -4.423f, 2.973f ) );
    list.push_back( vaVector3( 1.705f, -4.331f, 2.973f ) );
    list.push_back( vaVector3( 1.655f, -4.236f, 2.973f ) );
    list.push_back( vaVector3( 1.590f, -4.114f, 2.973f ) );
    list.push_back( vaVector3( 1.532f, -3.987f, 2.835f ) );
    list.push_back( vaVector3( 1.475f, -3.862f, 2.697f ) );
    list.push_back( vaVector3( 1.417f, -3.735f, 2.558f ) );
    list.push_back( vaVector3( 1.374f, -3.640f, 2.454f ) );
    list.push_back( vaVector3( 1.316f, -3.514f, 2.316f ) );
    list.push_back( vaVector3( 1.254f, -3.388f, 2.244f ) );
    list.push_back( vaVector3( 1.205f, -3.295f, 2.244f ) );
    list.push_back( vaVector3( 1.139f, -3.171f, 2.244f ) );
    list.push_back( vaVector3( 1.090f, -3.079f, 2.244f ) );
    list.push_back( vaVector3( 1.025f, -2.956f, 2.244f ) );
    list.push_back( vaVector3( 0.975f, -2.861f, 2.244f ) );
    list.push_back( vaVector3( 0.910f, -2.739f, 2.244f ) );
    list.push_back( vaVector3( 0.861f, -2.646f, 2.244f ) );
    list.push_back( vaVector3( 0.796f, -2.523f, 2.244f ) );
    list.push_back( vaVector3( 0.729f, -2.401f, 2.276f ) );
    list.push_back( vaVector3( 0.679f, -2.307f, 2.276f ) );
    list.push_back( vaVector3( 0.614f, -2.184f, 2.276f ) );
    list.push_back( vaVector3( 0.549f, -2.061f, 2.276f ) );
    list.push_back( vaVector3( 0.485f, -1.942f, 2.276f ) );
    list.push_back( vaVector3( 0.436f, -1.848f, 2.276f ) );
    list.push_back( vaVector3( 0.386f, -1.755f, 2.276f ) );
    list.push_back( vaVector3( 0.322f, -1.633f, 2.276f ) );
    list.push_back( vaVector3( 0.258f, -1.513f, 2.276f ) );
    list.push_back( vaVector3( 0.209f, -1.420f, 2.276f ) );
    list.push_back( vaVector3( 0.159f, -1.326f, 2.276f ) );
    list.push_back( vaVector3( -2.971f, -17.451f, 5.401f ) );
    list.push_back( vaVector3( -3.016f, -17.394f, 5.402f ) );
    list.push_back( vaVector3( -3.042f, -17.361f, 5.403f ) );
    list.push_back( vaVector3( -3.083f, -17.309f, 5.404f ) );
    list.push_back( vaVector3( -3.126f, -17.254f, 5.405f ) );
    list.push_back( vaVector3( -3.174f, -17.193f, 5.407f ) );
    list.push_back( vaVector3( -3.224f, -17.129f, 5.408f ) );
    list.push_back( vaVector3( -3.277f, -17.061f, 5.410f ) );
    list.push_back( vaVector3( -3.335f, -16.988f, 5.411f ) );
    list.push_back( vaVector3( -3.395f, -16.911f, 5.413f ) );
    list.push_back( vaVector3( -3.458f, -16.830f, 5.415f ) );
    list.push_back( vaVector3( -3.525f, -16.745f, 5.417f ) );
    list.push_back( vaVector3( -3.592f, -16.660f, 5.419f ) );
    list.push_back( vaVector3( -3.657f, -16.577f, 5.420f ) );
    list.push_back( vaVector3( -3.722f, -16.494f, 5.422f ) );
    list.push_back( vaVector3( -3.807f, -16.385f, 5.425f ) );
    list.push_back( vaVector3( -3.870f, -16.304f, 5.426f ) );
    list.push_back( vaVector3( -3.935f, -16.223f, 5.428f ) );
    list.push_back( vaVector3( -3.999f, -16.140f, 5.430f ) );
    list.push_back( vaVector3( -4.084f, -16.032f, 5.432f ) );
    list.push_back( vaVector3( -4.147f, -15.952f, 5.434f ) );
    list.push_back( vaVector3( -4.210f, -15.871f, 5.436f ) );
    list.push_back( vaVector3( -4.276f, -15.786f, 5.438f ) );
    list.push_back( vaVector3( -4.339f, -15.706f, 5.440f ) );
    list.push_back( vaVector3( -4.403f, -15.625f, 5.441f ) );
    list.push_back( vaVector3( -4.528f, -15.465f, 5.445f ) );
    list.push_back( vaVector3( -4.594f, -15.382f, 5.447f ) );
    list.push_back( vaVector3( -4.657f, -15.300f, 5.449f ) );
    list.push_back( vaVector3( -4.722f, -15.218f, 5.450f ) );
    list.push_back( vaVector3( -4.785f, -15.137f, 5.452f ) );
    list.push_back( vaVector3( -4.869f, -15.030f, 5.455f ) );
    list.push_back( vaVector3( -4.932f, -14.950f, 5.456f ) );
    list.push_back( vaVector3( -4.995f, -14.869f, 5.458f ) );
    list.push_back( vaVector3( -5.078f, -14.764f, 5.461f ) );
    list.push_back( vaVector3( -5.140f, -14.684f, 5.462f ) );
    list.push_back( vaVector3( -5.202f, -14.605f, 5.464f ) );
    list.push_back( vaVector3( -5.342f, -14.427f, 5.468f ) );
    list.push_back( vaVector3( -5.403f, -14.349f, 5.470f ) );
    list.push_back( vaVector3( -5.484f, -14.246f, 5.472f ) );
    list.push_back( vaVector3( -5.547f, -14.166f, 5.474f ) );
    list.push_back( vaVector3( -5.627f, -14.063f, 5.476f ) );
    list.push_back( vaVector3( -5.706f, -13.962f, 5.478f ) );
    list.push_back( vaVector3( -5.788f, -13.859f, 5.481f ) );
    list.push_back( vaVector3( -5.906f, -13.708f, 5.484f ) );
    list.push_back( vaVector3( -5.986f, -13.605f, 5.486f ) );
    list.push_back( vaVector3( -6.046f, -13.529f, 5.488f ) );
    list.push_back( vaVector3( -6.126f, -13.427f, 5.490f ) );
    list.push_back( vaVector3( -6.205f, -13.326f, 5.492f ) );
    list.push_back( vaVector3( -6.284f, -13.225f, 5.495f ) );
    list.push_back( vaVector3( -6.362f, -13.126f, 5.497f ) );
    list.push_back( vaVector3( -6.422f, -13.050f, 5.498f ) );
    list.push_back( vaVector3( -6.500f, -12.950f, 5.501f ) );
    list.push_back( vaVector3( -6.577f, -12.851f, 5.503f ) );
    list.push_back( vaVector3( -6.655f, -12.752f, 5.505f ) );
    list.push_back( vaVector3( -6.732f, -12.654f, 5.507f ) );
    list.push_back( vaVector3( -6.790f, -12.579f, 5.509f ) );
    list.push_back( vaVector3( -6.866f, -12.482f, 5.511f ) );
    list.push_back( vaVector3( -6.943f, -12.384f, 5.513f ) );
    list.push_back( vaVector3( -7.021f, -12.285f, 5.515f ) );
    list.push_back( vaVector3( -7.080f, -12.209f, 5.517f ) );
    list.push_back( vaVector3( -7.160f, -12.108f, 5.519f ) );
    list.push_back( vaVector3( -7.219f, -12.033f, 5.521f ) );
    list.push_back( vaVector3( -7.295f, -11.935f, 5.523f ) );
    list.push_back( vaVector3( -7.353f, -11.862f, 5.525f ) );
    list.push_back( vaVector3( -7.428f, -11.766f, 5.527f ) );
    list.push_back( vaVector3( -7.487f, -11.691f, 5.528f ) );
    list.push_back( vaVector3( -7.564f, -11.593f, 5.531f ) );
    list.push_back( vaVector3( -7.639f, -11.496f, 5.533f ) );
    list.push_back( vaVector3( -7.716f, -11.399f, 5.535f ) );
    list.push_back( vaVector3( -7.774f, -11.325f, 5.536f ) );
    list.push_back( vaVector3( -7.849f, -11.229f, 5.539f ) );
    list.push_back( vaVector3( -7.923f, -11.134f, 5.541f ) );
    list.push_back( vaVector3( -7.981f, -11.061f, 5.542f ) );
    list.push_back( vaVector3( -8.056f, -10.965f, 5.544f ) );
    list.push_back( vaVector3( -8.129f, -10.872f, 5.547f ) );
    list.push_back( vaVector3( -8.203f, -10.778f, 5.549f ) );
    list.push_back( vaVector3( -8.277f, -10.683f, 5.551f ) );
    list.push_back( vaVector3( -8.350f, -10.590f, 5.553f ) );
    list.push_back( vaVector3( -8.424f, -10.496f, 5.555f ) );
    list.push_back( vaVector3( -8.496f, -10.404f, 5.557f ) );
    list.push_back( vaVector3( -8.569f, -10.311f, 5.559f ) );
    list.push_back( vaVector3( -8.642f, -10.218f, 5.561f ) );
    list.push_back( vaVector3( -8.714f, -10.125f, 5.563f ) );
    list.push_back( vaVector3( -8.804f, -10.010f, 5.566f ) );
    list.push_back( vaVector3( -8.877f, -9.917f, 5.568f ) );
    list.push_back( vaVector3( -8.951f, -9.823f, 5.570f ) );
    list.push_back( vaVector3( -9.025f, -9.728f, 5.572f ) );
    list.push_back( vaVector3( -9.097f, -9.636f, 5.574f ) );
    list.push_back( vaVector3( -9.171f, -9.542f, 5.576f ) );
    list.push_back( vaVector3( -9.247f, -9.446f, 5.578f ) );
    list.push_back( vaVector3( -9.321f, -9.351f, 5.580f ) );
    list.push_back( vaVector3( -9.393f, -9.259f, 5.582f ) );
    list.push_back( vaVector3( -9.465f, -9.167f, 5.584f ) );
    list.push_back( vaVector3( -9.555f, -9.053f, 5.587f ) );
    list.push_back( vaVector3( -9.628f, -8.960f, 5.589f ) );
    list.push_back( vaVector3( -9.700f, -8.868f, 5.591f ) );
    list.push_back( vaVector3( -9.788f, -8.755f, 5.593f ) );
    list.push_back( vaVector3( -9.877f, -8.642f, 5.596f ) );
    list.push_back( vaVector3( -9.948f, -8.551f, 5.570f ) );
    list.push_back( vaVector3( -10.039f, -8.435f, 5.427f ) );
    list.push_back( vaVector3( -10.131f, -8.318f, 5.284f ) );
    list.push_back( vaVector3( -10.203f, -8.226f, 5.171f ) );
    list.push_back( vaVector3( -10.277f, -8.131f, 5.055f ) );
    list.push_back( vaVector3( -10.350f, -8.039f, 4.941f ) );
    list.push_back( vaVector3( -10.405f, -7.968f, 4.855f ) );
    list.push_back( vaVector3( -10.478f, -7.875f, 4.740f ) );
    list.push_back( vaVector3( -10.551f, -7.783f, 4.627f ) );
    list.push_back( vaVector3( -10.622f, -7.691f, 4.514f ) );
    list.push_back( vaVector3( -10.695f, -7.598f, 4.400f ) );
    list.push_back( vaVector3( -10.770f, -7.503f, 4.284f ) );
    list.push_back( vaVector3( -10.842f, -7.411f, 4.171f ) );
    list.push_back( vaVector3( -10.932f, -7.295f, 4.029f ) );
    list.push_back( vaVector3( -11.005f, -7.203f, 3.915f ) );
    list.push_back( vaVector3( -11.078f, -7.109f, 3.800f ) );
    list.push_back( vaVector3( -11.167f, -6.996f, 3.662f ) );
    list.push_back( vaVector3( -11.240f, -6.903f, 3.547f ) );
    list.push_back( vaVector3( -11.312f, -6.812f, 3.436f ) );
    list.push_back( vaVector3( -11.400f, -6.698f, 3.296f ) );
    list.push_back( vaVector3( -11.472f, -6.607f, 3.184f ) );
    list.push_back( vaVector3( -11.563f, -6.492f, 3.043f ) );
    list.push_back( vaVector3( -11.635f, -6.399f, 2.930f ) );
    list.push_back( vaVector3( -11.724f, -6.286f, 2.791f ) );
    list.push_back( vaVector3( -11.795f, -6.196f, 2.679f ) );
    list.push_back( vaVector3( -11.882f, -6.084f, 2.543f ) );
    list.push_back( vaVector3( -11.968f, -5.974f, 2.408f ) );
    list.push_back( vaVector3( -12.056f, -5.862f, 2.270f ) );
    list.push_back( vaVector3( -12.142f, -5.753f, 2.215f ) );
    list.push_back( vaVector3( -12.210f, -5.665f, 2.217f ) );
    list.push_back( vaVector3( -12.296f, -5.556f, 2.219f ) );
    list.push_back( vaVector3( -12.364f, -5.469f, 2.221f ) );
    list.push_back( vaVector3( -12.434f, -5.380f, 2.223f ) );
    list.push_back( vaVector3( -12.502f, -5.293f, 2.225f ) );
    list.push_back( vaVector3( -12.570f, -5.207f, 2.227f ) );
    list.push_back( vaVector3( -12.667f, -5.039f, 2.231f ) );
    list.push_back( vaVector3( -12.652f, -4.919f, 2.232f ) );
    list.push_back( vaVector3( -12.638f, -4.801f, 2.233f ) );
    list.push_back( vaVector3( -12.614f, -4.607f, 2.236f ) );
    list.push_back( vaVector3( -12.596f, -4.453f, 2.238f ) );
    list.push_back( vaVector3( -12.577f, -4.296f, 2.240f ) );
    list.push_back( vaVector3( -12.558f, -4.140f, 2.242f ) );
    list.push_back( vaVector3( -12.535f, -3.949f, 2.244f ) );
    list.push_back( vaVector3( -12.516f, -3.795f, 2.246f ) );
    list.push_back( vaVector3( -12.497f, -3.640f, 2.248f ) );
    list.push_back( vaVector3( -12.478f, -3.485f, 2.250f ) );
    list.push_back( vaVector3( -12.404f, -3.354f, 2.251f ) );
    list.push_back( vaVector3( -12.318f, -3.286f, 2.251f ) );
    list.push_back( vaVector3( -12.228f, -3.216f, 2.251f ) );
    list.push_back( vaVector3( -12.137f, -3.145f, 2.251f ) );
    list.push_back( vaVector3( -12.047f, -3.074f, 2.251f ) );
    list.push_back( vaVector3( -11.940f, -2.990f, 2.251f ) );
    list.push_back( vaVector3( -11.854f, -2.923f, 2.251f ) );
    list.push_back( vaVector3( -11.788f, -2.871f, 2.251f ) );
    list.push_back( vaVector3( -11.681f, -2.787f, 2.251f ) );
    list.push_back( vaVector3( -11.594f, -2.719f, 2.251f ) );
    list.push_back( vaVector3( -11.506f, -2.650f, 2.251f ) );
    list.push_back( vaVector3( -11.418f, -2.581f, 2.251f ) );
    list.push_back( vaVector3( -11.311f, -2.497f, 2.251f ) );
    list.push_back( vaVector3( -11.244f, -2.444f, 2.251f ) );
    list.push_back( vaVector3( -11.176f, -2.391f, 2.251f ) );
    list.push_back( vaVector3( -11.161f, -2.273f, 2.252f ) );
    list.push_back( vaVector3( -11.142f, -2.114f, 2.254f ) );
    list.push_back( vaVector3( -11.123f, -1.956f, 2.256f ) );
    list.push_back( vaVector3( -11.109f, -1.837f, 2.258f ) );
    list.push_back( vaVector3( -11.090f, -1.679f, 2.260f ) );
    list.push_back( vaVector3( -11.066f, -1.484f, 2.262f ) );
    list.push_back( vaVector3( -11.052f, -1.368f, 2.263f ) );
    list.push_back( vaVector3( -11.037f, -1.245f, 2.265f ) );
    list.push_back( vaVector3( -11.018f, -1.088f, 2.267f ) );
    list.push_back( vaVector3( -10.999f, -0.933f, 2.269f ) );
    list.push_back( vaVector3( -10.976f, -0.739f, 2.271f ) );
    list.push_back( vaVector3( -10.952f, -0.546f, 2.274f ) );
    list.push_back( vaVector3( -10.929f, -0.352f, 2.276f ) );
    list.push_back( vaVector3( -10.910f, -0.195f, 2.278f ) );
    list.push_back( vaVector3( -10.882f, 0.034f, 2.281f ) );
    list.push_back( vaVector3( -10.863f, 0.190f, 2.283f ) );
    list.push_back( vaVector3( -10.774f, 0.301f, 2.283f ) );
    list.push_back( vaVector3( -10.685f, 0.371f, 2.283f ) );
    list.push_back( vaVector3( -10.576f, 0.456f, 2.283f ) );
    list.push_back( vaVector3( -10.468f, 0.541f, 2.283f ) );
    list.push_back( vaVector3( -10.402f, 0.593f, 2.283f ) );
    list.push_back( vaVector3( -10.333f, 0.647f, 2.283f ) );
    list.push_back( vaVector3( -10.246f, 0.715f, 2.283f ) );
    list.push_back( vaVector3( -10.158f, 0.784f, 2.283f ) );
    list.push_back( vaVector3( -10.069f, 0.853f, 2.283f ) );
    list.push_back( vaVector3( -9.959f, 0.940f, 2.283f ) );
    list.push_back( vaVector3( -9.870f, 1.010f, 2.283f ) );
    list.push_back( vaVector3( -9.780f, 1.080f, 2.283f ) );
    list.push_back( vaVector3( -9.691f, 1.150f, 2.283f ) );
    list.push_back( vaVector3( -9.579f, 1.237f, 2.283f ) );
    list.push_back( vaVector3( -9.490f, 1.308f, 2.283f ) );
    list.push_back( vaVector3( -9.379f, 1.394f, 2.283f ) );
    list.push_back( vaVector3( -9.291f, 1.464f, 2.283f ) );
    list.push_back( vaVector3( -9.201f, 1.534f, 2.283f ) );
    list.push_back( vaVector3( -9.089f, 1.622f, 2.283f ) );
    list.push_back( vaVector3( -8.998f, 1.693f, 2.283f ) );
    list.push_back( vaVector3( -8.907f, 1.765f, 2.283f ) );
    list.push_back( vaVector3( -8.818f, 1.834f, 2.283f ) );
    list.push_back( vaVector3( -8.750f, 1.888f, 2.283f ) );
    list.push_back( vaVector3( -8.661f, 1.957f, 2.283f ) );
    list.push_back( vaVector3( -8.571f, 2.028f, 2.283f ) );
    list.push_back( vaVector3( -8.459f, 2.115f, 2.283f ) );
    list.push_back( vaVector3( -8.392f, 2.168f, 2.283f ) );
    list.push_back( vaVector3( -8.303f, 2.238f, 2.283f ) );
    list.push_back( vaVector3( -8.213f, 2.308f, 2.283f ) );
    list.push_back( vaVector3( -8.102f, 2.396f, 2.283f ) );
    list.push_back( vaVector3( -8.012f, 2.466f, 2.283f ) );
    list.push_back( vaVector3( -7.922f, 2.537f, 2.283f ) );
    list.push_back( vaVector3( -7.832f, 2.607f, 2.283f ) );
    list.push_back( vaVector3( -7.743f, 2.677f, 2.283f ) );
    list.push_back( vaVector3( -7.631f, 2.764f, 2.283f ) );
    list.push_back( vaVector3( -7.542f, 2.834f, 2.283f ) );
    list.push_back( vaVector3( -7.450f, 2.906f, 2.283f ) );
    list.push_back( vaVector3( -7.358f, 2.979f, 2.283f ) );
    list.push_back( vaVector3( -7.241f, 3.070f, 2.283f ) );
    list.push_back( vaVector3( -7.149f, 3.143f, 2.283f ) );
    list.push_back( vaVector3( -7.059f, 3.213f, 2.283f ) );
    list.push_back( vaVector3( -6.946f, 3.302f, 2.283f ) );
    list.push_back( vaVector3( -6.833f, 3.390f, 2.283f ) );
    list.push_back( vaVector3( -6.720f, 3.479f, 2.283f ) );
    list.push_back( vaVector3( -6.630f, 3.549f, 2.283f ) );
    list.push_back( vaVector3( -6.562f, 3.603f, 2.283f ) );
    list.push_back( vaVector3( -6.448f, 3.692f, 2.283f ) );
    list.push_back( vaVector3( -6.353f, 3.766f, 2.283f ) );
    list.push_back( vaVector3( -6.240f, 3.855f, 2.283f ) );
    list.push_back( vaVector3( -6.150f, 3.926f, 2.283f ) );
    list.push_back( vaVector3( -6.057f, 3.999f, 2.283f ) );
    list.push_back( vaVector3( -5.944f, 4.090f, 2.172f ) );
    list.push_back( vaVector3( -5.832f, 4.180f, 2.028f ) );
    list.push_back( vaVector3( -5.740f, 4.255f, 1.909f ) );
    list.push_back( vaVector3( -5.629f, 4.346f, 1.766f ) );
    list.push_back( vaVector3( -5.516f, 4.438f, 1.620f ) );
    list.push_back( vaVector3( -5.423f, 4.511f, 1.588f ) );
    list.push_back( vaVector3( -5.308f, 4.601f, 1.588f ) );
    list.push_back( vaVector3( -5.217f, 4.673f, 1.588f ) );
    list.push_back( vaVector3( -5.123f, 4.746f, 1.588f ) );
    list.push_back( vaVector3( -5.030f, 4.819f, 1.588f ) );
    list.push_back( vaVector3( -4.960f, 4.874f, 1.588f ) );
    list.push_back( vaVector3( -4.801f, 4.998f, 1.588f ) );
    list.push_back( vaVector3( -4.732f, 5.053f, 1.588f ) );
    list.push_back( vaVector3( -4.660f, 5.109f, 1.588f ) );
    list.push_back( vaVector3( -4.612f, 5.147f, 1.588f ) );
    list.push_back( vaVector3( -4.474f, 5.255f, 1.588f ) );
    list.push_back( vaVector3( -4.404f, 5.310f, 1.588f ) );
    list.push_back( vaVector3( -4.334f, 5.365f, 1.588f ) );
    list.push_back( vaVector3( -4.264f, 5.420f, 1.588f ) );
    list.push_back( vaVector3( -4.172f, 5.491f, 1.588f ) );
    list.push_back( vaVector3( -4.082f, 5.562f, 1.588f ) );
    list.push_back( vaVector3( -3.990f, 5.634f, 1.588f ) );
    list.push_back( vaVector3( -3.898f, 5.706f, 1.588f ) );
    list.push_back( vaVector3( -3.806f, 5.778f, 1.588f ) );
    list.push_back( vaVector3( -3.694f, 5.866f, 1.588f ) );
    list.push_back( vaVector3( -3.625f, 5.920f, 1.588f ) );
    list.push_back( vaVector3( -3.514f, 5.984f, 1.587f ) );
    list.push_back( vaVector3( -3.471f, 6.002f, 1.587f ) );
    list.push_back( vaVector3( -3.423f, 6.022f, 1.586f ) );
    list.push_back( vaVector3( -3.370f, 6.044f, 1.585f ) );
    list.push_back( vaVector3( -3.312f, 6.069f, 1.584f ) );
    list.push_back( vaVector3( -3.228f, 6.105f, 1.583f ) );
    list.push_back( vaVector3( -3.160f, 6.134f, 1.582f ) );
    list.push_back( vaVector3( -3.084f, 6.165f, 1.581f ) );
    list.push_back( vaVector3( -2.978f, 6.211f, 1.579f ) );
    list.push_back( vaVector3( -2.893f, 6.246f, 1.578f ) );
    list.push_back( vaVector3( -2.803f, 6.284f, 1.576f ) );
    list.push_back( vaVector3( -2.710f, 6.324f, 1.575f ) );
    list.push_back( vaVector3( -2.581f, 6.379f, 1.573f ) );
    list.push_back( vaVector3( -2.485f, 6.419f, 1.572f ) );
    list.push_back( vaVector3( -2.358f, 6.473f, 1.570f ) );
    list.push_back( vaVector3( -2.262f, 6.514f, 1.568f ) );
    list.push_back( vaVector3( -2.134f, 6.568f, 1.566f ) );
    list.push_back( vaVector3( -2.037f, 6.609f, 1.565f ) );
    list.push_back( vaVector3( -1.909f, 6.663f, 1.563f ) );
    list.push_back( vaVector3( -1.813f, 6.704f, 1.561f ) );
    list.push_back( vaVector3( -1.682f, 6.759f, 1.559f ) );
    list.push_back( vaVector3( -1.587f, 6.800f, 1.558f ) );
    list.push_back( vaVector3( -1.461f, 6.853f, 1.556f ) );
    list.push_back( vaVector3( -1.336f, 6.906f, 1.554f ) );
    list.push_back( vaVector3( -1.243f, 6.946f, 1.552f ) );
    list.push_back( vaVector3( -1.116f, 6.999f, 1.551f ) );
    list.push_back( vaVector3( -0.992f, 7.052f, 1.549f ) );
    list.push_back( vaVector3( -0.896f, 7.092f, 1.547f ) );
    list.push_back( vaVector3( -0.771f, 7.145f, 1.545f ) );
    list.push_back( vaVector3( -0.648f, 7.197f, 1.543f ) );
    list.push_back( vaVector3( -0.525f, 7.249f, 1.541f ) );
    list.push_back( vaVector3( -0.429f, 7.290f, 1.540f ) );
    list.push_back( vaVector3( -0.307f, 7.342f, 1.538f ) );
    list.push_back( vaVector3( -0.216f, 7.380f, 1.537f ) );
    list.push_back( vaVector3( -0.096f, 7.431f, 1.535f ) );
    list.push_back( vaVector3( 0.026f, 7.483f, 1.533f ) );
    list.push_back( vaVector3( 0.146f, 7.534f, 1.531f ) );
    list.push_back( vaVector3( 0.267f, 7.585f, 1.529f ) );
    list.push_back( vaVector3( 0.390f, 7.637f, 1.527f ) );
    list.push_back( vaVector3( 0.591f, 7.722f, 1.524f ) );
    list.push_back( vaVector3( 0.682f, 7.761f, 1.523f ) );
    list.push_back( vaVector3( 0.798f, 7.810f, 1.521f ) );
    list.push_back( vaVector3( 0.887f, 7.848f, 1.520f ) );
    list.push_back( vaVector3( 1.004f, 7.897f, 1.518f ) );
    list.push_back( vaVector3( 1.124f, 7.948f, 1.516f ) );
    list.push_back( vaVector3( 1.215f, 7.987f, 1.515f ) );
    list.push_back( vaVector3( 1.333f, 8.036f, 1.513f ) );
    list.push_back( vaVector3( 1.450f, 8.086f, 1.511f ) );
    list.push_back( vaVector3( 1.537f, 8.123f, 1.510f ) );
    list.push_back( vaVector3( 1.652f, 8.172f, 1.508f ) );
    list.push_back( vaVector3( 1.767f, 8.221f, 1.506f ) );
    list.push_back( vaVector3( 1.882f, 8.269f, 1.505f ) );
    list.push_back( vaVector3( 1.995f, 8.317f, 1.503f ) );
    list.push_back( vaVector3( 2.081f, 8.353f, 1.502f ) );
    list.push_back( vaVector3( 2.197f, 8.402f, 1.500f ) );
    list.push_back( vaVector3( 2.314f, 8.452f, 1.498f ) );
    list.push_back( vaVector3( 2.427f, 8.500f, 1.496f ) );
    list.push_back( vaVector3( 2.539f, 8.547f, 1.495f ) );
    list.push_back( vaVector3( 2.651f, 8.595f, 1.493f ) );
    list.push_back( vaVector3( 2.787f, 8.653f, 1.491f ) );
    list.push_back( vaVector3( 2.899f, 8.700f, 1.489f ) );
    list.push_back( vaVector3( 3.011f, 8.747f, 1.487f ) );
    list.push_back( vaVector3( 3.147f, 8.805f, 1.485f ) );
    list.push_back( vaVector3( 9.551f, 6.751f, 5.534f ) );
    list.push_back( vaVector3( 9.490f, 6.726f, 5.516f ) );
    list.push_back( vaVector3( 9.443f, 6.706f, 5.503f ) );
    list.push_back( vaVector3( 9.367f, 6.675f, 5.482f ) );
    list.push_back( vaVector3( 9.312f, 6.652f, 5.467f ) );
    list.push_back( vaVector3( 9.225f, 6.617f, 5.443f ) );
    list.push_back( vaVector3( 9.162f, 6.591f, 5.425f ) );
    list.push_back( vaVector3( 9.060f, 6.549f, 5.397f ) );
    list.push_back( vaVector3( 8.953f, 6.505f, 5.367f ) );
    list.push_back( vaVector3( 8.877f, 6.474f, 5.345f ) );
    list.push_back( vaVector3( 8.760f, 6.426f, 5.313f ) );
    list.push_back( vaVector3( 8.641f, 6.377f, 5.280f ) );
    list.push_back( vaVector3( 8.523f, 6.328f, 5.247f ) );
    list.push_back( vaVector3( 8.478f, 6.215f, 5.225f ) );
    list.push_back( vaVector3( 8.410f, 6.046f, 5.192f ) );
    list.push_back( vaVector3( 8.342f, 5.875f, 5.159f ) );
    list.push_back( vaVector3( 8.275f, 5.706f, 5.126f ) );
    list.push_back( vaVector3( 8.229f, 5.591f, 5.104f ) );
    list.push_back( vaVector3( 8.278f, 5.471f, 5.104f ) );
    list.push_back( vaVector3( 8.327f, 5.351f, 5.104f ) );
    list.push_back( vaVector3( 8.377f, 5.230f, 5.104f ) );
    list.push_back( vaVector3( 8.394f, 5.187f, 5.104f ) );
    list.push_back( vaVector3( 8.394f, 5.187f, 5.104f ) );
    list.push_back( vaVector3( 8.394f, 5.187f, 5.104f ) );
    list.push_back( vaVector3( 8.426f, 5.200f, 5.084f ) );
    list.push_back( vaVector3( 8.482f, 5.223f, 5.048f ) );
    list.push_back( vaVector3( 8.547f, 5.249f, 5.007f ) );
    list.push_back( vaVector3( 8.622f, 5.280f, 4.959f ) );
    list.push_back( vaVector3( 8.678f, 5.303f, 4.923f ) );
    list.push_back( vaVector3( 8.770f, 5.341f, 4.865f ) );
    list.push_back( vaVector3( 8.871f, 5.383f, 4.800f ) );
    list.push_back( vaVector3( 8.944f, 5.413f, 4.754f ) );
    list.push_back( vaVector3( 9.061f, 5.461f, 4.679f ) );
    list.push_back( vaVector3( 9.146f, 5.496f, 4.625f ) );
    list.push_back( vaVector3( 9.278f, 5.550f, 4.541f ) );
    list.push_back( vaVector3( 9.424f, 5.610f, 4.448f ) );
    list.push_back( vaVector3( 9.529f, 5.653f, 4.381f ) );
    list.push_back( vaVector3( 9.685f, 5.717f, 4.281f ) );
    list.push_back( vaVector3( 9.838f, 5.780f, 4.184f ) );
    list.push_back( vaVector3( 9.941f, 5.822f, 4.118f ) );
    list.push_back( vaVector3( 10.095f, 5.886f, 4.020f ) );
    list.push_back( vaVector3( -3.797f, -2.068f, 0.627f ) );
    list.push_back( vaVector3( -3.773f, -2.117f, 0.632f ) );
    list.push_back( vaVector3( -3.738f, -2.190f, 0.638f ) );
    list.push_back( vaVector3( -3.708f, -2.251f, 0.644f ) );
    list.push_back( vaVector3( -3.686f, -2.297f, 0.648f ) );
    list.push_back( vaVector3( -3.651f, -2.369f, 0.654f ) );
    list.push_back( vaVector3( -3.625f, -2.422f, 0.659f ) );
    list.push_back( vaVector3( -3.586f, -2.503f, 0.666f ) );
    list.push_back( vaVector3( -3.544f, -2.590f, 0.674f ) );
    list.push_back( vaVector3( -3.514f, -2.652f, 0.680f ) );
    list.push_back( vaVector3( -3.468f, -2.748f, 0.688f ) );
    list.push_back( vaVector3( -3.436f, -2.813f, 0.694f ) );
    list.push_back( vaVector3( -3.310f, -3.073f, 0.717f ) );
    list.push_back( vaVector3( -3.261f, -3.175f, 0.727f ) );
    list.push_back( vaVector3( -3.228f, -3.242f, 0.733f ) );
    list.push_back( vaVector3( -3.194f, -3.312f, 0.739f ) );
    list.push_back( vaVector3( -3.145f, -3.414f, 0.748f ) );
    list.push_back( vaVector3( -3.097f, -3.513f, 0.757f ) );
    list.push_back( vaVector3( -3.064f, -3.581f, 0.763f ) );
    list.push_back( vaVector3( -3.016f, -3.680f, 0.772f ) );
    list.push_back( vaVector3( -2.967f, -3.780f, 0.781f ) );
    list.push_back( vaVector3( -2.934f, -3.849f, 0.787f ) );
    list.push_back( vaVector3( -2.885f, -3.949f, 0.796f ) );
    list.push_back( vaVector3( -2.838f, -4.047f, 0.805f ) );
    list.push_back( vaVector3( -2.790f, -4.146f, 0.814f ) );
    list.push_back( vaVector3( -2.742f, -4.245f, 0.822f ) );
    list.push_back( vaVector3( -2.709f, -4.312f, 0.828f ) );
    list.push_back( vaVector3( -2.663f, -4.408f, 0.837f ) );
    list.push_back( vaVector3( -2.615f, -4.505f, 0.846f ) );
    list.push_back( vaVector3( -2.569f, -4.602f, 0.854f ) );
    list.push_back( vaVector3( -2.522f, -4.697f, 0.863f ) );
    list.push_back( vaVector3( -2.476f, -4.793f, 0.871f ) );
    list.push_back( vaVector3( -2.430f, -4.889f, 0.880f ) );
    list.push_back( vaVector3( -2.383f, -4.984f, 0.889f ) );
    list.push_back( vaVector3( -2.337f, -5.079f, 0.897f ) );
    list.push_back( vaVector3( -2.291f, -5.175f, 0.906f ) );
    list.push_back( vaVector3( -2.245f, -5.270f, 0.914f ) );
    list.push_back( vaVector3( -2.214f, -5.334f, 0.920f ) );
    list.push_back( vaVector3( -2.167f, -5.431f, 0.929f ) );
    list.push_back( vaVector3( -2.122f, -5.524f, 0.937f ) );
    list.push_back( vaVector3( -2.075f, -5.620f, 0.946f ) );
    list.push_back( vaVector3( -2.029f, -5.715f, 0.954f ) );
    list.push_back( vaVector3( -1.983f, -5.810f, 0.963f ) );
    list.push_back( vaVector3( -1.938f, -5.904f, 0.971f ) );
    list.push_back( vaVector3( -1.892f, -5.998f, 0.979f ) );
    list.push_back( vaVector3( -1.861f, -6.061f, 0.985f ) );
    list.push_back( vaVector3( -1.815f, -6.156f, 0.994f ) );
    list.push_back( vaVector3( -1.770f, -6.249f, 1.002f ) );
    list.push_back( vaVector3( -1.725f, -6.341f, 1.010f ) );
    list.push_back( vaVector3( -1.679f, -6.436f, 1.019f ) );
    list.push_back( vaVector3( -1.635f, -6.528f, 1.027f ) );
    list.push_back( vaVector3( -1.575f, -6.651f, 1.038f ) );
    list.push_back( vaVector3( -1.531f, -6.742f, 1.046f ) );
    list.push_back( vaVector3( -1.487f, -6.833f, 1.054f ) );
    list.push_back( vaVector3( -1.401f, -7.010f, 1.070f ) );
    list.push_back( vaVector3( -1.357f, -7.101f, 1.078f ) );
    list.push_back( vaVector3( -1.301f, -7.217f, 1.089f ) );
    list.push_back( vaVector3( -1.257f, -7.307f, 1.097f ) );
    list.push_back( vaVector3( -1.213f, -7.398f, 1.105f ) );
    list.push_back( vaVector3( -1.169f, -7.488f, 1.113f ) );
    list.push_back( vaVector3( -1.125f, -7.579f, 1.121f ) );
    list.push_back( vaVector3( -1.068f, -7.697f, 1.132f ) );
    list.push_back( vaVector3( -1.012f, -7.813f, 1.142f ) );
    list.push_back( vaVector3( -0.970f, -7.899f, 1.150f ) );
    list.push_back( vaVector3( -0.927f, -7.988f, 1.158f ) );
    list.push_back( vaVector3( -0.884f, -8.076f, 1.165f ) );
    list.push_back( vaVector3( -0.829f, -8.190f, 1.176f ) );
    list.push_back( vaVector3( -0.786f, -8.278f, 1.184f ) );
    list.push_back( vaVector3( -0.745f, -8.364f, 1.191f ) );
    list.push_back( vaVector3( -0.701f, -8.454f, 1.199f ) );
    list.push_back( vaVector3( -0.658f, -8.543f, 1.207f ) );
    list.push_back( vaVector3( -0.615f, -8.631f, 1.215f ) );
    list.push_back( vaVector3( -0.572f, -8.719f, 1.223f ) );
    list.push_back( vaVector3( -0.517f, -8.834f, 1.233f ) );
    list.push_back( vaVector3( -0.476f, -8.919f, 1.241f ) );
    list.push_back( vaVector3( -0.421f, -9.032f, 1.251f ) );
    list.push_back( vaVector3( -0.392f, -9.092f, 1.256f ) );
    list.push_back( vaVector3( -0.337f, -9.205f, 1.267f ) );
    list.push_back( vaVector3( -0.294f, -9.293f, 1.274f ) );
    list.push_back( vaVector3( -0.252f, -9.380f, 1.282f ) );
    list.push_back( vaVector3( -0.197f, -9.494f, 1.292f ) );
    list.push_back( vaVector3( -0.155f, -9.580f, 1.300f ) );
    list.push_back( vaVector3( -0.113f, -9.667f, 1.308f ) );
    list.push_back( vaVector3( -0.072f, -9.751f, 1.315f ) );
    list.push_back( vaVector3( -0.031f, -9.836f, 1.323f ) );
    list.push_back( vaVector3( 0.010f, -9.919f, 1.331f ) );
    list.push_back( vaVector3( 0.076f, -10.057f, 1.343f ) );
    list.push_back( vaVector3( 0.117f, -10.141f, 1.350f ) );
    list.push_back( vaVector3( 0.211f, -10.335f, 1.368f ) );
    list.push_back( vaVector3( 0.252f, -10.419f, 1.375f ) );
    list.push_back( vaVector3( 0.280f, -10.477f, 1.381f ) );
    list.push_back( vaVector3( 0.321f, -10.561f, 1.388f ) );
    list.push_back( vaVector3( 0.361f, -10.645f, 1.396f ) );
    list.push_back( vaVector3( 0.401f, -10.727f, 1.403f ) );
    list.push_back( vaVector3( 0.441f, -10.810f, 1.410f ) );
    list.push_back( vaVector3( 0.468f, -10.866f, 1.415f ) );
    list.push_back( vaVector3( 0.509f, -10.950f, 1.423f ) );
    list.push_back( vaVector3( 0.551f, -11.037f, 1.431f ) );
    list.push_back( vaVector3( 0.593f, -11.122f, 1.438f ) );
    list.push_back( vaVector3( 0.634f, -11.207f, 1.446f ) );
    list.push_back( vaVector3( 0.673f, -11.288f, 1.453f ) );
    list.push_back( vaVector3( 0.726f, -11.397f, 1.463f ) );
    list.push_back( vaVector3( 0.766f, -11.480f, 1.470f ) );
    list.push_back( vaVector3( 0.806f, -11.562f, 1.478f ) );
    list.push_back( vaVector3( 0.858f, -11.669f, 1.487f ) );
    list.push_back( vaVector3( 0.897f, -11.751f, 1.495f ) );
    list.push_back( vaVector3( 0.950f, -11.858f, 1.504f ) );
    list.push_back( vaVector3( 0.988f, -11.938f, 1.511f ) );
    list.push_back( vaVector3( 1.028f, -12.020f, 1.519f ) );
    list.push_back( vaVector3( 1.078f, -12.124f, 1.528f ) );
    list.push_back( vaVector3( 1.129f, -12.228f, 1.537f ) );
    list.push_back( vaVector3( 1.181f, -12.335f, 1.547f ) );
    list.push_back( vaVector3( 1.232f, -12.441f, 1.556f ) );
    list.push_back( vaVector3( 1.271f, -12.521f, 1.563f ) );
    list.push_back( vaVector3( 1.369f, -12.724f, 1.582f ) );
    list.push_back( vaVector3( 1.420f, -12.829f, 1.591f ) );
    list.push_back( vaVector3( 1.471f, -12.934f, 1.600f ) );
    list.push_back( vaVector3( 1.535f, -13.066f, 1.612f ) );

    entt::entity lightsParent = scene.CreateEntity( "TestLights" );

    float lightSize = 0.02f;
    float intensity = 0.05f;

    vaRandom rand(0);

    for( int i = 0; i < list.size(); i++ )
    {
        entt::entity lightEntity = scene.CreateEntity( vaStringTools::Format("light_%04d", i), vaMatrix4x4::FromScaleRotationTranslation( {lightSize, lightSize, lightSize}, vaMatrix3x3::Identity, list[i] ), lightsParent, 
            unitSphereMeshID );
            
        auto & newLight         = scene.Registry().emplace<Scene::LightPoint>( lightEntity );
        newLight.Color          = vaVector3::RandomNormal(rand).ComponentAbs( );
        newLight.Intensity      = intensity;
        newLight.FadeFactor     = 1.0f;
        newLight.Size           = lightSize + 0.01f;    // add epsilon to ensure emissive material hack works
        newLight.Range          = 25.0f;
        newLight.SpotInnerAngle = 0.0f;
        newLight.SpotOuterAngle = 0.0f;
        newLight.CastShadows    = false;

        scene.Registry().emplace<Scene::MaterialPicksLightEmissive>( lightEntity );
    }
#endif
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
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 2.922f, 5.273f, 1.520f ),      vaQuaternion( 0.660f, 0.255f, 0.255f, 0.660f ),     keyTime,     3.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 24
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 6.134f, 5.170f, 1.328f ),      vaQuaternion( 0.680f, 0.195f, 0.195f, 0.680f ),     keyTime,     7.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 32
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 7.658f, 4.902f, 1.616f ),      vaQuaternion( 0.703f, 0.078f, 0.078f, 0.703f ),     keyTime,     6.5f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 40
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 8.318f, 3.589f, 2.072f ),      vaQuaternion( 0.886f, -0.331f, -0.114f, 0.304f ),   keyTime,    14.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 48
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 8.396f, 3.647f, 2.072f ),      vaQuaternion( 0.615f, 0.262f, 0.291f, 0.684f ),     keyTime,     3.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 56
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 9.750f, 0.866f, 2.131f ),      vaQuaternion( 0.747f, -0.131f, -0.113f, 0.642f ),   keyTime,     3.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 64
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 11.496f, -0.826f, 2.429f ),    vaQuaternion( 0.602f, -0.510f, -0.397f, 0.468f ),   keyTime,    10.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 72
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 10.943f, -1.467f, 2.883f ),    vaQuaternion( 0.704f, 0.183f, 0.173f, 0.664f ),     keyTime,     1.2f, 1.8f*defaultDoFRange ) ); keyTime+=keyTimeStep;   // 80
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 7.312f, -3.135f, 2.869f ),     vaQuaternion( 0.692f, 0.159f, 0.158f, 0.686f ),     keyTime,     1.5f, 2.0f*defaultDoFRange ) ); keyTime+=keyTimeStep;   // 88
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 7.559f, -3.795f, 2.027f ),     vaQuaternion( 0.695f, 0.116f, 0.117f, 0.700f ),     keyTime,     1.0f, 1.8f*defaultDoFRange ) ); keyTime+=keyTimeStep;   // 96
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 6.359f, -4.580f, 1.856f ),     vaQuaternion( 0.749f, -0.320f, -0.228f, 0.533f ),   keyTime,     4.0f, 1.2f*defaultDoFRange ) ); keyTime+=keyTimeStep;   // 104
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 5.105f, -6.682f, 0.937f ),     vaQuaternion( 0.559f, -0.421f, -0.429f, 0.570f ),   keyTime,     2.0f, 1.2f*defaultDoFRange ) ); keyTime+=keyTimeStep;   // 112
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 3.612f, -5.566f, 1.724f ),     vaQuaternion( 0.771f, -0.024f, -0.020f, 0.636f ),   keyTime,     2.0f, 1.2f*defaultDoFRange ) ); keyTime+=keyTimeStep;   // 120
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 2.977f, -5.532f, 1.757f ),     vaQuaternion( 0.698f, -0.313f, -0.263f, 0.587f ),   keyTime,    12.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 128
    m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( 1.206f, -1.865f, 1.757f ),     vaQuaternion( 0.701f, -0.204f, -0.191f, 0.657f ),   keyTime,     2.0f, defaultDoFRange ) ); keyTime+=keyTimeStep;   // 136
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
        exposureSettings.AutoExposureKeyValue       = 0.5f;
        exposureSettings.ExposureMax                = 4.0f;
        exposureSettings.ExposureMin                = -4.0f;
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

#ifndef VA_SAMPLE_BUILD_FOR_LAB
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
                    m_currentScene = std::make_shared<vaScene>( );
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
#if 0
            m_currentSceneNew = m_currentScene->ToNew( );
            m_currentSceneNew->SaveJSON( vaCore::GetMediaRootDirectoryNarrow( ) + m_currentSceneNew->Name() + ".vaScene" );

            if( m_currentSceneNew->Name() == "LumberyardBistro" )
            {
                AddLumberyardTestLights( *m_currentSceneNew, GetRenderDevice().GetMeshManager().UnitSphere()->UIDObject_GetUID() /*, GetRenderDevice().GetMaterialManager().GetDefaultEmissiveLightMaterial()->UIDObject_GetUID()*/ );
            }
            m_sceneRenderer->SetScene( m_currentSceneNew );
#else
            m_sceneRenderer->SetScene( m_currentScene );
#endif
            m_presetCamerasDirty = true;
        }

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

#ifndef VA_SAMPLE_BUILD_FOR_LAB

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
    }
    else
    {
        ImGui::Text( "   no vaScene files found!" );
    }

    ImGui::Separator( );

    //ImGui::Checkbox( "Use depth pre-pass", &m_settings.DepthPrePass );
    if( ImGui::CollapsingHeader( "Main scene render view", ImGuiTreeNodeFlags_DefaultOpen ) )
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
#endif // VA_SAMPLE_BUILD_FOR_LAB
    {
        ScriptedTests( application );
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

    public:
        AutoBenchTool( VanillaSample& parent, vaMiniScriptInterface& scriptInterface, bool ensureVisualDeterminism, bool writeReport );
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
            thisPtr->MainRenderView( )->Settings( ).PathTracer      = false;
            thisPtr->MainRenderView( )->Settings( ).ShowWireframe   = false;

            shared_ptr<vaGTAO> gtao = thisPtr->MainRenderView( )->GTAO( );
            auto oldSettings        = gtao->Settings( );
            auto & activeSettings   = gtao->Settings( );

            // add the settings to search
            autoTune.AddSearchSetting( "RadiusMultiplier",          &activeSettings.RadiusMultiplier        , 0.9f, 2.0f );
            autoTune.AddSearchSetting( "FalloffRange",              &activeSettings.FalloffRange            , 0.0f, 0.95f );
            //autoTune.AddSearchSetting( "SampleDistributionPower",   &activeSettings.SampleDistributionPower , 0.8f, 2.5f );
            //autoTune.AddSearchSetting( "ThinOccluderCompensation",  &activeSettings.ThinOccluderCompensation    , 0.0f, 0.4f );
            //autoTune.AddSearchSetting( "FinalValuePower",           &activeSettings.FinalValuePower         , 0.8f, 2.5f );

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
                    gtao->Settings().DenoiseLevel       = 1;
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
            thisPtr->MainRenderView( )->Settings( ).PathTracer      = false;
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
            AutoBenchTool autobench( *thisPtr, msi, false, true );

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
            thisPtr->MainRenderView( )->Settings( ).PathTracer      = false;
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

            //            // testPass: 0: standard pass, 1: gradient filter
            //            for( int testPass = 0; testPass < 2; testPass++ ) 
            //            {
            //                autobench.ReportAddText( vaStringTools::Format( "\r\nPASS %d: ", testPass ) );  

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

#ifndef VA_SAMPLE_BUILD_FOR_LAB
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


}



AutoBenchTool::AutoBenchTool( VanillaSample & parent, vaMiniScriptInterface & scriptInterface, bool ensureVisualDeterminism, bool writeReport ) : m_parent( parent ), m_scriptInterface( scriptInterface ), m_backupCameraStorage( (int64)0, (int64)1024 ) 
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
    WORKSPACE( Sample14_PointShadow );
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
