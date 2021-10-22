///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// #define _ITERATOR_DEBUG_LEVEL 0

#define VA_GTAO_SAMPLE 

// leave at "" for the app title to automatically match workspace name
#ifdef VA_GTAO_SAMPLE
#define VA_APP_TITLE                "XeGTAO Sample"
#define VA_MINIMAL_UI
#else
#define VA_APP_TITLE                ""
//#define VA_MINIMAL_UI
#endif

// #define VA_SAMPLE_BUILD_FOR_LAB
// #define VA_SAMPLE_DEMO_BUILD

#if (defined(VA_SAMPLE_BUILD_FOR_LAB) || defined(VA_SAMPLE_DEMO_BUILD)) && !defined(VA_MINIMAL_UI)
#define VA_MINIMAL_UI
#endif

#ifdef VA_MINIMAL_UI
#define VA_MINIMAL_UI_BOOL true
#else
#define VA_MINIMAL_UI_BOOL false
#endif

#define VA_IMGUI_INTEGRATION_ENABLED
// #define VA_LIBTIFF_INTEGRATION_ENABLED
#define VA_TASKFLOW_INTEGRATION_ENABLED
// #define VA_BULLETPHYSICS_INTEGRATION_ENABLED
#define VA_ASSIMP_INTEGRATION_ENABLED
//#define VA_OIDN_INTEGRATION_ENABLED
#define VA_ZLIB_INTEGRATION_ENABLED
#define VA_USE_PIX3
// #define VA_USE_PIX3_HIGH_FREQUENCY_CPU_TIMERS
#define VA_SCOPE_TRACE_ENABLED

