///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Core/vaCoreIncludes.h"
#include "Core/vaApplicationBase.h"
#include "Core/vaInput.h"
#include "Core/System/vaFileTools.h"

#include "Scene/vaCameraControllers.h"
#include "Scene/vaAssetImporter.h"
#include "Scene/vaScene.h"
#include "Rendering/vaSceneRenderer.h"

#include "Rendering/vaRenderCamera.h"
#include "Rendering/vaShader.h"
#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"
#include "Rendering/vaDebugCanvas.h"
#include "Rendering/Misc/vaZoomTool.h"
#include "Rendering/Misc/vaImageCompareTool.h"

#include "IntegratedExternals/vaImguiIntegration.h"


using namespace Vanilla;

void Workspace00_Scene( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static auto cameraFileName = [] { return vaCore::GetExecutableDirectoryNarrow() + "Workspace00_Scene.camerastate"; };

    struct Globals
    {
        shared_ptr<vaCameraControllerFreeFlight>
                                            CameraFreeFlightController      = nullptr;

        shared_ptr<vaZoomTool>              ZoomTool;
        shared_ptr<vaImageCompareTool>      ImageCompareTool;

        shared_ptr<vaRenderMesh>            MeshPlane;
        shared_ptr<vaRenderMesh>            LightSphereMesh;
        shared_ptr<vaRenderMesh>            TestSphereMesh;

        entt::entity                        MovableEntity;
        std::vector<entt::entity>           OtherEntities;

        // this has most of the thingies
        shared_ptr<vaScene>                 Scene;
        // this draws the scene
        shared_ptr<vaSceneRenderer>         SceneRenderer;
        // this is where the SceneRenderer draws the scene
        shared_ptr<vaSceneMainRenderView>   SceneMainView;

        shared_ptr<vaUISimplePanel>         UIPanel;

        float                               AngleLatLight                   = VA_PIf * 0.25f;
        float                               AngleLongLight                  = -1.2f;
        bool                                Wireframe                       = false;

        void                                Initialize( vaRenderDevice & renderDevice, vaApplicationBase & application )
        {   application;
            Scene           = std::make_shared<vaScene>( );
            SceneRenderer   = renderDevice.CreateModule<vaSceneRenderer>( );
            SceneMainView   = SceneRenderer->CreateMainView( );
            SceneRenderer->SetScene( Scene );

            entt::entity skyboxEntity = Scene->CreateEntity( "DistantIBL" );
            Scene::DistantIBLProbe & distantIBL = Scene->Registry( ).emplace<Scene::DistantIBLProbe>( skyboxEntity, Scene::DistantIBLProbe{} );
            distantIBL.SetImportFilePath( vaCore::GetMediaRootDirectoryNarrow( ) + "noon_grass_2k.hdr" );
//            Scene->GetDistantIBL( ).SetImportFilePath( mediaPath + "sky_cube.dds" );

            SceneMainView->Camera()->SetYFOV( 60.0f / 180.0f * VA_PIf );
            float angleCam = VA_PIf * 0.5f;
            SceneMainView->Camera()->SetPosition( 5.1f * vaVector3{ (float)cos( angleCam ), (float)sin( angleCam ), 0.7f } ); // application.GetTimeFromStart( )
            SceneMainView->Camera()->SetOrientationLookAt( { 0, 0, 2.5f } );

            CameraFreeFlightController = std::shared_ptr<vaCameraControllerFreeFlight>( new vaCameraControllerFreeFlight( ) );
            CameraFreeFlightController->SetMoveWhileNotCaptured( false );

            SceneMainView->Camera()->Load( cameraFileName() );

            SceneMainView->Camera()->AttachController( CameraFreeFlightController );

            // load the UFO asset
            renderDevice.GetAssetPackManager( ).LoadPacks( "ufo", true );  // these should be loaded automatically by scenes that need them but for now just load all in the asset folder

            //    // lights
            //    {
            //        globals->Lighting = renderDevice.CreateModule<vaSceneLighting>( );
            //        vector<shared_ptr<vaLight>> lights;
            //        // lights.push_back( std::make_shared<vaLight>( vaLight::MakeAmbient( "DefaultAmbient", vaVector3( 0.05f, 0.05f, 0.15f ), 1.0f ) ) );
            //        lights.push_back( globals->Light1 = std::make_shared<vaLight>( vaLight::MakeDirectional( "Light1", vaVector3( 1.0f, 1.0f, 0.9f ), 0.01f, vaVector3( 0.0f, -1.0f, -1.0f ).Normalized( ) ) ) );
            //        lights.push_back( globals->Light2 = std::make_shared<vaLight>( vaLight::MakePoint( "Light2", 0.25f, vaVector3( 300.0f, 100.0f, 100.0f ), 2.0f, vaVector3( 0.0f, 2.0f, 2.0f ) ) ) );
            //        globals->Light2->CastShadows = true;
            //        globals->Lighting->SetLights( lights );
            //
            //        globals->DistantIBL = std::make_shared<vaIBLProbe>( renderDevice );
            //        globals->DistantIBLData.SetImportFilePath( vaCore::GetMediaRootDirectoryNarrow( ) + "noon_grass_2k.hdr" );
            //        globals->Lighting->SetDistantIBL( globals->DistantIBL );
            //    }

            // meshes 
            {
                MeshPlane = vaRenderMesh::CreatePlane( renderDevice, vaMatrix4x4::Identity, 10.0f, 10.0f );

                // globals->MeshList.Insert( globals->MeshPlane, vaMatrix4x4::Translation( 0, 0, 0 ) );

                auto sphere = vaRenderMesh::CreateSphere( renderDevice, vaMatrix4x4::Scaling( 0.4f, 0.4f, 0.4f ), 4, true );
                LightSphereMesh = vaRenderMesh::CreateSphere( renderDevice, vaMatrix4x4::Scaling( 0.2f, 0.2f, 0.2f ), 2, true );
                TestSphereMesh = vaRenderMesh::CreateSphere( renderDevice, vaMatrix4x4::Scaling( 0.2f, 0.2f, 0.2f ), 2, true );
            }

            // camera settings
            {
                auto& renderCameraSettings = SceneMainView->Camera()->Settings( );
                renderCameraSettings.BloomSettings.UseBloom = true;
                renderCameraSettings.BloomSettings.BloomMultiplier = 0.1f;
                renderCameraSettings.BloomSettings.BloomSize = 0.3f;
            }

            // misc
            {
                ZoomTool            = std::make_shared<vaZoomTool>( renderDevice );
                ImageCompareTool    = std::make_shared<vaImageCompareTool>( renderDevice );
            }

            // test scene
            {
            }

            // UI
            UIPanel = std::make_shared<vaUISimplePanel>( [globals = this]( vaApplicationBase& application )
            {
                application;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
                //ImGui::SliderFloat( "Light2 long", &globals->AngleLongLight, -VA_PIf, VA_PIf );
                //globals->AngleLongLight = vaMath::AngleWrap( globals->AngleLongLight );
                //ImGui::SliderFloat( "Light2 lat", &globals->AngleLatLight, 0.0f, VA_PIf * 0.5f );
                //globals->AngleLatLight = vaMath::Clamp( globals->AngleLatLight, 0.0f, VA_PIf * 0.5f );


                if( ImGui::Button("set random parent") )
                    globals->Scene->SetParent( globals->MovableEntity, globals->OtherEntities[vaRandom::Singleton.NextIntRange((int)globals->OtherEntities.size()-1)] );
                if( ImGui::Button( "set random child" ) )
                    globals->Scene->SetParent( globals->OtherEntities[vaRandom::Singleton.NextIntRange( (int)globals->OtherEntities.size( ) - 1 )], globals->MovableEntity );

                ImGui::Checkbox( "Wireframe", &globals->Wireframe );
                ImGui::Separator( );
                ImGui::Text( "TODO: " );
                ImGui::Text( " [ ] hmm" );
#endif
                },
                    "Test Scene Workspace", 0, true, vaUIPanel::DockLocation::DockedLeft );

            
            Scene->CreateEntity( "Plane",   vaMatrix4x4::Identity, entt::null, MeshPlane->UIDObject_GetUID() );
            MovableEntity = Scene->CreateEntity( "MovableEntity", vaMatrix4x4::Identity, entt::null, LightSphereMesh->UIDObject_GetUID() );
            
            // this means "don't inherit transform from parent"
            Scene->Registry().emplace<Scene::TransformLocalIsWorldTag>(MovableEntity);

            // bunch of random objects
            vaRandom rnd(0);
            OtherEntities.push_back( entt::null );  // add one null entity for testing
            for( int i = 0; i < 100; i++ )
                OtherEntities.push_back( Scene->CreateEntity( string("entity_")+std::to_string(i), vaMatrix4x4::Translation(vaVector3::Random(rnd) * 2 - 1), entt::null, LightSphereMesh->UIDObject_GetUID() ) );
            for( int i = 0; i < 100; i++ )
                Scene->SetParent( OtherEntities[rnd.NextIntRange((int)OtherEntities.size()-1)], OtherEntities[rnd.NextIntRange((int)OtherEntities.size()-1)] );

        }
    };

    static shared_ptr<Globals> globals;

    if( applicationState == vaApplicationState::Initializing )
    {
        assert( globals == nullptr );
        globals = std::make_shared<Globals>( );
        globals->Initialize( renderDevice, application );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        globals->SceneMainView->Camera( )->Save( cameraFileName() );

        globals = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // main loop starts here

    VA_TRACE_CPU_SCOPE( MainLoop );

    // actual backbuffer could be null if started minimized or something else weird so just not do anything in that case.
    auto backbufferTexture = renderDevice.GetCurrentBackbufferTexture( );
    if( backbufferTexture == nullptr )
        { vaThreading::Sleep(10); return; }

    // this is "comparer stuff" and the main render target stuff
    vaViewport mainViewport( backbufferTexture->GetWidth( ), backbufferTexture->GetHeight( ) );

    // update camera - has to be done manually!
    globals->SceneMainView->Camera()->SetViewport( mainViewport );
    globals->SceneMainView->Camera()->Tick( deltaTime, application.HasFocus( ) ); //&& !freezeMotionAndInput );

    globals->SceneMainView->Settings( ).ShowWireframe = globals->Wireframe;

    vaRenderDeviceContext & renderContext = *renderDevice.GetMainContext( );

//     // Create/update depth
//     if( globals->DepthBuffer == nullptr || globals->DepthBuffer->GetSize( ) != backbufferTex->GetSize( ) || globals->DepthBuffer->GetSampleCount( ) != backbufferTex->GetSampleCount( ) )
//         globals->DepthBuffer = vaTexture::Create2D( renderDevice, vaResourceFormat::D32_FLOAT, backbufferTex->GetSizeX( ), backbufferTex->GetSizeY( ), 1, 1, 1, vaResourceBindSupportFlags::DepthStencil );
// 
//     // Create/update offscreen render target
//     if( globals->PreTonemapRT == nullptr || globals->PreTonemapRT->GetSizeX( ) != backbufferTex->GetSizeX( ) || globals->PreTonemapRT->GetSizeY( ) != backbufferTex->GetSizeY( ) )
//         globals->PreTonemapRT = vaTexture::Create2D( renderDevice, vaResourceFormat::R16G16B16A16_FLOAT, backbufferTex->GetSizeX( ), backbufferTex->GetSizeY( ), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget );
//     if( globals->PostTonemapRT == nullptr || globals->PostTonemapRT->GetSizeX( ) != backbufferTex->GetSizeX( ) || globals->PostTonemapRT->GetSizeY( ) != backbufferTex->GetSizeY( ) )
//         globals->PostTonemapRT = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_TYPELESS, backbufferTex->GetSizeX( ), backbufferTex->GetSizeY( ), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess,
//             vaResourceAccessFlags::Default, vaResourceFormat::R8G8B8A8_UNORM_SRGB, vaResourceFormat::R8G8B8A8_UNORM_SRGB, vaResourceFormat::Unknown, vaResourceFormat::R8G8B8A8_UNORM );
//     globals->SceneTick( globals->Registry, deltaTime );
//     globals->SceneCollectMeshes( globals->Registry, globals->MeshListDynamic, globals->TestSphereMesh );
// 
//     // tick lighting
//     globals->Lighting->Tick( deltaTime );

    // our stuff!
    if( !application.IsMouseCaptured() )
    {
        vaInputKeyboardBase * keyboard = application.GetInputKeyboard(); keyboard;
        vaInputMouseBase * mouse = application.GetInputMouse(); mouse;
        if( keyboard != nullptr )
        {
            float x = 0.0f; float y = 0.0f; float z = 0.0f; float kspeed = 1.0f;
            y += (keyboard->IsKeyDown( (vaKeyboardKeys)'W' ))?( deltaTime * kspeed ):(0.0f);
            y -= (keyboard->IsKeyDown( (vaKeyboardKeys)'S' ))?( deltaTime * kspeed ):(0.0f);
            x -= (keyboard->IsKeyDown( (vaKeyboardKeys)'A' ))?( deltaTime * kspeed ):(0.0f);
            x += (keyboard->IsKeyDown( (vaKeyboardKeys)'D' ))?( deltaTime * kspeed ):(0.0f);
            z += (keyboard->IsKeyDown( (vaKeyboardKeys)'Q' ))?( deltaTime * kspeed ):(0.0f);
            z -= (keyboard->IsKeyDown( (vaKeyboardKeys)'E' ))?( deltaTime * kspeed ):(0.0f);

            auto trans = globals->Scene->Registry().try_get<Scene::TransformLocal>( globals->MovableEntity );
            if( trans != nullptr && (x != 0 || y != 0 || z != 0) )
            {
                (*trans) = static_cast<const Scene::TransformLocal&>( (*trans) * vaMatrix4x4::Translation( { x, y, z } ) );
                globals->Scene->SetTransformDirtyRecursive( globals->MovableEntity );
            }
        }
    }

    application.TickUI( *globals->SceneMainView->Camera() );

    globals->Scene->TickBegin( deltaTime, application.GetCurrentTickIndex() );
    globals->Scene->TickEnd( );

    {
        // VA_TRACE_CPU_SCOPE( RenderLoop );

        // Do the rendering tick and present 
        renderDevice.BeginFrame( deltaTime );

        auto drawResults = globals->SceneRenderer->RenderTick( deltaTime, application.GetCurrentTickIndex() );

        const shared_ptr<vaTexture> & finalColor = globals->SceneMainView->GetOutputColor( );
        //    const shared_ptr<vaTexture> & finalDepth     = m_sceneMainView->GetTextureDepth();

        // this is possible
        if( finalColor == nullptr )
        {
            backbufferTexture->ClearRTV( renderContext, { 0.5f, 0.5f, 0.5f, 1.0f } );
        }
        else
        {
            // // Apply CMAA!
            // if( m_settings.CurrentAAOption == VanillaSample::AAType::CMAA2 )
            // {
            //     VA_TRACE_CPUGPU_SCOPE( CMAA2, renderContext );
            //     if( m_settings.CurrentAAOption == VanillaSample::AAType::CMAA2 )
            //         drawResults |= m_CMAA2->Draw( renderContext, finalColor );
            // }

            // various helper tools - at one point these should go and become part of the base app but for now let's just stick them in here
            {
                if( drawResults == vaDrawResultFlags::None && globals->ImageCompareTool != nullptr )
                    globals->ImageCompareTool->RenderTick( renderContext, finalColor );

                if( globals->ZoomTool != nullptr )
                    globals->ZoomTool->Draw( renderContext, finalColor );
            }

            VA_TRACE_CPUGPU_SCOPE( FinalApply, renderContext );

            renderDevice.StretchRect( renderContext, backbufferTexture, finalColor, vaVector4( (float)0.0f, (float)0.0f, (float)mainViewport.Width, (float)mainViewport.Height ), vaVector4( 0.0f, 0.0f, (float)mainViewport.Width, (float)mainViewport.Height ), false );
        }

        {
            auto & canvas3D = renderDevice.GetCanvas3D( );
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

        // update and draw imgui
        application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer( ), globals->SceneMainView->GetOutputDepth() );

        // present the frame, flip the buffers, etc
        renderDevice.EndAndPresentFrame( ( application.GetVsync( ) ) ? ( 1 ) : ( 0 ) );
    }
}





#if 0 // old!
void Workspace00_PBR( vaRenderDevice& renderDevice, vaApplicationBase& application, float deltaTime, vaApplicationState applicationState )
{
    struct Globals
    {
        shared_ptr<vaZoomTool>              ZoomTool;
        shared_ptr<vaImageCompareTool>      ImageCompareTool;
        shared_ptr<vaSkybox>                Skybox;
        shared_ptr<vaTexture>               SkyboxTexture;
        shared_ptr<vaRenderCamera>          Camera;
        shared_ptr<vaCameraControllerFreeFlight>
            CameraFreeFlightController;
        shared_ptr<vaRenderMesh>            MeshPlane;
        vaRenderInstanceList                   MeshList;
        vaRenderInstanceList                   MeshListDynamic;
        IBLProbeData                      DistantIBLData;
        shared_ptr<vaIBLProbe>              DistantIBL;
        shared_ptr<vaSceneLighting>              Lighting;
        shared_ptr<vaLight>                 Light1;
        shared_ptr<vaLight>                 Light2;
        shared_ptr<vaRenderMesh>            ShinyBalls[11];
        shared_ptr<vaRenderMaterial>        ShinyBallsMaterials[countof( Globals::ShinyBalls )];
        shared_ptr<vaRenderMaterial>        BaseMaterial;
        shared_ptr<vaRenderMaterial>        LightSphereMaterial;
        shared_ptr<vaRenderMesh>            LightSphereMesh;
        shared_ptr<vaTexture>               PreTonemapRT;
        shared_ptr<vaTexture>               PostTonemapRT;
        shared_ptr<vaTexture>               DepthBuffer;
        shared_ptr<vaPostProcessTonemap>    Tonemap;
        shared_ptr<vaUISimplePanel>         UIPanel;
        shared_ptr<vaTexture>               TestBaseColor;
        shared_ptr<vaTexture>               TestNormalmap;
        float                               AngleLatLight = VA_PIf * 0.25f;
        float                               AngleLongLight = -1.2f;
        bool                                ShinyBallsMaterialsDirty = true;
        bool                                Wireframe = false;
        int                                 ShinyBallsPreviewParameterIndex = 0;
        vector<string>                      ShinyBallsPreviewParameters = { "Roughness",    "Metallic",     "Reflectance" };
        vector<pair<float, float>>           ShinyBallsPreviewParameterMinMaxes = { {0.0f, 1.0f},   {0.0f, 1.0f},   {0.0f, 1.0f} };
    };
    static shared_ptr<Globals> globals;

    if( applicationState == vaApplicationState::Initializing )
    {
        assert( globals == nullptr );
        globals = std::make_shared<Globals>( );

        globals->Skybox = renderDevice.CreateModule<vaSkybox>( );

        globals->SkyboxTexture = vaTexture::CreateFromImageFile( renderDevice, vaStringTools::SimpleNarrow( vaCore::GetExecutableDirectory( ) ) + "Media\\sky_cube.dds", vaTextureLoadFlags::Default );

#if 0 // testing updating subresources
        globals->SkyboxTexture = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_UNORM, 1, 1, 1, 6, 1, vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::Cubemap, vaTextureContentsType::GenericColor );
        uint32 initialDataB = 0xFFFF0000; uint32 initialDataW = 0xFF00FFFF;
        vaTextureSubresourceData faceSubResB = { &initialDataB, 4, 4 }; vaTextureSubresourceData faceSubResW = { &initialDataW, 4, 4 };
        globals->SkyboxTexture->UpdateSubresources( 0, vector<vaTextureSubresourceData>{faceSubResB, faceSubResW, faceSubResB, faceSubResW, faceSubResB, faceSubResW } );
#endif

        globals->Skybox->SetCubemap( globals->SkyboxTexture );

        // scale brightness
        globals->Skybox->Settings( ).ColorMultiplier = 1.0f;

        // create camera and setup fov
        globals->Camera = std::make_shared<vaRenderCamera>( renderDevice, true );
        globals->Camera->SetYFOV( 60.0f / 180.0f * VA_PIf );
        float angleCam = VA_PIf * 0.5f;
        globals->Camera->SetPosition( 5.1f * vaVector3{ (float)cos( angleCam ), (float)sin( angleCam ), 0.7f } ); // application.GetTimeFromStart( )
        globals->Camera->SetOrientationLookAt( { 0, 0, 2.5f } );
        globals->CameraFreeFlightController = std::shared_ptr<vaCameraControllerFreeFlight>( new vaCameraControllerFreeFlight( ) );
        globals->CameraFreeFlightController->SetMoveWhileNotCaptured( false );
        globals->Camera->AttachController( globals->CameraFreeFlightController );

        // load the UFO asset
        renderDevice.GetAssetPackManager( ).LoadPacks( "ufo", true );  // these should be loaded automatically by scenes that need them but for now just load all in the asset folder

        // lights
        {
            globals->Lighting = renderDevice.CreateModule<vaSceneLighting>( );
            vector<shared_ptr<vaLight>> lights;
            // lights.push_back( std::make_shared<vaLight>( vaLight::MakeAmbient( "DefaultAmbient", vaVector3( 0.05f, 0.05f, 0.15f ), 1.0f ) ) );
            lights.push_back( globals->Light1 = std::make_shared<vaLight>( vaLight::MakeDirectional( "Light1", vaVector3( 1.0f, 1.0f, 0.9f ), 0.01f, vaVector3( 0.0f, -1.0f, -1.0f ).Normalized( ) ) ) );
            lights.push_back( globals->Light2 = std::make_shared<vaLight>( vaLight::MakePoint( "Light2", 0.25f, vaVector3( 300.0f, 100.0f, 100.0f ), 2.0f, vaVector3( 0.0f, 2.0f, 2.0f ) ) ) );
            globals->Light2->CastShadows = true;
            globals->Lighting->SetLights( lights );

            globals->DistantIBL = std::make_shared<vaIBLProbe>( renderDevice );
            globals->DistantIBLData.SetImportFilePath( vaCore::GetMediaRootDirectoryNarrow( ) + "noon_grass_2k.hdr" );
            globals->Lighting->SetDistantIBL( globals->DistantIBL );
        }

        // meshes 
        {
            globals->MeshPlane = vaRenderMesh::CreatePlane( renderDevice, vaMatrix4x4::Identity, 10.0f, 10.0f );

            globals->MeshList.Insert( globals->MeshPlane, vaMatrix4x4::Translation( 0, 0, 0 ) );

            auto sphere = vaRenderMesh::CreateSphere( renderDevice, vaMatrix4x4::Scaling( 0.4f, 0.4f, 0.4f ), 4, true );
            for( int i = 0; i < countof( globals->ShinyBalls ); i++ )
            {
                globals->ShinyBalls[i] = vaRenderMesh::CreateShallowCopy( *sphere );
                globals->MeshList.Insert( globals->ShinyBalls[i], vaMatrix4x4::Translation( -5.0f + i * 1.0f, 0.0f, 1.1f ) );
            }
            globals->LightSphereMesh = vaRenderMesh::CreateSphere( renderDevice, vaMatrix4x4::Scaling( 0.2f, 0.2f, 0.2f ), 2, true );
        }

        // test textures
        {
            globals->TestBaseColor = vaTexture::CreateFromImageFile( renderDevice, vaCore::GetMediaRootDirectory( ) + L"test_basecolor.dds", vaTextureLoadFlags::PresumeDataIsSRGB, vaResourceBindSupportFlags::ShaderResource, vaTextureContentsType::GenericColor );
            globals->TestNormalmap = vaTexture::CreateFromImageFile( renderDevice, vaCore::GetMediaRootDirectory( ) + L"test_normalmap.dds", vaTextureLoadFlags::PresumeDataIsLinear, vaResourceBindSupportFlags::ShaderResource, vaTextureContentsType::NormalsXY_UNORM );
            // TestNormalmap;
            // TestEmissiveColor;
            // TestClearCoatNormalmap
        }

        // materials
        {
            {
                // just for lights
                globals->LightSphereMaterial = renderDevice.GetMaterialManager( ).CreateRenderMaterial( );
                globals->LightSphereMaterial->SetupFromPreset( "FilamentStandard" );

                // this is to make the balls shiny
                globals->LightSphereMaterial->SetInputSlot( "EmissiveColor", vaVector3( 1.0f, 1.0f, 1.0f ), true, true );
                globals->LightSphereMaterial->SetInputSlot( "EmissiveIntensity", 1.0f, false, false );
                auto ssms = globals->LightSphereMaterial->GetMaterialSettings( );
                ssms.SpecialEmissiveLight = true;
                globals->LightSphereMaterial->SetMaterialSettings( ssms );
                globals->LightSphereMesh->SetMaterial( globals->LightSphereMaterial );
            }


            globals->BaseMaterial = renderDevice.GetMaterialManager( ).CreateRenderMaterial( );
            globals->BaseMaterial->SetupFromPreset( "FilamentStandard" );

            globals->BaseMaterial->SetTextureNode( "BaseColorTexture", *globals->TestBaseColor, vaStandardSamplerType::PointWrap );
            globals->BaseMaterial->ConnectInputSlotWithNode( "BaseColor", "BaseColorTexture" );

            globals->BaseMaterial->SetTextureNode( "NormalTexture", *globals->TestNormalmap, vaStandardSamplerType::AnisotropicWrap );
            globals->BaseMaterial->ConnectInputSlotWithNode( "Normal", "NormalTexture" );

            // create per-ball materials (these are actually initialized/updated in the main loop when required)
            for( int i = 0; i < countof( globals->ShinyBallsMaterials ); i++ )
            {
                globals->ShinyBallsMaterials[i] = renderDevice.GetMaterialManager( ).CreateRenderMaterial( );
                globals->ShinyBalls[i]->SetMaterial( globals->ShinyBallsMaterials[i] );
            }
            globals->MeshPlane->SetMaterial( globals->BaseMaterial );
        }

        // tonemapping and stuff
        {
            globals->Tonemap = renderDevice.CreateModule<vaPostProcessTonemap>( );
            auto& renderCameraSettings = globals->Camera->Settings( );
            renderCameraSettings.BloomSettings.UseBloom = true;
            renderCameraSettings.BloomSettings.BloomMultiplier = 0.1f;
            renderCameraSettings.BloomSettings.BloomSize = 0.3f;
        }

        // Misc
        {
            globals->ZoomTool = std::make_shared<vaZoomTool>( renderDevice );
            globals->ImageCompareTool = std::make_shared<vaImageCompareTool>( renderDevice );
        }

        // UI
        globals->UIPanel = std::make_shared<vaUISimplePanel>( [g = &( *globals )]( vaApplicationBase& application )
        {
            application;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
            ImGui::SliderFloat( "Light2 long", &globals->AngleLongLight, -VA_PIf, VA_PIf );
            globals->AngleLongLight = vaMath::AngleWrap( globals->AngleLongLight );
            ImGui::SliderFloat( "Light2 lat", &globals->AngleLatLight, 0.0f, VA_PIf * 0.5f );
            globals->AngleLatLight = vaMath::Clamp( globals->AngleLatLight, 0.0f, VA_PIf * 0.5f );
            if( ImGui::Button( "Import IBL", { -1, 0 } ) )
            {
                string file = vaFileTools::OpenFileDialog( "", vaCore::GetExecutableDirectoryNarrow( ) );
                if( file != "" )
                    globals->DistantIBLData.SetImportFilePath( file );
            }

            if( ImGuiEx_Combo( "Parameter Preview", globals->ShinyBallsPreviewParameterIndex, globals->ShinyBallsPreviewParameters ) )
                globals->ShinyBallsMaterialsDirty = true;

            ImGui::Checkbox( "Wireframe", &g->Wireframe );
            ImGui::Separator( );

            if( ImGui::CollapsingHeader( "Base material", ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
            {
                ImGui::Indent( );
                globals->ShinyBallsMaterialsDirty |= globals->BaseMaterial->UIPropertiesDraw( application );
                ImGui::Unindent( );
            }
            globals->Light1->UIPropertiesItemTickCollapsable( application, true, false, true );
            globals->Light2->UIPropertiesItemTickCollapsable( application, true, false, true );
            // ImGui::NewLine();
            // ImGui::Separator( );
            // ImGui::TextColored( { 1.0f, 0.5f, 0.5f, 1.0f }, "TODOs:" );
            // ImGui::TextWrapped( globals->TODOText.c_str( ) );
#endif
        },
            "PBRWorkspace", 0, true, vaUIPanel::DockLocation::DockedLeft );

        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        globals = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // main loop starts here

    if( globals->ShinyBallsMaterialsDirty )
    {
        // first update based on the base material
        for( int i = 0; i < countof( globals->ShinyBallsMaterials ); i++ )
        {
            globals->ShinyBallsMaterials[i]->SetupFromOther( *globals->BaseMaterial );

            // then set different for every ball from min until max
            auto minmax = globals->ShinyBallsPreviewParameterMinMaxes[globals->ShinyBallsPreviewParameterIndex];
            float value = vaMath::Lerp( minmax.first, minmax.second, float( i ) / float( countof( globals->ShinyBallsMaterials ) - 1.0f ) );
            globals->ShinyBallsMaterials[i]->SetInputSlot( globals->ShinyBallsPreviewParameters[globals->ShinyBallsPreviewParameterIndex], value, false, false );
        }

        globals->ShinyBallsMaterialsDirty = false;
    }


    globals->Light2->Position = vaGeometry::SphericalToCartesian( globals->AngleLongLight, VA_PIf * 0.5f - globals->AngleLatLight, 10.0f );
    //globals->Light2->Intensity = (float)vaMath::Max( 0.0, (0.5+sin(renderDevice.GetTotalTime()))) * 10.0f * vaVector3( 0.5f, 0.0f, 1.0f );

    auto backbufferTex = renderDevice.GetCurrentBackbufferTexture( );
    vaRenderDeviceContext& mainContext = *renderDevice.GetMainContext( );

    // Create/update depth
    if( globals->DepthBuffer == nullptr || globals->DepthBuffer->GetSize( ) != backbufferTex->GetSize( ) || globals->DepthBuffer->GetSampleCount( ) != backbufferTex->GetSampleCount( ) )
        globals->DepthBuffer = vaTexture::Create2D( renderDevice, vaResourceFormat::D32_FLOAT, backbufferTex->GetSizeX( ), backbufferTex->GetSizeY( ), 1, 1, 1, vaResourceBindSupportFlags::DepthStencil );

    // Create/update offscreen render target
    if( globals->PreTonemapRT == nullptr || globals->PreTonemapRT->GetSizeX( ) != backbufferTex->GetSizeX( ) || globals->PreTonemapRT->GetSizeY( ) != backbufferTex->GetSizeY( ) )
        globals->PreTonemapRT = vaTexture::Create2D( renderDevice, vaResourceFormat::R16G16B16A16_FLOAT, backbufferTex->GetSizeX( ), backbufferTex->GetSizeY( ), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget );
    if( globals->PostTonemapRT == nullptr || globals->PostTonemapRT->GetSizeX( ) != backbufferTex->GetSizeX( ) || globals->PostTonemapRT->GetSizeY( ) != backbufferTex->GetSizeY( ) )
        globals->PostTonemapRT = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_TYPELESS, backbufferTex->GetSizeX( ), backbufferTex->GetSizeY( ), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess,
            vaResourceAccessFlags::Default, vaResourceFormat::R8G8B8A8_UNORM_SRGB, vaResourceFormat::R8G8B8A8_UNORM_SRGB, vaResourceFormat::Unknown, vaResourceFormat::R8G8B8A8_UNORM );

    // setup and rotate camera
    globals->Camera->SetViewport( vaViewport( backbufferTex->GetWidth( ), backbufferTex->GetHeight( ) ) );
    globals->Camera->Tick( deltaTime, true );

    // dynamic meshes
    globals->MeshListDynamic.Reset( );
    if( globals->Light1->Type == vaLight::Type::Point || globals->Light1->Type == vaLight::Type::Spot )
        globals->MeshListDynamic.Insert( globals->LightSphereMesh, vaMatrix4x4::Translation( globals->Light1->Position ) );
    if( globals->Light2->Type == vaLight::Type::Point || globals->Light2->Type == vaLight::Type::Spot )
        globals->MeshListDynamic.Insert( globals->LightSphereMesh, vaMatrix4x4::Translation( globals->Light2->Position ) );

    // ufo
    vaAssetPackManager& assetPackManager = renderDevice.GetAssetPackManager( );
    if( !renderDevice.GetAssetPackManager( ).AnyAsyncOpExecuting( ) )   // have we stopped loading?
    {
        auto assetMesh = vaAssetRenderMesh::SafeCast( assetPackManager.FindAsset( "ufo_retro_toy_mesh" ) );
        if( assetMesh != nullptr && assetMesh->GetRenderMesh( ) != nullptr )
        {
            shared_ptr<vaRenderMesh> ufoMesh = assetMesh->GetRenderMesh( );
            vaMatrix4x4 baseTransform = vaMatrix4x4::Scaling( 0.1f, 0.1f, 0.1f ) * vaMatrix4x4::FromYawPitchRoll( 0, 0, -VA_PIf );
            globals->MeshListDynamic.Insert( ufoMesh, baseTransform * vaMatrix4x4::Translation( 0.0f, -2.0f, 3.0f ) );
        }
    }

    // tick lighting
    globals->Lighting->Tick( deltaTime );

    application.TickUI( );

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    globals->Camera->PreRenderTick( *renderDevice.GetMainContext( ), deltaTime );

    // must do importing here since it requires mainContext
    if( globals->DistantIBLData != globals->DistantIBL->GetContentsData( ) )
    {
        globals->DistantIBL->Reset( );
        if( globals->DistantIBLData.Enabled )
        {
            if( globals->DistantIBLData.ImportFilePath == "" )
            {
                // capture from scene not implemented here
                assert( false );
                globals->DistantIBLData.ImportFilePath = "";
            }
            else
                globals->DistantIBL->Import( mainContext, globals->DistantIBLData );
        }
    }
    if( globals->DistantIBL->HasContents( ) )
        globals->DistantIBL->SetToSkybox( *globals->Skybox );

    vaRenderOutputs preTonemapOutputs( globals->PreTonemapRT, globals->DepthBuffer );
    //currentBackbuffer->ClearRTV( mainContext, vaVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
    globals->DepthBuffer->ClearDSV( mainContext, true, globals->Camera->GetUseReversedZ( ) ? ( 0.0f ) : ( 1.0f ), false, 0 );

    // update shadows
    shared_ptr<vaShadowmap> queuedShadowmap = globals->Lighting->GetNextHighestPriorityShadowmapForRendering( );
    if( queuedShadowmap != nullptr )
    {
        queuedShadowmap->Draw( mainContext, globals->MeshList );
    }

    // needed for more rendering of more complex items - in this case skybox requires it only to get the camera
    vaDrawAttributes drawAttributes( *globals->Camera, vaDrawAttributes::OutputType::Forward, vaDrawAttributes::RenderFlags::None, globals->Lighting.get( ) );

    // opaque skybox
    globals->Skybox->Draw( mainContext, preTonemapOutputs, drawAttributes );

    // draw meshes
    renderDevice.GetMeshManager( ).Draw( mainContext, preTonemapOutputs, drawAttributes, globals->MeshList, vaBlendMode::Opaque, vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::EnableDepthWrite );
    renderDevice.GetMeshManager( ).Draw( mainContext, preTonemapOutputs, drawAttributes, globals->MeshListDynamic, vaBlendMode::Opaque, vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::EnableDepthWrite );

    if( globals->Wireframe )
    {
        vaDrawAttributes wireframeDrawAttributes( *globals->Camera, vaDrawAttributes::OutputType::Forward, vaDrawAttributes::RenderFlags::SetZOffsettedProjMatrix | vaDrawAttributes::RenderFlags::DebugWireframePass );
        renderDevice.GetMeshManager( ).Draw( mainContext, preTonemapOutputs, drawAttributes, globals->MeshList, vaBlendMode::Opaque, vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::DepthTestIncludesEqual );
    }

    // about to perform tonemapping
    vaRenderOutputs postTonemapOutputs( globals->PostTonemapRT, globals->DepthBuffer );

    // performing tonemapping from globals->PreTonemapRT to globals->PostTonemapRT
#if 1
    globals->Tonemap->TickAndApplyCameraPostProcess( mainContext, *globals->Camera, globals->PostTonemapRT, globals->PreTonemapRT );
#else // or just skip tonemapping (will mess up things likely)
    mainContext.CopySRVToCurrentOutput( globals->OffscreenRT );
#endif

    // image comparison tool (doesn't do anything unless used from UI)
    globals->ImageCompareTool->RenderTick( mainContext, globals->PostTonemapRT );

    // draw zoomtool if needed (doesn't do anything unless used from UI)
    globals->ZoomTool->Draw( mainContext, globals->PostTonemapRT );

    // Copy our image into backbuffer (yeah we could skip this and tonemap directly into backbuffer but then we can't apply any UAV processing above which requires UAV support which backbuffer doesn't allow)
    mainContext.CopySRVToRTV( backbufferTex, globals->PostTonemapRT );

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer( ), globals->DepthBuffer );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( ( application.GetVsync( ) ) ? ( 1 ) : ( 0 ) );
}
#endif