///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaInputMouse.h"
#include "vaApplicationWin.h"

using namespace Vanilla;

//static HCURSOR s_defaultCursorHandle = NULL;
//static HCURSOR s_prevCursorHandle    = NULL;

#define ENABLE_SET_CURSOR_TO_CENTRE_WHEN_CAPTURED

vaInputMouse::vaInputMouse( )
{
    m_captured = false;

    ResetAll( );

    vaInputMouseBase::SetCurrent( this );
}

vaInputMouse::~vaInputMouse( )
{
    assert( vaInputMouseBase::GetCurrent( ) == this );
    vaInputMouseBase::SetCurrent( NULL );
}

// vaSystemManagerSingletonBase<vaInputMouse>
void vaInputMouse::Tick( float deltaTime )
{
    VA_TRACE_CPU_SCOPE( vaInputMouse_Tick );

    // m_rawDeltaPos = {0,0};

    for( int i = 0; i < MK_MaxValue; i++ )
    {
        //bool isDown = ( GetAsyncKeyState( i ) & 0x8000 ) != 0;
        bool isDown = m_platformInputKeys[i];

        g_KeyUps[i] = g_Keys[i] && !isDown;
        g_KeyDowns[i] = !g_Keys[i] && isDown;

        g_Keys[i] = isDown;
    }

    CURSORINFO cinfo; cinfo.cbSize = sizeof( cinfo );
    BOOL ret = ::GetCursorInfo( &cinfo );
    if( !ret )
    {
        ResetAll( );
        return;
    }
    assert( ret ); ret;
    m_prevPos = m_currPos;
    m_currPos = vaVector2i( cinfo.ptScreenPos.x, cinfo.ptScreenPos.y );
    m_deltaPos = m_currPos - m_prevPos;

    m_timeFromLastMove += deltaTime;
    if( m_prevPos != m_currPos )
        m_timeFromLastMove = 0.0f;

    if( m_captured )
    {
        // return to centre
#ifdef ENABLE_SET_CURSOR_TO_CENTRE_WHEN_CAPTURED
        if( ::SetCursorPos( m_capturedWinCenterPos.x, m_capturedWinCenterPos.y ) )
            m_prevPos = m_currPos = m_capturedWinCenterPos;
#endif
    }

    if( m_firstPass )
    {
        m_firstPass = false;
        Tick( deltaTime );
    }
}

void vaInputMouse::ResetAll( )
{
    if( m_captured )
        ReleaseCapture();

    m_firstPass = true;

    m_prevPos = vaVector2i( 0, 0 );
    m_currPos = vaVector2i( 0, 0 );
    m_deltaPos = vaVector2i( 0, 0 );

    m_captured = false;
    m_capturedPos = vaVector2i( 0, 0 );
    m_capturedWinCenterPos = vaVector2i( 0, 0 );

    m_wheelDelta = 0;

//    m_platformInputPos = vaVector2i( 0, 0 );

    for( int i = 0; i < MK_MaxValue; i++ )
    {
        m_platformInputKeys[i] = false;
        g_Keys[i] = false;
        g_KeyUps[i] = false;
        g_KeyDowns[i] = false;
    }
    ReleaseCapture( );

    m_timeFromLastMove = 0.0f;
}

void vaInputMouse::SetCapture( )
{
    if( m_captured )
        return;

    m_captured = true;

    POINT oldPos;
    ::GetCursorPos( &oldPos );
    m_capturedPos = vaVector2i( oldPos.x, oldPos.y );

    RECT dwr;
    ::GetWindowRect( dynamic_cast<vaApplicationWin&>(vaApplicationBase::GetInstance( )).GetMainHWND( ), &dwr );
    vaVector2i winCentre;
    m_capturedWinCenterPos.x = ( dwr.left + dwr.right ) / 2;
    m_capturedWinCenterPos.y = ( dwr.top + dwr.bottom ) / 2;

    ::SetCapture( dynamic_cast<vaApplicationWin&>(vaApplicationBase::GetInstance( )).GetMainHWND( ) );
#ifdef ENABLE_SET_CURSOR_TO_CENTRE_WHEN_CAPTURED
    if( ::SetCursorPos( m_capturedWinCenterPos.x, m_capturedWinCenterPos.y ) )
        m_prevPos = m_currPos = m_capturedWinCenterPos;
#endif
}

void vaInputMouse::ReleaseCapture( )
{
    if( !m_captured )
        return;

    m_captured = false;

#ifdef ENABLE_SET_CURSOR_TO_CENTRE_WHEN_CAPTURED
    if( ::SetCursorPos( m_capturedPos.x, m_capturedPos.y ) )
        m_prevPos = m_currPos = m_capturedPos;
#endif
    ::ReleaseCapture( );
}

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp)                        ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp)                        ((int)(short)HIWORD(lp))
#endif

void vaInputMouse::WndMessage( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    hWnd; wParam; lParam;
    // this didn't really work out
    // static bool initialized = false;
    // if( !initialized )
    // {
    //     initialized = true;
    // 
    //     // from https://docs.microsoft.com/en-us/windows/win32/dxtecharts/taking-advantage-of-high-dpi-mouse-movement#wm_input
    //     // you can #include <hidusage.h> for these defines
    //     #ifndef HID_USAGE_PAGE_GENERIC
    //     #define HID_USAGE_PAGE_GENERIC         ((USHORT) 0x01)
    //     #endif
    //     #ifndef HID_USAGE_GENERIC_MOUSE
    //     #define HID_USAGE_GENERIC_MOUSE        ((USHORT) 0x02)
    //     #endif
    // 
    //     RAWINPUTDEVICE Rid[1];
    //     Rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC; 
    //     Rid[0].usUsage = HID_USAGE_GENERIC_MOUSE; 
    //     Rid[0].dwFlags = RIDEV_INPUTSINK;   
    //     Rid[0].hwndTarget = hWnd;
    //     RegisterRawInputDevices(Rid, 1, sizeof(Rid[0]));
    // }

    // coming from touch, ignore
    if( ( GetMessageExtraInfo( ) & 0x82 ) == 0x82 )
        return;

    if( (message >= WM_MOUSEFIRST) && (message <= WM_MOUSELAST) )
    {
        // POINT pt;
        // pt.x = GET_X_LPARAM( lParam );
        // pt.y = GET_Y_LPARAM( lParam );
        // 
        // if( hWnd != NULL )
        //     ::ClientToScreen( hWnd, &pt );
        // 
        // m_platformInputPos = vaVector2i( pt.x, pt.y );

        m_platformInputKeys[MK_Left]      = ( wParam & MK_LBUTTON ) != 0;
        m_platformInputKeys[MK_Right]     = ( wParam & MK_RBUTTON ) != 0;
        m_platformInputKeys[MK_Middle]    = ( wParam & MK_MBUTTON ) != 0;
        m_platformInputKeys[MK_XButton1]  = ( wParam & MK_XBUTTON1 ) != 0;
        m_platformInputKeys[MK_XButton2]  = ( wParam & MK_XBUTTON2 ) != 0;
    }

    // this didn't really work out
    // if( message == WM_INPUT )
    // {
    //     UINT dwSize = sizeof(RAWINPUT);
    //     static BYTE lpb[sizeof(RAWINPUT)];
    //     GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER));
    // 
    //     RAWINPUT* raw = (RAWINPUT*)lpb;
    //     if (raw->header.dwType == RIM_TYPEMOUSE) 
    //     {
    //         m_rawDeltaPos.x += raw->data.mouse.lLastX;
    //         m_rawDeltaPos.y += raw->data.mouse.lLastY;
    //     } 
    // }
}

vaVector2i vaInputMouse::GetCursorPosDirect( ) const
{
    // directly query for latest mouse position to reduce lag
    POINT pt;
    ::GetCursorPos( &pt ); 
    return vaVector2i( pt.x, pt.y );
}

vaVector2i vaInputMouse::GetCursorClientPosDirect( ) const
{
    // directly query for latest mouse position to reduce lag
    POINT pt;
    ::GetCursorPos( &pt ); 
    ::ScreenToClient( dynamic_cast<vaApplicationWin&>(vaApplicationBase::GetInstance( )).GetMainHWND(), &pt );
    return vaVector2i( pt.x, pt.y );
}

vaVector2  vaInputMouse::GetCursorClientNormalizedPosDirect( ) const
{
    POINT pt;
    ::GetCursorPos( &pt ); 
    ::ScreenToClient( dynamic_cast<vaApplicationWin&>(vaApplicationBase::GetInstance( )).GetMainHWND(), &pt );
    return vaVector2::ComponentDiv( vaVector2( (float)pt.x, (float)pt.y ), vaVector2( m_windClientSize ) );
}
        
