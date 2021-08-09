///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XeGTAO is based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion", 
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
// 
// Implementation:  Filip Strugar (filip.strugar@intel.com), Steve Mccalla <stephen.mccalla@intel.com>         (\_/)
// Version:         1.00                                                                                      (='.'=)
// Details:         https://github.com/GameTechDev/XeGTAO                                                     (")_(")
//
// Version history: see XeGTAO.h
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaGTAO.h"

#include "Core/vaInput.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/vaRenderDeviceContext.h"

#include "Rendering/vaRenderDevice.h"
#include "Rendering/vaRenderGlobals.h"

#include "Rendering/vaTextureHelpers.h"


using namespace Vanilla;

vaGTAO::vaGTAO( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params ),
    vaUIPanel( "XeGTAO", 10, !VA_MINIMAL_UI_BOOL, vaUIPanel::DockLocation::DockedLeftBottom ),
    m_CSPrefilterDepths16x16( params ),
    m_CSGTAOLow( params ),
    m_CSGTAOMedium( params ),
    m_CSGTAOHigh( params ),
    m_CSGTAOUltra( params ),
    m_CSDenoise( params ),
    m_CSPreDenoise( params ),
    m_constantBuffer( params )
{
    // Hilbert look-up texture! It's a 64 x 64 uint16 texture generated using XeGTAO::HilbertIndex
    {
        uint16 * data = new uint16[64*64];
        for( int x = 0; x < 64; x++ )
            for( int y = 0; y < 64; y++ )
                {
                    uint32 r2index = XeGTAO::HilbertIndex( x, y );
                    assert( r2index < 65536 );
                    data[ x + 64*y ] = (uint16)r2index;
                }
        m_hilbertLUT = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R16_UINT, 64, 64, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericLinear,
            data, 64*2 );
        delete[] data;
    }
}

void vaGTAO::UIPanelTick( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    VA_GENERIC_RAII_SCOPE( ImGui::PushItemWidth( 120.0f );, ImGui::PopItemWidth( ); );

    if( m_enableReferenceRTAO && !m_renderDevice.GetCapabilities( ).Raytracing.Supported )
    {
        VA_LOG_ERROR( "Raytracing not supported on the current adapter!" );
        m_enableReferenceRTAO = false;
    }

    if( m_enableReferenceRTAO )
    {
        ImGui::TextWrapped( "Raytraced reference AO is enabled; this disables TAA (because it's incompatible) and intentionally does not do AA itself for the purposes of making Auto-tune deterministic." );
        ImGui::TextWrapped( "In future AA will be added by default (and automatically disabled when used by Auto-tune)." );
        ImGui::Text( "" );
        ImGui::Text( "Raytraced AO ground truth settings:" );

        m_referenceRTAOConstants.TotalRaysLength = m_settings.Radius;
        if( ImGui::InputFloat( "Rays range (Effect radius)", &m_settings.Radius, 0.05f, 0.0f, "%.2f" ) )
            m_referenceRTAOAccumFrameCount = 0;
        m_referenceRTAOConstants.TotalRaysLength = m_settings.Radius;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("This is the 'Effect radius' from GTAO settings");

        if( ImGui::InputFloat( "RefRTAO : Average albedo", &m_referenceRTAOConstants.Albedo ) )
            m_referenceRTAOAccumFrameCount = 0;
        m_referenceRTAOConstants.Albedo = vaMath::Clamp( m_referenceRTAOConstants.Albedo, 0.0f, 1.0f );

        if( ImGui::InputInt( "RefRTAO : Max bounces", &m_referenceRTAOConstants.MaxBounces ) )
            m_referenceRTAOAccumFrameCount = 0;
        m_referenceRTAOConstants.MaxBounces = vaMath::Clamp( m_referenceRTAOConstants.MaxBounces, 1, 16 );

        ImGui::Text( "RefRTAO : Accumulated frames %d out of %d", m_referenceRTAOAccumFrameCount, m_referenceRTAOAccumFrameGoal );
    }
    else
    {
        ImGui::Separator( );

        if( XeGTAO::GTAOImGuiSettings( m_settings ) )
            m_referenceRTAOAccumFrameCount = 0;

        if( m_constantsMatchDefaults )
            ImGui::TextColored( {0.5f, 1.0f, 0.5f, 1.0f }, "Heuristics settings match defaults, shader will be faster" );
        else
            ImGui::TextColored( {1, 0.5f, 0.5f, 1.0f },    "Heuristics settings don't match defaults, shader will be slower" );

        ImGui::Separator( );
        ImGui::Text( "External settings:" );

        ImGui::Checkbox("Generate normals from depth", &m_generateNormals );
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Viewspace normals can be either supplied (recommended) or generated from the depth buffer (lower quality and performance, 32 bit working depth buffer recommended).");

        ImGui::Checkbox("Use 32bit working depth buffer", &m_use32bitDepth );
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Working depth buffer can be 16 bit (faster but slightly less quality) or 32 bit (slightly higher quality, slower). 32bit buffer is recommended if generating normals from depths.");

        if( m_generateNormals && !m_use32bitDepth )
            ImGui::TextColored( {1, 0.5f, 0.5f, 1.0f }, "^^ 16bit working depth not advised when generating normals!" );
        m_use16bitMath &= !m_use32bitDepth; // FP16 math not compatible with 32bit depths.

        ImGui::Checkbox("Use 16bit shader math", &m_use16bitMath );
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Faster on some GPUs, with some (limited) quality degradation. Not compatible with 32bit depths.");
    }

    if( ImGui::CollapsingHeader( "Development and debugging", 0 ) )
    {
        ImGui::Checkbox( "Enable raytraced AO ground truth", &m_enableReferenceRTAO );
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Raytraced reference!" );

        if( !m_enableReferenceRTAO )
        {
            ImGui::Checkbox("Debug: Show GTAO debug viz", &m_debugShowGTAODebugViz);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show GTAO debug visualization");

            if( ImGui::Checkbox("Debug: Show normals", &m_debugShowNormals) )
                m_debugShowEdges &= !m_debugShowNormals;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show screen space normals");

            if( ImGui::Checkbox("Debug: Show denoising edges", &m_debugShowEdges) )
                m_debugShowNormals &= !m_debugShowEdges;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show edges not crossed by denoising blur");

#ifndef VA_GTAO_SAMPLE
            ImGui::Text( "Dump DXIL disassembly to file:" );
            if( ImGui::Button("MainPass-High" ) )
                m_CSGTAOHigh->DumpDisassembly( "XeGTAO_MainPass.txt" );
            ImGui::SameLine();
            if( ImGui::Button("Denoise" ) )
                m_CSDenoise->DumpDisassembly( "XeGTAO_Denoise.txt" );
#endif
        }
    }

#endif
}

bool vaGTAO::UpdateTexturesAndShaders( int width, int height )
{
    bool hadChanges = false;
    std::vector< pair< string, string > > newShaderMacros;

    // global shader switches - can be omitted and GTAO will default to most common use case
    if( m_generateNormals )
        newShaderMacros.push_back( std::pair<std::string, std::string>( "XE_GTAO_GENERATE_NORMALS", "" ) );
    if( m_use32bitDepth )
        newShaderMacros.push_back( std::pair<std::string, std::string>( "XE_GTAO_FP32_DEPTHS", "" ) );
    m_use16bitMath &= !m_use32bitDepth; // FP16 math not compatible with 32bit depths.
    newShaderMacros.push_back( std::pair<std::string, std::string>( "XE_GTAO_USE_HALF_FLOAT_PRECISION", (m_use16bitMath)?("1"):("0") ) );

    // debugging switches
    if( m_debugShowGTAODebugViz )
        newShaderMacros.push_back(std::pair<std::string, std::string>( "XE_GTAO_SHOW_DEBUG_VIZ", "" ) );
    if( m_debugShowNormals )
        newShaderMacros.push_back( std::pair<std::string, std::string>( "XE_GTAO_SHOW_NORMALS", "" ) );
    if( m_debugShowEdges )
        newShaderMacros.push_back( std::pair<std::string, std::string>( "XE_GTAO_SHOW_EDGES", "" ) );

    if( m_hilbertLUT != nullptr )
        newShaderMacros.push_back( std::pair<std::string, std::string>( "XE_GTAO_HILBERT_LUT_AVAILABLE", "" ) );

    m_constantsMatchDefaults = 
           (m_settings.RadiusMultiplier         == XE_GTAO_DEFAULT_RADIUS_MULTIPLIER           )
        && (m_settings.SampleDistributionPower  == XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER   )
        && (m_settings.FalloffRange             == XE_GTAO_DEFAULT_FALLOFF_RANGE               )
        && (m_settings.ThinOccluderCompensation == XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION  )
        ;

    newShaderMacros.push_back( std::pair<std::string, std::string>( "XE_GTAO_USE_DEFAULT_CONSTANTS", (m_constantsMatchDefaults)?("1"):("0") ) );

    if( newShaderMacros != m_staticShaderMacros )
    {
        m_staticShaderMacros = newShaderMacros;
        m_shadersDirty = true;
    }

    if( m_shadersDirty )
    {
        m_shadersDirty = false;

        string shaderFileToUse = "vaGTAO.hlsl";

        // to allow parallel background compilation but still ensure they're all compiled after this function
        std::vector<shared_ptr<vaShader>> allShaders;
        allShaders.push_back( m_CSPrefilterDepths16x16.get() );
        allShaders.push_back( m_CSGTAOLow.get( ) );
        allShaders.push_back( m_CSGTAOMedium.get() );
        allShaders.push_back( m_CSGTAOHigh.get() );
        allShaders.push_back( m_CSGTAOUltra.get() );
        allShaders.push_back( m_CSPreDenoise.get() );
        allShaders.push_back( m_CSDenoise.get() );

        m_CSPrefilterDepths16x16->CreateShaderFromFile( shaderFileToUse, "CSPrefilterDepths16x16", m_staticShaderMacros, false );
        m_CSGTAOLow->CreateShaderFromFile( shaderFileToUse, "CSGTAOLow", m_staticShaderMacros, false );
        m_CSGTAOMedium->CreateShaderFromFile( shaderFileToUse, "CSGTAOMedium", m_staticShaderMacros, false );
        m_CSGTAOHigh->CreateShaderFromFile( shaderFileToUse, "CSGTAOHigh", m_staticShaderMacros, false );
        m_CSGTAOUltra->CreateShaderFromFile( shaderFileToUse, "CSGTAOUltra", m_staticShaderMacros, false );
        m_CSPreDenoise->CreateShaderFromFile( shaderFileToUse, "CSPreDenoise", m_staticShaderMacros, false );
        m_CSDenoise->CreateShaderFromFile( shaderFileToUse, "CSDenoise", m_staticShaderMacros, false );

        // wait until shaders are compiled! this allows for parallel compilation
        for( auto sh : allShaders ) sh->WaitFinishIfBackgroundCreateActive();

        hadChanges = true;
    }

    bool needsUpdate = false;

    needsUpdate |= (m_size.x != width) || (m_size.y != height);

    vaResourceFormat requiredDepthFormat = (m_use32bitDepth)?(vaResourceFormat::R32_FLOAT):(vaResourceFormat::R16_FLOAT);
    needsUpdate |= m_workingDepths == nullptr || m_workingDepths->GetResourceFormat() != requiredDepthFormat;

    m_size.x    = width;
    m_size.y    = height;

    if( needsUpdate )
    {
        hadChanges = true;

        m_debugImage            = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R11G11B10_FLOAT, m_size.x, m_size.y, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_workingDepths         = vaTexture::Create2D( GetRenderDevice(), requiredDepthFormat, m_size.x, m_size.y, XE_GTAO_DEPTH_MIP_LEVELS, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        for( int mip = 0; mip < XE_GTAO_DEPTH_MIP_LEVELS; mip++ )
            m_workingDepthsMIPViews[mip] = vaTexture::CreateView( m_workingDepths, m_workingDepths->GetBindSupportFlags( ), vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, mip, 1 );
        m_workingEdges          = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R8_UNORM, m_size.x, m_size.y, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_workingVisibility     = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R16_FLOAT, m_size.x, m_size.y, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_workingVisibilityPong = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R16_FLOAT, m_size.x, m_size.y, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );

        m_debugImage->SetName( "XeGTAO_DebugImage" );
        m_workingDepths->SetName( "XeGTAO_WorkingDepths" );
        m_workingEdges->SetName( "XeGTA_WorkingEdges" );
        m_workingVisibility->SetName( "XeGTAO_WorkingVisibility" );
        m_workingVisibilityPong->SetName( "XeGTAO_WorkingVisibilityPong" );
    }

    return hadChanges;
}

void vaGTAO::UpdateConstants( vaRenderDeviceContext & renderContext, const vaMatrix4x4 & projMatrix, bool usingTAA )
{
    XeGTAO::GTAOConstants consts;

    XeGTAO::GTAOUpdateConstants( consts, m_size.x, m_size.y, m_settings, &projMatrix._11, true, (usingTAA)?(GetRenderDevice().GetCurrentFrameIndex()%256):(0) );
      
    m_constantBuffer.Upload( renderContext, consts );
}

vaDrawResultFlags vaGTAO::Compute( vaRenderDeviceContext & renderContext, const vaCameraBase & cameraBase, bool usingTAA, const shared_ptr<vaTexture> & outputAO, const vaMatrix4x4 & projMatrix, const shared_ptr<vaTexture> & inputDepth, const shared_ptr<vaTexture> & inputNormals )
{
    assert( outputAO->GetSize( ) == inputDepth->GetSize( ) );
    assert( inputDepth->GetSampleCount( ) == 1 ); // MSAA no longer supported!

    m_generateNormals |= inputNormals == nullptr;   // if normals not provided, we must generate them ourselves

    UpdateTexturesAndShaders( inputDepth->GetSizeX( ), inputDepth->GetSizeY( ) );

#ifdef VA_GTAO_SAMPLE
    VA_TRACE_CPUGPU_SCOPE_SELECT_BY_DEFAULT( XeGTAO, renderContext );
#else
    VA_TRACE_CPUGPU_SCOPE( XeGTAO, renderContext );
#endif

    if( inputNormals != nullptr )
    { 
        assert( ((inputNormals->GetSizeX() == m_size.x) || (inputNormals->GetSizeX() == m_size.x-1)) && ( (inputNormals->GetSizeY() == m_size.y) || (inputNormals->GetSizeY() == m_size.y-1)) );
    }
    assert( !m_shadersDirty ); if( m_shadersDirty ) return vaDrawResultFlags::UnspecifiedError;

    UpdateConstants( renderContext, projMatrix, usingTAA );

    vaComputeItem computeItem;
    // UAV barriers not required in current setup because UAV<->SRV barriers are automatically inserted; this however might not
    // hold in future modifications so beware :)
    computeItem.GlobalUAVBarrierBefore  = false;
    computeItem.GlobalUAVBarrierAfter   = false;

    // constants used by all passes
    computeItem.ConstantBuffers[0]      = m_constantBuffer;

    // needed only for shader debugging viz
    vaDrawAttributes drawAttributes(cameraBase); 

    {
        VA_TRACE_CPUGPU_SCOPE( PrefilterDepths, renderContext );

        computeItem.ComputeShader = m_CSPrefilterDepths16x16;

        // input SRVs
        computeItem.ShaderResourceViews[0] = inputDepth;
        computeItem.ShaderResourceViews[5] = m_hilbertLUT;

        // note: in CSPrefilterDepths16x16 each is thread group handles a 16x16 block (with [numthreads(8, 8, 1)] and each logical thread handling a 2x2 block)
        computeItem.SetDispatch( (m_size.x + 16-1) / 16, (m_size.y + 16-1) / 16, 1 );

        renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs( m_workingDepthsMIPViews[0], m_workingDepthsMIPViews[1], m_workingDepthsMIPViews[2], m_workingDepthsMIPViews[3], m_workingDepthsMIPViews[4] ), &drawAttributes );
    }
    
    {
        VA_TRACE_CPUGPU_SCOPE( MainPass, renderContext );

        shared_ptr<vaComputeShader> shaders[] = { m_CSGTAOLow, m_CSGTAOMedium, m_CSGTAOHigh, m_CSGTAOUltra };
        computeItem.ComputeShader = shaders[m_settings.QualityLevel];

        // input SRVs
        computeItem.ShaderResourceViews[0] = m_workingDepths;
        computeItem.ShaderResourceViews[1] = inputNormals;      // if nullptr, shader is compiled with XE_GTAO_GENERATE_NORMALS

        computeItem.SetDispatch( (m_size.x + XE_GTAO_NUMTHREADS_X-1) / XE_GTAO_NUMTHREADS_X, (m_size.y + XE_GTAO_NUMTHREADS_Y-1) / XE_GTAO_NUMTHREADS_Y, 1 );

        renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs( m_workingVisibility, m_workingEdges, m_debugImage ), &drawAttributes );
    }
    bool twoPassDenoise = m_settings.DenoiseLevel == 2;
    {
        VA_TRACE_CPUGPU_SCOPE( Denoise, renderContext );

        computeItem.ComputeShader = (twoPassDenoise)?(m_CSPreDenoise):(m_CSDenoise);

        // input SRVs
        computeItem.ShaderResourceViews[0] = m_workingVisibility;
        computeItem.ShaderResourceViews[1] = m_workingEdges;

        computeItem.SetDispatch( (m_size.x + XE_GTAO_DENOISE_INTERIOR_X-1) / XE_GTAO_DENOISE_INTERIOR_X, (m_size.y + XE_GTAO_DENOISE_INTERIOR_Y-1) / XE_GTAO_DENOISE_INTERIOR_Y, 1 );

        renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs( (twoPassDenoise)?(m_workingVisibilityPong):(outputAO), nullptr, m_debugImage ), &drawAttributes );
    }
    if( twoPassDenoise )
    {
        VA_TRACE_CPUGPU_SCOPE( DenoisePassTwo, renderContext );

        computeItem.ComputeShader = m_CSDenoise;

        // input SRVs
        computeItem.ShaderResourceViews[0] = m_workingVisibilityPong;
        computeItem.ShaderResourceViews[1] = m_workingEdges;

        computeItem.SetDispatch( (m_size.x + XE_GTAO_DENOISE_INTERIOR_X-1) / XE_GTAO_DENOISE_INTERIOR_X, (m_size.y + XE_GTAO_DENOISE_INTERIOR_Y-1) / XE_GTAO_DENOISE_INTERIOR_Y, 1 );

        renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs( outputAO, nullptr, m_debugImage ), &drawAttributes );
    }

    return vaDrawResultFlags::None;
}

vaDrawResultFlags vaGTAO::ComputeReferenceRTAO( vaRenderDeviceContext & renderContext, const vaCameraBase & cameraBase, vaSceneRaytracing * sceneRaytracing, const shared_ptr<vaTexture> & inputDepth )
{
    VA_TRACE_CPUGPU_SCOPE( ReferenceRTAO, renderContext );
    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    bool hadChanges = UpdateTexturesAndShaders( inputDepth->GetSizeX( ), inputDepth->GetSizeY( ) );

    // we should have never reached this point if raytracing isn't supported!
    assert( m_renderDevice.GetCapabilities().Raytracing.Supported );

    // one-time init
    if( m_referenceRTAOShaders == nullptr )
    {
        vaShaderMacroContaner macros = { { "VA_RAYTRACING", "" } };

        m_referenceRTAOShaders = GetRenderDevice().CreateModule<vaShaderLibrary>( );
        m_referenceRTAOShaders->CreateShaderFromFile( "vaGTAO_RT.hlsl", "", macros, true ); //, { "VA_RTAO_MAX_NUM_BOUNCES", vaStringTools::Format("%d", maxNumBounces) } }, true );

        m_referenceRTAOConstantsBuffer              = vaRenderBuffer::Create<XeGTAO::ReferenceRTAOConstants>( GetRenderDevice(), 1, vaRenderBufferFlags::None, "ReferenceRTAOConstantsGPU" );
        //m_referenceRTAOConstantsBufferReadback      = vaRenderBuffer::Create<XeGTAO::ReferenceRTAOConstants>( GetRenderDevice(), 1, vaRenderBufferFlags::Readback, "ReferenceRTAOConstantsReadback" );
        hadChanges = true;
    }

        // textures init (sizes changed, etc)
    if( m_referenceRTAOBuffer == nullptr || m_referenceRTAOBuffer->GetSize( ) != inputDepth->GetSize( ) )
    {
        m_referenceRTAOBuffer           = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32_FLOAT, inputDepth->GetWidth(), inputDepth->GetHeight(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_referenceRTAONormalsDepths    = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32G32B32A32_FLOAT, inputDepth->GetWidth(), inputDepth->GetHeight(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
        m_referenceRTAOBuffer->SetName( "GTAO_ReferenceRTAOBuffer" );
        m_referenceRTAONormalsDepths->SetName( "GTAO_ReferenceRTAONormalsDepths" );
        hadChanges                      = true;
    }

    // camera changed? reset accumulation
    if( m_referenceRTAOLastCamera.GetViewport( ) != cameraBase.GetViewport( ) || m_referenceRTAOLastCamera.GetViewMatrix( ) != cameraBase.GetViewMatrix( ) || m_referenceRTAOLastCamera.GetProjMatrix( ) != cameraBase.GetProjMatrix( ) )
    {
        m_referenceRTAOLastCamera       = cameraBase;
        hadChanges                      = true;
    }

    if( hadChanges )
        m_referenceRTAOAccumFrameCount  = 0;

    // this updates constants (m_constantBuffer)
    UpdateConstants( renderContext, cameraBase.GetProjMatrix(), false );

    // we need to know about the scene
    vaDrawAttributes drawAttributes( cameraBase, vaDrawAttributes::RenderFlags::None, nullptr, sceneRaytracing );

    // setup some constants!
    vaRandom accumulateNoise( m_referenceRTAOAccumFrameCount );
    drawAttributes.Settings.Noise       = vaVector2( accumulateNoise.NextFloat( ), accumulateNoise.NextFloat( ) );

    m_referenceRTAOConstants.TotalRaysLength    = m_settings.Radius;
    m_referenceRTAOConstants.AccumulatedFrames  = m_referenceRTAOAccumFrameCount;
    m_referenceRTAOConstants.AccumulateFrameMax = m_referenceRTAOAccumFrameGoal;

    m_referenceRTAOConstantsBuffer->UploadSingle( renderContext, m_referenceRTAOConstants, 0 );

    vaRenderOutputs uavInputsOutputs;
    uavInputsOutputs.UnorderedAccessViews[0] = m_referenceRTAOConstantsBuffer;
    uavInputsOutputs.UnorderedAccessViews[1] = m_referenceRTAOBuffer;
    uavInputsOutputs.UnorderedAccessViews[2] = m_debugImage;
    uavInputsOutputs.UnorderedAccessViews[3] = m_referenceRTAONormalsDepths;

    vaRaytraceItem raytraceAO;
    raytraceAO.ShaderLibrary            = m_referenceRTAOShaders;
    raytraceAO.ShaderEntryRayGen        = "AORaygen";
    raytraceAO.ShaderEntryAnyHit        = ""; // if empty, material hit test will be used
    raytraceAO.ShaderEntryClosestHit    = "AOClosestHit";
    raytraceAO.ShaderEntryMiss          = "AOMiss";
    raytraceAO.MaxRecursionDepth        = 1; // <-> switched to looped path tracing approach, no recursion needed <-> REF_RTAO_MAX_BOUNCES+2;   // +2 is because first bounce is the primary camera ray (not yet computing AO)
    raytraceAO.ConstantBuffers[0]       = m_constantBuffer;    // <- not really needed/used at the moment
    raytraceAO.SetDispatch( m_referenceRTAOBuffer->GetWidth( ), m_referenceRTAOBuffer->GetHeight( ) );

    drawResults |= renderContext.ExecuteSingleItem( raytraceAO, uavInputsOutputs, &drawAttributes );

    vaComputeItem computeItem;
    // reusing some of the ASSAO bits and bobs 
    computeItem.ConstantBuffers[0]      = m_constantBuffer;

    // raytracing no longer needed
    drawAttributes.Raytracing = nullptr;

    m_referenceRTAOAccumFrameCount = std::min( m_referenceRTAOAccumFrameCount+1, m_referenceRTAOAccumFrameGoal );

    if( drawResults != vaDrawResultFlags::None )
        m_referenceRTAOAccumFrameCount = 0;

    return drawResults;
}



