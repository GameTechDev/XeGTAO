///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vaSceneRenderInstanceProcessor.h"

#include "Scene/vaScene.h"

#include "Rendering/vaAssetPack.h"
#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"
#include "Rendering/vaSceneRenderer.h"

#include "Rendering/vaRenderInstanceList.h"

#include <future>

//#undef VA_TASKFLOW_INTEGRATION_ENABLED

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
#include "IntegratedExternals/vaTaskflowIntegration.h"
#endif

namespace Vanilla
{
    struct vaSceneRenderInstanceProcessorLocalContext
    {
        vaSceneRenderInstanceProcessor &        This;
        vaScene &                               Scene;
        entt::basic_view< entt::entity, entt::exclude_t<>, const Scene::WorldBounds>
                                                BoundsView;
        shared_ptr<vaRenderInstanceStorage>     InstanceStorage;
        int64                                   ApplicationTickIndex;

        ShaderInstanceConstants *               UploadConstants = nullptr;
        vaRenderInstance *                      InstanceArray   = nullptr;

        std::atomic_uint32_t                    InstanceCounter = 0;
        uint32                                  MaxInstances    = 0;

        vaSceneRenderInstanceProcessorLocalContext( vaSceneRenderInstanceProcessor & _this, vaScene & scene, const entt::basic_view< entt::entity, entt::exclude_t<>, const Scene::WorldBounds> & boundsView, const shared_ptr<vaRenderInstanceStorage> & instanceStorage, int64 applicationTickIndex )
            : This(_this), Scene(scene), BoundsView(boundsView), InstanceStorage(instanceStorage), ApplicationTickIndex(applicationTickIndex) { }
    };
}


using namespace Vanilla;

vaSceneRenderInstanceProcessor::vaSceneRenderInstanceProcessor( vaSceneRenderer & sceneRenderer ) : m_sceneRenderer( sceneRenderer )
{
    assert( vaThreading::IsMainThread( ) );
    assert( !m_inAsync );
}

vaSceneRenderInstanceProcessor::~vaSceneRenderInstanceProcessor( ) 
{ 
    //SetScene( nullptr );
    assert( !m_inAsync );
}

void vaSceneRenderInstanceProcessor::PreSelectionProc( struct vaSceneRenderInstanceProcessorLocalContext & localContext )
{
    m_sceneRenderer.PrepareInstanceBatchProcessing( localContext.MaxInstances );
}

void vaSceneRenderInstanceProcessor::SelectionProc( vaSceneRenderInstanceProcessorLocalContext & localContext, uint32 entityBegin, uint32 entityEnd )
{
    vaSceneRenderInstanceProcessor::SceneItem localList[c_ConcurrentChuckMaxItemCount];
    int localCount = 0;

    entt::registry & registry = localContext.Scene.Registry();
    entt::basic_view< entt::entity, entt::exclude_t<>, const Scene::WorldBounds> & registryView = localContext.BoundsView;
    const auto & cregistry = std::as_const( registry ); 

    // this is a _shared_ lock for using vaUIDObjectRegistrar::FindFPNoMutexLock 
    {
        std::shared_lock mapLock( vaUIDObjectRegistrar::Mutex( ) );
        assert( m_inAsync );
        float rtanfovhy = 1.0f / std::tanf( m_LODSettings.ReferenceYFOV * 0.5f );

        for( uint32 index = entityBegin; index < entityEnd; index++ )
        {
            entt::entity entity = registryView[index];

            // in theory this shouldn't happen; in practice it does and until the reason is found let's just assert and not crash
            if( !cregistry.all_of<Scene::WorldBounds, Scene::TransformWorld>( entity ) )
            {
                assert( false );
                continue;
            }

            // we're guaranteed that these components exist at this point (caller should have an assert for it too)
            const Scene::WorldBounds &      worldBounds     = cregistry.get<Scene::WorldBounds>( entity );
            //const Scene::TransformWorld &   worldTransform  = cregistry.get<Scene::TransformWorld>( entity );

            vaFramePtr<vaRenderMesh> renderMesh = nullptr;
            const Scene::RenderMesh * renderMeshComponent   = cregistry.try_get<Scene::RenderMesh>( entity );
            if( renderMeshComponent != nullptr )
            {
                renderMesh = vaUIDObjectRegistrar::FindFPNoMutexLock<vaRenderMesh>( renderMeshComponent->MeshUID );
                if( renderMesh == nullptr )
                {
                    Report( vaDrawResultFlags::AssetsStillLoading );
                    continue;
                }
                // one should really lock the render mesh here

                // resolve material here - both easier to manage and faster (when this gets parallelized)
                auto materialID = (renderMeshComponent->OverrideMaterialUID != vaGUID::Null)?(renderMeshComponent->OverrideMaterialUID):(renderMesh->GetMaterialID());
                vaFramePtr<vaRenderMaterial> renderMaterial;
                if( materialID.IsNull( ) )
                    renderMaterial = vaFramePtr<vaRenderMaterial>( renderMesh->GetRenderDevice( ).GetMaterialManager( ).GetDefaultMaterial( ) );
                else
                    renderMaterial = vaUIDObjectRegistrar::FindFPNoMutexLock<vaRenderMaterial>( materialID );

                // If no material, it's still loading (or at least I think so?) so report it and get the default one.
                if( renderMaterial == nullptr )
                {
                    Report( vaDrawResultFlags::AssetsStillLoading );
                    renderMaterial = vaFramePtr<vaRenderMaterial>( renderMesh->GetManager( ).GetRenderDevice( ).GetMaterialManager( ).GetDefaultMaterial( ) );
                }

                // Figure out 'reference distance' (distance from the bounding sphere center to the LOD reference point, which is usually the main camera position)
                const vaBoundingSphere & bs = worldBounds.BS;
                float distSq    = ( bs.Center - m_LODSettings.Reference ).LengthSq( );
                float dist      = std::sqrtf( distSq );
                if( (dist - bs.Radius) > std::min( m_LODSettings.MaxViewDistance, renderMeshComponent->VisibilityRange ) )
                    continue;

                // Compute LOD scaling factor: 
                // 1.) get rough bounding sphere and do approx projection to screen (valid only at screen center but we want that - don't want LODs to change as we turn around)
                // 2.) we then use 1 / screen projected to compute LODRangeFactor which is effectively "1 / boundsScreenYSize" and use that to find the closest LOD!
                // 3.) also further scale by filter.LODReferenceScale which can be (but doesn't have to be) resolution dependent!
                // 4.) code is a mod of https://stackoverflow.com/questions/21648630/radius-of-projected-sphere-in-screen-space :)
                float sbsr = rtanfovhy * bs.Radius / std::sqrtf( std::max( 0.001f, distSq - bs.Radius * bs.Radius ) );
                float LODRangeFactor = 1.0f / ( sbsr * m_LODSettings.Scale );

                // Figure out the correct mesh LOD based on mesh settings
                float meshLOD = renderMesh->FindLOD( LODRangeFactor );
                const std::vector<vaRenderMesh::LODPart> & LODParts = renderMesh->GetLODParts();
                if( renderMesh->HasOverrideLODLevel( localContext.ApplicationTickIndex ) )
                    meshLOD = renderMesh->GetOverrideLODLevel( );
                int LODPartCount             = std::min( (int)LODParts.size(), vaRenderMesh::LODPart::MaxLODParts );
                if( LODParts.size() == 0 || LODParts[0].IndexCount == 0 )
                    { assert( false ); Report( vaDrawResultFlags::UnspecifiedError ); };   // should this assert? I guess empty mesh is valid? Or not really?
                meshLOD = vaMath::Clamp( meshLOD, 0.0f, (float)(LODPartCount - 1) );

                bool isDecal = renderMaterial->GetMaterialSettings( ).LayerMode == vaLayerMode::Decal;

                bool showAsSelected = false;
                showAsSelected      |= renderMesh->GetUIShowSelectedAppTickIndex( ) >= localContext.ApplicationTickIndex;
                showAsSelected      |= renderMaterial->GetUIShowSelectedAppTickIndex( ) >= localContext.ApplicationTickIndex;

                localList[localCount++] = { entity, renderMesh, renderMaterial, dist, meshLOD, false, isDecal, showAsSelected };
            }
        }
    }

    if( localCount == 0 )
        return;

    uint32 baseInstanceIndex = localContext.InstanceCounter.fetch_add( localCount );

    m_sceneRenderer.ProcessInstanceBatch( localList, localCount, baseInstanceIndex );

    for( int i = 0; i < localCount; i++ )
    {
        auto & item = localList[i];
        auto & renderInstance = localContext.InstanceArray[baseInstanceIndex+i];
        if( !item.IsUsed )
        {
            renderInstance.Mesh     = nullptr;
            renderInstance.Material = nullptr;
            continue;
        }

        m_uniqueMeshes.Insert( item.Mesh );
        m_uniqueMaterials.Insert( item.Material );
        
        const Scene::TransformWorld & worldTransform = cregistry.get<Scene::TransformWorld>( item.Entity );

        renderInstance.Transform        = worldTransform;
        renderInstance.EmissiveAdd      = vaVector4( 0.0f, 0.0f, 0.0f, 1.0f );
        renderInstance.Mesh             = item.Mesh;
        renderInstance.Material         = item.Material;
        renderInstance.MeshLOD          = item.MeshLOD;
        renderInstance.DistanceFromRef  = item.DistanceFromRef;
        renderInstance.Flags.IsDecal    = item.IsDecal;

        renderInstance.OriginInfo.EntityID          = (uint32)(item.Entity);                        assert( static_cast<uint64>(item.Entity) < 0xFFFFFFFF );
        renderInstance.OriginInfo.SceneID           = (uint32)(localContext.Scene.RuntimeIDGet());  assert( localContext.Scene.RuntimeIDGet() < 0xFFFFFFFF );
        renderInstance.OriginInfo.MeshAssetID       = ( renderInstance.Mesh->GetParentAsset( ) == nullptr ) ? ( 0xFFFFFFFF ) : ( static_cast<uint32>( renderInstance.Mesh->GetParentAsset( )->RuntimeIDGet( ) ) );
        renderInstance.OriginInfo.MaterialAssetID   = ( renderInstance.Material->GetParentAsset( ) == nullptr ) ? ( 0xFFFFFFFF ) : ( static_cast<uint32>( renderInstance.Material->GetParentAsset( )->RuntimeIDGet( ) ) );

        const Scene::MaterialPicksLightEmissive* materialPicksLightEmissive = cregistry.try_get<Scene::MaterialPicksLightEmissive>( item.Entity );
        if( materialPicksLightEmissive != nullptr )
        {
            const Scene::LightPoint* lightPoint = cregistry.try_get<Scene::LightPoint>( item.Entity );
            if( lightPoint != nullptr )
                renderInstance.EmissiveAdd = vaVector4( lightPoint->Color * ( lightPoint->Intensity * materialPicksLightEmissive->IntensityMultiplier ), materialPicksLightEmissive->OriginalMultiplier );
        }

        // if( isWireframe )
        // {
        //     if( isTransparent )
        //         instanceConsts.EmissiveAdd = vaVector4( 0.0f, 0.5f, 0.0f, 0.0f );
        //     else
        //         instanceConsts.EmissiveAdd = vaVector4( 0.5f, 0.0f, 0.0f, 0.0f );
        // }

        if( item.ShowAsSelected )
        {
            float highlight = 0.5f * (float)vaMath::Sin( localContext.Scene.GetTime( ) * VA_PI * 2.0 ) + 0.5f;
            renderInstance.EmissiveAdd = vaVector4( highlight*0.8f, highlight*0.9f, highlight*1.0f, highlight );
        }

        // Finally, upload to shader constants
        //ShaderInstanceConstants instanceConstants;
        //renderInstance.WriteToShaderConstants( instanceConstants );
        //memcpy( &localContext.UploadConstants[baseInstanceIndex+i], &instanceConstants, sizeof(instanceConstants) );
        renderInstance.WriteToShaderConstants( localContext.UploadConstants[baseInstanceIndex+i] );
    }
}

void vaSceneRenderInstanceProcessor::ScheduleSelection( const vaLODSettings & LODSettings, vaScene & scene, const shared_ptr<vaRenderInstanceStorage> & instanceStorage, int64 applicationTickIndex )
{
    m_LODSettings = LODSettings;

    m_selectResults = (uint32)vaDrawResultFlags::None;

    assert( !m_inAsync );
    assert( m_instanceCount == 0 );
    m_asyncFinalized = false;
    m_canConsume = false;

    assert( m_currentInstanceStorage == nullptr && instanceStorage != nullptr );
    m_currentInstanceStorage = instanceStorage;

    // TODO: use some kind of object pool here
    vaSceneRenderInstanceProcessorLocalContext * localContext = new vaSceneRenderInstanceProcessorLocalContext( *this, scene, scene.Registry().view<const Scene::WorldBounds>( ), instanceStorage, applicationTickIndex );

    auto narrowBefore = [ localContext ]( vaScene::ConcurrencyContext& ) noexcept -> std::pair<uint32, uint32>
    {
        assert( localContext->This.m_inAsync );

        assert( !localContext->This.m_canConsume );
        localContext->This.m_uniqueMeshes.StartAppending();
        localContext->This.m_uniqueMaterials.StartAppending();

        localContext->MaxInstances      = (uint32)localContext->BoundsView.size( );
        localContext->InstanceStorage->StartWriting( localContext->MaxInstances );
        localContext->UploadConstants   = localContext->InstanceStorage->GetShaderConstantsUploadArray();
        localContext->InstanceArray     = localContext->InstanceStorage->GetInstanceArray();
        assert( localContext->InstanceStorage->GetInstanceMaxCount() >= localContext->MaxInstances );

        localContext->This.PreSelectionProc( *localContext );

        // tell scene work manager how to do the parallel for loop (see 'wide' below)
        return std::make_pair( localContext->MaxInstances, (uint32)c_ConcurrentChuckMaxItemCount );
    };

    auto wide = [ localContext ]( int begin, int end, vaScene::ConcurrencyContext& ) noexcept
    {
        assert( !localContext->This.m_uniqueMeshes.IsConsuming() );
        assert( !localContext->This.m_uniqueMaterials.IsConsuming() );

        localContext->This.SelectionProc( *localContext, begin, end );
    };

    // this one has to happen on the main thread because "vaRenderInstanceList::Stop" spawns its own taskflow stuff and that's not allowed to happen from non-master thread
    auto narrowAfter = [ &outInstanceCount = m_instanceCount, localContext ]( vaScene::ConcurrencyContext & )
    {
        assert( localContext->InstanceCounter <= localContext->MaxInstances );

        assert( localContext->This.m_inAsync );
        assert( !localContext->This.m_asyncFinalized );
        localContext->This.m_asyncFinalized = true;

        assert( localContext->This.m_inAsync );
        assert( localContext->This.m_asyncFinalized );  // have you waited for "render_selections"?

        outInstanceCount.store( localContext->InstanceCounter );

        localContext->This.m_uniqueMeshes.StartConsuming();
        localContext->This.m_uniqueMaterials.StartConsuming();

        localContext->This.m_inAsync = false;
        assert( !localContext->This.m_canConsume );
        localContext->This.m_canConsume = true;

        delete localContext;
    };

    auto workAdded = scene.AddWork< const Scene::WorldBounds, const Scene::TransformWorld, const Scene::RenderMesh, const Scene::MaterialPicksLightEmissive, const Scene::LightPoint,
        const Scene::Name, const Scene::Relationship, const Scene::IgnoreByIBLTag>
        ( "SelectForRendering", "render_selections", "render_selections", std::move( narrowBefore ), std::move( wide ), std::move( narrowAfter ), nullptr );
    assert( workAdded ); workAdded;

    m_inAsync = true;
}

void vaSceneRenderInstanceProcessor::FinalizeSelectionAndPreRenderUpdate( vaRenderDeviceContext & renderContext, const shared_ptr<vaSceneRaytracing> & raytracer )
{
    assert( !m_inAsync );
    VA_TRACE_CPUGPU_SCOPE( SceneRenderInstanceProcessor, renderContext );

    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: PreRenderUpdate should return 'true' if ready to use and 'false' if any of the shaders didn't (yet) compile or anything isn't 
    // TODO: and this should be returned back to the scene renderer for the state testing
    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

    // Process all meshes
    {
        VA_TRACE_CPUGPU_SCOPE( Meshes, renderContext );
        // TODO: this could maybe be split into multithreaded and non-threaded parts it ever needed
        for( vaFramePtr<vaRenderMesh> meshPtr : m_uniqueMeshes.Elements() )
            meshPtr->PreRenderUpdate( renderContext );
    }

    // Process all materials
    {
        VA_TRACE_CPUGPU_SCOPE( Materials, renderContext );
        // TODO: this could maybe be split into multithreaded and non-threaded parts it ever needed
        for( vaFramePtr<vaRenderMaterial> materialPtr : m_uniqueMaterials.Elements() )
            materialPtr->PreRenderUpdate( renderContext );
    }

    {
        VA_TRACE_CPUGPU_SCOPE( InstanceStorage, renderContext );
        assert( m_currentInstanceStorage != nullptr );
        m_currentInstanceStorage->StopAndUpload( renderContext, m_instanceCount );
        m_currentInstanceStorage = nullptr;
    }

    if( raytracer != nullptr )
        raytracer->PreRenderUpdate( renderContext, m_uniqueMeshes.Elements(), m_uniqueMaterials.Elements() );
}

void vaSceneRenderInstanceProcessor::PostRenderCleanup( )
{
    assert( !m_inAsync );
    m_uniqueMeshes.Clear();
    m_uniqueMaterials.Clear();
    m_instanceCount = 0;
}

