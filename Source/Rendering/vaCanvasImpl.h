///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef VA_CANVAS_TYPE

#ifdef VA_CANVAS_IMPL_INCLUDED
#error This file can only be included from one cpp file
#endif

#define VA_CANVAS_IMPL_INCLUDED

// this file should go out completely in the future; calls like these made to API independent module (vaRenderDeviceContext) should be delegated to API dependent (for ex. vaRenderDeviceContextDX11) 
// using std::function callbacks instead for clarity

namespace Vanilla
{

}

#endif