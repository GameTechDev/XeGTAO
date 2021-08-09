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

#ifndef UNICODE
#error "DXUT requires a Unicode build."
#endif

#include <dxgi1_6.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <sdkddkver.h>
#include "d3dx12.h"

//#include <WindowsX.h>


#pragma comment( lib, "d3dcompiler.lib" )
#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "d3d12.lib" )
#pragma comment( lib, "dxgi.lib" )


//#define DXUT_AUTOLIB
//
//// #define DXUT_AUTOLIB to automatically include the libs needed for DXUT 
//#ifdef DXUT_AUTOLIB
//#pragma comment( lib, "comctl32.lib" )
//#pragma comment( lib, "dxguid.lib" )
//#pragma comment( lib, "ole32.lib" )
//#pragma comment( lib, "uuid.lib" )
//#endif
//
//#pragma warning( disable : 4100 ) // disable unreferenced formal parameter warnings for /W4 builds


#if defined(DEBUG) || defined(_DEBUG)
#ifndef V
#define V(x)           { hr = (x); if( FAILED(hr) ) { VA_ASSERT_ALWAYS( L"FAILED(hr) == true" ); } } //DXUTTrace( __FILE__, (DWORD)__LINE__, hr, L#x, true ); } }
#endif
#ifndef V_RETURN
#define V_RETURN(x)    { hr = (x); if( FAILED(hr) ) { VA_ASSERT_ALWAYS( L"FAILED(hr) == true" ); return hr; } } //return DXUTTrace( __FILE__, (DWORD)__LINE__, hr, L#x, true ); } }
#endif
#else
#ifndef V
#define V(x)           { hr = (x); }
#endif
#ifndef V_RETURN
#define V_RETURN(x)    { hr = (x); if( FAILED(hr) ) { return hr; } }
#endif
#endif

