///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaInputKeyboard.h"

#include "Core\vaProfiler.h"

using namespace Vanilla;

vaInputKeyboard::vaInputKeyboard( )
{
    ResetAll( );

    vaInputKeyboardBase::SetCurrent( this );
}

vaInputKeyboard::~vaInputKeyboard( )
{
    assert( vaInputKeyboardBase::GetCurrent( ) == this );
    vaInputKeyboardBase::SetCurrent( NULL );
}


void vaInputKeyboard::Tick( float deltaTime )
{
    VA_TRACE_CPU_SCOPE( vaInputKeyboard_Tick );
    deltaTime;
    // m_timeFromLastUpdate += deltaTime;
    // if( m_timeFromLastUpdate > m_updateMinFrequency )
    //     m_timeFromLastUpdate = vaMath::Clamp( m_timeFromLastUpdate-m_updateMinFrequency, 0.0f, m_updateMinFrequency );
    // else
    //     return;
#if 0 // old, CPU-intensive approach

    for( int i = 0; i < KK_MaxValue; i++ )
    {
       bool isDown = (GetAsyncKeyState(i) & 0x8000) != 0;

       g_KeyUps[i]    = g_Keys[i] && !isDown;
       g_KeyDowns[i]  = !g_Keys[i] && isDown;

       g_Keys[i]      = isDown;
    }
#else
    byte keyStates[256];
    if( !GetKeyboardState( keyStates ) )
    {
        // See GetLastErrorAsString.. in vaPlatformFileStream on how to format the message into text
        DWORD errorMessageID = ::GetLastError( );
        VA_WARN( "GetKeyboardState returns false, error: %x", errorMessageID );
    }

    for( int i = 0; i < KK_MaxValue; i++ )
    {
       bool isDown = (keyStates[i] & 0x80) != 0;

       g_KeyUps[i]    = g_Keys[i] && !isDown;
       g_KeyDowns[i]  = !g_Keys[i] && isDown;

       g_Keys[i]      = isDown;
    }
#endif
}

void vaInputKeyboard::ResetAll( )
{
   for( int i = 0; i < KK_MaxValue; i++ )
   {
      g_Keys[i]         = false;
      g_KeyUps[i]       = false;
      g_KeyDowns[i]	=    false;
   }
}