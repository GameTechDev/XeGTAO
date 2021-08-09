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

// temporary include-all until I figure out what exactly to do with this...

#include <assert.h>

#include <string>
#include <vector>
//#include <list>
//#include <array>
////#include <allocators>
////#include <deque>
#include <memory>
////#include <algorithm>
//#include <map>
//#include <functional>
//#include <queue>
#include <atomic>
//#include <thread>        
#include <mutex>  
//#include <shared_mutex>  
//#include <condition_variable>
//#include <limits>
//#include <variant>

namespace Vanilla
{
    // typedef-ing to Vanilla so we can switch to custom lib / allocator more easily later

    using std::string;
    using std::string_view;
    using std::wstring;

    // using std::vector;
    // using std::deque;
    // using std::queue;
    using std::shared_ptr;
    //using std::make_shared;
    using std::unique_ptr;
    using std::weak_ptr;
    using std::pair;
    // using std::map;

    using atomic_bool            = std::atomic_bool;
    using atomic_sbyte           = std::atomic<std::int8_t>   ;
    using atomic_byte            = std::atomic<std::uint8_t>  ;
    using atomic_int16           = std::atomic<std::int16_t>  ;
    using atomic_uint16          = std::atomic<std::uint16_t> ;
    using atomic_int32           = std::atomic<std::int32_t>  ;
    using atomic_uint32          = std::atomic<std::uint32_t> ;
    using atomic_int64           = std::atomic<std::int64_t>  ;
    using atomic_uint64          = std::atomic<std::uint64_t> ;

    // shrug :)
    template<typename T> T & dont_move( T && t ) { return t; }

    // returns index of found & removed item or -1 if not found
    template<class _Ty, class _Alloc = std::allocator<_Ty>>
    inline int vector_find_and_remove( std::vector<_Ty> & list, const _Ty & value )
    {
        for( int i = 0; i < list.size(); i++ )
        {
            if( list[i] == value )
            {
                if( i < ( list.size( ) - 1 ) )
                    list[i] = list.back();
                list.pop_back();
                return i;
            }
        }
        return -1;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // TODO: (FS, 19/04/2020): this threading stuff below is obsolete and needs to be removed
    //
    // This is a wrapper around mutex to allow for assert_locked_by_caller (based on idea from https://stackoverflow.com/questions/21892934/how-to-assert-if-a-stdmutex-is-locked).
    // Otherwise mostly compatible with std::mutex so easy to go back/forth - but in _DEBUG it inherits std::mutex with protected so to avoid any unique_lock<std::mutex> on it
    // so that it doesn't skip over the debug functionality.
#ifdef _DEBUG
    class mutex : protected std::mutex
#else
    class mutex : public std::mutex
#endif
    {
    private:
#ifdef _DEBUG
        std::atomic<std::thread::id>    m_holder;
#endif
    public:
#ifdef _DEBUG
        void lock( )
        {
            std::mutex::lock( );
            m_holder = std::this_thread::get_id( );
        }
        bool try_lock( )
        {
            bool locked = std::mutex::try_lock( );
            if( locked )
                m_holder = std::this_thread::get_id( );
            return locked;
        }
        void unlock( )
        {
            m_holder = std::thread::id( );
            std::mutex::unlock( );
        }
#endif

#ifdef _DEBUG
        bool locked_by_caller( ) const
        {
            return m_holder == std::this_thread::get_id( );
        }
#endif
        void assert_locked_by_caller( ) const
        {
#ifdef _DEBUG
            assert( locked_by_caller() );
#endif
        }
    };

    // this is a wrapper around recursive_mutex and adds few things for asserting
    // TODO: modify it to look like the above (std-style) and check for correctness
    class vaRecursiveMutex : private std::recursive_mutex
    {
#ifdef _DEBUG
        int m_lockDepth;
#endif
    public:
#ifdef _DEBUG
        vaRecursiveMutex( ) : m_lockDepth( 0 ) { }
        ~vaRecursiveMutex( ) { this->lock(); assert( m_lockDepth == 1 ); this->unlock(); }

	    void lock()
		{	
            std::recursive_mutex::lock();
            m_lockDepth++;
            assert( m_lockDepth > 0 );
        }

	    void unlock()
		{	
            m_lockDepth--;
            assert( m_lockDepth >= 0 );
            std::recursive_mutex::unlock();
        }

	    bool try_lock() noexcept
		{	
            bool ret = (std::recursive_mutex::try_lock());
            if( ret )
                m_lockDepth++;
		    return ret;
		}

        // just a safety to make sure this mutex was not locked somewhere higher up in the stack
        void assertNotRecursivelyLocked( )
        {
            lock( );
            assert( m_lockDepth == 1 );
            unlock();
        }

        // just a safety to make sure this mutex was locked somewhere higher up in the stack
        void assertRecursivelyLocked( )
        {
            lock( );
            assert( m_lockDepth > 1 );
            unlock();
        }
#else
        vaRecursiveMutex( ) { }
        ~vaRecursiveMutex( ) {  }

        void lock()                         { std::recursive_mutex::lock();             }
        void unlock()                       { std::recursive_mutex::unlock();           }
        bool try_lock() noexcept            { return std::recursive_mutex::try_lock();  }
        void assertNotRecursivelyLocked( )  { }
        void assertRecursivelyLocked( )     { }
#endif

	    vaRecursiveMutex(const vaRecursiveMutex&) = delete;
	    vaRecursiveMutex& operator=(const vaRecursiveMutex&) = delete;
	};

    // this needs to go out
   using vaRecursiveMutexScopeLock  = std::lock_guard<vaRecursiveMutex>;

#define VA_RECURSIVE_MUTEX_SCOPE_LOCK( name )             vaRecursiveMutexScopeLock VA_COMBINE(localscope,__LINE__)( name )


   //////////////////////////////////////////////////////////////////////////
   // see https://stackoverflow.com/questions/14939190/boost-shared-from-this-and-multiple-inheritance or https://stackoverflow.com/questions/15549722/double-inheritance-of-enable-shared-from-this
   template<typename T>
   struct enable_shared_from_this_virtual;
   //
   class enable_shared_from_this_virtual_base : public std::enable_shared_from_this<enable_shared_from_this_virtual_base>
   {
       typedef std::enable_shared_from_this<enable_shared_from_this_virtual_base> base_type;
       template<typename T>
       friend struct enable_shared_from_this_virtual;

       std::shared_ptr<enable_shared_from_this_virtual_base> shared_from_this( )
       {
           return base_type::shared_from_this( );
       }
       std::shared_ptr<enable_shared_from_this_virtual_base const> shared_from_this( ) const
       {
           return base_type::shared_from_this( );
       }
   };
   //
   template<typename T>
   struct enable_shared_from_this_virtual : virtual enable_shared_from_this_virtual_base
   {
       typedef enable_shared_from_this_virtual_base base_type;

   public:
       std::shared_ptr<T> shared_from_this( )
       {
           std::shared_ptr<T> result( base_type::shared_from_this( ), static_cast<T*>( this ) );
           return result;
       }

       std::shared_ptr<T const> shared_from_this( ) const
       {
           std::shared_ptr<T const> result( base_type::shared_from_this( ), static_cast<T const*>( this ) );
           return result;
       }
   };
   //////////////////////////////////////////////////////////////////////////
}