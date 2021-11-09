///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaSceneRenderer.h"

#include "vaRenderMesh.h"
#include "vaRenderMaterial.h"

#include "Core/vaApplicationBase.h"

#include "Rendering/Effects/vaSkybox.h"
#include "Rendering/Effects/vaDepthOfField.h"

#include "Scene/vaScene.h"

#include "Rendering/vaSceneRaytracing.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "vaSceneMainRenderView.h"

using namespace Vanilla;

vaSceneRenderer::vaSceneRenderer( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params ),
    vaUIPanel( "SceneRenderer", 2, !VA_MINIMAL_UI_BOOL, vaUIPanel::DockLocation::DockedLeft, "SceneRenderers" ),
    m_instanceProcessor( *this )
{
    m_lighting          = GetRenderDevice( ).CreateModule<vaSceneLighting>( );
    m_skybox            = GetRenderDevice( ).CreateModule<vaSkybox>( );
    m_instanceStorage   = GetRenderDevice( ).CreateModule<vaRenderInstanceStorage>( );
}

vaSceneRenderer::~vaSceneRenderer( )
{

}

const vaCameraBase* vaSceneRenderer::GetLODReferenceCamera( ) const
{
    // just pick first view for now but need to think about this more carefully
    if( m_mainViews.size() > 0 )
        return m_mainViews[0]->Camera().get();
    return nullptr;
}

vaDrawResultFlags vaSceneRenderer::DrawDepthOnly( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaRenderInstanceList & renderSelection, vaRenderInstanceList::SortHandle renderSelectionSort, const vaCameraBase & camera, const vaDrawAttributes::GlobalSettings & globalSettings )
{
    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    assert( renderOutputs.DepthStencil != nullptr );

    vaRenderMaterialShaderType shaderType = vaRenderMaterialShaderType::DepthOnly;
    if( renderOutputs.RenderTargetCount > 0 )
        shaderType = vaRenderMaterialShaderType::RichPrepass;

    // Depth pre-pass
    {
        VA_TRACE_CPUGPU_SCOPE( DepthOnly, renderContext );
        vaDrawAttributes drawAttributes( camera, vaDrawAttributes::RenderFlags::None, nullptr, nullptr, globalSettings );

        vaRenderMeshDrawFlags flags = vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::EnableDepthWrite | vaRenderMeshDrawFlags::DisableVRS;
        drawResults |= GetRenderDevice( ).GetMeshManager( ).Draw( renderContext, renderOutputs, shaderType, drawAttributes, renderSelection, vaBlendMode::Opaque, flags, renderSelectionSort );
    }
    return drawResults;
}

vaDrawResultFlags vaSceneRenderer::DrawOpaque( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaRenderInstanceList & renderSelection, vaRenderInstanceList::SortHandle renderSelectionSort, const vaCameraBase & camera, const vaDrawAttributes::GlobalSettings & globalSettings, const shared_ptr<vaTexture> & ssaoTexture, bool drawSky )
{
    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    if( !m_settingsGeneral.DepthPrepass )
        { assert( ssaoTexture == nullptr ); }

    // this is a bit ugly, setting it as a m_lighting state - I'm leaving it like that for now
    m_lighting->SetAOMap( ssaoTexture );

    // Forward opaque
    VA_TRACE_CPUGPU_SCOPE( Forward, renderContext );

    vaDrawAttributes drawAttributes( camera, vaDrawAttributes::RenderFlags::None, m_lighting.get( ), nullptr, globalSettings );

    vaRenderMeshDrawFlags drawFlags;
    if( m_settingsGeneral.DepthPrepass )
        drawFlags = vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::DepthTestEqualOnly | vaRenderMeshDrawFlags::DepthTestIncludesEqual;
    else
        drawFlags = vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::EnableDepthWrite | vaRenderMeshDrawFlags::DisableVRS;


    drawResults |= GetRenderDevice( ).GetMeshManager( ).Draw( renderContext, renderOutputs, vaRenderMaterialShaderType::Forward, drawAttributes, renderSelection, vaBlendMode::Opaque, drawFlags, renderSelectionSort );

    m_lighting->SetAOMap( nullptr );

    // opaque skybox
    if( drawSky && m_skybox->IsEnabled() )
    {
        VA_TRACE_CPUGPU_SCOPE( Sky, renderContext );
        drawResults |= m_skybox->Draw( renderContext, renderOutputs, drawAttributes );
    }
    return drawResults;
}

vaDrawResultFlags vaSceneRenderer::DrawTransparencies( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaRenderInstanceList & renderSelection, vaRenderInstanceList::SortHandle renderSelectionSort, const vaCameraBase & camera, const vaDrawAttributes::GlobalSettings & globalSettings )
{
    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    vaCameraBase nonJitteredCamera = camera;
    nonJitteredCamera.SetSubpixelOffset( vaVector2(0.0f, 0.0f) );

    vaDrawAttributes drawAttributes( nonJitteredCamera, vaDrawAttributes::RenderFlags::None, m_lighting.get( ), nullptr, globalSettings );

    bool alphaTAAHackEnabled = GetRenderDevice( ).GetMaterialManager().GetAlphaTAAHackEnabled();
    vaRenderOutputs localRenderOutputs = renderOutputs;
    if( alphaTAAHackEnabled )
    {
        assert( renderOutputs.DepthStencil != nullptr );
        drawAttributes.BaseGlobals.ShaderResourceViews[SHADERGLOBAL_DEPTH_TEXTURESLOT] = renderOutputs.DepthStencil;
        localRenderOutputs.DepthStencil = nullptr;
    }

    {
        VA_TRACE_CPUGPU_SCOPE( Transparencies, renderContext );

        vaRenderMeshDrawFlags drawFlags = (alphaTAAHackEnabled)?(vaRenderMeshDrawFlags::None):(vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::DepthTestIncludesEqual);
        drawResults |= GetRenderDevice( ).GetMeshManager( ).Draw( renderContext, localRenderOutputs, vaRenderMaterialShaderType::Forward, drawAttributes, renderSelection, vaBlendMode::AlphaBlend, drawFlags, renderSelectionSort );
    }
    return drawResults;
}

shared_ptr<vaSceneMainRenderView> vaSceneRenderer::CreateMainView( ) 
{ 
    shared_ptr<vaSceneMainRenderView> retVal( new vaSceneMainRenderView( this->shared_from_this( ) ) );
    m_allViews.push_back( retVal ); 
    m_mainViews.push_back( retVal ); 
    return retVal; 
}

void vaSceneRenderer::UpdateSettingsDependencies( )
{
    if( m_viewPointShadow == nullptr )
    {
        m_viewPointShadow = shared_ptr<vaPointShadowRV>( new vaPointShadowRV( this->shared_from_this( ) ) );
        m_allViews.push_back( m_viewPointShadow );
    }

    if( m_viewLightProbe == nullptr )
    {
        m_viewLightProbe = shared_ptr<vaLightProbeRV>( new vaLightProbeRV( this->shared_from_this( ) ) );
        m_allViews.push_back( m_viewLightProbe );
    }
}

void vaSceneRenderer::OnNewScene( )
{
    m_instanceProcessor.SetScene( m_scene );
    m_lighting->SetScene( m_scene );
    //m_shadowsStable = false;
    //m_IBLsStable = false;
    //m_ui_contextReset = true;

    m_sceneCallbacksToken = std::make_shared<int>( 42 );
    if( m_scene != nullptr )
    {
        m_scene->e_TickBegin.AddWithToken( m_sceneCallbacksToken, this, &vaSceneRenderer::OnSceneTickBegin );
        //m_scene->e_TickEnd.AddWithToken( m_sceneCallbacksToken, this, &vaSceneRenderer::OnSceneTickEnd );
    }
}

void vaSceneRenderer::OnSceneTickBegin( vaScene & scene, float deltaTime, int64 applicationTickIndex )
{
    VA_TRACE_CPU_SCOPE( ScenePreRender ); scene;

    bool raytracingRequired = false;
    PreprocessViews( raytracingRequired );
    raytracingRequired &= GetRenderDevice().GetCapabilities().Raytracing.Supported;
    if( raytracingRequired && ( m_raytracer == nullptr ) )
        m_raytracer = GetRenderDevice( ).CreateModule<vaSceneRaytracing>( (void*)this );
    if( !raytracingRequired && ( m_raytracer != nullptr ) )
        m_raytracer = nullptr;

    assert( m_scene != nullptr && m_scene.get() == &scene );
    if( m_scene == nullptr )
    {
        m_sceneTickDrawResults = vaDrawResultFlags::UnspecifiedError;
        return;
    }
    
    // at the moment only 1 main view supported
    assert( m_mainViews.size() == 1 );
    if( m_mainViews.size() != 1 )
        return;

    // This schedules the multithreaded update from the scene, but does not start it yet! It starts after this function exits.
    vaLODSettings LODSettings = GetLODReferenceCamera()->GetLODSettings( );
    m_instanceProcessor.SetSelectionParameters( LODSettings, m_instanceStorage, applicationTickIndex );

    for( int i = 0; i < (int)m_allViews.size( ); i++ )
    {
        auto view = m_allViews[i].lock();
        if( view != nullptr )
            view->ResetDrawResults( );
    }

    m_sceneTickDrawResults = vaDrawResultFlags::None;

    UpdateSettingsDependencies( );

    //m_worldBase = m_LODReferenceView.position?
    m_worldBase = {0,0,0};  // use m_LOD`cessor.Parameters.Reference
    m_lighting->SetWorldBase( m_worldBase );
    
    // in the future all these below can be ran from the vaScene::Async
    if( m_scene != nullptr )
        m_lighting->UpdateFromScene( *m_scene, deltaTime, applicationTickIndex );
   
    shared_ptr<vaShadowmap> nextShadowMap = GetLighting( )->GetNextHighestPriorityShadowmapForRendering( );
    pair<shared_ptr<vaIBLProbe>, Scene::IBLProbe> nextIBLProbe = GetLighting( )->GetNextHighestPriorityIBLProbeForRendering( );

    // this is now one or the other but in future they could/should happen in parallel, at runtime
    if( nextShadowMap != nullptr ) 
        m_viewPointShadow->SetActiveShadowmap( nextShadowMap );
    else if( nextIBLProbe.first != nullptr )
        m_viewLightProbe->SetActiveProbe( nextIBLProbe.first, nextIBLProbe.second );

    for( int i = 0; i < (int)m_allViews.size( ); i++ )
    {
        auto view = m_allViews[i].lock( );
        if( view != nullptr )
            view->PreRenderTick( deltaTime );
    }

    // Skybox picks up its stuff from DistantIBL
    if( m_lighting->GetDistantIBLProbe( )->HasContents( ) && m_lighting->GetDistantIBLProbe( )->HasSkybox( ) )
        m_lighting->GetDistantIBLProbe( )->SetToSkybox( *m_skybox );
    else
    {
        if( m_scene != nullptr )
            m_skybox->UpdateFromScene( *m_scene, deltaTime, applicationTickIndex );
        else
            m_skybox->Disable( );
    }
}

void vaSceneRenderer::PrepareInstanceBatchProcessing( uint32 maxInstances )
{
    maxInstances;
    VA_TRACE_CPU_SCOPE( ProcessInstanceBatch );
    // m_instanceStorage->StartWriting( maxInstances ); <- moved one level up to the caller, vaSceneRenderInstanceProcessor
    assert( m_scene != nullptr );
    if( m_raytracer != nullptr )
        m_raytracer->PrepareInstanceBatchProcessing( m_instanceStorage );
    for( int i = 0; i < (int)m_allViews.size( ); i++ )
    {
        auto view = m_allViews[i].lock( );
        if( view != nullptr )
            view->PrepareInstanceBatchProcessing( m_instanceStorage );
    }
}

void vaSceneRenderer::ProcessInstanceBatch( vaSceneRenderInstanceProcessor::SceneItem * items, uint32 itemCount, uint32 baseInstanceIndex )
{
    VA_TRACE_CPU_SCOPE( ProcessInstanceBatch );
    assert( m_scene != nullptr );
    if( m_raytracer != nullptr )
        m_raytracer->ProcessInstanceBatch( *m_scene, items, itemCount, baseInstanceIndex );
    for( int i = 0; i < (int)m_allViews.size( ); i++ )
    {
        auto view = m_allViews[i].lock( );
        if( view != nullptr )
            view->ProcessInstanceBatch( *m_scene, items, itemCount, baseInstanceIndex );
    }
}

vaDrawResultFlags vaSceneRenderer::RenderTick( float deltaTime, int64 applicationTickIndex )
{
    assert( vaThreading::IsMainThread() );
    VA_TRACE_CPU_SCOPE( SceneRenderer );

    // perhaps consider clearing buffers in this case?
    if( m_scene == nullptr )
        return vaDrawResultFlags::None;
    
    // if there's a mismatch it means you haven't properly tickled the scene and they're out of sync
    assert( m_scene == nullptr || m_scene->GetLastApplicationTickIndex() == applicationTickIndex ); applicationTickIndex;

    // reset 'starting' draw results
    m_sceneTickDrawResults = vaDrawResultFlags::None;

    // wait for selections to finish and call PreRenderTickParallelFinished which waits on any view-specific custom threading
    m_scene->Async().WaitAsyncComplete( "renderlists_done_marker" );

    m_sceneTickDrawResults |= m_instanceProcessor.ResultFlags();

    for( int i = 0; i < (int)m_allViews.size( ); i++ )
    {
        auto view = m_allViews[i].lock( );
        if( view != nullptr )
            m_sceneTickDrawResults |= view->PreRenderTickParallelFinished( );
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    vaRenderDeviceContext & renderContext = *GetRenderDevice().GetMainContext();

    // we can call FinalizeSelection after the m_scene->TickWait( "renderlists_done_marker" ) call above!
    m_instanceProcessor.FinalizeSelectionAndPreRenderUpdate( renderContext, m_raytracer );

    vaDrawResultFlags drawResults = m_sceneTickDrawResults;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Render section - stuff that talks to the GPU (uses rendering context)
    {
        VA_TRACE_CPU_SCOPE( Render );

        // in future loop over multiple of these and assign shadowmaps/lights from here
        m_viewPointShadow->RenderTick( deltaTime, renderContext, drawResults );

        // still some shadow maps to render?
        drawResults |= ( GetLighting( )->GetNextHighestPriorityShadowmapForRendering( ) != nullptr )    ? ( vaDrawResultFlags::PendingVisualDependencies ) : ( vaDrawResultFlags::None );

        // in future loop over multiple of these and assign probes from here
        m_viewLightProbe->RenderTick( deltaTime, renderContext, drawResults );

        // not all IBLs up-to-date?
        if( m_lighting->HasPendingVisualDependencies( ) )
            drawResults |= vaDrawResultFlags::PendingVisualDependencies;

        // main render views
        for( int i = 0; i < m_mainViews.size( ); i++ )
            m_mainViews[i]->RenderTick( deltaTime, renderContext, drawResults );
    }
    //
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // the selection contents here are no longer used so nuke them! otherwise we're leaving potentially dangling vaFramePtr-s!
    if( m_raytracer != nullptr )
        m_raytracer->PostRenderCleanup( );
    m_instanceProcessor.PostRenderCleanup( );

    return drawResults;
}

void vaSceneRenderer::UIPanelTick( vaApplicationBase & application )
{
    for( int i = 0; i < m_mainViews.size( ); i++ )
    {
        if( ImGui::CollapsingHeader( vaStringTools::Format( "Main view %d", i ).c_str(), ImGuiTreeNodeFlags_DefaultOpen ) )
            m_mainViews[i]->UITick( application );
    }
    ImGui::Separator();

    application;
    if( m_scene == nullptr )
        ImGui::Text( "No scene connected" );
    else
    {
        ImGui::Text( "Scene '%s' connected", m_scene->Name().c_str() );
        ImGui::SameLine();
        if( ImGui::Button( "Open scene UI" ) )
            m_scene->UIPanelSetFocusNextFrame();
    }
    ImGui::Separator();

    if( ImGui::CollapsingHeader( "Stats" ) )
    {
        ImGui::Indent();

        for( int i = 0; i < m_mainViews.size( ); i++ )
        {
            ImGui::Text( "Main view %d: ", i );
            m_mainViews[i]->UIDisplayStats();
        }
        if( m_viewPointShadow )
        {
            ImGui::Separator();
            ImGui::Text( "Point shadow view: " );
            m_viewPointShadow->UIDisplayStats( );
        }

        if( m_viewLightProbe )
        {
            ImGui::Separator();
            ImGui::Text( "IBL probe view: " );
            m_viewLightProbe->UIDisplayStats( );
        }

        ImGui::Unindent();
    }
}

void vaSceneRenderer::UIPanelTickAlways( vaApplicationBase & application )
{
    for( int i = 0; i < m_mainViews.size( ); i++ )
        m_mainViews[i]->UITickAlways( application );
}

void vaSceneRenderer::PreprocessViews( bool & raytracingRequired )
{
    raytracingRequired = false;
    for( int i = (int)m_allViews.size()-1; i >= 0; i-- )
    {
        auto view = m_allViews[i].lock();
        if( view == nullptr )
        {
            m_allViews.erase( m_allViews.begin() + i );
            continue;
        }
        raytracingRequired |= view->RequiresRaytracing();
    }
}
