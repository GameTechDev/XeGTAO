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

#include "Core/vaCore.h"

#include "System/vaPlatformSocket.h"


namespace Vanilla
{

   struct vaSocketAddress
   {
      // ipv6 in the future?
       unsigned char m_Address[128];
   };

   class vaSocket
   {
   protected:
      vaPlatformSocketType    m_socket;
      bool                    m_created;
      uint32                  m_maxConnections;

   protected:
      vaSocket();
      ~vaSocket();
   
   public:

      static vaSocket *       Create( bool typeTCP = true );
      static vaSocket *       Create( const vaPlatformSocketType & init );
      static void             Destroy( vaSocket * socket );

   public:
      bool                    Bind( uint16 port );
      bool                    Listen();
      bool                    Connect( const vaSocketAddress & serverAddress );
      vaSocket*               Accept();
      void                    Close()                                                                    ;
      int                     Receive( void *pBuffer, unsigned int size)                                 ;
      int                     ReceiveFrom( void *pBuffer, unsigned int size, vaSocketAddress & addr )    ;
      bool                    Send( void *pBuffer, unsigned int size )                                   ;
      bool                    SendTo( void *pBuffer, unsigned int Size, const vaSocketAddress & dest )   ;
      bool                    IsDataPending()                                                            ;
   };

}


