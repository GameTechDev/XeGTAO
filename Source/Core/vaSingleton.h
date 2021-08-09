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

#include "vaPlatformBase.h"

// Frequently used includes
#include <assert.h>

// I don't think there's any use case where this is needed - all of these get created in vaCore::Initialize,
// before any of the other threads are even spawned.
//#define VA_SINGLETON_USE_ATOMICS

namespace Vanilla
{
    ////////////////////////////////////////////////////////////////////////////////////////////////
    // Simple base class for a singleton.
    //  - ensures that the class is indeed a singleton
    //  - provides access to it
    //  1.) inherit YourClass from vaSingletonBase<YourClass>
    //  2.) you're responsible for creation/destruction of the object and its thread safety!
    //
    template< class SingletonType >
    class vaSingletonBase
    {
    private:
#ifdef VA_SINGLETON_USE_ATOMICS
        static std::atomic<SingletonType *>     s_instance;
        static std::atomic_bool                 s_instanceValid;
#else
        static SingletonType *                  s_instance;
        static bool                             s_instanceValid;
#endif

    protected:
        vaSingletonBase( )
        {
#ifdef VA_SINGLETON_USE_ATOMICS
            SingletonType * previous = s_instance.exchange( static_cast<SingletonType *>( this ) );
#else
            SingletonType * previous = s_instance; s_instance = static_cast<SingletonType *>( this );
            //assert( vaThreading::IsMainThread() );
#endif
            assert( previous == NULL );
            previous; // unreferenced in Release
            s_instanceValid = true;
        }
        virtual ~vaSingletonBase( )
        {
            InvalidateInstance( );
#ifdef VA_SINGLETON_USE_ATOMICS
            SingletonType * previous = s_instance.exchange( nullptr );
#else
            SingletonType * previous = s_instance; s_instance = nullptr;
#endif
            assert( previous != NULL );
            previous; // unreferenced in Release
        }
        void InvalidateInstance( )              { s_instanceValid = false; }

    public:

        static SingletonType &                  GetInstance( )          { SingletonType * retVal = s_instance; assert( retVal != NULL ); return *retVal; }
        static SingletonType *                  GetInstancePtr( )       { return s_instance; }
        static bool                             GetInstanceValid( )     { return s_instanceValid; }
    };
    //
#ifdef VA_SINGLETON_USE_ATOMICS
    template <class SingletonType>
    std::atomic<SingletonType *> vaSingletonBase<SingletonType>::s_instance; // = nullptr;
    template <class SingletonType>
    std::atomic_bool vaSingletonBase<SingletonType>::s_instanceValid; // = nullptr;
#else
    template <class SingletonType>
    SingletonType * vaSingletonBase<SingletonType>::s_instance = nullptr;
    template <class SingletonType>
    bool vaSingletonBase<SingletonType>::s_instanceValid = false;
#endif
    ////////////////////////////////////////////////////////////////////////////////////////////////


    ////////////////////////////////////////////////////////////////////////////////////////////////
    // Simple base class for a multiton.
    //  - just keeps a list of objects of the type (array, not map/dictionary!)
    template< class MultitonType >
    class vaMultitonBase
    {
    public:
        struct LockedInstances
        {
            mutex &                         Mutex;
            const std::vector<MultitonType *> &  Instances;

            LockedInstances( mutex & mutex, std::vector<MultitonType *> & instances ) : Mutex( mutex ), Instances( instances ) { mutex.lock( ); }
            ~LockedInstances( ) { Mutex.unlock( ); }
        };
    private:
        static mutex                        s_allInstancesMutex;
        static std::vector<MultitonType *>  s_allInstances;

        int                                 m_instanceIndex;

    protected:
        vaMultitonBase( )
        {
            LockedInstances li = GetInstances( ); // lock instances!
            m_instanceIndex = (int)li.Instances.size( );
            s_allInstances.push_back( (MultitonType*)this );
        }
        virtual ~vaMultitonBase( )
        {
            LockedInstances li = GetInstances( ); // lock instances!

            assert( s_allInstances.size( ) > 0 );

            // we're not the last one? swap with the last 
            if( m_instanceIndex != li.Instances.size( ) - 1 )
            {
                MultitonType * lastOne = s_allInstances.back( );
                lastOne->m_instanceIndex = m_instanceIndex;
                s_allInstances[m_instanceIndex] = lastOne;
            }
            s_allInstances.pop_back( );
        }

    public:

        static LockedInstances              GetInstances( ) { return LockedInstances( s_allInstancesMutex, s_allInstances ); }
    };
    //
    template <class MultitonType>
    mutex                                   vaMultitonBase<MultitonType>::s_allInstancesMutex;
    template <class MultitonType>
    std::vector<MultitonType *>             vaMultitonBase<MultitonType>::s_allInstances;
    ////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // This is a simple class that gives each instance a new 64bit ID at construction.
    // There is no tracking of any kind so the cost is minimal (one InterlockedAdd)
    template< class TrackableType >
    class vaRuntimeID
    {
    private:
        static std::atomic_uint64_t         s_runtimeIDCounter;
        uint64 const                        m_runtimeID;
    protected:
        vaRuntimeID( ) : m_runtimeID( s_runtimeIDCounter.fetch_add(1ull) )  { }
    public:
        uint64                              RuntimeIDGet( ) const noexcept  { return m_runtimeID; }
    };
    template< class TrackableType >
    std::atomic_uint64_t vaRuntimeID<TrackableType>::s_runtimeIDCounter = 0ull;
    
    ////////////////////////////////////////////////////////////////////////////////////////////////
}