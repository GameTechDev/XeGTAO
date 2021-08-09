///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaZoomTool.h"
#include "Core/vaInput.h"
#include "Core/vaApplicationBase.h"

#include "IntegratedExternals/vaImguiIntegration.h"

using namespace Vanilla;

vaZoomTool::vaZoomTool( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params ), 
    m_constantsBuffer( params ),
    m_CSZoomToolFloat( params ),
    m_CSZoomToolUnorm( params ),
    vaUIPanel( "ZoomTool", -1, true, vaUIPanel::DockLocation::DockedLeftBottom )
{ 
//    assert( vaRenderingCore::IsInitialized() );

    m_CSZoomToolFloat->CreateShaderFromFile( "vaHelperTools.hlsl", "ZoomToolCS", { ( pair< string, string >( "VA_ZOOM_TOOL_SPECIFIC", "" ) ) }, false );
    m_CSZoomToolUnorm->CreateShaderFromFile( "vaHelperTools.hlsl", "ZoomToolCS", { ( pair< string, string >( "VA_ZOOM_TOOL_SPECIFIC", "" ) ), pair< string, string >( "VA_ZOOM_TOOL_USE_UNORM_FLOAT", "" ) }, false );
}

vaZoomTool::~vaZoomTool( )
{
}

void vaZoomTool::HandleMouseInputs( vaInputMouseBase & mouseInput )
{
    if( m_settings.Enabled )
    {
        if( mouseInput.IsKeyClicked( MK_Left ) )
        {
            m_settings.BoxPos = mouseInput.GetCursorClientPos() - vaVector2i( m_settings.BoxSize.x / 2, m_settings.BoxSize.y / 2 );
        }
    }
}

void vaZoomTool::Draw( vaRenderDeviceContext & renderContext, shared_ptr<vaTexture> colorInOut )
{
    if( !m_settings.Enabled )
        return;

    UpdateConstants( renderContext );

    vaComputeItem computeItem;
    vaRenderOutputs outputs;

    computeItem.ConstantBuffers[ ZOOMTOOL_CONSTANTSBUFFERSLOT ] = m_constantsBuffer;
    outputs.UnorderedAccessViews[ 0 ] = colorInOut;
    
    int threadGroupCountX = ( colorInOut->GetSizeX( ) + 16 - 1 ) / 16;
    int threadGroupCountY = ( colorInOut->GetSizeY( ) + 16 - 1 ) / 16;

    computeItem.ComputeShader = ( vaResourceFormatHelpers::IsFloat( colorInOut->GetUAVFormat() ) ) ? ( m_CSZoomToolFloat.get() ) : ( m_CSZoomToolUnorm.get() );
    computeItem.SetDispatch( threadGroupCountX, threadGroupCountY, 1 );

    renderContext.ExecuteSingleItem( computeItem, outputs, nullptr );
}

void vaZoomTool::UIPanelTickAlways( vaApplicationBase & application )
{
    application;
    if( ( vaInputKeyboardBase::GetCurrent( ) != NULL ) && vaInputKeyboardBase::GetCurrent( )->IsKeyDown( KK_CONTROL ) && vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( ( vaKeyboardKeys )'Z' ) )
    {
        m_settings.Enabled = !m_settings.Enabled;
        if( m_settings.Enabled )
            UIPanelSetFocusNextFrame( );
    }
}

void vaZoomTool::UIPanelTick( vaApplicationBase & application )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui::PushItemWidth( 120.0f );

    ImGui::Checkbox( "Enabled", &m_settings.Enabled );
    ImGui::InputInt( "ZoomFactor", &m_settings.ZoomFactor, 1 );
    m_settings.ZoomFactor = vaMath::Clamp( m_settings.ZoomFactor, 2, 32 );

    ImGui::InputInt2( "BoxPos", &m_settings.BoxPos.x );
    ImGui::InputInt2( "BoxSize", &m_settings.BoxSize.x );

    ImGui::PopItemWidth();
#endif

    // Some misc input handling
    {
        if( application.HasFocus( ) && !vaInputMouseBase::GetCurrent( )->IsCaptured( ) 
#ifdef VA_IMGUI_INTEGRATION_ENABLED
        && !ImGui::GetIO().WantTextInput && !ImGui::GetIO().WantCaptureMouse
#endif
        )
        {
            HandleMouseInputs( *vaInputMouseBase::GetCurrent() );
        }
    }
}

void vaZoomTool::UpdateConstants( vaRenderDeviceContext & renderContext )
{
    ZoomToolShaderConstants consts;

    consts.SourceRectangle  = vaVector4( (float)m_settings.BoxPos.x, (float)m_settings.BoxPos.y, (float)m_settings.BoxPos.x+m_settings.BoxSize.x, (float)m_settings.BoxPos.y+m_settings.BoxSize.y );
    consts.ZoomFactor       = m_settings.ZoomFactor;

    m_constantsBuffer.Upload( renderContext, consts );
}


