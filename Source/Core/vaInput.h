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


namespace Vanilla
{
    enum vaKeyboardKeys
    {
        // Analogue to Windows Virtual Keys, Standard Set

        KK_LBUTTON = 0x01,
        KK_RBUTTON = 0x02,
        KK_CANCEL = 0x03,
        KK_MBUTTON = 0x04,    /* NOT contiguous with L & RBUTTON */
        KK_XBUTTON1 = 0x05,    /* NOT contiguous with L & RBUTTON */
        KK_XBUTTON2 = 0x06,    /* NOT contiguous with L & RBUTTON */

        KK_BACK = 0x08,
        KK_TAB = 0x09,

        // 0x0A - 0x0B : reserved

        KK_CLEAR = 0x0C,
        KK_RETURN = 0x0D,

        // Use VK_L* and VK_R* versions to distinguish between left and right
        KK_SHIFT = 0x10,
        KK_CONTROL = 0x11,
        KK_MENU = 0x12,
        KK_ALT = KK_MENU,
        KK_PAUSE = 0x13,
        KK_CAPITAL = 0x14,

        KK_KANA = 0x15,
        KK_HANGUL = 0x15,
        KK_JUNJA = 0x17,
        KK_FINAL = 0x18,
        KK_HANJA = 0x19,
        KK_KANJI = 0x19,

        KK_ESCAPE = 0x1B,

        KK_CONVERT = 0x1C,
        KK_NONCONVERT = 0x1D,
        KK_ACCEPT = 0x1E,
        KK_MODECHANGE = 0x1F,

        KK_SPACE = 0x20,
        KK_PRIOR = 0x21,
        KK_NEXT = 0x22,
        KK_END = 0x23,
        KK_HOME = 0x24,
        KK_LEFT = 0x25,
        KK_UP = 0x26,
        KK_RIGHT = 0x27,
        KK_DOWN = 0x28,
        KK_SELECT = 0x29,
        KK_PRINT = 0x2A,
        KK_EXECUTE = 0x2B,
        KK_SNAPSHOT = 0x2C,
        KK_INSERT = 0x2D,
        KK_DELETE = 0x2E,
        KK_HELP = 0x2F,

        // VK_0 - VK_9 are the same as ASCII '0' - '9' (0x30 - 0x39)
        // 0x40 : unassigned
        // VK_A - VK_Z are the same as ASCII 'A' - 'Z' (0x41 - 0x5A)

        KK_LWIN = 0x5B,
        KK_RWIN = 0x5C,
        KK_APPS = 0x5D,

        // 0x5E : reserved      

        KK_SLEEP = 0x5F,

        KK_NUMPAD0 = 0x60,
        KK_NUMPAD1 = 0x61,
        KK_NUMPAD2 = 0x62,
        KK_NUMPAD3 = 0x63,
        KK_NUMPAD4 = 0x64,
        KK_NUMPAD5 = 0x65,
        KK_NUMPAD6 = 0x66,
        KK_NUMPAD7 = 0x67,
        KK_NUMPAD8 = 0x68,
        KK_NUMPAD9 = 0x69,
        KK_MULTIPLY = 0x6A,
        KK_ADD = 0x6B,
        KK_SEPARATOR = 0x6C,
        KK_SUBTRACT = 0x6D,
        KK_DECIMAL = 0x6E,
        KK_DIVIDE = 0x6F,
        KK_F1 = 0x70,
        KK_F2 = 0x71,
        KK_F3 = 0x72,
        KK_F4 = 0x73,
        KK_F5 = 0x74,
        KK_F6 = 0x75,
        KK_F7 = 0x76,
        KK_F8 = 0x77,
        KK_F9 = 0x78,
        KK_F10 = 0x79,
        KK_F11 = 0x7A,
        KK_F12 = 0x7B,
        KK_F13 = 0x7C,
        KK_F14 = 0x7D,
        KK_F15 = 0x7E,
        KK_F16 = 0x7F,
        KK_F17 = 0x80,
        KK_F18 = 0x81,
        KK_F19 = 0x82,
        KK_F20 = 0x83,
        KK_F21 = 0x84,
        KK_F22 = 0x85,
        KK_F23 = 0x86,
        KK_F24 = 0x87,

        // 0x88 - 0x8F : unassigned

        KK_NUMLOCK = 0x90,
        KK_SCROLL = 0x91,

        // NEC PC-9800 kbd definitions
        KK_OEM_NEC_EQUAL = 0x92,   // '=' key on numpad

        // Fujitsu/OASYS kbd definitions
        KK_OEM_FJ_JISHO = 0x92,   // 'Dictionary' key
        KK_OEM_FJ_MASSHOU = 0x93,   // 'Unregister word' key
        KK_OEM_FJ_TOUROKU = 0x94,   // 'Register word' key
        KK_OEM_FJ_LOYA = 0x95,   // 'Left OYAYUBI' key
        KK_OEM_FJ_ROYA = 0x96,   // 'Right OYAYUBI' key

        // 0x97 - 0x9F : unassigned

        // Use VK_SHIFT, VK_CONTROL and VK_MENU if there's no need to distinguish between left and right

        KK_LSHIFT = 0xA0,
        KK_RSHIFT = 0xA1,
        KK_LCONTROL = 0xA2,
        KK_RCONTROL = 0xA3,
        KK_LMENU = 0xA4,
        KK_RMENU = 0xA5,

        KK_BROWSER_BACK = 0xA6,
        KK_BROWSER_FORWARD = 0xA7,
        KK_BROWSER_REFRESH = 0xA8,
        KK_BROWSER_STOP = 0xA9,
        KK_BROWSER_SEARCH = 0xAA,
        KK_BROWSER_FAVORITES = 0xAB,
        KK_BROWSER_HOME = 0xAC,

        KK_VOLUME_MUTE = 0xAD,
        KK_VOLUME_DOWN = 0xAE,
        KK_VOLUME_UP = 0xAF,
        KK_MEDIA_NEXT_TRACK = 0xB0,
        KK_MEDIA_PREV_TRACK = 0xB1,
        KK_MEDIA_STOP = 0xB2,
        KK_MEDIA_PLAY_PAUSE = 0xB3,
        KK_LAUNCH_MAIL = 0xB4,
        KK_LAUNCH_MEDIA_SELECT = 0xB5,
        KK_LAUNCH_APP1 = 0xB6,
        KK_LAUNCH_APP2 = 0xB7,

        // 0xB8 - 0xB9 : reserved

        KK_OEM_1 = 0xBA,   // ';:' for US
        KK_OEM_PLUS = 0xBB,   // '+' any country
        KK_OEM_COMMA = 0xBC,   // ',' any country
        KK_OEM_MINUS = 0xBD,   // '-' any country
        KK_OEM_PERIOD = 0xBE,   // '.' any country
        KK_OEM_2 = 0xBF,   // '/?' for US
        KK_OEM_3 = 0xC0,   // '`~' for US

        //0xC1 - 0xD7 : reserved
        // 0xD8 - 0xDA : unassigned

        KK_OEM_4 = 0xDB,  //  '[{' for US
        KK_OEM_5 = 0xDC,  //  '\|' for US
        KK_OEM_6 = 0xDD,  //  ']}' for US
        KK_OEM_7 = 0xDE,  //  ''"' for US
        KK_OEM_8 = 0xDF,

        // 0xE0 : reserved

        // Various extended or enhanced keyboards
        KK_OEM_AX = 0xE1,  //  'AX' key on Japanese AX kbd
        KK_OEM_102 = 0xE2,  //  "<>" or "\|" on RT 102-key kbd.
        KK_ICO_HELP = 0xE3,  //  Help key on ICO
        KK_ICO_00 = 0xE4,  //  00 key on ICO

        KK_PROCESSKEY = 0xE5,

        KK_ICO_CLEAR = 0xE6,

        KK_PACKET = 0xE7,

        // 0xE8 : unassigned

        // Nokia/Ericsson definitions

        KK_OEM_RESET = 0xE9,
        KK_OEM_JUMP = 0xEA,
        KK_OEM_PA1 = 0xEB,
        KK_OEM_PA2 = 0xEC,
        KK_OEM_PA3 = 0xED,
        KK_OEM_WSCTRL = 0xEE,
        KK_OEM_CUSEL = 0xEF,
        KK_OEM_ATTN = 0xF0,
        KK_OEM_FINISH = 0xF1,
        KK_OEM_COPY = 0xF2,
        KK_OEM_AUTO = 0xF3,
        KK_OEM_ENLW = 0xF4,
        KK_OEM_BACKTAB = 0xF5,

        KK_ATTN = 0xF6,
        KK_CRSEL = 0xF7,
        KK_EXSEL = 0xF8,
        KK_EREOF = 0xF9,
        KK_PLAY = 0xFA,
        KK_ZOOM = 0xFB,
        KK_NONAME = 0xFC,
        KK_PA1 = 0xFD,
        KK_OEM_CLEAR = 0xFE,


        KK_MaxValue = 0xFF
    };

    class vaInputKeyboardBase
    {
    public:
        vaInputKeyboardBase( )                                                          { }
        virtual ~vaInputKeyboardBase( )                                                 { }

        virtual bool                    IsKeyDown( vaKeyboardKeys key )                 = 0;
        virtual bool                    IsKeyClicked( vaKeyboardKeys key )              = 0;
        virtual bool                    IsKeyReleased( vaKeyboardKeys key )             = 0;

        // This is for checking modifier keys like CTRL in combination with others to avoid a situation where ctrl and some key 
        // were clicked and released in the same frame; in that case just IsKeyDown( KK_CONTROL ) would return false although
        // it was down at the same time with the other key.
        // So, for example, instead of 
        //      if( ...->IsKeyDown( KK_CONTROL ) && ...->IsKeyClicked( ( vaKeyboardKeys )'C' ) )
        // one should use
        //      if( ...->IsKeyDownOrClicked( KK_CONTROL ) && ...->IsKeyClicked( ( vaKeyboardKeys )'C' ) )
        virtual bool                    IsKeyDownOrClicked( vaKeyboardKeys key )        { return IsKeyDown( key ) || IsKeyClicked( key ); }

    public:
        static vaInputKeyboardBase *    GetCurrent( )                                   { return s_current; }

    protected:
        static vaInputKeyboardBase *    s_current;
        static void                     SetCurrent( vaInputKeyboardBase * current )     { s_current = current; }

    };

    enum vaMouseKeys
    {
        MK_Left = 0x01,
        MK_Right = 0x02,
        MK_Cancel = 0x03,
        MK_Middle = 0x04,
        MK_XButton1 = 0x05,
        MK_XButton2 = 0x06,

        MK_MaxValue = 0xFF
    };


    class vaInputMouseBase
    {
    protected:
        virtual vaVector2i          GetCursorPos( ) const = 0;

    public:
        vaInputMouseBase( )                                                             { }
        virtual ~vaInputMouseBase( )                                                    { }

        virtual bool                IsKeyDown( vaMouseKeys key ) const                  = 0;
        virtual bool                IsKeyClicked( vaMouseKeys key ) const               = 0;   
        virtual bool                IsKeyReleased( vaMouseKeys key ) const              = 0;   
        //
        virtual vaVector2i          GetCursorClientPos( ) const                         = 0;
        virtual vaVector2           GetCursorClientNormalizedPos( ) const               = 0;
        virtual vaVector2i          GetCursorDelta( ) const                             = 0;
        //
        virtual vaVector2i          GetCursorPosDirect( ) const                         = 0;     
        virtual vaVector2i          GetCursorClientPosDirect( ) const                   = 0;
        virtual vaVector2           GetCursorClientNormalizedPosDirect( ) const         = 0;
        //
        virtual float               GetWheelDelta( ) const                              = 0;
        //
        virtual float               TimeFromLastMove( ) const                           = 0;
        //
        virtual bool                IsCaptured( ) const                                 = 0;
        //
        virtual vaVector2           GetCursorClientPosf( ) const                        { return vaVector2(GetCursorClientPos())+vaVector2(0.5f, 0.5f); }

    public:
        static vaInputMouseBase *       GetCurrent( )                                   { return s_current; }

    protected:
        static vaInputMouseBase *       s_current;
        static void                     SetCurrent( vaInputMouseBase * current )        { s_current = current; }

    };

}