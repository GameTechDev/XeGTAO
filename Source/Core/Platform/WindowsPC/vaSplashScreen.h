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

namespace Vanilla
{
    // utility class to show the splash screen on load
    class vaSplashScreen : public vaSingletonBase<vaSplashScreen>
    {
    private:
        HBITMAP             m_hbitmap           = NULL;
        HWND                m_hwnd              = NULL;
        SIZE                m_hbitmapSize       = {0,0};
        static const int    c_fadeoutTickTotal  = 5;
        int                 m_fadeoutTickCurrent= c_fadeoutTickTotal;

    public:
        vaSplashScreen( );
        virtual ~vaSplashScreen();

        static bool FadeOut( bool immediateClose = false );
    };

}