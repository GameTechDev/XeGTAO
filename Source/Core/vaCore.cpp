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

#include "Core/Misc/vaBenchmarkTool.h"

#include "vaProfiler.h"

#include "Core/vaInput.h"

#include "Core/System/vaFileTools.h"

#include "Core/vaUI.h"

#include "Core/vaUIDObject.h"

#include "Rendering/vaRendering.h"

#include "Core/vaApplicationBase.h"

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
#include "IntegratedExternals/vaTaskflowIntegration.h"
#endif

#include "vaSplashScreen.h"

// for GUIDs
#include <Objbase.h>
#pragma comment( lib, "Ole32.lib" )
#pragma comment( lib, "Rpcrt4.lib" )

using namespace Vanilla;

bool vaCore::s_initialized = false;

bool vaCore::s_appQuitFlag = false;
bool vaCore::s_appQuitButRestartFlag = false;
bool vaCore::s_appSafeQuitFlag = false;

bool vaCore::s_currentlyInitializing = false;
bool vaCore::s_currentlyDeinitializing = false;
std::vector<weak_ptr<bool>> vaCore::s_contentDirtyFlags;


#ifdef VA_USE_NATIVE_WINDOWS_TIMER
LARGE_INTEGER   vaCore::s_appStartTime;
LARGE_INTEGER   vaCore::s_timerFrequency;
double          vaCore::s_timerFrequencyRD;
#else
std::chrono::time_point<std::chrono::steady_clock>
vaCore::s_appStartTime;
#endif

vaRandom vaRandom::Singleton;

alignas( VA_ALIGN_PAD * 2 ) static std::mutex           
                            s_globalStringDictionaryMutex;
static vaStringDictionary   s_globalStringDictionary;

// int omp_thread_count( )
// {
//     int n = 0;
// #pragma omp parallel reduction(+:n)
//     n += 1;
//     return n;
// }

void vaCore::Initialize( bool liveRestart )
{
    if( !liveRestart )
    {
#ifdef VA_USE_NATIVE_WINDOWS_TIMER
        ::QueryPerformanceCounter(&s_appStartTime);
        ::QueryPerformanceFrequency(&s_timerFrequency);
        s_timerFrequencyRD = 1.0 / double(s_timerFrequency.QuadPart);
#else
        s_appStartTime = std::chrono::steady_clock::now( );
#endif
    }

    // Initializing more than once?
    assert( !s_initialized );

    if( liveRestart )
    { assert( vaThreading::IsMainThread() ); }

    // std::thread::hardware_concurrency()
    int physicalPackages, physicalCores, logicalCores;
    vaThreading::GetCPUCoreCountInfo( physicalPackages, physicalCores, logicalCores );

    if( !liveRestart )
    {
        vaThreading::SetMainThread( );

        vaMemory::Initialize( );

        // splash after memory init to avoid warnings...
        if( vaSplashScreen::GetInstancePtr() == nullptr )
            new vaSplashScreen( );

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
        //new vaTF( std::max( 2, physicalCores ) );
        //new vaTF( std::max( 2, (physicalCores + logicalCores*2)/3 ) );
        //new vaTF( std::max( 2, (physicalCores + logicalCores*5)/6 ) );
        new vaTF( std::max( 2, logicalCores-1 ) );
        //new vaTF( std::max( 2, logicalCores ) );
#endif

        new vaUIDObjectRegistrar( );

        vaPlatformBase::Initialize( );

        new vaLog( );

        void PlatformLogSystemInfo( );
        PlatformLogSystemInfo( );

        new vaRenderingModuleRegistrar( );
    }

    vaFileTools::Initialize( );

    new vaUIManager( );
    new vaUIConsole( );

    new vaBackgroundTaskManager( );

    new vaBenchmarkTool( );

    // useful to make things more deterministic during restarts
    vaRandom::Singleton.Seed(0);

    s_initialized = true;

//    vaVector4 a = {0.4f, 0.3f, 0.2f,      0.0f};
//    vaVector4 b = {1000.4f, 0.3f, 0.2f,   0.1f};
//    vaVector4 c = {0.4f, 1000.4f, 0.2f,   0.5f};
//    vaVector4 d = {0.1f, 0.2f, 0.3f,      1.0f };
//
//    uint32 pa = Pack_R10G10B10FLOAT_A2_UNORM( a );
//    uint32 pb = Pack_R10G10B10FLOAT_A2_UNORM( b );
//    uint32 pc = Pack_R10G10B10FLOAT_A2_UNORM( c );
//    uint32 pd = Pack_R10G10B10FLOAT_A2_UNORM( d );
//
//    a = Unpack_R10G10B10FLOAT_A2_UNORM( pa );
//    b = Unpack_R10G10B10FLOAT_A2_UNORM( pb );
//    c = Unpack_R10G10B10FLOAT_A2_UNORM( pc );
//    d = Unpack_R10G10B10FLOAT_A2_UNORM( pd );

}

void vaCore::Deinitialize( bool liveRestart )
{
    assert( s_initialized );

    delete vaBenchmarkTool::GetInstancePtr( );

    delete vaBackgroundTaskManager::GetInstancePtr( );
    delete vaUIConsole::GetInstancePtr( );
    delete vaUIManager::GetInstancePtr( );

    if( AnyContentDirty( ) )
        VA_LOG_WARNING( "There was some dirty content reported by vaCore::AnyContentDirty before deinitialization." );
    std::swap( s_contentDirtyFlags, decltype(s_contentDirtyFlags)() ); // same as s_contentDirtyFlags.clear( ); s_contentDirtyFlags.shrink_to_fit();
    
    vaFileTools::Deinitialize( );

    if( !liveRestart )
    {
        delete vaRenderingModuleRegistrar::GetInstancePtr( );

        delete vaLog::GetInstancePtr( );

        vaPlatformBase::Deinitialize( );

        delete vaUIDObjectRegistrar::GetInstancePtr( );

        vaTracer::Cleanup( false );

        {
            std::unique_lock lock( s_globalStringDictionaryMutex );
            s_globalStringDictionary.Reset( );
        }

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
        delete vaTF::GetInstancePtr( );
#endif

        if( vaSplashScreen::GetInstancePtr() != nullptr )
            delete vaSplashScreen::GetInstancePtr();

        vaFramePtrStatic::Cleanup( );

        vaMemory::Deinitialize( );
    }
    else
    {
        vaTracer::Cleanup( true );
    }

    s_initialized = false;
}

void vaCore::DebugOutput( const wstring & message )
{
    vaPlatformBase::DebugOutput( message.c_str( ) );
}

void vaCore::Error( const wstring & messageFormat, const char * fileName, int lineIndex, ... )
{
    va_list args;
    va_start( args, lineIndex );
    std::wstring ret = vaStringTools::SimpleWiden( vaStringTools::Format( "%s:%d : ", fileName, lineIndex ) ) + vaStringTools::Format( messageFormat.c_str(), args );
    va_end( args );

    if( vaLog::GetInstancePtr( ) != nullptr )
        VA_LOG_ERROR( L"%s", ret.c_str( ) );

    vaPlatformBase::Error( ret.c_str() );
}


void vaCore::Error( const string & messageFormat, const char * fileName, int lineIndex, ... )
{
    va_list args;
    va_start( args, lineIndex );
    std::string ret = vaStringTools::Format( "%s:%d : ", fileName, lineIndex ) + vaStringTools::Format( messageFormat.c_str(), args );
    va_end( args );
    
    if( vaLog::GetInstancePtr( ) != nullptr )
        VA_LOG_ERROR( "%s", ret.c_str( ) );

    vaPlatformBase::Error( vaStringTools::SimpleWiden(ret).c_str( ) );
}

void vaCore::Warning( const wstring & messageFormat, const char * fileName, int lineIndex, ... )
{
    va_list args;
    va_start( args, lineIndex );
    std::wstring ret = vaStringTools::SimpleWiden( vaStringTools::Format( "%s:%d : ", fileName, lineIndex ) ) + vaStringTools::Format( messageFormat.c_str(), args );
    va_end( args );

    if( vaLog::GetInstancePtr( ) != nullptr )
        VA_LOG_WARNING( L"%s", ret.c_str( ) );
}

void vaCore::Warning( const string & messageFormat, const char * fileName, int lineIndex, ... )
{
    va_list args;
    va_start( args, lineIndex );
    std::string ret = vaStringTools::Format( "%s:%d : ", fileName, lineIndex ) + vaStringTools::Format( messageFormat.c_str(), args );
    va_end( args );

    if( vaLog::GetInstancePtr( ) != nullptr )
        VA_LOG_WARNING( "%s", ret.c_str( ) );
}

void vaCore::DebugOutput( const string & message )
{
    vaPlatformBase::DebugOutput( vaStringTools::SimpleWiden( message ).c_str( ) );
}

void vaCore::MessageLoopTick( )
{
    if( vaApplicationBase::GetInstanceValid( ) )
        vaApplicationBase::GetInstance( ).MessageLoopTick( );
}

bool vaCore::MessageBoxYesNo( const wchar_t * title, const wchar_t * messageFormat, ... )
{
    va_list args;
    va_start( args, messageFormat );
    std::wstring ret = vaStringTools::Format( messageFormat, args );
    va_end( args );
    return vaPlatformBase::MessageBoxYesNo( title, ret.c_str( ) );
}

vaGUID vaCore::GUIDCreate( )
{
    vaGUID ret;
    ::CoCreateGuid( &ret );
    return ret;
}

vaGUID vaGUID::Create( )
{
    vaGUID ret;
    ::CoCreateGuid( &ret );
    return ret;
}

const vaGUID vaGUID::Null( 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 );

const vaGUID & vaCore::GUIDNull( )
{
    static vaGUID null = vaGUID( GUID_NULL );
    static vaGUID null2 = vaGUID::Null;
    return null;
}

wstring vaCore::GUIDToString( const vaGUID & id )
{
    wstring ret;
    wchar_t * buffer;
    RPC_STATUS s = UuidToStringW( &id, (RPC_WSTR*)&buffer );
    VA_ASSERT( s == RPC_S_OK, L"GUIDToString failed" );
    s;  // unreferenced in Release
    ret = buffer;
    RpcStringFree( (RPC_WSTR*)&buffer );
    return ret;
}

vaGUID vaCore::GUIDFromString( const wstring & str )
{
    vaGUID ret;
    RPC_STATUS s = UuidFromStringW( (RPC_WSTR)str.c_str( ), &ret );
    VA_ASSERT( s == RPC_S_OK, L"GUIDFromString failed" );
    if( s != RPC_S_OK )
        return GUIDNull( );
    return ret;
}

string vaGUID::ToString( ) const
{
    string ret;
    char* buffer;
    RPC_STATUS s = UuidToStringA( this, (RPC_CSTR*)&buffer );
    VA_ASSERT( s == RPC_S_OK, L"GUIDToStringA failed" );
    s;  // unreferenced in Release
    ret = buffer;
    RpcStringFreeA( (RPC_CSTR*)&buffer );
    return ret;
}

vaGUID vaGUID::FromString( const string & str )
{
    vaGUID ret;
    RPC_STATUS s = UuidFromStringA( (RPC_CSTR)str.c_str( ), &ret );
    VA_ASSERT( s == RPC_S_OK, L"GUIDFromString failed" );
    if( s != RPC_S_OK )
        return vaGUID::Null;
    return ret;
}

string vaCore::GUIDToStringA( const vaGUID & id )
{
    string ret;
    char * buffer;
    RPC_STATUS s = UuidToStringA( &id, (RPC_CSTR*)&buffer );
    VA_ASSERT( s == RPC_S_OK, L"GUIDToStringA failed" );
    s;  // unreferenced in Release
    ret = buffer;
    RpcStringFreeA( (RPC_CSTR*)&buffer );
    return ret;
}

vaGUID vaCore::GUIDFromString( const string & str )
{
    vaGUID ret;
    RPC_STATUS s = UuidFromStringA( (RPC_CSTR)str.c_str( ), &ret );
    VA_ASSERT( s == RPC_S_OK, L"GUIDFromString failed" );
    if( s != RPC_S_OK )
        return GUIDNull( );
    return ret;
}

string vaCore::GetWorkingDirectoryNarrow( ) 
{ 
    return vaStringTools::SimpleNarrow( GetWorkingDirectory( ) ); 
}

string vaCore::GetExecutableDirectoryNarrow( ) 
{ 
    return vaStringTools::SimpleNarrow( GetExecutableDirectory( ) ); 
}

string vaCore::GetMediaRootDirectoryNarrow( ) 
{ 
    return vaStringTools::SimpleNarrow( GetMediaRootDirectory( ) ); 
}

vaMappedString vaCore::MapString( const string& str )
{
    std::unique_lock lock( s_globalStringDictionaryMutex );
    return s_globalStringDictionary.Map( str );
}

vaMappedString vaCore::MapString( const char* str )
{
    std::unique_lock lock( s_globalStringDictionaryMutex );
    return s_globalStringDictionary.Map( str );
}

void vaCore::AddContentDirtyTracker( weak_ptr<bool>&& dirtyFlag )
{
    s_contentDirtyFlags.push_back( std::move(dirtyFlag) );
}

bool vaCore::AnyContentDirty( )
{
    bool anyDirty = false;
    for( int i = (int)s_contentDirtyFlags.size( ) - 1; i >= 0; i-- )
    {
        auto dirtyFlag = s_contentDirtyFlags[i].lock();
        if( dirtyFlag == nullptr )
            s_contentDirtyFlags.erase( s_contentDirtyFlags.begin() + i );
        else
            anyDirty |= *dirtyFlag;
    }
    return anyDirty;
}

void vaCore::System( string sytemCommand )
{
    std::thread([sytemCommand](){ system( sytemCommand.c_str() ); }).detach();
}

vaInputKeyboardBase *    vaInputKeyboardBase::s_current = NULL;
vaInputMouseBase *       vaInputMouseBase::s_current = NULL;
