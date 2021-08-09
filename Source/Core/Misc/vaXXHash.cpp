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

#include "vaXXHash.h"
#include "xxhash.h"

using namespace Vanilla;

vaXXHash64::vaXXHash64( uint64 seed )
{
    assert( false ); // code below never tested - please run through debugger to make sure it's all fine

    m_state = XXH64_createState();
    XXH64_reset( m_state, seed );
}
vaXXHash64::~vaXXHash64( )
{
    XXH64_freeState( m_state );
}

void vaXXHash64::AddBytes( const void * dataPtr, int64 length )
{
    XXH_errorcode const addResult = XXH64_update( m_state, dataPtr, length );
    assert(addResult != XXH_ERROR);
}

uint64 vaXXHash64::Digest( ) const
{
    return XXH64_digest( m_state );
}

uint64 vaXXHash64::Compute( const void * dataPtr, int64 length, uint64 seed )
{
    return XXH64( dataPtr, length, seed );
}
