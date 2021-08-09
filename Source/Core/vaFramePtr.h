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

#include "vaConcurrency.h"

#include <memory>
#include <vector>
#include <array>

#pragma warning(disable : 4324)

namespace Vanilla
{
    // Inspired by https://en.wikipedia.org/wiki/Hazard_pointer:
    // Creating a vaFramePtr from a shared_ptr will backup the shared_ptr to a temporary storage guaranteed to last
    // until the next call to the static vaFramePtr::NextFrame(). The frame_ptr will then only physically hold a 
    // raw pointer and a 'FrameID' counter. If the FrameID is less than a current frame counter, it means it's
    // potentially out of scope.

    // This avoids redundant insertions to the list, speeding up things significantly!
    class vaFramePtrTag
    {
        template< typename Type >
        friend class vaFramePtr;
        friend class vaFramePtrStatic;

        alignas(VA_ALIGN_PAD)  char                    m_fptPadding1[VA_ALIGN_PAD];
        alignas(VA_ALIGN_PAD)  std::atomic_uint64_t    m_fptLast = 0;
        alignas(VA_ALIGN_PAD)  char                    m_fptPadding2[VA_ALIGN_PAD];

    protected:
        // Is the hazard pointer currently (this frame) locked by someone?
        // This is intended only for asserting the case where for any reason it is unsafe to touch the object in
        // certain ways after a first hazard pointer was claimed in the frame; an example is a render mesh where
        // it's not safe to change it (other than skinning) once multithreaded processing has started, in order
        // to reduce costly locking / thread synchronization.
        bool                    FramePtr_MaybeActive( );
    };

    //
    class vaFramePtrStatic final
    {
        // friends
        template< typename Type >
        friend class vaFramePtr;
        friend class vaApplicationBase;
        friend class vaCore;

        // data
        struct DataBlock
        {
            alignas(VA_ALIGN_PAD)  char        Padding1[VA_ALIGN_PAD];
            alignas(VA_ALIGN_PAD)  std::mutex  Mutex;
            alignas(VA_ALIGN_PAD)  std::vector<std::shared_ptr<vaFramePtrTag>>      
                                                Pointers;
            alignas(VA_ALIGN_PAD)  char        Padding2[VA_ALIGN_PAD];
        };

        static constexpr int                                c_dataBlockCount = 47;  // this is purely to reduce contention between threads
        static alignas(VA_ALIGN_PAD) DataBlock              s_dataBlocks[c_dataBlockCount];
        static alignas(VA_ALIGN_PAD) std::atomic_uint64_t   s_frameCounter;
#ifdef _DEBUG
        static alignas(VA_ALIGN_PAD) std::atomic_bool       s_inNextFrame;
#endif

        // access
        static std::pair<DataBlock *, std::unique_lock<std::mutex>>        
                                    LockBlock( )
        {
            thread_local static int threadBlockIndex = vaConcurrency::ThreadHash() % c_dataBlockCount;

            DataBlock * block = &s_dataBlocks[threadBlockIndex];
#if 0 // self-aligning version
            if( block->Mutex.try_lock( ) )
                return {block, std::unique_lock( block->Mutex, std::adopt_lock ) };
            else
            {
                threadBlockIndex = (threadBlockIndex+1) % c_dataBlockCount;
                block = &s_dataBlocks[threadBlockIndex];
                return { block, std::unique_lock( block->Mutex ) };
            }
#else // static version that seems faster than the self-aligning one [shrug]
            return { block, std::unique_lock( block->Mutex ) };
#endif
        }

        static uint64               Insert( std::shared_ptr<vaFramePtrTag> && ref ) noexcept
        {
            assert( !s_inNextFrame );
            
            // this lock is to prevent race condition with other threads with overlapping hash ID that use the same bucket;
            // (it can also prevent catastrophic errors when NextFrame is called while this executes, which is disallowed and checked against
            // with the above assert; but this is not the main purpose of the lock and should never be relied upon)
            auto [block, lock] = LockBlock();

            uint64 currentFrame = CurrentFrame();
            // another thread could have updated m_fptLast in the meantime, in which case we don't have to do anything
            if( ref->m_fptLast.exchange( currentFrame /*, std::memory_order_acq_rel*/ ) != currentFrame )
                block->Pointers.push_back( std::move(ref) );
            return currentFrame;
        }

        // TODO: rename to InvalidateAndGC ?
        static void                 NextFrame( bool freeMemory = false ) noexcept
        {
#ifdef _DEBUG
            s_inNextFrame = true;
#endif

            // first lock all; we can do it in order because no one else locks any two at the same time but it'd be nice to be able
            // to lock them usim std::scoped_lock
            std::array< std::unique_lock<std::mutex>, c_dataBlockCount > locks;
            for( int i = 0; i < c_dataBlockCount; i++ )
                locks[i] = std::move(std::unique_lock<std::mutex>{s_dataBlocks[i].Mutex});


            // need to figure out how to use scoped_lock with an array
            // std::scoped_lock allLock( 
            //     s_dataBlocks[0].Mutex, s_dataBlocks[1].Mutex, s_dataBlocks[2].Mutex, s_dataBlocks[3].Mutex, 
            //     s_dataBlocks[4].Mutex, s_dataBlocks[5].Mutex, s_dataBlocks[6].Mutex, s_dataBlocks[7].Mutex,
            //     s_dataBlocks[8].Mutex, s_dataBlocks[9].Mutex, s_dataBlocks[10].Mutex, s_dataBlocks[11].Mutex, 
            //     s_dataBlocks[12].Mutex, s_dataBlocks[13].Mutex, s_dataBlocks[14].Mutex, s_dataBlocks[15].Mutex );

            // increase the frame counter
            uint64 prevCounter = s_frameCounter.fetch_add( 1, std::memory_order_acq_rel );
            uint64 currentCounter = prevCounter + 1;

            int totalCleaned = 0;
            for( int i = 0; i < c_dataBlockCount; i++ )
            {
                if( !freeMemory )
                {
                    auto & pointers = s_dataBlocks[i].Pointers;
#if 0
                    totalCleaned += (int)s_dataBlocks[i].Pointers.size( );
                    pointers.clear( ); currentCounter;
#else
                    for( int j = (int)pointers.size()-1; j >= 0; j-- )
                    {
                        // null ptr should have been disallowed at insertion time - this indicates a serious bug!
                        assert( pointers[j].use_count() != 0 );

                        // There should not be any skipping of frames - indicates a bug; we extend the 'hold' on 
                        // the pointer automatically below if use_count( ) == 1 but this has to happen each frame.
                        assert( pointers[j]->m_fptLast.load( std::memory_order_acquire ) == prevCounter );
                        
                        // Most likely only we are holding the ptr - release it (and in an unlikely case we're wrong and someone has just .lock-ed a 
                        // weak_ptr in another thread then it doesn't matter, the shared_ptr will get grabbed by vaFramePtr again next frame at a
                        // slight perf penalty.
                        if( pointers[j].use_count( ) == 1 )
                        {
                            if( j < (int)pointers.size( ) - 1 )
                                std::swap( pointers[j], pointers.back() );
                            pointers.pop_back( );
                            totalCleaned++;
                        }
                        else
                        {
                            pointers[j]->m_fptLast.store( currentCounter, std::memory_order_release ); // std::memory_order_relaxed?
                        }
                    }
#endif
                }
                else
                {
                    // Just clear it all
                    totalCleaned += (int)s_dataBlocks[i].Pointers.size( );
                    std::swap( s_dataBlocks[i].Pointers, std::vector<std::shared_ptr<vaFramePtrTag>>{} );
                }
            }
            // VA_WARN( "TotalCleaned: %d", totalCleaned );
#ifdef _DEBUG
            s_inNextFrame = false;
#endif
        }
    public:
        static uint64               CurrentFrame( ) noexcept
        {
            assert( !s_inNextFrame );
            return s_frameCounter.load( std::memory_order_acquire );
        }

        // this will advance frame and remove all held pointers - useful for ensuring objects that need to be destroyed before some systems (like textures before device) are gone 
        static void                 Cleanup( ) noexcept
        {
            assert( !s_inNextFrame );
            NextFrame( true );
        }
    };

#ifdef _DEBUG
#define VA_FRAME_PTR_DEBUG
#endif

    template< typename Type >
    class vaFramePtr final 
    {
        template< typename Type >
        friend class vaFramePtr;
    private:
        Type *                      m_rawPtr;
#ifdef VA_FRAME_PTR_DEBUG
        uint64                      m_rawPtrFrameID;
#endif

    public:

        vaFramePtr( ) noexcept
            : m_rawPtr( nullptr )
#ifdef VA_FRAME_PTR_DEBUG
            , m_rawPtrFrameID( 0 )  
#endif
        { }

#ifdef VA_FRAME_PTR_DEBUG            
        ~vaFramePtr( ) noexcept
        {
            // you must reset (at least with "ptr = vaFramePtr{};") before the frame goes out of scope or 
            // m_rawPtr is a potential dangling pointer!
            assert( m_rawPtr == nullptr || m_rawPtrFrameID == vaFramePtrStatic::CurrentFrame( ) );
        }
#endif

        // Copy constructor
        template< typename InType >//, std::enable_if_t< std::is_convertible_v<InType*, Type*> > >
        vaFramePtr( const vaFramePtr<InType> & other ) noexcept
        {
            m_rawPtr        = static_cast<Type*>( other.m_rawPtr );
#ifdef VA_FRAME_PTR_DEBUG
            m_rawPtrFrameID = other.m_rawPtrFrameID;
#endif
        }

        // = assignment operator
        template< typename InType >//, std::enable_if_t< std::is_convertible_v<InType*, Type*> > >
        vaFramePtr<Type> & operator = ( const vaFramePtr<InType> & other ) noexcept
        {
            m_rawPtr        = static_cast<Type*>(other.m_rawPtr);
#ifdef VA_FRAME_PTR_DEBUG
            m_rawPtrFrameID = other.m_rawPtrFrameID;
#endif
            return *this;
        }

        // Move constructor
        template< typename InType >//, typename = std::enable_if_t<std::is_convertible<InType*, Type*> > >
        vaFramePtr( vaFramePtr<InType> && other ) noexcept
        {
            m_rawPtr        = static_cast<Type*>( other.m_rawPtr );
#ifdef VA_FRAME_PTR_DEBUG
            m_rawPtrFrameID = other.m_rawPtrFrameID;
            other.Reset();
#endif
        }

        // = move operator
        template< typename InType >//, typename = std::enable_if_t<std::is_convertible_v<InType*, Type*> > >
        vaFramePtr<Type> operator = ( vaFramePtr<InType> && other ) noexcept
        {
            m_rawPtr        = static_cast<Type*>(other.m_rawPtr);
#ifdef VA_FRAME_PTR_DEBUG
            m_rawPtrFrameID = other.m_rawPtrFrameID;
            other.Reset();
#endif
            return *this;
        }

        // Initialize from shared_ptr!
        template< typename InType >
        vaFramePtr( const std::shared_ptr<InType> & smartPtr ) //, typename std::enable_if< std::is_convertible_v<InType*, Type*> >::type* = nullptr ) noexcept
        {
            SetFromSharedPtr( smartPtr );
        }

        // Set from shared_ptr!
        template< typename InType>// , std::enable_if_t< std::is_convertible_v<InType*, Type*> > >
        vaFramePtr<Type> & operator = ( const std::shared_ptr<InType> & smartPtr ) noexcept 
        {
            SetFromSharedPtr( smartPtr );
            return *this;
        }

        // Initialize from ptr that inherits enable_shared_from_this!
        template< typename InType >
        vaFramePtr( InType * ptr ) //, typename std::enable_if< std::is_convertible_v<InType*, Type*> >::type* = nullptr ) noexcept
        {
            SetFromPtrWithSharedFromThis( ptr );
        }

        // Set from ptr that inherits enable_shared_fromn_this!
        template< typename InType>// , std::enable_if_t< std::is_convertible_v<InType*, Type*> > >
        vaFramePtr<Type>& operator = ( InType * ptr ) noexcept
        {
            SetFromPtrWithSharedFromThis( ptr );
            return *this;
        }

        // Initialize to 0
        vaFramePtr( nullptr_t ) noexcept { Reset( ); }

        vaFramePtr<Type> & operator = ( nullptr_t ) noexcept
        {
            Reset( );
            return *this;
        }

        _NODISCARD Type * Get( ) const noexcept 
        {
#ifdef VA_FRAME_PTR_DEBUG
            if( m_rawPtr == nullptr || !Valid( ) )
                return nullptr;
#endif
            return m_rawPtr;
        }

        _NODISCARD Type* get( ) const noexcept          { return Get( ); }

        // _NODISCARD operator Type * ( ) const noexcept
        // {
        //     return Get( );
        // }

        template <class _Ty2 = Type, std::enable_if_t<!std::disjunction_v<std::is_array<_Ty2>, std::is_void<_Ty2>>, int> = 0>
        _NODISCARD _Ty2 & operator * ( ) const noexcept 
        {
            auto ret = Get( );
            assert( ret != nullptr );
            return *ret;
        }

        template <class _Ty2 = Type, std::enable_if_t<!std::is_array_v<_Ty2>, int> = 0>
        _NODISCARD _Ty2 * operator->( ) const noexcept 
        {
            auto ret = Get( );
            assert( ret != nullptr );
            return ret;
        }

#ifdef VA_FRAME_PTR_DEBUG
        bool                        Valid( ) const noexcept 
        {
            return m_rawPtrFrameID == vaFramePtrStatic::CurrentFrame( );
        }
#endif
        void                        Reset( ) noexcept 
        {
            m_rawPtr        = nullptr;
#ifdef VA_FRAME_PTR_DEBUG
            m_rawPtrFrameID = 0;
#endif
        }

        private:

        template< typename InType>// , std::enable_if_t< std::is_convertible_v<InType*, Type*> > >
        void SetFromSharedPtr( const std::shared_ptr<InType>& smartPtr ) noexcept
        {
            if( smartPtr == nullptr )
                Reset( );
            else
            {
                if constexpr( std::is_base_of_v< vaFramePtrTag, Type> )
                {
                    uint64 currentFrame = vaFramePtrStatic::CurrentFrame( );
                    vaFramePtrTag* tag = static_cast<vaFramePtrTag*>( smartPtr.get( ) );
                    if( tag->m_fptLast.load( /*std::memory_order_acquire*/ ) != currentFrame )	// this is just early out performance optimisation to avoid locking below in most cases
                    {
                    	// this will update the m_fptLast safely, in case NextFrame happens in the meantime and currentFrame is no longer valid
                        currentFrame = vaFramePtrStatic::Insert( std::shared_ptr<vaFramePtrTag>( smartPtr ) );
                    }
#ifdef VA_FRAME_PTR_DEBUG
                    m_rawPtrFrameID = currentFrame;
#endif
                    m_rawPtr = static_cast<Type*>( smartPtr.get( ) );
                }
                else
                {
                    static_assert( false, "This no longer works without vaFramePtrTag - the option was there before but is disabled because it's significantly less efficient" );
#ifdef VA_FRAME_PTR_DEBUG
                    m_rawPtrFrameID = vaFramePtrStatic::Insert( std::shared_ptr<void>( smartPtr ) );
#endif
                    m_rawPtr = static_cast<Type*>( smartPtr.get( ) );
                }
            }
        }

        template< typename InType>// , std::enable_if_t< std::is_convertible_v<InType*, Type*> > >
        void SetFromPtrWithSharedFromThis( InType* ptrWithSharedFromThis ) noexcept
        {
            if( ptrWithSharedFromThis == nullptr )
                Reset( );
            else
            {
                if constexpr( std::is_base_of_v< vaFramePtrTag, Type> ) 
                {
                    uint64 currentFrame = vaFramePtrStatic::CurrentFrame( );
                    vaFramePtrTag* tag = static_cast<vaFramePtrTag*>( ptrWithSharedFromThis );
                    if( tag->m_fptLast.load( /*std::memory_order_acquire*/ ) != currentFrame )	// this is just early out performance optimisation to avoid locking below in most cases
                    {
                        auto smartPtr = ptrWithSharedFromThis->weak_from_this( ).lock( );
                        if( smartPtr == nullptr )
                            { assert( false ); Reset( ); return; }
                    	// this will update the m_fptLast safely, in case NextFrame happens in the meantime and currentFrame is no longer valid
                        currentFrame = vaFramePtrStatic::Insert( std::shared_ptr<vaFramePtrTag>( smartPtr ) );
                    }
#ifdef VA_FRAME_PTR_DEBUG
                    m_rawPtrFrameID = currentFrame;
#endif
                    m_rawPtr = static_cast<Type*>( ptrWithSharedFromThis );
                    assert( m_rawPtr != nullptr );
                }
                else
                {
                    static_assert( false, "This no longer works without vaFramePtrTag - the option was there before but is disabled because it's significantly less efficient" );
#ifdef VA_FRAME_PTR_DEBUG
                    m_rawPtrFrameID = vaFramePtrStatic::Insert( std::shared_ptr<void>( smartPtr ) );
#endif
                    m_rawPtr = static_cast<Type*>( ptrWithSharedFromThis );
                }
            }
        }
    };

    inline bool vaFramePtrTag::FramePtr_MaybeActive( )
    {
        uint64 currentFrame = vaFramePtrStatic::CurrentFrame( );
        return m_fptLast.load( /*std::memory_order_acquire*/ ) < currentFrame;
    }


    inline alignas(VA_ALIGN_PAD) vaFramePtrStatic::DataBlock vaFramePtrStatic::s_dataBlocks[c_dataBlockCount];
    inline alignas(VA_ALIGN_PAD) std::atomic_uint64_t vaFramePtrStatic::s_frameCounter = 1;            // start from 1 so no vaFramePtr-s are Valid by default at any point
#ifdef _DEBUG
    inline alignas(VA_ALIGN_PAD) std::atomic_bool vaFramePtrStatic::s_inNextFrame = false;
#endif


    template <class _Ty1, class _Ty2>
    _NODISCARD bool operator==( const vaFramePtr<_Ty1>& _Left, const vaFramePtr<_Ty2>& _Right ) noexcept            { return _Left.Get( ) == _Right.Get( ); }

    template <class _Ty1, class _Ty2>
    _NODISCARD bool operator!=( const vaFramePtr<_Ty1>& _Left, const vaFramePtr<_Ty2>& _Right ) noexcept            { return _Left.Get( ) != _Right.Get( ); }

    template <class Type>
    _NODISCARD bool operator==( const vaFramePtr<Type> & _Left, std::nullptr_t ) noexcept                           { return _Left.Get( ) == nullptr; }

    template <class Type>
    _NODISCARD bool operator==( std::nullptr_t, const vaFramePtr<Type>& _Right ) noexcept                           { return nullptr == _Right.Get( ); }

    template <class Type>
    _NODISCARD bool operator!=( const vaFramePtr<Type> & _Left, std::nullptr_t ) noexcept                           { return _Left.Get( ) != nullptr; }

    template <class Type>
    _NODISCARD bool operator!=( std::nullptr_t, const vaFramePtr<Type>& _Right ) noexcept                           { return nullptr != _Right.Get( ); }

    template <class _Ty1, class _Ty2>
    _NODISCARD bool operator==( const std::shared_ptr<_Ty1>& _Left, const vaFramePtr<_Ty2>& _Right ) noexcept       { return _Left.get( ) == _Right.get( ); }

    template <class _Ty1, class _Ty2>
    _NODISCARD bool operator==( const vaFramePtr<_Ty1>& _Left, const std::shared_ptr<_Ty2>& _Right ) noexcept       { return _Left.get( ) == _Right.get( ); }

    template <class _Ty1, class _Ty2>
    _NODISCARD bool operator!=( const std::shared_ptr<_Ty1>& _Left, const vaFramePtr<_Ty2>& _Right ) noexcept       { return _Left.get( ) != _Right.get( ); }

    template <class _Ty1, class _Ty2>
    _NODISCARD bool operator!=( const vaFramePtr<_Ty1>& _Left, const std::shared_ptr<_Ty2>& _Right ) noexcept       { return _Left.get( ) != _Right.get( ); }

    template <class _Ty1, class _Ty2>
    _NODISCARD bool operator >( const vaFramePtr<_Ty1>& _Left, const vaFramePtr<_Ty2>& _Right ) noexcept            { return _Left.Get( ) > _Right.Get( ); }

    template <class _Ty1, class _Ty2>
    _NODISCARD bool operator <( const vaFramePtr<_Ty1>& _Left, const vaFramePtr<_Ty2>& _Right ) noexcept            { return _Left.Get( ) < _Right.Get( ); }
}

namespace std 
{
    template < typename Type >
    struct hash<Vanilla::vaFramePtr<Type>>
    {
        std::size_t operator()( Vanilla::vaFramePtr<Type> const & k ) const
        {
            return hash<void *>()( static_cast<void*>(k.get()) );
        }
    };

}