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

#include "vaUIDObject.h"

#include "vaLog.h"

using namespace Vanilla;

vaUIDObject::vaUIDObject( const vaGUID & uid ) noexcept 
    : m_uid( uid )
{
}

vaUIDObject::~vaUIDObject( ) noexcept
{
    bool removed = vaUIDObjectRegistrar::Untrack( this );
    removed;    // it's ok if it wasn't removed; for ex, the asset manager will remove resources on unload to avoid
                // them being tracked but it can't guarantee actual objects being deleted as well
}

vaUIDObjectRegistrar::vaUIDObjectRegistrar()
{
    assert( vaThreading::IsMainThread() );

    // ensure there's no objects with null guid in, ever
    m_nullObject = std::shared_ptr<vaUIDObject>( new vaUIDObject( vaGUID::Null ) );

    m_objectsMap.max_load_factor( 0.25f );
}

vaUIDObjectRegistrar::~vaUIDObjectRegistrar( )
{
    Untrack( vaGUID::Null );

    std::unique_lock mapLock( m_objectsMapMutex );
    // not 0? memory leak or not all objects deleted before the registrar was deleted (bug)
    assert( m_objectsMap.size( ) == 0 );
}


bool vaUIDObjectRegistrar::TrackNoMutexLock( vaUIDObject * obj ) noexcept
{
    if( obj->m_tracked )
    {
        // VA_LOG_WARNING( "vaUIDObjectRegistrar::Track() - object already tracked" );
        return false;
    }

    auto it = m_objectsMap.find( obj->m_uid );
    if( it != m_objectsMap.end( ) )
    {
        assert( false );
        VA_LOG_ERROR( "vaUIDObjectRegistrar::Track() - object with the same UID already exists: this is a potential bug, the new object will not be tracked and will not be searchable by vaUIDObjectRegistrar::Find" );
        return false;
    }
    else
    {
        m_objectsMap.insert( std::make_pair( obj->m_uid, obj ) );
        obj->m_tracked = true;
        return true;
    }
}

bool vaUIDObjectRegistrar::UntrackNoMutexLock( vaUIDObject * obj ) noexcept
{
    // if not tracked just ignore it, it's probably fine, we can allow untrack multiple times
    if( !obj->m_tracked )
        return false;

    auto it = m_objectsMap.find( obj->m_uid );
    if( it == m_objectsMap.end( ) )
    {
        VA_ERROR( "vaUIDObjectRegistrar::Untrack() - A tracked vaUIDObject couldn't be found: this is an indicator of a more serious error such as an algorithm bug or a memory overwrite. Don't ignore it." );
        return false;
    }
    else
    {
        // if this isn't correct, we're removing wrong object - this is a serious error, don't ignore it!
        if( obj != it->second )
        {
            VA_ERROR( "vaUIDObjectRegistrar::Untrack() - A tracked vaUIDObject could be found in the map but the pointers don't match: this is an indicator of a more serious error such as an algorithm bug or a memory overwrite. Don't ignore it." );
            return false;
        }
        else
        {
            obj->m_tracked = false;
            m_objectsMap.erase( it );
            return true;
        }
    }
}

bool vaUIDObjectRegistrar::UntrackNoMutexLock( const vaGUID & uid ) noexcept
{
    auto it = m_objectsMap.find( uid );
    if( it != m_objectsMap.end( ) )
    {
        it->second->m_tracked = false;
        m_objectsMap.erase( it );
        return true;
    }
    return false;
}

void vaUIDObjectRegistrar::SwapIDs( const shared_ptr<vaUIDObject> & a, const shared_ptr<vaUIDObject> & b ) noexcept
{
    auto & s = vaUIDObjectRegistrar::GetInstance( );

    std::unique_lock mapLock( s.m_objectsMapMutex );

    bool aWasTracked = a->m_tracked;
    if( aWasTracked )
        s.UntrackNoMutexLock( a.get() );
    bool bWasTracked = a->m_tracked;
    if( bWasTracked )
        s.UntrackNoMutexLock( b.get() );

    // swap UIDs in objects
    std::swap( a->m_uid, b->m_uid );

    // swap tracking as well - I think this is what we want, the UID that was in to stay in
    if( bWasTracked )
        s.TrackNoMutexLock( a.get() );
    if( aWasTracked )
        s.TrackNoMutexLock( b.get() );
}

/*
void vaUIDObjectRegistrar::DeviceReset( ) noexcept
{
    auto & s = vaUIDObjectRegistrar::GetInstance( );

    std::unique_lock mapLock( s.m_objectsMapMutex );
    
    auto it = s.m_objectsMap.cbegin();
    while( it != s.m_objectsMap.cend( ) )
    {
        if( it->second->m_removeOnDeviceReset )
        {
            assert( it->second->m_tracked );
            it->second->m_tracked = false;
            it = s.m_objectsMap.erase( it );
        }
        else
            it++;
    }
}
*/