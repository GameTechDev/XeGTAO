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

#include "vaMemory.h"

using namespace Vanilla;

#include "IntegratedExternals/vaAssimpIntegration.h"

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
#include "IntegratedExternals/vaTaskflowIntegration.h"
#endif

struct vaPSOKeyDataHasher
{
    std::size_t operator () ( const vaMemoryBuffer& key ) const
    {
        // see vaGraphicsPSODescDX12::FillKey and vaComputePSODescDX12::FillKey - hash is in the first 64 bits of the buffer :)

        return *reinterpret_cast<uint64*>( key.GetData( ) );
    }
};

_CrtMemState s_memStateStart;

void vaMemory::Initialize( )
{

    // initialize some of the annoying globals so they appear before the s_memStateStart checkpoint
    {
    #ifdef VA_ASSIMP_INTEGRATION_ENABLED
        {
            Assimp::Importer importer;

            static unsigned char s_simpleObj [] = {35,9,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,86,101,114,116,105,99,101,115,58,32,56,13,10,35,9,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,80,111,105,110,116,115,58,32,48,13,10,35,9,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,76,105,110,101,115,58,32,48,13,10,35,9,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,70,97,99,101,115,58,32,54,13,10,35,9,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,77,97,116,101,114,105,97,108,115,58,32,49,13,10,13,10,111,32,49,13,10,13,10,35,32,86,101,114,116,101,120,32,108,105,115,116,13,10,13,10,118,32,45,48,46,53,32,45,48,46,53,32,48,46,53,13,10,118,32,45,48,46,53,32,45,48,46,53,32,45,48,46,53,13,10,118,32,45,48,46,53,32,48,46,53,32,45,48,46,53,13,10,118,32,45,48,46,53,32,48,46,53,32,48,46,53,13,10,118,32,48,46,53,32,45,48,46,53,32,48,46,53,13,10,118,32,48,46,53,32,45,48,46,53,32,45,48,46,53,13,10,118,32,48,46,53,32,48,46,53,32,45,48,46,53,13,10,118,32,48,46,53,32,48,46,53,32,48,46,53,13,10,13,10,35,32,80,111,105,110,116,47,76,105,110,101,47,70,97,99,101,32,108,105,115,116,13,10,13,10,117,115,101,109,116,108,32,68,101,102,97,117,108,116,13,10,102,32,52,32,51,32,50,32,49,13,10,102,32,50,32,54,32,53,32,49,13,10,102,32,51,32,55,32,54,32,50,13,10,102,32,56,32,55,32,51,32,52,13,10,102,32,53,32,56,32,52,32,49,13,10,102,32,54,32,55,32,56,32,53,13,10,13,10,35,32,69,110,100,32,111,102,32,102,105,108,101,13,10,};
            const aiScene * scene = importer.ReadFileFromMemory( s_simpleObj, _countof(s_simpleObj), 0, "obj" );
            scene;
            //const aiScene * scene = importer.ReadFile( "C:\\Work\\Art\\crytek-sponza-other\\banner.obj", 0 );
        }
    #endif

    // this damn thing is only to force taskflow globals to initialize before the initial checkpoint so we avoid memory warnings....
// #ifdef VA_TASKFLOW_INTEGRATION_ENABLED
//         tf::Taskflow workFlow;
//         std::atomic_int32_t hrmpf = 0;
//         workFlow.parallel_for( 0, 42, 1, [&hrmpf] (int i) { i; hrmpf++; } , 1Ui64 ).second;
//         vaTF::GetInstance( ).Executor( ).run( workFlow ).wait();
//         assert( hrmpf == 42 );
// #endif

    }

    {
    #if defined(DEBUG) || defined(_DEBUG)
    //	_CrtSetDbgFlag( 0
    //		| _CRTDBG_ALLOC_MEM_DF 
    //		|  _CRTDBG_CHECK_CRT_DF
    //		| _CRTDBG_CHECK_EVERY_128_DF
    //		| _CRTDBG_LEAK_CHECK_DF 
    //		);
        vaCore::DebugOutput( L"CRT memory checkpoint start" );
        _CrtMemCheckpoint( &s_memStateStart );
      //_CrtSetBreakAlloc( 291 );  // <- if this didn't work, the allocation probably happens before Initialize() or is a global variable
    #endif

    }
}

void vaMemory::Deinitialize()
{
#if defined(DEBUG) || defined(_DEBUG)
   _CrtMemState memStateStop, memStateDiff;
   _CrtMemCheckpoint( &memStateStop );

   vaCore::DebugOutput( L"Checking for memory leaks...\n" );
   if( _CrtMemDifference( &memStateDiff, &s_memStateStart, &memStateStop ) )
   {
        _CrtMemDumpStatistics( &memStateDiff );
        
        // doesn't play nice with .net runtime
        _CrtDumpMemoryLeaks();

        VA_WARN( L"Memory leaks detected - check debug output; using _CrtSetBreakAlloc( allocationIndex ) in vaMemory::Initialize() might help to track it down. " );

        // note: assimp sometimes leaks during importing - that's benign
        assert( false );
   }
   else
   {
        vaCore::DebugOutput( L"No memory leaks detected!\n" );
   }
#endif
}


