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

//#ifndef _WINDOWS_

//#define STRICT
// 
// // Works with Windows 2000 and later and Windows 98 or later
// #undef _WIN32_IE
// #undef WINVER
// #undef _WIN32_WINDOWS
// #undef _WIN32_WINNT
// #define WINVER         0x0500 
// #define _WIN32_WINDOWS 0x0410 
// #define _WIN32_WINNT   0x0500 

#define VC_EXTRALEAN		        // Exclude rarely-used stuff from Windows headers
#define WIN32_LEAN_AND_MEAN         // Exclude rarely-used stuff from Windows headers
#include <windows.h>

#include <tchar.h>

//#include "crtdbg.h"

#include "Core/vaCore.h"

//#endif

namespace Vanilla
{
   class vaPlatformBase
   {
   public:
      static void       Initialize( );
      static void       Deinitialize( );
      static void       Error( const wchar_t * messageString );
      static void       Warning( const wchar_t * messageString );
      static void       DebugOutput( const wchar_t * message );
      static bool       MessageBoxYesNo( const wchar_t * titleString, const wchar_t * messageString );
      static void       SetThreadName( const std::string & name );

   };

   class vaWindows
   {
   public:
      static void       SetMainHWND( HWND hWnd );
      static HWND       GetMainHWND( );
   };
}

