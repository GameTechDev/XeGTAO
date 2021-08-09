///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaMemoryStream.h"
#include "..\vaMath.h"

#include <stdio.h>
#include <memory.h>
#include <algorithm>

//////////////////////////////////////////////////////////////////////////////
// vaMemoryStream
//////////////////////////////////////////////////////////////////////////////
using namespace Vanilla;
//
vaMemoryStream::vaMemoryStream( void * buffer, int64 bufferSize )
{
    m_buffer = (uint8 *)buffer;
    m_bufferSize = bufferSize;
    m_autoBufferCapacity = 0;
    m_pos = 0;
    m_autoBuffer = false;
}
//
vaMemoryStream::vaMemoryStream( int64 initialSize, int64 reserve )
{
    reserve = vaMath::Max( reserve, 16i64 );
    reserve = vaMath::Max( reserve, initialSize );
    assert( reserve > 0 );
    assert( reserve >= initialSize );
    m_buffer = new uint8[reserve];
    m_autoBufferCapacity = reserve;
    m_bufferSize = initialSize;
    m_pos = 0;
    m_autoBuffer = true;
}
//
vaMemoryStream::vaMemoryStream( const vaMemoryStream & copyFrom )
{
    if( copyFrom.m_buffer != nullptr )
    {
        m_buffer = new uint8[copyFrom.m_bufferSize];
        memcpy( m_buffer, copyFrom.m_buffer, copyFrom.m_bufferSize );
    }
    else
    {
        m_buffer = nullptr;
    }
    m_bufferSize = copyFrom.m_bufferSize;
    m_autoBufferCapacity = copyFrom.m_autoBufferCapacity;
    m_pos = copyFrom.m_pos;
    m_autoBuffer = copyFrom.m_autoBuffer;
}
//
vaMemoryStream::~vaMemoryStream( void )
{
    if( m_autoBuffer )
    {
        delete[] m_buffer;
    }
}
//
bool vaMemoryStream::Read( void * buffer, int64 count, int64 * outCountRead )
{
    int64 countToReallyRead = count;
    if( count + m_pos > m_bufferSize )
        countToReallyRead = m_bufferSize - m_pos;

    memcpy( buffer, m_buffer + m_pos, countToReallyRead );

    m_pos += countToReallyRead;

    if( outCountRead != nullptr )
        *outCountRead = countToReallyRead;

    return count == countToReallyRead;
}
bool vaMemoryStream::Write( const void * buffer, int64 count, int64 * outCountWritten )
{
    assert( outCountWritten == NULL ); // not implemented!
    outCountWritten;

    if( ( count + m_pos ) > m_bufferSize )
    {
        if( m_autoBuffer )
        {
            if( ( count + m_pos ) > m_autoBufferCapacity )
            {
                // grow by needed + 50%
                Grow( count + m_pos + (m_autoBufferCapacity+1)/2 );
            }
            m_bufferSize = count + m_pos;
        }
        else
        {
            // buffer overrun, should handle this differently now that we've got 
            assert( false );
            return false;
        }
    }

    memcpy( m_buffer + m_pos, buffer, count );

    m_pos += count;

    return true;
}
//
void vaMemoryStream::Resize( int64 newSize )
{
    if( m_autoBuffer )
    {
        if( newSize > m_autoBufferCapacity )
        {
            Grow( newSize );
        }
        m_bufferSize = newSize;
        m_pos = std::min( m_pos, m_bufferSize );
    }
    else
    {
        // buffer overrun
        assert( false );
        return;
    }
}
//
void vaMemoryStream::Grow( int64 nextCapacity )
{
    assert( m_autoBuffer );
    assert( nextCapacity > m_autoBufferCapacity );

    uint8 * newBuffer = new uint8[nextCapacity];
    memcpy( newBuffer, m_buffer, m_bufferSize );
    m_autoBufferCapacity = nextCapacity;
    delete[] m_buffer;
    m_buffer = newBuffer;
}
//
///////////////////////////////////////////////////////////////////////////////////////////////////