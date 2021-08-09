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

#include "Core/System/vaThreading.h"

#include <tchar.h>

using namespace Vanilla;


void vaThreading::Sleep( uint32 milliseconds )
{
   ::Sleep( milliseconds );
}

void vaThreading::YieldProcessor( )
{
    ::YieldProcessor( );
}

typedef BOOL( WINAPI *LPFN_GLPI )(
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION,
    PDWORD );


// Helper function to count set bits in the processor mask.
static DWORD CountSetBits( ULONG_PTR bitMask )
{
    DWORD LSHIFT = sizeof( ULONG_PTR ) * 8 - 1;
    DWORD bitSetCount = 0;
    ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;
    DWORD i;

    for( i = 0; i <= LSHIFT; ++i )
    {
        bitSetCount += ( ( bitMask & bitTest ) ? 1 : 0 );
        bitTest /= 2;
    }

    return bitSetCount;
}

void vaThreading::GetCPUCoreCountInfo( int & physicalPackages, int & physicalCores, int & logicalCores )
{
    static int s_physicalPackages = -1;
    static int s_physicalCores = -1;
    static int s_logicalCores = -1;

    if( s_physicalPackages != -1 && s_physicalCores != -1 && s_logicalCores != -1 )
    {
        physicalPackages = s_physicalPackages;
        physicalCores = s_physicalCores;
        logicalCores = s_logicalCores;
        return;
    }

    // how difficult could it be to get logical core count?
    // well, apparently, according to https://msdn.microsoft.com/en-us/library/windows/desktop/ms683194%28v=vs.85%29.aspx,  difficult:

    LPFN_GLPI glpi;
    BOOL done = FALSE;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
    DWORD returnLength = 0;
    DWORD logicalProcessorCount = 0;
    DWORD numaNodeCount = 0;
    DWORD processorCoreCount = 0;
    DWORD processorL1CacheCount = 0;
    DWORD processorL2CacheCount = 0;
    DWORD processorL3CacheCount = 0;
    DWORD processorPackageCount = 0;
    DWORD byteOffset = 0;
    PCACHE_DESCRIPTOR Cache;

    physicalPackages = -1;
    physicalCores = -1;
    logicalCores = -1;

    glpi = (LPFN_GLPI)GetProcAddress(
        GetModuleHandle( TEXT( "kernel32" ) ),
        "GetLogicalProcessorInformation" );
    if( NULL == glpi )
    {
        _tprintf( TEXT( "\nGetLogicalProcessorInformation is not supported.\n" ) );
        assert( false );
        return;
    }

    while( !done )
    {
        DWORD rc = glpi( buffer, &returnLength );

        if( FALSE == rc )
        {
            if( GetLastError( ) == ERROR_INSUFFICIENT_BUFFER )
            {
                if( buffer )
                    free( buffer );

                buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(
                    returnLength );

                if( NULL == buffer )
                {
                    _tprintf( TEXT( "\nError: Allocation failure\n" ) );
                    assert( false );
                    return;
                }
            }
            else
            {
                _tprintf( TEXT( "\nError %d\n" ), GetLastError( ) );
                assert( false );
                return;
            }
        }
        else
        {
            done = TRUE;
        }
    }

    ptr = buffer;

    while( byteOffset + sizeof( SYSTEM_LOGICAL_PROCESSOR_INFORMATION ) <= returnLength )
    {
        switch( ptr->Relationship )
        {
        case RelationNumaNode:
            // Non-NUMA systems report a single record of this type.
            numaNodeCount++;
            break;

        case RelationProcessorCore:
            processorCoreCount++;

            // A hyperthreaded core supplies more than one logical processor.
            logicalProcessorCount += CountSetBits( ptr->ProcessorMask );
            break;

        case RelationCache:
            // Cache data is in ptr->Cache, one CACHE_DESCRIPTOR structure for each cache. 
            Cache = &ptr->Cache;
            if( Cache->Level == 1 )
            {
                processorL1CacheCount++;
            }
            else if( Cache->Level == 2 )
            {
                processorL2CacheCount++;
            }
            else if( Cache->Level == 3 )
            {
                processorL3CacheCount++;
            }
            break;

        case RelationProcessorPackage:
            // Logical processors share a physical package.
            processorPackageCount++;
            break;

        default:
            _tprintf( TEXT( "\nError: Unsupported LOGICAL_PROCESSOR_RELATIONSHIP value.\n" ) );
            break;
        }
        byteOffset += sizeof( SYSTEM_LOGICAL_PROCESSOR_INFORMATION );
        ptr++;
    }

    //_tprintf( TEXT( "\nGetLogicalProcessorInformation results:\n" ) );
    //_tprintf( TEXT( "Number of NUMA nodes: %d\n" ),
    //    numaNodeCount );
    //_tprintf( TEXT( "Number of physical processor packages: %d\n" ),
    //    processorPackageCount );
    //_tprintf( TEXT( "Number of processor cores: %d\n" ),
    //    processorCoreCount );
    //_tprintf( TEXT( "Number of logical processors: %d\n" ),
    //    logicalProcessorCount );
    //_tprintf( TEXT( "Number of processor L1/L2/L3 caches: %d/%d/%d\n" ),
    //    processorL1CacheCount,
    //    processorL2CacheCount,
    //    processorL3CacheCount );
    //
    //
    free( buffer );

    physicalPackages = processorPackageCount;
    physicalCores = processorCoreCount;
    logicalCores = logicalProcessorCount;
    s_physicalPackages  = physicalPackages;
    s_physicalCores     = physicalCores;
    s_logicalCores      = logicalCores;
}

void vaThreading::MainThreadSetup( )
{
    //::SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL );
}
