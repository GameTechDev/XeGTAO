///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaSceneRenderViews.h"
#include "vaSceneRenderer.h"

#include "vaRenderMesh.h"
#include "vaRenderMaterial.h"

#include "Rendering/vaRenderGlobals.h"
#include "Rendering/Effects/vaSkybox.h"
#include "Rendering/Effects/vaASSAOLite.h"
#include "Rendering/Effects/vaGTAO.h"
#include "Rendering/Effects/vaDepthOfField.h"
#include "Rendering/Effects/vaPostProcess.h"
#include "Rendering/Effects/vaPostProcessTonemap.h"
#include "Rendering/Effects/vaCMAA2.h"
#include "Rendering/Effects/vaTAA.h"
#include "Rendering/vaSceneLighting.h"

#include "Scene/vaScene.h"
#include "Scene/vaSceneSystems.h"

#include "Core/vaInput.h"

#include "Rendering/vaPathTracer.h"

#include <future>

// #ifdef VA_TASKFLOW_INTEGRATION_ENABLED
// #include "IntegratedExternals/vaTaskflowIntegration.h"
// #endif

#include "IntegratedExternals/vaImguiIntegration.h"


using namespace Vanilla;

void vaSceneRenderViewBase::ProcessInstanceBatchCommon( entt::registry & registry, vaSceneRenderInstanceProcessor::SceneItem * items, uint32 itemCount, vaRenderInstanceList * opaqueList, vaRenderInstanceList * transparentList, const vaRenderInstanceList::FilterSettings & filter, const vaSceneSelectionFilterType & customFilter, uint32 baseInstanceIndex )
{
    baseInstanceIndex;

    if( itemCount == 0 )
        return;

    const auto & cregistry = std::as_const( registry );

    const vaPlane* frustumPlanes = filter.FrustumPlanes.data( );
    const int       frustumPlaneCount = (int)filter.FrustumPlanes.size( );

    for( uint32 i = 0; i < itemCount; i++ )
    {
        entt::entity entity = items[i].Entity;

        if( !cregistry.valid(entity) || !cregistry.any_of<Scene::WorldBounds>(entity) )
        {
            assert( false ); // abort( );
            continue;
        }

        const Scene::WorldBounds & worldBounds = cregistry.get<Scene::WorldBounds>( entity );

        vaFramePtr<vaRenderMesh> & renderMesh           = items[i].Mesh;
        vaFramePtr<vaRenderMaterial> & renderMaterial   = items[i].Material;

        // todo - do this: http://bitsquid.blogspot.com/2016/10/the-implementation-of-frustum-culling.html

        // if it doesn't pass the frustum test, cull it here
        //if( worldBounds.AABB.IntersectFrustum( frustumPlanes, frustumPlaneCount ) == vaIntersectType::Outside )
        if( worldBounds.BS.IntersectFrustum( frustumPlanes, frustumPlaneCount ) == vaIntersectType::Outside )
            continue;

        const Scene::TransformWorld & worldTransform = cregistry.get<Scene::TransformWorld>( entity );

        int baseShadingRate = 0;

        bool doSelect = ( customFilter != nullptr ) ? ( customFilter( entity, worldTransform, worldBounds, *renderMesh, *renderMaterial, baseShadingRate ) ) : ( true );
        if( doSelect )
        {
            vaShadingRate finalShadingRate = renderMaterial->ComputeShadingRate( baseShadingRate );

            if( renderMaterial->IsTransparent( ) )
            {
                if( transparentList != nullptr )
                {
                    transparentList->Insert( baseInstanceIndex+i, finalShadingRate );
                    items[i].IsUsed = true;
                }
            }
            else
            {
                if( opaqueList != nullptr )
                {
                    opaqueList->Insert( baseInstanceIndex+i, finalShadingRate );
                    items[i].IsUsed = true;
                }
            }
        }
    }
}

vaSceneRenderViewBase::vaSceneRenderViewBase( const shared_ptr<vaSceneRenderer> & parentRenderer ) 
    : m_parentRenderer( parentRenderer ), m_renderDevice( parentRenderer->GetRenderDevice() )   
{ 
}

void vaSceneRenderViewBase::UIDisplayStats( )
{
    ImGui::Text( "ItemsDrawn:       %d", m_basicStats.ItemsDrawn );
    ImGui::Text( "TrianglesDrawn:   %.3fk", (float)(m_basicStats.TrianglesDrawn/1000.0f) );
    ImGui::Text( "DrawErrors:       %s", vaDrawResultFlagsUIName( m_basicStats.DrawResultFlags ).c_str() );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

vaPointShadowRV::vaPointShadowRV( const shared_ptr<vaSceneRenderer> & parentRenderer )
    : vaSceneRenderViewBase( parentRenderer )
{
}

void vaPointShadowRV::PreRenderTick( float deltaTime )
{
    vaSceneRenderViewBase::PreRenderTick( deltaTime );

    if( m_shadowmap == nullptr )
        return;

    m_basicStats.DrawResultFlags    = vaDrawResultFlags::None;
    m_lastPreRenderDrawResults      = vaDrawResultFlags::None;

    shared_ptr<vaSceneRenderer> sceneRenderer = m_parentRenderer.lock();
    assert( sceneRenderer != nullptr && sceneRenderer->GetScene( ) != nullptr );

    VA_TRACE_CPU_SCOPE( PointShadowPreRender );

    m_selectionOpaque.StartCollecting( sceneRenderer->GetInstanceStorage() );
    // after this, ProcessInstanceBatch will get called and then, after all is processed, PreRenderTickParallelFinished gets called
}

// this gets called from worker threads to provide chunks for processing!
void vaPointShadowRV::ProcessInstanceBatch( vaScene& scene, vaSceneRenderInstanceProcessor::SceneItem* items, uint32 itemCount, uint32 baseInstanceIndex )
{
    if( m_shadowmap == nullptr )
        return;

    vaSceneRenderViewBase::ProcessInstanceBatchCommon( scene.Registry( ), items, itemCount, &m_selectionOpaque, nullptr, vaRenderInstanceList::FilterSettings::ShadowmapCull( *m_shadowmap ), m_selectionFilter, baseInstanceIndex );
}

vaDrawResultFlags vaPointShadowRV::PreRenderTickParallelFinished( )
{
    if( m_shadowmap == nullptr )
        return vaDrawResultFlags::None;

    m_selectionOpaque.StopCollecting( );

    m_basicStats.DrawResultFlags    |= m_selectionOpaque.ResultFlags( );
    m_lastPreRenderDrawResults      = m_basicStats.DrawResultFlags;
    return m_lastPreRenderDrawResults;
}

void vaPointShadowRV::RenderTick( float deltaTime, vaRenderDeviceContext & renderContext, vaDrawResultFlags & currentDrawResults )
{
    deltaTime; renderContext;
    vaDrawResultFlags drawResults = vaDrawResultFlags::None;
    shared_ptr<vaSceneRenderer> sceneRenderer = m_parentRenderer.lock( );
    assert( sceneRenderer != nullptr  );

    if( m_shadowmap == nullptr )
        return;

    // skip rendering of shadows if things are not currently completely loaded or whatnot
    if( currentDrawResults != vaDrawResultFlags::None )
    {
        currentDrawResults |= m_selectionOpaque.ResultFlags( );
        m_basicStats.DrawResultFlags |= currentDrawResults;
        m_shadowmap = nullptr;
        m_selectionOpaque.Reset( );
        return;
    }

    VA_TRACE_CPU_SCOPE( PointShadowRender );

    // TODO: move all this shadow map code here - let the shadowmaps be responsible only for storage
    drawResults |= m_shadowmap->Draw( renderContext, m_selectionOpaque );
    m_basicStats.ItemsDrawn += (int)m_selectionOpaque.Count( );

    m_shadowmap = nullptr;
    m_selectionOpaque.Reset( );

    drawResults |= m_selectionOpaque.ResultFlags( );
    m_basicStats.DrawResultFlags |= drawResults;
    currentDrawResults |= drawResults;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

vaLightProbeRV::vaLightProbeRV( const shared_ptr<vaSceneRenderer> & parentRenderer )
    : vaSceneRenderViewBase( parentRenderer )
{

}

void vaLightProbeRV::PreRenderTick( float deltaTime )
{
    vaSceneRenderViewBase::PreRenderTick( deltaTime );

    if( m_probe == nullptr || !m_probeData.Enabled )    // all good, nothing to do
        return;

    m_basicStats.DrawResultFlags    = vaDrawResultFlags::None;
    m_lastPreRenderDrawResults      = vaDrawResultFlags::None;
    
    shared_ptr<vaSceneRenderer> sceneRenderer = m_parentRenderer.lock( );
    assert( sceneRenderer != nullptr );

    assert( sceneRenderer->GetScene( ) != nullptr );

    m_sortDepthPrepass  = vaRenderInstanceList::EmptySortHandle;
    m_sortOpaque        = vaRenderInstanceList::EmptySortHandle;
    m_sortTransparent   = vaRenderInstanceList::EmptySortHandle;

    if( m_probeData.ImportFilePath == "" )
    {
        vaVector3 probePos = m_probe->GetContentsData( ).Position;

        // This is where we would start the async sorts if we had that implemented - or actually at some point below
        if( sceneRenderer->GeneralSettings( ).DepthPrepass && sceneRenderer->GeneralSettings( ).SortDepthPrepass )
            m_sortDepthPrepass = m_selectionOpaque.ScheduleSort( vaRenderInstanceList::SortSettings::Standard( probePos, true/*, false*/ ) );

        //if( sceneRenderer->GeneralSettings( ).SortOpaque ) <- we now always sort opaque due to decals, sorry
            m_sortOpaque = m_selectionOpaque.ScheduleSort( vaRenderInstanceList::SortSettings::Standard( probePos, false/*, true*/ ) );

        m_sortTransparent = m_selectionTransparent.ScheduleSort( vaRenderInstanceList::SortSettings::Standard( probePos, false/*, false*/ ) );

        m_selectionOpaque.StartCollecting( sceneRenderer->GetInstanceStorage() );
        m_selectionTransparent.StartCollecting( sceneRenderer->GetInstanceStorage() );

        m_selectionFilter = [ &registry = sceneRenderer->GetScene()->Registry() ] ( const entt::entity entity, const vaMatrix4x4 & worldTransform, const Scene::WorldBounds & , const vaRenderMesh & mesh, const vaRenderMaterial & material, int & outBaseShadingRate ) -> bool
        {
            worldTransform; worldTransform; mesh; material; outBaseShadingRate; 
            const auto & cregistry = std::as_const( registry );
            return !cregistry.any_of<Scene::IgnoreByIBLTag>( entity );
        };
        // after this, ProcessInstanceBatch will get called and then, after all is processed, PreRenderTickParallelFinished gets called
    }
}

// this gets called from worker threads to provide chunks for processing!
void vaLightProbeRV::ProcessInstanceBatch( vaScene & scene, vaSceneRenderInstanceProcessor::SceneItem * items, uint32 itemCount, uint32 baseInstanceIndex )
{
    if( m_probe == nullptr || !m_probeData.Enabled )    // all good, nothing to do
        return;

    if( m_probeData.ImportFilePath == "" )
    {
        vaSceneRenderViewBase::ProcessInstanceBatchCommon( scene.Registry( ), items, itemCount, &m_selectionOpaque, &m_selectionTransparent, vaRenderInstanceList::FilterSettings::EnvironmentProbeCull( m_probeData ), m_selectionFilter, baseInstanceIndex );
    }
}


vaDrawResultFlags vaLightProbeRV::PreRenderTickParallelFinished( )
{
    if( m_probe == nullptr || !m_probeData.Enabled )    // all good, nothing to do
        return vaDrawResultFlags::None;

    if( m_probeData.ImportFilePath == "" )
    {
        m_selectionOpaque.StopCollecting( );
        m_selectionTransparent.StopCollecting( );
    }

    m_basicStats.DrawResultFlags |= m_selectionOpaque.ResultFlags( );
    m_basicStats.DrawResultFlags |= m_selectionTransparent.ResultFlags( );
    m_lastPreRenderDrawResults = m_basicStats.DrawResultFlags;
    return m_lastPreRenderDrawResults;
}

void vaLightProbeRV::RenderTick( float deltaTime, vaRenderDeviceContext & renderContext, vaDrawResultFlags & currentDrawResults )
{
    deltaTime; renderContext;

    if( m_probe == nullptr || !m_probeData.Enabled )    // all good, nothing to do
        return;

    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    shared_ptr<vaSceneRenderer> sceneRenderer = m_parentRenderer.lock( );
    assert( sceneRenderer != nullptr );

    assert( sceneRenderer->GetScene( ) != nullptr );

    // skip rendering of shadows if things are not currently completely loaded or whatnot
    if( currentDrawResults != vaDrawResultFlags::None )
    {
        currentDrawResults |= m_selectionOpaque.ResultFlags( );
        currentDrawResults |= m_selectionTransparent.ResultFlags( );
        m_basicStats.DrawResultFlags |= currentDrawResults;
        m_probe->Reset();
        m_probe = nullptr;
        m_selectionOpaque.Reset();
        m_selectionTransparent.Reset();
        return;
    }

    if( m_probeData.ImportFilePath != "" )
    {
        bool success = m_probe->Import( renderContext, m_probeData );
        //assert( success ); 
        success; // this isn't actually handled correctly - it will attempt importing again
    }
    else
    {
        CubeFaceCaptureCallback faceCapture = [ & ]
            ( vaRenderDeviceContext& renderContext, const vaCameraBase& faceCamera, const shared_ptr<vaTexture>& faceDepth, const shared_ptr<vaTexture>& faceColor )
        {

            vaDrawAttributes::GlobalSettings globalSettings;
            globalSettings.SpecialEmissiveScale = 0.1f;
            globalSettings.DisableGI = true;

            // clear the main render target / depth
            faceDepth->ClearDSV( renderContext, true, faceCamera.GetUseReversedZ( ) ? ( 0.0f ) : ( 1.0f ), false, 0 );
            faceColor->ClearRTV( renderContext, { 0,0,0,0 } );

            // Depth pre-pass
            drawResults |= sceneRenderer->DrawDepthOnly( renderContext, vaRenderOutputs::FromRTDepth( nullptr, faceDepth ), m_selectionOpaque, m_sortDepthPrepass, faceCamera, globalSettings );

            // this clear is not actually needed!
            // // clear light accumulation (radiance) RT
            // gbufferOutput.GetRadiance( )->ClearRTV( mainContext, vaVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );

            // Opaque stuff
            drawResults |= sceneRenderer->DrawOpaque( renderContext, vaRenderOutputs::FromRTDepth( faceColor, faceDepth ), m_selectionOpaque, m_sortOpaque, faceCamera, globalSettings, nullptr, true );

            m_basicStats.ItemsDrawn += (int)m_selectionOpaque.Count( );

            // Transparent stuff
            m_selectionTransparent; // <- not really doing it for now, a bit more complexity

            return drawResults;
        };
        if( m_probe->Capture( renderContext, m_probeData, faceCapture ) == vaDrawResultFlags::None )
        {
            assert( m_probe->GetContentsData( ) == m_probeData );
        }
    }
    m_probe = nullptr;
    m_probeData = Scene::IBLProbe();

    drawResults |= m_selectionOpaque.ResultFlags();
    drawResults |= m_selectionTransparent.ResultFlags();
    m_basicStats.DrawResultFlags |= drawResults;
    currentDrawResults |= drawResults;
    m_selectionOpaque.Reset( );
    m_selectionTransparent.Reset( );
}
