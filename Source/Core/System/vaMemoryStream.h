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
#include "Core/vaMemory.h"

#include "vaStream.h"

namespace Vanilla
{
    class vaMemoryStream : public vaStream
    {
        uint8 *                 m_buffer;
        int64                   m_bufferSize;
        int64                   m_pos;
        //
        bool                    m_autoBuffer;
        int64                   m_autoBufferCapacity;
        //
    public:
        
        // this version gets fixed size external buffer and provides read/write access to it without the ability to resize
        vaMemoryStream( void * buffer, int64 bufferSize );

        // this version keep internal buffer that grows on use (or can be manually resized)
        vaMemoryStream( int64 initialSize = 0, int64 reserve = 0 );

        vaMemoryStream( const vaMemoryStream & copyFrom );

        // operator ==

        virtual ~vaMemoryStream( void );

        virtual bool            CanSeek( ) { return true; }
        virtual void            Seek( int64 position ) { m_pos = position; }
        virtual void            Close( ) { assert( false ); }
        virtual bool            IsOpen( ) const { return m_buffer != NULL; }
        virtual int64           GetLength( ) { return m_bufferSize; }
        virtual int64           GetPosition( ) const override { return m_pos; }
        virtual void            Truncate( ) { assert( false ); }

        virtual bool            Read( void * buffer, int64 count, int64 * outCountRead = NULL );
        virtual bool            Write( const void * buffer, int64 count, int64 * outCountWritten = NULL );

        uint8 *                 GetBuffer( ) { return m_buffer; }
        void                    Resize( int64 newSize );

    private:
        void                    Grow( int64 nextCapacity );
    };


}

