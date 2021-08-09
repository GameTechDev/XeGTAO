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

#include "vaCore.h"
#include "vaSingleton.h"
#include "vaGeometry.h"
#include <time.h>
#include <chrono>

#include "Core/System/vaSystemTimer.h"
#include "Core/System/vaFileStream.h"
#include "Core/vaStringTools.h"

namespace Vanilla
{

    // system-wide logging colors, can't think of a better place to put them
#define LOG_COLORS_NEUTRAL  (vaVector4( 0.8f, 0.8f, 0.8f, 1.0f ) )
#define LOG_COLORS_SUCCESS  (vaVector4( 0.0f, 0.8f, 0.2f, 1.0f ) )
#define LOG_COLORS_WARNING  (vaVector4( 0.8f, 0.8f, 0.1f, 1.0f ) )
#define LOG_COLORS_ERROR    (vaVector4( 1.0f, 0.1f, 0.1f, 1.0f ) )

    class vaLog : public vaSingletonBase< vaLog >
    {
    public:
        struct Entry
        {
            vaVector4           Color;
            wstring             Text;
            time_t              LocalTime;
            double              SystemTime;

            Entry( const vaVector4 & color, const std::wstring & text, time_t localTime, double appTime ) : Color( color ), Text( text ), LocalTime( localTime ), SystemTime( appTime ) { }
        };

        std::vector<Entry>      m_logEntries;
        double                  m_lastAddedTime;

        vaSystemTimer           m_timer;

        vaRecursiveMutex        m_mutex;

        vaFileStream            m_outStream;

        static const int        c_maxEntries = 100000;
        

    private:
        friend class vaCore;
        vaLog( );
        ~vaLog( );

    public:
        // must lock mutex when using this
        const std::vector<Entry> &   Entries( )  { return m_logEntries; }
        void                    Clear( );

        // must lock mutex if using this to have any meaning
        int                     FindNewest( float maxAgeSeconds );

        // these will lock mutex
        void                    Add( const char * messageFormat, ... );
        void                    Add( const wchar_t * messageFormat, ... );

        void                    Add( const vaVector4 & color, const char * messageFormat, ... );
        void                    Add( const vaVector4 & color, const wchar_t * messageFormat, ... );

#pragma warning ( suppress: 4996 )
        void                    Add( const vaVector4 & color, const std::wstring & text );
        void                    Add( const vaVector4 & color, const std::string & text )   { Add( color, vaStringTools::SimpleWiden( text ) ); }

        vaRecursiveMutex &      Mutex( ) { return m_mutex; }

    private:
    };
    ////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // For measuring & logging occasional long taking tasks like loading of a level or similar. 
    class vaTimerLogScope
    {
    private:
         double         m_start;
         vaVector4      m_color;
         string         m_info;
    public:
        vaTimerLogScope( const string & info, const vaVector4 & color = LOG_COLORS_NEUTRAL ) : m_info(info), m_color(color)
        {
            m_start = vaCore::TimeFromAppStart();
            vaLog::GetInstance().Add( m_color, "%s : starting...", m_info.c_str() );
        }
        ~vaTimerLogScope( )
        {
            double stop = vaCore::TimeFromAppStart();
            float elapsed = (float)(stop-m_start);
            if( elapsed < 1.0f )
                vaLog::GetInstance().Add( m_color, "%s : done, time taken %.3f milliseconds.", m_info.c_str(), elapsed*1000.0f );
            else
                vaLog::GetInstance().Add( m_color, "%s : done, time taken %.3f seconds.", m_info.c_str(), elapsed );
        }
    };
    ////////////////////////////////////////////////////////////////////////////////////////////////



#define VA_LOG( format, ... )                   do { vaLog::GetInstance().Add( LOG_COLORS_NEUTRAL, format, __VA_ARGS__ );   } while(false)
#define VA_LOG_SUCCESS( format, ... )           do { vaLog::GetInstance().Add( LOG_COLORS_SUCCESS, format, __VA_ARGS__ );   } while(false)
#define VA_LOG_WARNING( format, ... )           do { vaLog::GetInstance().Add( LOG_COLORS_WARNING, format, __VA_ARGS__ );   } while(false)
#define VA_LOG_ERROR( format, ... )             do { vaLog::GetInstance().Add( LOG_COLORS_ERROR, format, __VA_ARGS__ );     } while(false)

#define VA_LOG_STACKINFO( format, ... )         do { { vaLog::GetInstance().Add( L"%s:%d : " format , VA_WIDE(__FILE__), __LINE__, __VA_ARGS__); }                     } while(false)
#define VA_LOG_WARNING_STACKINFO( format, ... ) do { { vaLog::GetInstance().Add( LOG_COLORS_WARNING, L"%s:%d : " format , VA_WIDE(__FILE__), __LINE__, __VA_ARGS__); } } while(false)
#define VA_LOG_ERROR_STACKINFO( format, ... )   do { { vaLog::GetInstance().Add( LOG_COLORS_ERROR, L"%s:%d : " format , VA_WIDE(__FILE__), __LINE__, __VA_ARGS__); }   } while(false)
}