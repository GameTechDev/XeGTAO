///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaRenderCamera.h"
#include "Rendering/vaRendering.h"

#include "IntegratedExternals/vaImguiIntegration.h"
#include "Scene/vaCameraControllers.h"

using namespace Vanilla;

vaRenderCamera::vaRenderCamera( vaRenderDevice & renderDevice, bool visibleInUI ) : 
    m_visibleInUI( visibleInUI ),
    vaUIPanel( "Camera", 0, visibleInUI, vaUIPanel::DockLocation::DockedLeftBottom ),
    vaRenderingModule( renderDevice )
{
    float initialValue[] = { 0.0f };

    m_avgLuminancePrevLastWrittenIndex = 0;
    for( int i = 0; i < c_backbufferCount; i++ )
        m_avgLuminancePrevCPU[i] = vaTexture::Create2D( renderDevice, vaResourceFormat::R32_FLOAT, 1, 1, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead | vaResourceAccessFlags::CPUReadManuallySynced, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericColor, initialValue, 4 );
    ResetHistory();
}

vaRenderCamera::~vaRenderCamera( )
{

}

float vaRenderCamera::GetEV100( bool includeExposureCompensation ) const 
{
    // see https://google.github.io/filament/Filament.html#lighting/directlighting/pre-exposedlights
    float exposureCompensation = ( includeExposureCompensation ) ? ( m_settings.ExposureSettings.ExposureCompensation ) : ( 0.0f );
    return m_settings.ExposureSettings.Exposure + exposureCompensation;
}

void vaRenderCamera::UpdateLuminance( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & inputLuminance )
{
    // this assert fires if you have never called PreRenderTick - it needs to be called before the camera is used for rendering
    assert( m_lastAverageLuminance != -1.0f );

    // this assert gets triggered if you have been calling vaPostProcessTonemap::TickAndApplyCameraPostProcess without
    // previously calling PreRenderTick!
    assert( m_avgLuminancePrevCPUHasData[m_avgLuminancePrevLastWrittenIndex] == false );

    m_avgLuminancePrevCPU[m_avgLuminancePrevLastWrittenIndex]->CopyFrom( renderContext, inputLuminance );
    m_avgLuminancePrevCPUHasData[m_avgLuminancePrevLastWrittenIndex] = true;
    
    // need to remember the undo the pre-exposure multiplier from the final number
    m_avgLuminancePrevCPUPreExposure[m_avgLuminancePrevLastWrittenIndex] = 1.0f; // <- done in the shader for now - GetPreExposureMultiplier( true );
}

void vaRenderCamera::PreRenderTick( vaRenderDeviceContext & renderContext, float deltaTime, bool alwaysUseDefaultLuminance )
{
    VA_TRACE_CPUGPU_SCOPE( vaRenderCameraPreRenderTick, renderContext );
    
    assert( m_renderDevice.IsFrameStarted() );

    // copy oldest used GPU texture to CPU (will induce sync if still being rendered to, so that's why use the one from
    // _countof( m_avgLuminancePrev )-1 frames behind.
    int oldestLuminanceIndex = ( m_avgLuminancePrevLastWrittenIndex + 1 ) % c_backbufferCount;

    // we must work on the main context due to mapping limitations
    assert( &renderContext == renderContext.GetRenderDevice().GetMainContext() );

    bool hadLuminance = false;
    if( !m_avgLuminancePrevCPUHasData[oldestLuminanceIndex] || alwaysUseDefaultLuminance )
    {
        m_lastAverageLuminance = vaMath::Clamp( m_lastAverageLuminance, m_settings.ExposureSettings.DefaultAvgLuminanceMinWhenDataNotAvailable, m_settings.ExposureSettings.DefaultAvgLuminanceMaxWhenDataNotAvailable );
    }
    else
    {
        hadLuminance = true;
        float data[1] = { 0.0f };
        if( m_avgLuminancePrevCPU[oldestLuminanceIndex]->TryMap( renderContext, vaResourceMapType::Read, false ) )
        {
            auto & mappedData = m_avgLuminancePrevCPU[oldestLuminanceIndex]->GetMappedData();
            memcpy( data, mappedData[0].Buffer, sizeof( data ) );
            m_avgLuminancePrevCPU[oldestLuminanceIndex]->Unmap( renderContext );
            float value = data[0];
            value = expf( value ); // computing geometric mean - exp( avg( log( x ) ) )
            m_lastAverageLuminance = value;// m_avgLuminancePrevCPUPreExposure[m_avgLuminancePrevLastWrittenIndex];
        }
        else
        {
            // if we had to wait, something is broken with the algorithm (or unlikely, graphics driver) - fix it
            VA_LOG_ERROR( "vaRenderCamera::PreRenderTick - unable to map texture to get last luminance data" );
            assert( false );
        }
    }

    // we've used this up, we can update it again in UpdateLuminance
    m_avgLuminancePrevCPUHasData[oldestLuminanceIndex] = false;

    // Advance it here so that even if UpdateLuminance happens multiple times per frame (it will assert), we don't
    // stall the gpu.
    m_avgLuminancePrevLastWrittenIndex = ( m_avgLuminancePrevLastWrittenIndex + 1 ) % c_backbufferCount;

    // now update all the settings
    m_settings.ExposureSettings.Exposure                    = vaMath::Clamp( m_settings.ExposureSettings.Exposure, m_settings.ExposureSettings.ExposureMin, m_settings.ExposureSettings.ExposureMax );
    m_settings.ExposureSettings.ExposureMin                 = vaMath::Clamp( m_settings.ExposureSettings.ExposureMin, -20.0f, m_settings.ExposureSettings.ExposureMax );
    m_settings.ExposureSettings.ExposureMax                 = vaMath::Clamp( m_settings.ExposureSettings.ExposureMax, m_settings.ExposureSettings.ExposureMin, 20.0f );
    m_settings.ExposureSettings.AutoExposureAdaptationSpeed = vaMath::Clamp( m_settings.ExposureSettings.AutoExposureAdaptationSpeed, 0.01f, std::numeric_limits<float>::infinity() );
    m_settings.ExposureSettings.AutoExposureKeyValue        = vaMath::Clamp( m_settings.ExposureSettings.AutoExposureKeyValue, 0.0f, 2.0f );
    m_settings.ExposureSettings.HDRClamp                    = vaMath::Clamp( m_settings.ExposureSettings.HDRClamp, 0.0f, 65504.0f );

    m_settings.TonemapSettings.Saturation                   = vaMath::Clamp( m_settings.TonemapSettings.Saturation, 0.0f, 5.0f );
    m_settings.TonemapSettings.ModifiedReinhardWhiteLevel   = vaMath::Clamp( m_settings.TonemapSettings.ModifiedReinhardWhiteLevel, 0.0f, VA_FLOAT_HIGHEST );

    m_settings.BloomSettings.BloomSize                      = vaMath::Clamp( m_settings.BloomSettings.BloomSize, 0.0f, 10.0f            ); 
    m_settings.BloomSettings.BloomMultiplier                = vaMath::Clamp( m_settings.BloomSettings.BloomMultiplier, 0.0f, 1.0f       ); 
    m_settings.BloomSettings.BloomMinThreshold              = vaMath::Clamp( m_settings.BloomSettings.BloomMinThreshold, 0.0f, 65535.0f    );
    m_settings.BloomSettings.BloomMaxClamp                  = vaMath::Clamp( m_settings.BloomSettings.BloomMaxClamp, 0.0f, 65504.0f );

    if( m_settings.ExposureSettings.UseAutoExposure && deltaTime > 0 && hadLuminance )
    {
        float exposureLerpK = vaMath::TimeIndependentLerpF( deltaTime, m_settings.ExposureSettings.AutoExposureAdaptationSpeed );
        if( m_settings.ExposureSettings.AutoExposureAdaptationSpeed == std::numeric_limits<float>::infinity() )
            exposureLerpK = 1.0f;

        m_lastAverageLuminance  = vaMath::Max( 0.00001f, m_lastAverageLuminance );

        const float quantizeScale = 1024.0f; //4096.0f; //16384.0f; //32768.0f; //65536.0f;

        if( m_settings.ExposureSettings.UseAutoAutoExposureKeyValue )
        {
            // from https://mynameismjp.wordpress.com/2010/04/30/a-closer-look-at-tone-mapping/
            m_settings.ExposureSettings.AutoExposureKeyValue = 1.03f - ( 2.0f / ( 2 + std::log10( m_lastAverageLuminance + 1 ) ) );
            
            m_settings.ExposureSettings.AutoExposureKeyValue = roundf( m_settings.ExposureSettings.AutoExposureKeyValue * quantizeScale ) / quantizeScale;  // quantize a bit to reduce unpredictability
        }

        float linearExposure        = vaMath::Max( 0.00001f, ( m_settings.ExposureSettings.AutoExposureKeyValue / m_lastAverageLuminance ) );
        float newExposure           = std::log2( linearExposure );

        newExposure = roundf( newExposure * quantizeScale ) / quantizeScale;  // quantize a bit to reduce unpredictability

        // clamp before lerping to avoid super-fast lerp
        newExposure = vaMath::Clamp( newExposure, m_settings.ExposureSettings.ExposureMin, m_settings.ExposureSettings.ExposureMax );

        // if already very close, just snap to it to avoid numerical variations
        if( vaMath::Abs( m_settings.ExposureSettings.Exposure - newExposure ) < (1.0f/quantizeScale) )
            m_settings.ExposureSettings.Exposure = newExposure;
        else
            m_settings.ExposureSettings.Exposure  = vaMath::Lerp( m_settings.ExposureSettings.Exposure, newExposure, exposureLerpK );
        
        m_settings.ExposureSettings.Exposure  = vaMath::Clamp( m_settings.ExposureSettings.Exposure, m_settings.ExposureSettings.ExposureMin, m_settings.ExposureSettings.ExposureMax );
    }
}

void vaRenderCamera::ResetHistory( )
{
    for( int i = 0; i < _countof( m_avgLuminancePrevCPU ); i++ )
    {
        m_avgLuminancePrevCPUHasData[i] = false;
        m_avgLuminancePrevCPUPreExposure[i] = 1.0f;
    }
}

void vaRenderCamera::UIPanelTick( vaApplicationBase & application )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    //ImGui::PushStyleColor( ImGuiCol_Text, titleColor );
    //ImGui::Text( "Info:" );
    //ImGui::PopStyleColor( 1 );
    ImGui::Text( vaStringTools::Format( "Camera (pos: %.2f, %.2f, %.2f, dir: %.3f, %.3f, %.3f)", m_position.x, m_position.y, m_position.z, m_direction.x, m_direction.y, m_direction.z ).c_str( ) );

    ImGui::Separator();

    if( m_YFOVMain )
    {
        float yfov = m_YFOV / ( VA_PIf ) * 180.0f;
        ImGui::InputFloat( "FOV Y", &yfov, 5.0f, 0.0f );
        m_YFOV = vaMath::Clamp( yfov, 20.0f, 140.0f ) * ( VA_PIf ) / 180.0f;
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Camera Y field of view" );
    }
    else
    {
        float xfov = m_XFOV / ( VA_PIf ) * 180.0f;
        ImGui::InputFloat( "FOV X", &xfov, 5.0f, 0.0f );
        m_XFOV = vaMath::Clamp( xfov, 20.0f, 140.0f ) * ( VA_PIf ) / 180.0f;
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Camera X field of view" );
    }

    if( m_controller == nullptr )
        ImGui::Text( "No controller attached" );
    else
    {
        ImGui::Text( "Attached controller: '%s'", m_controller->UIPropertiesItemGetDisplayName( ).c_str( ) );
        m_controller->UIPropertiesItemTick( application, false, false );
    }

    //ImGui::PushItemWidth( 120.0f );

    if( ImGui::CollapsingHeader( "Post-process", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        ImGui::Checkbox( "Post-processing enabled", &m_settings.EnablePostProcess );
        //ImGui::Separator();
        //ImGui::NewLine( );
        VA_GENERIC_RAII_SCOPE( ImGui::Indent( ImGui::GetStyle().IndentSpacing * 0.5f );, ImGui::Unindent( ImGui::GetStyle().IndentSpacing * 0.5f ); );
        ImGui::Text( "Exposure:" );
        ImGui::InputFloat( "User Exposure Compensation", &m_settings.ExposureSettings.ExposureCompensation, 0.1f );
        ImGui::Checkbox( "UseAutoExposure", &m_settings.ExposureSettings.UseAutoExposure );
        ImGui::InputFloat( "Exposure", &m_settings.ExposureSettings.Exposure, 0.1f );
        ImGui::InputFloat2( "ExposureMinMax", &m_settings.ExposureSettings.ExposureMin, "%.2f" );
        ImGui::InputFloat( "AutoExposureAdaptationSpeed", &m_settings.ExposureSettings.AutoExposureAdaptationSpeed, 0.5f );
        ImGui::Checkbox( "UseAutoAutoExposureKeyValue", &m_settings.ExposureSettings.UseAutoAutoExposureKeyValue );
        ImGui::InputFloat( "AutoExposureKeyValue", &m_settings.ExposureSettings.AutoExposureKeyValue, 0.05f );
        ImGui::InputFloat( "HDRClamp", &m_settings.ExposureSettings.HDRClamp, 0.1f );
        //        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Tooltip example" );

        ImGui::Text( "Tonemapping:" );
        ImGui::Text( " (settings currently not exposed - code in flux)" );
        //ImGui::InputFloat( "Saturation", &m_settings.TonemapSettings.Saturation, 0.1f );
        //ImGui::Checkbox( "UseModifiedReinhard", &m_settings.TonemapSettings.UseModifiedReinhard );    
        //ImGui::InputFloat( "ModifiedReinhardWhiteLevel", &m_settings.TonemapSettings.ModifiedReinhardWhiteLevel, 0.5f );

        ImGui::Text( "Bloom:" );
        ImGui::Checkbox( "UseBloom", &m_settings.BloomSettings.UseBloom );
        ImGui::InputFloat( "BloomSize", &m_settings.BloomSettings.BloomSize, 0.01f );
        ImGui::InputFloat( "BloomMultiplier", &m_settings.BloomSettings.BloomMultiplier, 0.01f );
        ImGui::InputFloat( "BloomMinThreshold", &m_settings.BloomSettings.BloomMinThreshold, 0.02f );
        ImGui::InputFloat( "BloomMaxClamp", &m_settings.BloomSettings.BloomMaxClamp, 0.1f );
    }

    if( ImGui::CollapsingHeader( "Level of detail", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        ImGui::InputFloat( "Multiplier", &m_settings.LODSettings.Multiplier );
    }

    //ImGui::PopItemWidth();
#endif
}

vaLODSettings vaRenderCamera::GetLODSettings( ) const
{
    vaLODSettings ret =vaCameraBase::GetLODSettings( );
    ret.Scale *= m_settings.LODSettings.Multiplier;
    return ret;
}
