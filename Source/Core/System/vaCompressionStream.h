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

namespace Vanilla
{
    struct vaCompressionStreamWorkingContext;

    class vaCompressionStream : public vaStream
    {
    public:
        enum class Profile
        {
            Default             = 0,
            PassThrough         = 1,
        };

    private:
        
        Profile                 m_compressionProfile;

        shared_ptr<vaStream>    m_compressedStream;
        vaStream *              m_compressedStreamNakedPtr;

        bool                    m_decompressing;

        vaCompressionStreamWorkingContext *
                                m_workingContext;

    public:
        vaCompressionStream( bool decompressing, shared_ptr<vaStream> compressedStream, Profile profile = Profile::Default );
        vaCompressionStream( bool decompressing, vaStream * compressedStreamNakedPtr, Profile profile = Profile::Default );     // same as above except no smart pointer
        virtual ~vaCompressionStream( void );

        virtual bool            CanSeek( ) override                 { return false; }
        virtual void            Seek( int64 position ) override     { assert( false ); position; }
        virtual void            Close( ) override;
        virtual bool            IsOpen( ) const override            { return GetInnerStream() != nullptr; }
        virtual int64           GetLength( ) override               { assert( false ); return -1; }
        virtual int64           GetPosition( ) const override       { assert( false ); return -1; }
        virtual void            Truncate( ) override                { assert( false ); }

        virtual bool            CanRead( ) const override           { return IsOpen() && m_decompressing; }
        virtual bool            CanWrite( )const  override          { return IsOpen() && !m_decompressing; }

        virtual bool            Read( void * buffer, int64 count, int64 * outCountRead = NULL );
        virtual bool            Write( const void * buffer, int64 count, int64 * outCountWritten = NULL );

    private:
        void                    Initialize( bool decompressing );
        vaStream *              GetInnerStream( ) const             { return (m_compressedStream!=nullptr)?(m_compressedStream.get()):(m_compressedStreamNakedPtr); }
    };


}

