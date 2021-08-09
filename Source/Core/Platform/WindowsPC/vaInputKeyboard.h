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

#include "Core/vaInput.h"

namespace Vanilla
{
    class vaInputKeyboard : public vaSingletonBase < vaInputKeyboard >, public vaInputKeyboardBase
    {
    public:
        static const int        c_InitPriority = 1;
        static const int        c_TickPriority = -1000;

    protected:
        bool                    g_Keys[KK_MaxValue];
        bool                    g_KeyUps[KK_MaxValue];
        bool                    g_KeyDowns[KK_MaxValue];

    private:
        friend class vaWindowsDirectX;
        friend class vaApplicationBase;
        friend class vaApplicationWin;
        vaInputKeyboard( );
        ~vaInputKeyboard( );

    protected:

    public:
        virtual bool            IsKeyDown( vaKeyboardKeys key )               { return g_Keys[key]; }
        virtual bool            IsKeyClicked( vaKeyboardKeys key )            { return g_KeyDowns[key]; }
        virtual bool            IsKeyReleased( vaKeyboardKeys key )           { return g_KeyUps[key]; }
        //

    protected:
        void                    Tick( float );
        void                    ResetAll( );

    };

}