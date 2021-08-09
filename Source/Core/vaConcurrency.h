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

#include <memory>
#include <shared_mutex>
#include <unordered_set>

#pragma warning(disable : 4324)

// 0 - use just shared_ptr, 1 - use my lc_shared_mutex; 2 - use AlexeyAB's contention free mutex (doesn't fully work at the moment because there's no try_lock_shared)
#define VA_LC_MUTEX             1

namespace Vanilla
{
    constexpr int VA_ALIGN_PAD = std::hardware_destructive_interference_size;

    // this is going to replace vaThreading over time
    class vaConcurrency sealed
    {
    public:
        static uint32                           ThreadHash( );
    };

    // I don't know what this should be called, I'm sure I'm reinventing the wheel, but it's a low contention atomic tool 
    // for fast summing up or incrementing or decrementing or whatever values from many threads. Fast to write,
    // slow to read (adjustable with BlockCount parameter).
    // TODO: most if it is unimplemented yet!
    template< typename CounterType, int BlockCount = 17 > // 3, 5, 9, 17, 31, 61, 101...
    class lc_atomic_counter
    {
        struct Block
        {
            alignas( VA_ALIGN_PAD ) char    Padding[VA_ALIGN_PAD];
            alignas( VA_ALIGN_PAD ) std::atomic<CounterType> 
                                            Value;
        };
        Block                        m_blocks[BlockCount];
        alignas( VA_ALIGN_PAD ) char m_padding[VA_ALIGN_PAD];

    public:
        lc_atomic_counter( )                                        { reset( 0 );           }
        lc_atomic_counter( CounterType initialValue )               { reset(initialValue);  }

        void                        reset( CounterType value )      { for( int i = 0; i < countof( m_blocks ); i++ ) m_blocks[i].Value.store( value, std::memory_order_release ); }

        void                        store( CounterType value )      { m_blocks[thread_index()].Value.store( value, std::memory_order_release ); }
        //void                        add( CounterType value )      // not implemented yet

        CounterType                 highest( ) const
        {
            CounterType maxVal = m_blocks[0].Value;
            for( int i = 1; i < countof( m_blocks ); i++ ) 
                maxVal = std::max( maxVal, m_blocks[i].Value.load( std::memory_order_acquire ) );
            return maxVal;
        }


    private:
        int         thread_index( )
        {
            thread_local static int threadIndex = vaConcurrency::ThreadHash() % BlockCount;
            return threadIndex;
        }
    };

    template< typename Type >
    struct vaPaddedObject
    {
        alignas( VA_ALIGN_PAD ) char        m_padding0[VA_ALIGN_PAD];
        alignas( VA_ALIGN_PAD ) Type        m_object;
        alignas( VA_ALIGN_PAD ) char        m_padding1[VA_ALIGN_PAD];

        operator Type & ( )         { return m_object; }
        Type &          Get( )      { return m_object; }
    };

    // Simple append list designed for low contention insertion from many threads
    // It can be in two states: appending (new items can get added) or consuming (added items can be read). 
    // StartAppending()/StartConsuming() are used to switch between states and going from 'appending' to 
    // 'consuming' will collate and prepare data for reading, while going from 'consuming' to 'appending' 
    // will empty the list and prepare for appending anew.
    //
    // While in appending (!IsConsuming) state, only Append and Clear are allowed.
    // While in consuming (IsConsuming) state, only Count, [] access operator and GetItemsUnsafe are allowed.
    //
    // This container is unbound (will grow as needed) - Clear will remove all allocated memory.
    template< typename ElementType, int MaxThreadsSupported = 128, int BlockElementCount = 384 >
    class vaAppendConsumeList
    {
        struct LocalBlock
        {
            int                         Counter         = 0;
            ElementType                 Data[BlockElementCount];
        };

        std::atomic_bool            m_consuming         = false;

        std::mutex                  m_transitionMutex;
        std::mutex                  m_masterListMutex;
        std::vector<ElementType>    m_masterList;

        static constexpr int        c_localBlockCount   = MaxThreadsSupported;
        std::array<LocalBlock, c_localBlockCount>    
                                    m_localBlocks;

    public:
        vaAppendConsumeList( )  noexcept    { }
        ~vaAppendConsumeList( ) noexcept    { }

        // for debugging/asserting
        bool                        IsConsuming( ) const noexcept                       { return m_consuming; }

        // returns true if state changed
        bool                        StartAppending( ) noexcept                          { return Transition( false ); }
        bool                        StartConsuming( ) noexcept                          { return Transition( true ); }

        size_t                      Count( ) const noexcept
        { 
            assert( m_consuming ); 
            if( m_consuming.load() ) 
                return m_masterList.size(); 
            else
                return 0;
        }

        std::pair<const ElementType*, size_t>      
                                    GetItemsUnsafe( ) const
        {
            assert( m_consuming );
            return { m_masterList.data(), m_masterList.size() };
        }

        std::vector<ElementType> &
                                    GetVectorUnsafe( )
        {
            assert( m_consuming );
            return  m_masterList;
        }

        ElementType & operator [] ( size_t index ) noexcept
        {
            assert( m_consuming );
            return m_masterList[index];
        }

        const ElementType & operator [] ( size_t index ) const noexcept
        {
            assert( m_consuming );
            return m_masterList[index];
        }

        void                        Append( const ElementType & element ) noexcept
        {
            Append( std::move(ElementType{element}) );
        }
        void                        Append( ElementType && element ) noexcept
        {
            assert( !m_consuming.load() );
            LocalBlock * block = GetLocalBlock();
            assert( block );

            // if full, first commit
            if( block->Counter == BlockElementCount )
                CommitInner( *block );

            block->Data[block->Counter++] = std::move(element);
        }

        // This one ignores the thread local storage and append a batch of elements directly to the main storage, with the lock
        void                        AppendBatch( const ElementType * elements, const int arrayCount ) noexcept
        {
            assert( !m_consuming.load( ) );

            std::unique_lock lock( m_masterListMutex );
            for( int i = 0; i < arrayCount; i++ )
                m_masterList.emplace_back( /*std::move*/( elements[i] ) );
        }

        void                        Clear( )
        {
            //assert( m_consuming.load( ) );
            std::unique_lock transitionLock( m_transitionMutex );
            std::unique_lock masterLock( m_masterListMutex );
            for( int i = 0; i < c_localBlockCount; i++ )
                CommitOuter( m_localBlocks[i] );
            m_masterList.clear(); // std::swap( m_masterList, std::vector<ElementType>{} );
        }

        vaAppendConsumeList( const vaAppendConsumeList & )              = delete;
        vaAppendConsumeList& operator=( const vaAppendConsumeList& )    = delete;

    private:

        bool                        Transition( bool consuming ) noexcept
        {
            std::unique_lock transitionLock( m_transitionMutex );

            bool prevVal = m_consuming.exchange( consuming );
            if( prevVal == consuming )
                return false;     // transitioning into the same state - this used to be an assert but I've relaxed this for simplicity

            if( !prevVal )  // transitioning from appending to consuming
            {
                std::unique_lock masterLock( m_masterListMutex );
                for( int i = 0; i < c_localBlockCount; i++ )
                    CommitOuter( m_localBlocks[i] );
            }
            else
            {
                // transitioning from consuming to appending
                std::unique_lock masterLock( m_masterListMutex );
                m_masterList.clear( );
            }
            return true;
        }

        void                        CommitInner( LocalBlock & block ) noexcept
        {
            std::unique_lock lock( m_masterListMutex );
            for( int i = 0; i < BlockElementCount; i++ )
                m_masterList.emplace_back( std::move( block.Data[i] ) );
            block.Counter = 0;
        }

        void                        CommitOuter( LocalBlock & block ) noexcept
        {
            for( int i = 0; i < block.Counter; i++ )
                m_masterList.emplace_back( std::move(block.Data[i]) );
            block.Counter = 0;
        }

        LocalBlock *    GetLocalBlock( ) noexcept
        {
            static atomic_uint32    s_threadIndexCounter = 0;
            thread_local static int s_threadIndex = s_threadIndexCounter.fetch_add( 1 ); // std::hash<std::thread::id>( )( std::this_thread::get_id( ) ) % c_mutexCount;
            if( s_threadIndex < c_localBlockCount )
            {
                return &m_localBlocks[s_threadIndex];
            }
            else 
            {
                // this is catastrophic - you need to either get more 'MaxThreadsSupported' or figure out what is exhausting the storage (non-task pool threads??)
                assert( false );
                abort();            
                // return nullptr;
            }
        }
    };

    // Very similar to the vaAppendConsumeList, except it holds unique elements using an unordered_set
    template< typename ElementType, int MaxThreadsSupported = 128, int BlockElementCount = 128 >
    class vaAppendConsumeSet
    {
        struct LocalBlock
        {
            std::unordered_set<ElementType> Data;
        };

        std::atomic_bool            m_consuming         = false;

        std::mutex                      m_transitionMutex;
        std::mutex                      m_masterSetMutex;
        std::unordered_set<ElementType> m_masterSet;

        static constexpr int            c_localBlockCount   = MaxThreadsSupported;
        std::array<LocalBlock, c_localBlockCount>    
                                        m_localBlocks;

    public:
        vaAppendConsumeSet( )  noexcept    { }
        ~vaAppendConsumeSet( ) noexcept    { }
        vaAppendConsumeSet( const vaAppendConsumeSet & )              = delete;
        vaAppendConsumeSet& operator=( const vaAppendConsumeSet& )    = delete;

        // for debugging/asserting
        bool                        IsConsuming( ) const noexcept                       { return m_consuming; }

        // returns true if state changed
        bool                        StartAppending( ) noexcept                          { return Transition( false ); }
        bool                        StartConsuming( ) noexcept                          { return Transition( true ); }

        const std::unordered_set<ElementType> &
                                    Elements( ) const noexcept                          { assert( m_consuming ); return  m_masterSet; }

        void                        Insert( const ElementType & element ) noexcept
        {
            assert( !m_consuming.load() );
            LocalBlock * block = GetLocalBlock();
            assert( block );

            // if full, first commit
            if( block->Data.size() == BlockElementCount )
                CommitInner( *block );

            block->Data.insert( element );
        }

        void                        Clear( )
        {
            //assert( m_consuming.load( ) );
            std::unique_lock transitionLock( m_transitionMutex );
            std::unique_lock masterLock( m_masterSetMutex );
            for( int i = 0; i < c_localBlockCount; i++ )
                CommitOuter( m_localBlocks[i] );
            m_masterSet.clear();// std::swap( m_masterSet, std::unordered_set<ElementType>{} );
        }

    private:

        bool                        Transition( bool consuming ) noexcept
        {
            std::unique_lock transitionLock( m_transitionMutex );

            bool prevVal = m_consuming.exchange( consuming );
            if( prevVal == consuming )
                return false;     // transitioning into the same state - this used to be an assert but I've relaxed this for simplicity

            if( !prevVal )  // transitioning from appending to consuming
            {
                std::unique_lock masterLock( m_masterSetMutex );
                for( int i = 0; i < c_localBlockCount; i++ )
                    CommitOuter( m_localBlocks[i] );
            }
            else
            {
                // transitioning from consuming to appending
                std::unique_lock masterLock( m_masterSetMutex );
                m_masterSet.clear( );
            }
            return true;
        }

        void                        CommitInner( LocalBlock & block ) noexcept
        {
            std::unique_lock lock( m_masterSetMutex );
            CommitOuter( block );
        }

        void                        CommitOuter( LocalBlock & block ) noexcept
        {
            m_masterSet.merge( block.Data );
            block.Data.clear( );
        }

        LocalBlock *    GetLocalBlock( ) noexcept
        {
            static atomic_uint32    s_threadIndexCounter = 0;
            thread_local static int s_threadIndex = s_threadIndexCounter.fetch_add( 1 ); // std::hash<std::thread::id>( )( std::this_thread::get_id( ) ) % c_mutexCount;
            if( s_threadIndex < c_localBlockCount )
            {
                return &m_localBlocks[s_threadIndex];
            }
            else 
            {
                // this is catastrophic - you need to either get more 'MaxThreadsSupported' or figure out what is exhausting the storage (non-task pool threads??)
                assert( false );
                abort();            
                // return nullptr;
            }
        }
    };

    inline uint32 vaConcurrency::ThreadHash( )
    {
#if 1
        static atomic_uint32    s_threadIndexCounter = 0;
        thread_local static uint32 s_threadHash = (uint32)std::hash<unsigned int>()( s_threadIndexCounter.fetch_add( 1 ) );
#else
        thread_local static uint32 s_threadHash = (uint32)(std::hash<std::thread::id>( )( std::this_thread::get_id( ) ));
#endif
        return s_threadHash;
    }


#if VA_LC_MUTEX == 0

    template< int MutexCount = 31 > // 17, 31, 61, 101...
    class lc_shared_mutex : public std::shared_mutex    { };

#elif VA_LC_MUTEX == 1

    // low contention version of shared_mutex - for use where actual unique (read-write) locks are very rare 
    template< int MutexCount = 31 > // 17, 31, 61, 101...
    class lc_shared_mutex
    {
        static constexpr int                    c_mutexCount = MutexCount;

        struct SM
        {
            alignas( VA_ALIGN_PAD ) char              Padding[256];
            alignas( VA_ALIGN_PAD ) std::shared_mutex M;
        };

        alignas( VA_ALIGN_PAD ) SM                m_mutexes[c_mutexCount];
        alignas( VA_ALIGN_PAD ) char              m_padding2[256];

    public:
        lc_shared_mutex( ) noexcept {}

        ~lc_shared_mutex( ) noexcept {}

        void lock( ) noexcept                   // lock exclusive
        {
            for( int i = 0; i < c_mutexCount; i++ )
                m_mutexes[i].M.lock( );
        }

        _NODISCARD bool try_lock( ) noexcept    // try to lock exclusive
        {
            for( int i = 0; i < c_mutexCount; i++ )
            {
                // if any can't be locked, go back unlock all that we already locked and return false
                if( !m_mutexes[i].M.try_lock( ) )
                {
                    for( int j = i - 1; j >= 0; j-- )
                        m_mutexes[j].M.unlock( );
                }
            }
            return true;
        }

        void unlock( ) noexcept                 // unlock exclusive
        {
            for( int i = 0; i < c_mutexCount; i++ )
                m_mutexes[i].M.unlock( );
        }

        void lock_shared( ) noexcept            // lock non-exclusive
        {
            m_mutexes[thread_index( )].M.lock_shared( );
        }

        _NODISCARD bool try_lock_shared( ) noexcept // try to lock non-exclusive
        {
            return m_mutexes[thread_index( )].M.try_lock_shared( );;
        }

        void unlock_shared( ) noexcept          // unlock non-exclusive
        {
            m_mutexes[thread_index( )].M.unlock_shared( );
        }

        // there is no native_handle for this one!
        //_NODISCARD native_handle_type native_handle( ) noexcept /* strengthened */ { // get native handle
        //    return &_Myhandle;
        //}

        lc_shared_mutex( const lc_shared_mutex& ) = delete;
        lc_shared_mutex& operator=( const lc_shared_mutex& ) = delete;

    private:
        int         thread_index( )
        {
            thread_local static int threadIndex = vaConcurrency::ThreadHash( ) % c_mutexCount;
            return threadIndex;
        }
    };

#elif VA_LC_MUTEX == 2

    // from https://github.com/AlexeyAB/object_threadsafe - licensed under Apache-2

    // contention free shared mutex (same-lock-type is recursive for X->X, X->S or S->S locks), but (S->X - is UB)
    template<unsigned contention_free_count = 36, bool shared_flag = false>
    class contention_free_shared_mutex {
        std::atomic<bool> want_x_lock;
        struct cont_free_flag_t { alignas(std::hardware_destructive_interference_size*2) std::atomic<int> value; cont_free_flag_t() { value = 0; } }; // C++17
        //struct cont_free_flag_t { char tmp[60]; std::atomic<int> value; cont_free_flag_t( ) { value = 0; } };   // tmp[] to avoid false sharing
        typedef std::array<cont_free_flag_t, contention_free_count> array_slock_t;

        const std::shared_ptr<array_slock_t> shared_locks_array_ptr;  // 0 - unregistred, 1 registred & free, 2... - busy
        char avoid_falsesharing_1[64];

        array_slock_t& shared_locks_array;
        char avoid_falsesharing_2[64];

        int recursive_xlock_count;


        enum index_op_t { unregister_thread_op, get_index_op, register_thread_op };

#if (_WIN32 && _MSC_VER < 1900) // only for MSVS 2013
        typedef int64_t thread_id_t;
        std::atomic<thread_id_t> owner_thread_id;
        std::array<int64_t, contention_free_count> register_thread_array;
        int64_t get_fast_this_thread_id( ) {
            static __declspec( thread ) int64_t fast_this_thread_id = 0;  // MSVS 2013 thread_local partially supported - only POD
            if( fast_this_thread_id == 0 ) {
                std::stringstream ss;
                ss << std::this_thread::get_id( );   // https://connect.microsoft.com/VisualStudio/feedback/details/1558211
                fast_this_thread_id = std::stoll( ss.str( ) );
            }
            return fast_this_thread_id;
        }

        int get_or_set_index( index_op_t index_op = get_index_op, int set_index = -1 ) {
            if( index_op == get_index_op ) {  // get index
                auto const thread_id = get_fast_this_thread_id( );

                for( size_t i = 0; i < register_thread_array.size( ); ++i ) {
                    if( register_thread_array[i] == thread_id ) {
                        set_index = i;   // thread already registred                
                        break;
                    }
                }
            }
            else if( index_op == register_thread_op ) {  // register thread
                register_thread_array[set_index] = get_fast_this_thread_id( );
            }
            return set_index;
        }

#else
        typedef std::thread::id thread_id_t;
        std::atomic<std::thread::id> owner_thread_id;
        std::thread::id get_fast_this_thread_id( ) { return std::this_thread::get_id( ); }

        struct unregister_t {
            int thread_index;
            std::shared_ptr<array_slock_t> array_slock_ptr;
            unregister_t( int index, std::shared_ptr<array_slock_t> const& ptr ) : thread_index( index ), array_slock_ptr( ptr ) {}
            unregister_t( unregister_t&& src ) : thread_index( src.thread_index ), array_slock_ptr( std::move( src.array_slock_ptr ) ) {}
            ~unregister_t( ) { if( array_slock_ptr.use_count( ) > 0 ) ( *array_slock_ptr )[thread_index].value--; }
        };

        int get_or_set_index( index_op_t index_op = get_index_op, int set_index = -1 ) {
            thread_local static std::unordered_map<void*, unregister_t> thread_local_index_hashmap;
            // get thread index - in any cases
            {
                auto it = thread_local_index_hashmap.find( this );
                if( it != thread_local_index_hashmap.cend( ) )
                    set_index = it->second.thread_index;
            }

            if( index_op == unregister_thread_op ) {  // unregister thread
                if( shared_locks_array[set_index].value == 1 ) // if isn't shared_lock now
                    thread_local_index_hashmap.erase( this );
                else
                    return -1;
            }
            else if( index_op == register_thread_op ) {  // register thread
                thread_local_index_hashmap.emplace( this, unregister_t( set_index, shared_locks_array_ptr ) );

                // remove info about deleted contfree-mutexes
                for( auto it = thread_local_index_hashmap.begin( ), ite = thread_local_index_hashmap.end( ); it != ite;) {
                    if( it->second.array_slock_ptr->at( it->second.thread_index ).value < 0 )    // if contfree-mtx was deleted
                        it = thread_local_index_hashmap.erase( it );
                    else
                        ++it;
                }
            }
            return set_index;
        }

#endif

    public:
        contention_free_shared_mutex( ) :
            shared_locks_array_ptr( std::make_shared<array_slock_t>( ) ), shared_locks_array( *shared_locks_array_ptr ), want_x_lock( false ), recursive_xlock_count( 0 ),
            owner_thread_id( thread_id_t( ) ) {}

        ~contention_free_shared_mutex( ) {
            for( auto& i : shared_locks_array ) i.value = -1;
        }


        bool unregister_thread( ) { return get_or_set_index( unregister_thread_op ) >= 0; }

        int register_thread( ) {
            int cur_index = get_or_set_index( );

            if( cur_index == -1 ) {
                if( shared_locks_array_ptr.use_count( ) <= (int)shared_locks_array.size( ) )  // try once to register thread
                {
                    for( size_t i = 0; i < shared_locks_array.size( ); ++i ) {
                        int unregistred_value = 0;
                        if( shared_locks_array[i].value == 0 )
                            if( shared_locks_array[i].value.compare_exchange_strong( unregistred_value, 1 ) ) {
                                cur_index = (int)i;
                                get_or_set_index( register_thread_op, cur_index );   // thread registred success
                                break;
                            }
                    }
                    //std::cout << "\n thread_id = " << std::this_thread::get_id() << ", register_thread_index = " << cur_index <<
                    //    ", shared_locks_array[cur_index].value = " << shared_locks_array[cur_index].value << std::endl;
                }
            }
            return cur_index;
        }

        void lock_shared( ) {
            int const register_index = register_thread( );

            if( register_index >= 0 ) {
                int recursion_depth = shared_locks_array[register_index].value.load( std::memory_order_acquire );
                assert( recursion_depth >= 1 );

                if( recursion_depth > 1 )
                    shared_locks_array[register_index].value.store( recursion_depth + 1, std::memory_order_release ); // if recursive -> release
                else {
                    shared_locks_array[register_index].value.store( recursion_depth + 1, std::memory_order_seq_cst ); // if first -> sequential
                    while( want_x_lock.load( std::memory_order_seq_cst ) ) {
                        shared_locks_array[register_index].value.store( recursion_depth, std::memory_order_seq_cst );
                        for( volatile size_t i = 0; want_x_lock.load( std::memory_order_seq_cst ); ++i )
                            if( i % 100000 == 0 ) std::this_thread::yield( );
                        shared_locks_array[register_index].value.store( recursion_depth + 1, std::memory_order_seq_cst );
                    }
                }
                // (shared_locks_array[register_index] == 2 && want_x_lock == false) ||     // first shared lock
                // (shared_locks_array[register_index] > 2)                                 // recursive shared lock
            }
            else {
                if( owner_thread_id.load( std::memory_order_acquire ) != get_fast_this_thread_id( ) ) {
                    size_t i = 0;
                    for( bool flag = false; !want_x_lock.compare_exchange_weak( flag, true, std::memory_order_seq_cst ); flag = false )
                        if( ++i % 100000 == 0 ) std::this_thread::yield( );
                    owner_thread_id.store( get_fast_this_thread_id( ), std::memory_order_release );
                }
                ++recursive_xlock_count;
            }
        }

        void unlock_shared( ) {
            int const register_index = get_or_set_index( );

            if( register_index >= 0 ) {
                int const recursion_depth = shared_locks_array[register_index].value.load( std::memory_order_acquire );
                assert( recursion_depth > 1 );

                shared_locks_array[register_index].value.store( recursion_depth - 1, std::memory_order_release );
            }
            else {
                if( --recursive_xlock_count == 0 ) {
                    owner_thread_id.store( decltype( owner_thread_id )( ), std::memory_order_release );
                    want_x_lock.store( false, std::memory_order_release );
                }
            }
        }

        void lock( ) {
            // forbidden upgrade S-lock to X-lock - this is an excellent opportunity to get deadlock
            int const register_index = get_or_set_index( );
            if( register_index >= 0 )
                assert( shared_locks_array[register_index].value.load( std::memory_order_acquire ) == 1 );

            if( owner_thread_id.load( std::memory_order_acquire ) != get_fast_this_thread_id( ) ) {
                {
                    size_t i = 0;
                    for( bool flag = false; !want_x_lock.compare_exchange_weak( flag, true, std::memory_order_seq_cst ); flag = false )
                        if( ++i % 1000000 == 0 ) std::this_thread::yield( );
                }

                owner_thread_id.store( get_fast_this_thread_id( ), std::memory_order_release );

                for( auto& i : shared_locks_array )
                    while( i.value.load( std::memory_order_seq_cst ) > 1 );
            }

            ++recursive_xlock_count;
        }

        void unlock( ) {
            assert( recursive_xlock_count > 0 );
            if( --recursive_xlock_count == 0 ) {
                owner_thread_id.store( decltype( owner_thread_id )( ), std::memory_order_release );
                want_x_lock.store( false, std::memory_order_release );
            }
        }
    };

    template< int MutexCount = 31 >
    class lc_shared_mutex : public contention_free_shared_mutex<MutexCount> { };

#else

#error Unrecognized/missing VA_LC_MUTEX setting

#endif


}