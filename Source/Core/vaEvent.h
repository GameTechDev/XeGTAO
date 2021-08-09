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
#include <vector>
#include <functional>

// notes/todos:
// * inspiration: http://nercury.github.io/c++/interesting/2016/02/22/weak_ptr-and-event-cleanup.html
// * void weak_ptr for the guarantor token - is this ok? basically we don't care about the type of the thing the weak_ptr is pointing to, we
//   only use it as a 'guarantor' of the callback being alive - obviously if we mess it up at call time and the 'guarantor' token doesn't really guarantee callback lifetime then we have an issue?
// * variadic templates - Invoke, cool - not cool?
// * best way to make it thread-safe?
// * exceptions? yay nay? nay for now
// * callback invoke order - should it be deterministic (at the moment it's reverse of add but can change as a result of removal)
// * function naming - does something else make more sense?
// * AddWithToken - what to do during recursion?
// * Remove - is current thing to do during recursion ok?
// * deleted copy/assignment operators so we can just make event variables publically accessible without someone messing them up - kool/not kool?


namespace Vanilla
{
    // Event dispatcher with lazy removal
    template< typename FunctionType >
    class vaEvent final 
    {
    private:
        struct CallbackItem
        {
            weak_ptr<void>              GuarantorToken;
            std::function<FunctionType> Callback;
        };
        std::vector<CallbackItem>   m_callbacks;

        int32                       m_recursionDepth    = 0;

    public:
        vaEvent( )                  { }
        ~vaEvent( )                 { assert( m_recursionDepth == 0 ); }

        vaEvent( const vaEvent & )                  = delete;
        vaEvent & operator = ( const vaEvent & )    = delete;

    public:
        template <typename ... ArgsType >
        void Invoke( ArgsType && ... args )
        {
            // recursive mutex here?

            m_recursionDepth++;

            bool removalNeeded = false;

            for( int i = (int)m_callbacks.size( )-1; i >= 0 ; i-- )
            {
                shared_ptr<void> lockedToken = m_callbacks[i].GuarantorToken.lock();
                if( lockedToken != nullptr )
                {
                     m_callbacks[i].Callback(std::forward<ArgsType>(args)...);
                }
                else
                    removalNeeded = true;
            }

            // only allow removal if we're not being called recursively (rec depth is 1)
            if( removalNeeded && (m_recursionDepth == 1) )
            {
                assert( m_recursionDepth == 1 );
                for( int i = (int)m_callbacks.size( )-1; i >= 0 ; i-- )
                {
                    shared_ptr<void> lockedToken = m_callbacks[i].GuarantorToken.lock();
                    
                    if( lockedToken == nullptr )
                    {
                        // if this bit is slow, we could 'if-last-erase, if-not-last-swap-with-last-and-erase' but this would reshuffle
                        // the call order which otherwise is deterministic and same reverse of the 'Add' order. do we want to do this? 
                        // not sure, maybe if needed.
                        // also, instead of 'removalNeeded' - we could collect indices of that need removing in a stack buffer but
                        // I honestly doubt this will ever be a performance bottleneck.

                        // just erase it
                        m_callbacks.erase( m_callbacks.begin() + i );
                    }
                }
            }

            m_recursionDepth--;
            assert( m_recursionDepth >= 0 );
        }

        template <typename ... ArgsType >
        void operator ()( ArgsType&& ... args )
        {
            Invoke( std::forward<ArgsType>(args)... );
        }

        // 'Naked' add callback; caller ensures that the guarantorToken guarantees callback validity (can be weak_ptr to this->shared_from_this() or a member variable, etc.)
        // Generic add example with automatic parameters (could be made into a macro?):
        //      Event_Something.Add( myObject.m_tokenSharedPtr, [](auto && ...params) {myObject->OnTick(params...);} );
        void AddWithToken( const weak_ptr<void> & guarantorToken, const std::function<FunctionType> & callback )
        { 
            // // adding while in recursion? might actually not be bad because we're reverse-iterating but since we haven't thought it through, let's assert here
            // assert( m_recursionDepth == 0 );

            m_callbacks.push_back( { guarantorToken, callback } ); 
        }

        // Automatic 'Naked' version for member-to-objects (if guarantor is not the shared_ptr to object itself)
        template< typename Object, typename MemberFunctionType >
        void AddWithToken( const weak_ptr<void> & guarantorToken, Object * objectPtr, MemberFunctionType objectMemberCallback )
        {
            // TODO: use std::invoke or a macro? see https://isocpp.org/wiki/faq/pointers-to-members - something like   #define CALL_MEMBER_FN(object,ptrToMember)  ((object).*(ptrToMember))
            AddWithToken( guarantorToken, [=](auto && ...params) 
            { 
                ((*objectPtr).*(objectMemberCallback))(params...);
            } );
        }

        // 'Safe' add callback - in this case guarantor token object and callback is its member
        template< typename Object, typename MemberFunctionType >
        void Add( const shared_ptr<Object> & objectSharedPtr, MemberFunctionType objectMemberCallback )
        {
            AddWithToken( objectSharedPtr, objectSharedPtr.get(), objectMemberCallback );
        }

        // will also remove all items with <null> tokens so use the empty pointer for preemptive removal if needed for any reason (clearing of lambda storage & references?)
        void Remove( const weak_ptr<void> & tokenToRemove )
        { 
            for( int i = (int)m_callbacks.size( )-1; i >= 0 ; i-- )
            {
                shared_ptr<void> lockedToken = m_callbacks[i].GuarantorToken.lock();
                if( lockedToken == nullptr || lockedToken == tokenToRemove.lock() )
                {
                    if( m_recursionDepth == 0 )
                        m_callbacks.erase( m_callbacks.begin() + i );   // remove right now
                    else
                        m_callbacks[i].GuarantorToken.reset();                   // just make sure it's never invoked again

                    // don't break though, let it loop and clear all in case of multiple callbacks with the same token, which should be legal
                }
            }
        }

        void RemoveAll( )
        { 
            // removing while in recursion? the below should be handling it OK but I'm not 100% sure it's ok, so step through it and verify before removing assert
            assert( m_recursionDepth == 0 );

            for( int i = (int)m_callbacks.size( )-1; i >= 0 ; i-- )
            {
                if( m_recursionDepth == 0 )
                    m_callbacks.erase( m_callbacks.begin() + i );   // remove right now
                else
                    m_callbacks[i].GuarantorToken.reset();                   // just make sure it's never invoked again
            }
        }
    };

}