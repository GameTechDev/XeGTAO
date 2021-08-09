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

#include "..\vaCore.h"

#include <ctime>
#include <functional>

namespace Vanilla
{
    // vaMiniScript implements a way to run c++ script code as a coroutine - they each get their own thread,
    // but are never run in parallel with the main thread (the one that created vaMiniScript), and they hand
    // over / yield execution each other.
    // The main thread calls TickScript() which unblocks the script thread (if any) and waits until it
    // calls YieldExecution(), and so on.

    class vaMiniScriptInterface
    {
    protected:
        virtual ~vaMiniScriptInterface( ) { }

    public:
        // script should always check for the return value and if it's false it must stop the function
        virtual bool                        YieldExecution( )    = 0;

        // loop YieldExecution until either at least deltaTime amount of time passes or YieldExecution returns false
        virtual bool                        YieldExecutionFor( float deltaTime )    = 0;

        // loop YieldExecution until either at least numberOfFrames number of frames or YieldExecution returns false
        virtual bool                        YieldExecutionFor( int numberOfFrames )    = 0;

        // (optional) will get called from the main thread when TickUI gets called
        virtual void                        SetUICallback( const std::function< void( ) > & UIFunction )    = 0;

        // just returns the 'float deltaTime' that the TickScript received
        virtual float                       GetDeltaTime( ) = 0;
    };

    class vaMiniScript : public vaMiniScriptInterface
    {
    private:
        enum ExecutionOwnership
        {
            EO_MainThread,
            EO_ScriptThread,
            EO_Inactive
        };

    private:
        bool                                m_active            = false;
        bool                                m_stopRequested     = false;

        std::function< void( vaMiniScriptInterface & ) >
                                            m_scriptFunction;
        std::function< void( ) >            m_UIFunction;

        std::thread                         m_scriptThread;

        std::mutex                          m_mutex;
        std::condition_variable             m_cv;
        
        ExecutionOwnership                  m_currentOwnership      = EO_Inactive;
        
        float                               m_lastDeltaTime;

        std::atomic<std::thread::id>        m_mainThreadID      = std::this_thread::get_id();
        std::atomic<std::thread::id>        m_scriptThreadID;

    private:

    public:
        vaMiniScript( );
        virtual ~vaMiniScript( );

        bool                                Start( const std::function< void( vaMiniScriptInterface & ) > & scriptFunction );
        bool                                IsActive( )                         { std::unique_lock lk( m_mutex ); return m_active; }
        void                                TickScript( float deltaTime );
        void                                TickUI( );
        void                                Stop( );

    private:
        void                                ScriptThread( );

        virtual void                        SetUICallback( const std::function< void( ) > & UIFunction ) override   { assert( std::this_thread::get_id() == m_scriptThreadID ); std::unique_lock lk( m_mutex ); m_UIFunction = UIFunction; }
        virtual bool                        YieldExecution( ) override;
        virtual bool                        YieldExecutionFor( float deltaTime );
        virtual bool                        YieldExecutionFor( int numberOfFrames );
        virtual float                       GetDeltaTime( ) override                                                { assert( std::this_thread::get_id() == m_scriptThreadID ); std::unique_lock lk( m_mutex ); return m_lastDeltaTime; }
    };
}