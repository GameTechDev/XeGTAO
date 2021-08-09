///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
#include "..\DXUT\Core\DXUT.h"

#include "DxRecOMatic.h"

#ifdef USE_REC_O_MATIC

#include "..\DxCanvas.h"

class CriticalSectionRAIIWrapper
{
   CRITICAL_SECTION *      CS;
   bool                    isIn;
public:

   CriticalSectionRAIIWrapper( CRITICAL_SECTION * criticalSection ) : CS(criticalSection), isIn(false) {}

   ~CriticalSectionRAIIWrapper()
   {
      if( isIn )
      {
         Leave();
      }
   }

   void Enter()
   {
      assert( !isIn );
      ::EnterCriticalSection( CS );
      isIn = true;
   }

   void Leave()
   {
      assert( isIn );
      ::LeaveCriticalSection( CS );
      isIn = false;
   }
};

DxRecOMatic::DxRecOMatic(void)
{
   isRecording = false;
   screenGrabSourceSurface = NULL;
   screenGrabLockableSurface = NULL;
   workerThreadHasJob = false;
   shutdownWorker = false;

   ::InitializeCriticalSection(&CS);

   workerThread = ::CreateThread( NULL, 0, ThreadProcProxy, this, CREATE_SUSPENDED, NULL );

   if( DxEventNotifier::GetD3DDevice() != NULL )
   {
      OnCreateDevice();
      OnResetDevice( &DxEventNotifier::GetBackbufferSurfaceDesc() );
   }
}

DxRecOMatic::~DxRecOMatic(void)
{
   StopRecording();

   SAFE_RELEASE( screenGrabSourceSurface );
   SAFE_RELEASE( screenGrabLockableSurface );

   ::ResumeThread(workerThread);

   CriticalSectionRAIIWrapper csw( &CS );
   csw.Enter();
   workerThreadHasJob = true;
   shutdownWorker = true;
   csw.Leave();

   ::WaitForSingleObject( workerThread, WAIT_TIMEOUT );

   ::DeleteCriticalSection(&CS);
}

DWORD WINAPI ThreadProcProxy( LPVOID lpThreadParameter )
{
   DxRecOMatic * pThis = (DxRecOMatic *)lpThreadParameter;
   return pThis->WriterThreadProc();
}

static int GetBytesPerScanline(int nWidth, int nBitsPerPixel)
{
   return ( (nWidth * nBitsPerPixel + 31) & (~31) ) / 8;
}


bool DxRecOMatic::StartRecording( const char * path, int frameRate )
{
   assert( !isRecording );
   if( isRecording )
      return false;

   this->path = path;
   this->frameDeltaTime = 1.0f / (float)frameRate;

   this->isRecording = true;

   std::string dir, file, ext;
   vaSplitPath( path, dir, file, ext );
   this->fileNameBase = dir + file;

   const D3DSURFACE_DESC & bbSurfDesc = DxEventNotifier::GetBackbufferSurfaceDesc();

   HDC    hdcDesktop = GetDC( GetDesktopWindow() );
   captureDC = CreateCompatibleDC( hdcDesktop );
   ReleaseDC( GetDesktopWindow(), hdcDesktop );

   BITMAPINFO& bmpInfo = this->screenBitmapInfo;
   ZeroMemory(&bmpInfo,sizeof(BITMAPINFO));
   bmpInfo.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
   bmpInfo.bmiHeader.biBitCount=24;
   bmpInfo.bmiHeader.biCompression = BI_RGB;
   bmpInfo.bmiHeader.biWidth= bbSurfDesc.Width;
   bmpInfo.bmiHeader.biHeight= (int)bbSurfDesc.Height;
   bmpInfo.bmiHeader.biPlanes = 1;
   bmpInfo.bmiHeader.biClrUsed = 0;
   bmpInfo.bmiHeader.biClrImportant = 0;

   int bpsl = GetBytesPerScanline( bmpInfo.bmiHeader.biWidth, bmpInfo.bmiHeader.biBitCount);
   bmpInfo.bmiHeader.biSizeImage = bpsl * bmpInfo.bmiHeader.biHeight;

   screenBitmapScanLineSize = (bmpInfo.bmiHeader.biWidth * bmpInfo.bmiHeader.biBitCount + 31) / 32 * 4;

   this->screenBitmap = CreateDIBSection( captureDC, &bmpInfo, DIB_RGB_COLORS, (void**)&screenBitmapData, NULL, NULL );

   outFrameCounter = 0;

   ::ResumeThread(workerThread);

   return true;
}

void DxRecOMatic::StopRecording( )
{
   if( !isRecording )
      return;

   CriticalSectionRAIIWrapper csw( &CS );
   csw.Enter();

   // still processing previous frame? then wait.
   while( workerThreadHasJob )
   {
      csw.Leave();
      ::Sleep(10);
      csw.Enter();
   }

   this->isRecording = false;

   ::SuspendThread(workerThread);

   DeleteObject( screenBitmap );
   DeleteDC( captureDC );

}

void DxRecOMatic::GrabBackbuffer( )
{
   if( !isRecording )
      return;
   //HWND    hwndTemp = CreateWindow( L"STATIC", L"Hurra", WS_VISIBLE | SS_BITMAP, 0, 0, 400, 400, NULL, 0, GetModuleHandle( 0 ), 0 );
   //SendMessage( hwndTemp, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)ScreenGrab() );
   //StopRecording();

   CriticalSectionRAIIWrapper csw( &CS );
   csw.Enter();

   // still processing previous frame? then wait.
   while( workerThreadHasJob )
   {
      csw.Leave();
      ::Sleep(10);
      csw.Enter();
   }

   IDirect3DDevice9* pd3dDevice = DxEventNotifier::GetD3DDevice();

   IDirect3DSurface9* pRenderTargetSurface = NULL;
   if( FAILED(pd3dDevice->GetRenderTarget(0, &pRenderTargetSurface)) )
      { assert( false ); return; }

   if( FAILED(pd3dDevice->StretchRect(pRenderTargetSurface, NULL, screenGrabSourceSurface, NULL, D3DTEXF_NONE)) )
      { assert( false ); return; }
   SAFE_RELEASE( pRenderTargetSurface );

   if( FAILED(pd3dDevice->GetRenderTargetData(screenGrabSourceSurface, screenGrabLockableSurface)) )
      { assert( false ); return; }

   workerThreadHasJob = true;
}

HRESULT DxRecOMatic::OnCreateDevice()
{ 
   return S_OK; 
}

HRESULT DxRecOMatic::OnResetDevice(const D3DSURFACE_DESC* pBackBufferSurfaceDesc)
{
   if( FAILED(GetD3DDevice()->CreateRenderTarget(pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height,
      D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, false, &screenGrabSourceSurface, NULL)) )
      return E_UNEXPECTED;

   if( FAILED(GetD3DDevice()->CreateOffscreenPlainSurface(pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height,
      D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &screenGrabLockableSurface, NULL)) )
      return E_UNEXPECTED;

   return S_OK; 
}

void DxRecOMatic::OnLostDevice()
{
   StopRecording();

   SAFE_RELEASE( screenGrabSourceSurface );
   SAFE_RELEASE( screenGrabLockableSurface );
}

void DxRecOMatic::OnDestroyDevice()
{
   StopRecording();
}

static bool DIBSectionSave( const char * fileName, BITMAPINFOHEADER * lpbi, void * dibBits )
{
   BITMAPFILEHEADER   hdr;

   if( !lpbi || !fileName )
      return FALSE;

   FILE * file = fopen( fileName, "wb" );
   if( file == NULL )
      return false;

   DWORD dwBitmapInfoSize = sizeof(BITMAPINFO) + (0)*sizeof(RGBQUAD);
   DWORD dwFileHeaderSize = dwBitmapInfoSize + sizeof(hdr);

   // Fill in the fields of the file header 
   hdr.bfType       = ((WORD) ('M' << 8) | 'B');
   hdr.bfSize       = dwFileHeaderSize + lpbi->biSizeImage;
   hdr.bfReserved1  = 0;
   hdr.bfReserved2  = 0;
   hdr.bfOffBits    = dwFileHeaderSize;

   // Write the file header 
   fwrite( &hdr, 1, sizeof(hdr), file );

   // Write the DIB header
   fwrite( lpbi, 1, dwBitmapInfoSize, file );

   // Write DIB bits
   fwrite( dibBits, 1, lpbi->biSizeImage, file );

   fclose( file );

   return TRUE;
}

unsigned int DxRecOMatic::WriterThreadProc()
{

#pragma warning (disable : 4127)
   while( true )
   {
      CriticalSectionRAIIWrapper csw( &CS );
      csw.Enter();

      // still processing previous frame? then wait.
      while( !workerThreadHasJob )
      {
         csw.Leave();
         ::Sleep(10);
         csw.Enter();
      }

      if( shutdownWorker )
      {
         return 0;
      }

      const D3DSURFACE_DESC & bbSurfDesc = GetBackbufferSurfaceDesc();

      //Create a lock on the DestinationTargetSurface
      D3DLOCKED_RECT lockedRC;
      if( FAILED( screenGrabLockableSurface->LockRect( &lockedRC, NULL, D3DLOCK_NO_DIRTY_UPDATE|D3DLOCK_NOSYSLOCK|D3DLOCK_READONLY ) ) )
      { assert( false ); return 0; }

      LPBYTE lpSource = reinterpret_cast<LPBYTE>(lockedRC.pBits);

      LPBYTE lpDestination = (LPBYTE)screenBitmapData;
      LPBYTE lpDestinationTemp = lpDestination;

      // Copy the data
      lpDestinationTemp += (bbSurfDesc.Height-1) * screenBitmapScanLineSize;
      for ( unsigned int iY = 0; iY < bbSurfDesc.Height; ++iY )
      {
         for ( unsigned int iX = 0; iX < bbSurfDesc.Width; ++iX )
         {
            UINT pixel = ((UINT*)lpSource)[iX];
            lpDestinationTemp[iX*3 + 0] = (BYTE)((pixel >> 0) & 0xFF);
            lpDestinationTemp[iX*3 + 1] = (BYTE)((pixel >> 8) & 0xFF);
            lpDestinationTemp[iX*3 + 2] = (BYTE)((pixel >> 16) & 0xFF);
         }

         lpSource += lockedRC.Pitch;
         lpDestinationTemp -= screenBitmapScanLineSize;
      }


      //Unlock the rectangle
      if( FAILED(screenGrabLockableSurface->UnlockRect()) )
      { assert( false ); return 0; }

      std::string fileName = vaStringFormat( "%s_%06d.bmp", this->fileNameBase.c_str(), this->outFrameCounter );
      this->outFrameCounter++;
      DIBSectionSave( fileName.c_str(), &screenBitmapInfo.bmiHeader, screenBitmapData );

      // We're done, signal that and leave critical section
      workerThreadHasJob = false;
      csw.Leave();
   }

   return 0;
}

void DxRecOMatic::Render( )
{
   const D3DSURFACE_DESC & bbSurfDesc = GetBackbufferSurfaceDesc();
   GetCanvas2D()->DrawString( bbSurfDesc.Width - 100, bbSurfDesc.Height - 32, 0xFFFF0000, L"Recording..." );
}

#endif // USE_REC_O_MATIC

*/