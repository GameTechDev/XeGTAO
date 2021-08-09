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

#include "vaPlatformBase.h"


namespace Vanilla
{
   inline vaSystemTimer::vaSystemTimer()
   {
      m_platformData.StartTime.QuadPart      = 0;
      m_platformData.CurrentTime.QuadPart    = 0;
      m_platformData.CurrentDelta.QuadPart   = 0;

      // this should be static and initialized only once really!
      ::QueryPerformanceFrequency( &m_platformData.QPF_Frequency );

      m_isRunning = false;
   }

   inline vaSystemTimer::~vaSystemTimer()
   {
   }

   inline void vaSystemTimer::Start( )
   {
      assert( !m_isRunning );
      m_isRunning = true;

      QueryPerformanceCounter( &m_platformData.StartTime );
      m_platformData.CurrentTime = m_platformData.StartTime;
      m_platformData.CurrentDelta.QuadPart   = 0;
   }

   inline void vaSystemTimer::Stop( )
   {
      assert( m_isRunning );
      m_isRunning = false;

      m_platformData.StartTime.QuadPart      = 0;
      m_platformData.CurrentTime.QuadPart    = 0;
      m_platformData.CurrentDelta.QuadPart   = 0;
   }

   inline void vaSystemTimer::Tick( )
   {
      if( !m_isRunning )
         return;

      LARGE_INTEGER currentTime;
      QueryPerformanceCounter( &currentTime );
      
      m_platformData.CurrentDelta.QuadPart = currentTime.QuadPart - m_platformData.CurrentTime.QuadPart;
      m_platformData.CurrentTime = currentTime;

   }

   inline double vaSystemTimer::GetTimeFromStart( ) const
   {
      LARGE_INTEGER timeFromStart;
      timeFromStart.QuadPart = m_platformData.CurrentTime.QuadPart - m_platformData.StartTime.QuadPart;

      return timeFromStart.QuadPart / (double) m_platformData.QPF_Frequency.QuadPart;
   }

   inline double vaSystemTimer::GetDeltaTime( ) const
   {
      return m_platformData.CurrentDelta.QuadPart / (double) m_platformData.QPF_Frequency.QuadPart;
   }

   inline double vaSystemTimer::GetCurrentTimeDouble( ) const
   {
       LARGE_INTEGER currentTime;
       QueryPerformanceCounter( &currentTime );

       return currentTime.QuadPart / (double) m_platformData.QPF_Frequency.QuadPart;
   }

}