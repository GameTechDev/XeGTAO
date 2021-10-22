///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaApplicationBase.h"

#include "Core/System/vaFileTools.h"

#include "Rendering/vaShader.h"

//#include "Rendering/vaAssetPack.h"

#include "vaInputMouse.h"

#include "Rendering/vaRenderDevice.h"
#include "Rendering/vaRenderDeviceContext.h"
#include "Rendering/vaDebugCanvas.h"

#include "Core/vaProfiler.h"

#include "Core/vaUI.h"

#include "IntegratedExternals/vaImguiIntegration.h"

using namespace Vanilla;

bool IsRemoteSession( void );

vaApplicationBase::vaApplicationBase( const Settings & settings, const std::shared_ptr<vaRenderDevice> & renderDevice, const vaApplicationLoopFunction & callback )
    : m_settings( settings ), m_renderDevice( renderDevice ), m_tickExCallback( callback ), vaUIPanel( "System & Performance", -10, true, vaUIPanel::DockLocation::DockedRight, "", vaVector2( 500, 550 ) )
{
    m_initialized = false;

    m_currentWindowPosition = vaVector2i( -1, -1 );
    m_currentWindowClientSize = vaVector2i( -1, -1 );
    m_lastNonFullscreenWindowClientSize = vaVector2i( -1, -1 );

    for( int i = 0; i < _countof( m_frametimeHistory ); i++ ) 
    {
        m_frametimeHistory[i] = 0.0f;
        m_frametimeHistorySync[i] = 0.0f;
        m_frametimeHistoryPresent[i] = 0.0f;
    }
    m_frametimeHistoryLast = 0;
    m_avgFramerate = 0.0f;
    m_avgFrametime = 0.0f;
    m_accumulatedDeltaFrameTime = 0.0f;

    m_shouldQuit = false;
    m_running = false;

    m_hasFocus = false;

    m_blockInput    = false;

    m_cmdLineParams = vaStringTools::SplitCmdLineParams( m_settings.CmdLine );

    m_setWindowSizeNextFrame = vaVector2i( 0, 0 );

    vaUIManager::GetInstance().RegisterMenuItemHandler( "System", m_aliveToken, std::bind( &vaApplicationBase::UIMenuHandler, this, std::placeholders::_1 ) );
}

vaApplicationBase::~vaApplicationBase( )
{
	// not really needed, handled by m_aliveToken
    // vaUIManager::GetInstance( ).UnregisterMenuItemHandler( "System" );
    assert( !m_running );
}

void vaApplicationBase::Quit( )
{
    assert( m_running );

    m_shouldQuit = true;
}

void vaApplicationBase::Initialize( )
{
    assert( !m_initialized );
    if( m_tickExCallback )
        m_tickExCallback( GetRenderDevice(), *this, std::numeric_limits<float>::lowest(), vaApplicationState::Initializing );
    m_initialized = true;
}

void vaApplicationBase::Deinitialize( )
{
    assert( m_initialized );
    m_initialized = false;
    if( m_tickExCallback )
        m_tickExCallback( GetRenderDevice(), *this, std::numeric_limits<float>::lowest(), vaApplicationState::ShuttingDown );
}

void vaApplicationBase::NamedSerializeSettings( vaXMLSerializer & serializer )
{
    if( serializer.IsReading( ) )
    {
        vaVector2i windowPos( -1, -1 );
        serializer.Serialize<int>( "WindowPositionX", windowPos.x );
        serializer.Serialize<int>( "WindowPositionY", windowPos.y );
        if( windowPos.x != -1 && windowPos.y != -1 )
        {
            SetWindowPosition( windowPos );
        }
    }
    else
    {
        vaVector2i windowPos = GetWindowPosition();
        serializer.Serialize<int>( "WindowPositionX", windowPos.x );
        serializer.Serialize<int>( "WindowPositionY", windowPos.y );
    }

    if( serializer.IsReading( ) )
    {
        assert( !IsFullscreen() ); // expecting it to not be fullscreen here
        vaVector2i windowSize;
        serializer.Serialize<int>( "WindowClientSizeX", windowSize.x );
        serializer.Serialize<int>( "WindowClientSizeY", windowSize.y );
        if( windowSize.x > 0 && windowSize.y > 0 )
            m_setWindowSizeNextFrame = windowSize;
            //SetWindowClientAreaSize( windowSize );
        if( !IsFullscreen() )
            m_lastNonFullscreenWindowClientSize = windowSize;
    }
    else
    {
        vaVector2i windowSize = GetWindowClientAreaSize();
        if( IsFullscreen() )
            windowSize = m_lastNonFullscreenWindowClientSize;
        serializer.Serialize<int>( "WindowClientSizeX", windowSize.x );
        serializer.Serialize<int>( "WindowClientSizeY", windowSize.y );
    }

    vaFullscreenState fullscreenState = m_currentFullscreenState;

    serializer.Serialize<bool>( "Vsync", m_settings.Vsync, m_settings.Vsync );

    serializer.Serialize<int>( "FullscreenState", reinterpret_cast<int&>(fullscreenState), reinterpret_cast<int const &>(m_settings.StartFullscreenState) );

    if( serializer.IsReading( ) )
    {
        if( GetFullscreenState() != fullscreenState )
            SetFullscreenState( fullscreenState );
    }

    if( serializer.SerializeOpenChildElement( "ApplicationSettings" ) )
    {
        Event_SerializeSettings.Invoke( serializer );

        bool ok = serializer.SerializePopToParentElement( "ApplicationSettings" );
        assert( ok ); ok;
    }

    if( serializer.SerializeOpenChildElement( "UISettings" ) )
    {
        vaUIManager::GetInstance().SerializeSettings( serializer );
        bool ok = serializer.SerializePopToParentElement( "UISettings" );
        assert( ok ); ok;
    }
}


void vaApplicationBase::Tick( float deltaTime )
{
    // VA_TRACE_CPU_SCOPE( vaApplicationBase_Tick );

    m_tickCounter++;

    // assuming Y-based scaling
    m_uiScaling = float(m_currentWindowClientSize.y) / 1080.0f;

    if( m_blockInput && IsMouseCaptured() )
        this->ReleaseMouse();

    if( m_hasFocus )
    {
        vaInputMouse::GetInstance( ).Tick( deltaTime );
        vaInputKeyboard::GetInstance( ).Tick( deltaTime );
    }
    else
    {
        vaInputMouse::GetInstance( ).ResetAll( );
        vaInputKeyboard::GetInstance( ).ResetAll( );
    }

    // must be a better way to do this
    if( m_blockInput )
    {
        vaInputMouse::GetInstance( ).ResetAll();
        vaInputKeyboard::GetInstance( ).ResetAll();
    }

    bool uiHasMouseFocus = false;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    uiHasMouseFocus = ImGui::GetIO().WantCaptureMouse && !IsMouseCaptured( );
#endif
    
    // use middle mouse button to switch to 'game' mode (mouse captured mode)
    // also use Ctrl+Enter for the same
    if( !m_blockInput && /*!uiHasMouseFocus &&*/ HasFocus() )
    {
        if( vaInputMouse::GetInstance( ).IsKeyClicked( MK_Middle ) || (vaInputKeyboard::GetInstance( ).IsKeyDown( KK_LCONTROL ) && vaInputKeyboard::GetInstance( ).IsKeyClicked( KK_RETURN ) ) )
        {
            if( IsMouseCaptured( ) )
                this->ReleaseMouse( );
            else
                this->CaptureMouse( );
        }
    }

    if( IsRemoteSession() && IsMouseCaptured() && vaInputMouse::GetInstance( ).IsKeyReleased( MK_Middle ) )
        this->ReleaseMouse( );
    
    // use Esc to leave 'game' mode (not sure what we'll do in the future)
    if( !m_blockInput && HasFocus() && IsMouseCaptured() && vaInputKeyboard::GetInstance( ).IsKeyClicked( KK_ESCAPE ) )
        this->ReleaseMouse( );

    // if this triggers, there's a mismatch between TickUI and DrawUI last frame
    assert( m_uiCameraUpdateTickNumber == -1 );

    {
        // VA_TRACE_CPU_SCOPE( Event_Tick );
        if( m_tickExCallback )
            m_tickExCallback( GetRenderDevice(), *this, deltaTime, vaApplicationState::Running );
        Event_Tick.Invoke( deltaTime );
    }

    // if this triggers, there's a mismatch between TickUI and DrawUI last frame
    assert( m_uiCameraUpdateTickNumber == -1 );

    vaInputMouse::GetInstance( ).ResetWheelDelta( );

    {
        VA_TRACE_CPU_SCOPE( vaFramePtrStatic_NextFrame );
        vaFramePtrStatic::NextFrame();
    }

    if( vaCore::GetAppQuitFlag() )
        Quit();
}

bool vaApplicationBase::IsMouseCaptured( ) const
{
    return vaInputMouse::GetInstance( ).IsCaptured( );
}

void vaApplicationBase::UpdateFramerateStats( float deltaTime )
{
    VA_TRACE_CPU_SCOPE( vaApplicationBase_UpdateFramerateStats );

    m_lastDeltaTime = deltaTime;
    m_frametimeHistoryLast = ( m_frametimeHistoryLast + 1 ) % _countof( m_frametimeHistory );

    // add this frame's time to the accumulated frame time
    m_accumulatedDeltaFrameTime += deltaTime;

    // remove oldest frame time from the accumulated frame time
    m_accumulatedDeltaFrameTime -= m_frametimeHistory[m_frametimeHistoryLast];

    m_frametimeHistory[m_frametimeHistoryLast] = deltaTime;
    m_frametimeHistorySync[m_frametimeHistoryLast] = (float)m_renderDevice->GetTimeSpanCPUGPUSync( );
    m_frametimeHistoryPresent[m_frametimeHistoryLast] = (float)m_renderDevice->GetTimeSpanCPUPresent( );

    m_avgFrametime = ( m_accumulatedDeltaFrameTime / (float)_countof( m_frametimeHistory ) );
    m_avgFramerate = 1.0f / m_avgFrametime;

    float avgFramerate = GetAvgFramerate( );
    float avgFrametimeMs = GetAvgFrametime( ) * 1000.0f;
    m_basicFrameInfo = vaStringTools::Format( L"%.2fms/frame avg (%.2fFPS, %dx%d)", avgFrametimeMs, avgFramerate, m_currentWindowClientSize.x, m_currentWindowClientSize.y );
#ifdef _DEBUG
    m_basicFrameInfo += L" DEBUG";
#endif
}

void vaApplicationBase::OnGotFocus( )
{
    vaInputKeyboard::GetInstance( ).ResetAll( );
    vaInputMouse::GetInstance( ).ResetAll( );

    m_hasFocus = true;
}

void vaApplicationBase::OnLostFocus( )
{
    vaInputKeyboard::GetInstance( ).ResetAll( );
    vaInputMouse::GetInstance( ).ResetAll( );
    
    m_hasFocus = false;
}

void vaApplicationBase::TickUI( const vaCameraBase & camera )
{
    // if this triggers, you might have forgotten to call DrawUI? this isn't ideal because it 
    assert( m_uiCameraUpdateTickNumber == -1 );

    assert( !m_renderDevice->IsFrameStarted( ) );

    m_uiCamera = camera;
    m_uiCameraUpdateTickNumber = m_tickCounter;

    {
        VA_TRACE_CPU_SCOPE( ImGuiNewFrame );
        m_renderDevice->ImGuiNewFrame( );
    }

    vaUIManager::GetInstance( ).TickUI( *this );
}

void vaApplicationBase::DrawUI( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const shared_ptr<vaTexture> & depthBuffer )
{
    assert( m_uiCameraUpdateTickNumber != -1 );

    assert( m_renderDevice->IsFrameStarted() );

    vaUIManager::GetInstance( ).e_BeforeDrawUI( renderContext );

    // this limitation is in because of ImGui implementation; it can be removed by adding support for drawing into any RT (of suitable dimensions)
    // into platform-specific vaRenderDevice implementations.
    vaRenderOutputs outputs = renderContext.GetRenderDevice( ).GetCurrentBackbuffer();
    outputs.DepthStencil = depthBuffer;
    assert( renderOutputs.RenderTargets[0] == outputs.RenderTargets[0] ); renderOutputs;

    {
        VA_TRACE_CPUGPU_SCOPE( DebugCanvas2D, renderContext );
        m_renderDevice->GetCanvas3D( ).Render( renderContext, outputs, m_uiCamera );
    }
    {
        VA_TRACE_CPUGPU_SCOPE( DebugCanvas2D, renderContext );
        m_renderDevice->GetCanvas2D( ).Render( renderContext, outputs );
    }

    // let's render imgui anyway to allow for using internal imgui draw functionality like vaDebugCanvas::DrawText does
    // if( vaUIManager::GetInstance().IsVisible() )    // <- we can still call the ImGuiRender because nothing gets queued in this case, but this will save us some perf setting up device and etc.
    {
        VA_TRACE_CPU_SCOPE( ImGuiRender );
        m_renderDevice->ImGuiRender( outputs, renderContext );
    }

    m_uiCameraUpdateTickNumber = -1;
}

void vaApplicationBase::UIMenuHandler( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    bool fpsLimited = m_settings.FramerateLimit != 0;
    if( ImGui::MenuItem("Enable 30FPS limiter", "", &fpsLimited ) )
    {
        m_settings.FramerateLimit = (fpsLimited)?(30):(0);
    }
    if( ImGui::MenuItem( "Dump perf tracing report", "CTRL+T" ) )
        vaTracer::DumpChromeTracingReportToFile();

#ifdef _DEBUG
    if( ImGui::MenuItem( "Show ImGui demo", "", &vaUIManager::GetInstance().m_showImGuiDemo ) )
    { }
#endif

#endif
}

void vaApplicationBase::UIPanelTickAlways( vaApplicationBase & )
{
    // All of these require CTRL+something
    if( ( vaInputKeyboardBase::GetCurrent( ) != NULL ) && vaInputKeyboardBase::GetCurrent( )->IsKeyDown( KK_CONTROL ) )
    {
        // recompile shaders if needed - not sure if this should be here...
        if( vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( ( vaKeyboardKeys )'R' ) )
            vaShader::ReloadAll( );

        if( vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( ( vaKeyboardKeys )'T' ) )
            vaTracer::DumpChromeTracingReportToFile();
    }

    // "are you sure you want to quit there are unsaved changes" popup
    {
        const char* popupName = "QuitAppConfirm";
        if( vaCore::GetAppSafeQuitFlag( ) && !ImGui::IsPopupOpen( popupName ) )
        {
            if( vaCore::AnyContentDirty( ) )
                ImGui::OpenPopup( popupName );
            else
                vaCore::SetAppQuitFlag( true );
            vaCore::SetAppSafeQuitFlag( false );
        }
        if( ImGui::BeginPopupModal( popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::Text( "\nAll those beautiful unsaved changes will be lost if you leave me now.\n\n" );

            ImGui::Separator( );

            // static bool dont_ask_me_next_time = false;
            // ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 0, 0 ) );
            // ImGui::Checkbox( "Don't ask me next time (doesn't actually work - this is just for testing the imgui dialog)", &dont_ask_me_next_time );
            // ImGui::PopStyleVar( );

            if( ImGui::Button( "Quit", ImVec2( 120, 0 ) ) )
            {
                vaCore::SetAppQuitFlag( true );
                ImGui::CloseCurrentPopup( );
            }
            ImGui::SetItemDefaultFocus( );
            ImGui::SameLine( );
            if( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup( );
            }
            ImGui::EndPopup( );
        }
    }
}

void vaApplicationBase::UIPanelTick( vaApplicationBase & application )
{
    assert( &application == this ); application;
    assert( vaUIManager::GetInstance().IsVisible() );

#ifdef VA_IMGUI_INTEGRATION_ENABLED

    // if( rightWindow )
    {
        ImVec4 infoColor = ImVec4( 1.0f, 1.0f, 0.0f, 1.0f );

        string frameInfo = vaStringTools::SimpleNarrow( GetBasicFrameInfoText() );

        // graph (there is some CPU/drawing cost to this)
        //if( (int)HelperUIFlags::ShowStatsGraph & (int)uiFlags )
        {
            float frameTimeMax = 0.0f;
            float frameTimeMin = VA_FLOAT_HIGHEST;
            float frameTimesMS[_countof( m_frametimeHistory )];
            float frameTimeAvg = 0.0f;
            float avgCPUGPUSync = 0.0f;
            float avgCPUPresent = 0.0f;
            for( int i = 0; i < _countof( m_frametimeHistory ); i++ )
            {
                frameTimesMS[i] = m_frametimeHistory[(i+m_frametimeHistoryLast+1) % _countof( m_frametimeHistory )] * 1000.0f;
                frameTimeMax = vaMath::Max( frameTimeMax, frameTimesMS[i] );
                frameTimeMin = vaMath::Min( frameTimeMin, frameTimesMS[i] );
                frameTimeAvg += frameTimesMS[i];
                avgCPUGPUSync+= m_frametimeHistorySync[(i+m_frametimeHistoryLast+1) % _countof( m_frametimeHistory )] * 1000.0f;
                avgCPUPresent+= m_frametimeHistoryPresent[(i+m_frametimeHistoryLast+1) % _countof( m_frametimeHistory )] * 1000.0f;
            }
            frameTimeAvg  /= (float)_countof( m_frametimeHistory );
            avgCPUGPUSync /= (float)_countof( m_frametimeHistory );
            avgCPUPresent /= (float)_countof( m_frametimeHistory );

            static float avgFrametimeGraphMax = 1.0f;
            avgFrametimeGraphMax = vaMath::Lerp( avgFrametimeGraphMax, frameTimeMax * 1.5f, 0.05f );
            avgFrametimeGraphMax = vaMath::Min( 1000.0f, vaMath::Max( avgFrametimeGraphMax, frameTimeMax * 1.1f ) );

            const int graphHeightInLines = 8;
            float graphWidth    = ImGui::GetContentRegionAvail().x;
            float graphHeight   = ImGui::GetTextLineHeight() * graphHeightInLines + ImGui::GetStyle().ItemSpacing.y * 2.0f;

            for( int i = 0; i < graphHeightInLines - 1; i++ )
                frameInfo += "\n";

            // frameInfo += vaStringTools::Format( "CPU: %.2fms\n", timeCPUFrame * 1000.0f );

            auto tracerView     = vaTracer::GetViewableTracerView( );
            const vaTracerView::Node * node = ( tracerView != nullptr && tracerView->GetConnectionIsGPU() )?( tracerView->FindNodeRecursive("GPUFrame") ) : ( nullptr );
            if( node != nullptr )
                frameInfo += vaStringTools::Format( "GPU: %.2fms, ", node->TimeTotalAvgPerFrame * 1000.0f );
            else
                frameInfo += vaStringTools::Format( "GPU: ----, " );

            frameInfo += vaStringTools::Format( "CPU-GPU sync: %.2fms, present: %.2fms", avgCPUGPUSync, avgCPUPresent );

            ImGui::PushStyleColor( ImGuiCol_Text, infoColor );
            ImGui::PushStyleColor( ImGuiCol_PlotLines, ImVec4( 1.0f, 1.0f, 1.0f, 1.0f ) );
            ImGui::PlotLines( "", frameTimesMS, _countof(frameTimesMS), 0, frameInfo.c_str(), 0.0f, avgFrametimeGraphMax, ImVec2( graphWidth, graphHeight ) );
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Frame (ms) min: %.2f, max: %.2f, avg: %.2f", frameTimeMin, frameTimeMax, frameTimeAvg );
            ImGui::PopStyleColor( 2 );
        }

        //if( (int)HelperUIFlags::ShowResolutionOptions & (int)uiFlags )
        {
            ImGui::Separator( );

            {
                bool fullscreen = IsFullscreen( );

                ImGui::PushItemWidth( ImGui::GetFontSize() * 8.0f );

                int ws[2] = { m_currentWindowClientSize.x, m_currentWindowClientSize.y };
                if( ImGui::InputInt2( "Resolution", ws, ImGuiInputTextFlags_EnterReturnsTrue | ( ( IsFullscreen( ) ) ? ( ImGuiInputTextFlags_ReadOnly ) : ( 0 ) ) ) )
                {
                    if( ( ws[0] != m_currentWindowClientSize.x ) || ( ws[1] != m_currentWindowClientSize.y ) )
                    {
                        m_setWindowSizeNextFrame = vaVector2i( ws[0], ws[1] );
                    }
                }
                if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Edit and press enter to change resolution. Works only in Windowed, Fullscreen modes currently can only use desktop resolution." );
                ImGui::PopItemWidth( );
                ImGui::SameLine();
                ImGuiEx_VerticalSeparator( );
                ImGui::SameLine();

                bool wasFullscreen = fullscreen;

                if( GetFullscreenState( ) == vaFullscreenState::FullscreenBorderless )
                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 1.0f, 0.0f, 1.0f ) );
                ImGui::Checkbox( "Fullscreen", &fullscreen );
                if( GetFullscreenState( ) == vaFullscreenState::FullscreenBorderless )
                    ImGui::PopStyleColor( );

                if( ImGui::IsItemHovered( ) ) 
                {
                    if( GetFullscreenState( ) == vaFullscreenState::FullscreenBorderless )
                        ImGui::SetTooltip( "Currently in Fullscreen Borderless. Click to switch to Windowed." );
                    else if( GetFullscreenState( ) == vaFullscreenState::Fullscreen )
                        ImGui::SetTooltip( "Currently in Fullscreen. Click to switch to Windowed." );
                    else if( GetFullscreenState( ) == vaFullscreenState::Windowed )
                        ImGui::SetTooltip( "Currently in Windowed. Click to switch to Fullscreen or hold Shift and click to switch to Fullscreen Borderless." );
                }


                if( wasFullscreen != fullscreen )
                {
                    if( vaInputKeyboard::GetInstance().IsKeyDown( KK_SHIFT ) )
                        SetFullscreenState( ( fullscreen ) ? ( vaFullscreenState::FullscreenBorderless ) : ( vaFullscreenState::Windowed ) );
                    else
                        SetFullscreenState( ( fullscreen ) ? ( vaFullscreenState::Fullscreen ) : ( vaFullscreenState::Windowed ) );
                }

                ImGui::SameLine();
                ImGuiEx_VerticalSeparator( );
                ImGui::SameLine();
                ImGui::Checkbox( "Vsync", &m_settings.Vsync );
                if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Enable/disable vsync. Even with vsync off there can be a sync in some API/driver/mode combinations." );
                ImGui::Separator();
                {
                    char buff1[256]; strcpy_s( buff1, _countof(buff1), m_renderDevice->GetAPIName().c_str() );
                    char buff2[256]; strcpy_s( buff2, _countof(buff2), m_renderDevice->GetAdapterNameID().c_str() );

                    string buttonInfo = vaStringTools::Format( "%s, %s (click to change)", m_renderDevice->GetAPIName().c_str(), m_renderDevice->GetAdapterNameID().c_str() );
                    static int aaSelection = -1;
                    if( ImGui::Button( buttonInfo.c_str() ) )
                    {
                        if( vaCore::AnyContentDirty() )
                            VA_LOG_ERROR( "There is some modified and unsaved content that would be lost if you changed the API/device - please save or discard the changes first." );
                        else
                            ImGui::OpenPopup( "Select API and Adapter" );
                    }
                    if( ImGui::BeginPopupModal( "Select API and Adapter", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse ) )
                    {
                        std::vector<string> arr;
                        for( int i = 0; i < m_enumeratedAPIsAdapters.size(); i++ )
                        {
                            auto & aa = m_enumeratedAPIsAdapters[i];
                            arr.push_back( aa.first + " : " + aa.second );
                            if( aaSelection == -1 && aa.first == m_renderDevice->GetAPIName() && aa.second == m_renderDevice->GetAdapterNameID() )
                                aaSelection = i;
                        }
                        ImGui::PushItemWidth( -1 );
                        ImGuiEx_ListBox( "###APIAdapter", aaSelection, arr );
                        ImGui::PopItemWidth( );

                        ImGui::InvisibleButton( "spacer", ImVec2(ImGui::GetFontSize() * 12.0f, 0.1f) );
                        ImGui::SameLine();
                        if( ImGui::Button( "Select and restart", ImVec2(ImGui::GetFontSize() * 12.0f, 0) ) )
                        {
                            SaveDefaultGraphicsAPIAdapter( m_enumeratedAPIsAdapters[aaSelection] );
                            vaCore::SetAppQuitFlag( true, true );
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        ImGui::SetItemDefaultFocus();
                        if( ImGui::Button( "Cancel", ImVec2(ImGui::GetFontSize() * 12.0f, 0) ) )
                        {
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                    else
                        aaSelection = -1;
                }


#ifdef VA_REMOTERY_INTEGRATION_ENABLED
                if( ImGui::Button( "Launch Remotery profiler" ) )
                {
                    wstring remoteryPath = vaCore::GetExecutableDirectory() + L"../../Source/IntegratedExternals/remotery/vis/index.html";
                    if( !vaFileTools::FileExists( remoteryPath ) )
                        remoteryPath = vaCore::GetExecutableDirectory() + L"remotery/vis/index.html";
                    if( vaFileTools::FileExists( remoteryPath ) )
                    {
                        std::thread([remoteryPath](){ system( vaStringTools::SimpleNarrow( remoteryPath ).c_str() ); }).detach();
                    }
                    else
                    {
                        VA_LOG_WARNING( "Cannot find Remotery html interface on '%s'", remoteryPath.c_str() );
                    }
                }
#endif
            }
        }

        {
            ImGui::Separator( );

            if( ImGui::CollapsingHeader( "Performance tracing", ImGuiTreeNodeFlags_Framed | ((/*m_helperUISettings.GPUProfilerDefaultOpen*/true)?(ImGuiTreeNodeFlags_DefaultOpen):(0)) ) )
            {
                vaTracer::TickImGui( application, m_lastDeltaTime );
            }
        }
    }

#endif

}

void vaApplicationBase::SaveDefaultGraphicsAPIAdapter( pair<string, string> apiAdapter )
{
    const string settingsFileName = GetDefaultGraphicsAPIAdapterInfoFileName( );

    vaFileStream settingsFile;
    if( settingsFile.Open( settingsFileName, FileCreationMode::Create ) )
    {
        settingsFile.WriteValue<int64>(42);
        settingsFile.WriteString( apiAdapter.first );
        settingsFile.WriteString( " - " );
        settingsFile.WriteString( apiAdapter.second );
    }
    else
    {
        VA_WARN( "Unable to open '%s'", settingsFileName.c_str() );
    }
    settingsFile.Close();

    assert( apiAdapter == LoadDefaultGraphicsAPIAdapter() );
}

pair<string, string> vaApplicationBase::LoadDefaultGraphicsAPIAdapter( )
{
    const string settingsFileName = GetDefaultGraphicsAPIAdapterInfoFileName( );

    vaFileStream settingsFile;
    if( settingsFile.Open( settingsFileName, FileCreationMode::Open ) )
    {
        int64 header; string a, b, c;

        if( settingsFile.ReadValue<int64>( header ) && header == 42 &&
            settingsFile.ReadString( a ) &&
            settingsFile.ReadString( b ) &&
            settingsFile.ReadString( c ) )
            return make_pair(a, c);
        else
            VA_LOG( "Unable to read '%s' to read default graphics adapter", settingsFileName.c_str() );
    }
    else
        VA_WARN( "Unable to open '%s'", settingsFileName.c_str() );
    return make_pair(string(""), string(""));
}

vaDebugCanvas2D& vaApplicationBase::GetCanvas2D( )
{
    return m_renderDevice->GetCanvas2D();
}

vaDebugCanvas3D& vaApplicationBase::GetCanvas3D( )
{
    return m_renderDevice->GetCanvas3D();
}

vaInputKeyboardBase * vaApplicationBase::GetInputKeyboard( ) const
{
    return vaInputKeyboard::GetInstancePtr();
}

vaInputMouseBase * vaApplicationBase::GetInputMouse( ) const
{
    return vaInputMouse::GetInstancePtr();
}
