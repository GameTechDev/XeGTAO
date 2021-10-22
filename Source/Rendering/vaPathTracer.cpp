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
#include "IntegratedExternals/vaOIDNIntegration.h"  // VA_OIDN_INTEGRATION_ENABLED

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

namespace Vanilla
{
#ifdef VA_OIDN_INTEGRATION_ENABLED
    struct OIDNData
    {
        OIDNDevice                  Device              = nullptr;
        OIDNFilter                  Filter              = nullptr;

        OIDNBuffer                  Beauty              = nullptr;
        OIDNBuffer                  Output              = nullptr;
        OIDNBuffer                  AuxAlbedo           = nullptr;
        OIDNBuffer                  AuxNormals          = nullptr;

        shared_ptr<vaTexture>       BeautyGPU;
        shared_ptr<vaTexture>       BeautyCPU;

        shared_ptr<vaTexture>       DenoisedGPU;
        shared_ptr<vaTexture>       DenoisedCPU;

        shared_ptr<vaTexture>       AuxAlbedoGPU;
        shared_ptr<vaTexture>       AuxAlbedoCPU;
        shared_ptr<vaTexture>       AuxNormalsGPU;
        shared_ptr<vaTexture>       AuxNormalsCPU;

        shared_ptr<vaComputeShader> CSPrepareDenoiserInputs;
        //shared_ptr<vaComputeShader> CSDebugDisplayDenoiser;

        int                         Width               = 0;
        int                         Height              = 0;
        int                         BytesPerPixel       = 0;
        size_t                      BufferSize          = 0;

        OIDNData( )
        {
            Device = oidnNewDevice( OIDN_DEVICE_TYPE_DEFAULT );
            //oidnSetDevice1b( Device, "setAffinity", false );
            //oidnSetDevice1i( Device, "numThreads", vaThreading::GetCPULogicalCores() );
            oidnCommitDevice( Device );
            // Create a filter for denoising a beauty (color) image using optional auxiliary images too
            Filter = oidnNewFilter( Device, "RT" ); // generic ray tracing filter
            
        }
        ~OIDNData( )
        {
            oidnReleaseBuffer(Beauty);
            oidnReleaseBuffer(Output);
            oidnReleaseBuffer(AuxAlbedo );
            oidnReleaseBuffer(AuxNormals);
            oidnReleaseFilter(Filter);
            oidnReleaseDevice(Device);
        }

        void UpdateTextures( vaRenderDevice & device, int width, int height )
        {
            if( BeautyGPU == nullptr || Width != width || Height != height )
            {
                Width  = width;
                Height = height;

                if( Beauty      != nullptr ) oidnReleaseBuffer(Beauty);
                if( Output      != nullptr ) oidnReleaseBuffer(Output);
                if( AuxAlbedo   != nullptr ) oidnReleaseBuffer(AuxAlbedo);
                if( AuxNormals  != nullptr ) oidnReleaseBuffer(AuxNormals);

                BeautyGPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess );
                BeautyCPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead );

                DenoisedGPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess );
                DenoisedCPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPUWrite );

                AuxNormalsGPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess );
                AuxNormalsCPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead );
                AuxAlbedoGPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess );
                AuxAlbedoCPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead );

                BytesPerPixel = 4*4;
                BufferSize    = Width * Height * BytesPerPixel;

                Beauty      = oidnNewBuffer( Device, BufferSize );
                Output      = oidnNewBuffer( Device, BufferSize );
                AuxAlbedo   = oidnNewBuffer( Device, BufferSize );
                AuxNormals  = oidnNewBuffer( Device, BufferSize );

                oidnSetFilterImage( Filter, "color",    Beauty, OIDN_FORMAT_FLOAT3, Width, Height, 0, BytesPerPixel, BytesPerPixel * Width );
                oidnSetFilterImage( Filter, "output",   Output, OIDN_FORMAT_FLOAT3, Width, Height, 0, BytesPerPixel, BytesPerPixel * Width );
                oidnSetFilterImage( Filter, "albedo",   AuxAlbedo, OIDN_FORMAT_FLOAT3, Width, Height, 0, BytesPerPixel, BytesPerPixel * Width );
                oidnSetFilterImage( Filter, "normal",   AuxNormals, OIDN_FORMAT_FLOAT3, Width, Height, 0, BytesPerPixel, BytesPerPixel * Width );
                oidnSetFilter1b( Filter, "hdr", true );         // beauty image is HDR
                oidnSetFilter1b( Filter, "cleanAux", true );    // auxiliary images are not noisy
                oidnCommitFilter( Filter );
            }
        }

        static void CopyContents( vaRenderDeviceContext & renderContext, OIDNBuffer destination, const shared_ptr<vaTexture> & source, size_t bufferSize )
        {
            // map CPU buffer Vanilla side
            if( !source->TryMap( renderContext, vaResourceMapType::Read ) )
            { assert( false ); return; }
            // map CPU buffer OIDN side
            void * dst = oidnMapBuffer( destination, OIDN_ACCESS_WRITE_DISCARD, 0, bufferSize );
            // copy CPU Vanilla -> CPU OIDN 
            memcpy( dst, source->GetMappedData()[0].Buffer, bufferSize );
            // unmap OIDN side
            oidnUnmapBuffer( destination, dst );
            // unmap Vanilla side
            source->Unmap( renderContext );
        }

        static void CopyContents( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & destination, OIDNBuffer source, size_t bufferSize )
        {
            // map Vanilla-side
            if( !destination->TryMap( renderContext, vaResourceMapType::Write ) )
            { assert( false ); return; }
            // map OIDN-side
            void * src = oidnMapBuffer( source, OIDN_ACCESS_READ, 0, bufferSize );
            // copy CPU OIDN -> CPU Vanilla
            memcpy( destination->GetMappedData()[0].Buffer, src, bufferSize );
            oidnUnmapBuffer( source, src );
            destination->Unmap( renderContext );
        }

        void VanillaToDenoiser( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & beautySrc )
        {
            VA_TRACE_CPU_SCOPE( VanillaToDenoiser );

            // copy GPU any-color-format -> GPU R32G32B32A32_FLOAT
            renderContext.CopySRVToRTV( BeautyGPU, beautySrc );
            // copy GPU -> CPU Vanilla side
            BeautyCPU->CopyFrom( renderContext, BeautyGPU );
            CopyContents( renderContext, Beauty, BeautyCPU, BufferSize );
            AuxAlbedoCPU->CopyFrom( renderContext, AuxAlbedoGPU );
            CopyContents( renderContext, AuxAlbedo, AuxAlbedoCPU, BufferSize );
            AuxNormalsCPU->CopyFrom( renderContext, AuxNormalsGPU );
            CopyContents( renderContext, AuxNormals, AuxNormalsCPU, BufferSize );
        }

        void Denoise( )
        {
            VA_TRACE_CPU_SCOPE( Denoise );

            oidnExecuteFilter( Filter );

            // Check for errors
            const char* errorMessage;
            if( oidnGetDeviceError( Device, &errorMessage) != OIDN_ERROR_NONE )
                VA_WARN("Error: %s\n", errorMessage );
        }

        void DenoiserToVanilla( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & output )
        {
            VA_TRACE_CPU_SCOPE( DenoiserToVanilla );

            CopyContents( renderContext, DenoisedCPU, Output, BufferSize );
            // copy CPU Vanilla -> GPU Vanilla
            DenoisedGPU->CopyFrom( renderContext, DenoisedCPU );
            // GPU R32G32B32A32_FLOAT - > copy GPU any-color-format
            renderContext.CopySRVToRTV( output, DenoisedGPU );
        }
    };
    #endif // VA_OIDN_INTEGRATION_ENABLED
}

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

        if( ImGuiEx_Combo( "Mode", (int&)m_mode, {"Static Accumulate", "Real-Time"} ) )
            m_accumSampleIndex = 0;

        if( m_mode == vaPathTracer::Mode::StaticAccumulate )
        {
            ImGui::Text( "Accumulated frames: %d (out of %d target)", m_accumSampleIndex, m_accumSampleTarget );

            if( ImGui::Checkbox( "Anti-aliasing", &m_enableAA ) )
                m_accumSampleIndex = 0;
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Enables subpixel jitter (not completely functional yet)" );

            if( ImGui::InputInt( "Accumulation target count", &m_staticAccumSampleTarget ) )
                m_accumSampleIndex = 0;
            m_staticAccumSampleTarget = vaMath::Clamp( m_staticAccumSampleTarget, 1, 1048576 );
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "How many samples to collect before stopping (one sample per frame, will restart on camera/system changes)" );
        }
        else if ( m_mode == vaPathTracer::Mode::RealTime )
        {
            if( ImGui::InputInt( "Accumulation target count", &m_realtimeAccumSampleTarget ) )
                m_accumSampleIndex = 0;
            m_realtimeAccumSampleTarget = vaMath::Clamp( m_realtimeAccumSampleTarget, 1, 64 );
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "How many samples to collect each frame" );

#ifdef VA_OIDN_INTEGRATION_ENABLED
            if( ImGui::Checkbox( "OIDN denoiser", &m_realtimeAccumDenoise) )
                m_accumSampleIndex = 0;
#else
            ImGui::Text( "VA_OIDN_INTEGRATION_ENABLED not set - no denoising enabled" );
            m_realtimeAccumDenoise = false;
#endif // #ifdef VA_OIDN_INTEGRATION_ENABLED

        }

        if( ImGui::InputInt( "Max bounces", &m_maxBounces ) )
            m_accumSampleIndex = 0;
        m_maxBounces = vaMath::Clamp( m_maxBounces, 0, c_maxBounceUpperBound );
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "How many times does the ray bounce before stopping" );


        //ImGui::Separator();
        if( ImGui::CollapsingHeader( "Features", 0 ) )
        {
            if( ImGui::Checkbox( "Firefly clamping", &m_enableFireflyClamp ) )
                m_accumSampleIndex = 0;

            if( ImGui::Checkbox( "Path space regularization", &m_enablePathRegularization ) )
                m_accumSampleIndex = 0;

            if( ImGui::InputFloat( "Texture MIP offset", &m_globalMIPOffset ) )
                m_accumSampleIndex = 0;
            m_globalMIPOffset    = vaMath::Clamp( m_globalMIPOffset, -10.0f, 10.0f );
        }

        if( ImGui::CollapsingHeader( "Performance", 0 ) )
        {

            if( ImGui::Checkbox( "Per-bounce sort", &m_enablePerBounceSort ) )
                m_accumSampleIndex = 0;
#if !VA_USE_RAY_SORTING
            m_enablePerBounceSort = false;
#endif
        }

        //ImGui::Separator();
        if( ImGui::CollapsingHeader( "Debugging", 0 ) )
        {
            m_debugViz = vaMath::Clamp( m_debugViz, ShaderDebugViewType::None, ShaderDebugViewType((uint)ShaderDebugViewType::MaxValue-(uint)1) );
            if( ImGuiEx_Combo( "Debug view", (int&)m_debugViz, { "None", "Bounce heatmap", "Viewspace depth", "GeometryTexcoord0",
                "GeometryNormalNonInterpolated", "GeometryNormalInterpolated", "GeometryTangentInterpolated", "GeometryBitangentInterpolated", "ShadingNormal", 
                "MaterialBaseColor", "MaterialBaseColorAlpha", "MaterialEmissive", "MaterialMetalness", "MaterialRoughness", "MaterialReflectance", "MaterialAmbientOcclusion", 
                "ReflectivityEstimate", "MaterialID", "ShaderID", "DenoiserAuxAlbedo", "DenoiserAuxNormals" } ) )
                m_accumSampleIndex = 0;

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
                m_accumSampleIndex = 0;
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
                        m_accumSampleIndex = 0;
                    }

                    if( ImGui::Button( "Move camera to test pos", { -1, 0 } ) )
                        m_divergenceMirrorCameraSetPos = true;

                    if( ImGui::SliderFloat( "Roughness (slider)", &m_divergenceMirrorRoughness, 0.0f, 1.0f ) || ImGui::InputFloat( "Roughness", &m_divergenceMirrorRoughness ) )
                    {
                        m_divergenceMirrorRoughness = vaMath::Clamp( m_divergenceMirrorRoughness, 0.0f, 1.0f );
                        m_divergenceMirrorMaterial->SetInputSlotDefaultValue( "Roughness", m_divergenceMirrorRoughness );
                        m_accumSampleIndex = 0;
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
                    m_accumSampleIndex = 0;
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

vaDrawResultFlags vaPathTracer::InnerPass( vaRenderDeviceContext & renderContext, vaDrawAttributes & drawAttributes, const shared_ptr<vaSkybox> & skybox/*, const shared_ptr<vaTexture>& outputColor, const shared_ptr<vaTexture>& outputDepth*/, uint totalPathCount, bool denoisingEnabled, vaRenderOutputs uavInputsOutputs )
{
    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

#ifndef VA_USE_RAY_SORTING
#error VA_USE_RAY_SORTING must be defined to 0 or 1
#endif


    // constants buffer
    ShaderPathTracerConstants constants;
    {
        memset( &constants, 0, sizeof(constants) );

        if( skybox != nullptr )
            skybox->UpdateConstants( drawAttributes, constants.Sky );

        constants.AccumFrameCount           = m_accumSampleIndex;
        constants.AccumFrameTargetCount     = m_accumSampleTarget;
        constants.MaxBounces                = m_maxBounces;
        if( (uint)m_debugViz >= (uint)ShaderDebugViewType::SurfacePropsBegin && (uint)m_debugViz <= (uint)ShaderDebugViewType::SurfacePropsEnd )
            constants.MaxBounces            = 0;
        constants.EnableAA                  = m_enableAA?1:0;

        const float defaultFireflyClampThreshold = 8.0f; // maybe expose through UI?
        constants.FireflyClampThreshold     = m_enableFireflyClamp?defaultFireflyClampThreshold:1e20f;
        constants.DummyParam1               = 0;
        constants.DummyParam2               = 0;

        constants.ViewportX                 = m_radianceAccumulation->GetWidth( );
        constants.ViewportY                 = m_radianceAccumulation->GetHeight( );
        constants.MaxPathCount              = m_radianceAccumulation->GetWidth( ) * m_radianceAccumulation->GetHeight( );
        constants.PerBounceSortEnabled      = (m_enablePerBounceSort)?(1):(0);
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

        constants.DenoisingEnabled          = denoisingEnabled;

        m_constantBuffer->Upload( renderContext, constants );
    }

    // used in shaders for Noise3D - gets added to object space noise and is not related to path tracing sampling noise
    vaRandom accumulateNoise( m_accumSampleIndex );
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

    // initialize control buffer! <- this is no longer used for anything
    {
        ShaderPathTracerControl control;
        control.CurrentRayCount     = constants.MaxPathCount; // <- not dynamically updating
        control.NextRayCount        = constants.MaxPathCount; // <- not dynamically updating
        m_pathTracerControl->UploadSingle( renderContext, control, 0 );
    }

    {   // initialize ("kickoff")
#if VA_USE_RAY_SORTING
        VA_TRACE_CPUGPU_SCOPE( RaygenKickoff, renderContext );
        vaRaytraceItem raytraceItemKickoff = raytraceItem;
        raytraceItemKickoff.RayGen = "RaygenKickoff";
        raytraceItemKickoff.SetDispatch( totalPathCount );
        drawResults |= renderContext.ExecuteSingleItem( raytraceItemKickoff, uavInputsOutputs, &drawAttributes );
#else
        VA_TRACE_CPUGPU_SCOPE( Kickoff, renderContext );
        computeItem.ComputeShader = m_CSKickoff;
        computeItem.SetDispatch( (constants.MaxPathCount + 31)/32 );
        drawResults |= renderContext.ExecuteSingleItem( computeItem, uavInputsOutputs, &drawAttributes );
#endif
    }

    for( int bounce = 0; bounce <= m_maxBounces; bounce++ ) // <= because first ray is primary ray - if we start from a raster pass then it will be different
    {
        VA_TRACE_CPUGPU_SCOPE_CUSTOMNAME( pass, vaStringTools::Format( "pass_%d", bounce ).c_str(), renderContext );

#if VA_USE_RAY_SORTING
        if( m_enablePerBounceSort )
        {
            const uint32 maxSortKey = std::as_const(GetRenderDevice().GetMaterialManager()).Materials().Count()+1;
            m_GPUSort->Sort( renderContext, m_pathSortKeys, m_pathListSorted, totalPathCount, maxSortKey );
        }
#endif

        {   // raygen pass
            VA_TRACE_CPUGPU_SCOPE( PathTracing, renderContext );

            raytraceItem.SetDispatch( totalPathCount );

            drawResults |= renderContext.ExecuteSingleItem( raytraceItem, uavInputsOutputs, &drawAttributes );
        }
    }
    return drawResults;
}

vaDrawResultFlags vaPathTracer::Draw( vaRenderDeviceContext & renderContext, vaDrawAttributes & _drawAttributes, const shared_ptr<vaSkybox> & skybox, const shared_ptr<vaTexture> & outputColor, const shared_ptr<vaTexture> & outputDepth )
{
    VA_TRACE_CPUGPU_SCOPE( PathTracerAll, renderContext );

    vaDrawResultFlags drawResults = vaDrawResultFlags::None;
    vaDrawAttributes localDrawAttributes = _drawAttributes;
    localDrawAttributes.Settings.MIPOffset = m_globalMIPOffset;

    assert( localDrawAttributes.Raytracing != nullptr );

    // this will cause bugs - path tracing (at the moment) does not support primary ray AO from SSAO
    assert( localDrawAttributes.Lighting->GetAOMap( ) == nullptr );

    // we should have never reached this point if raytracing isn't supported!
    assert( m_renderDevice.GetCapabilities().Raytracing.Supported );

    // create denoiser stuff
#ifdef VA_OIDN_INTEGRATION_ENABLED
    if( m_oidn == nullptr )
        m_oidn = std::make_shared<OIDNData>();
#endif // VA_OIDN_INTEGRATION_ENABLED

    // one-time init
    if( m_shaderLibrary == nullptr )
    {
        vaShaderMacroContaner macros = { { "VA_RAYTRACING", "" } };

        std::vector<shared_ptr<vaShader>> allShaders;

        allShaders.push_back( m_shaderLibrary   = vaShaderLibrary::CreateFromFile(  GetRenderDevice(), "vaPathTracer.hlsl", "", macros, false ) ); //, { "VA_RTAO_MAX_NUM_BOUNCES", vaStringTools::Format("%d", maxNumBounces) } }, true );

        allShaders.push_back( m_PSWriteToOutput = vaPixelShader::CreateFromFile(    GetRenderDevice(), "vaPathTracer.hlsl", "PSWriteToOutput", macros, false ) );

        allShaders.push_back( m_CSKickoff       = vaComputeShader::CreateFromFile(  GetRenderDevice(), "vaPathTracer.hlsl", "CSKickoff", macros, false ) );

#ifdef VA_OIDN_INTEGRATION_ENABLED
        allShaders.push_back( m_oidn->CSPrepareDenoiserInputs   = vaComputeShader::CreateFromFile( GetRenderDevice( ), "vaPathTracer.hlsl", "CSPrepareDenoiserInputs", {}, false ) );
        //allShaders.push_back( m_oidn->CSDebugDisplayDenoiser    = vaComputeShader::CreateFromFile( GetRenderDevice( ), "vaPathTracer.hlsl", "CSDebugDisplayDenoiser", {}, false ) );
#endif // VA_OIDN_INTEGRATION_ENABLED
                                                                                                                    

        // wait until shaders are compiled! this allows for parallel compilation but ensures all are compiled after this point
        for( auto sh : allShaders ) sh->WaitFinishIfBackgroundCreateActive();

        m_accumSampleIndex   = 0;        // restart accumulation (if any)
    }

    // camera or shaders changed? reset accumulation
    int64 shaderContentsID = vaShader::GetLastUniqueShaderContentsID( );
    if( m_accumLastCamera.GetViewport( ) != localDrawAttributes.Camera.GetViewport( ) || m_accumLastCamera.GetViewMatrix( ) != localDrawAttributes.Camera.GetViewMatrix( ) || m_accumLastCamera.GetProjMatrix( ) != localDrawAttributes.Camera.GetProjMatrix( ) 
        || m_accumLastShadersID != shaderContentsID )
        m_accumSampleIndex       = 0;    // restart accumulation (if any)

    const uint totalPathCount = ((outputColor->GetWidth() + (VA_PATH_TRACER_DISPATCH_TILE_SIZE-1))/VA_PATH_TRACER_DISPATCH_TILE_SIZE) * ((outputColor->GetHeight() + (VA_PATH_TRACER_DISPATCH_TILE_SIZE-1))/VA_PATH_TRACER_DISPATCH_TILE_SIZE)
                                    * VA_PATH_TRACER_DISPATCH_TILE_SIZE*VA_PATH_TRACER_DISPATCH_TILE_SIZE;

    // per-framebuffer-resize init
    if( m_radianceAccumulation == nullptr || m_radianceAccumulation->GetSize( ) != outputColor->GetSize( ) )
    {
        m_radianceAccumulation  = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32G32B32A32_FLOAT, outputColor->GetWidth(), outputColor->GetHeight(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_viewspaceDepth        = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32_FLOAT, outputColor->GetWidth(), outputColor->GetHeight(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_accumSampleIndex       = 0;    // restart accumulation (if any)

        m_pathGeometryHitInfoPayloads   = vaRenderBuffer::Create<ShaderGeometryHitPayload>( GetRenderDevice(), totalPathCount, vaRenderBufferFlags::None, "PathGeometryHitInfoPayloads" );
        m_pathPayloads                  = vaRenderBuffer::Create<ShaderPathPayload>( GetRenderDevice(), totalPathCount, vaRenderBufferFlags::None, "PathPayloads" );

        std::vector<uint32> unsortedPaths( totalPathCount, 0 ); 
        std::iota(unsortedPaths.begin(), unsortedPaths.end(), 0);   // set indices to {0, 1, ..., totalPathCount-1}
        //m_pathListUnsorted              = vaRenderBuffer::Create( GetRenderDevice(), totalPathCount, vaResourceFormat::R32_UINT, vaRenderBufferFlags::None, "PathListUnsorted",   unsortedPaths.data() );
        m_pathListSorted                = vaRenderBuffer::Create( GetRenderDevice(), totalPathCount, vaResourceFormat::R32_UINT, vaRenderBufferFlags::None, "PathListSorted",     unsortedPaths.data() );

        m_pathSortKeys                  = vaRenderBuffer::Create( GetRenderDevice(), totalPathCount, vaResourceFormat::R32_UINT, vaRenderBufferFlags::None, "PathKeys",   nullptr );
        //m_pathSortKeysSorted            = vaRenderBuffer::Create( GetRenderDevice(), totalPathCount, vaResourceFormat::R32_UINT, vaRenderBufferFlags::None, "PathKeysSorted",     nullptr );
    }

    // if accumulating from scratch, save the settings that require a restart on change (such as camera location)
    if( m_accumSampleIndex == 0 )
    {
        m_accumLastCamera       = localDrawAttributes.Camera;
        m_accumLastShadersID    = vaShader::GetLastUniqueShaderContentsID( );
    }

#ifdef VA_OIDN_INTEGRATION_ENABLED
    bool denoisingEnabled = drawResults == vaDrawResultFlags::None && m_mode == Mode::RealTime && m_realtimeAccumDenoise;
#else
    bool denoisingEnabled = false;
#endif
    
    // these are shared with all raytrace and compute passes for now for simplicity
    vaRenderOutputs uavInputsOutputs;
    {
        uavInputsOutputs.UnorderedAccessViews[0] = m_radianceAccumulation;
        uavInputsOutputs.UnorderedAccessViews[1] = m_viewspaceDepth;
        uavInputsOutputs.UnorderedAccessViews[2] = m_pathGeometryHitInfoPayloads;
        uavInputsOutputs.UnorderedAccessViews[3] = m_pathPayloads;
        //uavInputsOutputs.UnorderedAccessViews[4] = m_pathTracerControl;
        uavInputsOutputs.UnorderedAccessViews[4] = m_pathListSorted;                // this could/should be a SRV
        uavInputsOutputs.UnorderedAccessViews[5] = m_pathSortKeys;
        if( denoisingEnabled )
        {
#ifdef VA_OIDN_INTEGRATION_ENABLED
            m_oidn->UpdateTextures( GetRenderDevice(), m_radianceAccumulation->GetWidth(), m_radianceAccumulation->GetHeight() );

            uavInputsOutputs.UnorderedAccessViews[6] = m_oidn->AuxAlbedoGPU;
            uavInputsOutputs.UnorderedAccessViews[7] = m_oidn->AuxNormalsGPU;
#endif
        }
    }

    if( m_mode == Mode::StaticAccumulate )
    {
        m_accumSampleTarget = m_staticAccumSampleTarget;
        m_sampleNoiseOffset = 0;
        drawResults |= InnerPass( renderContext, localDrawAttributes, skybox, totalPathCount, denoisingEnabled, uavInputsOutputs );
    }
    else if( m_mode == Mode::RealTime )
    {
        m_accumSampleTarget = m_realtimeAccumSampleTarget;
        m_sampleNoiseOffset = m_realtimeAccumSampleTarget * (GetRenderDevice().GetCurrentFrameIndex() % 64);
        m_accumSampleIndex  = 0;

        for( m_accumSampleIndex = 0; m_accumSampleIndex < m_accumSampleTarget; m_accumSampleIndex++ )
        {
            drawResults |= InnerPass( renderContext, localDrawAttributes, skybox, totalPathCount, denoisingEnabled, uavInputsOutputs );
        }

        if( denoisingEnabled )
        {
#ifdef VA_OIDN_INTEGRATION_ENABLED
            VA_TRACE_CPUGPU_SCOPE( PrepareDenoiserInputs, renderContext );
            vaComputeItem computeItem;
            computeItem.ComputeShader = m_oidn->CSPrepareDenoiserInputs;
            computeItem.SetDispatch( (outputColor->GetWidth()+7)/8, (outputColor->GetWidth()+7)/8 );
            computeItem.GenericRootConst = m_accumSampleTarget;
            drawResults |= renderContext.ExecuteSingleItem( computeItem, uavInputsOutputs, nullptr );
#endif // VA_OIDN_INTEGRATION_ENABLED
        }

    } else { assert( false ); }
    
    // raytracing no longer needed
    vaDrawAttributes nonRTAttrs = localDrawAttributes;
    nonRTAttrs.Raytracing = nullptr;

    // apply to render target
    // (used to be a compute shader - this makes more sense when outputting depth)
    {
        vaGraphicsItem graphicsItem;
        GetRenderDevice().FillFullscreenPassGraphicsItem( graphicsItem );
        graphicsItem.ConstantBuffers[VA_PATH_TRACER_CONSTANTBUFFER_SLOT] = m_constantBuffer;
        graphicsItem.ShaderResourceViews[VA_PATH_TRACER_RADIANCE_SRV_SLOT] = m_radianceAccumulation;
        graphicsItem.ShaderResourceViews[VA_PATH_TRACER_VIEWSPACE_DEPTH_SRV_SLOT] = m_viewspaceDepth;

        if( denoisingEnabled )
        {
#ifdef VA_OIDN_INTEGRATION_ENABLED
            graphicsItem.ShaderResourceViews[VA_PATH_TRACER_DENOISE_AUX_ALBEDO_SRV_SLOT ] = m_oidn->AuxAlbedoGPU;
            graphicsItem.ShaderResourceViews[VA_PATH_TRACER_DENOISE_AUX_NORMALS_SRV_SLOT] = m_oidn->AuxNormalsGPU;
#endif // VA_OIDN_INTEGRATION_ENABLED
        }

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

    m_accumSampleIndex = std::min( m_accumSampleIndex+1, m_accumSampleTarget );

    // if there was an error during rendering (such as during shader recompiling and not available)
    if( drawResults != vaDrawResultFlags::None )
        m_accumSampleIndex = 0;      // restart accumulation (if any)

    // this has to be done again after the draw, just in case a shader got recompiled in the meantime
    shaderContentsID = vaShader::GetLastUniqueShaderContentsID( );
    if( m_accumLastShadersID != shaderContentsID )
        m_accumSampleIndex = 0;      // restart accumulation (if any)

    if( denoisingEnabled && m_debugViz == ShaderDebugViewType::None )
    {
        drawResults |= Denoise( renderContext, outputColor, outputDepth );
    }

    return drawResults;
}

vaDrawResultFlags vaPathTracer::Denoise( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & outputColor, const shared_ptr<vaTexture> & outputDepth )
{
    outputColor; outputDepth;
    VA_TRACE_CPUGPU_SCOPE( Denoise, renderContext );
    vaDrawResultFlags drawResults = vaDrawResultFlags::None; 

#ifdef VA_OIDN_INTEGRATION_ENABLED
    m_oidn->VanillaToDenoiser( renderContext, outputColor );
    m_oidn->Denoise( );
    m_oidn->DenoiserToVanilla( renderContext, outputColor );
#endif // #ifdef VA_OIDN_INTEGRATION_ENABLED

    return drawResults;
}

