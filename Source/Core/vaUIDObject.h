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

#include "vaCoreIncludes.h"
#include "vaConcurrency.h"
#include "vaSingleton.h"
#include "System/vaStream.h"

#include "Core/vaProfiler.h"

namespace Vanilla
{
    ////////////////////////////////////////////////////////////////////////////////////////////////
    // vaUIDObject/vaUIDObjectRegistrar is used to assign GUIDs to objects and allow search to 
    // establish connections persistent over different runs. Useful for stuff like resources.
    //
    // They don't get added to the registry automatically because it would make a partially 
    // constructed object searchable from other threads; rather, you have to call UIDObject_Track
    // after they're fully constructed.
    // They do get removed from the registry automatically because it's assumed that the thread
    // deleting it is the only one which has access to it.
    // However, in case you want to remove it before (make it unsearchable), use UIDObject_Untrack!
    class vaUIDObject : virtual public vaFramePtrTag, public std::enable_shared_from_this<vaUIDObject>
    {
    private:
        friend class vaUIDObjectRegistrar;
        vaGUID /*const*/                            m_uid;                                  // removed const to be able to have SwapIDs but no one else anywhere should ever be modifying this!!
        atomic_bool                                 m_tracked = false;                      // this variable is protected by vaUIDObjectRegistrar::m_objectsMapMutex

    protected:
        explicit vaUIDObject( const vaGUID & uid ) noexcept;

    public:
        virtual ~vaUIDObject( ) noexcept;

    public:
        const vaGUID &                               UIDObject_GetUID( ) const noexcept         { return m_uid; }
        bool                                         UIDObject_IsTracked( ) const noexcept;
        bool                                         UIDObject_Track( ) noexcept;
        bool                                         UIDObject_Untrack( ) noexcept;
    };

    class vaUIDObjectRegistrar : protected vaSingletonBase< vaUIDObjectRegistrar >
    {
    protected:
        friend class vaUIDObject;
        friend class vaCore;

        std::unordered_map< vaGUID, vaUIDObject *, vaGUIDHasher > 
                                                    m_objectsMap;

        lc_shared_mutex<61>                         m_objectsMapMutex;

        shared_ptr<vaUIDObject>                     m_nullObject;

    private:
        friend class vaCore;
        vaUIDObjectRegistrar( );
        ~vaUIDObjectRegistrar( );

    private:
        //In theory, these could be made public. But not all implications have been thought through so for now leave them private.
        static bool                                 IsTracked( const vaUIDObject * obj )  noexcept;
        static bool                                 Track( vaUIDObject * obj ) noexcept         { auto & s = vaUIDObjectRegistrar::GetInstance( ); std::unique_lock mapLock( s.m_objectsMapMutex ); return s.TrackNoMutexLock( obj ); }
        static bool                                 Untrack( vaUIDObject * obj ) noexcept       { auto & s = vaUIDObjectRegistrar::GetInstance( ); std::unique_lock mapLock( s.m_objectsMapMutex ); return s.UntrackNoMutexLock( obj ); }
        static bool                                 Untrack( const vaGUID & uid ) noexcept      { auto & s = vaUIDObjectRegistrar::GetInstance( ); std::unique_lock mapLock( s.m_objectsMapMutex ); return s.UntrackNoMutexLock( uid ); }

    public:

        template< class T >
        static shared_ptr<T>                        Find( const vaGUID & uid ) noexcept;

        template< class T >
        static vaFramePtr<T>                        FindFP( const vaGUID & uid ) noexcept;

        // To use this you have to lock the mutex. But make sure you're not locking the mutex and calling any other self-locking ones (like FindFP) because recursive locks are not supported.
        template< class T >
        static vaFramePtr<T>                        FindFPNoMutexLock( const vaGUID & uid ) noexcept;

        static bool                                 Has( const vaGUID & uid ) noexcept;

        // // NOTE: This went out due to threading contention issue with weakPointer.lock and the function itself not being completely correct
        // // faster version of Find - you provide a weakPointer that might or might not point to the object with the ID - if it is,
        // // it's a cheap call; if not, regular Find() is performed
        // template< class T >
        // static inline std::shared_ptr<T>            FindCached( const vaGUID & uid, std::weak_ptr<T> & inOutCachedPtr ) noexcept;

        // Exchange two object IDs
        static void                                 SwapIDs( const shared_ptr<vaUIDObject> & a, const shared_ptr<vaUIDObject> & b ) noexcept;

        static auto &                               Mutex( )                                    { return vaUIDObjectRegistrar::GetInstance( ).m_objectsMapMutex; }

    private:
        // could make these public if one ever needs to track/untrack a bunch of objects at once or similar
        //mutex &                                     GetMutex( ) { return m_objectsMapMutex; }
        bool                                        TrackNoMutexLock( vaUIDObject * obj ) noexcept;
        bool                                        UntrackNoMutexLock( vaUIDObject * obj ) noexcept;
        bool                                        UntrackNoMutexLock( const vaGUID & uid ) noexcept;

        shared_ptr<vaUIDObject>                     FindNoMutexLockRaw( const vaGUID & uid ) const noexcept;

        template< class T >
        shared_ptr<T>                               FindNoMutexLock( const vaGUID & uid ) const noexcept;

        //void                                        UntrackIfTracked( const shared_ptr<vaUIDObject> & obj ) noexcept    { std::unique_lock mapLock( m_objectsMapMutex ); if( obj->m_tracked ) UntrackNoMutexLock(obj);    }
    };

    // inline 

    inline bool vaUIDObjectRegistrar::IsTracked( const vaUIDObject * obj ) noexcept
    {
        std::shared_lock mapLock( vaUIDObjectRegistrar::GetInstance( ).m_objectsMapMutex );
        return obj->m_tracked;
    }

    template< class T>
    inline shared_ptr<T> vaUIDObjectRegistrar::Find( const vaGUID & uid ) noexcept
    {
        auto & s = vaUIDObjectRegistrar::GetInstance( ); 
        std::shared_lock mapLock( s.m_objectsMapMutex );
        return s.FindNoMutexLock<T>( uid );
    }

    template< class T >
    inline vaFramePtr<T> vaUIDObjectRegistrar::FindFP( const vaGUID & uid ) noexcept
    {
        auto & s = vaUIDObjectRegistrar::GetInstance( );
        std::shared_lock mapLock( s.m_objectsMapMutex );
        return FindFPNoMutexLock<T>( uid );
    }

    template< class T >
    inline vaFramePtr<T> vaUIDObjectRegistrar::FindFPNoMutexLock( const vaGUID & uid ) noexcept
    {
        if( uid == vaCore::GUIDNull( ) )
            return nullptr;

        auto & s = vaUIDObjectRegistrar::GetInstance( );
        
        auto it = s.m_objectsMap.find( uid );
        if( it == s.m_objectsMap.end( ) )
        {
            return nullptr;
        }
        else
        {
            if( !it->second->m_tracked )
            {
                VA_ERROR( "vaUIDObjectRegistrar::FindNoMutexLock() - Something has gone really bad here - object is not marked as tracked but was found in the map. Don't ignore it." );
                return nullptr;
            }
        }
        
        return vaFramePtr<T>( it->second );
    }

    inline bool vaUIDObjectRegistrar::Has( const vaGUID & uid ) noexcept
    {
        auto& s = vaUIDObjectRegistrar::GetInstance( );
        std::shared_lock mapLock( s.m_objectsMapMutex );
        auto & map = s.m_objectsMap;
        return map.find( uid ) != map.end();
    }

    inline shared_ptr<vaUIDObject> vaUIDObjectRegistrar::FindNoMutexLockRaw( const vaGUID & uid ) const noexcept
    {
        if( uid == vaCore::GUIDNull( ) )
            return nullptr;

        auto it = m_objectsMap.find( uid );
        if( it == m_objectsMap.end( ) )
        {
            return nullptr;
        }
        else
        {
            if( !it->second->m_tracked )
            {
                VA_ERROR( "vaUIDObjectRegistrar::FindNoMutexLock() - Something has gone really bad here - object is not marked as tracked but was found in the map. Don't ignore it." );
                return nullptr;
            }
            return it->second->weak_from_this().lock();
        }
    }

    template< class T >
    inline shared_ptr<T> vaUIDObjectRegistrar::FindNoMutexLock( const vaGUID & uid ) const noexcept
    {
        shared_ptr<vaUIDObject> rawPtr = FindNoMutexLockRaw( uid );
        if( rawPtr == nullptr )
            return nullptr;

        return std::static_pointer_cast<T>( rawPtr );
    }

    inline bool SaveUIDObjectUID( vaStream & outStream, const shared_ptr<vaUIDObject> & obj ) noexcept
    {
        if( obj == nullptr )
        {
            VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<vaGUID>( vaCore::GUIDNull( ) ) );
        }
        else
        {
            VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<vaGUID>( obj->UIDObject_GetUID( ) ) );
        }
        return true;
    }

    inline bool vaUIDObject::UIDObject_IsTracked( ) const noexcept  { return vaUIDObjectRegistrar::IsTracked( this ); }
    inline bool vaUIDObject::UIDObject_Track( ) noexcept            { return vaUIDObjectRegistrar::Track( this ); }
    inline bool vaUIDObject::UIDObject_Untrack( ) noexcept          { return vaUIDObjectRegistrar::Untrack( this ); }
}