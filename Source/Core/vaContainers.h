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

namespace Vanilla
{
    // There might be a generic C++ pattern for this problem, but I couldn't find one so I wrote these two classes.
    //
    // for example check vaUIDObject!
    // 
    // - vaTT_Trackee can only be created with a reference to vaTT_Tracker, and is then tracked in an array by the tracker.
    // - vaTT_Trackee object can only be tracked by one vaTT_Tracker object.
    // - On destruction _Trackee gets automatically removed from the _Tracker list, and it always knows its index so removing/adding is fast.
    // - If a _Tracker is destroyed, its tracked objects will get disconnected and become untracked and they can destruct at later time but cannot be tracked again.
    // - The array of tracked objects can be obtained by using vaTT_Tracker::TTGetTrackedObjects for read-only purposes.
    // - One vaTT_Trackee object cannot be tracked by more than one vaTT_Trackers, but you can create multiple Trackee-s and assign them to different trackers.

    template< class TTTagType >
    class vaTT_Trackee;


    template< class TTTagType >
    class vaTT_Tracker
    {
    private:
        template< class TTTagType >
        friend class vaTT_Trackee;
        std::vector< vaTT_Trackee<TTTagType> * >            m_tracker_objects;
        mutex                                               m_tracker_objects_lock;
        bool                                                m_tracker_objects_lock_already_locked;

    public:
        typedef std::function<void( int newTrackeeIndex )>                                         TrackeeAddedCallbackType;
        typedef std::function<void( int toBeRemovedTrackeeIndex, int toBeReplacedByTrackeeIndex )> TrackeeBeforeRemovedCallbackType;

        TrackeeAddedCallbackType                            m_onAddedCallback;
        TrackeeBeforeRemovedCallbackType                    m_beforeRemovedCallback;

    public:
        vaTT_Tracker( ) : m_tracker_objects_lock_already_locked( false ) { }
        virtual ~vaTT_Tracker( );
        const std::vector< vaTT_Trackee<TTTagType> * > &    GetTrackedObjects( ) const          { return m_tracker_objects; };

        TTTagType                                           operator[]( std::size_t idx)        { return m_tracker_objects[idx]->m_tag; }
        const TTTagType                                     operator[]( std::size_t idx) const  { return m_tracker_objects[idx]->m_tag; }
        size_t                                              size( ) const                       { return m_tracker_objects.size();  }

        void                                                SetAddedCallback( const TrackeeAddedCallbackType & callback )                   { m_onAddedCallback   = callback; }
        void                                                SetBeforeRemovedCallback( const TrackeeBeforeRemovedCallbackType & callback )   { m_beforeRemovedCallback = callback; }
    };


    template< class TTTagType >
    class vaTT_Trackee
    {
        typedef vaTT_Tracker<TTTagType>     vaTT_TrackerT;
        friend vaTT_TrackerT;
    private:
        vaTT_TrackerT *                     m_tracker;
        int                                 m_index;
        TTTagType const                     m_tag;

    public:
        vaTT_Trackee( vaTT_TrackerT * tracker, TTTagType tag )
            : m_tag( tag )
        {
            m_tracker = tracker;
            assert( tracker != nullptr );
            if( tracker == nullptr )
                return;

            std::unique_lock<mutex> trackerLock( m_tracker->m_tracker_objects_lock );
            
            // to warn on recursive locks. only for debugging. not exception safe.
            assert( !m_tracker->m_tracker_objects_lock_already_locked );
            m_tracker->m_tracker_objects_lock_already_locked = true; 


            m_tracker->m_tracker_objects.push_back( this );
            m_index = (int)m_tracker->m_tracker_objects.size( ) - 1;
            assert( m_index == (int)m_tracker->m_tracker_objects.size( ) - 1 );

            if( m_tracker->m_onAddedCallback != nullptr )
            {
                m_tracker->m_onAddedCallback( m_index );
            }

            // to warn on recursive locks. only for debugging. not exception safe.
            assert( m_tracker->m_tracker_objects_lock_already_locked );
            m_tracker->m_tracker_objects_lock_already_locked = false; 
        }
        virtual ~vaTT_Trackee( )
        {
            if( m_tracker == nullptr )
                return;

            std::unique_lock<mutex> trackerLock( m_tracker->m_tracker_objects_lock );
            
            // to warn on recursive locks. only for debugging. not exception safe.
            assert( !m_tracker->m_tracker_objects_lock_already_locked );
            m_tracker->m_tracker_objects_lock_already_locked = true; 

            int index = m_index;
            assert( this == m_tracker->m_tracker_objects[index] );

            // not last one? move the last one to our place and update its index
            if( index < ( (int)m_tracker->m_tracker_objects.size( ) - 1 ) )
            {
                int replacedByIndex = (int)m_tracker->m_tracker_objects.size( ) - 1;
                if( m_tracker->m_beforeRemovedCallback != nullptr )
                {
                    m_tracker->m_beforeRemovedCallback( m_index, replacedByIndex );
                }
                m_tracker->m_tracker_objects[index] = m_tracker->m_tracker_objects[ replacedByIndex ];
                m_tracker->m_tracker_objects[index]->m_index = index;
            }
            else
            {
                if( m_tracker->m_beforeRemovedCallback != nullptr )
                {
                    m_tracker->m_beforeRemovedCallback( m_index, -1 );
                }
            }
            m_tracker->m_tracker_objects.pop_back( );

            // to warn on recursive locks. only for debugging. not exception safe.
            assert( m_tracker->m_tracker_objects_lock_already_locked );
            m_tracker->m_tracker_objects_lock_already_locked = false; 
        }

    public:
        const vaTT_TrackerT *           TT_GetTracker( ) const { return m_tracker; };
        TTTagType                       TT_GetTag( ) const     { return m_tag; }
        // int                             GetIndex( ) const   { return m_index; } <- it changes at runtime; not sure anyone wants to change it
    };


    template< class TTTagType >
    inline vaTT_Tracker<TTTagType>::~vaTT_Tracker( )
    {
        for( int i = 0; i < m_tracker_objects.size( ); i++ )
            m_tracker_objects[i]->m_tracker = nullptr;
    }

    // Simple bounded key/value searchable circular buffer used for caching stuff. This has only the functionality that was needed so far.
    template< typename _Key, typename _Element, uint32 _Size >
    class vaCircularCache
    {
        std::array<_Key, _Size>         m_keys;
        std::array<_Element, _Size>     m_elements;

        uint32                          m_count       = 0;
        uint32                          m_last        = (uint32)(_Size-1);

    public:
        _Element *                      Find( const _Key & key )
        {
            for( uint32 i = 0; i < m_count; i++ )
            {
                int index = ( _Size + m_last - i ) % _Size;
                if( m_keys[index] == key )
                    return &m_elements[index];
            }
            return nullptr;
        }

        const _Element *                      Find( const _Key & key ) const
        {
            for( uint32 i = 0; i < m_count; i++ )
            {
                int index = ( _Size + m_last - i ) % _Size;
                if( m_keys[index] == key )
                    return &m_elements[index];
            }
            return nullptr;
        }

        _Element *                      Insert( const _Key & key )
        {
            // insert into circular buffer
            m_last              = ( m_last + 1 ) % _Size;
            m_count             = std::min( m_count + 1, _Size );
            m_keys[m_last]      = key;
            return &m_elements[m_last];
        }

        void                            Reset( )
        {
            m_count     = 0;
            m_last      = (uint32)(_Size-1);
        }

        void                            Reset( const _Key & nullKey, const _Element & nullElement )
        {
            for( uint32 i = 0; i < m_count; i++ )
            {
                m_keys[i]     = nullKey;
                m_elements[i] = nullElement;
            }
            Reset( );
        }
    };

    template<typename _Key> 
    struct vaMurmurPtrHasher
    {
        size_t operator()( const _Key & _key ) const
        {
            uint64 key = reinterpret_cast< uint64 >( static_cast<const void*>( _key ) );
            key ^= ( key >> 33 );
            key *= 0xff51afd7ed558ccd;
            key ^= ( key >> 33 );
            key *= 0xc4ceb9fe1a85ec53;
            key ^= ( key >> 33 );
            return key;
        }
    };

    // Simple hashed bounded key/value searchable circular buffer used for caching stuff. This has only the functionality that was needed so far.
    template< typename _Key, typename _Element, uint32 _Size, uint32 _Buckets, class _Hasher = std::hash<_Key> >
    class vaHashedCircularCache
    {
        std::array< vaCircularCache<_Key, _Element, _Size>, _Buckets >
                                        m_buckets;
        _Hasher                         m_hasher;

    private:
        uint32                          Index( const _Key & key ) const
        {
            return (uint32)(m_hasher( key )) % _Buckets;
        }

    public:
        const _Element *                Find( const _Key & key ) const
        {
            return m_buckets[ Index(key) ].Find( key );
        }

        const _Element *                Insert( const _Key & key, const _Element & element )
        {
            _Element* ret = m_buckets[Index( key )].Insert( key );
            *ret = element;
            return ret;
        }

        const _Element *                Insert( const _Key & key, _Element && element )
        {
            _Element * ret = m_buckets[ Index(key) ].Insert( key );
            *ret = std::move(element);
            return ret;
        }

        _Element *                      Insert( const _Key & key )
        {
            return m_buckets[Index( key )].Insert( key );
        }

        void                            Reset( )
        {
            for( uint32 i = 0; i < _Buckets; i++ )
                m_buckets[i].Reset( );
        }

        void                            Reset( const _Key & nullKey, const _Element & nullElement )
        {
            for( uint32 i = 0; i < _Buckets; i++ )
                m_buckets[i].Reset( nullKey, nullElement );
        }
    };

    // 18 Dec 2020 - this was knocked up quickly at the end of the day; it >seems< to work but could be buggy. Sorry.
    // Use examples:
    //  - Iterating over the packed array:
    //    > for( uint32 i : container.PackedArray() )
    //    >     container.At(i)->SetInputsDirty();
    //  - Iterating over the packed array in reverse order:
    //    > for( int32 i = (int32)container.PackedArray().size()-1; i>=0; i-- )
    //    >     container.At(container.PackedArray()[i])->UIDObject_Untrack();
    template< typename ElementType >
    class vaSparseArray
    {
        static constexpr uint32         c_invalidIndex                      = 0xFFFFFFFF;
        static constexpr uint32         c_unusedBit                         = (1UL << 31);

        std::vector<ElementType>        m_sparseArray;
        std::vector<uint32>             m_sparseDualPurposeList;            // serves two purposes: if sparse array index in use, holds packed array index (and c_unusedBit is not set); if not in use, c_unusedBit is set and the rest points to next empty sparse array index
        std::vector<uint32>             m_packedArray;

        uint32                          m_nextFree                          = c_invalidIndex;

    public:
        vaSparseArray( )                { }
        ~vaSparseArray( )               { Clear(); }

        // sparse array size
        uint32                          Size( ) const                       { return (uint32)m_sparseArray.size();  }
        
        // number of elements in sparse array (excluding empty spaces)
        uint32                          Count( ) const                      { return (uint32)m_packedArray.size(); }
        // is element in sparse array? (doesn't bound check)
        bool                            Has( uint32 sparseIndex ) const     { return (m_sparseDualPurposeList[sparseIndex] & c_unusedBit) == 0; }
        // access element in sparse array, const
        const ElementType &             At( uint32 sparseIndex ) const      { assert( Has(sparseIndex) ); return m_sparseArray[sparseIndex]; }
        // access element in sparse array
        ElementType &                   At( uint32 sparseIndex )            { assert( Has(sparseIndex) ); return m_sparseArray[sparseIndex]; }

        // sometimes you want to iterate through all elements skipping any holes
        const std::vector<uint32> &     PackedArray( ) const                { return m_packedArray; }

        // inserts the new value to the sparse array (and adds tracking with the packed array) and returns its sparse index - access with At()
        uint32                          Insert( const ElementType & value ) 
        { 
            uint32 retVal;
            if( m_nextFree == c_invalidIndex )  // no unused sparse indices - add new
            {
                // store value
                m_sparseArray.push_back( value );
                // get sparse index to return
                retVal = (uint32)m_sparseArray.size()-1;
                // store sparse index to packed array
                m_packedArray.push_back( retVal );
                // store packed array index to the dual purpose list - used for fast removal
                m_sparseDualPurposeList.push_back( (uint32)m_packedArray.size()-1 );
            }
            else
            {
                // reuse unused sparse index and advance m_nextFree head if pointing to unused
                retVal = m_nextFree & ~c_unusedBit;
                m_nextFree = m_sparseDualPurposeList[retVal];
                // write value to reused index
                m_sparseArray[retVal] = value;
                // store sparse index to packed array
                m_packedArray.push_back( retVal );
                // store packed array index to the dual purpose list - used for fast removal
                m_sparseDualPurposeList[retVal] = (uint32)m_packedArray.size()-1;
            }
            return retVal;
        }

        void                            Remove( uint32 sparseIndex )
        {
            assert( Has( sparseIndex ) );
            uint32 packedIndex = m_sparseDualPurposeList[sparseIndex];
            
            // remove from packed array; if not last, swap with last and patch the last's index to its new location
            if( packedIndex != m_packedArray.size()-1 )
            {
                assert( m_sparseDualPurposeList[ m_packedArray.back() ] == m_packedArray.size()-1 );
                m_sparseDualPurposeList[ m_packedArray.back() ] = packedIndex;
                std::swap( m_packedArray.back(), m_packedArray[packedIndex] );
            }
            m_packedArray.pop_back();
            
            // store the current 'next free' (which could be c_invalidIndex) to the newly freed sparseIndex location
            m_sparseDualPurposeList[sparseIndex] = m_nextFree;
            // set m_nextFree head to the newly freed
            m_nextFree = sparseIndex | c_unusedBit;
        }

        void                            Clear( )
        {
#ifdef _DEBUG
            assert( m_sparseArray.size() == m_sparseDualPurposeList.size() );
            uint32 unusedCount = 0;
            uint32 nextFree = m_nextFree;
            while( nextFree != c_invalidIndex )
            {
                unusedCount++;
                nextFree = m_sparseDualPurposeList[nextFree & ~c_unusedBit];
            }
            assert( unusedCount == (m_sparseArray.size() - m_packedArray.size()) );
#endif
            m_sparseArray.clear();
            m_sparseDualPurposeList.clear();
            m_packedArray.clear();
            m_nextFree = c_invalidIndex;
        }
    };

}
