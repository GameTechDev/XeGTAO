///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaApplicationWin.h"

#include "Core/vaUI.h"
#include "IntegratedExternals\vaImguiIntegration.h"

#include "Rendering/vaRenderDevice.h"
#include "Rendering/DirectX/vaRenderDeviceDX12.h"
#include "Rendering/vaRendering.h"
#include "Rendering/vaShader.h"

//#include "Rendering/vaRenderMesh.h"
//#include "Rendering/vaRenderMaterial.h"
//#include "Rendering/vaAssetPack.h"
#include "Core/System/vaFileTools.h"

#include "Core/vaProfiler.h"

#include <timeapi.h>

#include <tpcshrd.h>

#ifdef VA_IMGUI_INTEGRATION_ENABLED
#include "IntegratedExternals/imgui/backends/imgui_impl_win32.h"
#endif

#include "vaSplashScreen.h"

using namespace Vanilla;

// utility class to limit the fps if needed
class vaFPSLimiter : public vaSingletonBase<vaFPSLimiter>
{
private:
    LARGE_INTEGER       m_startTimestamp;
    LARGE_INTEGER       m_frequency;

    double              m_lastTimestamp;
    double              m_prevError;

public:
    vaFPSLimiter()
    {
        QueryPerformanceFrequency( &m_frequency );
        QueryPerformanceCounter( &m_startTimestamp );

        m_lastTimestamp             = 0.0;
        m_prevError                 = 0.0;

        // Set so that ::Sleep below is accurate to within 1ms. This itself can adversely affects battery life on Windows 7 but should not have any impact on 
        // Windows 8 and above; for details please check following article with special attention to "Update, July 13, 2013" part: 
        // at https://randomascii.wordpress.com/2013/07/08/windows-timer-resolution-megawatts-wasted/ 
        timeBeginPeriod( 1 );

    }
    ~vaFPSLimiter()
    {
        timeEndPeriod( 1 );
    }

private:
    double              GetTime( )
    {
        LARGE_INTEGER   currentTime;
        QueryPerformanceCounter( &currentTime );
        
        return (double)(currentTime.QuadPart - m_startTimestamp.QuadPart) / (double)m_frequency.QuadPart;
    }

public:
    void                FramerateLimit( int fpsTarget )
    {
        double deltaTime = GetTime() - m_lastTimestamp;

        double targetDeltaTime = 1.0 / (double)fpsTarget;

        double diffFromTarget = targetDeltaTime - deltaTime + m_prevError;
        if( diffFromTarget > 0.0f )
        {
            double timeToWait = diffFromTarget;

            int timeToSleepMS = (int)(timeToWait * 1000);
            if( timeToSleepMS > 0 )
                Sleep( timeToSleepMS );
        }

        double prevTime = m_lastTimestamp;
        m_lastTimestamp = GetTime();
        double deltaError = targetDeltaTime - (m_lastTimestamp - prevTime);

        // dampen the spring-like effect, but still remain accurate to any positive/negative creep induced by our sleep mechanism
        m_prevError = deltaError * 0.9 + m_prevError * 0.1;

        // clamp error handling to 1 frame length
        if( m_prevError > targetDeltaTime )
            m_prevError = targetDeltaTime;
        if( m_prevError < -targetDeltaTime )
            m_prevError = -targetDeltaTime;
        
        // shift last time by error to compensate
        m_lastTimestamp += m_prevError;
    }
};

vaApplicationWin::Settings::Settings( const string & appName, const wstring & cmdLine, int cmdShow  ) : vaApplicationBase::Settings( appName, cmdLine ), CmdShow( cmdShow )
{
    Cursor      = LoadCursor( NULL, IDC_ARROW );
    Icon        = NULL;
    SmallIcon   = NULL;
}

// https://randomascii.wordpress.com/2012/07/05/when-even-crashing-doesnt-work/
static void disable_exception_swallowing( void )
{
    typedef BOOL( WINAPI *tGetPolicy )( LPDWORD lpFlags );
    typedef BOOL( WINAPI *tSetPolicy )( DWORD dwFlags );
    const DWORD EXCEPTION_SWALLOWING = 0x1;

    HMODULE kernel32 = GetModuleHandleA( "kernel32" );
    tGetPolicy pGetPolicy = (tGetPolicy)GetProcAddress( kernel32, "GetProcessUserModeExceptionPolicy" );
    tSetPolicy pSetPolicy = (tSetPolicy)GetProcAddress( kernel32, "SetProcessUserModeExceptionPolicy" );

    if( pGetPolicy && pSetPolicy )
    {
        DWORD dwFlags;
        if( pGetPolicy( &dwFlags ) )
        {
            // Turn off the filter
            pSetPolicy( dwFlags & ~EXCEPTION_SWALLOWING );
        }
    }
}

vaApplicationWin::vaApplicationWin( const Settings & settings, const std::shared_ptr<vaRenderDevice> & renderDevice, const vaApplicationLoopFunction & callback )
    : vaApplicationBase( settings, renderDevice, callback ), m_localSettings( settings )
{
    m_wndClassName = L"VanillaApp";
    m_hWnd = NULL;

    // if( m_settings.UserOutputWindow != NULL )
    // {
    //     RECT rect;
    //     GetWindowRect( m_settings.UserOutputWindow, &rect );
    // }

    m_systemMenu = 0;

    m_preventWMSIZEResizeSwapChain = false;
    m_inResizeOrMove = false;

    m_cursorHand = 0;
    m_cursorArrow = 0;
    m_cursorNone = 0;

    disable_exception_swallowing( );

    new vaFPSLimiter( );

    m_enumeratedAPIsAdapters = EnumerateGraphicsAPIsAndAdapters();
}

vaApplicationWin::~vaApplicationWin( )
{
    delete vaFPSLimiter::GetInstancePtr();

    assert( !m_running );

    vaSingletonBase<vaApplicationBase>::InvalidateInstance( );
}

void vaApplicationWin::Initialize( )
{
    vaApplicationBase::Initialize();


#ifdef VA_IMGUI_INTEGRATION_ENABLED
    // not really solved - not sure what I want to do here anyway; draw native at 4k on a laptop? not really practical from performance standpoint.
    // something to ponder on for the future.
//    ImGui_ImplWin32_EnableDpiAwareness( );
#endif

    {
        static bool classRegistred = false;
        if( !classRegistred )
        {
            WNDCLASSEX wcex;

            wcex.cbSize = sizeof( WNDCLASSEX );

            wcex.style = 0; //CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = WndProcStatic;
            wcex.cbClsExtra = 0;
            wcex.cbWndExtra = 0;
            wcex.hInstance = GetModuleHandle( NULL );
            wcex.hIcon = m_localSettings.Icon;
            wcex.hCursor = m_localSettings.Cursor;
            wcex.hbrBackground = (HBRUSH)( COLOR_WINDOW + 1 );
            wcex.lpszMenuName = NULL; //MAKEINTRESOURCE(IDC_VANILLA);
            wcex.lpszClassName = m_wndClassName.c_str( );
            wcex.hIconSm = NULL; //LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

            ATOM ret = RegisterClassEx( &wcex );
            assert( ret != 0 ); ret;
            classRegistred = true;
        }

        wstring wname = vaStringTools::SimpleWiden( m_settings.WindowTitle );
        m_hWnd = CreateWindow( m_wndClassName.c_str( ), wname.c_str(), WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, GetModuleHandle( NULL ), NULL );
        m_currentFullscreenState = vaFullscreenState::Windowed;

        RegisterTouchWindow( m_hWnd, 0 );

        vaWindows::SetMainHWND( m_hWnd );

        assert( m_hWnd != NULL );
        // SetWindowClientAreaSize( vaVector2i( m_settings.StartScreenWidth, m_settings.StartScreenHeight ) );
        m_setWindowSizeNextFrame.x = m_settings.StartScreenWidth;
        m_setWindowSizeNextFrame.y = m_settings.StartScreenHeight;

        // Load settings here so we can change window position and stuff
        {
            const wstring settingsFileName = GetSettingsFileName( );
            vaFileStream settingsFile;
            if( settingsFile.Open( settingsFileName, FileCreationMode::Open ) )
            {
                VA_LOG( L"Loading settings from '%s'...", settingsFileName.c_str( ) );
                vaXMLSerializer loadSerializer( settingsFile );
                if( loadSerializer.IsReading( ) )
                {
                    NamedSerializeSettings( loadSerializer );
                }
                else
                {
                    VA_WARN( L"Settings file '%s' is corrupt...", settingsFileName.c_str( ) );
                }
            }
            else
            {
                VA_WARN( L"Unable to load settings from '%s'...", settingsFileName.c_str( ) );
            }
        }

        if( GetFullscreenState() == vaFullscreenState::Windowed )
            SetWindowClientAreaSize( m_setWindowSizeNextFrame );
        m_setWindowSizeNextFrame = vaVector2i( 0, 0 );
        
        if( m_setFullscreenStateNextFrame != m_currentFullscreenState )
        {
            SetFullscreenWindowInternal( GetFullscreenState() != vaFullscreenState::Windowed );
        }
        ShowWindow( m_hWnd, SW_SHOWDEFAULT ); //m_settings.CmdShow );
        UpdateWindow( m_hWnd );

        // have to update this as well in case there was a fullscreen toggle
        {
            RECT wrect;
            if( ::GetClientRect( m_hWnd, &wrect ) )
            {
                int width = wrect.right - wrect.left;
                int height = wrect.bottom - wrect.top;

                if( width != m_currentWindowClientSize.x || height != m_currentWindowClientSize.y )
                {
                    m_currentWindowClientSize.x = width;
                    m_currentWindowClientSize.y = height;
                    if( !IsFullscreen() )
                        m_lastNonFullscreenWindowClientSize = m_currentWindowClientSize;
                }
            }
        }
    }

    vaLog::GetInstance( ).Add( LOG_COLORS_NEUTRAL, L"vaApplicationWin initialized (%d, %d)", m_currentWindowClientSize.x, m_currentWindowClientSize.y );

    m_renderDevice->CreateSwapChain( m_currentWindowClientSize.x, m_currentWindowClientSize.y, m_hWnd, GetFullscreenState() ); //(IsFullscreen())?(vaFullscreenState::Fullscreen):(vaFullscreenState::Windowed) );
    
    m_setFullscreenStateNextFrame = vaFullscreenState::Unknown;
    
    // can be downgraded from Fullscreen to FullscreenBorderless or Windowed for a number of reasons
    m_currentFullscreenState = m_renderDevice->GetFullscreenState();
}

void vaApplicationWin::Tick( float deltaTime )
{
    //VA_TRACE_CPU_SCOPE( vaApplicationWin_Tick );

    assert( m_initialized );

    if( m_settings.FramerateLimit > 0 )
    {
        VA_TRACE_CPU_SCOPE( FPSLIMITER );
        vaFPSLimiter::GetInstance().FramerateLimit( m_settings.FramerateLimit );
    }

    vaApplicationBase::Tick( deltaTime );

    if( vaSplashScreen::FadeOut( ) )
    {
        //SetWindowPos( m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
        SetForegroundWindow( m_hWnd );
    }
}

void vaApplicationWin::CaptureMouse( )
{
    if( IsMouseCaptured( ) )
        return;

    vaInputMouse::GetInstance( ).SetCapture( );

    ::SetCursor( m_cursorNone );

    Event_MouseCaptureChanged.Invoke( );
}

void vaApplicationWin::ReleaseMouse( )
{
    if( !IsMouseCaptured( ) )
        return;

    vaInputMouse::GetInstance( ).ReleaseCapture( );

    Event_MouseCaptureChanged.Invoke( );
}

void vaApplicationWin::UpdateMouseClientWindowRect( )
{
    RECT rc;
    GetClientRect(m_hWnd, &rc); // get client coords
    ClientToScreen(m_hWnd, reinterpret_cast<POINT*>(&rc.left)); // convert top-left
    ClientToScreen(m_hWnd, reinterpret_cast<POINT*>(&rc.right)); // convert bottom-right
    vaInputMouse::GetInstance( ).SetWindowClientRect( rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top );
}

bool vaApplicationWin::MessageLoopTick( )
{
    MSG msg;
    if( PeekMessage( &msg, 0, 0, 0, PM_REMOVE ) )
    {
        if( msg.message == WM_CLOSE )
        {
            int dbg = 0;
            dbg++;
        }

        if( msg.message == WM_QUIT /*|| msg.message == WM_CLOSE*/ || msg.message == WM_DESTROY )
            m_shouldQuit = true;

        if( ( msg.hwnd == m_hWnd ) && ( msg.message == WM_DESTROY ) )
        {
            m_hWnd = NULL;
        }

        ::TranslateMessage( &msg );

        bool overrideDefault = false;
        //if( ( m_settings.UserOutputWindow != NULL ) && ( msg.hwnd == m_settings.UserOutputWindow ) )
        PreWndProcOverride( msg.hwnd, msg.message, msg.wParam, msg.lParam, overrideDefault );

        if( !overrideDefault )
            ::DispatchMessage( &msg );
        return true;
    }
    return false;
}

void vaApplicationWin::Run( )
{
    Initialize( );

    assert( m_initialized );

    m_running = true;

    m_mainTimer.Start( );

    //
    int wmMessagesPerFrame = 0;

    Event_Started.Invoke( );

    // Main message loop:
    vaLog::GetInstance( ).Add( LOG_COLORS_NEUTRAL, L"vaApplicationWin entering main loop" );
    while( !m_shouldQuit )
    {
        VA_TRACE_CPU_SCOPE( RootLoop );

        m_mainTimer.Tick( );

        if( ( wmMessagesPerFrame < 10 ) )
        {
            VA_TRACE_CPU_SCOPE( WindowsMessageLoop );
            while( ( wmMessagesPerFrame < 10 ) && MessageLoopTick() )
                wmMessagesPerFrame++;
        }
        // else
        {
            //VA_TRACE_CPU_SCOPE( ApplicationLoop );

            bool windowOk = UpdateUserWindowChanges( );

            wmMessagesPerFrame = 0;

            if( !windowOk )
            {
                // maybe the window was closed?
                m_shouldQuit = true;
                continue;
            }

            extern bool evilg_inOtherMessageLoop_PreventTick;
            if( evilg_inOtherMessageLoop_PreventTick )
                continue;

            double totalElapsedTime = m_mainTimer.GetTimeFromStart( );
            totalElapsedTime;
            double deltaTime = m_mainTimer.GetDeltaTime( );

            UpdateFramerateStats( (float)deltaTime );
            if( m_settings.WindowTitleAppendBasicInfo )
            {
                // VA_TRACE_CPU_SCOPE( vaApplicationWin_SetWindowText );
                
                m_windowTitleInfoTimeFromLastUpdate += (float)deltaTime;
                if( m_windowTitleInfoTimeFromLastUpdate > m_windowTitleInfoUpdateFrequency    )
                {
                    m_windowTitleInfoTimeFromLastUpdate = vaMath::Clamp( m_windowTitleInfoTimeFromLastUpdate-m_windowTitleInfoUpdateFrequency, 0.0f, m_windowTitleInfoUpdateFrequency );

                    wstring newTitle = vaStringTools::SimpleWiden(m_settings.WindowTitle) + L" " + m_basicFrameInfo;
                    ::SetWindowText( m_hWnd, newTitle.c_str() );
                }
            }
            //         vaCore::Tick( (float)deltaTime );

            assert( !m_inTick ); m_inTick = true;
            Tick( (float)deltaTime );
            assert( m_inTick ); m_inTick = false;

            // fullscreen state of the device has changed due to external reasons (alt-tab, etc)? make sure we sync up
            if( m_currentFullscreenState != m_renderDevice->GetFullscreenState() )
                m_setFullscreenStateNextFrame = m_renderDevice->GetFullscreenState();

            if( m_setFullscreenStateNextFrame != vaFullscreenState::Unknown )
            {
                SetFullscreenWindowInternal( m_setFullscreenStateNextFrame != vaFullscreenState::Windowed );
                m_currentFullscreenState = m_setFullscreenStateNextFrame;
                m_setFullscreenStateNextFrame = vaFullscreenState::Unknown;
            }

            if( m_setWindowSizeNextFrame.x != 0 && m_setWindowSizeNextFrame.y != 0 )
            {
                if( !IsFullscreen() )
                    SetWindowClientAreaSize( vaVector2i( m_setWindowSizeNextFrame.x, m_setWindowSizeNextFrame.y ) );
                m_setWindowSizeNextFrame = vaVector2i( 0, 0 );
            }

            if( m_shouldQuit )
                ::DestroyWindow( m_hWnd );
        }
    }
    vaLog::GetInstance( ).Add( LOG_COLORS_NEUTRAL, L"vaApplicationWin main loop closed, exiting..." );
    Event_BeforeStopped.Invoke( );

    // Save settings
    {
        const wstring settingsFileName = GetSettingsFileName( );

        VA_LOG( L"Saving settings to '%s'...", settingsFileName.c_str() );
        vaXMLSerializer saveSerializer;
        NamedSerializeSettings( saveSerializer );

        vaFileStream settingsFile;
        if( settingsFile.Open( settingsFileName, FileCreationMode::Create ) )
        {
            saveSerializer.WriterSaveToFile( settingsFile );
        }
        else
        {
            VA_WARN( L"Unable to save settings to '%s'...", settingsFileName.c_str() );
        }
    }

    Deinitialize( );

    m_renderDevice->StartShuttingDown();

    // cleanup the message queue just in case
    int safetyBreakNum = 200;
    MSG msg;
    while( ::PeekMessage( &msg, 0, 0, 0, PM_REMOVE ) && ( safetyBreakNum > 0 ) )
    {
        ::TranslateMessage( &msg );
        ::DispatchMessage( &msg );
        safetyBreakNum--;
    }

    Event_Stopped.Invoke( );

    m_running = false;
}

void vaApplicationWin::UpdateDeviceSizeOnWindowResize( )
{
    ReleaseMouse( );
    //Event_BeforeWindowResized.Invoke( );
    if( !m_renderDevice->ResizeSwapChain( m_currentWindowClientSize.x, m_currentWindowClientSize.y, m_currentFullscreenState ) )
    {
        VA_WARN( "Swap chain resize failed!" );
    }
    // can be downgraded from Fullscreen to FullscreenBorderless or Windowed for a number of reasons
    m_currentFullscreenState = m_renderDevice->GetFullscreenState( );
}

bool vaApplicationWin::IsWindowFullscreenInternal( ) const
{
    DWORD dwStyle = GetWindowLong( m_hWnd, GWL_STYLE );
    
    return ( dwStyle & WS_OVERLAPPEDWINDOW ) == 0;
}


#ifdef VA_IMGUI_INTEGRATION_ENABLED
IMGUI_IMPL_API LRESULT  ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

void vaApplicationWin::SetFullscreenWindowInternal( bool fullscreen )
{
    DWORD dwStyle = GetWindowLong( m_hWnd, GWL_STYLE );
    if( fullscreen )
    {
        MONITORINFO mi = { sizeof( mi ) };
        if( /*GetWindowPlacement( m_hWnd, &m_windowPlacement ) &&*/
            GetMonitorInfo( MonitorFromWindow( m_hWnd, MONITOR_DEFAULTTOPRIMARY ), &mi ) )
        {
            SetWindowLong( m_hWnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW );
            SetWindowPos( m_hWnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED );
        }
        // m_lastFullscreen = true;
    }
    else
    {
        m_renderDevice->SetWindowed();

        SetWindowLong( m_hWnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW );
        //SetWindowPlacement( m_hWnd, &m_windowPlacement );
        SetWindowPos( m_hWnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED );
        m_setWindowSizeNextFrame = m_lastNonFullscreenWindowClientSize;
        // m_lastFullscreen = false;
    }
}

void vaApplicationWin::PreWndProcOverride( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, bool & overrideDefault )
{
    LRESULT ret;
    wParam; lParam; ret;
    // event_WinProcOverride( overrideDefault, ret, (void*&)hWnd, message, wParam, lParam );
    if( overrideDefault )
        return;

    PAINTSTRUCT ps;
    HDC hdc;

    switch( message )
    {
    case WM_PAINT:
        ps; hdc; hWnd;
        // hdc = BeginPaint( hWnd, &ps );
        // // TODO: Add any drawing code here...
        // EndPaint( hWnd, &ps );
        overrideDefault = true;
        break;
    }
}

#ifdef VA_IMGUI_INTEGRATION_ENABLED
static void ResetImGuiInputs( )
{
    ImGuiIO& io = ImGui::GetIO( );
    io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    for( int i = 0; i < countof(io.MouseDown); i++ )
        io.MouseDown[i] = false;
    io.MouseWheel   = 0;
    io.MouseWheelH  = 0;
    io.KeyCtrl      = false;
    io.KeyShift     = false;
    io.KeyAlt       = false;
    io.KeySuper     = false;
    for( int i = 0; i < countof( io.KeysDown ); i++ )
        io.KeysDown[i] = false;
    for( int i = 0; i < countof( io.NavInputs ); i++ )
        io.NavInputs[i] = 0;
}
#endif

LRESULT vaApplicationWin::WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    vaInputMouse::GetInstance().WndMessage( hWnd, message, wParam, lParam );

    enum {
#define MAKE_COMMAND(c) ((c)<<4)
        FIRST_COMMAND = 1337,
        CMD_ON_TOP = MAKE_COMMAND( FIRST_COMMAND ),
        CMD_TOGGLE_GRAB = MAKE_COMMAND( FIRST_COMMAND + 1 ),
        CMD_TOGGLE_VSYNC = MAKE_COMMAND( FIRST_COMMAND + 2 ),
        CMD_TOGGLE_FS = MAKE_COMMAND( FIRST_COMMAND + 3 ),
#undef MAKE_COMMAND
    };

    if( message == WM_MOUSEWHEEL )
    {
        vaInputMouse::GetInstance().AccumulateWheelDelta( (float)(GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA) );
    }

#ifdef VA_IMGUI_INTEGRATION_ENABLED
    if( vaUIManager::GetInstance().IsVisible() )
    {
        if( !IsMouseCaptured() )
        {
            LRESULT res = ImGui_ImplWin32_WndProcHandler( hWnd, message, wParam, lParam );
            if( res )
                return res;
        }
        else
            ResetImGuiInputs();
    }
#endif

    int wmId, wmEvent;

    switch( message )
    {
    case WM_SETCURSOR:
        // this currently never happens since ::SetCapture disables WM_SETCURSOR but leave it in for future possibility
        if( IsMouseCaptured() )
        {
            SetCursor( m_cursorNone );
            //SetWindowLongPtr( hWnd, DWLP_MSGRESULT, TRUE );
            return TRUE;
        }
        else
            return DefWindowProc( hWnd, message, wParam, lParam );

    case WM_CREATE:
        m_cursorHand = LoadCursor( NULL, IDC_HAND );
        m_cursorArrow = LoadCursor( NULL, IDC_ARROW );
        m_systemMenu = GetSystemMenu( hWnd, FALSE );

        {
            int curWidth = 0;
            int curHeight = 0;
            curWidth    = GetSystemMetrics( SM_CXCURSOR );
            curHeight   = GetSystemMetrics( SM_CYCURSOR );

            BYTE * andMask  = new BYTE[curWidth * curHeight];
            BYTE * xorMask   = new BYTE[curWidth * curHeight];
            memset( andMask, 0xFF, curWidth * curHeight );
            memset( xorMask, 0x00, curWidth * curHeight );
            m_cursorNone = ::CreateCursor( GetModuleHandle( NULL ), curWidth/2, curHeight/2, curWidth, curHeight, andMask, xorMask );
            delete[] andMask;
            delete[] xorMask;
        }

        // InsertMenuA( m_systemMenu, 0, 0, CMD_ON_TOP, "On Top\tCtrl+A" );
        return 0;
    case WM_INITMENUPOPUP:
        if( (HMENU)wParam == m_systemMenu )
        {
            return 0;
        }
        break;
    case WM_COMMAND:
        wmId = LOWORD( wParam );
        wmEvent = HIWORD( wParam );
        //// Parse the menu selections:
        //switch (wmId)
        //{
        ////case IDM_ABOUT:
        ////   DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
        ////   break;
        //case IDM_EXIT:
        //   DestroyWindow(hWnd);
        //   break;
        //default:
        //   return DefWindowProc(hWnd, message, wParam, lParam);
        //}
        //break;
    case WM_MOVE:
        {
            RECT rc;
            GetWindowRect(m_hWnd, &rc); // get client coords
            m_currentWindowPosition = vaVector2i( rc.left, rc.top );
        }
        break;
    case WM_DESTROY:
        ::DestroyCursor( m_cursorNone );
    case WM_CLOSE:
        vaCore::SetAppSafeQuitFlag(true);
        return 0;
        //if( !m_shouldQuit )
        {
            //m_shouldQuit = true;
            // return DefWindowProc( hWnd, message, wParam, lParam );
        }
        break;
    case WM_ENTERSIZEMOVE:
        m_inResizeOrMove = true;
        break;
    case WM_EXITSIZEMOVE:
        // if( m_inResizeOrMove )
        //     UpdateDeviceSizeOnWindowResize( );
        m_inResizeOrMove = false;
        break;
    case WM_ACTIVATE:
    {
        if( wParam == 0 )
            OnLostFocus( );
        else
            OnGotFocus( );
        return 0;
    }
    case WM_KILLFOCUS:
        OnLostFocus( );
        break;
    case WM_SETFOCUS:
        // if( m_inResizeOrMove )
        //     UpdateDeviceSizeOnWindowResize( );
        m_inResizeOrMove = false;
        OnGotFocus( );
        break;
    case WM_KEYDOWN:
        if( wParam == 27 ) // ESC
        {
            //m_shouldQuit = true;
            //::DestroyWindow( m_hWnd );
        }
        break;
    case WM_KEYUP:
        break;
    case WM_SIZE:
    {
        // if( m_hWnd != NULL )
        // {
        //     RECT rect;
        //     ::GetClientRect( m_hWnd, &rect );
        // 
        //     int newWidth    =  rect.right - rect.left;
        //     int newHeight   =  rect.bottom - rect.top;
        // 
        //     if( ( newWidth != 0 ) && ( newHeight != 0 ) )
        //     {
        //         m_currentWindowClientSize.x = newWidth;
        //         m_currentWindowClientSize.y = newHeight;
        // 
        //         if( !m_preventWMSIZEResizeSwapChain && !m_inResizeOrMove )
        //             UpdateDeviceSizeOnWindowResize( );
        //     }
        // }
        // else
        // {
        //     int dbg = 0;
        //     dbg++;
        // }
    }
    break;
    case WM_SYSKEYDOWN:
    {        
        if( wParam == VK_RETURN ) //&& (m_localSettings.UserOutputWindow == NULL) )
        {
            if( m_currentFullscreenState == vaFullscreenState::Windowed )
                m_setFullscreenStateNextFrame = vaFullscreenState::Fullscreen;
            else
                m_setFullscreenStateNextFrame = vaFullscreenState::Windowed;
            //ToggleFullscreen( );
        }
        // if( wParam == VK_TAB )
        // {
        //    // move out of fullscreen
        //    // (doesn't work - doesn't ever get called)
        //    int dbg = 0;
        //    dbg++;
        // }
        if( wParam == VK_F4 )
            vaCore::SetAppSafeQuitFlag( true );
    }
    break;
    case WM_SYSCOMMAND:
    {
        // TODO: make the commands user-configurable?
        int command = wParam & 0xfff0;
        switch( command )
        {
        case CMD_ON_TOP:
            //toggle_always_on_top_GUI( );
            return 0;
        case SC_MAXIMIZE:
            //if( !current_mode.allow_resize )
            //{
            //    toggle_windowed_mode_GUI( );
            //    return 0;
            //}
            break;
//        case SC_CLOSE:
//            m_shouldQuit = true;
//            ::PostQuitMessage( 0 );
//            return 0;
//        default:
//            return 0;
        }

        // let windows do its thing
        // window_in_modal_loop = true;
        // LRESULT result = DefWindowProc( hWnd, WM_SYSCOMMAND, wParam, lParam );
        // window_in_modal_loop = false;
        // 
        // cursor_clip_pending = current_state.prefs.clip_cursor;
        return DefWindowProc( hWnd, message, wParam, lParam );
    } break;

//#define VA_DISABLE_WINDOWED_SIZE_LIMIT
#ifdef VA_DISABLE_WINDOWED_SIZE_LIMIT
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO * lpMinMaxInfo = (MINMAXINFO *)lParam;
        DefWindowProc( hWnd, message, wParam, lParam );
        lpMinMaxInfo->ptMaxSize.x = 8192;
        lpMinMaxInfo->ptMaxSize.y = 8192;
        lpMinMaxInfo->ptMaxTrackSize.x = 8192;
        lpMinMaxInfo->ptMaxTrackSize.y = 8192;
        return 0;
    } break;
#endif // #ifdef VA_DISABLE_WINDOWED_SIZE_LIMIT

    default:
        return DefWindowProc( hWnd, message, wParam, lParam );
    }
    return 0;
}

vaVector2i vaApplicationWin::GetWindowPosition( ) const 
{
    RECT wrect;
    if( ::GetWindowRect( m_hWnd, &wrect ) )
        return vaVector2i( wrect.left, wrect.top );
    else
        return m_currentWindowPosition;
}

void vaApplicationWin::SetWindowPosition( const vaVector2i & position )
{
    ::SetWindowPos( m_hWnd, 0, position.x, position.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER );
}

vaVector2i vaApplicationWin::GetWindowClientAreaSize( ) const
{
    return m_currentWindowClientSize;
}

void vaApplicationWin::SetWindowClientAreaSize( const vaVector2i & clientSize )
{
    // VA_ASSERT( m_localSettings.UserOutputWindow == NULL, L"Using user window, this isn't going to work" );

    if( clientSize.x == m_currentWindowClientSize.x && clientSize.y == m_currentWindowClientSize.y )
        return;

    m_currentWindowClientSize.x = clientSize.x;
    m_currentWindowClientSize.y = clientSize.y;
    if( !IsFullscreen() )
        m_lastNonFullscreenWindowClientSize = m_currentWindowClientSize;

    if( m_hWnd == NULL )
        return;

    LONG style = ::GetWindowLong( m_hWnd, GWL_STYLE );

    RECT rect;
    rect.left = 0; rect.top = 0;
    rect.right = clientSize.x;
    rect.bottom = clientSize.y;
    ::AdjustWindowRect( &rect, (DWORD)style, false );

    RECT wrect;
    ::GetWindowRect( m_hWnd, &wrect );
    ::MoveWindow( m_hWnd, wrect.left, wrect.top, ( rect.right - rect.left ), ( rect.bottom - rect.top ), TRUE );
}

LRESULT CALLBACK vaApplicationWin::WndProcStatic( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    if( vaApplicationWin::GetInstanceValid()  )
        return dynamic_cast<vaApplicationWin&>(vaApplicationBase::GetInstance( ) ).WndProc( hWnd, message, wParam, lParam );
    else
        return DefWindowProc( hWnd, message, wParam, lParam );
}

bool vaApplicationWin::UpdateUserWindowChanges( )
{
    VA_TRACE_CPU_SCOPE( vaApplicationWin_UpdateUserWindowChanges );

    // if( m_localSettings.UserOutputWindow == NULL ) return true;
    if( m_hWnd == NULL )
        return false;
    //assert( m_localSettings.UserOutputWindow == m_hWnd );

    RECT wrect;
    if( !::GetClientRect( m_hWnd, &wrect ) )
    {
        return false;
    }
    int width = wrect.right - wrect.left;
    int height = wrect.bottom - wrect.top;
    UpdateMouseClientWindowRect();

    if( width != m_currentWindowClientSize.x || height != m_currentWindowClientSize.y )
    {
        m_currentWindowClientSize.x = width;
        m_currentWindowClientSize.y = height;
        if( !IsFullscreen() )
            m_lastNonFullscreenWindowClientSize = m_currentWindowClientSize;
    }
    if( m_renderDevice->GetSwapChainTextureSize( ) != m_currentWindowClientSize || m_renderDevice->GetFullscreenState() != m_currentFullscreenState )
    {
        UpdateDeviceSizeOnWindowResize( );
    }
    return true;
}

std::vector<pair<string, string>> vaApplicationWin::EnumerateGraphicsAPIsAndAdapters( )
{
    std::vector<pair<string, string>> ret;
    ret.push_back( std::make_pair("default", "default") );
    //vaRenderDeviceDX11::StaticEnumerateAdapters( ret );
    vaRenderDeviceDX12::StaticEnumerateAdapters( ret );
    return ret;
}

void vaApplicationWin::Run( const vaApplicationWin::Settings & settings, const vaApplicationLoopFunction & callback, string defaultAPI )
{
    if( defaultAPI == "" )
        defaultAPI = vaRenderDeviceDX12::StaticGetAPIName();
    do
    {
        {
            auto defaultAPIAdapter = LoadDefaultGraphicsAPIAdapter();
            if( defaultAPIAdapter.first == "default" || defaultAPIAdapter.first == "" )
                defaultAPIAdapter.first = defaultAPI;

            //////////////////////////////////////////////////////////////////////////
            // DirectX specific
            std::shared_ptr<vaRenderDevice> renderDevice;
            //if( defaultAPIAdapter.first == vaRenderDeviceDX11::StaticGetAPIName() )
            //    renderDevice = std::make_shared<vaRenderDeviceDX11>( defaultAPIAdapter.second );
            //else 
                if( defaultAPIAdapter.first == vaRenderDeviceDX12::StaticGetAPIName() )
                renderDevice = std::make_shared<vaRenderDeviceDX12>( defaultAPIAdapter.second );
            else
            {
                assert( false );
                return;
            }
            //////////////////////////////////////////////////////////////////////////

            if( renderDevice->IsValid() )    // device can get created but in a broken state, in which case we should just exit
            {
                shared_ptr<vaApplicationWin> application = std::shared_ptr<vaApplicationWin>( new vaApplicationWin( settings, renderDevice, callback ) );
                application->Run();
            }
        }

        if( !vaCore::GetAppQuitButRestartingFlag() )
            return;
        else
        {
            vaCore::Deinitialize( true );
            vaCore::Initialize( true );
            vaCore::SetAppQuitFlag( false );
            vaCore::SetAppSafeQuitFlag( false );
        }

    } while( true );


}
