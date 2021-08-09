// Based on https://github.com/Cyan4973/xxHash, original license below:
// xxHash Library
// Copyright (c) 2012-2014, Yann Collet
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
// 
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
// 
// * Redistributions in binary form must reproduce the above copyright notice, this
//   list of conditions and the following disclaimer in the documentation and/or
//   other materials provided with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
// ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include "Core/vaCoreTypes.h"
#include <string>
#include <assert.h>

typedef struct XXH64_state_s XXH64_state_t;   /* incomplete type */

namespace Vanilla
{
    class vaXXHash64
    {
    private:
        XXH64_state_t *     m_state = nullptr;

    public:
        vaXXHash64( uint64 seed = 0 );
        ~vaXXHash64( );

        vaXXHash64( const vaXXHash64 & copy ) = delete;
        vaXXHash64 & operator = ( const vaXXHash64 & copy ) = delete;

    public:
        // compute xxhash directly from input buffer
        static uint64       Compute( const void * dataPtr, int64 length, uint64 seed = 0 );

        // get current value
        //operator             uint64( ) const                  { return Digest(); }
        uint64              Digest( ) const;

        void                AddBytes( const void * dataPtr, int64 length );

        inline void         AddString( const std::string & str )
        {
            int length = (int)str.length( );
            AddValue( length );
            if( length > 0 )  AddBytes( str.c_str( ), length );
        }

        inline void         AddString( const std::wstring & str )
        {
            int length = (int)str.length( ) * 2;
            AddValue( length );
            if( length > 0 )  AddBytes( str.c_str( ), length );
        }

        template< class ValueType >
        inline void         AddValue( ValueType val )
        {
            AddBytes( &val, sizeof( val ) );
        }

    private:

    };

}
