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
#include "Core/Misc/simplexnoise1234.h"

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

#include <random>

using namespace Vanilla;

// Content settings
#ifdef _DEBUG
enum { NUM_ASTEROIDS            = 1000 };
#else
enum { NUM_ASTEROIDS            = 50000 };
#endif

#define RATIO_OF_ICE_ASTEROIDS   (1.0f / 100.0f)
// enum { TEXTURE_DIM              = 512 };    // Req'd to be pow2 at the moment
// enum { NUM_UNIQUE_MESHES        = 10 };     // 1000
// enum { MESH_MAX_SUBDIV_LEVELS   = 2 }; // 4x polys for each step.
// See common_defines.h for NUM_UNIQUE_TEXTURES (also needed by shader now)

#define SIM_ORBIT_RADIUS 4500.f
#define SIM_DISC_RADIUS  1200.f
#define SIM_MIN_SCALE    0.2f

struct AsteroidStatic
{
//    vaVector3 surfaceColor;
//    vaVector3 deepColor;
    vaVector4   spinAxis;
    float       scale;
    float       spinVelocity;
    float       orbitVelocity;
    // unsigned int vertexStart;
    // unsigned int textureIndex;
};

void SetupAsteroids( vaScene & scene, std::vector<shared_ptr<vaRenderMesh>> asteroidMeshes, shared_ptr<vaRenderMesh> iceAsteroidMesh, int numAsteroids, float ratioOfIceAsteroids )
{
    // registry is where all entities and components are stored, and it provides traversal structures (groups, views, observers)
    entt::registry & registry = scene.Registry( );
    registry;

    std::mt19937 rng( 0 );
    vaRandom rnd( 0 );

    static int const COLOR_SCHEMES[] = {
        156, 139, 113,  55,  49,  40,
        156, 139, 113,  58,  38,  14,
        156, 139, 113,  98, 101, 104,
        156, 139, 113, 205, 197, 178,
        153, 146, 136,  88,  88,  88,
        189, 181, 164, 148, 108, 102,
    };

    static int const NUM_COLOR_SCHEMES = (int)( sizeof( COLOR_SCHEMES ) / ( 6 * sizeof( int ) ) );

    // Constants
    std::normal_distribution<float> orbitRadiusDist( SIM_ORBIT_RADIUS, 0.6f * SIM_DISC_RADIUS );
    std::normal_distribution<float> heightDist( 0.0f, 0.4f );
    std::uniform_real_distribution<float> angleDist( -VA_PIf, VA_PIf );
    std::uniform_real_distribution<float> radialVelocityDist( 5.0f, 15.0f );
    std::uniform_real_distribution<float> spinVelocityDist( -2.0f, 2.0f );
    std::normal_distribution<float> scaleDist( 1.3f, 0.7f );
    std::normal_distribution<float> colorSchemeDist( 0, NUM_COLOR_SCHEMES - 1 );
    //    std::uniform_int_distribution<unsigned int> textureIndexDist(0, textureCount-1);

    //auto instancesPerMesh = std::max( 1U, asteroidCount / meshInstanceCount );

    // Approximate SRGB->Linear for colors
    float linearColorSchemes[NUM_COLOR_SCHEMES * 6];
    for( int i = 0; i < ARRAYSIZE( linearColorSchemes ); ++i ) {
        linearColorSchemes[i] = std::powf( (float)COLOR_SCHEMES[i] / 255.0f, 2.2f );
    }

    auto parentOfAll = scene.CreateEntity( "ProceduralAsteroids", vaMatrix4x4::Identity );
    parentOfAll;

    // Create a torus of asteroids that spin around the ring
    for( int i = 0; i < numAsteroids; i++ )
    {
        float a = i / (float)numAsteroids;
        float r = 20.0f;
        float distXY = 20.0f;
        float distZ = 20.0f;

        shared_ptr<vaRenderMesh> meshToUse = asteroidMeshes[i % asteroidMeshes.size()];
        float additionalScale = 1.0f;
        
        if( rnd.NextFloat() < ratioOfIceAsteroids )
        {
            meshToUse = iceAsteroidMesh;
            additionalScale = 5.0f;
        }
        entt::entity entity = scene.CreateEntity( "Asteroid", vaMatrix4x4::FromTranslation( { std::sinf(a*VA_PIf*2*r)*distXY, std::cosf(a*VA_PIf*2*r)*distXY, (a-0.5f)*distZ } ), parentOfAll, meshToUse->UIDObject_GetUID() );

//    // this means "don't inherit transform from parent" - is it needed for asteroids? 
//    // probably, if we're not setting world pos directly, which should be a good idea
//        scene->Registry( ).emplace<Scene::TransformLocalIsWorldTag>( MovableEntity );

        // static part (it drives the per-asteroid sim)

        auto scale = scaleDist( rng ) * 1.0f;
#if SIM_USE_GAMMA_DIST_SCALE
        scale = scale * 0.3f;
#endif
        scale = std::max( scale, SIM_MIN_SCALE ) * additionalScale;
        auto scaleMatrix = vaMatrix4x4::Scaling( scale, scale, scale );

        auto orbitRadius = orbitRadiusDist( rng );
        auto discPosZ = float( SIM_DISC_RADIUS ) * heightDist( rng );

        auto disc = vaMatrix4x4::Translation( orbitRadius, 0.0f, discPosZ );

        auto positionAngle = angleDist( rng );
        auto orbit = vaMatrix4x4::RotationZ( positionAngle );

        AsteroidStatic & asteroidStatic = registry.emplace<AsteroidStatic>( entity );

        // Static data
        asteroidStatic.spinVelocity = spinVelocityDist( rng ) / scale; // Smaller asteroids spin faster
        asteroidStatic.orbitVelocity = radialVelocityDist( rng ) / ( scale * orbitRadius ); // Smaller asteroids go faster, and use arc length
        //asteroidStatic.vertexStart = mVertexCountPerMesh * meshInstance;
        asteroidStatic.spinAxis.AsVec3() = ( vaVector3::RandomPointOnSphere( rnd ).Normalized( ) );
        asteroidStatic.scale = scale;
        //     asteroidStatic.textureIndex = textureIndexDist(rng);

        // auto colorScheme = ( (int)abs( colorSchemeDist( rng ) ) ) % NUM_COLOR_SCHEMES;
        // auto c = linearColorSchemes + 6 * colorScheme;
        // asteroidStatic.surfaceColor = vaVector3( c[0], c[1], c[2] );
        // asteroidStatic.deepColor = vaVector3( c[3], c[4], c[5] );

        // // Initialize dynamic data
        // mAsteroidDynamic[i].world = scaleMatrix * disc * orbit;

        assert( asteroidStatic.scale > 0.0f );
        assert( asteroidStatic.orbitVelocity > 0.0f );

        // Initialize dynamic data
        Scene::TransformLocal& transform = registry.get<Scene::TransformLocal>( entity );
        transform = scaleMatrix * disc * orbit;
    }
}

struct AsteroidsMotionWorkNode : vaSceneAsync::WorkNode
{
    vaScene &                   Scene;
    entt::basic_view< entt::entity, entt::exclude_t<>, const AsteroidStatic>
                                View;
    float                       DeltaTime = 0.0f;
    bool &                      AnimateAsteroids;

    AsteroidsMotionWorkNode( vaScene & scene, bool & animateAsteroids ) : Scene( scene ), View( scene.Registry().view<std::add_const_t<AsteroidStatic>>( ) ), AnimateAsteroids( animateAsteroids ),
        vaSceneAsync::WorkNode( "MoveAsteroids", {}, {"motion_done_marker"}, Scene::AccessPermissions::ExportPairLists<
            const AsteroidStatic, Scene::TransformLocal >() )
    { 
    }

    virtual void                    ExecutePrologue( float deltaTime, int64 applicationTickIndex ) override     { DeltaTime = deltaTime; applicationTickIndex; }
    //
    // Asynchronous narrow processing; called after ExecuteWide, returned std::pair<uint, uint> will be used to immediately repeat ExecuteWide if non-zero
    virtual std::pair<uint, uint>   ExecuteNarrow( const uint32 pass, vaSceneAsync::ConcurrencyContext & ) override
    {
        if( pass == 0 && AnimateAsteroids )
            return { (uint32)View.size( ), vaTF::c_chunkBaseSize * 2 };
        return { 0, 0 };
    }
    //
    // Asynchronous wide processing; items run in chunks to minimize various overheads
    virtual void                    ExecuteWide( const uint32 pass, const uint32 itemBegin, const uint32 itemEnd, vaSceneAsync::ConcurrencyContext & ) override
    {
        assert( pass == 0 ); pass;

        entt::registry &    registry = Scene.Registry();
        auto & dirtyList    = Scene.ListDirtyTransforms();

        for( uint32 i = itemBegin; i < itemEnd; i++ )
        {
            auto entity = View[i];
            if( !registry.any_of<Scene::TransformLocal>( entity ) )
                continue;
            const AsteroidStatic& staticData = std::as_const( registry ).get<AsteroidStatic>( entity );
            Scene::TransformLocal& transform = registry.get<Scene::TransformLocal>( entity );
            float deltaTime = DeltaTime;
            {
                auto orbit = vaMatrix4x4::RotationZ( staticData.orbitVelocity * deltaTime );
                auto spin = vaMatrix4x4::RotationAxis( staticData.spinAxis.AsVec3( ), staticData.spinVelocity * deltaTime );
                transform = spin * transform * orbit;
            }
            dirtyList.Append( entity );
        }
    }
    // virtual void                    ExecuteEpilogue( ) override { }
};

void Workspace01_Asteroids( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static auto cameraFileName = [] { return vaCore::GetExecutableDirectoryNarrow() + "Workspace01_Asteroids.camerastate"; };

    struct Globals : public std::enable_shared_from_this<Globals>
    {
        shared_ptr<vaCameraControllerFreeFlight>
                                            CameraFreeFlightController      = nullptr;

        shared_ptr<vaZoomTool>              ZoomTool;
        shared_ptr<vaImageCompareTool>      ImageCompareTool;

        shared_ptr<vaRenderMesh>            MeshPlane;
        shared_ptr<vaRenderMesh>            LightSphereMesh;
        shared_ptr<vaRenderMesh>            TestSphereMesh;
        shared_ptr<vaRenderMesh>            UFOMesh;
        shared_ptr<vaRenderMesh>            FighterMesh;
        shared_ptr<vaRenderMesh>            IceAsteroidMesh;

        std::vector<shared_ptr<vaRenderMesh>>    AsteroidStandardMeshes;

        //std::array<shared_ptr<vaRenderMesh>[MESH_MAX_SUBDIV_LEVELS+1], NUM_UNIQUE_MESHES>
        //                                    AsteroidMeshes;
        //shared_ptr<vaTexture>               AsteroidTexture;
        //shared_ptr<vaRenderMaterial>        AsteroidMaterial;
        bool                                AnimateAsteroids = false;
        entt::entity                        MovableEntity;
        entt::entity                        FighterEntity;
        std::vector<entt::entity>           OtherEntities;

        // this has most of the thingies
        shared_ptr<vaScene>                 Scene;
        // this draws the scene
        shared_ptr<vaSceneRenderer>         SceneRenderer;
        // this is where the SceneRenderer draws the scene
        shared_ptr<vaSceneMainRenderView>   SceneMainView;

        shared_ptr<AsteroidsMotionWorkNode> MotionWorkerNode;

        shared_ptr<vaUISimplePanel>         UIPanel;

        float                               AngleLatLight                   = VA_PIf * 0.25f;
        float                               AngleLongLight                  = -1.2f;
        bool                                Wireframe                       = false;

        void                                Initialize( vaRenderDevice & renderDevice, vaApplicationBase & application )
        {   application;
            void EnTTTest( );
            EnTTTest( );

            // we've got to register all components we're about to be using
            vaSceneComponentRegistry::RegisterComponent<AsteroidStatic>();

            Scene               = std::make_shared<vaScene>( "Asteroids!!" );

            MotionWorkerNode    = std::make_shared<AsteroidsMotionWorkNode>( *Scene, AnimateAsteroids );
            Scene->Async().AddWorkNode( MotionWorkerNode );
            SceneRenderer   = renderDevice.CreateModule<vaSceneRenderer>( );
            SceneMainView   = SceneRenderer->CreateMainView( );
            SceneMainView->SetCursorHoverInfoEnabled(true);
            SceneRenderer->SetScene( Scene );
            SceneRenderer->GeneralSettings().DepthPrepass   = false;  // we have to do it here but having no depth prepass doesn't work in some scenarios and disables ASSAO.
            // SceneRenderer->GeneralSettings().SortOpaque     = false;    // we're mostly CPU bound so let's avoid sort cost

            //AsteroidSim = std::make_shared<AsteroidsSimulation>(1337, NUM_ASTEROIDS, NUM_UNIQUE_MESHES, MESH_MAX_SUBDIV_LEVELS);

            entt::entity skyboxEntity = Scene->CreateEntity( "DistantIBL" );
            Scene::DistantIBLProbe& distantIBL = Scene->Registry( ).emplace<Scene::DistantIBLProbe>( skyboxEntity, Scene::DistantIBLProbe{} );
            distantIBL.SetImportFilePath( vaCore::GetMediaRootDirectoryNarrow( ) + "spacebox.dds" );
//            Scene->GetDistantIBL( ).SetImportFilePath( mediaPath + "sky_cube.dds" );

            SceneMainView->Camera()->SetYFOV( 60.0f / 180.0f * VA_PIf );
            float angleCam = VA_PIf * 0.5f;
            SceneMainView->Camera()->SetPosition( 5.1f * vaVector3{ (float)cos( angleCam ), (float)sin( angleCam ), 0.7f } ); // application.GetTimeFromStart( )
            SceneMainView->Camera()->SetOrientationLookAt( { 0, 0, 2.5f } );

            CameraFreeFlightController = std::shared_ptr<vaCameraControllerFreeFlight>( new vaCameraControllerFreeFlight( ) );
            CameraFreeFlightController->SetMoveWhileNotCaptured( false );

            if( !SceneMainView->Camera()->Load( cameraFileName() ) )
            {
                SceneMainView->Camera()->SetPosition( {9130.291931f, -3350.640213f, 1610.1215305f} );
                SceneMainView->Camera()->SetOrientation( {-0.443121850f, 0.639720142f, 0.516257763f, -0.357601881f} );
            }

            SceneMainView->Camera()->AttachController( CameraFreeFlightController );

            // load the UFO asset
            vaAssetPackManager & assetPackManager = renderDevice.GetAssetPackManager( );
            assetPackManager.LoadPacks( "ufo", true );  // these should be loaded automatically by scenes that need them but for now just load all in the asset folder
            assetPackManager.LoadPacks( "sf_light_fighter_x6", true );
            assetPackManager.LoadPacks( "asteroid_pack", true );

            // meshes 
            {
                MeshPlane = vaRenderMesh::CreatePlane( renderDevice, vaMatrix4x4::Identity, 10.0f, 10.0f );

                // globals->MeshList.Insert( globals->MeshPlane, vaMatrix4x4::Translation( 0, 0, 0 ) );

                LightSphereMesh = vaRenderMesh::CreateSphere( renderDevice, vaMatrix4x4::Scaling( 0.2f, 0.2f, 0.2f ), 2, true );
                TestSphereMesh = vaRenderMesh::CreateSphere( renderDevice, vaMatrix4x4::Scaling( 0.2f, 0.2f, 0.2f ), 2, true );
            }

            // camera settings
            {
                auto& renderCameraSettings = SceneMainView->Camera()->Settings( );
                renderCameraSettings.ExposureSettings.ExposureCompensation = -0.4f;
                renderCameraSettings.BloomSettings.UseBloom = true;
                renderCameraSettings.BloomSettings.BloomMultiplier = 0.05f;
                renderCameraSettings.BloomSettings.BloomSize = 0.3f;
            }

            // misc
            {
                ZoomTool            = std::make_shared<vaZoomTool>( renderDevice );
                ImageCompareTool    = std::make_shared<vaImageCompareTool>( renderDevice );
            }

#if 0
            // create asteroids material!
            {
                AsteroidTexture = CreateTexture( renderDevice, 0 );

                AsteroidMaterial = renderDevice.GetMaterialManager( ).CreateRenderMaterial( );

                AsteroidMaterial->SetupFromPreset( "FilamentStandard" );

                // this is probably how textures are connected
                AsteroidMaterial->SetTextureNode( "BaseColorTexture", AsteroidTexture, vaStandardSamplerType::PointWrap );
                AsteroidMaterial->ConnectInputSlotWithNode( "BaseColor", "BaseColorTexture" );
                //
                //AsteroidMaterial->SetTextureNode( "NormalTexture", *globals->TestNormalmap, vaStandardSamplerType::AnisotropicWrap );
                //AsteroidMaterial->ConnectInputSlotWithNode( "Normal", "NormalTexture" );

                // // make them shine for now
                // AsteroidMaterial->SetInputSlot( "EmissiveColor", vaVector3( 0.5f, 1.0f, 0.5f ), true, true );
                // AsteroidMaterial->SetInputSlot( "EmissiveIntensity", 1.0f, false, false );

                auto shaderSettings = AsteroidMaterial->GetShaderSettings();
                shaderSettings.BaseMacros.push_back( { "ASTEROID_SHADER_HACKS", "" } );
                AsteroidMaterial->SetShaderSettings(shaderSettings);
            }
#endif

#if 0
            // create asteroid meshes!
            {
                for( int i = 0; i < AsteroidMeshes.size(); i++ )
                {
                    // create mesh
                   CreateAsteroidMesh( renderDevice, i , AsteroidMeshes[i]);

                    // set material
                   for (int iLOD = 0; iLOD < MESH_MAX_SUBDIV_LEVELS+1; iLOD++)
                   {
                       AsteroidMeshes[i][iLOD]->SetMaterial(AsteroidMaterial);
                   }
                }
            }
#endif

            assetPackManager.WaitFinishAsyncOps( ); // make sure we've stopped loading or below might not be available
            UFOMesh = assetPackManager.FindRenderMesh( "ufo_retro_toy_mesh" );
            IceAsteroidMesh = assetPackManager.FindRenderMesh( "iceasteroid_mesh" );
            FighterMesh = assetPackManager.FindRenderMesh( "sf_light_fighter_x6_mesh" );

#if 1
            // find individual asteroid meshes
            for( int i = 0; ; i++ )
            {
                auto newMesh = assetPackManager.FindRenderMesh( vaStringTools::Format( "asteroid_mesh_%02d", i ) );
                if( newMesh == nullptr )
                    break;
                // ////newMesh->GetParentAsset()->Rename( vaStringTools::Format( "asteroid_mesh_%02d", i ) );
                // newMesh->RebuildLODs( 0.0007f, 105.0f / 180.0f * VA_PIf );
                // newMesh->TNTesselate();
                // if( newMesh->GetAABB().Size.Length() > 14.0f )   // tiny ones don't need this many triangles
                //     newMesh->TNTesselate();
                AsteroidStandardMeshes.push_back( newMesh );
            }
#endif

#if 1
            // create asteroids in the scene
            SetupAsteroids( *Scene, AsteroidStandardMeshes, IceAsteroidMesh, NUM_ASTEROIDS, RATIO_OF_ICE_ASTEROIDS );
#endif

            // just a random scene, TODO: remove all this
            {
#if 1 // UFO in the middle
                MovableEntity = Scene->CreateEntity( "UFOEntity", vaMatrix4x4::Identity );
                Scene->CreateEntity( "UFOMesh", vaMatrix4x4::Scaling(3, 3, 3) * vaMatrix4x4::RotationX( VA_PIf * 1.0f ), MovableEntity, UFOMesh->UIDObject_GetUID() );
#endif
            
                // this means "don't inherit transform from parent"
                // Scene->Registry().emplace<Scene::TransformLocalIsWorldTag>(MovableEntity);

                FighterEntity = Scene->CreateEntity( "FighterEntity", vaMatrix4x4::FromTranslation( { 0, 3750, 0 } ) );
                // resize, use "gltf orientation matrix" and rotate so X is ahead
                Scene->CreateEntity( "FighterMesh", vaMatrix4x4::Scaling( 0.1f, 0.1f, 0.1f )* vaMatrix4x4( vaMatrix3x3{ 1, 0, 0, 0, -1, 0, 0, 0, -1 } )* vaMatrix4x4::RotationZ( VA_PIf * 0.5f ), FighterEntity, FighterMesh->UIDObject_GetUID( ) );

                // show all standard asteroid meshes in a grid
#if 0
                auto standardAsteroids = Scene->CreateEntity( "StandardAsteroids", vaMatrix4x4::Identity, nullptr );
                int dimK = std::max( 1, (int)sqrt(AsteroidStandardMeshes.size()) - 1 );
                for( int i = 0; i < AsteroidStandardMeshes.size(); i++ )
                {
                    vaMatrix4x4 transform = vaMatrix4x4::FromTranslation( { ( i%dimK - dimK * 0.5f ) * 40.0f, (i/dimK - dimK * 0.5f ) * 40.0f, 0 } );
                    Scene->CreateEntity( AsteroidStandardMeshes[i]->GetParentAsset()->Name(), transform, AsteroidStandardMeshes[i], standardAsteroids ); //vaMatrix4x4::Scaling(3, 3, 3) * vaMatrix4x4::RotationX( VA_PIf * 1.0f ), UFOMesh, MovableEntity );
                }
#endif
                // Scene->CreateEntity( AsteroidStandardMeshes[5]->GetParentAsset()->Name(), vaMatrix4x4::Identity, AsteroidStandardMeshes[5], standardAsteroids ); //vaMatrix4x4::Scaling(3, 3, 3) * vaMatrix4x4::RotationX( VA_PIf * 1.0f ), UFOMesh, MovableEntity );

#if 1           // bunch of random spheres in a random hierarchy
                vaRandom rnd(0);
                OtherEntities.push_back( entt::null );  // add one null entity for testing
                for( int i = 0; i < 1000; i++ )
                    OtherEntities.push_back( Scene->CreateEntity( string("entity_")+std::to_string(i), vaMatrix4x4::Scaling(1.05f,1.05f,1.05f) * vaMatrix4x4::Translation(10 * (vaVector3::Random(rnd) * 2 - 1)), entt::null, LightSphereMesh->UIDObject_GetUID() ) );
                for( int i = 0; i < 1000; i++ )
                    Scene->SetParent( OtherEntities[rnd.NextIntRange((int)OtherEntities.size()-1)], OtherEntities[rnd.NextIntRange((int)OtherEntities.size()-1)] );
#endif
            }

            // UI
            UIPanel = std::make_shared<vaUISimplePanel>( [ globals = this ]( vaApplicationBase& application )
            {
                application;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
                // here's where all UI goes!
                ImGui::Text( "Total scene objects: %d", (int)globals->Scene->Registry( ).size( ) );
                ImGui::Checkbox( "Wireframe", &globals->Wireframe );
                ImGui::Checkbox( "Animate asteroids", &globals->AnimateAsteroids );

                //                ImGui::Separator( );
                //                ImGui::Text( "TODO: " );
                //                ImGui::Text( " [x] stars skybox / IBL! " );
#endif
            },
                "Asteroids Workspace", 0, true, vaUIPanel::DockLocation::DockedLeft );
            UIPanel->UIPanelSetFocusNextFrame( );
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

#if 0
//    This can't happen here because vaApplication ticks the NextFrame and the parallel execution would overlap it (a bug).
//    GREAT IDEA: allow for manual vaFramePtrStatic::NextFrame & disable in-app one; just simply add vaApplicationBase::ManualFramePtrNextFrame which calls NextFrame and prevents the next automatic one (but not the full ones)
    // Time to stop any async processing!
    if( globals->Scene->IsTicking( ) )
        globals->Scene->TickEnd( );
    application.ManualFramePtrNextFrame();
#endif

    // our stuff!
#if 0
    if( !application.IsMouseCaptured() )
    {
        vaInputKeyboardBase * keyboard = application.GetInputKeyboard(); keyboard;
        vaInputMouseBase * mouse = application.GetInputMouse(); mouse;
        if( keyboard != nullptr )
        {
            float x = 0.0f; float y = 0.0f; float z = 0.0f; float kspeed = 10.0f;
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
#endif

    // tick UI before scene because some of the scene UI doesn't want to happen during scene async (at the moment)
    application.TickUI( *globals->SceneMainView->Camera( ) );

    globals->Scene->TickBegin( deltaTime, application.GetCurrentTickIndex( ) );

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
                    canvas3D.DrawLine( { i * gridStep, -gridCount * gridStep, 0.0f }, { i * gridStep, +gridCount * gridStep, 0.0f }, 0x30000000 );
                    canvas3D.DrawLine( { -gridCount * gridStep, i * gridStep, 0.0f }, { +gridCount * gridStep, i * gridStep, 0.0f }, 0x30000000 );
                }
            }

            // const auto & cursorHoverInfo = globals->SceneMainView->GetCursorHoverInfo();
            // if( cursorHoverInfo.HasData )
            // {
            //      canvas3D.DrawSphere( cursorHoverInfo.WorldspacePos, cursorHoverInfo.ViewspaceDepth * 0.1f, 0xFF000000, 0xFF00FF00 );
            // }
        }

        // update and draw imgui
        application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer( ), globals->SceneMainView->GetOutputDepth() );

        // present the frame, flip the buffers, etc
        renderDevice.EndAndPresentFrame( ( application.GetVsync( ) ) ? ( 1 ) : ( 0 ) );
    }

    // End of frame; time to stop any async scene processing!
    globals->Scene->TickEnd( );
}











#if 0

extern void CreateAsteroidsFromGeospheres( AstMesh* outMesh, unsigned int subdivLevelCount, unsigned int meshInstanceCount, unsigned int rngSeed, unsigned int* outSubdivIndexOffsets, unsigned int* vertexCountPerMesh );

typedef struct D3D11_SUBRESOURCE_DATA
{
    const void* pSysMem;
    UINT SysMemPitch;
    UINT SysMemSlicePitch;
} 	D3D11_SUBRESOURCE_DATA;

void FillNoise2D_RGBA8( D3D11_SUBRESOURCE_DATA* subresources, size_t width, size_t height, size_t mipLevels,
    float seed, float persistence, float noiseScale, float noiseStrength, float redScale, float greenScale, float blueScale );


void CreateAsteroidMesh( vaRenderDevice& device, int randomSeed, shared_ptr<vaRenderMesh>mesh[MESH_MAX_SUBDIV_LEVELS + 1] )
{
    const int subdivLevelCount = MESH_MAX_SUBDIV_LEVELS;

    AstMesh mMeshes;
    unsigned int mVertexCountPerMesh;
    std::vector<unsigned int> mIndexOffsets( subdivLevelCount + 2 );
    std::mt19937 rng( randomSeed );

    CreateAsteroidsFromGeospheres( &mMeshes, subdivLevelCount, subdivLevelCount, rng( ), mIndexOffsets.data( ), &mVertexCountPerMesh );

    // vector<vaRenderMesh::StandardVertex> newVertices(mMeshes.vertices.size());
    // for (int i = 0; i < (int)mMeshes.vertices.size(); i++)
    // {
    //     vaRenderMesh::StandardVertex& svert = newVertices[i];
    //     svert.Position = vaVector3::TransformCoord(mMeshes.vertices[i].Pos, vaMatrix4x4::Identity);
    //     svert.Color = 0xFFFFFFFF;
    //     svert.Normal = vaVector4(vaVector3::TransformNormal(mMeshes.vertices[i].Norm, vaMatrix4x4::Identity), 0.0f);
    //     svert.TexCoord0 = vaVector2(0,0);
    //     svert.TexCoord1 = vaVector2(0, 0);
    // }

    for( int iLOD = 0; iLOD <= subdivLevelCount; iLOD++ )
    {
        mesh[iLOD] = device.GetMeshManager( ).CreateRenderMesh( vaCore::GUIDCreate( ), false );
        if( mesh[iLOD] == nullptr )
        {
            assert( false );
            return;
        }
        // mesh->CreateTriangleMesh(device, newVertices, mMeshes.indices);
        shared_ptr<vaRenderMesh::StandardTriangleMesh> triMesh = std::make_shared< vaRenderMesh::StandardTriangleMesh>( device );
        for( int i = mIndexOffsets[subdivLevelCount - iLOD]; i < (int)mIndexOffsets[subdivLevelCount - iLOD + 1]; i += 3 )
        {
            vaRenderMesh::StandardVertex a = AstToVaVertex( mMeshes.vertices[mMeshes.indices[i + 0]] );
            vaRenderMesh::StandardVertex b = AstToVaVertex( mMeshes.vertices[mMeshes.indices[i + 1]] );
            vaRenderMesh::StandardVertex c = AstToVaVertex( mMeshes.vertices[mMeshes.indices[i + 2]] );
            triMesh->AddTriangleMergeDuplicates<vaRenderMesh::StandardVertex>( a, b, c, vaRenderMesh::StandardVertex::IsDuplicate, 128 );
        }

        mesh[iLOD]->SetTriangleMesh( triMesh );
        mesh[iLOD]->SetFrontFaceWindingOrder( vaWindingOrder::CounterClockwise );

        if( true )
        {
            assert( vaThreading::IsMainThread( ) ); // warning, potential bug - don't automatically start tracking if adding from another thread; rather finish initialization completely and then manually call UIDObject_Track
            mesh[iLOD]->UIDObject_Track( ); // needs to be registered to be visible/searchable by various systems such as rendering
        }
    }

}

template <typename T>
inline T Align( T v, T align )
{
    return ( v + ( align - 1 ) ) & ~( align - 1 );
}


shared_ptr<vaTexture> CreateTexture( vaRenderDevice& device, int randomSeed )
{
    device; randomSeed;

    const unsigned int mTextureDim = 512;
    //const unsigned int mTextureCount        
    const unsigned int mTextureArraySize = 3;    // triplanar projection
    const unsigned int mTextureMipLevels = vaMath::FloorLog2( mTextureDim );

    const int textureCount = 1;

    auto SubresourceIndex = [ mTextureMipLevels, mTextureArraySize ]( unsigned int texture, unsigned int arrayElement = 0, unsigned int mip = 0 ) -> unsigned int
    {
        return mip + mTextureMipLevels * ( arrayElement + mTextureArraySize * texture );
    };

    std::vector<byte> mTextureDataBuffer;
    std::vector<D3D11_SUBRESOURCE_DATA> mTextureSubresources;

    assert( ( mTextureDim & ( mTextureDim - 1 ) ) == 0 ); // Must be pow2 currently; we don't handle wacky mip chains

    // std::cout
    //     << "Creating " << textureCount << " "
    //     << mTextureDim << "x" << mTextureDim << " textures..." << std::endl;

    // Allocate space
    UINT texelSizeInBytes = 4; // RGBA8
    UINT extraSpaceForMips = 2;
    UINT totalTextureSizeInBytes = texelSizeInBytes * mTextureDim * mTextureDim * mTextureArraySize * extraSpaceForMips;
    totalTextureSizeInBytes = Align( totalTextureSizeInBytes, 64U ); // Avoid false sharing

    mTextureDataBuffer.resize( totalTextureSizeInBytes * textureCount );
    mTextureSubresources.resize( mTextureArraySize * mTextureMipLevels * textureCount );

    // Parallel over textures
    std::vector<unsigned int> rngSeeds( textureCount );
    {
        std::mt19937 seeds;
        for( auto& i : rngSeeds ) i = seeds( );
    }

    //concurrency::parallel_for( UINT( 0 ), textureCount, [ & ]( UINT t )
    UINT t = 0;
    {
        std::mt19937 rng( rngSeeds[t] );
        auto randomNoise = std::uniform_real_distribution<float>( 0.0f, 10000.0f );
        auto randomNoiseScale = std::uniform_real_distribution<float>( 100, 150 );
        auto randomPersistence = std::normal_distribution<float>( 0.9f, 0.2f );

        BYTE* data = mTextureDataBuffer.data( ) + t * totalTextureSizeInBytes;
        for( UINT a = 0; a < mTextureArraySize; ++a ) {
            for( UINT m = 0; m < mTextureMipLevels; ++m ) {
                auto width = mTextureDim >> m;
                auto height = mTextureDim >> m;

                D3D11_SUBRESOURCE_DATA initialData = {};
                initialData.pSysMem = data;
                initialData.SysMemPitch = width * texelSizeInBytes;
                mTextureSubresources[SubresourceIndex( t, a, m )] = initialData;

                data += initialData.SysMemPitch * height;
            }
        }

        // Use same parameters for each of the tri-planar projection planes/cube map faces/etc.
        float noiseScale = randomNoiseScale( rng ) / float( mTextureDim );
        float persistence = randomPersistence( rng );
        float strength = 1.5f;
        noiseScale; persistence; strength;

        for( UINT a = 0; a < mTextureArraySize; ++a ) {
            float redScale = 255.0f;
            float greenScale = 255.0f;
            float blueScale = 255.0f;

            redScale; greenScale; blueScale;

            // DEBUG colors
#if 0
            redScale = t & 1 ? 255.0f : 0.0f;
            greenScale = t & 2 ? 255.0f : 0.0f;
            blueScale = t & 4 ? 255.0f : 0.0f;
#endif

            FillNoise2D_RGBA8( &mTextureSubresources[SubresourceIndex( t, a )], mTextureDim, mTextureDim, mTextureMipLevels,
                randomNoise( rng ), persistence, noiseScale, strength, redScale, greenScale, blueScale );
        }
    }; // ); // parallel_for

    auto subRes = mTextureSubresources[SubresourceIndex( 0, 0, 0 )];

    shared_ptr<vaTexture> texture = vaTexture::Create2D( device, vaResourceFormat::R8G8B8A8_UNORM_SRGB, mTextureDim, mTextureDim, /*mTextureMipLevels*/1, 1, 1, vaResourceBindSupportFlags::ShaderResource,
        vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic,
        vaTextureFlags::None, vaTextureContentsType::GenericColor,
        subRes.pSysMem, subRes.SysMemPitch );

    return texture;
}

// Very simple multi-octave simplex noise helper
// Returns noise in the range [0, 1] vs. the usual [-1, 1]
template <size_t N = 4>
class NoiseOctaves
{
private:
    float mWeights[N];
    float mWeightNorm;

public:
    NoiseOctaves( float persistence = 0.5f )
    {
        float weightSum = 0.0f;
        for( size_t i = 0; i < N; ++i ) {
            mWeights[i] = persistence;
            weightSum += persistence;
            persistence *= persistence;
        }
        mWeightNorm = 0.5f / weightSum; // Will normalize to [-0.5, 0.5]
    }

    // Returns [0, 1]
    float operator()( float x, float y, float z ) const
    {
        float r = 0.0f;
        for( size_t i = 0; i < N; ++i ) {
            r += mWeights[i] * snoise3( x, y, z );
            x *= 2.0f; y *= 2.0f; z *= 2.0f;
        }
        return r * mWeightNorm + 0.5f;
    }

    // Returns [0, 1]
    float operator()( float x, float y, float z, float w ) const
    {
        float r = 0.0f;
        for( size_t i = 0; i < N; ++i ) {
            r += mWeights[i] * snoise4( x, y, z, w );
            x *= 2.0f; y *= 2.0f; z *= 2.0f; w *= 2.0f;
        }
        return r * mWeightNorm + 0.5f;
    }
};


void CreateIcosahedron( AstMesh* outMesh )
{
    static const float a = std::sqrt( 2.0f / ( 5.0f - std::sqrt( 5.0f ) ) );
    static const float b = std::sqrt( 2.0f / ( 5.0f + std::sqrt( 5.0f ) ) );

    static const size_t num_vertices = 12;
    static const AstVertex vertices[num_vertices] = // x, y, z
    {
        {{-b,  a,  0}},
        {{ b,  a,  0}},
        {{-b, -a,  0}},
        {{ b, -a,  0}},
        {{ 0, -b,  a}},
        {{ 0,  b,  a}},
        {{ 0, -b, -a}},
        {{ 0,  b, -a}},
        {{ a,  0, -b}},
        {{ a,  0,  b}},
        {{-a,  0, -b}},
        {{-a,  0,  b}},
    };

    static const size_t num_triangles = 20;
    static const IndexType indices[num_triangles * 3] =
    {
         0,  5, 11,
         0,  1,  5,
         0,  7,  1,
         0, 10,  7,
         0, 11, 10,
         1,  9,  5,
         5,  4, 11,
        11,  2, 10,
        10,  6,  7,
         7,  8,  1,
         3,  4,  9,
         3,  2,  4,
         3,  6,  2,
         3,  8,  6,
         3,  9,  8,
         4,  5,  9,
         2, 11,  4,
         6, 10,  2,
         8,  7,  6,
         9,  1,  8,
    };

    outMesh->clear( );
    outMesh->vertices.insert( outMesh->vertices.end( ), vertices, vertices + num_vertices );
    outMesh->indices.insert( outMesh->indices.end( ), indices, indices + num_triangles * 3 );
}

void ComputeAvgNormalsInPlace( AstMesh* outMesh )
{
    for( auto& v : outMesh->vertices ) {
        v.Norm.x = 0.0f;
        v.Norm.y = 0.0f;
        v.Norm.z = 0.0f;
    }

    assert( outMesh->indices.size( ) % 3 == 0 ); // trilist
    size_t triangles = outMesh->indices.size( ) / 3;
    for( size_t t = 0; t < triangles; ++t )
    {
        auto v1 = &outMesh->vertices[outMesh->indices[t * 3 + 0]];
        auto v2 = &outMesh->vertices[outMesh->indices[t * 3 + 1]];
        auto v3 = &outMesh->vertices[outMesh->indices[t * 3 + 2]];

        // Two edge vectors u,v
        auto ux = v2->Pos.x - v1->Pos.x;
        auto uy = v2->Pos.y - v1->Pos.y;
        auto uz = v2->Pos.z - v1->Pos.z;
        auto vx = v3->Pos.x - v1->Pos.x;
        auto vy = v3->Pos.y - v1->Pos.y;
        auto vz = v3->Pos.z - v1->Pos.z;

        // cross(u,v)
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;

        // Do not normalize... weight average by contributing face area
        v1->Norm.x += nx; v1->Norm.y += ny; v1->Norm.z += nz;
        v2->Norm.x += nx; v2->Norm.y += ny; v2->Norm.z += nz;
        v3->Norm.x += nx; v3->Norm.y += ny; v3->Norm.z += nz;
    }

    // Normalize
    for( auto& v : outMesh->vertices ) {
        float n = 1.0f / std::sqrt( v.Norm.x * v.Norm.x + v.Norm.y * v.Norm.y + v.Norm.z * v.Norm.z );
        v.Norm.x *= n;
        v.Norm.y *= n;
        v.Norm.z *= n;
    }
}

// Maps edge (lower index first!) to
struct Edge
{
    Edge( IndexType i0, IndexType i1 )
        : v0( i0 ), v1( i1 )
    {
        if( v0 > v1 )
            std::swap( v0, v1 );
    }
    IndexType v0;
    IndexType v1;

    bool operator<( const Edge& c ) const
    {
        return v0 < c.v0 || ( v0 == c.v0 && v1 < c.v1 );
    }
};

typedef std::map<Edge, IndexType> MidpointMap;

inline IndexType EdgeMidpoint( AstMesh* mesh, MidpointMap* midpoints, Edge e )
{
    auto index = midpoints->find( e );
    if( index == midpoints->end( ) )
    {
        auto a = mesh->vertices[e.v0];
        auto b = mesh->vertices[e.v1];

        AstVertex m;
        m.Pos.x = ( a.Pos.x + b.Pos.x ) * 0.5f;
        m.Pos.y = ( a.Pos.y + b.Pos.y ) * 0.5f;
        m.Pos.z = ( a.Pos.z + b.Pos.z ) * 0.5f;

        index = midpoints->insert( std::make_pair( e, static_cast<IndexType>( mesh->vertices.size( ) ) ) ).first;
        mesh->vertices.push_back( m );
    }
    return index->second;
}


void SubdivideInPlace( AstMesh* outMesh )
{
    MidpointMap midpoints;

    std::vector<IndexType> newIndices;
    newIndices.reserve( outMesh->indices.size( ) * 4 );
    outMesh->vertices.reserve( outMesh->vertices.size( ) * 2 );

    assert( outMesh->indices.size( ) % 3 == 0 ); // trilist
    size_t triangles = outMesh->indices.size( ) / 3;
    for( size_t t = 0; t < triangles; ++t )
    {
        auto t0 = outMesh->indices[t * 3 + 0];
        auto t1 = outMesh->indices[t * 3 + 1];
        auto t2 = outMesh->indices[t * 3 + 2];

        auto m0 = EdgeMidpoint( outMesh, &midpoints, Edge( t0, t1 ) );
        auto m1 = EdgeMidpoint( outMesh, &midpoints, Edge( t1, t2 ) );
        auto m2 = EdgeMidpoint( outMesh, &midpoints, Edge( t2, t0 ) );

        IndexType indices[] = {
            t0, m0, m2,
            m0, t1, m1,
            m0, m1, m2,
            m2, m1, t2,
        };
        newIndices.insert( newIndices.end( ), indices, indices + 4 * 3 );
    }

    std::swap( outMesh->indices, newIndices ); // Constant time
}


void SpherifyInPlace( AstMesh* outMesh, float radius )
{
    for( auto& v : outMesh->vertices ) {
        float n = radius / std::sqrt( v.Pos.x * v.Pos.x + v.Pos.y * v.Pos.y + v.Pos.z * v.Pos.z );
        v.Pos.x *= n;
        v.Pos.y *= n;
        v.Pos.z *= n;
    }
}

void CreateGeospheres( AstMesh* outMesh, unsigned int subdivLevelCount, unsigned int* outSubdivIndexOffsets )
{
    CreateIcosahedron( outMesh );
    outSubdivIndexOffsets[0] = 0;

    std::vector<AstVertex> vertices( outMesh->vertices );
    std::vector<IndexType> indices( outMesh->indices );

    for( unsigned int i = 0; i < subdivLevelCount; ++i ) {
        outSubdivIndexOffsets[i + 1] = (unsigned int)indices.size( );
        SubdivideInPlace( outMesh );

        // Ensure we add the proper offset to the indices from this subdiv level for the combined mesh
        // This avoids also needing to track a base vertex index for each subdiv level
        IndexType vertexOffset = (IndexType)vertices.size( );
        vertices.insert( vertices.end( ), outMesh->vertices.begin( ), outMesh->vertices.end( ) );

        for( auto newIndex : outMesh->indices ) {
            indices.push_back( newIndex + vertexOffset );
        }
    }
    outSubdivIndexOffsets[subdivLevelCount + 1] = (unsigned int)indices.size( );

    SpherifyInPlace( outMesh, 1.0f );

    // Put the union of vertices/indices back into the mesh object
    std::swap( outMesh->indices, indices );
    std::swap( outMesh->vertices, vertices );
}


void CreateAsteroidsFromGeospheres( AstMesh* outMesh,
    unsigned int subdivLevelCount, unsigned int meshInstanceCount,
    unsigned int rngSeed,
    unsigned int* outSubdivIndexOffsets, unsigned int* vertexCountPerMesh )
{
    assert( subdivLevelCount <= meshInstanceCount );

    std::mt19937 rng( rngSeed );

    AstMesh baseMesh;
    CreateGeospheres( &baseMesh, subdivLevelCount, outSubdivIndexOffsets );

    // Per unique mesh
    *vertexCountPerMesh = (unsigned int)baseMesh.vertices.size( );
    std::vector<AstVertex> vertices;
    vertices.reserve( meshInstanceCount * baseMesh.vertices.size( ) );
    // Reuse indices for the different unique meshes

    auto randomNoise = std::uniform_real_distribution<float>( 0.0f, 10000.0f );
    auto randomPersistence = std::normal_distribution<float>( 0.95f, 0.04f );
    float noiseScale = 0.5f;
    float radiusScale = 0.9f;
    float radiusBias = 0.3f;

    // Create and randomize unique vertices for each mesh instance
    for( unsigned int m = 0; m < meshInstanceCount; ++m ) {
        AstMesh newMesh( baseMesh );
        NoiseOctaves<4> textureNoise( randomPersistence( rng ) );
        float noise = randomNoise( rng );

        for( auto& v : newMesh.vertices ) {
            float radius = textureNoise( v.Pos.x * noiseScale, v.Pos.y * noiseScale, v.Pos.z * noiseScale, noise );
            radius = radius * radiusScale + radiusBias;
            v.Pos.x *= radius;
            v.Pos.y *= radius;
            v.Pos.z *= radius;
        }
        ComputeAvgNormalsInPlace( &newMesh );

        vertices.insert( vertices.end( ), newMesh.vertices.begin( ), newMesh.vertices.end( ) );
    }

    // Copy to output
    std::swap( outMesh->indices, baseMesh.indices );
    std::swap( outMesh->vertices, vertices );
}

void GenerateMips2D_XXXX8( D3D11_SUBRESOURCE_DATA* subresources, size_t widthLevel0, size_t heightLevel0, size_t mipLevels )
{
    for( size_t m = 1; m < mipLevels; ++m ) {
        auto rowPitchSrc = subresources[m - 1].SysMemPitch;
        const BYTE* dataSrc = (BYTE*)subresources[m - 1].pSysMem;

        auto rowPitchDst = subresources[m].SysMemPitch;
        BYTE* dataDst = (BYTE*)subresources[m].pSysMem;

        auto width = widthLevel0 >> m;
        auto height = heightLevel0 >> m;

        // Iterating byte-wise is simpler in this case (pulls apart color nicely)
        // Not optimized at all, obviously...
        for( size_t y = 0; y < height; ++y ) {
            auto rowSrc0 = ( dataSrc + ( y * 2 + 0 ) * rowPitchSrc );
            auto rowSrc1 = ( dataSrc + ( y * 2 + 1 ) * rowPitchSrc );
            auto rowDst = ( dataDst + (y)*rowPitchDst );
            for( size_t x = 0; x < width; ++x ) {
                for( size_t comp = 0; comp < 4; ++comp ) {
                    uint32_t c = rowSrc0[x * 8 + comp + 0];
                    c += rowSrc0[x * 8 + comp + 4];
                    c += rowSrc1[x * 8 + comp + 0];
                    c += rowSrc1[x * 8 + comp + 4];
                    c = c / 4;
                    assert( c < 256 );
                    rowDst[4 * x + comp] = (byte)c;
                }
            }
        }
    }
}


void FillNoise2D_RGBA8( D3D11_SUBRESOURCE_DATA* subresources, size_t width, size_t height, size_t mipLevels,
    float seed, float persistence, float noiseScale, float noiseStrength,
    float redScale, float greenScale, float blueScale )
{
    NoiseOctaves<4> textureNoise( persistence );

    // Level 0
    for( size_t y = 0; y < height; ++y ) {
        uint32_t* row = (uint32_t*)( (BYTE*)subresources[0].pSysMem + y * subresources[0].SysMemPitch );
        for( size_t x = 0; x < width; ++x ) {
            auto c = textureNoise( (float)x * noiseScale, (float)y * noiseScale, seed );
            c = std::max( 0.0f, std::min( 1.0f, ( c - 0.5f ) * noiseStrength + 0.5f ) );

            int32_t cr = (int32_t)( c * redScale );
            int32_t cg = (int32_t)( c * greenScale );
            int32_t cb = (int32_t)( c * blueScale );
            assert( cr >= 0 && cr < 256 );
            assert( cg >= 0 && cg < 256 );
            assert( cb >= 0 && cb < 256 );

            row[x] = ( cr ) << 16 | ( cg ) << 8 | ( cb ) << 0;
        }
    }

    if( mipLevels > 1 )
        GenerateMips2D_XXXX8( subresources, width, height, mipLevels );
}

#endif

void EnTTTest( )
{
#if 0
    entt::registry registry;

    auto a = registry.create();
    auto b = registry.create( );
    auto c = registry.create( );

    registry.emplace<Scene::Name>( a, "A" );
    registry.emplace<Scene::Name>( b, "B" );
    registry.emplace<Scene::Name>( c, "C" );
    
    registry.emplace<Scene::TransformDirtyTag>( a );
    registry.emplace<Scene::TransformDirtyTag>( b );

    registry.emplace<Scene::WorldBoundsDirtyTag>( a );
    registry.emplace<Scene::WorldBoundsDirtyTag>( b );
    registry.emplace<Scene::WorldBoundsDirtyTag>( c );

    registry.destroy( b );

    auto d = registry.create( );
    registry.emplace<Scene::WorldBoundsDirtyTag>( d );

    registry.destroy( a );

    auto viewWorldBoundsDirty = registry.view<Scene::WorldBoundsDirtyTag>( );
    viewWorldBoundsDirty.each( [ & ]( entt::entity entity )
    {
        assert( registry.valid( entity ) );
    } );
#endif
}

