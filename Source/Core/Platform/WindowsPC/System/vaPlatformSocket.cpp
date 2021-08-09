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

#include "Core/System/vaSocket.h"
#include "Core/vaSingleton.h"


#pragma comment( lib, "Ws2_32.lib" )


namespace Vanilla
{ 
   class vaNetworkManagerWin32 : public vaSingletonBase<vaNetworkManagerWin32>
   {

   public:
      vaNetworkManagerWin32() { }

      virtual ~vaNetworkManagerWin32()                               { }

//      virtual void OnInitialize( InitializationPass pass )
//      {
//         if( pass != IP_Regular ) return;
//
//          WORD wVersionRequested;
//          WSADATA wsaData;
//          int err;
//
//         // Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h 
//          wVersionRequested = MAKEWORD(2, 2);
//
//          err = WSAStartup(wVersionRequested, &wsaData);
//
//
//          VA_ASSERT( err == 0, L"WSAStartup failed with error: %d", err );
//
//          // Confirm that the WinSock DLL supports 2.2.
//          // Note that if the DLL supports versions greater    
//          // than 2.2 in addition to 2.2, it will still return 
//          // 2.2 in wVersion since that is the version we      
//          // requested.                                        
//
//          VA_ASSERT( !(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2), L"Could not find a usable version of Winsock.dll", err );
//      }
//
//      virtual void OnDeinitialize( InitializationPass pass )
//      {
//         if( pass != IP_Regular ) return;
//
//         WSACleanup();
//      }

      virtual void OnTick( float deltaTime )
      { 
         deltaTime; 
      }

   };

   // void vaNetworkManagerWin32_CreateManager()
   // {
   //    new vaNetworkManagerWin32();
   // }

}


using namespace Vanilla;
//using namespace std;

/*

class vaSocketWin : public vaSocket
{
private:

public:
   vaSocketWin( );
   ~vaSocketWin( );
   bool Initialize( bool typeTCP );

protected:	
   vaSocketWin( SOCKET init );

   virtual bool Bind( uint16 port );
   virtual bool Listen( );
   virtual bool Connect( const vaSocketAddress& server );
   virtual void Close( );

   virtual vaSocket * Accept( );
      
   virtual int Receive( void *pBuffer, unsigned int nSize );
   virtual int ReceiveFrom( void *pBuffer, unsigned int nSize, vaSocketAddress& addr );

   virtual bool Send( void *pBuffer, unsigned int nSize );
   virtual bool SendTo( void *pBuffer, unsigned int Size, const vaSocketAddress& dest );

   virtual bool IsDataPending();
};
*/

vaSocket::vaSocket()
{
   m_socket          = NULL;
   m_created         = false;
   m_maxConnections  = 0;
}

vaSocket::~vaSocket()
{
   Close();
}

vaSocket * vaSocket::Create( const vaPlatformSocketType & init )
{
   vaSocket * ret = new vaSocket();

   ret->m_socket = init;
   ret->m_created = true;
   ret->m_maxConnections = 16;

   return ret;
}

vaSocket * vaSocket::Create( bool typeTCP )
{
   vaSocket * ret = new vaSocket();

   // Initialize
   {
      if( typeTCP )
      {
         ret->m_socket = socket(AF_INET,     // address family
            SOCK_STREAM,       // socket type
            IPPROTO_TCP);      // protocol
      }
      else
      {
         ret->m_socket = socket(AF_INET,     // address family
            SOCK_DGRAM,        // socket type
            IPPROTO_UDP);      // protocol
      }

      if (ret->m_socket == INVALID_SOCKET)
      {
         VA_ASSERT_ALWAYS( L"Unable to create socket" );
         delete ret;
         return NULL;
      }

      ret->m_created = true;

      if( typeTCP )
      {
         int nSize = 32 * 1024;
         int result;
         result = setsockopt( ret->m_socket, SOL_SOCKET, SO_SNDBUF, (char*)&nSize, 4 );
         assert( result != SOCKET_ERROR );
         result = setsockopt( ret->m_socket, SOL_SOCKET, SO_RCVBUF, (char*)&nSize, 4 );
         assert( result != SOCKET_ERROR );

         //unsigned long enable = 1;
         //ioctlsocket(m_socket,FIONBIO,	&enable);
      }
      else
      {
         bool bBroadcast = true;
         int result = setsockopt( ret->m_socket,SOL_SOCKET,SO_BROADCAST,(const char*)&bBroadcast,sizeof(bBroadcast) );
         result;
      }
   }
   
   return ret;
}

void vaSocket::Destroy( vaSocket * socket )
{
   delete socket;
}

bool vaSocket::Bind( uint16 port )
{
   VA_ASSERT( m_created, L"Socket not created\n" );

   SOCKADDR_IN saServer;		

   saServer.sin_family = AF_INET;
   saServer.sin_addr.s_addr = INADDR_ANY;	// let WinSock supply address
   saServer.sin_port = htons( port );

    int nRet = bind( m_socket,            // socket 
               (LPSOCKADDR)&saServer,     // our address
               sizeof(struct sockaddr) ); // size of address structure
   
   if( nRet == SOCKET_ERROR )
   {
      VA_ASSERT(m_created, L"Failed to Bind Socket\n");
      closesocket(m_socket);
      m_created = false;
      return false;
   }

   return true;
}


bool vaSocket::Listen()
{
   VA_ASSERT( m_created, L"Socket not created\n" );

   int nRet = listen( m_socket, m_maxConnections ); // number of connection request queue
   
   if( nRet == SOCKET_ERROR )
   {
      closesocket(m_socket);
      m_created = false;
      return false;
   }
   return true;
}
   

bool vaSocket::Connect( const vaSocketAddress& server )
{
   VA_ASSERT(m_created, L"Socket not created\n");
    
   // connect to the server
   int nRet = connect(m_socket, (LPSOCKADDR)&server, sizeof(struct sockaddr));
   
   if (nRet == SOCKET_ERROR)
   {
      int lasterr = WSAGetLastError ();
      lasterr;
      closesocket(m_socket);
      m_created = false;
      return false;
   }

    return true;
}

   
vaSocket* vaSocket::Accept( )
{
   VA_ASSERT(m_created, L"Socket not created\n");

    SOCKET remoteSocket;

   remoteSocket = accept(m_socket,	NULL, NULL); // accept on listening socket
   
   if (remoteSocket == INVALID_SOCKET)
   {
      closesocket(m_socket);
        m_created = false;
      return NULL;
   }
   else
   {
      int nSize = 32 * 1024;
      setsockopt(remoteSocket, SOL_SOCKET, SO_SNDBUF, (char*)&nSize, 4);
      setsockopt(remoteSocket, SOL_SOCKET, SO_RCVBUF, (char*)&nSize, 4);
      //unsigned long enable = 1;
      //ioctlsocket(m_socket,FIONBIO,	&enable);
   }

    return vaSocket::Create( remoteSocket );
}
   

void vaSocket::Close( )
{
   VA_ASSERT( m_created, L"Socket not created\n" );
   closesocket( m_socket );
   m_created = false;
}


int vaSocket::Receive( void *pBuffer, unsigned int nSize )
{
   VA_ASSERT( m_created, L"Socket not created\n" );
   return recv( m_socket, (char*)pBuffer, nSize, 0 );
}


int vaSocket::ReceiveFrom( void *pBuffer, unsigned int nSize, vaSocketAddress& addr )
{
   VA_ASSERT(m_created, L"Socket not created\n");
   int nAddrSize = sizeof(addr);
   return recvfrom(m_socket, (char*) pBuffer,nSize, 0, (LPSOCKADDR)&addr, &nAddrSize);
}

   
bool vaSocket::Send(void *pBuffer, unsigned int nSize)
{
   VA_ASSERT(m_created, L"Socket not created\n");

    int nRet = send(m_socket,(char *)pBuffer, nSize, 0);
    
   if (nRet == SOCKET_ERROR)
   {
        return false;
   }
    
   return true;
}
   

bool vaSocket::SendTo(void *pBuffer, unsigned int Size, const vaSocketAddress& dest)
{
   VA_ASSERT(m_created, L"Socket not created\n");

    int nRet = sendto(m_socket,(char *)pBuffer, Size, 0, ((LPSOCKADDR)&dest), sizeof(dest));
    
   if (nRet == SOCKET_ERROR)
        return false;

    return true;
}

   
bool vaSocket::IsDataPending()
{
   VA_ASSERT(m_created, L"Socket not created\n");

   fd_set rd_set;
   fd_set err_set;

   FD_ZERO(&rd_set);
   FD_ZERO(&err_set);
#pragma warning( suppress : 4127 )
   FD_SET(m_socket, &rd_set);
#pragma warning( suppress : 4127 )
   FD_SET(m_socket, &err_set);
   
   struct timeval time;
   time.tv_sec = 0;
   time.tv_usec = 0;

   int iRet = select(0, &rd_set,NULL, &err_set, &time);
   iRet;

   //if there is error
   if( FD_ISSET(m_socket, &err_set) )
   {
      VA_ASSERT_ALWAYS( L"socket error" );
   }

   //check if there is data pending
   if (FD_ISSET(m_socket, &rd_set))
   {
      return true;
   }	

   return false;
}
