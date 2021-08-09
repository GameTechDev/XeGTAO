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

// this could go out - just saving .png-s and using ffmpeg or something works fine
#if 0

#include "..\Common.h"

#ifdef USE_REC_O_MATIC

#include "..\DxEventNotifier.h"

#include <string>

// this class is just horrible
class DxRecOMatic : DxEventReceiver
{
   bool                       isRecording;
   std::string                path;
   float                      frameDeltaTime;

   IDirect3DSurface9 *        screenGrabSourceSurface;
   IDirect3DSurface9 *        screenGrabLockableSurface;

   //Avi *                      avi;

   BITMAPINFO                 screenBitmapInfo;

   HBITMAP                    screenBitmap;
   void *                     screenBitmapData;
   int                        screenBitmapScanLineSize;
   HDC                        captureDC;

   CRITICAL_SECTION           CS;

   HANDLE                     workerThread;
   bool                       workerThreadHasJob;
   bool                       shutdownWorker;

   std::string                fileNameBase;
   int                        outFrameCounter;

public:
   DxRecOMatic(void);
   ~DxRecOMatic(void);

   bool                       StartRecording( const char * path, int frameRate = 25 );
   void                       StopRecording( );

   bool                       IsRecording( )                                                    { return isRecording; }
   float                      GetFrameDeltaTime( )                                              { return frameDeltaTime; }

   void                       GrabBackbuffer( );

   void                       Render( );

private:

   virtual HRESULT 				OnCreateDevice();
   virtual HRESULT            OnResetDevice(const D3DSURFACE_DESC* pBackBufferSurfaceDesc);
   virtual void               OnLostDevice();
   virtual void               OnDestroyDevice();

private:
   friend DWORD WINAPI        ThreadProcProxy( LPVOID lpThreadParameter );

   unsigned int               WriterThreadProc();
};

#endif //USE_REC_O_MATIC

#endif