///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaLargeBitmapFile.h"

#include "Core/vaProfiler.h"

#include "Core/Misc/stack_container.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// for temporary compatibility
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace enki
{
    struct TaskSetPartition
    {
        uint32_t start;
        uint32_t end;

        TaskSetPartition( ) { }
        TaskSetPartition( uint32_t start, uint32_t end ) : start( start ), end( end ) { }
    };

    class ITaskSet
    {
    public:
        ITaskSet( )
            : m_SetSize( 1 )
            , m_MinRange( 1 )
            , m_RunningCount( 0 )
            , m_RangeToRun( 1 )
        {}

        ITaskSet( uint32_t setSize_ )
            : m_SetSize( setSize_ )
            , m_MinRange( 1 )
            , m_RunningCount( 0 )
            , m_RangeToRun( 1 )
        {}

        ITaskSet( uint32_t setSize_, uint32_t minRange_ )
            : m_SetSize( setSize_ )
            , m_MinRange( minRange_ )
            , m_RunningCount( 0 )
            , m_RangeToRun( minRange_ )
        {}

        // Execute range should be overloaded to process tasks. It will be called with a
        // range_ where range.start >= 0; range.start < range.end; and range.end < m_SetSize;
        // The range values should be mapped so that linearly processing them in order is cache friendly
        // i.e. neighbouring values should be close together.
        // threadnum should not be used for changing processing of data, it's intended purpose
        // is to allow per-thread data buckets for output.
        virtual void            ExecuteRange( TaskSetPartition range, uint32_t threadnum ) = 0;

        // Size of set - usually the number of data items to be processed, see ExecuteRange. Defaults to 1
        uint32_t                m_SetSize;

        // Minimum size of of TaskSetPartition range when splitting a task set into partitions.
        // This should be set to a value which results in computation effort of at least 10k
        // clock cycles to minimize tast scheduler overhead.
        // NOTE: The last partition will be smaller than m_MinRange if m_SetSize is not a multiple
        // of m_MinRange.
        // Also known as grain size in literature.
        uint32_t                m_MinRange;

        bool                    GetIsComplete( ) const
        {
            return 0 == m_RunningCount.load( std::memory_order_acquire );
        }
    private:
        friend class           TaskScheduler;
        std::atomic<int32_t>   m_RunningCount;
        uint32_t               m_RangeToRun;
    };
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Vanilla;

int vaLargeBitmapFile::s_TotalUsedMemory = 0;

VA_LBF_THREADSAFE_LINE( mutex vaLargeBitmapFile::s_TotalUsedMemoryMutex; )

#pragma warning ( suppress: 4505 ) // unreferenced local function has been removed
static bool CreateNewStorageFile( vaFileStream & fileStream, const wstring & filePath, int64 size )
{
    if( fileStream.IsOpen( ) )
    {
        VA_LOG( "vaLargeBitmapFile::CreateNewStorageFile failed, fileStream already open" );
        return false;
    }
    if( !fileStream.Open( filePath, FileCreationMode::Create, FileAccessMode::ReadWrite, FileShareMode::None ) )
    {
        VA_LOG( "vaLargeBitmapFile::CreateNewStorageFile failed, file creation filed" );
        return false;
    }
    assert( fileStream.GetPosition() == 0 );
    int64 remainingSize = size;
    byte allZeroes[ 1024 * 32 ];
    memset( allZeroes, 0, sizeof(allZeroes) );
    while( remainingSize > 0 )
    {
        if( !fileStream.Write( allZeroes, vaMath::Min( remainingSize, (int64)sizeof( allZeroes ) ) ) )
        {
            VA_LOG( "vaLargeBitmapFile::CreateNewStorageFile failed, error initializing storage with zeroes" );
            fileStream.Close();
            return false;
        }
        remainingSize -= sizeof(allZeroes);
    }
    return true;
}

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

#include <stdlib.h>
#include <malloc.h>
#include <assert.h>

#pragma warning( disable : 4996 )

static void WriteInt32( FILE * file, int val )
{
    int count = (int)fwrite( &val, 1, sizeof(val), file );
    assert( count == sizeof(val) );
    count; // to suppress compiler warning
    //for( int i = 0; i < 4; i++ )
    //{
    //   stream.WriteByte( (byte)( val & 0xFF ) );
    //   val = val >> 8;
    //}
}

static int ReadInt32( FILE * file )
{
    int ret;
    int count = (int)fread( &ret, 1, sizeof(ret), file );
    assert( count == sizeof(ret) );
    count; // to suppress compiler warning
    //for( int i = 0; i < 4; i++ )
    //   ret |= stream.ReadByte() << ( 8 * i );
    return ret;
}


int vaLargeBitmapFile::GetPixelFormatBPP( vaLargeBitmapFile::PixelFormat  pixelFormat )
{
    switch( pixelFormat )
    {
        case ( Format8BitGrayScale ):       return 1;
        case ( Format16BitGrayScale ):      return 2;
        case ( Format24BitRGB ):            return 3;
        case ( Format32BitRGBA ):           return 4;
        case ( Format16BitA4R4G4B4 ):       return 2;
        case ( FormatGeneric8Bit   ):       return 1;
        case ( FormatGeneric16Bit  ):       return 2;
        case ( FormatGeneric32Bit  ):       return 4;
        case ( FormatGeneric64Bit  ):       return 8;
        case ( FormatGeneric128Bit ):       return 16;
        default: return -1;
    }
}


vaLargeBitmapFile::vaLargeBitmapFile( FILE * file, const wstring & filePath, vaLargeBitmapFile::PixelFormat  pixelFormat, int width, int height, int blockDim, bool readOnly )
{
    VA_LBF_THREADSAFE_LINE( std::unique_lock<std::shared_mutex> lock( m_GlobalMutex ); )
    m_filePath          = filePath;
    m_File              = file;
    m_PixelFormat       = pixelFormat;
    m_Width             = width;
    m_Height            = height;
    m_BlockDim          = blockDim;
    m_ReadOnly          = readOnly;
    m_BytesPerPixel     = vaLargeBitmapFile::GetPixelFormatBPP( pixelFormat );
    m_UsedMemory        = 0;

    m_BlocksX           = ( width - 1 ) / blockDim + 1;
    m_BlocksY           = ( height - 1 ) / blockDim + 1;
    m_EdgeBlockWidth    = width - ( m_BlocksX - 1 ) * blockDim;
    m_EdgeBlockHeight   = height - ( m_BlocksY - 1 ) * blockDim;

    if( ( ( blockDim - 1 ) & blockDim ) != 0 ) 
    {
        assert( false ); // "blockDim must be power of 2"
        VA_ERROR( "blockDim must be power of 2" );
    }

    m_BlockDimBits = 0; int a = blockDim;
    while( a > 1 ) { m_BlockDimBits++; a /= 2; }

    // this storage is a bit weird, but that's the way I built it initially so there it is
    m_BigDataBlocksArray = new DataBlock[m_BlocksX*m_BlocksY];
    m_DataBlocks = new DataBlock*[m_BlocksX];
    for( int x = 0; x < m_BlocksX; x++ )
    {
        m_DataBlocks[x] = &m_BigDataBlocksArray[m_BlocksY * x];
        for( int y = 0; y < m_BlocksY; y++ )
        {
        m_DataBlocks[x][y].pData = 0;
        m_DataBlocks[x][y].Width = (unsigned short)( ( x == ( m_BlocksX - 1 ) ) ? ( m_EdgeBlockWidth ) : ( blockDim ) );
        m_DataBlocks[x][y].Height = (unsigned short)( ( y == ( m_BlocksY - 1 ) ) ? ( m_EdgeBlockHeight ) : ( blockDim ) );
        m_DataBlocks[x][y].Modified = false;
        }
    }
    // tempBuffer = new byte[BytesPerPixel * BlockDim * BlockDim];

    VA_LBF_THREADSAFE_LINE( std::unique_lock<mutex> fileAccessMutex( m_fileAccessMutex ); )
    _fseeki64( m_File, c_TotalHeaderSize, SEEK_SET );

    m_AsyncOpRunningCount = 0;

}

vaLargeBitmapFile::~vaLargeBitmapFile()
{
    Close();
}

shared_ptr<vaLargeBitmapFile> vaLargeBitmapFile::Create( const wstring & filePath, vaLargeBitmapFile::PixelFormat  pixelFormat, int width, int height )
{
    int bytesPerPixel = GetPixelFormatBPP( pixelFormat );
    if( bytesPerPixel < 0 || bytesPerPixel > 8 ) 
    {
        assert( false ); // "bytesPerPixel < 0 || bytesPerPixel > 4" // could work for bytesPerPixel > 4, but is not tested
        VA_ERROR( "vaLargeBitmapFile - bytesPerPixel < 0 || bytesPerPixel > 4 - could work for bytesPerPixel > 4, but is not tested" );
        return nullptr;
    }

    FILE * file = _wfopen( filePath.c_str(), L"w+b" ); // File.Open( path, FileMode.Create, FileAccess.ReadWrite, FileShare.None );
    if( file == 0 )
    {
        VA_LOG( "vaLargeBitmapFile::Create failed, error creating file" );
        return nullptr;
    }

    // pre-allocate file and initialize to zero
    int64 fileSize = (int64)bytesPerPixel * width * height + c_TotalHeaderSize;
    int64 remainingSize = fileSize;
    byte allZeroes[ 1024 * 32 ];
    memset( allZeroes, 0, sizeof(allZeroes) );
    while( remainingSize > 0 )
    {
        int toWrite = vaMath::Min( (int)remainingSize, (int)sizeof( allZeroes ) );
        int count = (int)fwrite( &allZeroes, 1, toWrite, file );
        if( count != toWrite )
        {
            VA_LOG( "vaLargeBitmapFile::CreateNewStorageFile failed, error initializing file" );
            fclose( file );
            return nullptr;
        }
        remainingSize -= sizeof(allZeroes);
    }

    _fseeki64( file, fileSize-1, SEEK_SET );
    char dummy = 0;
    fwrite( &dummy, 1, 1, file );

    _fseeki64( file, 0, SEEK_SET );

    WriteInt32( file, (int)pixelFormat );
    WriteInt32( file, width );
    WriteInt32( file, height );

    int blockDim = 256;

    int version = c_FormatVersion;
    WriteInt32( file, version );
    WriteInt32( file, blockDim );

    int64 pos = _ftelli64(file);
    for( ; pos < c_TotalHeaderSize; pos++ )
        fwrite( &dummy, 1, 1, file );

    return shared_ptr<vaLargeBitmapFile>( new vaLargeBitmapFile( file, filePath, pixelFormat, width, height, blockDim, false ) );
}

shared_ptr<vaLargeBitmapFile> vaLargeBitmapFile::Open( const wstring & filePath, bool readOnly )
{
    FILE * file;
    if( readOnly )
        file = _wfopen(filePath.c_str(), L"rb"); // File.Open( path, FileMode.Open, FileAccess.Read, FileShare.Read );
    else
        file = _wfopen(filePath.c_str(), L"r+b"); // File.Open( path, FileMode.Open, FileAccess.ReadWrite, FileShare.None );

    if( file == 0 )
    {
        VA_LOG( "vaLargeBitmapFile::Open failed, error opening file" );
        return nullptr;
    }

    vaLargeBitmapFile::PixelFormat  pixelFormat = (vaLargeBitmapFile::PixelFormat )ReadInt32( file );
    int bytesPerPixel =  GetPixelFormatBPP( pixelFormat );
    int width         = ReadInt32( file );
    int height        = ReadInt32( file );
    int version       = ReadInt32( file );

    int blockDim = 128;
    if( version > 0 )
        blockDim = ReadInt32( file );

    int64 current = _ftelli64(file);
    _fseeki64(file, 0, SEEK_END);
    int64 filelength = _ftelli64(file);
    _fseeki64(file, current, SEEK_SET);
      
    if( ( (int64)bytesPerPixel * width * height + c_TotalHeaderSize ) != filelength )
    {
        assert( false ); // file is probably corrupt
    }

    return shared_ptr<vaLargeBitmapFile>( new vaLargeBitmapFile( file, filePath, pixelFormat, width, height, blockDim, readOnly ) );
}

void vaLargeBitmapFile::Close()
{
    assert( m_AsyncOpRunningCount.load() == 0 );  // if this fires, there's still async ops on this object - you have to wait for them all to stop before this can be done

    VA_LBF_THREADSAFE_LINE( std::unique_lock<std::shared_mutex> lock( m_GlobalMutex ); )

    if( m_File == 0 ) 
    {
        assert( m_UsedMemory == 0 );
        assert( m_DataBlocks == nullptr );
        assert( m_BigDataBlocksArray == nullptr );
        return;
    }

// #define TRACK_BLOCKS
#ifdef TRACK_BLOCKS
    int dataBlocksTotal     = 0;
    int usedMemoryBefore    = 0;
    int releasedBlocks      = 0;
    {
        VA_LBF_THREADSAFE_LINE( std::unique_lock<mutex> usedMemoryMutexLock( m_UsedMemoryMutex ); )
        dataBlocksTotal = m_BlocksX * m_BlocksY;
        usedMemoryBefore = m_UsedMemory;
    }
#endif

    if( m_DataBlocks )
    {
        for( int x = 0; x < m_BlocksX; x++ )
        {
            for( int y = 0; y < m_BlocksY; y++ )
            { 
                DataBlock & db = m_DataBlocks[x][y];
                VA_LBF_THREADSAFE_LINE( std::unique_lock<std::shared_mutex> uniqueBlockLock( db.Mutex ); )
                if( m_DataBlocks[x][y].pData != 0 ) 
                {
#ifdef TRACK_BLOCKS
                    releasedBlocks++;
#endif
                    ReleaseBlock( x, y ); 
                    {
                        int blockSize = db.Width * db.Height * m_BytesPerPixel;
                        VA_LBF_THREADSAFE_LINE( std::unique_lock<mutex> usedMemoryMutexLock( m_UsedMemoryMutex ); )
                        m_UsedMemory -= blockSize;

                        {
                            VA_LBF_THREADSAFE_LINE( std::unique_lock<mutex> totalUsedMemoryMutexLock( s_TotalUsedMemoryMutex ); )
                            s_TotalUsedMemory -= blockSize;
                        }
                    }
                }
            }
        }
        delete[] m_DataBlocks;
        delete[] m_BigDataBlocksArray;
        m_DataBlocks = nullptr;
        m_BigDataBlocksArray = nullptr;
    }

    VA_LBF_THREADSAFE_LINE( std::unique_lock<mutex> fileAccessMutex( m_fileAccessMutex ); )
    fclose( m_File );
    m_File = 0;
    {
        VA_LBF_THREADSAFE_LINE( std::unique_lock<mutex> usedMemoryMutexLock( m_UsedMemoryMutex ); )
        assert( m_UsedMemory == 0 );
        m_UsedBlocks.clear();
    }
    assert( m_DataBlocks == nullptr );
    assert( m_BigDataBlocksArray == nullptr );
}

void vaLargeBitmapFile::ReleaseBlock( int bx, int by )
{
    DataBlock & db = m_DataBlocks[bx][by];
    if( db.pData == 0 ) 
    {
        assert( false ); // "block not loaded"
        VA_ERROR( "block not loaded" );
    }

    if( db.Modified ) 
        SaveBlock( bx, by );

    free( db.pData );
    db.Modified = false;
    db.pData = 0;
}

// #include <sstream>

void vaLargeBitmapFile::LoadBlock( int bx, int by, bool skipFileRead )
{
    DataBlock & db = m_DataBlocks[bx][by];
    if( db.pData != 0 ) 
    {
        assert( false ); // "Block already loaded"
        VA_ERROR( "block already loaded" );
    }

    // std::stringstream ss;
    // ss << std::this_thread::get_id();
    // string threadID = ss.str();
    // VA_LOG( "Loading block %d, %d, this: %llx, thread: %s", bx, by, this, threadID.c_str() );

    //load
    {
        VA_LBF_THREADSAFE_LINE( std::unique_lock<mutex> usedMemoryMutexLock( m_UsedMemoryMutex ); )
        int tryCount = 0;
        while( (m_UsedMemory > c_MemoryLimit) && (m_UsedBlocks.size() > 0) )
        {
            DataBlockID dbid = (DataBlockID)m_UsedBlocks.back();
            if( tryCount > (int)m_UsedBlocks.size() )
            {
                // can't remove any? too small memory limit then, drop out
                assert( false );
                break;
            }
        
            if( (dbid.Bx == bx) && (dbid.By == by) )
            {
                // skip ourselves
                m_UsedBlocks.pop_back();
                m_UsedBlocks.push_front( dbid );
                tryCount++;
                continue;
            }
        
            DataBlock & blockToRelease = m_DataBlocks[dbid.Bx][dbid.By];
            #ifdef VA_LBF_THREADSAFE
            std::unique_lock<std::shared_mutex> uniqueBlockLock( blockToRelease.Mutex, std::defer_lock ); 
            if( !uniqueBlockLock.try_lock() )
            {
                // ok drop it for now and try another
                m_UsedBlocks.pop_back();
                m_UsedBlocks.push_front( dbid );
                tryCount++;
                continue;
            }
            #endif

            // VA_LOG( "Releasing block %d, %d, this: %llx, thread: %s", dbid.Bx, dbid.By, this, threadID.c_str() );
            ReleaseBlock( dbid.Bx, dbid.By );
            {
                int removedBlockSize = blockToRelease.Width * blockToRelease.Height * m_BytesPerPixel;
                m_UsedMemory -= removedBlockSize;

                {
                    VA_LBF_THREADSAFE_LINE( std::unique_lock<mutex> totalUsedMemoryMutexLock( s_TotalUsedMemoryMutex ); )
                    s_TotalUsedMemory -= removedBlockSize;
                }
            }
            m_UsedBlocks.pop_back();
        }
    }

    int blockSize = db.Width * db.Height * m_BytesPerPixel;

    assert( db.pData == nullptr );
    db.pData = (char*)malloc( blockSize );

    if( !skipFileRead )
    {
        VA_LBF_THREADSAFE_LINE( std::unique_lock<mutex> fileAccessMutex( m_fileAccessMutex ); )
      
        _fseeki64( m_File, GetBlockStartPos( bx, by ), SEEK_SET );
        if( (int)fread( db.pData, 1, blockSize, m_File ) != blockSize )
        {
            assert( false );
        }
    }
    db.Modified = false;
    // VA_LOG( "Block %d, %d loaded, this: %llx, thread: %s", bx, by, this, threadID.c_str() );

    {
        VA_LBF_THREADSAFE_LINE( std::unique_lock<mutex> usedMemoryMutexLock( m_UsedMemoryMutex ); )

        for( int i = 0; i < m_UsedBlocks.size(); i++ )
        {
            if( (m_UsedBlocks[i].Bx == bx) && (m_UsedBlocks[i].By == by) )
            {
                assert( false );
            }
        }

        m_UsedBlocks.push_front( DataBlockID( bx, by ) );
        m_UsedMemory += blockSize;
    
        {
            VA_LBF_THREADSAFE_LINE( std::unique_lock<mutex> totalUsedMemoryMutexLock( s_TotalUsedMemoryMutex ); )
            s_TotalUsedMemory += blockSize;
        }
    }
}

void vaLargeBitmapFile::SaveBlock( int bx, int by )
{
    DataBlock & db = m_DataBlocks[bx][by];
    if( db.pData == 0 )
    {
        assert( false ); // "block not loaded"
        VA_ERROR( "block not loaded" );
    }

    int blockSize = db.Width * db.Height * m_BytesPerPixel;
      
    VA_LBF_THREADSAFE_LINE( std::unique_lock<mutex> fileAccessMutex( m_fileAccessMutex ); )
    _fseeki64( m_File, GetBlockStartPos( bx, by ), SEEK_SET );
    fwrite( db.pData, 1, blockSize, m_File );
    db.Modified = false;
}

int64 vaLargeBitmapFile::GetBlockStartPos( int bx, int by )
{
    int64 pos = c_TotalHeaderSize;

    pos += (int64)by * ( m_BlocksX - 1 ) * ( (int64)m_BlockDim * m_BlockDim * m_BytesPerPixel );
    pos += (int64)by * ( (int64)m_BlockDim * m_EdgeBlockWidth * m_BytesPerPixel );

    if( by == ( m_BlocksY - 1 ) )
        pos += ( (int64)bx ) * ( (int64)m_BlockDim * m_EdgeBlockHeight * m_BytesPerPixel );
    else
        pos += ( (int64)bx ) * ( (int64)m_BlockDim * m_BlockDim * m_BytesPerPixel );

    return pos;
}

void vaLargeBitmapFile::GetPixel( int x, int y, void* pPixel )
{
#ifdef VA_LBF_THREADSAFE
    std::shared_lock<std::shared_mutex> lock( m_GlobalMutex ); 
#endif

    assert( x >= 0 && x <= m_Width && y >= 0 && y <= m_Height );
    int bx = x >> m_BlockDimBits;
    int by = y >> m_BlockDimBits;
    x -= bx << m_BlockDimBits;
    y -= by << m_BlockDimBits;
    DataBlock & db = m_DataBlocks[bx][by];
#ifdef VA_LBF_THREADSAFE
    std::shared_lock<std::shared_mutex> sharedBlockLock( db.Mutex ); 
    std::unique_lock<std::shared_mutex> uniqueBlockLock( db.Mutex, std::defer_lock ); 
#endif
    if( db.pData == 0 )
    {
#ifdef VA_LBF_THREADSAFE
        sharedBlockLock.unlock();
        uniqueBlockLock.lock();
#endif
        if( db.pData == 0 )
        {
            LoadBlock( bx, by );
        }
//        else
//        {
//            int dbg = 0;
//            dbg++;
//        }
    }
    memcpy( pPixel, db.pData + ( ( db.Width * y + x ) * m_BytesPerPixel ), m_BytesPerPixel );
}

void vaLargeBitmapFile::SetPixel( int x, int y, void* pPixel )
{
#ifdef VA_LBF_THREADSAFE
    std::shared_lock<std::shared_mutex> lock( m_GlobalMutex ); 
#endif

    assert( !m_ReadOnly );
    assert( x >= 0 && x <= m_Width && y >= 0 && y <= m_Height );

    int bx = x >> m_BlockDimBits;
    int by = y >> m_BlockDimBits;
    x -= bx << m_BlockDimBits;
    y -= by << m_BlockDimBits;
    DataBlock & db = m_DataBlocks[bx][by];
#ifdef VA_LBF_THREADSAFE
    std::unique_lock<std::shared_mutex> uniqueBlockLock( db.Mutex ); 
#endif
    if( db.pData == 0 )
    {
        LoadBlock( bx, by );
    }

    char* pTo = db.pData + ( ( db.Width * y + x ) * m_BytesPerPixel );
    char* pFrom = (char*)pPixel;
    for( int b = 0; b < m_BytesPerPixel; b++ )
    {
        *pTo = *pFrom;
        pTo++; pFrom++;
    }

    db.Modified = true;
}

namespace 
{
    struct BlockOp
    {
        uint16                 bx;
        uint16                 by;
    };

    typedef vaStackVector< BlockOp, 2048 >  BlockOpStackVector; 
}

bool vaLargeBitmapFile::ReadRect( void * dstBuffer, int dstPitchInBytes, int64 dstSizeInBytes, int rectPosX, int rectPosY, int rectSizeX, int rectSizeY
#ifdef VA_ENKITS_INTEGRATION_ENABLED
    , vaEnkiTS * threadScheduler, shared_ptr<enki::ITaskSet> * outPtrTaskSetToWaitOn 
#endif
)
{
#ifdef VA_LBF_THREADSAFE
    std::shared_lock<std::shared_mutex> lock( m_GlobalMutex ); 
#endif

    if( dstBuffer == nullptr )
    {
        assert( false );    // but why?
        return false;
    }

    if( dstPitchInBytes < ( rectSizeX * m_BytesPerPixel ) )
    {
        assert( false );    // destination stride looks too small
        return false;
    }

    if( dstSizeInBytes < ( (rectSizeY-1) * dstPitchInBytes + rectSizeX * m_BytesPerPixel ) )
    {
        assert( false );    // destination size looks too small
        return false;
    }

    if( ( rectPosX < 0 ) || ( ( rectPosX + rectSizeX ) > m_Width ) || ( rectPosY < 0 ) || ( ( rectPosY + rectSizeY ) > m_Height ) || ( rectSizeX < 0 ) || ( rectSizeY < 0 ) )
    {
        assert( false );    // invalid lock region (out of range)
        return false;
    }

    int blockXFrom = rectPosX / m_BlockDim;
    int blockYFrom = rectPosY / m_BlockDim;
    int blockXTo = ( rectPosX + rectSizeX - 1 ) / m_BlockDim;
    int blockYTo = ( rectPosY + rectSizeY - 1 ) / m_BlockDim;

    assert( blockXTo < m_BlocksX );
    assert( blockYTo < m_BlocksY );

#if 0
    for( int by = blockYFrom; by <= blockYTo; by++ )
    {
        for( int bx = blockXFrom; bx <= blockXTo; bx++ )
        {
            int bw = ( bx == ( m_BlocksX - 1 ) ) ? ( m_EdgeBlockWidth ) : ( m_BlockDim );
            int bh = ( by == ( m_BlocksY - 1 ) ) ? ( m_EdgeBlockHeight ) : ( m_BlockDim );

            DataBlock & db = m_DataBlocks[bx][by];
            VA_LBF_THREADSAFE_LINE( std::shared_lock<std::shared_mutex> sharedBlockLock( db.Mutex ); )
            VA_LBF_THREADSAFE_LINE( std::unique_lock<std::shared_mutex> uniqueBlockLock( db.Mutex, std::defer_lock ); )
            if( db.pData == 0 )
            {
                // upgrade the lock to unique so we can load from disk
                VA_LBF_THREADSAFE_LINE( sharedBlockLock.unlock(); )
                VA_LBF_THREADSAFE_LINE( uniqueBlockLock.lock(); )
                {
                    // could have been loaded by someone else in the meantime 
                    if( db.pData == 0 )
                    {
                        LoadBlock( bx, by );
                    }
                }
                // continue this block with unique lock!
            }
            int fromX = vaMath::Max( bx * m_BlockDim, rectPosX );
            int fromY = vaMath::Max( by * m_BlockDim, rectPosY );
            int toX = vaMath::Min( bx * m_BlockDim + bw, rectPosX + rectSizeX );
            int toY = vaMath::Min( by * m_BlockDim + bh, rectPosY + rectSizeY );

            for( int y = fromY; y < toY; y++ )
            {
                int lly = y - rectPosY;
                int llxf = fromX - rectPosX;
                int llxt = toX - rectPosX;
                void * dstTo = (char*)dstBuffer + (dstPitchInBytes * lly + llxf * m_BytesPerPixel);
                int bytesCount = (toX-fromX) * m_BytesPerPixel;

                int bly = y - ( by * m_BlockDim );
                int blxf = fromX - ( bx * m_BlockDim );

                memcpy( dstTo, &db.pData[( db.Width * bly + blxf ) * m_BytesPerPixel], bytesCount );
            }
        }
    }
#else

    struct BlockOpTaskSet : enki::ITaskSet
    {
        BlockOpStackVector                  blockOpVector;
        vaLargeBitmapFile &                 _this;
        const void *                        dstBuffer;
        const int                           dstPitchInBytes;
        const int                           rectPosX;
        const int                           rectPosY;
        const int                           rectSizeX;
        const int                           rectSizeY;

        BlockOpTaskSet( vaLargeBitmapFile & _this, void * dstBuffer, int dstPitchInBytes, int rectPosX, int rectPosY, int rectSizeX, int rectSizeY, int blockXFrom, int blockYFrom, int blockXTo, int blockYTo ) : 
            ITaskSet( (uint32_t) (blockXTo-blockXFrom+1) * (blockYTo-blockYFrom+1) ),
            _this(_this), dstBuffer( dstBuffer ), dstPitchInBytes( dstPitchInBytes ), rectPosX( rectPosX ), rectPosY( rectPosY ), rectSizeX( rectSizeX ), rectSizeY( rectSizeY ) 
        { 
            _this.m_AsyncOpRunningCount++;

            blockOpVector->resize( m_SetSize ); //, blockOpTemplate );
            int blockOpCounter = 0;

            for( int by = blockYFrom; by <= blockYTo; by++ )
            {
                for( int bx = blockXFrom; bx <= blockXTo; bx++ )
                {
                    BlockOp & blockOp = blockOpVector[blockOpCounter];
                    blockOp.bx = (uint16)bx;
                    blockOp.by = (uint16)by;
                    blockOpCounter++;
                }
            }

            assert( blockOpCounter == blockOpVector->size() );
        }
        virtual ~BlockOpTaskSet( )
        {
            _this.m_AsyncOpRunningCount--;
            assert( _this.m_AsyncOpRunningCount.load() >= 0 );  // if this fires, something is seriously wrong - fix it
        }

        virtual void            ExecuteRange( enki::TaskSetPartition range, uint32_t threadnum )
        {
            VA_TRACE_CPU_SCOPE( ReadRectBlock );

            threadnum; // unreferenced

            for( uint32 i = range.start; i < range.end; i++ )
            {
                BlockOp & blockOp = blockOpVector[i];
                int bx = blockOp.bx;
                int by = blockOp.by;
                int bw = ( bx == ( _this.m_BlocksX - 1 ) ) ? ( _this.m_EdgeBlockWidth ) : ( _this.m_BlockDim );
                int bh = ( by == ( _this.m_BlocksY - 1 ) ) ? ( _this.m_EdgeBlockHeight ) : ( _this.m_BlockDim );

                DataBlock & db = _this.m_DataBlocks[bx][by];
                VA_LBF_THREADSAFE_LINE( std::shared_lock<std::shared_mutex> sharedBlockLock( db.Mutex ); )
                VA_LBF_THREADSAFE_LINE( std::unique_lock<std::shared_mutex> uniqueBlockLock( db.Mutex, std::defer_lock ); )
                if( db.pData == 0 )
                {
                    // upgrade the lock to unique so we can load from disk
                    VA_LBF_THREADSAFE_LINE( sharedBlockLock.unlock(); )
                    VA_LBF_THREADSAFE_LINE( uniqueBlockLock.lock(); )
                    {
                        // could have been loaded by someone else in the meantime 
                        if( db.pData == 0 )
                        {
                            _this.LoadBlock( bx, by );
                        }
                        // else
                        // {
                        //     int dbg = 0;
                        //     dbg++;
                        // }
                    }
                    // continue this block with unique lock!
                }
                int fromX = vaMath::Max( bx * _this.m_BlockDim, rectPosX );
                int fromY = vaMath::Max( by * _this.m_BlockDim, rectPosY );
                int toX = vaMath::Min( bx * _this.m_BlockDim + bw, rectPosX + rectSizeX );
                int toY = vaMath::Min( by * _this.m_BlockDim + bh, rectPosY + rectSizeY );

                for( int y = fromY; y < toY; y++ )
                {
                    int lly = y - rectPosY;
                    int llxf = fromX - rectPosX;
                    //int llxt = toX - rectPosX;
                    void * dstTo = (char*)dstBuffer + (dstPitchInBytes * lly + llxf * _this.m_BytesPerPixel);
                    int bytesCount = (toX-fromX) * _this.m_BytesPerPixel;

                    int bly = y - ( by * _this.m_BlockDim );
                    int blxf = fromX - ( bx * _this.m_BlockDim );

                    memcpy( dstTo, &db.pData[( db.Width * bly + blxf ) * _this.m_BytesPerPixel], bytesCount );
                }
            }
        }
    };

#ifdef VA_ENKITS_INTEGRATION_ENABLED
    if( threadScheduler == nullptr )
#endif
    {
        // non-threaded
        BlockOpTaskSet opSet( *this, dstBuffer, dstPitchInBytes, rectPosX, rectPosY, rectSizeX, rectSizeY, blockXFrom, blockYFrom, blockXTo, blockYTo );
        opSet.ExecuteRange( enki::TaskSetPartition( 0, opSet.m_SetSize ), 0 );
    }
#ifdef VA_ENKITS_INTEGRATION_ENABLED
    else
    {
        if( outPtrTaskSetToWaitOn == nullptr )
        {
            // threaded but non-async
            BlockOpTaskSet opSet( *this, dstBuffer, dstPitchInBytes, rectPosX, rectPosY, rectSizeX, rectSizeY, blockXFrom, blockYFrom, blockXTo, blockYTo );
            threadScheduler->AddTaskSetToPipe( &opSet );
            threadScheduler->WaitforTaskSet( &opSet );
        }
        else
        {
            (*outPtrTaskSetToWaitOn) = std::make_shared<BlockOpTaskSet>( *this, dstBuffer, dstPitchInBytes, rectPosX, rectPosY, rectSizeX, rectSizeY, blockXFrom, blockYFrom, blockXTo, blockYTo );
            threadScheduler->AddTaskSetToPipe( (*outPtrTaskSetToWaitOn).get() );
        }
    }
#endif
#endif

    return true;
}

bool vaLargeBitmapFile::WriteRect( void * srcBuffer, int srcPitchInBytes, int rectPosX, int rectPosY, int rectSizeX, int rectSizeY
#ifdef VA_ENKITS_INTEGRATION_ENABLED
    , vaEnkiTS * threadScheduler, shared_ptr<enki::ITaskSet> * outPtrTaskSetToWaitOn 
#endif
)
{
//    VA_TRACE_CPU_SCOPE( vaLargeBitmapFile_WriteRect );

    VA_LBF_THREADSAFE_LINE( std::shared_lock<std::shared_mutex> lock( m_GlobalMutex ); )

    if( srcBuffer == nullptr )
    {
        assert( false );    // but why?
        return false;
    }

    if( srcPitchInBytes < ( rectSizeX * m_BytesPerPixel ) )
    {
        assert( false );    // source stride looks too small
        return false;
    }

    if( ( rectPosX < 0 ) || ( ( rectPosX + rectSizeX ) > m_Width ) || ( rectPosY < 0 ) || ( ( rectPosY + rectSizeY ) > m_Height ) || ( rectSizeX < 0 ) || ( rectSizeY < 0 ) )
    {
        assert( false );    // invalid lock region (out of range)
        return false;
    }

    int blockXFrom = rectPosX / m_BlockDim;
    int blockYFrom = rectPosY / m_BlockDim;
    int blockXTo = ( rectPosX + rectSizeX - 1 ) / m_BlockDim;
    int blockYTo = ( rectPosY + rectSizeY - 1 ) / m_BlockDim;

    assert( blockXTo < m_BlocksX );
    assert( blockYTo < m_BlocksY );

#if 0
    for( int by = blockYFrom; by <= blockYTo; by++ )
    {
        for( int bx = blockXFrom; bx <= blockXTo; bx++ )
        {
            int bw = ( bx == ( m_BlocksX - 1 ) ) ? ( m_EdgeBlockWidth ) : ( m_BlockDim );
            int bh = ( by == ( m_BlocksY - 1 ) ) ? ( m_EdgeBlockHeight ) : ( m_BlockDim );

            DataBlock & db = m_DataBlocks[bx][by];
            VA_LBF_THREADSAFE_LINE( std::unique_lock<std::shared_mutex> uniqueBlockLock( db.Mutex ); )

            if( db.pData == 0 )
                LoadBlock( bx, by );

            // memcpy( pPixel, db.pData + ( ( db.Width * y + x ) * m_BytesPerPixel ), m_BytesPerPixel );

            int fromX = vaMath::Max( bx * m_BlockDim, rectPosX );
            int fromY = vaMath::Max( by * m_BlockDim, rectPosY );
            int toX = vaMath::Min( bx * m_BlockDim + bw, rectPosX + rectSizeX );
            int toY = vaMath::Min( by * m_BlockDim + bh, rectPosY + rectSizeY );

            for( int y = fromY; y < toY; y++ )
            {
                int lly = y - rectPosY;
                int llxf = fromX - rectPosX;
                int llxt = toX - rectPosX;
                void * srcFrom = (char*)srcBuffer + (srcPitchInBytes * lly + llxf * m_BytesPerPixel);
                int bytesCount = (toX-fromX) * m_BytesPerPixel;

                int bly = y - ( by * m_BlockDim );
                int blxf = fromX - ( bx * m_BlockDim );

                memcpy( &db.pData[( db.Width * bly + blxf ) * m_BytesPerPixel], srcFrom, bytesCount );
            }
            db.Modified = true;
        }
    }

#else

    struct BlockOpTaskSet : enki::ITaskSet
    {
        BlockOpStackVector                  blockOpVector;
        vaLargeBitmapFile &                 _this;
        const void *                        srcBuffer;
        const int                           srcPitchInBytes;
        const int                           rectPosX;
        const int                           rectPosY;
        const int                           rectSizeX;
        const int                           rectSizeY;

        BlockOpTaskSet( vaLargeBitmapFile & _this, void * srcBuffer, int srcPitchInBytes, int rectPosX, int rectPosY, int rectSizeX, int rectSizeY, int blockXFrom, int blockYFrom, int blockXTo, int blockYTo ) : 
            ITaskSet( (uint32_t) (blockXTo-blockXFrom+1) * (blockYTo-blockYFrom+1) ),
            _this(_this), srcBuffer( srcBuffer ), srcPitchInBytes( srcPitchInBytes ), rectPosX( rectPosX ), rectPosY( rectPosY ), rectSizeX( rectSizeX ), rectSizeY( rectSizeY )
        { 
            _this.m_AsyncOpRunningCount++;

            blockOpVector->resize( m_SetSize ); //, blockOpTemplate );
            int blockOpCounter = 0;

            for( int by = blockYFrom; by <= blockYTo; by++ )
            {
                for( int bx = blockXFrom; bx <= blockXTo; bx++ )
                {
                    BlockOp & blockOp = blockOpVector[blockOpCounter];
                    blockOp.bx = (uint16)bx;
                    blockOp.by = (uint16)by;
                    blockOpCounter++;
                }
            }
            assert( blockOpCounter == blockOpVector->size() );
        }
        virtual ~BlockOpTaskSet( )
        {
            _this.m_AsyncOpRunningCount--;
            assert( _this.m_AsyncOpRunningCount.load() >= 0 );  // if this fires, something is seriously wrong - fix it
        }

        virtual void            ExecuteRange( enki::TaskSetPartition range, uint32_t threadnum )
        {
            VA_TRACE_CPU_SCOPE( WriteRectBlock );

            threadnum; // unreferenced

            for( uint32 i = range.start; i < range.end; i++ )
            {
                BlockOp & blockOp = blockOpVector[i];
                int bx = blockOp.bx;
                int by = blockOp.by;
                int bw = ( bx == ( _this.m_BlocksX - 1 ) ) ? ( _this.m_EdgeBlockWidth ) : ( _this.m_BlockDim );
                int bh = ( by == ( _this.m_BlocksY - 1 ) ) ? ( _this.m_EdgeBlockHeight ) : ( _this.m_BlockDim );
                DataBlock & db = _this.m_DataBlocks[bx][by];
                VA_LBF_THREADSAFE_LINE( std::unique_lock<std::shared_mutex> uniqueBlockLock( db.Mutex ); )
                if( db.pData == 0 )
                {
                    _this.LoadBlock( bx, by );
                }

                // memcpy( pPixel, db.pData + ( ( db.Width * y + x ) * m_BytesPerPixel ), m_BytesPerPixel );

                int fromX = vaMath::Max( bx * _this.m_BlockDim, rectPosX );
                int fromY = vaMath::Max( by * _this.m_BlockDim, rectPosY );
                int toX = vaMath::Min( bx * _this.m_BlockDim + bw, rectPosX + rectSizeX );
                int toY = vaMath::Min( by * _this.m_BlockDim + bh, rectPosY + rectSizeY );

                for( int y = fromY; y < toY; y++ )
                {
                    int lly = y - rectPosY;
                    int llxf = fromX - rectPosX;
                    //int llxt = toX - rectPosX;
                    void * srcFrom = (char*)srcBuffer + (srcPitchInBytes * lly + llxf * _this.m_BytesPerPixel);
                    int bytesCount = (toX-fromX) * _this.m_BytesPerPixel;

                    int bly = y - ( by * _this.m_BlockDim );
                    int blxf = fromX - ( bx * _this.m_BlockDim );

                    memcpy( &db.pData[( db.Width * bly + blxf ) * _this.m_BytesPerPixel], srcFrom, bytesCount );
                }
                db.Modified = true;
            }
        }
    };

#ifdef VA_ENKITS_INTEGRATION_ENABLED
    if( threadScheduler == nullptr )
#endif
    {
        // non-threaded
        BlockOpTaskSet opSet( *this, srcBuffer, srcPitchInBytes, rectPosX, rectPosY, rectSizeX, rectSizeY, blockXFrom, blockYFrom, blockXTo, blockYTo );
        opSet.ExecuteRange( enki::TaskSetPartition( 0, opSet.m_SetSize ), 0 );
    }
#ifdef VA_ENKITS_INTEGRATION_ENABLED
    else
    {
        if( outPtrTaskSetToWaitOn == nullptr )
        {
            // threaded but non-async
            BlockOpTaskSet opSet( *this, srcBuffer, srcPitchInBytes, rectPosX, rectPosY, rectSizeX, rectSizeY, blockXFrom, blockYFrom, blockXTo, blockYTo );
            threadScheduler->AddTaskSetToPipe( &opSet );
            threadScheduler->WaitforTaskSet( &opSet );
        }
        else
        {
            (*outPtrTaskSetToWaitOn) = std::make_shared<BlockOpTaskSet>( *this, srcBuffer, srcPitchInBytes, rectPosX, rectPosY, rectSizeX, rectSizeY, blockXFrom, blockYFrom, blockXTo, blockYTo );
            threadScheduler->AddTaskSetToPipe( (*outPtrTaskSetToWaitOn).get() );
        }
    }
#endif

#endif

    return true;
}

template< typename T >
static void ClampBorders( void * _dstBuffer, int dstPitchInBytes, int dstRectSizeX, int dstRectSizeY, int offLeft, int offTop, int offRight, int offBottom
#ifdef VA_ENKITS_INTEGRATION_ENABLED
                , vaEnkiTS * threadScheduler 
#endif
    )
{
#ifdef VA_ENKITS_INTEGRATION_ENABLED
    threadScheduler; // maybe I should implement it?
#endif

    byte * dstBuffer = (byte *)_dstBuffer;
    int m_BytesPerPixel = sizeof(T);
    
    int corrL = 0, corrT = 0, corrR = 0, corrB = 0;

    if( corrL < offLeft )
    {
        for( int y = ( offTop - corrT ); y < ( dstRectSizeY - offBottom + corrB ); y++ )
        {
            T srcValue = *((T*)&dstBuffer[offLeft * m_BytesPerPixel + y * dstPitchInBytes]);
            T * dstFrom = (T*)&dstBuffer[ 0 * m_BytesPerPixel + y * dstPitchInBytes];
            for( corrL = 0; corrL < offLeft; corrL++ ) 
                dstFrom[corrL] = srcValue;
        }
        corrL = offLeft;
    }
    if( corrR < offRight )
    {
        for( int y = ( offTop - corrT ); y < (dstRectSizeY - offBottom + corrB ); y++ )
        {
            T srcValue = *((T*)&dstBuffer[(dstRectSizeX - offRight - 1) * m_BytesPerPixel + y * dstPitchInBytes]);
            T * dstFrom = (T*)&dstBuffer[ (dstRectSizeX - offRight) * m_BytesPerPixel + y * dstPitchInBytes];
            for( corrR = 0; corrR < offRight; corrR++ ) 
                dstFrom[corrR] = srcValue;
        }
        corrR = offRight;
    }
    if( corrT < offTop )
    {
        int srcY = offTop;
        byte * srcLocation = &dstBuffer[(offLeft-corrL) * m_BytesPerPixel + srcY * dstPitchInBytes];
        int pixelCount = dstRectSizeX - offRight + corrR - (offLeft-corrL);
        for( corrT = 0; corrT < offTop; corrT++ ) 
        {
            int dstY = offTop - corrT - 1;
            memcpy( &dstBuffer[(offLeft-corrL) * m_BytesPerPixel + dstY * dstPitchInBytes], srcLocation, pixelCount * m_BytesPerPixel );
        }
        // corrT = offTop;
    }
    if( corrB < offBottom )
    {
        int srcY = dstRectSizeY - offBottom - 1;
        byte * srcLocation = &dstBuffer[(offLeft-corrL) * m_BytesPerPixel + srcY * dstPitchInBytes];
        int pixelCount = dstRectSizeX - offRight + corrR - (offLeft-corrL);
        for( corrB = 0; corrB < offBottom; corrB++ ) 
        {
            int dstY = dstRectSizeY - offBottom + corrB;
            memcpy( &dstBuffer[(offLeft-corrL) * m_BytesPerPixel + dstY * dstPitchInBytes], srcLocation, pixelCount * m_BytesPerPixel );
        }
        // corrB = offBottom;
    }

}

bool vaLargeBitmapFile::ReadRectClampBorders( void * _dstBuffer, int dstPitchInBytes, int64 dstSizeInBytes, int dstRectPosX, int dstRectPosY, int dstRectSizeX, int dstRectSizeY
#ifdef VA_ENKITS_INTEGRATION_ENABLED
    , vaEnkiTS * threadScheduler 
#endif
)
{
    VA_LBF_THREADSAFE_LINE( std::shared_lock<std::shared_mutex> lock( m_GlobalMutex ); )

    byte * dstBuffer = (byte *)_dstBuffer;
    
    // left/top clamping
    int offLeft     = vaMath::Max( 0, -dstRectPosX );
    int offTop      = vaMath::Max( 0, -dstRectPosY );
    assert( offLeft >= 0 );
    assert( offTop >= 0 );

    int dstRectPosRight = dstRectPosX + dstRectSizeX;
    int dstRectPosBottom = dstRectPosY + dstRectSizeY;

    // right/bottom clamping
    int offRight    = vaMath::Max( 0, dstRectPosRight - m_Width );
    int offBottom   = vaMath::Max( 0, dstRectPosBottom - m_Height );

    int readRectSizeX = dstRectSizeX - offLeft - offRight;
    int readRectSizeY = dstRectSizeY - offTop - offBottom;

    if( (readRectSizeX <= 0) || (readRectSizeY <= 0) )
    {
        assert( false );
        return false;
    }

    byte * dstBufferOffsettedTL = ((byte*)dstBuffer) + offLeft * m_BytesPerPixel + offTop * dstPitchInBytes;
    bool ret = ReadRect( dstBufferOffsettedTL, dstPitchInBytes, dstSizeInBytes - (dstBufferOffsettedTL-dstBuffer), dstRectPosX + offLeft, dstRectPosY + offTop, readRectSizeX, readRectSizeY
#ifdef VA_ENKITS_INTEGRATION_ENABLED
        , threadScheduler 
#endif
    );
    if( !ret )
    {
        assert( false );
        return ret;
    }

    if( (offLeft > 0) || (offTop > 0) || (offBottom > 0) || (offRight > 0 )  )
    {
        VA_TRACE_CPU_SCOPE( ClampBorders );
        int corrL = 0, corrT = 0, corrR = 0, corrB = 0;

        if( m_BytesPerPixel == 1 )
            ClampBorders<byte>( _dstBuffer, dstPitchInBytes, dstRectSizeX, dstRectSizeY, offLeft, offTop, offRight, offBottom
#ifdef VA_ENKITS_INTEGRATION_ENABLED
                , threadScheduler 
#endif
                );
        else if( m_BytesPerPixel == 2 )
            ClampBorders<uint16>( _dstBuffer, dstPitchInBytes, dstRectSizeX, dstRectSizeY, offLeft, offTop, offRight, offBottom
#ifdef VA_ENKITS_INTEGRATION_ENABLED
                , threadScheduler 
#endif
                );
        else if( m_BytesPerPixel == 4 )
            ClampBorders<uint32>( _dstBuffer, dstPitchInBytes, dstRectSizeX, dstRectSizeY, offLeft, offTop, offRight, offBottom
#ifdef VA_ENKITS_INTEGRATION_ENABLED
                , threadScheduler 
#endif
                );
        else
        {
            // fallback to memcpy version
            if( corrL < offLeft )
            {
                for( int y = ( offTop - corrT ); y < ( dstRectSizeY - offBottom + corrB ); y++ )
                {
                    int srcX = offLeft;
                    byte * srcLocation = &dstBuffer[srcX * m_BytesPerPixel + y * dstPitchInBytes];

                    //byte * dstFrom = &dstBuffer[ 0 * m_BytesPerPixel + y * dstPitchInBytes];

                    for( corrL = 0; corrL < offLeft; corrL++ ) 
                    {
                        int dstX = offLeft - corrL - 1;
                        memcpy( &dstBuffer[dstX * m_BytesPerPixel + y * dstPitchInBytes], srcLocation, m_BytesPerPixel );
                    }
                }
                corrL = offLeft;
            }
            if( corrR < offRight )
            {
                for( int y = ( offTop - corrT ); y < (dstRectSizeY - offBottom + corrB ); y++ )
                {
                    int srcX = dstRectSizeX - offRight - 1;
                    byte * srcLocation = &dstBuffer[srcX * m_BytesPerPixel + y * dstPitchInBytes];
                    for( corrR = 0; corrR < offRight; corrR++ ) 
                    {
                        int dstX = dstRectSizeX - offRight + corrR;
                        memcpy( &dstBuffer[dstX * m_BytesPerPixel + y * dstPitchInBytes], srcLocation, m_BytesPerPixel );
                    }
                }
                corrR = offRight;
            }
            if( corrT < offTop )
            {
                int srcY = offTop;
                byte * srcLocation = &dstBuffer[(offLeft-corrL) * m_BytesPerPixel + srcY * dstPitchInBytes];
                int pixelCount = dstRectSizeX - offRight + corrR - (offLeft-corrL);
                for( corrT = 0; corrT < offTop; corrT++ ) 
                {
                    int dstY = offTop - corrT - 1;
                    memcpy( &dstBuffer[(offLeft-corrL) * m_BytesPerPixel + dstY * dstPitchInBytes], srcLocation, pixelCount * m_BytesPerPixel );
                }
                corrT = offTop;
            }
            if( corrB < offBottom )
            {
                int srcY = dstRectSizeY - offBottom - 1;
                byte * srcLocation = &dstBuffer[(offLeft-corrL) * m_BytesPerPixel + srcY * dstPitchInBytes];
                int pixelCount = dstRectSizeX - offRight + corrR - (offLeft-corrL);
                for( corrB = 0; corrB < offBottom; corrB++ ) 
                {
                    int dstY = dstRectSizeY - offBottom + corrB;
                    memcpy( &dstBuffer[(offLeft-corrL) * m_BytesPerPixel + dstY * dstPitchInBytes], srcLocation, pixelCount * m_BytesPerPixel );
                }
                corrB = offBottom;
            }
        }
    }

    return true;
}


vaLargeBitmapFile::PixelFormat vaLargeBitmapFile::GetMatchingPixelFormat( vaResourceFormat format )
{
    switch( format )
    {
    case Vanilla::vaResourceFormat::Unknown:
        break;
    case Vanilla::vaResourceFormat::R32G32B32A32_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::R32G32B32A32_FLOAT:
        break;
    case Vanilla::vaResourceFormat::R32G32B32A32_UINT:
        break;
    case Vanilla::vaResourceFormat::R32G32B32A32_SINT:
        break;
    case Vanilla::vaResourceFormat::R32G32B32_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::R32G32B32_FLOAT:
        break;
    case Vanilla::vaResourceFormat::R32G32B32_UINT:
        break;
    case Vanilla::vaResourceFormat::R32G32B32_SINT:
        break;
    case Vanilla::vaResourceFormat::R16G16B16A16_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::R16G16B16A16_FLOAT:
        break;
    case Vanilla::vaResourceFormat::R16G16B16A16_UNORM:
        break;
    case Vanilla::vaResourceFormat::R16G16B16A16_UINT:
        break;
    case Vanilla::vaResourceFormat::R16G16B16A16_SNORM:
        break;
    case Vanilla::vaResourceFormat::R16G16B16A16_SINT:
        break;
    case Vanilla::vaResourceFormat::R32G32_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::R32G32_FLOAT:
        break;
    case Vanilla::vaResourceFormat::R32G32_UINT:
        break;
    case Vanilla::vaResourceFormat::R32G32_SINT:
        break;
    case Vanilla::vaResourceFormat::R32G8X24_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::D32_FLOAT_S8X24_UINT:
        break;
    case Vanilla::vaResourceFormat::R32_FLOAT_X8X24_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::X32_TYPELESS_G8X24_UINT:
        break;
    case Vanilla::vaResourceFormat::R10G10B10A2_TYPELESS:
    case Vanilla::vaResourceFormat::R10G10B10A2_UNORM:
    case Vanilla::vaResourceFormat::R10G10B10A2_UINT:
    case Vanilla::vaResourceFormat::R11G11B10_FLOAT:
        return FormatGeneric32Bit;
        break;
    case Vanilla::vaResourceFormat::R8G8B8A8_TYPELESS:
    case Vanilla::vaResourceFormat::R8G8B8A8_UNORM:
    case Vanilla::vaResourceFormat::R8G8B8A8_UNORM_SRGB:
    case Vanilla::vaResourceFormat::R8G8B8A8_UINT:
    case Vanilla::vaResourceFormat::R8G8B8A8_SNORM:
    case Vanilla::vaResourceFormat::R8G8B8A8_SINT:
        return vaLargeBitmapFile::Format32BitRGBA;
        break;
    case Vanilla::vaResourceFormat::R16G16_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::R16G16_FLOAT:
        break;
    case Vanilla::vaResourceFormat::R16G16_UNORM:
        break;
    case Vanilla::vaResourceFormat::R16G16_UINT:
        break;
    case Vanilla::vaResourceFormat::R16G16_SNORM:
        break;
    case Vanilla::vaResourceFormat::R16G16_SINT:
        break;
    case Vanilla::vaResourceFormat::R32_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::D32_FLOAT:
        break;
    case Vanilla::vaResourceFormat::R32_FLOAT:
        break;
    case Vanilla::vaResourceFormat::R32_UINT:
        break;
    case Vanilla::vaResourceFormat::R32_SINT:
        break;
    case Vanilla::vaResourceFormat::R24G8_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::D24_UNORM_S8_UINT:
        break;
    case Vanilla::vaResourceFormat::R24_UNORM_X8_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::X24_TYPELESS_G8_UINT:
        break;
    case Vanilla::vaResourceFormat::R8G8_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::R8G8_UNORM:
        break;
    case Vanilla::vaResourceFormat::R8G8_UINT:
        break;
    case Vanilla::vaResourceFormat::R8G8_SNORM:
        break;
    case Vanilla::vaResourceFormat::R8G8_SINT:
        break;
    case Vanilla::vaResourceFormat::R16_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::R16_FLOAT:
        break;
    case Vanilla::vaResourceFormat::D16_UNORM:
        break;
    case Vanilla::vaResourceFormat::R16_UNORM:
        break;
    case Vanilla::vaResourceFormat::R16_UINT:
        break;
    case Vanilla::vaResourceFormat::R16_SNORM:
        break;
    case Vanilla::vaResourceFormat::R16_SINT:
        break;
    case Vanilla::vaResourceFormat::R8_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::R8_UNORM:
        break;
    case Vanilla::vaResourceFormat::R8_UINT:
        break;
    case Vanilla::vaResourceFormat::R8_SNORM:
        break;
    case Vanilla::vaResourceFormat::R8_SINT:
        break;
    case Vanilla::vaResourceFormat::A8_UNORM:
        break;
    case Vanilla::vaResourceFormat::R1_UNORM:
        break;
    case Vanilla::vaResourceFormat::R9G9B9E5_SHAREDEXP:
        break;
    case Vanilla::vaResourceFormat::R8G8_B8G8_UNORM:
        break;
    case Vanilla::vaResourceFormat::G8R8_G8B8_UNORM:
        break;
    case Vanilla::vaResourceFormat::BC1_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::BC1_UNORM:
        break;
    case Vanilla::vaResourceFormat::BC1_UNORM_SRGB:
        break;
    case Vanilla::vaResourceFormat::BC2_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::BC2_UNORM:
        break;
    case Vanilla::vaResourceFormat::BC2_UNORM_SRGB:
        break;
    case Vanilla::vaResourceFormat::BC3_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::BC3_UNORM:
        break;
    case Vanilla::vaResourceFormat::BC3_UNORM_SRGB:
        break;
    case Vanilla::vaResourceFormat::BC4_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::BC4_UNORM:
        break;
    case Vanilla::vaResourceFormat::BC4_SNORM:
        break;
    case Vanilla::vaResourceFormat::BC5_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::BC5_UNORM:
        break;
    case Vanilla::vaResourceFormat::BC5_SNORM:
        break;
    case Vanilla::vaResourceFormat::B5G6R5_UNORM:
        break;
    case Vanilla::vaResourceFormat::B5G5R5A1_UNORM:
        break;
    case Vanilla::vaResourceFormat::B8G8R8A8_UNORM:
        break;
    case Vanilla::vaResourceFormat::B8G8R8X8_UNORM:
        break;
    case Vanilla::vaResourceFormat::R10G10B10_XR_BIAS_A2_UNORM:
        break;
    case Vanilla::vaResourceFormat::B8G8R8A8_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::B8G8R8A8_UNORM_SRGB:
        break;
    case Vanilla::vaResourceFormat::B8G8R8X8_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::B8G8R8X8_UNORM_SRGB:
        break;
    case Vanilla::vaResourceFormat::BC6H_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::BC6H_UF16:
        break;
    case Vanilla::vaResourceFormat::BC6H_SF16:
        break;
    case Vanilla::vaResourceFormat::BC7_TYPELESS:
        break;
    case Vanilla::vaResourceFormat::BC7_UNORM:
        break;
    case Vanilla::vaResourceFormat::BC7_UNORM_SRGB:
        break;
    case Vanilla::vaResourceFormat::AYUV:
        break;
    case Vanilla::vaResourceFormat::Y410:
        break;
    case Vanilla::vaResourceFormat::Y416:
        break;
    case Vanilla::vaResourceFormat::NV12:
        break;
    case Vanilla::vaResourceFormat::P010:
        break;
    case Vanilla::vaResourceFormat::P016:
        break;
    case Vanilla::vaResourceFormat::F420_OPAQUE:
        break;
    case Vanilla::vaResourceFormat::YUY2:
        break;
    case Vanilla::vaResourceFormat::Y210:
        break;
    case Vanilla::vaResourceFormat::Y216:
        break;
    case Vanilla::vaResourceFormat::NV11:
        break;
    case Vanilla::vaResourceFormat::AI44:
        break;
    case Vanilla::vaResourceFormat::IA44:
        break;
    case Vanilla::vaResourceFormat::P8:
        break;
    case Vanilla::vaResourceFormat::A8P8:
        break;
    case Vanilla::vaResourceFormat::B4G4R4A4_UNORM:
        break;
    case Vanilla::vaResourceFormat::MaxVal:
        break;
    default:
        break;
    }

    assert( false );

    return vaLargeBitmapFile::FormatUnknown;
}

#ifdef VA_LIBTIFF_INTEGRATION_ENABLED
bool vaLargeBitmapFile::ExportToTiffFile( const wstring & outFilePath )
{
    LibTiff::TIFF* tif = LibTiff::TIFFOpen( vaStringTools::SimpleNarrow(outFilePath).c_str(), "w" );
    if( tif == nullptr )
    {
        assert( false );
        return false;
    }

    int samplesPerPixel = 3;
    int bitsPerSample   = 8;
    int photometric     = PHOTOMETRIC_RGB;
    int planarconfig    = PLANARCONFIG_CONTIG;
    switch( m_PixelFormat )
    {
    case Vanilla::vaLargeBitmapFile::Format16BitGrayScale:
        samplesPerPixel = 1;
        bitsPerSample   = 16;
        photometric     = PHOTOMETRIC_MINISBLACK;
        break;
    case Vanilla::vaLargeBitmapFile::Format8BitGrayScale:
        assert( false ); // not yet implemented
        return false;
        break;
    case Vanilla::vaLargeBitmapFile::Format24BitRGB:
        samplesPerPixel = 3;
        bitsPerSample   = 8;
        photometric     = PHOTOMETRIC_RGB;
        break;
    case Vanilla::vaLargeBitmapFile::Format32BitRGBA:
        samplesPerPixel = 4;
        bitsPerSample   = 8;
        photometric     = PHOTOMETRIC_RGB; // what about a?
        break;
    case Vanilla::vaLargeBitmapFile::Format16BitA4R4G4B4:
        assert( false ); // not yet implemented
        return false;
        break;
    default:
        assert( false );
        return false;
        break;
    }

    LibTiff::TIFFSetField( tif, TIFFTAG_IMAGEWIDTH, m_Width );               // set the width of the image
    LibTiff::TIFFSetField( tif, TIFFTAG_IMAGELENGTH, m_Height );             // set the height of the image
    LibTiff::TIFFSetField( tif, TIFFTAG_SAMPLESPERPIXEL, samplesPerPixel );  // set number of channels per pixel
    LibTiff::TIFFSetField( tif, TIFFTAG_BITSPERSAMPLE, bitsPerSample );      // set the size of the channels
    LibTiff::TIFFSetField( tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE );
    LibTiff::TIFFSetField( tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);   // set the origin of the image (top left for us)
    LibTiff::TIFFSetField( tif, TIFFTAG_PLANARCONFIG, planarconfig);
    LibTiff::TIFFSetField( tif, TIFFTAG_PHOTOMETRIC, photometric);
    // LibTiff::TIFFSetField( tif, TIFFTAG_REFERENCEBLACKWHITE, refblackwhite );
    // LibTiff::TIFFSetField( tif, TIFFTAG_TRANSFERFUNCTION, gray );
    LibTiff::TIFFSetField( tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_NONE );

    int rowPitch    = m_BytesPerPixel * m_Width;
    int stripHeight = m_BlockDim;

    byte * stripBuffer = new byte[rowPitch * stripHeight];
    
    int lastLoadedRow = -1;
    int currentStripRowBaseOffset = -stripHeight;

    //Now writing image to the file one strip at a time
    for( int32 row = 0; row < m_Height; row++ )
    {
        if( row > lastLoadedRow )
        {
            currentStripRowBaseOffset = currentStripRowBaseOffset + stripHeight;
            int nextStripRowBaseOffset = vaMath::Min( currentStripRowBaseOffset + stripHeight, m_Height );
            ReadRect( stripBuffer, rowPitch, (int64)rowPitch * stripHeight, 0, row, m_Width, nextStripRowBaseOffset - currentStripRowBaseOffset );
            lastLoadedRow = nextStripRowBaseOffset-1;
        }

        int rowInStrip = row - currentStripRowBaseOffset;
        assert( rowInStrip >= 0 && rowInStrip < stripHeight );

        byte * bufferPtr = &stripBuffer[ rowPitch * rowInStrip ];

        if( LibTiff::TIFFWriteScanline( tif, bufferPtr, row, 0 ) < 0 )
        {
            delete[] stripBuffer;
            assert( false );
            return false;
        }
    }

    LibTiff::TIFFClose(tif);
    delete[] stripBuffer;

    return true;
}
#endif