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

namespace Vanilla
{
    string vaAATypeToUIName( vaAAType aaType )
    {
        switch( aaType )
        {
        case vaAAType::None:                        return "None";
        case vaAAType::TAA:                         return "TAA";
        case vaAAType::CMAA2:                       return "CMAA2";
        case vaAAType::SuperSampleReference:        return "SuperSampleReference";
        case vaAAType::SuperSampleReferenceFast:    return "SuperSampleReferenceFast";
        case vaAAType::MaxValue:
        default: assert( false ); return nullptr; break;
        }
    }
}

using namespace Vanilla;

static void ProcessInstanceBatchCommon( entt::registry & registry, vaSceneRenderInstanceProcessor::SceneItem * items, uint32 itemCount, vaRenderInstanceList * opaqueList, vaRenderInstanceList * transparentList, const vaRenderInstanceList::FilterSettings & filter, const vaSceneSelectionFilterType & customFilter, uint32 baseInstanceIndex )
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

vaSceneMainRenderView::vaSceneMainRenderView( const shared_ptr<vaSceneRenderer> & parentRenderer ) 
    : vaSceneRenderViewBase( parentRenderer )
#if 0
    , m_CSSmartUpsampleFloat( parentRenderer->GetRenderDevice() )
    , m_CSSmartUpsampleUnorm( parentRenderer->GetRenderDevice() )
    , m_PSSmartUpsampleDepth( parentRenderer->GetRenderDevice() )
#endif
{ 
    m_camera = std::make_shared<vaRenderCamera>( parentRenderer->GetRenderDevice( ), !VA_MINIMAL_UI_BOOL );

    m_postProcessTonemap    = parentRenderer->GetRenderDevice( ).CreateModule<vaPostProcessTonemap>( );
    m_ASSAO                 = parentRenderer->GetRenderDevice( ).CreateModule<vaASSAOLite>( );
    m_GTAO                  = parentRenderer->GetRenderDevice( ).CreateModule<vaGTAO>( );

#if 0
    m_CSSmartUpsampleFloat->CreateShaderFromFile( L"vaHelperTools.hlsl", "cs_5_0", "SmartUpscaleCS", { ( pair< string, string >( "VA_SMART_UPSCALE_SPECIFIC", "" ) ) }, false );
    m_CSSmartUpsampleUnorm->CreateShaderFromFile( L"vaHelperTools.hlsl", "cs_5_0", "SmartUpscaleCS", { ( pair< string, string >( "VA_SMART_UPSCALE_SPECIFIC", "" ) ), pair< string, string >( "VA_SMART_UPSCALE_UNORM_FLOAT", "" ) }, false );
    m_PSSmartUpsampleDepth->CreateShaderFromFile( L"vaHelperTools.hlsl", "ps_5_0", "SmartUpscaleDepthPS", { ( pair< string, string >( "VA_SMART_UPSCALE_SPECIFIC", "" ) ) }, false );
#endif
}

void vaSceneMainRenderView::UIDisplayStats( )
{
    ImGui::Checkbox( "Enable cursor hover info", &m_enableCursorHoverInfo );
    if( m_enableCursorHoverInfo )
    {
        VA_GENERIC_RAII_SCOPE( ImGui::Indent( ); , ImGui::Unindent( ); );

        // if( !m_cursorHoverInfo.HasData )
        //     ImGui::Text( "No hover data" );
        // else
        // {
        //     ImGui::Text( "Hover location: %d, %d", m_cursorHoverInfo.ViewportPos.x, m_cursorHoverInfo.ViewportPos.y );
        //     ImGui::Text( "World location (based on depth): \n   %.3f, %.3f, %.3f", m_cursorHoverInfo.WorldspacePos.x, m_cursorHoverInfo.WorldspacePos.y, m_cursorHoverInfo.WorldspacePos.z );
        //     ImGui::Text( "Viewspace depth: %.3f", m_cursorHoverInfo.ViewspaceDepth );
        //     ImGui::Text( "Items under cursor: %d", (int)m_cursorHoverInfo.Items.size() );
        //     {
        //         ImGui::Separator( );
        //         for( int i = 0; i < (int)m_cursorHoverInfo.Items.size( ); i++ )
        //         {
        //             ImGui::Text( "Item %d", i );
        //             VA_GENERIC_RAII_SCOPE( ImGui::Indent( ); , ImGui::Unindent( ); );
        //             const auto & item = m_cursorHoverInfo.Items[i];
        //             ImGui::Text( "SceneID:      %d", item.SceneID    );
        //             ImGui::Text( "EntityID:     %d", item.EntityID   );
        //             ImGui::Text( "MeshID:       %d", item.MeshID     );
        //             ImGui::Text( "MaterialID:   %d", item.MaterialID );
        //         }
        //         ImGui::Separator( );
        //         ImGui::Text( "(these are runtime IDs, they can change between different application runs)" );
        //     }
        // }
    }
}

void vaSceneMainRenderView::UITickAlways( vaApplicationBase & )
{
#ifndef VA_GTAO_SAMPLE
    if( ( vaInputKeyboardBase::GetCurrent( ) != NULL ) && vaInputKeyboardBase::GetCurrent( )->IsKeyDown( KK_CONTROL ) && vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( ( vaKeyboardKeys )'P' ) )
        m_settings.PathTracer = !m_settings.PathTracer;
#endif
}

void vaSceneMainRenderView::UITick( vaApplicationBase & application )
{
    // AA (some remnants of old compatibility code here)
    {
        //bool aaTypeApplicable[(int)vaAAType::MaxValue]; for( int i = 0; i < _countof( aaTypeApplicable ); i++ ) aaTypeApplicable[i] = true;

        std::vector<string> aaOptions;
#ifndef VA_GTAO_SAMPLE
        aaOptions.resize((int)vaAAType::MaxValue);
#else
        aaOptions.resize((int)vaAAType::TAA+1);
#endif
        for( int i = 0; i < (int)aaOptions.size(); i++ ) 
            aaOptions[i] = vaAATypeToUIName( (vaAAType) i );

        //for( int i = 0; i < _countof(aaTypeApplicable); i++ ) 
        //    if( !aaTypeApplicable[i] ) vals[i] = "(not applicable)";
        //vaAAType prevAAOption = m_settings.AAType;
        ImGuiEx_Combo( "AA", (int&)m_settings.AAType, aaOptions );
        // // some modes not applicable for static images (screenshots)
        // if( !aaTypeApplicable[(int)m_settings.AAType] )
        //     m_settings.AAType = prevAAOption;

#if 0 // reducing UI clutter
        if( m_settings.CurrentAAOption == VanillaSample::AAType::SuperSampleReference || m_settings.CurrentAAOption == VanillaSample::AAType::SuperSampleReferenceFast )
        {
            int indx = (m_settings.CurrentAAOption == VanillaSample::AAType::SuperSampleReferenceFast)?1:0;
            ImGui::Text( "SuperSampleReference works on 2 levels:" );
            ImGui::Text( "scene is rendered at 2x height & width, " );
            ImGui::Text( "with box downsample; also, each pixel" );
            ImGui::Text( "is average of PixGridRes x PixGridRes" );
            ImGui::Text( "samples (so total sample count per pixel" );
            ImGui::Text( "is (2*2*PixGridRes*PixGridRes)." );
            ImGui::InputInt( "SS PixGridRes", &m_SSGridRes[indx] );
            m_SSGridRes[indx] = vaMath::Clamp( m_SSGridRes[indx], 1, 8 );
            ImGui::InputFloat( "SS textures MIP bias", &m_SSMIPBias[indx], 0.05f );
            m_SSMIPBias[indx] = vaMath::Clamp( m_SSMIPBias[indx], -10.0f, 10.0f );
            ImGui::InputFloat( "SS sharpen", &m_SSSharpen[indx], 0.01f );
            m_SSSharpen[indx] = vaMath::Clamp( m_SSSharpen[indx], 0.0f, 1.0f );
            ImGui::InputFloat( "SS ddx/ddy bias", &m_SSDDXDDYBias[indx], 0.05f );
            m_SSDDXDDYBias[indx] = vaMath::Clamp( m_SSDDXDDYBias[indx], 0.0f, 1.0f );
        }
#endif    
    }

    ImGuiEx_Combo( "AO", (int&)m_settings.AOOption, { string("Disabled"), string("ASSAOLite"), string("XeGTAO") } );
    
    if( m_settings.AOOption != 0 )
    {
//        VA_GENERIC_RAII_SCOPE( ImGui::Indent();, ImGui::Unindent(); );
        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.8f, 0.5f, 1.0f ) );
        ImGui::Checkbox( "Display debug AO channel only", &m_settings.DebugShowAO );
        ImGui::PopStyleColor( );
        //ImGui::Text( (m_settings.AOOption==1)?("ASSAO-specific settings:"):("XeGTAO-specific settings") );
#ifdef VA_GTAO_SAMPLE
        if( m_settings.AOOption == 1 )
            m_ASSAO->UIPanelTickCollapsable( application, false, true, true );
        else if( m_settings.AOOption == 2 )
            m_GTAO->UIPanelTickCollapsable( application, false, true, true );
#endif
    }

#ifndef VA_GTAO_SAMPLE
    ImGui::Separator();
    ImGui::Checkbox( "Experimental path tracer (Ctrl+P)", &m_settings.PathTracer );

    if( m_settings.PathTracer && m_pathTracer != nullptr )
    {
        VA_GENERIC_RAII_SCOPE( ImGui::Indent();, ImGui::Unindent(); );
        m_pathTracer->UITick( application );
    }
#endif

    ImGui::Separator();
    ImGui::Checkbox( "Draw wireframe", &m_settings.ShowWireframe );
    ImGui::Checkbox( vaStringTools::Format( "Enable camera auto exposure (%.3f)###AutoExposure", m_camera->Settings().ExposureSettings.Exposure ).c_str(), &m_camera->Settings().ExposureSettings.UseAutoExposure );
    if( !m_camera->Settings().ExposureSettings.UseAutoExposure )
    {
        VA_GENERIC_RAII_SCOPE( ImGui::Indent( ); , ImGui::Unindent( ); );
        ImGui::InputFloat( "Camera exposure", &m_camera->Settings().ExposureSettings.Exposure ); m_camera->Settings().ExposureSettings.Exposure = vaMath::Clamp( m_camera->Settings().ExposureSettings.Exposure, m_camera->Settings().ExposureSettings.ExposureMin, m_camera->Settings().ExposureSettings.ExposureMax ); }
#if 0
    ImGui::Checkbox( "HalfResUpscale", &m_settings.HalfResUpscale );
#endif
    }

void vaSceneMainRenderView::PreRenderTick( float deltaTime )
{
    // path tracer can request camera position move, used for experiments - weird but it is what it is; move to some earlier place if moving camera here causes any issues anywhere
    if( m_settings.PathTracer && m_pathTracer != nullptr )
    {
        auto camPosRequest = m_pathTracer->GetNextCameraRequest( );
        if( camPosRequest != nullptr )
        {
            auto controller = m_camera->GetAttachedController( );
            m_camera->AttachController( nullptr );
            m_camera->SetPosition( camPosRequest->first );
            m_camera->SetDirection( camPosRequest->second );
            m_camera->Tick( 0, false );
            m_camera->AttachController( controller );
        }
    }

    vaSceneRenderViewBase::PreRenderTick( deltaTime );

    m_basicStats.DrawResultFlags    = vaDrawResultFlags::None;
    m_lastPreRenderDrawResults      = vaDrawResultFlags::None;

    shared_ptr<vaSceneRenderer> sceneRenderer = m_parentRenderer.lock( );
    assert( sceneRenderer != nullptr && sceneRenderer->GetScene( ) != nullptr );
    auto raytracer = sceneRenderer->GetRaytracer();

    // path racing is 'special' - some conditions need to be checked first
    if( m_settings.PathTracer )
    {
        if( raytracer == nullptr )
        {
            VA_LOG_WARNING( "Raytracing not supported on this hardware or a similar error - disabling the path tracer." );
            m_settings.PathTracer = false;
        }

        if( m_SS != nullptr )
        {
            VA_LOG_WARNING( "Rasterization super-sampling not compatible with path tracer path - disabling the path tracer." );
            m_settings.PathTracer = false;
        }
    }

    if( m_settings.AAType == vaAAType::SuperSampleReference || m_settings.AAType == vaAAType::SuperSampleReferenceFast )
    {
        m_SS = std::make_shared<SuperSampling>( );
        m_SS->FastVersion = m_settings.AAType == vaAAType::SuperSampleReferenceFast;
    }
    else
        m_SS = nullptr;

    if( m_settings.AAType == vaAAType::CMAA2 )
        m_CMAA2 = (m_CMAA2==nullptr)?(GetRenderDevice().CreateModule<vaCMAA2>()):(m_CMAA2);
    else
        m_CMAA2 = nullptr;

    bool suppressTAA = false;
    static bool prevSuppressTAA = false;
    if( m_settings.AAType == vaAAType::TAA )
    {
        if( m_settings.PathTracer )
        {
            if( !prevSuppressTAA )
                VA_LOG_WARNING( "TAA does not work with pure path tracer at the moment - disabling TAA." );
            suppressTAA = true;
        }
        if( m_settings.AOOption == 2 && m_GTAO != nullptr && m_GTAO->RequiresRaytracing() && m_renderDevice.GetCapabilities().Raytracing.Supported )
        {
            if( !prevSuppressTAA )
                VA_LOG_WARNING( "TAA does not work with GTAO reference ray tracer at the moment - disabling TAA." );
            suppressTAA = true;
        }
    }
    prevSuppressTAA = suppressTAA;

    if( m_settings.AAType == vaAAType::TAA && !suppressTAA )
        m_TAA = (m_TAA==nullptr)?(GetRenderDevice().CreateModule<vaTAA>()):(m_TAA);
    else
        m_TAA = nullptr;

    if( m_TAA != nullptr )
    {
        // something else is messing with jitter? upgrade code to handle it
        assert( m_camera->GetSubpixelOffset( ) == vaVector2(0,0) );

        vaVector2 jitter = m_TAA->ComputeJitter( GetRenderDevice().GetCurrentFrameIndex() );
        m_camera->SetSubpixelOffset( jitter );
    }
    else
    {
        m_camera->SetSubpixelOffset( vaVector2(0,0) );
    }

    m_selectionFilter = [ /*&settings = m_settings, &dofEffect = m_DepthOfField,*/ &camera = m_camera ]( const entt::entity, const vaMatrix4x4 &, const Scene::WorldBounds & , const vaRenderMesh &, const vaRenderMaterial &, int & outBaseShadingRate ) -> bool
    {
        outBaseShadingRate; 
        return true;
    };

    // This is where we would start the async sorts if we had that implemented - or actually at some point below
    // if we're changing VRS shading rate
    m_sortDepthPrepass  = vaRenderInstanceList::EmptySortHandle;
    m_sortOpaque        = vaRenderInstanceList::EmptySortHandle;
    m_sortTransparent   = vaRenderInstanceList::EmptySortHandle;

    if( sceneRenderer->GeneralSettings( ).DepthPrepass && sceneRenderer->GeneralSettings( ).SortDepthPrepass )
        m_sortDepthPrepass  = m_selectionOpaque.ScheduleSort(       vaRenderInstanceList::SortSettings::Standard( *m_camera, true/*, false*/ ) );

    // if( sceneRenderer->GeneralSettings( ).SortOpaque ) <- we now always sort opaque due to decals, sorry
        m_sortOpaque        = m_selectionOpaque.ScheduleSort(       vaRenderInstanceList::SortSettings::Standard( *m_camera, false/*, true*/ ) );

    m_sortTransparent   = m_selectionTransparent.ScheduleSort(  vaRenderInstanceList::SortSettings::Standard( *m_camera, false/*, false*/ ) );

    m_selectionOpaque.StartCollecting( sceneRenderer->GetInstanceStorage() );
    m_selectionTransparent.StartCollecting( sceneRenderer->GetInstanceStorage() );

    // after this, ProcessInstanceBatch will get called and then, after all is processed, PreRenderTickParallelFinished gets called
}

// this gets called from worker threads to provide chunks for processing!
void vaSceneMainRenderView::ProcessInstanceBatch( vaScene & scene, vaSceneRenderInstanceProcessor::SceneItem * items, uint32 itemCount, uint32 baseInstanceIndex )
{
    ProcessInstanceBatchCommon( scene.Registry(), items, itemCount,  &m_selectionOpaque, &m_selectionTransparent, vaRenderInstanceList::FilterSettings::FrustumCull( *m_camera ), m_selectionFilter, baseInstanceIndex );
}

vaDrawResultFlags vaSceneMainRenderView::PreRenderTickParallelFinished( )
{
    m_selectionOpaque.StopCollecting( );
    m_selectionTransparent.StopCollecting( );

    m_basicStats.DrawResultFlags |= m_selectionOpaque.ResultFlags( );
    m_basicStats.DrawResultFlags |= m_selectionTransparent.ResultFlags( );
    m_lastPreRenderDrawResults = m_basicStats.DrawResultFlags;
    return m_lastPreRenderDrawResults;
}

void vaSceneMainRenderView::RenderTickInternal( vaRenderDeviceContext & renderContext, vaDrawResultFlags & currentDrawResults, vaDrawAttributes::GlobalSettings & globalSettings, bool skipCameraLuminanceUpdate, DepthPrepassType depthPrepass )
{
    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    // depth pre-pass not needed/useful with the path tracer - disable
    if( m_settings.PathTracer )
        depthPrepass = vaSceneMainRenderView::DepthPrepassType::None;

    shared_ptr<vaSceneRenderer> sceneRenderer = m_parentRenderer.lock( );
    auto raytracer = sceneRenderer->GetRaytracer();

    // this, among other things, collects old frame luminance measurements from tonemapping, which is why it needs the main context and etc.
    if( !skipCameraLuminanceUpdate )
        m_camera->PreRenderTick( renderContext, m_lastDeltaTime );

    // Clear the depth
    if( depthPrepass != DepthPrepassType::UseExisting )
    {
        m_workingDepth->ClearDSV( renderContext, true, m_camera->GetUseReversedZ( ) ? ( 0.0f ) : ( 1.0f ), false, 0 );
        if( m_workingNormals != nullptr )
            m_workingNormals->ClearRTV( renderContext, {0,0,0,0} );
    }

    // Depth pre-pass
    if( depthPrepass == DepthPrepassType::DrawAndUse )
    {
        // if normal exporting is not enabled, m_workingNormals will be nullptr
        drawResults |= sceneRenderer->DrawDepthOnly( renderContext, vaRenderOutputs::FromRTDepth( m_workingNormals, m_workingDepth ), m_selectionOpaque, m_sortDepthPrepass, *m_camera, globalSettings );
        m_basicStats.ItemsDrawn += (int)m_selectionOpaque.Count( );
    }

    // this clear is not actually needed as every pixel should be drawn into anyway! but draw pink debug background in debug to validate this
#ifdef _DEBUG
    // clear light accumulation (radiance) RT
    m_workingPreTonemapColor->ClearRTV( renderContext, vaVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
#else
    m_workingPreTonemapColor->ClearRTV( renderContext, vaVector4( 0.0f, 0.1f, 0.0f, 0.0f ) );
#endif

    bool hasDepthPrepass = depthPrepass != DepthPrepassType::None;

    // SSAO!
    shared_ptr<vaTexture> ssaoBuffer = nullptr;
    if( /*m_settings.AOOption != 0 &&*/ hasDepthPrepass )
    {
        if( m_SSAOScratchBuffer == nullptr || m_SSAOScratchBuffer->GetSize( ) != m_workingDepth->GetSize( ) )
        {
#ifdef _DEBUG   // just an imperfect way of making sure SSAO scratch buffer isn't getting re-created continuously (will also get triggered if you change res 1024 times... so that's why it's imperfect)
            static int recreateCount = 0; recreateCount++;
            assert( recreateCount < 1024 );
#endif
            m_SSAOScratchBuffer = vaTexture::Create2D( GetRenderDevice( ), vaResourceFormat::R8_UNORM, m_workingDepth->GetWidth( ), m_workingDepth->GetHeight( ), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
            m_SSAOScratchBuffer->SetName( "SSAOScratchBuffer" );
        }
        ssaoBuffer = m_SSAOScratchBuffer;
    }

    if( m_settings.AOOption == 0 && hasDepthPrepass )
    {
        // no AA - clear with 'white' - used to set it to nullptr previously but that sped up the rest of the pipeline and made on/off performance comparison for
        // just AO techniques more difficult; so now "no AO" is literally AO with no O :)
        ssaoBuffer->ClearUAV( renderContext, 1.0f );
    } 
    else if( m_settings.AOOption == 1 && m_ASSAO != nullptr && hasDepthPrepass )
    {
        drawResults |= m_ASSAO->Compute( renderContext, ssaoBuffer, m_camera->GetViewMatrix( ), m_camera->GetProjMatrix( ), m_workingDepth, m_workingNormals );
    }
    else if( m_settings.AOOption == 2 && m_GTAO != nullptr && hasDepthPrepass )
    {
        if( m_GTAO->RequiresRaytracing() && m_renderDevice.GetCapabilities().Raytracing.Supported )
            drawResults |= m_GTAO->ComputeReferenceRTAO( renderContext, *m_camera, raytracer.get(), m_workingDepth );

        drawResults |= m_GTAO->Compute( renderContext, *m_camera, m_TAA != nullptr, ssaoBuffer, m_workingDepth, m_workingNormals );
    }

    vaRenderOutputs preTonemapOutputs = vaRenderOutputs::FromRTDepth( m_workingPreTonemapColor, m_workingDepth );

    // for now only replace opaque stuff with the path tracer
    if( !m_settings.PathTracer )
    {
        // Opaque stuff
        drawResults |= sceneRenderer->DrawOpaque( renderContext, preTonemapOutputs, m_selectionOpaque, m_sortOpaque, *m_camera, globalSettings, (hasDepthPrepass)?(ssaoBuffer):(nullptr), true );
        m_basicStats.ItemsDrawn += (int)m_selectionOpaque.Count( );
    }
    else 
    {
        if( m_pathTracer == nullptr )
            m_pathTracer = GetRenderDevice().CreateModule<vaPathTracer>();

        vaDrawAttributes drawAttributes( *m_camera, vaDrawAttributes::RenderFlags::None, sceneRenderer->GetLighting().get( ), raytracer.get(), globalSettings );

        m_pathTracer->Draw( renderContext, drawAttributes, sceneRenderer->GetSkybox(), m_workingPreTonemapColor, m_workingDepth );

        // // we have to add Skybox (needs depth though - alternative is to do it in the miss shader - would be relatively easy to do!)
        // {
        //     VA_TRACE_CPUGPU_SCOPE( Sky, renderContext );
        //     drawAttributes.Raytracing = nullptr; 
        //     drawResults |= sceneRenderer->GetSkybox()->Draw( renderContext, vaRenderOutputs::FromRTDepth( m_workingPreTonemapColor, m_workingDepth ), drawAttributes );
        // }
    }

    vaPostProcess & pp = GetRenderDevice( ).GetPostProcess();

    // Debug stuff that goes before TAA/tonemapping
    bool debugViewActive = false;
    {
        if( m_settings.DebugShowAO && ssaoBuffer != nullptr && m_settings.AOOption != 0 )
        {
            if( m_settings.AOOption == 2 && (m_GTAO->DebugShowEdges() || m_GTAO->DebugShowNormals() || m_GTAO->ReferenceRTAOEnabled() ) )
            { debugViewActive = true; drawResults |= pp.MergeTextures( renderContext, m_workingPreTonemapColor, m_GTAO->DebugImage(), nullptr, nullptr, "float4( srcA.xyz, 1.0 )" ); }
            else
            { debugViewActive = true; drawResults |= pp.MergeTextures( renderContext, m_workingPreTonemapColor, ssaoBuffer, nullptr, nullptr, "float4( srcA.xxx, 1.0 )" ); }
        }
        if( m_settings.PathTracer && m_pathTracer != nullptr && m_pathTracer->DebugViz() != ShaderDebugViewType::None )
        { debugViewActive = true; } //drawResults |= pp.MergeTextures( renderContext, m_workingPreTonemapColor, m_workingPreTonemapColor, nullptr, nullptr, "float4( SRGB_to_LINEAR(srcA.rgb), 1.0 )" ); }
    }

    if( m_TAA != nullptr )
    {
        // reset TAA on some graphics settings change to prevent smooth fade; the main reason is to enable better before/after image comparisons
        uint32 currentSettingsHash = 0;
        currentSettingsHash = vaMath::Hash32Combine( currentSettingsHash, (int)(m_settings.AOOption) );
        currentSettingsHash = vaMath::Hash32Combine( currentSettingsHash, (m_settings.DebugShowAO)?(1):(0) );
        currentSettingsHash = vaMath::Hash32Combine( currentSettingsHash, (m_settings.PathTracer)?(1):(0) );
        currentSettingsHash = vaMath::Hash32Combine( currentSettingsHash, (m_settings.ShowWireframe)?(1):(0) );
        currentSettingsHash = vaMath::Hash32Combine( currentSettingsHash, (debugViewActive)?(1):(0) );
        if( m_GTAO != nullptr )
        {
            currentSettingsHash = vaMath::Hash32Combine( currentSettingsHash, (m_GTAO->Use16bitMath())?(1):(0) );
            currentSettingsHash = vaMath::Hash32Combine( currentSettingsHash, m_GTAO->Settings().QualityLevel );
            currentSettingsHash = vaMath::Hash32Combine( currentSettingsHash, m_GTAO->Settings().DenoiseLevel );
        }
        // when the data becomes valid, reset TAA
        currentSettingsHash = vaMath::Hash32Combine( currentSettingsHash, ((currentDrawResults | drawResults) != vaDrawResultFlags::None )?(1):(0) );

        if( currentSettingsHash != m_TAASettingsHash )
        {
            m_TAA->ResetHistory();
            // VA_LOG( "TAA history reset" );
        }
        m_TAASettingsHash = currentSettingsHash;

        VA_TRACE_CPUGPU_SCOPE( TAA, renderContext );
        drawResults |= m_TAA->Apply( renderContext, *m_camera, m_workingPreTonemapColor, m_workingDepth, m_reprojectionMatrix );
    }

    // Transparencies and effects
    if( !debugViewActive && !m_settings.PathTracer )
    {
        drawResults |= sceneRenderer->DrawTransparencies( renderContext, preTonemapOutputs, m_selectionTransparent, m_sortTransparent, *m_camera, globalSettings );
        m_basicStats.ItemsDrawn += (int)m_selectionTransparent.Count( );
    }

    // Tonemap to final color & postprocess
    {
        VA_TRACE_CPUGPU_SCOPE( TonemapAndPostFX, renderContext );

        // stop unrendered items from messing auto exposure
        if( drawResults != vaDrawResultFlags::None || debugViewActive )
            skipCameraLuminanceUpdate = true;

        vaPostProcessTonemap::AdditionalParams tonemapParams;
        tonemapParams.SkipTonemapper = debugViewActive;
        tonemapParams.SkipCameraLuminanceUpdate = skipCameraLuminanceUpdate;

        // This computes exposure, copies it to the readback buffer, applies bloom, applies tonemapping (unless camera vaRenderCamera::AllSettings::EnablePostProcess is false, in which case it doesn't apply bloom nor tonemapping)
        drawResults |= m_postProcessTonemap->TickAndApplyCameraPostProcess( renderContext, *m_camera, m_workingPostTonemapColor, m_workingPreTonemapColor, tonemapParams );

#if 0
#if 0
        // DoF setup
        {
            if( m_sceneMainView->Camera( )->GetAttachedController( ) == m_cameraFlythroughController )
            {
                m_settings.DoFFocalLength = m_cameraFlythroughController->GetLastUserParams( ).x;
                m_settings.DoFRange = m_cameraFlythroughController->GetLastUserParams( ).y;
            }
            if( m_DepthOfField != nullptr )
            {
                // this is not physically correct :(
                m_DepthOfField->Settings( ).InFocusFrom = m_settings.DoFFocalLength * ( 1.0f - m_settings.DoFRange );
                m_DepthOfField->Settings( ).InFocusTo = m_settings.DoFFocalLength * ( 1.0f + m_settings.DoFRange );

                const float transitionRange = 2.0f * m_settings.DoFRange;
                m_DepthOfField->Settings( ).NearTransitionRange = m_settings.DoFFocalLength * ( 2.0f * m_settings.DoFRange );
                m_DepthOfField->Settings( ).FarTransitionRange = m_settings.DoFFocalLength * ( 4.0f * m_settings.DoFRange );
            }
        }
#endif

        // Apply depth of field!
        // (intentionally applied before CMAA because some of it has sharp depth-based cutoff that causes aliasing)
        if( m_settings.EnableDOF )
        {
            vaDrawAttributes drawAttributes( mainContext, camera );
            // sceneContext needed for NDCToViewDepth to work - could be split out and made part of the constant buffer here
            drawResults |= m_DepthOfField->Draw( drawAttributes, mainDepthRT, mainColorRT, mainColorRTIgnoreSRGBConvView );
        }
#endif
    }

    // Debug stuff that goes after TAA/tonemapping
    {
        if( m_TAA != nullptr && m_TAA->ShowDebugImage( ) )
            drawResults |= pp.MergeTextures( renderContext, m_workingPreTonemapColor, m_TAA->DebugImage(), nullptr, nullptr, "float4( srcA.xyz, 1.0 )" );
    }

    // wireframe & debug & 3D UI
    {
        VA_TRACE_CPUGPU_SCOPE( DebugAndUI3D, renderContext );
        vaDrawAttributes drawAttributes( *m_camera, vaDrawAttributes::RenderFlags::SetZOffsettedProjMatrix | vaDrawAttributes::RenderFlags::DebugWireframePass, sceneRenderer->GetLighting().get(), nullptr, globalSettings );

        if( m_settings.ShowWireframe )
        {
            drawResults |= GetRenderDevice( ).GetMeshManager( ).Draw( renderContext, vaRenderOutputs::FromRTDepth( m_workingPostTonemapColor, m_workingDepth ), vaRenderMaterialShaderType::Forward, drawAttributes, m_selectionOpaque, vaBlendMode::Opaque, vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::DepthTestIncludesEqual );
            drawResults |= GetRenderDevice( ).GetMeshManager( ).Draw( renderContext, vaRenderOutputs::FromRTDepth( m_workingPostTonemapColor, m_workingDepth ), vaRenderMaterialShaderType::Forward, drawAttributes, m_selectionTransparent, vaBlendMode::Opaque, vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::DepthTestIncludesEqual );

            m_basicStats.ItemsDrawn     += (int)m_selectionOpaque.Count();
            m_basicStats.ItemsDrawn     += (int)m_selectionTransparent.Count();
            //m_basicStats.TrianglesDrawn += 
        }
    }

    // Apply CMAA!
    if( m_CMAA2 != nullptr )
    {
        VA_TRACE_CPUGPU_SCOPE( CMAA2, renderContext );
        drawResults |= m_CMAA2->Draw( renderContext, m_workingPostTonemapColor );
    }

    currentDrawResults |= drawResults;
}

void vaSceneMainRenderView::RenderTick( float deltaTime, vaRenderDeviceContext & renderContext, vaDrawResultFlags & currentDrawResults )
{
    deltaTime; renderContext;
    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    shared_ptr<vaSceneRenderer> sceneRenderer = m_parentRenderer.lock( );
    assert( sceneRenderer != nullptr );

    // stuff needed for reprojection (for temporal effects that need to project to NDC coords from previous RenderTick)
    {
        m_previousViewProj      = m_lastViewProj;
        vaMatrix4x4 viewProj    = m_camera->GetViewMatrix( ) * m_camera->GetProjMatrix( );
        m_lastViewProj          = viewProj;
        m_reprojectionMatrix    = viewProj.InversedHighPrecision( ) * m_previousViewProj;
    }

    auto raytracer = sceneRenderer->GetRaytracer();

    auto globalSettings = vaDrawAttributes::GlobalSettings();
    if( m_TAA != nullptr )
        globalSettings.MIPOffset = m_TAA->GetGlobalMIPOffset( );

    bool skipCameraLuminanceUpdate = false;

    vaViewport outputViewport   = m_camera->GetViewport();
    vaViewport workingViewport  = outputViewport;

    int msaaSampleCount = 1; // just in case we ever decide to support msaa again

#if 0 // some compute shader passes will not work with this anymore because I haven't written the manual SRGB conversion path
    vaResourceFormat formatPostTonemapColorRes  = vaResourceFormat::R8G8B8A8_TYPELESS;
    vaResourceFormat formatPostTonemapColorRTV  = vaResourceFormat::R8G8B8A8_UNORM_SRGB;
    vaResourceFormat formatPostTonemapColorSRV  = vaResourceFormat::R8G8B8A8_UNORM_SRGB;
    vaResourceFormat formatPostTonemapColorUAV  = vaResourceFormat::R8G8B8A8_UNORM;
#else
    vaResourceFormat formatPostTonemapColorRes  = vaResourceFormat::R11G11B10_FLOAT;    // vaResourceFormat::R16G16B16A16_FLOAT; // vaResourceFormat::R8G8B8A8_TYPELESS; 
    vaResourceFormat formatPostTonemapColorRTV  = formatPostTonemapColorRes;
    vaResourceFormat formatPostTonemapColorSRV  = formatPostTonemapColorRes;
    vaResourceFormat formatPostTonemapColorUAV  = formatPostTonemapColorRes;
#endif
    vaResourceFormat formatPreTonemapColor      = vaResourceFormat::R11G11B10_FLOAT;    // vaResourceFormat::R16G16B16A16_FLOAT

    if( m_SS != nullptr )
    {
        if( m_SS->AccumulationColor == nullptr || m_SS->AccumulationColor->GetWidth() != outputViewport.Width || m_SS->AccumulationColor->GetHeight() != outputViewport.Height )
        {
            m_SS->AccumulationColor = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R16G16B16A16_FLOAT, outputViewport.Width, outputViewport.Height, 1, 1, 1, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
            m_SSAOScratchBuffer->SetName( "SSAccumulation" );
        }

        workingViewport = vaViewport( outputViewport.Width * m_SS->GetSSResScale(), outputViewport.Height * m_SS->GetSSResScale() );

    } 
#if 0
    else if( m_settings.HalfResUpscale )
    {
        workingViewport.Width  = (workingViewport.Width  + 1) / 2;
        workingViewport.Height = (workingViewport.Height + 1) / 2;
    }
#endif

    // do our working buffers need to be re-created?
    if( m_workingDepth == nullptr || m_workingDepth->GetWidth( ) != workingViewport.Width || m_workingDepth->GetHeight( ) != workingViewport.Height || m_workingDepth->GetSampleCount() != msaaSampleCount )
    {

        if( m_outputColor == m_workingPostTonemapColor )
            m_outputColor = nullptr;
        if( m_outputDepth == m_workingDepth )
            m_outputDepth = nullptr;

        m_workingDepth              = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32_TYPELESS, workingViewport.Width, workingViewport.Height, 1, 1, msaaSampleCount, vaResourceBindSupportFlags::DepthStencil | vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default,
                                                            vaResourceFormat::R32_FLOAT, vaResourceFormat::Unknown, vaResourceFormat::D32_FLOAT, vaResourceFormat::Unknown );
        m_workingDepth->SetName( "WorkingDepth" );
        m_workingPreTonemapColor    = vaTexture::Create2D( GetRenderDevice(), formatPreTonemapColor, workingViewport.Width, workingViewport.Height, 1, 1, msaaSampleCount, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_workingPreTonemapColor->SetName( "WorkingPreTonemapColor" );

        m_workingPostTonemapColor   = vaTexture::Create2D( GetRenderDevice( ), formatPostTonemapColorRes, workingViewport.Width, workingViewport.Height, 1, 1, 1, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess,
            vaResourceAccessFlags::Default, formatPostTonemapColorSRV, formatPostTonemapColorRTV, vaResourceFormat::Unknown, formatPostTonemapColorUAV );
        m_workingPostTonemapColor->SetName( "WorkingPostTonemapColor" );

#if 1   // GTAO-specific: needed for GTAO!
        m_workingNormals        = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32_UINT, workingViewport.Width, workingViewport.Height, 1, 1, msaaSampleCount, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource );
        m_workingNormals->SetName( "WorkingNormals" );
#else
        m_workingNormals        = nullptr;
#endif
    }

    if( outputViewport == workingViewport ) // in this case we can just re-use working buffers
    {
        m_outputColor = m_workingPostTonemapColor;
        m_outputDepth = m_workingDepth;
    }
    else
    {
        // do our output buffers need to be re-created?
        if( m_outputDepth == nullptr || m_outputDepth->GetWidth( ) != outputViewport.Width || m_outputDepth->GetHeight( ) != outputViewport.Height )
        {
            m_outputColor = vaTexture::Create2D( GetRenderDevice( ), formatPostTonemapColorRes, outputViewport.Width, outputViewport.Height, 1, 1, 1, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess,
                vaResourceAccessFlags::Default, formatPostTonemapColorSRV, formatPostTonemapColorRTV, vaResourceFormat::Unknown, formatPostTonemapColorUAV );
            m_outputDepth = vaTexture::Create2D( GetRenderDevice( ), vaResourceFormat::R32_TYPELESS, outputViewport.Width, outputViewport.Height, 1, 1, 1, vaResourceBindSupportFlags::DepthStencil | vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default,
                vaResourceFormat::R32_FLOAT, vaResourceFormat::Unknown, vaResourceFormat::D32_FLOAT, vaResourceFormat::Unknown );
            m_outputColor->SetName( "OutputColor" );
            m_outputDepth->SetName( "OutputDepth" );
        }
    }


    if( m_SS != nullptr )
    {
        assert( m_SS->AccumulationColor != nullptr && m_SS->AccumulationColor->GetSizeX() == m_camera->GetViewportWidth() && m_SS->AccumulationColor->GetSizeY() == m_camera->GetViewportHeight() );

        m_SS->AccumulationColor->ClearRTV( renderContext, vaVector4( 0, 0, 0, 0 ) );

        int SSGridRes       = m_SS->GetSSGridRes( );
        int SSResScale      = m_SS->GetSSResScale( );
        float SSDDXDDYBias  = m_SS->GetSSDDXDDYBias( );
        float SSMIPBias     = m_SS->GetSSMIPBias( );
        float SSSharpen     = m_SS->GetSSSharpen( );

        vaDrawAttributes::GlobalSettings globalSSSettings = globalSettings;
        globalSSSettings.MIPOffset  = SSMIPBias;

        // SS messes up with pixel size which messes up with specular as it is based on ddx/ddy so compensate a bit here
        globalSSSettings.SpecularAAScale = vaMath::Lerp( (float)SSResScale, 1.0f, SSDDXDDYBias );

        float stepX = 1.0f / (float)SSGridRes;
        float stepY = 1.0f / (float)SSGridRes;
        float addMult = 1.0f / ( SSGridRes * SSGridRes );

        vaRandom fixedNoise( 0 );

        // TODO: switch to Halton sequence in the future
        for( int jx = 0; jx < SSGridRes; jx++ )
        {
            for( int jy = 0; jy < SSGridRes; jy++ )
            {
                vaVector2 offset( ( jx + 0.5f ) * stepX - 0.5f, ( jy + 0.5f ) * stepY - 0.5f );

                // vaVector2 rotatedOffset( offsetO.x * angleC - offsetO.y * angleS, offsetO.x * angleS + offsetO.y * angleC );

                // instead of angle-based rotation, do this weird grid shift
                offset.x += offset.y * stepX;
                offset.y += offset.x * stepY;

                // mainContext.SetRenderTarget( m_SSGBuffer->GetOutputColor( ), nullptr, true );

                m_camera->SetViewport( workingViewport );
                m_camera->SetSubpixelOffset( offset );
                m_camera->Tick( 0, false );

                globalSSSettings.Noise = vaVector2( fixedNoise.NextFloat( ), fixedNoise.NextFloat( ) );

                RenderTickInternal( renderContext, currentDrawResults, globalSSSettings, jx != 0 || jy != 0, (sceneRenderer->GeneralSettings( ).DepthPrepass)?(vaSceneMainRenderView::DepthPrepassType::DrawAndUse):(vaSceneMainRenderView::DepthPrepassType::None) );

                // this doesn't work if the user wants to use it as an actual depth buffer so let's think of something else
                // // if first loop, just copy over & downsample depth (using point filter) - this is not ideal by any means but we only use this depth for UI and debugging
                // if( jx == 0 && jy == 0 )
                //     currentDrawResults |= GetRenderDevice( ).StretchRect( renderContext, m_SS->DownsampledDepth, m_workingDepth, { 0,0,0,0 }, { 0,0,0,0 }, false, vaBlendMode::Opaque );

                if( SSResScale == 4 )
                {
                    // first downsample from 4x4 to 1x1
                    drawResults |= GetRenderDevice( ).GetPostProcess( ).Downsample4x4to1x1( renderContext, m_outputColor, m_workingPostTonemapColor, 0.0f );

                    // then accumulate
                    drawResults |= GetRenderDevice( ).StretchRect( renderContext, m_SS->AccumulationColor, m_outputColor, {0,0,0,0}, {0,0,0,0}, true, 
                                                                            vaBlendMode::Additive, vaVector4( addMult, addMult, addMult, addMult ) );
                }
                else
                {
                    assert( SSResScale == 1 || SSResScale == 2 );

                    // downsample and accumulate
                    drawResults |= GetRenderDevice( ).StretchRect( renderContext, m_SS->AccumulationColor, m_workingPostTonemapColor, {0,0,0,0}, {0,0,0,0}, true, 
                                                                            vaBlendMode::Additive, vaVector4( addMult, addMult, addMult, addMult ) );
                }
            }
        }

        // sharpen if needed, otherwise just save to m_SS->DownsampleTarget
        if( SSSharpen > 0 )
            drawResults |= GetRenderDevice( ).GetPostProcess( ).SimpleBlurSharpen( renderContext, m_outputColor, m_SS->AccumulationColor, SSSharpen );
        else
            drawResults |= GetRenderDevice( ).StretchRect( renderContext, m_outputColor, m_SS->AccumulationColor );

        // restore camera
        m_camera->SetViewport( outputViewport );
        m_camera->SetSubpixelOffset( );
        m_camera->Tick( 0, false );

        // and draw one 'normal res' depth because external stuff might need the depth
        {
            m_outputDepth->ClearDSV( renderContext, true, m_camera->GetUseReversedZ( ) ? ( 0.0f ) : ( 1.0f ), false, 0 );
            drawResults |= sceneRenderer->DrawDepthOnly( renderContext, vaRenderOutputs::FromRTDepth( nullptr, m_outputDepth ), m_selectionOpaque, m_sortDepthPrepass, *m_camera, globalSettings );
            m_basicStats.ItemsDrawn += (int)m_selectionOpaque.Count( );
        }
    }
#if 0
    else if( m_settings.HalfResUpscale )
    {
        m_camera->SetSubpixelOffset( vaVector2( -0.5f, -0.5f ) );
        m_camera->Tick( 0, false );

        // first high-res depth
        {
            m_outputDepth->ClearDSV( renderContext, true, m_camera->GetUseReversedZ( ) ? ( 0.0f ) : ( 1.0f ), false, 0 );
            drawResults |= sceneRenderer->DrawDepthOnly( renderContext, vaRenderOutputs::FromRTDepth( nullptr, m_outputDepth ), m_selectionOpaque, m_sortDepthPrepass, *m_camera, globalSettings );
            m_basicStats.ItemsDrawn += (int)m_selectionOpaque.Count( );
        }

        m_camera->SetViewport( workingViewport );
        m_camera->SetSubpixelOffset( );
        m_camera->Tick( 0, false );

        // now low-res depth
        {
#if 0
            m_workingDepth->ClearDSV( renderContext, true, m_camera->GetUseReversedZ( ) ? ( 0.0f ) : ( 1.0f ), false, 0 );
            drawResults |= sceneRenderer->DrawDepthOnly( renderContext, vaRenderOutputs::FromRTDepth( nullptr, m_workingDepth ), m_selectionOpaque, m_sortDepthPrepass, *m_camera, globalSettings );
            m_basicStats.ItemsDrawn += (int)m_selectionOpaque.Count( );
#else
            vaGraphicsItem graphicsItem;
            vaRenderOutputs outputs;
            GetRenderDevice().FillFullscreenPassGraphicsItem( graphicsItem );
            graphicsItem.ShaderResourceViews[1] = m_outputDepth;
            graphicsItem.PixelShader = m_PSSmartUpsampleDepth;
            graphicsItem.DepthEnable = true;
            graphicsItem.DepthWriteEnable = true;
            graphicsItem.DepthFunc = vaComparisonFunc::Always;
            vaDrawAttributes drawAttributes( *m_camera );
            renderContext.ExecuteSingleItem( graphicsItem, vaRenderOutputs::FromRTDepth( /*m_workingPostTonemapColor*/nullptr, m_workingDepth ), &drawAttributes );
#endif
        }

        RenderTickInternal( renderContext, currentDrawResults, globalSettings, skipCameraLuminanceUpdate, vaSceneMainRenderView::DepthPrepassType::UseExisting );

        // restore camera
        m_camera->SetViewport( outputViewport );
        m_camera->SetSubpixelOffset( );
        m_camera->Tick( 0, false );

#if 0
        {
            vaComputeItem computeItem;
            vaRenderOutputs outputs;

            //computeItem.ConstantBuffers[ZOOMTOOL_CONSTANTSBUFFERSLOT] = m_constantsBuffer;
            outputs.UnorderedAccessViews[0] = m_outputColor;
            computeItem.ShaderResourceViews[0] = m_workingPostTonemapColor;
            computeItem.ShaderResourceViews[1] = m_workingDepth;
            computeItem.ShaderResourceViews[2] = m_outputDepth;

            int threadGroupCountX = ( m_outputColor->GetSizeX( ) + 16 - 1 ) / 16;
            int threadGroupCountY = ( m_outputColor->GetSizeY( ) + 16 - 1 ) / 16;

            computeItem.ComputeShader = ( vaResourceFormatHelpers::IsFloat( m_outputColor->GetUAVFormat( ) ) ) ? ( m_CSSmartUpsampleFloat.get( ) ) : ( m_CSSmartUpsampleUnorm.get( ) );
            computeItem.SetDispatch( threadGroupCountX, threadGroupCountY, 1 );

            vaDrawAttributes drawAttributes( *m_camera );
            renderContext.ExecuteSingleItem( computeItem, outputs, &drawAttributes );

        }
#else
        {
            drawResults |= GetRenderDevice( ).StretchRect( renderContext, m_outputColor, m_workingPostTonemapColor, {0,0,0,0}, {0,0,0,0}, false );
        }
#endif
    }
#endif
    else
    {
        if( m_enableCursorHoverInfo )
        {
            globalSettings.CursorViewportPos = vaInputMouseBase::GetCurrent( )->GetCursorClientPosDirect( );
            globalSettings.CursorHoverInfoCollect = true;
        }
        RenderTickInternal( renderContext, currentDrawResults, globalSettings, skipCameraLuminanceUpdate, (sceneRenderer->GeneralSettings( ).DepthPrepass)?(vaSceneMainRenderView::DepthPrepassType::DrawAndUse):(vaSceneMainRenderView::DepthPrepassType::None) );
    }

    drawResults |= m_selectionOpaque.ResultFlags( );
    drawResults |= m_selectionTransparent.ResultFlags( );
    m_basicStats.DrawResultFlags |= drawResults;
    currentDrawResults |= drawResults;
    m_selectionOpaque.Reset( );
    m_selectionTransparent.Reset( );

    if( m_TAA != nullptr )
    {
        // should've been set by TAA
        assert( m_camera->GetSubpixelOffset( ) == m_TAA->GetCurrentJitter() );
        m_camera->SetSubpixelOffset( vaVector2(0,0) );
    }
    else
    {
        // something else is messing with jitter? upgrade code to handle it
        assert( m_camera->GetSubpixelOffset( ) == vaVector2(0,0) );
    }
}

bool vaSceneMainRenderView::RequiresRaytracing( ) const
{ 
    // additional requirements will be added here
    return m_settings.PathTracer || (m_GTAO != nullptr && m_GTAO->ReferenceRTAOEnabled()); 
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

    ProcessInstanceBatchCommon( scene.Registry( ), items, itemCount, &m_selectionOpaque, nullptr, vaRenderInstanceList::FilterSettings::ShadowmapCull( *m_shadowmap ), m_selectionFilter, baseInstanceIndex );
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
        ProcessInstanceBatchCommon( scene.Registry( ), items, itemCount, &m_selectionOpaque, &m_selectionTransparent, vaRenderInstanceList::FilterSettings::EnvironmentProbeCull( m_probeData ), m_selectionFilter, baseInstanceIndex );
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
