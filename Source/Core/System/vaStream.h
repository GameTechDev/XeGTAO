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

#include "Core/vaSTL.h"
#include "../vaMath.h"

namespace Vanilla
{
    class vaStream
    {
    public:
        virtual ~vaStream( void ) {}

        virtual bool            IsOpen( ) const                                                             = 0;
        virtual void            Close( )                                                                    = 0;
        virtual int64           GetLength( )                                                                = 0;
        virtual int64           GetPosition( ) const                                                        = 0;
        virtual bool            CanSeek( )                                                                  = 0;
        virtual void            Seek( int64 position )                                                      = 0;
        virtual void            Truncate( )                                                                 = 0;     // truncate everything behind current

        virtual bool            CanRead( )  const                                                           { return IsOpen(); }
        virtual bool            CanWrite( ) const                                                           { return IsOpen(); }

        // Return true if count number was actually read, false on error or on reading less than the requested number
        virtual bool            Read( void * buffer, int64 count, int64 * outCountRead = NULL )             = 0;

        // Return true if count number was actually written, false on error or on writing less than the requested number
        virtual bool            Write( const void * buffer, int64 count, int64 * outCountWritten = NULL )   = 0;

        template<typename T>
        inline bool             WriteValue( const T & val );
        template<typename ElementType>
        inline bool             WriteValueVector( const std::vector<ElementType> & elements );
        template<typename T>
        inline bool             ReadValue( T & val );
        template<typename T>
        inline bool             ReadValue( T & val, const T & def );
        template<typename ElementType>
        inline bool             ReadValueVector( std::vector<ElementType> & elements );

        // these use internal binary representation prefixed with size
        inline bool             WriteString( const wstring & str );
        inline bool             WriteString( const string & str );
        inline bool             ReadString( wstring & outStr );
        inline bool             ReadString( string & outStr );

        // these are supposed to just read a text file but I haven't sorted out any line ending or encoding conversions
        inline bool             ReadTXT( wstring & outStr, int64 count = -1 );
        inline bool             ReadTXT( string & outStr, int64 count = -1 );
        inline bool             WriteTXT( const wstring & str );
        inline bool             WriteTXT( const string & str );

        template<typename... Args>
        inline bool             WriteTXT( const char * fmtStr, ... )                                         { va_list args; va_start(args, fmtStr); string str = vaStringTools::Format( fmtStr, args ); va_end(args); return WriteTXT( str ); }

    };


    ///////////////////////////////////////////////////////////////////////////////////////////////////
    // inline

    template<typename T>
    inline bool vaStream::WriteValue( const T & val )
    {
        assert( CanWrite() );
        return Write( &val, sizeof( T ) );
    }

    template<typename T>
    inline bool vaStream::ReadValue( T & val )
    {
        assert( CanRead( ) );
        return Read( &val, sizeof( T ) );
    }
    template<typename T>
    inline bool vaStream::ReadValue( T & val, const T & deft )
    {
        assert( CanRead( ) );
        if( ReadValue( val ) )
            return true;
        val = deft;
        return false;
    }

    inline bool vaStream::WriteString( const wstring & str )
    {
        uint32 lengthInBytes = (uint32)str.size( ) * 2;
        if( !WriteValue<uint32>( lengthInBytes | ( 1 << 31 ) ) )      // 32nd bit flags it as unicode (utf16) for verification when reading
            return false;
        assert( lengthInBytes < ( 1 << 31 ) );
        if( lengthInBytes == 0 )
            return true;
        else
            return Write( str.c_str( ), lengthInBytes );
    }

    inline bool vaStream::WriteString( const string & str )
    {
        uint32 lengthInBytes = (uint32)str.size( );
        if( !WriteValue<uint32>( lengthInBytes ) )
            return false;
        assert( lengthInBytes < ( 1 << 31 ) );
        if( lengthInBytes == 0 )
            return true;
        else
            return Write( str.c_str( ), lengthInBytes );
    }

    inline bool vaStream::ReadString( wstring & outStr )
    {
        uint32 lengthInBytes;
        if( !ReadValue<uint32>( lengthInBytes ) )
            return false;
        assert( ( lengthInBytes & ( 1 << 31 ) ) != 0 );                // not reading a unicode string?
        lengthInBytes &= ~( 1 << 31 );
        assert( lengthInBytes % 2 == 0 );                           // must be an even number!

        // Empty string?
        if( lengthInBytes == 0 )
        {
            outStr = L"";
            return true;
        }

        wchar_t * buffer = new wchar_t[lengthInBytes / 2 + 1];          // TODO: remove dynamic alloc for smaller (most) reads - just use stack memory
        if( !Read( buffer, lengthInBytes ) )
        {
            delete[] buffer;
            return false;
        }
        buffer[lengthInBytes / 2] = 0;

        outStr = wstring( buffer, lengthInBytes / 2 );
        delete[] buffer;
        return true;
    }

    inline bool vaStream::ReadString( string & outStr )
    {
        uint32 lengthInBytes;
        if( !ReadValue<uint32>( lengthInBytes ) )
            return false;
        assert( ( lengthInBytes & ( 1 << 31 ) ) == 0 );                // not reading an ansi string?

        // Empty string?
        if( lengthInBytes == 0 )
        {
            outStr = "";
            return true;
        }

        char * buffer = new char[lengthInBytes + 1];          // TODO: remove dynamic alloc for smaller (most) reads - just use stack memory
        if( !Read( buffer, lengthInBytes ) )
        {
            delete[] buffer;
            return false;
        }
        buffer[lengthInBytes] = 0;

        outStr = string( buffer, lengthInBytes );
        delete[] buffer;
        return true;
    }

    inline bool vaStream::ReadTXT( wstring & outStr, int64 count )
    {
        int64 remainingSize = GetLength( ) - GetPosition( );
        if( count == -1 )
            count = remainingSize;
        else
            count = vaMath::Min( count, remainingSize );

        // not tested unicode .txt file reading - there's a header at the top I think? need to implement that, sorry.
        assert( false );

        // Empty string?
        if( count == 0 )
        {
            outStr = L"";
            return true;
        }

        wchar_t * buffer = new wchar_t[count / 2 + 1];          // TODO: remove dynamic alloc for smaller (most) reads - just use stack memory
        if( !Read( buffer, count ) )
        {
            delete[] buffer;
            return false;
        }
        buffer[count / 2] = 0;

        outStr = wstring( buffer, count / 2 );
        delete[] buffer;
        return true;
    }

    inline bool vaStream::ReadTXT( string & outStr, int64 count )
    {
        assert( CanRead( ) );
        int64 remainingSize = GetLength( ) - GetPosition( );
        if( count == -1 )
            count = remainingSize;
        else
            count = vaMath::Min( count, remainingSize );

        // Empty string?
        if( count == 0 )
        {
            outStr = "";
            return true;
        }

        char * buffer = new char[count + 1];          // TODO: remove dynamic alloc for smaller (most) reads - just use stack memory
        if( !Read( buffer, count ) )
        {
            delete[] buffer;
            return false;
        }
        buffer[count] = 0;

        outStr = string( buffer, count );
        delete[] buffer;
        return true;
    }

    inline bool vaStream::WriteTXT( const wstring & str )
    {
        assert( CanWrite( ) );
        uint32 lengthInBytes = (uint32)str.size( ) * 2;
        assert( lengthInBytes < ( 1 << 31 ) );
        if( lengthInBytes == 0 )
            return true;
        else
            return Write( str.c_str( ), lengthInBytes );
    }

    inline bool vaStream::WriteTXT( const string & str )
    {
        assert( CanWrite( ) );
        uint32 lengthInBytes = (uint32)str.size( );
        assert( lengthInBytes < ( 1 << 31 ) );
        if( lengthInBytes == 0 )
            return true;
        else
            return Write( str.c_str( ), lengthInBytes );
    }

#define VASTREAM_ALLOW_WHOLE_VECTOR_BUFFER_READWRITE


    template<typename ElementType>
    inline bool vaStream::WriteValueVector( const std::vector<ElementType> & elements )
    {
        assert( elements.size( ) < INT_MAX ); // not supported; to add support for 64bit size use most significant bit (sign) to indicate that the size is >= INT_MAX; this is backwards compatible and will not unnecessarily increase file size

        bool ret = WriteValue<int>( (int)elements.size( ) );
        assert( ret ); if( !ret ) return false;

        for( int i = 0; i < (int)elements.size( ); i++ )
        {
            ret = WriteValue<ElementType>( elements[i] );
            assert( ret ); if( !ret ) return false;
        }

        return true;
    }

    template<typename ElementType>
    inline bool vaStream::ReadValueVector( std::vector<ElementType> & elements )
    {
        assert( elements.size( ) == 0 ); // must be empty at the moment

        int count;
        if( !ReadValue<int>( count, -1 ) )
            return false;
        assert( count >= 0 ); if( count < 0 ) return false;

        if( count == 0 ) return true;

        elements.resize( count );

#ifdef VASTREAM_ALLOW_WHOLE_VECTOR_BUFFER_READWRITE
        int64 sizeToRead = count * sizeof( ElementType );
        if( !Read( elements.data(), sizeToRead ) )
            return false;
#else
        for( int i = 0; i < count; i++ )
        {
            bool ret = ReadValue<ElementType>( elements[i] );
            assert( ret ); if( !ret ) return false;
        }
#endif

        return true;
    }
}