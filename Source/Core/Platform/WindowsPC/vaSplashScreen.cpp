///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma warning ( disable : 4458  )

#include <windows.h>
#include <stdlib.h>
#include <gdiplus.h> // for splash screen
#pragma comment(lib,"gdiplus.lib")

#include "vaSplashScreen.h"

#include "Core/System/vaFileTools.h"

using namespace Vanilla;
using namespace Gdiplus;

const wchar_t * c_splashClass = L"vaSplashScreen";

vaSplashScreen::vaSplashScreen( )
{
    HINSTANCE instance = GetModuleHandle( NULL );
    if( instance == NULL )  // make Klocwork happy
    {
        assert( false );
        return;
    }

    wstring file = vaCore::GetMediaRootDirectory( ) + L"splash.png";
    if( !vaFileTools::FileExists( file ) )
    {
        VA_LOG_WARNING( "Splash screen image file '%s' not found", file.c_str() );
        m_hbitmap = NULL;
        return;
    }

    GdiplusStartupInput gpStartupInput;
    ULONG_PTR gpToken = 0;
    {
        VA_GENERIC_RAII_SCOPE( GdiplusStartup(&gpToken, &gpStartupInput, NULL);, GdiplusShutdown( gpToken ); )
        Gdiplus::Bitmap * bitmap = Gdiplus::Bitmap::FromFile( file.c_str(), false );
        if( bitmap == nullptr )
        {
            VA_LOG_WARNING( "Unable to load splash screen image file '%s'", file.c_str() );
            m_hbitmap = NULL;
            return;
        }

        bitmap->GetHBITMAP( Color(255, 255, 255), &m_hbitmap );
        assert( m_hbitmap != 0 );
        m_hbitmapSize.cx = bitmap->GetWidth();
        m_hbitmapSize.cy = bitmap->GetHeight();
        delete bitmap;
    }
   
    // register window class
    {
        WNDCLASS wc = { 0 };
        wc.lpfnWndProc      = DefWindowProc;
        wc.hInstance        = instance;
        wc.hIcon            = NULL;
        wc.hCursor          = NULL; // pass through constructor as well? not really needed...
        wc.lpszClassName    = c_splashClass;
        ::RegisterClass(&wc);
    }

    // create window
    m_hwnd = CreateWindowEx( WS_EX_TOOLWINDOW | WS_EX_LAYERED, c_splashClass, NULL, WS_POPUP | WS_VISIBLE, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    SetWindowPos( m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );

    // base location on the primary monitor
    POINT ptZero   = { 0, 0 };
    MONITORINFO monitorInfo = { 0 }; monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfo(MonitorFromPoint( ptZero, MONITOR_DEFAULTTOPRIMARY ), &monitorInfo);

    // put splash at the center
    POINT ptDest = {    monitorInfo.rcWork.left + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - m_hbitmapSize.cx) / 2,
                        monitorInfo.rcWork.top + (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - m_hbitmapSize.cy) / 2 };

    // create a secondary device context for the image
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmapPrev = (HBITMAP)SelectObject( hdcMem, m_hbitmap );

    // use alpha if available
    BLENDFUNCTION blendFunction = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    // this paints the window
    UpdateLayeredWindow( m_hwnd, hdcScreen, &ptDest, &m_hbitmapSize, hdcMem, &ptZero, RGB( 0, 0, 0 ), &blendFunction, ULW_ALPHA );

    // cleanup
    SelectObject( hdcMem, hBitmapPrev );
    DeleteDC( hdcMem );
    ReleaseDC( NULL, hdcScreen );
}

vaSplashScreen::~vaSplashScreen( )
{
    if( m_hwnd != NULL )
        ::DestroyWindow( m_hwnd );
    if( m_hbitmap != NULL )
        ::DeleteObject( m_hbitmap );
}

bool vaSplashScreen::FadeOut( bool immediateClose )
{
    vaSplashScreen * instance = vaSplashScreen::GetInstancePtr( );
    if( instance == nullptr )
        return false;

    BYTE alpha = (BYTE)( instance->m_fadeoutTickCurrent / float(c_fadeoutTickTotal) * 255.0f );
    ::SetLayeredWindowAttributes( instance->m_hwnd, 0, alpha, LWA_ALPHA);

    instance->m_fadeoutTickCurrent--;
    if( instance->m_fadeoutTickCurrent < 0 || immediateClose )
    {
        delete instance;
        return true;
    }
    return false;
}