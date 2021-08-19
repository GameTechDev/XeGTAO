///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaPlatformBase.h"

#include "Core/vaStringTools.h"

#include "System/vaPlatformSocket.h"
#include "Core/System/vaFileTools.h"

#include "Core/vaLog.h"

#include "assert.h"

#include <shobjidl.h> 

#include "intrin.h"

#include <tchar.h>

bool evilg_inOtherMessageLoop_PreventTick = false;

namespace Vanilla
{
    //void vaNetworkManagerWin32_CreateManager();
}

using namespace Vanilla;

static HWND    g_mainWindow = NULL;

void vaWindows::SetMainHWND( HWND hWnd )
{
    g_mainWindow = hWnd;
}

HWND vaWindows::GetMainHWND( )
{
    return g_mainWindow;
}

void vaPlatformBase::SetThreadName( const std::string & name )
{
    wstring wname = vaStringTools::SimpleWiden( name );
    HRESULT hr = SetThreadDescription( GetCurrentThread( ), wname.c_str() );
    hr;
    assert( SUCCEEDED( hr ) );
}

void vaPlatformBase::Initialize( )
{
    srand( ( unsigned )::GetTickCount( ) );

    //vaNetworkManagerWin32_CreateManager();

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    hr;
    assert( SUCCEEDED( hr ) );
}

void vaPlatformBase::Deinitialize( )
{
     CoUninitialize();
}

void vaPlatformBase::DebugOutput( const wchar_t * message )
{
    OutputDebugString( message );
}

void vaPlatformBase::Error( const wchar_t * messageString )
{
    DebugOutput( messageString ); DebugOutput( L"\n" );
    evilg_inOtherMessageLoop_PreventTick = true;
    ::MessageBoxW( NULL, messageString, L"Fatal error", MB_ICONERROR | MB_OK );
    VA_LOG_ERROR( messageString );
    evilg_inOtherMessageLoop_PreventTick = false;
    assert( false );
    // exit( 1 );
}

void vaPlatformBase::Warning( const wchar_t * messageString )
{
    DebugOutput( messageString ); DebugOutput( L"\n" );
    evilg_inOtherMessageLoop_PreventTick = true;
    //::MessageBoxW( NULL, messageString, L"Warning", MB_ICONWARNING | MB_OK );
    VA_LOG_WARNING( messageString );
    evilg_inOtherMessageLoop_PreventTick = false;
}

bool vaPlatformBase::MessageBoxYesNo( const wchar_t * titleString, const wchar_t * messageString )
{
    evilg_inOtherMessageLoop_PreventTick = true;
    int res = ::MessageBoxW( NULL, messageString, titleString, MB_ICONQUESTION | MB_YESNO );
    evilg_inOtherMessageLoop_PreventTick = false;

    return res == IDYES;
}

wstring vaCore::GetWorkingDirectory( )
{
    wchar_t buffer[4096];
    GetCurrentDirectory( _countof( buffer ), buffer );
    return wstring( buffer ) + L"\\";
}

wstring vaCore::GetExecutableDirectory( )
{
    wchar_t buffer[4096];

    GetModuleFileName( NULL, buffer, _countof( buffer ) );

    wstring outDir;
    vaFileTools::SplitPath( buffer, &outDir, NULL, NULL );

    outDir = vaFileTools::GetAbsolutePath( outDir );

    return outDir;
}

string vaCore::GetCPUIDName( )
{
    // Get extended ids.
    int CPUInfo[4] = { -1 };
    __cpuid( CPUInfo, 0x80000000 );
    unsigned int nExIds = CPUInfo[0];

    // Get the information associated with each extended ID.
    char CPUBrandString[0x40] = { 0 };
    for( unsigned int i = 0x80000000; i <= nExIds; ++i )
    {
        __cpuid( CPUInfo, i );

        // Interpret CPU brand string and cache information.
        if( i == 0x80000002 )
        {
            memcpy( CPUBrandString,
                CPUInfo,
                sizeof( CPUInfo ) );
        }
        else if( i == 0x80000003 )
        {
            memcpy( CPUBrandString + 16,
                CPUInfo,
                sizeof( CPUInfo ) );
        }
        else if( i == 0x80000004 )
        {
            memcpy( CPUBrandString + 32, CPUInfo, sizeof( CPUInfo ) );
        }
    }

    return CPUBrandString;
}

bool vaFileTools::EnsureDirectoryExists( const wchar_t * path )
{
    //if( DirectoryExists( path ) )
    //   return true;

    int pathLength = (int)wcslen( path );

    const wchar_t separator = L'|';

    wchar_t * workPathStr = new wchar_t[pathLength + 3];

    wcscpy_s( workPathStr, pathLength + 3, path );

    for( int i = 0; i < pathLength; i++ )
    {
        if( ( workPathStr[i] == L'\\' ) || ( workPathStr[i] == L'/' ) )
            workPathStr[i] = separator;
    }

    wchar_t * nextSep;

    while( ( nextSep = wcschr( workPathStr, separator ) ) != 0 )
    {
        *nextSep = 0;
        if( wcslen( workPathStr ) > 2 || wcslen( workPathStr ) && workPathStr[1] != L':' ) ::CreateDirectoryW( workPathStr, 0 );
        *nextSep = L'\\';
    }

    {
        pathLength = (int)wcslen( workPathStr );
        if( workPathStr[1] != L':' && ( pathLength > 1 || pathLength && workPathStr[0] != L'\\' ) || pathLength > 3 || pathLength > 2 && workPathStr[2] != L'\\' )
            CreateDirectoryW( workPathStr, 0 );
    }
    delete[] workPathStr;

    return true; // DirectoryExists( path );
}

void PlatformLogSystemInfo( )
{
    SYSTEM_INFO siSysInfo;
    GetNativeSystemInfo( &siSysInfo );

    VA_LOG("System info:");  
    VA_LOG("   OEM ID:                  %u",  siSysInfo.dwOemId );
    VA_LOG("   Number of processors:    %u",  siSysInfo.dwNumberOfProcessors );
    VA_LOG("   Processor type, level:   %u, %u", siSysInfo.dwProcessorType, siSysInfo.wProcessorLevel );
    VA_LOG("   Page size:               %u",  siSysInfo.dwPageSize );
    VA_LOG("   Active processor mask:   0x%x",  siSysInfo.dwActiveProcessorMask );
    VA_LOG("   TODO: add some more useful info here like CPU/GPU types, OS version etc :) " );
    
}