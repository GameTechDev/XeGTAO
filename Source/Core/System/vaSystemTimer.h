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

#include "System\vaPlatformSystemTimer.h"


namespace Vanilla
{

   class vaSystemTimer
   {
   public:

   protected:
      vaPlatformSystemTimerData              m_platformData;
      bool                                   m_isRunning;

   public:
      vaSystemTimer();
      ~vaSystemTimer();

   public:

      void                                   Start( );
      void                                   Stop( );
      bool                                   IsRunning( ) const         { return m_isRunning; }

      void                                   Tick( );

      // time elapsed (in seconds) from Start( ) to the last Tick( )
      double                                 GetTimeFromStart( ) const;

      // time elapsed (in seconds) between previous two Tick()s
      double                                 GetDeltaTime( ) const;

      double                                 GetCurrentTimeDouble( ) const;
   };
}

// Platform-specific implementation
#include "System\vaPlatformSystemTimer.inl"