///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Taken from AdVantage Terrain SDK and renamed to fit into Vanilla codebase
// old license:
////////////////////////////////////////////////////////////////////////
// AdVantage Terrain SDK, Copyright (C) 2004 - 2008 Filip Strugar.
// 
// Distributed as a FREE PUBLIC part of the SDK source code under the following rules:
// 
// * This code file can be used on 'as-is' basis. Author is not liable for any damages arising from its use.
// * The distribution of a modified version of the software is subject to the following restrictions:
//  1. The origin of this software must not be misrepresented; you must not claim that you wrote the original 
//     software. If you use this software in a product, an acknowledgment in the product documentation would be 
//     appreciated but is not required.
//  2. Altered source versions must be plainly marked as such and must not be misrepresented as being the original
//     software.
//  3. The license notice must not be removed from any source distributions.
////////////////////////////////////////////////////////////////////////


#pragma once

#include "Core/vaCoreIncludes.h"
#include "Core/Misc/vaResourceFormats.h"

#ifdef VA_LIBTIFF_INTEGRATION_ENABLED
#include "IntegratedExternals/vaLibTIFFIntegration.h"
#endif

#include <deque>

#define VA_LBF_THREADSAFE

#ifdef VA_LBF_THREADSAFE
    #define VA_LBF_THREADSAFE_LINE(x)       x
#else
    #define VA_LBF_THREADSAFE_LINE(x)       
#endif

namespace Vanilla
{

    /// <summary>
    /// LargetBitmapFile is a simple bitmap format that supports "unlimited" image sizes and where data is organized 
    /// into tiles to enable fast reads/writes of random image sub-regions.
    /// Access it also thread-safe with per-block granularity so different threads can read&write at the same time 
    /// (although if the operation covers multiple blocks, access order is not guaranteed)
    /// 
    /// Current file format version is 1 (specified in FormatVersion field): supports reading and writing 
    /// of versions 0, 1.
    /// </summary>
    class vaLargeBitmapFile
    {
    public:
        enum PixelFormat
        {
            FormatUnknown           = 0xFFFF,

            // Never change existing values when enabling new formats as they are probably used!
            Format16BitGrayScale    = 0,
            Format8BitGrayScale     = 1,
            Format24BitRGB          = 2,
            Format32BitRGBA         = 3,
            Format16BitA4R4G4B4     = 4,
            FormatGeneric8Bit       = 10,
            FormatGeneric16Bit      = 11,
            FormatGeneric32Bit      = 12,
            FormatGeneric64Bit      = 13,
            FormatGeneric128Bit     = 14,
        };


    public:
        static int                    GetPixelFormatBPP( PixelFormat pixelFormat );

        static const int              c_FormatVersion       = 1;
        static const int              c_MemoryLimit         = 32 * 1024 * 1024; // allow xMB of memory usage per instance (this could be upgraded to add a global memory limit as well - see s_TotalUsedMemory)
        static const int              c_UserHeaderSize      = 224;
        static const int              c_TotalHeaderSize     = 256;

    private:

        struct DataBlock
        {
#ifdef VA_LBF_THREADSAFE
            char *              pData;
#else
            char *              pData;
#endif
            unsigned short      Width;
            unsigned short      Height;
            bool                Modified;
            VA_LBF_THREADSAFE_LINE( std::shared_mutex   Mutex; )
        };

        struct DataBlockID
        {
            int                 Bx;
            int                 By;

            DataBlockID( int bx, int by ) { this->Bx = bx; this->By = by; }
        };

        static int                                  s_TotalUsedMemory;
        VA_LBF_THREADSAFE_LINE( static mutex        s_TotalUsedMemoryMutex; )

        int                                         m_UsedMemory;
        std::deque<DataBlockID>                     m_UsedBlocks;
        VA_LBF_THREADSAFE_LINE( mutable mutex       m_UsedMemoryMutex; )

        VA_LBF_THREADSAFE_LINE( mutex               m_fileAccessMutex; )
        FILE *                                      m_File;
        wstring                                     m_filePath;

        bool                                        m_ReadOnly;

        int                                         m_BlockDimBits;
        int                                         m_BlocksX;
        int                                         m_BlocksY;
        int                                         m_EdgeBlockWidth;
        int                                         m_EdgeBlockHeight;
        DataBlock **                                m_DataBlocks;
        DataBlock *                                 m_BigDataBlocksArray;

        PixelFormat                                 m_PixelFormat;
        int                                         m_Width;
        int                                         m_Height;
        int                                         m_BlockDim;
        int                                         m_BytesPerPixel;

        VA_LBF_THREADSAFE_LINE( mutable std::shared_mutex m_GlobalMutex; )

        std::atomic<int32>                          m_AsyncOpRunningCount;

    public:
#ifdef VA_LBF_THREADSAFE
        PixelFormat                                 GetPixelFormat( ) const     { std::shared_lock<std::shared_mutex> lock( m_GlobalMutex ); return m_PixelFormat; }
        int                                         GetBytesPerPixel( ) const   { std::shared_lock<std::shared_mutex> lock( m_GlobalMutex ); return m_BytesPerPixel; }
        int                                         GetWidth( ) const           { std::shared_lock<std::shared_mutex> lock( m_GlobalMutex ); return m_Width; }
        int                                         GetHeight( ) const          { std::shared_lock<std::shared_mutex> lock( m_GlobalMutex ); return m_Height; }
        const wstring &                             GetFilePath( ) const        { std::shared_lock<std::shared_mutex> lock( m_GlobalMutex ); return m_filePath; }
        bool                                        IsOpen( ) const             { std::shared_lock<std::shared_mutex> lock( m_GlobalMutex ); return m_File != 0; }
#else
        PixelFormat                                 GetPixelFormat( ) const     { return m_PixelFormat;     }
        int                                         GetBytesPerPixel( ) const   { return m_BytesPerPixel;   }
        int                                         GetWidth( ) const           { return m_Width;           }
        int                                         GetHeight( ) const          { return m_Height;          }
        const wstring &                             GetFilePath( ) const        { return m_filePath;        }
        bool                                        IsOpen( ) const             { return m_File != 0;       }
#endif

    protected:
        vaLargeBitmapFile( FILE * file, const wstring & filePath, PixelFormat pixelFormat, int width, int height, int blockDim, bool readOnly );

    public:
        ~vaLargeBitmapFile( );

    public:
        static shared_ptr<vaLargeBitmapFile>        Create( const wstring & filePath, PixelFormat pixelFormat, int width, int height );
        static shared_ptr<vaLargeBitmapFile>        Open( const wstring & filePath, bool readOnly );

        static vaLargeBitmapFile::PixelFormat       GetMatchingPixelFormat( vaResourceFormat format );

        void                                        Close( );

    private:
        void                                        ReleaseBlock( int bx, int by );
        void                                        LoadBlock( int bx, int by, bool skipFileRead = false );
        void                                        SaveBlock( int bx, int by );
        int64                                       GetBlockStartPos( int bx, int by );

    public:
        void                                        GetPixel( int x, int y, void* pPixel );
        void                                        SetPixel( int x, int y, void* pPixel );

        // clamped XY
        template< typename T >
        T                                           GetPixelSafe( int x, int y );

        bool                                        ReadRect( void * dstBuffer, int dstPitchInBytes, int64 dstSizeInBytes, int rectPosX, int rectPosY, int rectSizeX, int rectSizeY
#ifdef VA_ENKITS_INTEGRATION_ENABLED
            , vaEnkiTS * threadScheduler = nullptr, shared_ptr<enki::ITaskSet> * outPtrTaskSetToWaitOn = nullptr 
#endif
        );
        bool                                        WriteRect( void * srcBuffer, int srcPitchInBytes, int rectPosX, int rectPosY, int rectSizeX, int rectSizeY
#ifdef VA_ENKITS_INTEGRATION_ENABLED
            , vaEnkiTS * threadScheduler = nullptr, shared_ptr<enki::ITaskSet> * outPtrTaskSetToWaitOn = nullptr 
#endif
        );

        template< typename T >
        void                                        SetAllPixels( const T & value );

        bool                                        ReadRectClampBorders( void * _dstBuffer, int dstPitchInBytes, int64 dstSizeInBytes, int dstRectPosX, int dstRectPosY, int dstRectSizeX, int dstRectSizeY
#ifdef VA_ENKITS_INTEGRATION_ENABLED
            , vaEnkiTS * threadScheduler = nullptr 
#endif
        );

#ifdef VA_LIBTIFF_INTEGRATION_ENABLED
        bool                                        ExportToTiffFile( const wstring & outFilePath );
#endif

    };

    template< typename T >
    inline T vaLargeBitmapFile::GetPixelSafe( int x, int y )
    {
        if( sizeof( T ) != m_BytesPerPixel )
        {
            assert( false );       // type size must match - otherwise there will be issues
            return T();
        }

        T ret;
        x = vaMath::Clamp( x, 0, m_Width-1 );
        y = vaMath::Clamp( y, 0, m_Height-1 );
        GetPixel( x, y, &ret );
        return ret;
    }

    template< typename T >
    void vaLargeBitmapFile::SetAllPixels( const T & value )
    {
        VA_LBF_THREADSAFE_LINE( std::shared_lock<std::shared_mutex> lock( m_GlobalMutex ); )
        if( sizeof( T ) != m_BytesPerPixel )
        {
            assert( false );       // type size must match - otherwise there will be issues
            return;
        }

        T * oneBlock = new T[m_BlockDim * m_BlockDim];
        for( int i = 0; i < m_BlockDim * m_BlockDim; i++ )
            oneBlock[i] = value;

        for( int y = 0; y < m_BlocksY; y++ )
        {
            for( int x = 0; x < m_BlocksX; x++ )
            {
                DataBlock & db = m_DataBlocks[x][y];
                VA_LBF_THREADSAFE_LINE( std::unique_lock<std::shared_mutex> uniqueBlockLock( db.Mutex ); ) 
                if( db.pData == 0 )
                    LoadBlock( x, y, true );

                int size = db.Width * db.Height * m_BytesPerPixel;
                assert( size <= (sizeof(T)*m_BlockDim * m_BlockDim) );
                memcpy( db.pData, oneBlock, size );

                db.Modified = true;
            }
        }

        delete[] oneBlock;
    }



}
