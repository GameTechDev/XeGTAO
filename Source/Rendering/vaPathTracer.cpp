///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaPathTracer.h"

#include "Core/vaInput.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/vaRenderDeviceContext.h"

#include "Rendering/vaRenderDevice.h"
#include "Rendering/vaRenderGlobals.h"
#include "Rendering/vaShader.h"
#include "Rendering/vaRenderBuffers.h"
#include "Rendering/vaTexture.h"

#include "Rendering/vaSceneLighting.h"

#include "Rendering/Effects/vaSkybox.h"

#include "Rendering/vaRenderMaterial.h"
#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaAssetPack.h"

#include "Rendering/Shaders/vaRaytracingShared.h"
#include "Rendering/Shaders/vaPathTracerShared.h"

#include "Rendering/vaSceneRaytracing.h"

#include "Rendering/vaGPUSort.h"

using namespace Vanilla;

vaPathTracer::vaPathTracer( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params )
    //    , vaUIPanel( "PathTracer", 10, true, vaUIPanel::DockLocation::DockedLeftBottom )
    , m_constantBuffer( vaConstantBuffer::Create<ShaderPathTracerConstants>( params.RenderDevice, "ShaderPathTracerConstants" ) )
{
    // manually created material for the mirror
    m_divergenceMirrorMaterial = GetRenderDevice().GetMaterialManager().CreateRenderMaterial( );
    m_divergenceMirrorMaterial->SetupFromPreset( "FilamentStandard" );
    m_divergenceMirrorMaterial->SetInputSlotDefaultValue( "Roughness", 0.0f );
    m_divergenceMirrorMaterial->SetInputSlotDefaultValue( "Metallic", 1.0f );

    m_pathTracerControl = vaRenderBuffer::Create<ShaderPathTracerControl>( GetRenderDevice(), 1, vaRenderBufferFlags::None, "PathTracerControl" );

#if VA_USE_RAY_SORTING
    m_GPUSort = GetRenderDevice().CreateModule<vaGPUSort>( );
#endif
}

vaPathTracer::~vaPathTracer( ) 
{ 
    m_divergenceMirrorTestEnabled = false;
    UpdateDivergenceMirrorExperiment( );
}

void vaPathTracer::UITick( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    if( ImGui::CollapsingHeader("Path tracer", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        VA_GENERIC_RAII_SCOPE( ImGui::Indent();, ImGui::Unindent(); );

        ImGui::Text( "Accumulated frames: %d (out of %d target)", m_accumFrameCount, m_accumFrameTargetCount );

        if( ImGui::Checkbox( "Anti-aliasing", &m_enableAA ) )
            m_accumFrameCount = 0;
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Enables subpixel jitter (not completely functional yet)" );

        if( ImGui::InputInt( "Max bounces", &m_maxBounces ) )
            m_accumFrameCount = 0;
        m_maxBounces = vaMath::Clamp( m_maxBounces, 0, c_maxBounceUpperBound );
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "How many times does the ray bounce before stopping" );

        if( ImGui::InputInt( "Accumulation target count", &m_accumFrameTargetCount ) )
            m_accumFrameCount = 0;
        m_accumFrameTargetCount = vaMath::Clamp( m_accumFrameTargetCount, 1, 1048576 );
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "How many samples to collect before stopping (will restart on camera/system changes)" );

        //ImGui::Separator();
        if( ImGui::CollapsingHeader( "Advanced", 0 ) )
        {
            if( ImGui::Checkbox( "Path space regularization", &m_enablePathRegularization ) )
                m_accumFrameCount = 0;
        }

        //ImGui::Separator();
        if( ImGui::CollapsingHeader( "Debugging", 0 ) )
        {
            m_debugViz = vaMath::Clamp( m_debugViz, ShaderDebugViewType::None, ShaderDebugViewType::ShaderID );
            if( ImGuiEx_Combo( "Debug view", (int&)m_debugViz, { "None", "Bounce heatmap", "Viewspace depth", "GeometryTexcoord0",
                "GeometryNormalNonInterpolated", "GeometryNormalInterpolated", "GeometryTangentInterpolated", "GeometryBitangentInterpolated", "ShadingNormal", 
                "MaterialBaseColor", "MaterialBaseColorAlpha", "MaterialEmissive", "MaterialMetalness", "MaterialRoughness", "MaterialReflectance", "MaterialAmbientOcclusion", 
                "ReflectivityEstimate", "MaterialID", "ShaderID" } ) )
                m_accumFrameCount = 0;

            ImGui::Checkbox( "Show ray path under cursor", &m_debugPathUnderCursor );
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Show debug visualization for all rays from the current sample path starting from under mouse cursor pixel" );
            if( m_debugPathUnderCursor )
            {
                VA_GENERIC_RAII_SCOPE( ImGui::Indent();, ImGui::Unindent(); );
                ImGui::InputInt( "Number of rays to show (dim x dim)", &m_debugPathVizDim );
                m_debugPathVizDim = vaMath::Clamp( m_debugPathVizDim, 1, 10 );
            }

            ImGui::Checkbox( "Show direct lighting visualization", &m_debugLightsForPathUnderCursor );
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Show debug visualization for all visibility rays for the path starting from under mouse cursor pixel" );

            //ImGui::Checkbox( "Draw crosshair under cursor", &m_debugCrosshairUnderCursor );
            //if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "This is useful for tracking that single annoying pixel under zoom view for example (as other viz doesn't show zoomed up in it)" );

        }

        if( ImGui::CollapsingHeader( "Experiments", 0 ) )
        {
            if( ImGui::InputFloat( "Primary ray direction noise", &m_debugForcedDispatchDivergence ) )
                m_accumFrameCount = 0;
            m_debugForcedDispatchDivergence = vaMath::Clamp( m_debugForcedDispatchDivergence, 0.0f, 1.0f );
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Adds randomness to primary ray direction for stressing divergence" );

            if( ImGui::CollapsingHeader( "Divergence stress test mirrors", 0 ) )
            {
                VA_GENERIC_RAII_SCOPE( ImGui::Indent();, ImGui::Unindent(); );

                if( m_paintingsMesh == nullptr )
                    m_paintingsMesh = GetRenderDevice().GetAssetPackManager( ).FindRenderMesh( "_paris_paintings_l_l" );

                if( m_paintingsMesh == nullptr )
                    ImGui::Text( "Unable to find paintings mesh - is Bistro loaded?" );
                else
                {
                    if( ImGui::Checkbox( "Enable", &m_divergenceMirrorTestEnabled ) )
                    {
                        UpdateDivergenceMirrorExperiment( );
                        m_accumFrameCount = 0;
                    }

                    if( ImGui::Button( "Move camera to test pos", { -1, 0 } ) )
                        m_divergenceMirrorCameraSetPos = true;

                    if( ImGui::SliderFloat( "Roughness (slider)", &m_divergenceMirrorRoughness, 0.0f, 1.0f ) || ImGui::InputFloat( "Roughness", &m_divergenceMirrorRoughness ) )
                    {
                        m_divergenceMirrorRoughness = vaMath::Clamp( m_divergenceMirrorRoughness, 0.0f, 1.0f );
                        m_divergenceMirrorMaterial->SetInputSlotDefaultValue( "Roughness", m_divergenceMirrorRoughness );
                        m_accumFrameCount = 0;
                    }
                }
                
            }

            if( m_replaceAllBistroMaterials == 2 )
            {
                ImGui::Separator( );
                ImGui::TextWrapped( "All bistro materials have been replaced by a single one for performance testing - there is no way to undo this other than by restarting the app, sorry :)" );
                ImGui::Separator( );
            }
            if( m_replaceAllBistroMaterials == 1 && ImGui::Button( "Are you sure? To revert, app must be restarted!" ) )
            {
                shared_ptr<vaRenderMaterial> replMat = GetRenderDevice().GetAssetPackManager( ).FindRenderMaterial( "xxx_master_interior_01_plaster" ); // xxx_master_interior_01_plaster_red
                shared_ptr<vaAssetPack> pack = GetRenderDevice().GetAssetPackManager( ).FindLoadedPack( "new_bistro_meshes" );
                if( pack == nullptr || replMat == nullptr )
                {
                    VA_ERROR( "Unable to find Bistro meshes asset pack or replacement material" );
                }
                else
                {
                    m_replaceAllBistroMaterials++;
                    std::unique_lock<mutex> packLock( pack->Mutex( ) );

                    for( int i = 0; i < pack->Count( false ); i++ )
                    {
                        auto mesh = vaAssetRenderMesh::SafeCast( pack->At( i, false ) );
                        if( mesh == nullptr )
                            continue;
                        mesh->GetRenderMesh( )->SetMaterial( replMat );
                    }
                    m_accumFrameCount = 0;
                }
            }
            if( m_replaceAllBistroMaterials == 0 && ImGui::Button( "Replace all bistro material with a single one" ) )
                m_replaceAllBistroMaterials++;

        }

    }

#endif
}

void vaPathTracer::UpdateDivergenceMirrorExperiment( )
{
    if( m_divergenceMirrorTestEnabled && m_paintingsMaterialBackup == nullptr )
    {
        if( m_paintingsMesh == nullptr )
        { assert( false ); return; }
        m_paintingsMaterialBackup = m_paintingsMesh->GetMaterial( );
        m_paintingsMesh->SetMaterial( m_divergenceMirrorMaterial );
    }
    else if( !m_divergenceMirrorTestEnabled && m_paintingsMaterialBackup != nullptr )
    {
        if( m_paintingsMesh == nullptr )
        { assert( false ); return; }
        assert( m_paintingsMesh->GetMaterial() == m_divergenceMirrorMaterial );
        m_paintingsMesh->SetMaterial( m_paintingsMaterialBackup );
        m_paintingsMaterialBackup = nullptr;
    }

}

vaDrawResultFlags vaPathTracer::Draw( vaRenderDeviceContext & renderContext, vaDrawAttributes & drawAttributes, const shared_ptr<vaSkybox> & skybox, const shared_ptr<vaTexture> & outputColor, const shared_ptr<vaTexture> & outputDepth )
{
    VA_TRACE_CPUGPU_SCOPE( PathTracerAll, renderContext );
    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    skybox;

    assert( drawAttributes.Raytracing != nullptr );

    // this will cause bugs - path tracing (at the moment) does not support primary ray AO from SSAO
    assert( drawAttributes.Lighting->GetAOMap( ) == nullptr );

    // we should have never reached this point if raytracing isn't supported!
    assert( m_renderDevice.GetCapabilities().Raytracing.Supported );

    // one-time init
    if( m_shaderLibrary == nullptr )
    {
        vaShaderMacroContaner macros = { { "VA_RAYTRACING", "" } };

        std::vector<shared_ptr<vaShader>> allShaders;

        allShaders.push_back( m_shaderLibrary   = vaShaderLibrary::CreateFromFile(  GetRenderDevice(), "vaPathTracer.hlsl", "", macros, true ) ); //, { "VA_RTAO_MAX_NUM_BOUNCES", vaStringTools::Format("%d", maxNumBounces) } }, true );

        allShaders.push_back( m_PSWriteToOutput = vaPixelShader::CreateFromFile(    GetRenderDevice(), "vaPathTracer.hlsl", "PSWriteToOutput", macros, true ) );

        allShaders.push_back( m_CSKickoff       = vaComputeShader::CreateFromFile(  GetRenderDevice(), "vaPathTracer.hlsl", "CSKickoff", macros, true ) );

        // wait until shaders are compiled! this allows for parallel compilation but ensures all are compiled after this point
        for( auto sh : allShaders ) sh->WaitFinishIfBackgroundCreateActive();

        m_accumFrameCount   = 0;        // restart accumulation (if any)
    }

    // camera or shaders changed? reset accumulation
    int64 shaderContentsID = vaShader::GetLastUniqueShaderContentsID( );
    if( m_accumLastCamera.GetViewport( ) != drawAttributes.Camera.GetViewport( ) || m_accumLastCamera.GetViewMatrix( ) != drawAttributes.Camera.GetViewMatrix( ) || m_accumLastCamera.GetProjMatrix( ) != drawAttributes.Camera.GetProjMatrix( ) 
        || m_accumLastShadersID != shaderContentsID )
        m_accumFrameCount       = 0;    // restart accumulation (if any)

    const uint startingRayCount = ((outputColor->GetWidth() + (VA_PATH_TRACER_DISPATCH_TILE_SIZE-1))/VA_PATH_TRACER_DISPATCH_TILE_SIZE) * ((outputColor->GetHeight() + (VA_PATH_TRACER_DISPATCH_TILE_SIZE-1))/VA_PATH_TRACER_DISPATCH_TILE_SIZE)
                                    * VA_PATH_TRACER_DISPATCH_TILE_SIZE*VA_PATH_TRACER_DISPATCH_TILE_SIZE;

    // per-framebuffer-resize init
    if( m_radianceAccumulation == nullptr || m_radianceAccumulation->GetSize( ) != outputColor->GetSize( ) )
    {
        m_radianceAccumulation  = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32G32B32A32_FLOAT, outputColor->GetWidth(), outputColor->GetHeight(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_viewspaceDepth        = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32_FLOAT, outputColor->GetWidth(), outputColor->GetHeight(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_accumFrameCount       = 0;    // restart accumulation (if any)

        m_pathGeometryHitInfoPayloads   = vaRenderBuffer::Create<ShaderGeometryHitPayload>( GetRenderDevice(), startingRayCount, vaRenderBufferFlags::None, "PathGeometryHitInfoPayloads" );
        m_pathPayloads                  = vaRenderBuffer::Create<ShaderPathPayload>( GetRenderDevice(), startingRayCount, vaRenderBufferFlags::None, "PathPayloads" );

        std::vector<uint32> unsortedPaths( startingRayCount, 0 ); 
        std::iota(unsortedPaths.begin(), unsortedPaths.end(), 0);   // set indices to {0, 1, ..., startingRayCount-1}
        //m_pathListUnsorted              = vaRenderBuffer::Create( GetRenderDevice(), startingRayCount, vaResourceFormat::R32_UINT, vaRenderBufferFlags::None, "PathListUnsorted",   unsortedPaths.data() );
        m_pathListSorted                = vaRenderBuffer::Create( GetRenderDevice(), startingRayCount, vaResourceFormat::R32_UINT, vaRenderBufferFlags::None, "PathListSorted",     unsortedPaths.data() );

        m_pathSortKeys                  = vaRenderBuffer::Create( GetRenderDevice(), startingRayCount, vaResourceFormat::R32_UINT, vaRenderBufferFlags::None, "PathKeys",   nullptr );
        //m_pathSortKeysSorted            = vaRenderBuffer::Create( GetRenderDevice(), startingRayCount, vaResourceFormat::R32_UINT, vaRenderBufferFlags::None, "PathKeysSorted",     nullptr );
    }
    

    // // for now all debug viz-s require 1 sample only
    // if( m_debugViz != ShaderDebugViewType::None )
    //     m_accumFrameCount   = 0;

    // if accumulating from scratch, save the settings that require a restart on change (such as camera location)
    if( m_accumFrameCount == 0 )
    {
        m_accumLastCamera       = drawAttributes.Camera;
        m_accumLastShadersID    = shaderContentsID;
    }

#ifndef VA_USE_RAY_SORTING
#error VA_USE_RAY_SORTING must be defined to 0 or 1
#endif

    // constants buffer
    ShaderPathTracerConstants constants;
    {
        memset( &constants, 0, sizeof(constants) );

        if( skybox != nullptr )
            skybox->UpdateConstants( drawAttributes, constants.Sky );
        
        constants.MaxBounces                = m_maxBounces;

        if( (uint)m_debugViz >= (uint)ShaderDebugViewType::SurfacePropsBegin && (uint)m_debugViz <= (uint)ShaderDebugViewType::SurfacePropsEnd )
            constants.MaxBounces            = 0;

        constants.AccumFrameCount           = m_accumFrameCount;
        constants.AccumFrameTargetCount     = m_accumFrameTargetCount;
        constants.EnableAA                  = m_enableAA?1:0;

        constants.ViewportX                 = m_radianceAccumulation->GetWidth( );
        constants.ViewportY                 = m_radianceAccumulation->GetHeight( );
        constants.StartPathCount            = m_radianceAccumulation->GetWidth( ) * m_radianceAccumulation->GetHeight( );
        constants.Padding0                  = 0;
        //constants.DispatchRaysWidth         = m_radianceAccumulation->GetWidth( );
        //constants.DispatchRaysHeight        = m_radianceAccumulation->GetHeight( );
        //constants.DispatchRaysDepth         = 1;

        constants.DebugViewType             = m_debugViz;
        constants.Flags                     = 0;
        constants.Flags                     |= (m_enablePathRegularization)?(VA_RAYTRACING_FLAG_PATH_REGULARIZATION):(0);
        constants.Flags                     |= (m_debugPathUnderCursor)?(VA_RAYTRACING_FLAG_SHOW_DEBUG_PATH_VIZ):(0);
        constants.Flags                     |= (m_debugLightsForPathUnderCursor)?(VA_RAYTRACING_FLAG_SHOW_DEBUG_LIGHT_VIZ):(0);

        constants.DebugDivergenceTest       = m_debugForcedDispatchDivergence;

        constants.DebugPathVizDim           = m_debugPathVizDim;
        
        m_constantBuffer->Upload( renderContext, constants );
    }

    vaRenderOutputs uavInputsOutputs;
    uavInputsOutputs.UnorderedAccessViews[0] = m_radianceAccumulation;
    uavInputsOutputs.UnorderedAccessViews[1] = m_viewspaceDepth;
    uavInputsOutputs.UnorderedAccessViews[2] = m_pathGeometryHitInfoPayloads;
    uavInputsOutputs.UnorderedAccessViews[3] = m_pathPayloads;
    uavInputsOutputs.UnorderedAccessViews[4] = m_pathTracerControl;
    //uavInputsOutputs.UnorderedAccessViews[5] = m_pathListUnsorted;                
    uavInputsOutputs.UnorderedAccessViews[6] = m_pathListSorted;                // this could/should be a SRV
    uavInputsOutputs.UnorderedAccessViews[7] = m_pathSortKeys;
    
    // used in shaders for Noise3D - gets added to object space noise and is not related to path tracing sampling noise
    vaRandom accumulateNoise( m_accumFrameCount );
    drawAttributes.Settings.Noise       = vaVector2( accumulateNoise.NextFloat( ), accumulateNoise.NextFloat( ) );

    // globals
    vaRaytraceItem raytraceItem;
    raytraceItem.ConstantBuffers[VA_PATH_TRACER_CONSTANTBUFFER_SLOT] = m_constantBuffer;
    raytraceItem.ShaderResourceViews[VA_PATH_TRACER_SKYBOX_SRV_SLOT] = (skybox==nullptr)?(nullptr):(skybox->GetCubemap());
    raytraceItem.ShaderResourceViews[VA_PATH_TRACER_NULL_ACC_STRUCT] = drawAttributes.Raytracing->GetNullAccelerationStructure( );
    raytraceItem.ShaderLibrary                  = m_shaderLibrary;
    raytraceItem.RayGen              = "Raygen";
    raytraceItem.AnyHit              = "";
    raytraceItem.Miss                = "Miss";
    raytraceItem.MissSecondary       = "MissVisibility";
#if VA_USE_RAY_SORTING
    raytraceItem.ClosestHit          = "ClosestHit";
    raytraceItem.MaterialClosestHit  = "";
    raytraceItem.MaterialMissCallable= "PathTraceSurfaceResponse";
#else
    raytraceItem.ClosestHit          = "";
    raytraceItem.MaterialClosestHit  = "PathTraceClosestHit";
#endif
    raytraceItem.MaxRecursionDepth              = VA_PATH_TRACER_MAX_RECURSION;
    raytraceItem.MaxPayloadSize                 = sizeof(ShaderMultiPassRayPayload);
    vaComputeItem computeItem;
    computeItem.ConstantBuffers[VA_PATH_TRACER_CONSTANTBUFFER_SLOT] = m_constantBuffer;
    //computeItem.ShaderResourceViews[VA_PATH_TRACER_SKYBOX_SRV_SLOT] = (skybox==nullptr)?(nullptr):(skybox->GetCubemap());

    // initialize control buffer!
    {
        ShaderPathTracerControl control;
        control.CurrentRayCount     = constants.StartPathCount; // <- not dynamically updating
        control.NextRayCount        = constants.StartPathCount; // <- not dynamically updating
        m_pathTracerControl->UploadSingle( renderContext, control, 0 );
    }

    {   // initialize ("kickoff")
#if VA_USE_RAY_SORTING
        VA_TRACE_CPUGPU_SCOPE( RaygenKickoff, renderContext );
        vaRaytraceItem raytraceItemKickoff = raytraceItem;
        raytraceItemKickoff.RayGen = "RaygenKickoff";
        raytraceItemKickoff.SetDispatch( startingRayCount );
        drawResults |= renderContext.ExecuteSingleItem( raytraceItemKickoff, uavInputsOutputs, &drawAttributes );
#else
        VA_TRACE_CPUGPU_SCOPE( Kickoff, renderContext );
        computeItem.ComputeShader = m_CSKickoff;
        computeItem.SetDispatch( (constants.StartPathCount + 31)/32 );
        drawResults |= renderContext.ExecuteSingleItem( computeItem, uavInputsOutputs, &drawAttributes );
#endif
    }

    for( int bounce = 0; bounce <= m_maxBounces; bounce++ ) // <= because first ray is primary ray - if we start from a raster pass then it will be different
    {
        VA_TRACE_CPUGPU_SCOPE_CUSTOMNAME( pass, vaStringTools::Format( "pass_%d", bounce ).c_str(), renderContext );
        // {   // manage counters
        //     VA_TRACE_CPUGPU_SCOPE( TickCounters, renderContext );
        //     computeItem.ComputeShader = m_CSTickCounters;
        //     computeItem.SetDispatch( 1 );
        //     drawResults |= renderContext.ExecuteSingleItem( computeItem, uavInputsOutputs, nullptr );
        // }

#if VA_USE_RAY_SORTING
        const uint32 maxSortKey = std::as_const(GetRenderDevice().GetMaterialManager()).Materials().Count()+1;
        m_GPUSort->Sort( renderContext, m_pathSortKeys, m_pathListSorted, startingRayCount, maxSortKey );
#endif

        {   // raygen pass
            VA_TRACE_CPUGPU_SCOPE( PathTracing, renderContext );

            raytraceItem.SetDispatch( startingRayCount );

            drawResults |= renderContext.ExecuteSingleItem( raytraceItem, uavInputsOutputs, &drawAttributes );
        }
    }

    // raytracing no longer needed
    vaDrawAttributes nonRTAttrs = drawAttributes;
    nonRTAttrs.Raytracing = nullptr;

    // apply to render target
    // (used to be a compute shader - this makes more sense when outputting depth)
    {
        vaGraphicsItem graphicsItem;
        GetRenderDevice().FillFullscreenPassGraphicsItem( graphicsItem );
        graphicsItem.ConstantBuffers[VA_PATH_TRACER_CONSTANTBUFFER_SLOT] = m_constantBuffer;
        graphicsItem.ShaderResourceViews[VA_PATH_TRACER_RADIANCE_SRV_SLOT] = m_radianceAccumulation;
        graphicsItem.ShaderResourceViews[VA_PATH_TRACER_VIEWSPACE_DEPTH_SRV_SLOT] = m_viewspaceDepth;
        graphicsItem.PixelShader        = m_PSWriteToOutput;
        //graphicsItem.SetDispatch( (outputColor->GetWidth( ) + 7) / 8, (outputColor->GetHeight( ) + 7) / 8, 1 );
        graphicsItem.BlendMode          = vaBlendMode::Opaque;
        graphicsItem.DepthEnable        = true;
        graphicsItem.DepthWriteEnable   = true;
        graphicsItem.DepthFunc          = vaComparisonFunc::Always;

        drawResults |= renderContext.ExecuteSingleItem( graphicsItem, vaRenderOutputs::FromRTDepth( outputColor, outputDepth ), &nonRTAttrs );
    }

    m_accumFrameCount = std::min( m_accumFrameCount+1, m_accumFrameTargetCount );

    // if there was an error during rendering (such as during shader recompiling and not available)
    if( drawResults != vaDrawResultFlags::None )
        m_accumFrameCount = 0;      // restart accumulation (if any)

    // this has to be done again after the draw, just in case a shader got recompiled in the meantime
    shaderContentsID = vaShader::GetLastUniqueShaderContentsID( );
    if( m_accumLastShadersID != shaderContentsID )
        m_accumFrameCount = 0;      // restart accumulation (if any)

    return drawResults;
}



