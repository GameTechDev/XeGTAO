///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaEvent.h"
//using namespace Vanilla;

namespace Vanilla
{

    void vaEventTest( )
    {
        vaEvent<void(int)> testEvent;
        testEvent.Invoke( 1 );
        {
            shared_ptr< int > aNumberInMemory = std::make_shared<int>( int(41) );
            testEvent.AddWithToken( weak_ptr<void>(aNumberInMemory), [aNumberInMemory](int p) { *aNumberInMemory += p; } );
            testEvent.Invoke( 1 );
            assert( *aNumberInMemory == 42 );
            testEvent.Remove( weak_ptr<void>(aNumberInMemory) );
            testEvent.Invoke( 1 );
            assert( *aNumberInMemory == 42 );
        }
        testEvent.Invoke( 1 );
    }

}