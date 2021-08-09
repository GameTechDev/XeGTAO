///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Rendering/vaSceneRaytracing.h"

#include "Rendering/vaSceneRenderer.h"

#include "vaRenderMesh.h"
#include "vaRenderMaterial.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Scene/vaScene.h"


using namespace Vanilla;

vaSceneRaytracing::vaSceneRaytracing( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params ),
    vaUIPanel( "SceneRaytracer", 2, true, vaUIPanel::DockLocation::DockedLeft, "SceneRaytracers" ),
    m_sceneRenderer( *((vaSceneRenderer *)params.UserParam0) )
{
}

vaSceneRaytracing::~vaSceneRaytracing( )
{

}

const shared_ptr<vaRenderBuffer> & vaSceneRaytracing::GetScratch( uint64 minSize )
{
    if( m_scratchResource == nullptr || m_scratchResource->GetDataSize() < minSize )
    {
        uint64 size = vaMath::Align( minSize, 1024 );
        m_scratchResource = vaRenderBuffer::Create( GetRenderDevice( ), size, 1, vaRenderBufferFlags::None, "RTScratchResource" );
    }

    return m_scratchResource;
}

void vaSceneRaytracing::UpdateAndSetToGlobals( vaRenderDeviceContext & renderContext, vaShaderItemGlobals & shaderItemGlobals, const vaDrawAttributes & drawAttributes )
{
    renderContext; shaderItemGlobals; drawAttributes;

    assert( drawAttributes.Raytracing == this );

    // // forgot to call SetWorldBase before Tick before this? :) there's an order requirement here, sorry
    // assert( drawAttributes.Settings.WorldBase == m_lastTickedWorldBase );
    // assert( m_lastTickedWorldBase == m_worldBase );
    // 
    // UpdateShaderConstants( renderContext, drawAttributes );

    // assert( shaderItemGlobals.ConstantBuffers[RAY] == nullptr );
    // shaderItemGlobals.ConstantBuffers[LIGHTINGGLOBAL_CONSTANTSBUFFERSLOT] = m_constantsBuffer.GetBuffer( );
    shaderItemGlobals.RaytracingAcceleationStructSRV = m_topLevelAccelerationStructure[m_currentBackbuffer];

    shaderItemGlobals.ShaderResourceViews[SHADERGLOBAL_INSTANCE_CONSTANTBUFFERS_TEXTURESLOT] = m_instanceStorage->GetInstanceRenderBuffer();
}

void vaSceneRaytracing::PrepareInstanceBatchProcessing( const shared_ptr<vaRenderInstanceStorage> & instanceStorage )
{
    m_instanceStorage = instanceStorage;

    m_instanceList.resize( instanceStorage->GetInstanceMaxCount() );
    //m_geometryList.StartAppending( );
    m_instanceCount = 0;
}

void vaSceneRaytracing::ProcessInstanceBatch( vaScene & scene, vaSceneRenderInstanceProcessor::SceneItem * items, uint32 itemCount, uint32 baseInstanceIndex )
{
    scene; items; itemCount; baseInstanceIndex;

    //InstanceItem instanceItems[vaSceneRenderInstanceProcessor::c_ConcurrentChuckMaxItemCount];
    //GeometryItem geometryItems[vaSceneRenderInstanceProcessor::c_ConcurrentChuckMaxItemCount];
    
    //uint32 geometryItemCount = 0;
    
    entt::registry& registry = scene.Registry( );
    const auto& cregistry = std::as_const( registry );

    for( uint32 i = 0; i < itemCount; i++ )
    {
        int globalIndex = baseInstanceIndex + i;
        m_instanceList[globalIndex].Transform      = cregistry.get<Scene::TransformWorld>( items[i].Entity );
        m_instanceList[globalIndex].InstanceIndex  = baseInstanceIndex+i;

        // Let the processor know that this item is used!
        items[i].IsUsed = true;

        /*
        bool geometryFound = false;
        for( uint32 j = 0; j < geometryItemCount; j++ )
        {
            // use some kind of hash table here in the future (check out vaConcurrency?), but for now this is good enough
            if( geometryItems[j].Mesh == items[i].Mesh )
            {
                geometryFound = true;
                break;
            }
        }
        if( !geometryFound )
        {
            geometryItems[geometryItemCount++].Mesh = items[i].Mesh;
        }
        */
    }
    //m_instanceList.AppendBatch( instanceItems, itemCount );
    //m_geometryList.AppendBatch( geometryItems, geometryItemCount );
    m_instanceCount += itemCount;
}

void vaSceneRaytracing::PreRenderUpdate( vaRenderDeviceContext & renderContext, const std::unordered_set<vaFramePtr<vaRenderMesh>> & meshes, const std::unordered_set<vaFramePtr<vaRenderMaterial>> & materials )
{
    // due to resource management, one vaSceneRaytracing instance can only handle being used once per frame; this restriction could be removed if need be.
    assert( m_lastFrameIndex < GetRenderDevice().GetCurrentFrameIndex() );
    m_lastFrameIndex = GetRenderDevice().GetCurrentFrameIndex();
    m_currentBackbuffer = m_lastFrameIndex%vaRenderDevice::c_BackbufferCount;

    //m_instanceList.StartConsuming();
    //m_geometryList.StartConsuming();
    
    // // remove duplicates (for higher perf... https://stackoverflow.com/questions/1041620/whats-the-most-efficient-way-to-erase-duplicates-and-sort-a-vector)
    // std::vector<GeometryItem> & geoms = m_geometryList.GetVectorUnsafe( );
    // std::sort( geoms.begin( ), geoms.end( ), []( const GeometryItem & lhs, const GeometryItem & rhs ) { return lhs.Mesh < rhs.Mesh; } );
    // geoms.erase( std::unique( geoms.begin( ), geoms.end( ), []( const GeometryItem & lhs, const GeometryItem & rhs ) { return lhs.Mesh == rhs.Mesh; } ), geoms.end( ) );

    //for( uint32 i = 0; i < m_geometryList.Count( ); i++ )
    //{
    //    GeometryItem & geom = m_geometryList[i];
    //    vaRenderMesh & mesh = *geom.Mesh;
    //    // render mesh internal data might need update - this has to be done using unique lock so nobody else does it at the same time
    //    std::shared_lock meshLock( mesh.Mutex( ) );
    //    mesh.UpdateGPUDataIfNeeded( renderContext, meshLock );
    //}

    for( vaFramePtr<vaRenderMesh> meshPtr : meshes )
    {
        // meshPtr->Update( renderContext );
    }

    for( vaFramePtr<vaRenderMaterial> materialPtr : materials )
    {
        // meshPtr->Update( renderContext );
    }


    PreRenderUpdateInternal( renderContext, meshes, materials );
}

void vaSceneRaytracing::PostRenderCleanup( )
{
    m_instanceCount = 0;
    //m_instanceList.clear();
    //m_instanceList.Clear( );
    //m_geometryList.Clear( );
    m_instanceStorage = nullptr;
    PostRenderCleanupInternal();
}

void vaSceneRaytracing::UIPanelTick( vaApplicationBase & application )
{
    application;
    ImGui::Text( "Raytracing-specific settings:" );
    ImGui::InputFloat( "MIPOffset", &m_settings.MIPOffset );
    //ImGui::InputFloat( "MIPSlopeModifier", &m_settings.MIPSlopeModifier );
    ImGui::Separator();
    ImGui::Text( "Info on the number of instances, geometries and stuff would be nice here!" );

    m_settings.MIPOffset        = vaMath::Clamp( m_settings.MIPOffset       , -16.0f, 16.0f );
    //m_settings.MIPSlopeModifier = vaMath::Clamp( m_settings.MIPSlopeModifier, 0.01f, 2.0f );
}

void vaSceneRaytracing::UIPanelTickAlways( vaApplicationBase & application )
{
    application;
}
