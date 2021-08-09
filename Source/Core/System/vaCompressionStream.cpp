///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Core\vaCoreIncludes.h"

#include "vaCompressionStream.h"

#include "IntegratedExternals/vaZlibIntegration.h"



//////////////////////////////////////////////////////////////////////////////
// vaCompressionStream
//////////////////////////////////////////////////////////////////////////////
using namespace Vanilla;
using namespace Zlib;
//
namespace Vanilla
{
    struct vaCompressionStreamWorkingContext
    {
        z_stream        strm;
        unsigned char   workingBuffer[256*1024];

        bool            flushFlag;  // if set during decompression then it means 'no more input data'


        vaCompressionStreamWorkingContext( )
        {
            strm.zalloc     = Z_NULL;
            strm.zfree      = Z_NULL;
            strm.opaque     = Z_NULL;
            strm.avail_in   = 0;
            strm.next_in    = Z_NULL;
            flushFlag       = false;
            workingBuffer[0]= 0;
        }
    };
}
//
vaCompressionStream::vaCompressionStream( bool decompressing, shared_ptr<vaStream> inoutStream, Profile profile )
    : m_decompressing( decompressing ), m_compressedStream( inoutStream ), m_compressedStreamNakedPtr(nullptr), m_compressionProfile( profile ), m_workingContext( nullptr )
{
    Initialize( decompressing );
}
//
vaCompressionStream::vaCompressionStream( bool decompressing, vaStream * inoutStream, Profile profile )
    : m_decompressing( decompressing ), m_compressedStream( nullptr ), m_compressedStreamNakedPtr(inoutStream), m_compressionProfile( profile ), m_workingContext( nullptr )
{
    Initialize( decompressing );
}
//
void vaCompressionStream::Initialize( bool decompressing )
{
    m_workingContext = new vaCompressionStreamWorkingContext( );

    // just to make sure we're actually reading/writing an underlying vaCompressionStream
    const uint32 c_magicHeader = 0x37EB769C;

    uint32 magicHeader = 0;

    // nothing else supported
    assert( m_compressionProfile == vaCompressionStream::Profile::Default );

    int ret;

    if( decompressing )
    {
        bool allOk = true;
        uint32 dummy0; // to expand with something
        uint64 dummy1; // to expand with something else (possibly packed file size?)
        allOk &= GetInnerStream( )->ReadValue<uint32>( magicHeader );
        allOk &= GetInnerStream( )->ReadValue<uint32>( (uint32&)m_compressionProfile );
        allOk &= GetInnerStream( )->ReadValue<uint32>( dummy0 );
        allOk &= GetInnerStream( )->ReadValue<uint64>( dummy1 );
        allOk &= magicHeader == c_magicHeader;
        allOk &= m_compressionProfile == vaCompressionStream::Profile::Default;

        if( allOk )
            ret = inflateInit( &m_workingContext->strm );
        else
            ret = Z_DATA_ERROR;
    }
    else
    {
        bool allOk = true;
        allOk &= GetInnerStream( )->WriteValue<uint32>( c_magicHeader );
        allOk &= GetInnerStream( )->WriteValue<uint32>( (uint32)m_compressionProfile );
        allOk &= GetInnerStream( )->WriteValue<uint32>( 0 );
        allOk &= GetInnerStream( )->WriteValue<uint64>( 0 );

        if( allOk )
            ret = deflateInit( &m_workingContext->strm, Z_DEFAULT_COMPRESSION );
        else
            ret = Z_DATA_ERROR;
    }

    if( ret != Z_OK )
    {
        assert( false );
        m_compressedStream = nullptr;
        m_compressedStreamNakedPtr = nullptr;
        return;
    }
}
//
vaCompressionStream::~vaCompressionStream(void) 
{
    Close( );
}
//
void vaCompressionStream::Close( )
{
    if( !IsOpen() )
        return;

    int ret = 0;
    if( !m_decompressing )
    {
        m_workingContext->flushFlag = true;
        bool allOk = Write( nullptr, 0 );
        assert( allOk ); allOk;
        ret = deflateEnd( &m_workingContext->strm );
    }
    else
    {
        if( m_workingContext != nullptr )
        {
            ret = inflateEnd( &m_workingContext->strm );
        }
    }
    ret;
    assert( ret >= 0 );

    m_compressedStream = nullptr;
    m_compressedStreamNakedPtr = nullptr;
    if( m_workingContext != nullptr )
    {
        delete m_workingContext;
        m_workingContext = nullptr;
    }
}
//
bool vaCompressionStream::Read( void * buffer, int64 count, int64 * outCountRead )
{ 
    if( !m_decompressing || !IsOpen() )
    {
        if( outCountRead != nullptr )
            *outCountRead = 0;
        return false;
    }
    assert( m_workingContext != nullptr );

    if( count >= INT_MAX )
    {
        const int64 stepSize = 0x40000000;
        int stepsToDo = (int)(( count + stepSize - 1 ) / stepSize);

        for( int step = 0; step < stepsToDo; step++ )
        {
            void * bufferStep = ( (char *)buffer ) + stepSize * step;
            int64 bufferStepCount = vaMath::Min( stepSize, count - step * stepSize );
            int64 stepOutRead = 0;
            bool retVal = Read( bufferStep, bufferStepCount, &stepOutRead );
            if( outCountRead != nullptr )
                (*outCountRead) += stepOutRead;
            if( !retVal )
                return false;
        }
        return true;
    }

    int64 totalRead = 0;

    m_workingContext->strm.avail_out = (uint32)count;
    m_workingContext->strm.next_out = (Bytef *)buffer;

    do 
    {
        // if 0 available, need to load from input stream - flushFlag serves as "input empty"
        if( m_workingContext->strm.avail_in == 0 )
        {
            if( !m_workingContext->flushFlag )
            {
                int64 numRead = 0;
                GetInnerStream( )->Read( m_workingContext->workingBuffer, sizeof(m_workingContext->workingBuffer), &numRead );
                m_workingContext->strm.next_in = m_workingContext->workingBuffer;
                m_workingContext->strm.avail_in = (uint32)numRead;
                m_workingContext->flushFlag = numRead == 0;         // reached end of reading
            }
            else
            {
                // no more in available but not Z_STREAM_END? bad.
                assert( false );
                Close( );
                if( outCountRead != nullptr )
                    *outCountRead = totalRead;
                return totalRead == count;
            }
        }

        int availOutPrev = m_workingContext->strm.avail_out;
        int ret = inflate( &m_workingContext->strm, Z_NO_FLUSH );
        assert( ret != Z_STREAM_ERROR );  /* state not clobbered */
        switch( ret ) 
        {
        case Z_NEED_DICT:
            ret = Z_DATA_ERROR;     /* and fall through */
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            (void)inflateEnd( &m_workingContext->strm );
            assert( false );
            delete m_workingContext;
            m_workingContext = nullptr;
            Close();
            return false;
        }

        int numInflated = availOutPrev - m_workingContext->strm.avail_out;
        totalRead += numInflated;

        if( ret == Z_STREAM_END )
        {
            // assert( m_workingContext->flushFlag );
            Close( );
            if( outCountRead != nullptr )
                *outCountRead = totalRead;
            return totalRead == count;
        }

    } while( totalRead < count );

    m_workingContext->strm.avail_out    = 0;
    m_workingContext->strm.next_out     = nullptr;

    if( outCountRead != nullptr )
        *outCountRead = totalRead;

    return totalRead == count; 
}
bool vaCompressionStream::Write( const void * buffer, int64 count, int64 * outCountWritten )
{ 
    if( m_decompressing || !IsOpen())
    {
        assert( false );
        return false;
    }
    assert( m_workingContext != nullptr );
    
    if( count >= INT_MAX )
    {
        const int64 stepSize = 0x40000000;
        int stepsToDo = (int)( ( count + stepSize - 1 ) / stepSize );

        for( int step = 0; step < stepsToDo; step++ )
        {
            const void * bufferStep = ((const char *)buffer) + stepSize * step;
            int64 bufferStepCount = vaMath::Min( stepSize, count - step * stepSize );
            int64 stepOutWritten = 0;
            bool retVal = Write( bufferStep, bufferStepCount, &stepOutWritten );
            if( outCountWritten != nullptr )
                (*outCountWritten) += stepOutWritten;
            if( !retVal )
                return false;
        }
        return true;
    }

    m_workingContext->strm.avail_in = (uint32)count;
    m_workingContext->strm.next_in  = (z_const Bytef *)buffer;
    
    int flush = m_workingContext->flushFlag ? Z_FINISH : Z_NO_FLUSH;

    int64 totalNumDeflated = 0;

    /* run deflate() on input until output buffer not full, finish
        compression if all of source has been read in */
    do 
    {
        m_workingContext->strm.avail_out  = sizeof( m_workingContext->workingBuffer );
        m_workingContext->strm.next_out   = m_workingContext->workingBuffer;
        int ret = deflate( &m_workingContext->strm, flush );

        // negative values are errors
        if( ret < 0 )
        {
            assert( false );
            Close();
            return false;
        }

        int numDeflated = sizeof(m_workingContext->workingBuffer) - m_workingContext->strm.avail_out;
        totalNumDeflated += numDeflated;

        bool allOk = GetInnerStream( )->Write( m_workingContext->workingBuffer, numDeflated );
        assert( allOk ); allOk;
    } while( m_workingContext->strm.avail_out == 0 );

    m_workingContext->strm.avail_in = 0;
    m_workingContext->strm.next_in  = nullptr;
    
    // all input should have been used
    assert( m_workingContext->strm.avail_in == 0 );

    if( outCountWritten != nullptr )
        *outCountWritten = count;

    return true; 
}
//

// USED FOR TESTING
/*
auto memStream = vaFileTools::LoadMemoryStream( "C:\\Work\\INTC_SHARE\\bistro_compressed_vs_uncompressed_textures.pdn" );// "C:\\Work\\INTC_SHARE\\CMAA2\\Projects\\CMAA2\\Media\\AssetPacks\\Bistro_Research_Interior_AssetPack.apack" );

#if 0
    shared_ptr<vaFileStream> fileOut = std::make_shared<vaFileStream>();
    fileOut->Open( "C:\\Work\\INTC_SHARE\\CMAA2\\Projects\\CMAA2\\Media\\AssetPacks\\aaa.aaa", FileCreationMode::Create );

    vaCompressionStream compressor( false, fileOut );
    compressor.Write( memStream->GetBuffer(), memStream->GetLength() );
    compressor.Close();
#else
    {
        auto memStreamComp = vaFileTools::LoadMemoryStream( "C:\\Work\\INTC_SHARE\\CMAA2\\Projects\\CMAA2\\Media\\AssetPacks\\aaa.aaa" );

        //shared_ptr<vaMemoryStream> memStreamOut = std::make_shared<vaMemoryStream>( (int64)0, 1024*1024 );
        shared_ptr<vaFileStream> fileOut = std::make_shared<vaFileStream>( );
        fileOut->Open( "C:\\Work\\INTC_SHARE\\bistro_compressed_vs_uncompressed_textures_1.pdn", FileCreationMode::Create );

        vaCompressionStream compressor( true, memStreamComp );

        char buffer[768 * 1024];
        int64 numberRead = 0;
        do 
        {
            compressor.Read( buffer, sizeof(buffer), &numberRead );
            if( numberRead > 0 )
                fileOut->Write( buffer, numberRead );
                //memStreamOut->Write( buffer, numberRead );
        } while( numberRead > 0 );
        //assert( memStream->GetLength() == memStreamOut->GetLength() );
        //assert( memcmp( memStream->GetBuffer(), memStreamOut->GetBuffer(), memStream->GetLength() ) == 0 );
    }
#endif
*/

///////////////////////////////////////////////////////////////////////////////////////////////////