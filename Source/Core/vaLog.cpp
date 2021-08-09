///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaLog.h"

#include "vaCoreIncludes.h"

#include "Misc/vaPropertyContainer.h"

// if VA_REMOTERY_INTEGRATION_ENABLED, dump log stuff there too
#ifdef VA_REMOTERY_INTEGRATION_ENABLED
#include "IntegratedExternals\vaRemoteryIntegration.h"
#endif

#include <sstream>


using namespace Vanilla;

vaLog::vaLog( )
{
    m_lastAddedTime = 0;

    m_timer.Start();

    if( !m_outStream.Open( vaCore::GetExecutableDirectory( ) + L"log.txt", FileCreationMode::Create, FileAccessMode::Write, FileShareMode::Read ) )
    {
        // where does one log failure to open the log file?
        vaCore::DebugOutput( L"Unable to open log output file" );
    }
    else
    {
        // Using byte order marks: https://msdn.microsoft.com/en-us/library/windows/desktop/dd374101
        uint16 utf16LE = 0xFEFF;
        m_outStream.WriteValue<uint16>(utf16LE);
    }
}

vaLog::~vaLog( )
{
    m_timer.Stop();

    m_outStream.Close();
}

void vaLog::Clear( )
{
    vaRecursiveMutexScopeLock lock( m_mutex );
    m_logEntries.clear();
}

void vaLog::Add( const vaVector4 & color, const std::wstring & text )
{
    vaRecursiveMutexScopeLock lock( m_mutex );

    vaCore::DebugOutput( /*L"vaLog: " + */text + L"\n" );

#ifdef VA_REMOTERY_INTEGRATION_ENABLED
    rmt_LogText( vaStringTools::SimpleNarrow(text).c_str() );
#endif

    std::wstringstream sstext( text );
    wstring line;
    //int lineNum = 1;
    while( std::getline( sstext, line ) )
    {
        time_t locTime = time( NULL );
        double now = m_timer.GetCurrentTimeDouble( );
        assert( now >= m_lastAddedTime );
        if( now > m_lastAddedTime )
            m_lastAddedTime = now;
        m_logEntries.push_back( Entry( color, line, locTime, m_lastAddedTime ) );

        if( m_logEntries.size( ) > c_maxEntries )
        {
            const int countToDelete = c_maxEntries / 10;
            m_logEntries.erase( m_logEntries.begin(), m_logEntries.begin()+countToDelete );
        }

        if( m_outStream.IsOpen() )
        {
            char buff[64];
#pragma warning ( suppress: 4996 )
            strftime( buff, sizeof( buff ), "%H:%M:%S: ", localtime( &locTime ) );

            m_outStream.WriteTXT( vaStringTools::SimpleWiden(buff) + line + L"\r\n" );
        }
    }
}

void vaLog::Add( const char * messageFormat, ... )
{
    va_list args;
    va_start( args, messageFormat );
    std::string txt = vaStringTools::Format( messageFormat, args );
    va_end( args );

    Add( LOG_COLORS_NEUTRAL, vaStringTools::SimpleWiden(txt) );
}

void vaLog::Add( const wchar_t * messageFormat, ... )
{
    va_list args;
    va_start( args, messageFormat );
    std::wstring txt = vaStringTools::Format( messageFormat, args );
    va_end( args );

    Add( LOG_COLORS_NEUTRAL, txt );
}

void vaLog::Add( const vaVector4 & color, const char * messageFormat, ... )
{
    va_list args;
    va_start( args, messageFormat );
    std::string txt = vaStringTools::Format( messageFormat, args );
    va_end( args );

    Add( color, vaStringTools::SimpleWiden( txt ) );
}

void vaLog::Add( const vaVector4 & color, const wchar_t * messageFormat, ... )
{
    va_list args;
    va_start( args, messageFormat );
    std::wstring txt = vaStringTools::Format( messageFormat, args );
    va_end( args );

    Add( color, txt );
}

int vaLog::FindNewest( float maxAgeSeconds )
{
    vaRecursiveMutexScopeLock lock( m_mutex );

    if( m_logEntries.size() == 0 )
        return (int)m_logEntries.size();

    double now = m_timer.GetCurrentTimeDouble( );
    double searchTime = now - (double)maxAgeSeconds;

    assert( now >= m_lastAddedTime );

    // nothing to return?
    if( m_logEntries.back().SystemTime < searchTime )
        return (int)m_logEntries.size();

    // return all?
    if( m_logEntries.front( ).SystemTime >= searchTime )
        return 0;

    int currentIndex    = (int)m_logEntries.size()-1;
    int prevStepIndex   = currentIndex;
    int stepSize        = vaMath::Max( 1, currentIndex / 100 );
    while( true )
    {
        assert( m_logEntries[prevStepIndex].SystemTime >= searchTime );

        currentIndex = vaMath::Max( 0, prevStepIndex - stepSize );

        if( m_logEntries[currentIndex].SystemTime < searchTime )
        {
            if( stepSize == 1 )
                return currentIndex+1;
            currentIndex = prevStepIndex + stepSize;
            stepSize = (stepSize+1) / 2;
        }
        else
        {
            prevStepIndex = currentIndex;
        }
    }

    // shouldn't ever happen!
    assert( false );
    return -1;
}


