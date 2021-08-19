///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Project info
//
// Vanilla codebase was originally created by filip.strugar@intel.com for personal research & development use.
//
// It was intended to be an MIT-licensed platform for experimentation with DirectX, with rudimentary asset loading 
// through AssImp, simple rendering pipeline and support for at-runtime shader recompilation, basic post-processing, 
// basic GPU profiling and UI using Imgui, basic non-optimized vector math and other helpers.
//
// While the original codebase was designed in a platform independent way, the current state is that it only supports 
// DirectX 11 & DirectX 12 on Windows Desktop and HLSL shaders. So, it's not platform or API independent. I've abstracted 
// a lot of core stuff, and the graphics API parts are abstracted through the VA_RENDERING_MODULE_CREATE system, which 
// once handled Dx9 and OpenGL modules.
//
// It is very much work in progress with future development based on short term needs, so feel free to use it any way 
// you like (and feel free to contribute back to it), but also use it at your own peril!
// 
// Filip Strugar, 14 October 2016
// 
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "..\vaConfig.h"

// reverting back to 'unsecure' for cross-platform compatibility
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

// CRT's memory leak detection
#if defined(DEBUG) || defined(_DEBUG)

#define _CRTDBG_MAP_ALLOC
#define _CRTDBG_MAP_ALLOC_NEW
#include <stdlib.h>
#include <crtdbg.h>

#endif

#include "vaSTL.h"

#include "vaPlatformBase.h"

#include "vaCoreTypes.h"

#include "Core/Misc/vaXXHash.h"

#include <assert.h>
#include <stdlib.h>

// not platform independent yet - maybe use http://graemehill.ca/minimalist-cross-platform-uuid-guid-generation-in-c++/
#define __INLINE_ISEQUAL_GUID
#include <initguid.h>
#include <cguid.h>

#ifdef _MSC_VER
#define VA_USE_NATIVE_WINDOWS_TIMER
#endif

namespace Vanilla
{
    class vaSystemManagerBase;
    class vaMappedString;

    struct vaGUID : GUID
    {
        vaGUID( ) noexcept { }
        vaGUID( const char * str );
        vaGUID( unsigned long data1, unsigned short data2, unsigned short data3, unsigned char data4[8] ) noexcept { Data1 = data1; Data2 = data2; Data3 = data3; for( int i = 0; i < _countof( Data4 ); i++ ) Data4[i] = data4[i]; }
        vaGUID( unsigned long data1, unsigned short data2, unsigned short data3, unsigned char data40, unsigned char data41, unsigned char data42, unsigned char data43, unsigned char data44, unsigned char data45, unsigned char data46, unsigned char data47 ) noexcept { Data1 = data1; Data2 = data2; Data3 = data3; Data4[0] = data40; Data4[1] = data41; Data4[2] = data42; Data4[3] = data43; Data4[4] = data44; Data4[5] = data45; Data4[6] = data46; Data4[7] = data47; }
        explicit vaGUID( const GUID & copy ) noexcept { Data1 = copy.Data1; Data2 = copy.Data2; Data3 = copy.Data3; for( int i = 0; i < _countof( Data4 ); i++ ) Data4[i] = copy.Data4[i]; }
        vaGUID( const vaGUID & copy ) noexcept { Data1 = copy.Data1; Data2 = copy.Data2; Data3 = copy.Data3; for( int i = 0; i < _countof( Data4 ); i++ ) Data4[i] = copy.Data4[i]; }

        vaGUID & operator =( const GUID & copy ) noexcept { Data1 = copy.Data1; Data2 = copy.Data2; Data3 = copy.Data3; for( int i = 0; i < _countof( Data4 ); i++ ) Data4[i] = copy.Data4[i]; return *this; }

        const static vaGUID             Null;

        // todo: move other GUID stuff from core here
        static vaGUID                   Create( );
        std::string                     ToString( ) const;

        bool                            IsNull( ) const noexcept    { struct Z{ uint64 a; uint64 b; }; const Z * zuid = reinterpret_cast<const Z*>(this); return zuid->a == 0 && zuid->b == 0; }

        static vaGUID                   FromString( const string & str );
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // vaCore 
    class vaCore
    {
    private:
        static bool                     s_initialized;
        static bool                     s_managersInitialized;

        static bool                     s_currentlyInitializing;
        static bool                     s_currentlyDeinitializing;

        static bool                     s_appQuitFlag;
        static bool                     s_appQuitButRestartFlag;
        static bool                     s_appSafeQuitFlag;          // this, in turn, will set s_appQuitFlag if there's no unsaved changes anywhere - otherwise it will prompt

        static std::vector<weak_ptr<bool>>
                                        s_contentDirtyFlags;

        // use native timers on windows so they can be sync-ed to DirectX / GPU
#ifdef VA_USE_NATIVE_WINDOWS_TIMER
        static LARGE_INTEGER            s_appStartTime;
        static LARGE_INTEGER            s_timerFrequency;
        static double                   s_timerFrequencyRD; // 1 / double(s_timerFrequency)
#else
        static std::chrono::time_point<std::chrono::steady_clock>
                                        s_appStartTime;
#endif

    public:

        // Initialize the system - must be called before any other calls
        static void                     Initialize( bool liveRestart = false );

        // This must only be called from the same thread that called Initialize
        static void                     Deinitialize( bool liveRestart = false );

        // // This must only be called from the same thread that called Initialize
        // static void                  Tick( float deltaTime );

        static void                     Error(   const wstring & messageFormat, const char * fileName, int lineIndex, ... );
        static void                     Error(   const string & messageFormat,  const char * fileName, int lineIndex, ... );
        static void                     Warning( const wstring & messageFormat, const char * fileName, int lineIndex, ... );
        static void                     Warning( const string & messageFormat,  const char * fileName, int lineIndex, ... );
        static void                     DebugOutput( const wstring & message );
        static void                     DebugOutput( const string & message );

        static bool                     MessageBoxYesNo( const wchar_t * title, const wchar_t * messageFormat, ... );
        static void                     MessageLoopTick( );   // if blocking the main thread to wait for the same thread that just started a messagebox and expects input, you have to call this in the waiting loop - messy, not to be used except for error reporting / debugging

        static wstring                  GetWorkingDirectory( );
        static wstring                  GetExecutableDirectory( );
        static wstring                  GetMediaRootDirectory( )            { return vaCore::GetExecutableDirectory( ) + L"Media\\"; }

        static string                   GetWorkingDirectoryNarrow( );
        static string                   GetExecutableDirectoryNarrow( );
        static string                   GetMediaRootDirectoryNarrow( );

        // TODO: remove all these and move to vaGUID
        static vaGUID                   GUIDCreate( );
        static const vaGUID &           GUIDNull( );
        static wstring                  GUIDToString( const vaGUID & id );
        static vaGUID                   GUIDFromString( const wstring & str );
        static string                   GUIDToStringA( const vaGUID & id );
        static vaGUID                   GUIDFromString( const string & str );
        //      static int32                    GUIDGetHashCode( const vaGUID & id );

        static string                   GetCPUIDName( );

        static vaMappedString           MapString( const string & str );
        static vaMappedString           MapString( const char * str );

        static bool                     GetAppQuitFlag( )                       { return s_appQuitFlag; }
        static bool                     GetAppQuitButRestartingFlag( )          { return s_appQuitButRestartFlag; }
        static bool                     GetAppSafeQuitFlag( )                   { return s_appSafeQuitFlag; }

        // this is if you want to prevent the app exiting without a message box saying that there's unsaved work
        // (no need to remove the tracker - just destroy the ptr)
        static void                     AddContentDirtyTracker( weak_ptr<bool> && dirtyFlag );
        static bool                     AnyContentDirty( );

        // this will trigger app quit next frame (when the app picks it up); second flag is used to automatically restart 
        static void                     SetAppQuitFlag( bool quitFlag, bool quitButRestart = false ) { s_appQuitFlag = quitFlag; s_appQuitButRestartFlag = quitButRestart && quitFlag; };

        // this will trigger a prompt 
        static void                     SetAppSafeQuitFlag( bool safeQuitFlag ) { s_appSafeQuitFlag = safeQuitFlag; }

        static void                     System( string sytemCommand );

        // this is thread-safe (as long as the threads are spawned after first line of Initialize() :) )
#ifdef VA_USE_NATIVE_WINDOWS_TIMER
        inline static double            TimeFromAppStart( )                 { LARGE_INTEGER now; ::QueryPerformanceCounter(&now); return (now.QuadPart - s_appStartTime.QuadPart) * s_timerFrequencyRD; }
        inline static uint64            NativeAppStartTime( )               { return s_appStartTime.QuadPart; }
        inline static uint64            NativeTimerFrequency( )             { return s_timerFrequency.QuadPart; }
#else
        inline static double            TimeFromAppStart( )                 { return std::chrono::duration<double>( std::chrono::steady_clock::now( ) - s_appStartTime ).count( ); }
#endif

    private:

    };
    typedef vaCore    vaLevel0;
    ////////////////////////////////////////////////////////////////////////////////////////////////

    inline vaGUID::vaGUID( const char * str )
    {
        *this = vaCore::GUIDFromString( str );
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////
    inline bool operator == ( const vaGUID & left, const vaGUID & right ) noexcept 
    { 
#if 0
        return std::memcmp( &left, &right, sizeof( vaGUID ) ) == 0; 
#else
        struct Z{ uint64 a; uint64 b; }; 
        const Z * zleft = reinterpret_cast<const Z*>(&left); 
        const Z * zright = reinterpret_cast<const Z*>(&right); 
        return zleft->a == zright->a && zleft->b == zright->b;
#endif
    }
    //
    // struct vaGUIDComparer
    // {
    //     bool operator()( const vaGUID & left, const vaGUID & Right ) const noexcept
    //     {
    //         // comparison logic goes here
    //         return memcmp( &left, &right, sizeof( Right ) ) < 0;
    //     }
    // };
    //
    struct vaGUIDHasher
    {
        size_t operator()( const vaGUID & uid ) const noexcept
        {
#if 0
            struct Z{ uint64 a; uint64 b; }; const Z * zuid = reinterpret_cast<const Z*>(&uid); 
            std::hash<std::uint64_t> uint64hash;
            return uint64hash( zuid->a ) ^ uint64hash( zuid->b );
#else
            return vaXXHash64::Compute( &uid, sizeof(uid) );
#endif
        }
    };
    ////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // just adds an assert in debug
    template< typename OutTypeName, typename InTypeName >
    inline OutTypeName vaSaferStaticCast( InTypeName ptr )
    {
#ifdef _DEBUG
        OutTypeName ret = dynamic_cast<OutTypeName>( ptr );
        //assert( ret != nullptr );
        return ret;
#else
        return static_cast <OutTypeName>( ptr );
#endif
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // cached dynamic cast for when we know the return type is always the same - it's rather trivial now, 
    // could be made safe for release path too
    template< typename OutTypeName, typename InTypeName >
    inline OutTypeName vaCachedDynamicCast( InTypeName thisPtr, void*& cachedVoidPtr )
    {
        if( cachedVoidPtr == nullptr )
            cachedVoidPtr = static_cast<void*>( dynamic_cast<OutTypeName>( thisPtr ) );
        OutTypeName retVal = static_cast<OutTypeName>( cachedVoidPtr );
#ifdef _DEBUG
        OutTypeName validPtr = dynamic_cast<OutTypeName>( thisPtr );
        assert( validPtr != NULL );
        assert( retVal == validPtr );
#endif
        return retVal;
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // time & log - see vaTimerLogScope
    ////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // custom generic RAII helper
    template< typename AcquireType, typename FinalizeType >
    class vaGenericScope
    {
        FinalizeType            m_finalize;
    public:
        vaGenericScope( AcquireType&& acquire, FinalizeType&& finalize ) : m_finalize( std::move( finalize ) ) { acquire( ); }
        ~vaGenericScope( ) { m_finalize( ); }
    };
    ////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // can a std::function capture this without additional allocations?
    template< typename CallableType >
    constexpr bool vaIfItFitsISits( )
    {
        constexpr size_t _Space_size = 32; //( std::_Small_object_num_ptrs - 2 ) * sizeof( void* );   // this is totally specific to MSVC 2019 so let's be a bit more conservative
        return _Space_size >= sizeof( CallableType ) && std::is_nothrow_move_constructible<CallableType>::value;
    }
    template< typename CallableType >
    inline void vaAssertSits( CallableType && )
    {
        using decayed = std::decay_t<CallableType>;
        if constexpr( !vaIfItFitsISits<decayed>( ) )
        {
            size_t sizec = sizeof(decayed); 
            bool mc = std::is_nothrow_move_constructible<CallableType>::value; 
            sizec;  // <- should be small (check out vaIfItFitsISits)
            mc;     // <- should be true
            assert( false );            // too big or not move constructible!
        }
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////


// should expand to something like: GenericScope scopevar_1( [ & ]( ) { ImGui::PushID( Scene::Components::TypeName( i ).c_str( ) ); }, [ & ]( ) { ImGui::PopID( ); } );
#define VA_GENERIC_RAII_SCOPE( enter, leave ) vaGenericScope VA_COMBINE( _generic_raii_scopevar_, __COUNTER__ ) ( [&](){ enter }, [&](){ leave } );



    // Various defines

#define VA_COMBINE1(X,Y) X##Y  // helper macro
#define VA_COMBINE(X,Y) VA_COMBINE1(X,Y)

#define VA_WIDE1(x)        L ## x
#define VA_WIDE(x)         VA_WIDE1(x)
//#define VA_NT(x)        L#x


#ifdef _DEBUG
#define VA_ASSERT_ALWAYS( format, ... )                           do { vaCore::Warning( format, __FILE__, __LINE__, __VA_ARGS__ ); assert( false ); } while( false )
#define VA_ASSERT( condition, format, ... )  if( !(condition) )   do { VA_ASSERT_ALWAYS( format, __VA_ARGS__ ); } while( false ) //{ vaCore::Warning( L"%s:%d\n" format , VA_T(__FILE__), __LINE__, __VA_ARGS__ ); assert( false ); } 
#else
#define VA_ASSERT_ALWAYS( format, ... )  { }
#define VA_ASSERT( condition, format, ... )  { }
#endif

// recoverable error or a warning
#define VA_WARN( format, ... )                                      do { vaCore::Warning( format, __FILE__, __LINE__, __VA_ARGS__); } while( false )

// irrecoverable error, save the log and die gracefully
#define VA_ERROR( format, ... )                                     do { vaCore::Error( format, __FILE__, __LINE__, __VA_ARGS__); } while( false )

#define VA_VERIFY( x, format, ... )                                 do { bool __va_verify_res = (x); if( !__va_verify_res ) { VA_ASSERT_ALWAYS( format, __VA_ARGS__ ); } } while( false )
#define VA_VERIFY_RETURN_IF_FALSE( x, format, ... )                 do { bool __va_verify_res = (x); if( !__va_verify_res ) { VA_ASSERT_ALWAYS( format, __VA_ARGS__ ); return false; } } while( false )

// Other stuff

// warning C4201: nonstandard extension used : nameless struct/union
#pragma warning( disable : 4201 )

// warning C4239: nonstandard extension used : 'default argument' 
#pragma warning( disable : 4239 )

#ifndef SAFE_DELETE
#define SAFE_DELETE(p)          { if (p) { delete (p);     (p)=NULL; } }
#endif
#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p)    { if (p) { delete[] (p);   (p)=NULL; } }
#endif
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)         { if (p) { (p)->Release(); (p)=NULL; } }
#endif
#ifndef SAFE_RELEASE_ARRAY
#define SAFE_RELEASE_ARRAY(p)   { for( int i = 0; i < _countof(p); i++ ) if (p[i]) { (p[i])->Release(); (p[i])=NULL; } }
#endif

#ifndef VERIFY_TRUE_RETURN_ON_FALSE
#define VERIFY_TRUE_RETURN_ON_FALSE( x )        \
do                                              \
{                                               \
if( !(x) )                                      \
{                                               \
    assert( false );                            \
    return false;                               \
}                                               \
} while( false )
#endif

    template <typename _CountofType, size_t _SizeOfArray>
    char( *__va_countof_helper( _UNALIGNED _CountofType( &_Array )[_SizeOfArray] ) )[_SizeOfArray];

    #define countof(_Array) (sizeof(*__va_countof_helper(_Array)) + 0)

    template<typename>
    struct __array_size;
    template<typename T, size_t N>
    struct __array_size<std::array<T, N> > {
        static size_t const size = N;
    };

#define array_size(_Array) (__array_size<decltype(_Array)>::size)
}