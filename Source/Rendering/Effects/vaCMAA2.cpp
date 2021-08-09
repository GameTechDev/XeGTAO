///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: Apache-2.0 OR MIT 
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaCMAA2.h"

#include "IntegratedExternals/vaImguiIntegration.h"

using namespace Vanilla;

vaCMAA2::vaCMAA2( const vaRenderingModuleParams & params ) : vaRenderingModule( params ), vaUIPanel( "CMAA2", 0, false )
{ 
    m_debugShowEdges = false;
}

vaCMAA2::~vaCMAA2( )
{
}

void vaCMAA2::UIPanelTick( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui::PushItemWidth( 120.0f );

    ImGui::Checkbox( "Extra sharp", &m_settings.ExtraSharpness );
    ImGuiEx_Combo( "Quality preset", (int&)m_settings.QualityPreset, { string("LOW"), string("MEDIUM"), string("HIGH"), string("ULTRA") } );
    ImGui::Checkbox( "Show edges", &m_debugShowEdges );

    ImGui::PopItemWidth();
#endif
}

