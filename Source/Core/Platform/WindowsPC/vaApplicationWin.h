///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaCoreIncludes.h"

#include "Core/vaApplicationBase.h"

#include "vaInputKeyboard.h"
#include "vaInputMouse.h"

namespace Vanilla
{

    class vaRenderDevice;

    class vaApplicationWin : public vaApplicationBase
    {
    public:
        struct Settings : vaApplicationBase::Settings
        {
            HCURSOR         Cursor;
            HICON           Icon;
            HICON           SmallIcon;
            int             CmdShow;

            Settings( const string & appName = "Name me plz", const wstring & cmdLine = L"", int cmdShow = SW_SHOWDEFAULT );
        };

    protected:
        const Settings                      m_localSettings;

        wstring                             m_wndClassName;
        HWND                                m_hWnd;

        HMENU                               m_systemMenu;

        HCURSOR                             m_cursorHand;
        HCURSOR                             m_cursorArrow;
        HCURSOR                             m_cursorNone;

        bool                                m_preventWMSIZEResizeSwapChain;
        bool                                m_inResizeOrMove;

        vaInputKeyboard                     m_keyboard;
        vaInputMouse                        m_mouse;

    public:
        vaApplicationWin( const Settings & settings, const std::shared_ptr<vaRenderDevice> & renderDevice, const vaApplicationLoopFunction & callback = nullptr );
        virtual ~vaApplicationWin( );

        //
    public:
        // run the main loop!
        virtual void                        Run( );
        //
    public:
        const vaSystemTimer &               GetMainTimer( ) const                   { return m_mainTimer; }
        //
        vaVector2i                          GetWindowPosition( ) const override ;
        void                                SetWindowPosition( const vaVector2i & position ) override ;
        //
        vaVector2i                          GetWindowClientAreaSize( ) const override ;
        void                                SetWindowClientAreaSize( const vaVector2i & clientSize ) override ;
        //        //
        float                               GetAvgFramerate( ) const                { return m_avgFramerate; }
        float                               GetAvgFrametime( ) const                { return m_avgFrametime; }
        //
        virtual void                        CaptureMouse( );
        virtual void                        ReleaseMouse( );
        //
    protected:
        virtual void                        Initialize( ) override;
        void                                UpdateMouseClientWindowRect( );
        //
    protected:

        virtual void                        Tick( float deltaTime );

        bool                                IsWindowFullscreenInternal( ) const;
        void                                SetFullscreenWindowInternal( bool fullscreen );

    public:

        HWND                                GetMainHWND( ) const                    { return m_hWnd; }

        virtual bool                        MessageLoopTick( ) override;

    protected:
        virtual void                        PreWndProcOverride( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, bool & overrideDefault );
        virtual LRESULT                     WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );

    private:
        static LRESULT CALLBACK             WndProcStatic( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );

        void                                UpdateDeviceSizeOnWindowResize( );
        //
        bool                                UpdateUserWindowChanges( );

    public:
        // This creates the render device, application and calls initialize and shutdown callbacks - just an example of use, one can do everything manually instead
        // Callback arguments are: vaRenderDevice & device, vaApplicationBase & application, float deltaTime, ApplicationStage state
        static void                         Run( const vaApplicationWin::Settings & settings, const vaApplicationLoopFunction & callback, string defaultAPI = "DirectX12" );

        static std::vector<pair<string, string>> EnumerateGraphicsAPIsAndAdapters( );

    };

}