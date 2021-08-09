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


namespace Vanilla
{
    ////////////////////////////////////////////////////////////////////////////////////////////////
    // vaMemory
    class vaMemory
    {
    private:
       friend class vaCore;

       static void						Initialize( );
       static void						Deinitialize( );
    };

    // Just a simple generic self-contained memory buffer helper class, for passing data as argument, etc.
    class vaMemoryBuffer
    {
        uint8 *                 m_buffer;
        int64                   m_bufferSize;
        bool                    m_hasOwnership;     // if it has ownership, will delete[] and expect it to be of uint8[] type - could expand with custom deallocator if needed

    public:
        enum class InitType
        {
            Copy,               // copy buffer (caller is free to release/modify the memory pointer after this call)
            TakeOwnership,      // take buffer pointer and delete[] when object is destroyed (caller should not ever use the memory after this call)
            View,               // take buffer pointer but don't delete[] when object is destroyed (caller should not ever use the memory after this call)
        };

    public:
        vaMemoryBuffer( ) noexcept : m_buffer( nullptr ), m_bufferSize( 0 ), m_hasOwnership( false ) { }
        vaMemoryBuffer( int64 bufferSize ) noexcept : m_buffer( new uint8[bufferSize] ), m_bufferSize( bufferSize ), m_hasOwnership( true ) { }
        vaMemoryBuffer( uint8 * buffer, int64 bufferSize, InitType initType = InitType::Copy ) noexcept : m_buffer( (initType != InitType::Copy)?(buffer):(new uint8[bufferSize]) ), m_bufferSize( bufferSize ), m_hasOwnership( initType != InitType::View )
        { 
            if( initType == InitType::Copy )
                memcpy( m_buffer, buffer, bufferSize );
            else
            {
                // if taking ownership or just viewing, must have been allocated with new uint8[]
                assert( typeid(m_buffer) == typeid(buffer) );
            }
        }
        vaMemoryBuffer( const vaMemoryBuffer & copySrc ) noexcept : m_buffer( new uint8[copySrc.GetSize()] ), m_bufferSize( copySrc.GetSize() ), m_hasOwnership( true )
        {
            memcpy( m_buffer, copySrc.GetData(), m_bufferSize );
        }
        vaMemoryBuffer( vaMemoryBuffer && moveSrc ) noexcept : m_buffer( moveSrc.GetData() ), m_bufferSize( moveSrc.GetSize() ), m_hasOwnership( moveSrc.m_hasOwnership )
        {
            moveSrc.m_buffer        = nullptr;
            moveSrc.m_bufferSize    = 0;
            moveSrc.m_hasOwnership  = false;
        }

        ~vaMemoryBuffer( ) noexcept
        {
            Clear( );
        }

        vaMemoryBuffer & operator = ( const vaMemoryBuffer & copySrc ) noexcept
        {
            // avoid copy if buffers same size!
            if( copySrc.m_bufferSize != m_bufferSize )
            {
                Clear( );
                m_buffer = new uint8[copySrc.GetSize()];
                m_bufferSize = copySrc.GetSize();
            }
            m_hasOwnership = true;
            memcpy( m_buffer, copySrc.GetData(), m_bufferSize );

            return *this;
        }

        bool operator == ( const vaMemoryBuffer & other ) const noexcept 
        {
            if( m_bufferSize != other.m_bufferSize )
                return false;

            // comparison logic goes here
            return memcmp( m_buffer, other.m_buffer, m_bufferSize ) == 0;
        }

        void Clear( )
        {
            if( m_buffer != nullptr && m_hasOwnership )
                delete[] m_buffer;
            m_buffer = nullptr;
            m_bufferSize = 0;
            m_hasOwnership = false;
        }

    public:
        uint8 *     GetData( ) const noexcept        { return m_buffer; }
        int64       GetSize( ) const noexcept        { return m_bufferSize; }
    };

}