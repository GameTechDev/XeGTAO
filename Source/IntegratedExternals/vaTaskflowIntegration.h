/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Taskflow:                     https://github.com/cpp-taskflow/cpp-taskflow
// Taskflow license:             MIT license (https://github.com/cpp-taskflow/cpp-taskflow/blob/master/LICENSE)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaCoreIncludes.h"

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED

#pragma warning ( push )
#pragma warning ( disable: 4267 4703 4701 )

#include <taskflow/taskflow.hpp>  // Cpp-Taskflow is header-only

#pragma warning ( pop )


namespace Vanilla
{


    class vaTF : protected vaSingletonBase<vaTF>
    {
        tf::Executor                    m_executor;
        shared_ptr<tf::ObserverInterface> 
                                        m_observer = nullptr;

        friend tf::ObjectPool<tf::Node>& tf::get_node_pool( );
        tf::ObjectPool<tf::Node>        m_pool;

    protected:
        friend class vaCore;
        explicit vaTF( int threadsToUse = 0 );
        ~vaTF( );

    public:

        static tf::Executor &           Executor( )         { return GetInstance().m_executor; }

        static int                      ThreadCount( )      { return (int)GetInstance().m_executor.num_workers( ); }
        static int                      ThreadID( )         { int retVal = GetInstance().m_executor.this_worker_id( ); assert( retVal < ThreadCount() ); return retVal; }

        // same as FlowBuilder::parallel_for except without S(tart) and T(erminate) nodes and with .run and taskflow built in
        // example:
        //   auto workFlowFuture = vaTF::GetInstance( ).parallel_for( 0, int( m_workersActive - 1 ), workerFunction );
        //   workFlowFuture.wait( );
        template <typename C>
        static std::future<void>        parallel_for( int begin, int end, C && callable, int chunk, const char * name = "" );

        template <typename C>
        static std::pair<tf::Task, tf::Task>   
                                        parallel_for_emplace( tf::FlowBuilder & flow, int begin, int end, C&& callable, int chunk, const char * name = "" );

        // fire and forget call - see https://github.com/taskflow/taskflow/issues/172 for future
        template <typename C>
        static std::future<void>        async( C && callable );

    private:
        bool                            IsTracing( )        { return m_observer != nullptr; }
        bool                            StartTracing( );
        bool                            StopTracing( string dumpFilePath );
    };

    template <typename CallableType>
    inline std::pair<tf::Task, tf::Task> vaTF::parallel_for_emplace( tf::FlowBuilder & flow, int beg, int end, CallableType && callable, int chunkSize, const char * name )
    {
        using NonRefCallableType = std::remove_reference_t<CallableType>;

        // default partition equals to the worker count
        if( chunkSize == 0 )
            chunkSize = 1;

        int totalCount = end-beg;
        if( totalCount < 0 )
            { assert( false ); return {{},{}}; };

        auto S = flow.placeholder( );

        if( totalCount == 0 )
        {
            auto T = flow.placeholder( );
            if( S.num_successors( ) == 0 ) {
                S.precede( T ); }
            return std::make_pair( S, T );
        }

        const int chunkCount = (totalCount+chunkSize-1)/chunkSize;
        assert( chunkCount > 0 );

#define VA_PFE_SEPARATE_CALLABLE_CAPTURE

#ifdef VA_PFE_SEPARATE_CALLABLE_CAPTURE
        struct ChunkContext final
        {
            NonRefCallableType *    Callable;
            int                     Beg;
            int                     End;
            void operator() ( tf::Subflow & subflow ) const noexcept
            {
                if constexpr( std::is_invocable_v<CallableType, int> )
                {
                    for( auto i = Beg; i < End; i++ )
                        (*Callable)( i );
                }
                else if constexpr( std::is_invocable_v<CallableType, int, int> )
                {
                    (*Callable)( Beg, End );
                }
                else if constexpr( std::is_invocable_v<CallableType, int, tf::Subflow &> )
                {
                    for( auto i = Beg; i < End; i++ )
                        ( *Callable )( i, subflow );
                }
                else if constexpr( std::is_invocable_v<CallableType, int, int, tf::Subflow &> )
                {
                    ( *Callable )( Beg, End, subflow );
                }
                else
                {
                    static_assert( false, "Not a valid callable type!" );
                }
            }
        };
        // TODO: use some kind of object pool here
        NonRefCallableType * storedCallable = new NonRefCallableType( callable );
        auto T = flow.emplace( [ storedCallable ]( ) {
            delete storedCallable;
        } );
#else
        auto T = flow.placeholder( );
#endif

        for( int i = 0; i < chunkCount; i++ )
        {
            int chunkBeg    = i * chunkSize;
            int chunkEnd    = std::min(end, (i+1) * chunkSize );
#ifdef VA_PFE_SEPARATE_CALLABLE_CAPTURE
            ChunkContext chunk { storedCallable, chunkBeg, chunkEnd };
            chunk.Callable  = storedCallable;
            auto task = flow.emplace( chunk );
#else
            auto task = flow.emplace( [ chunkBeg, chunkEnd, copiedCallable{dont_move(callable)} ] ()
            {
                if constexpr( std::is_invocable_v<CallableType, int> )
                {
                    for( auto i = Beg; i < End; i++ )
                        copiedCallable( i );
                }
                else if constexpr( std::is_invocable_v<CallableType, int, int > )
                {
                    copiedCallable( Beg, End );
                }
                else
                {
                    static_assert( false, "Not a valid callable type!" );
                }
            } );
#endif
            S.precede( task ); task.precede( T );
            if( name != nullptr )
                task.name( name );
        }

        return std::make_pair(S, T); 
    }

    template <typename C>
    inline std::future<void> vaTF::parallel_for( int beg, int end, C&& callable, int chunk, const char * name )
    {
        // from https://github.com/taskflow/taskflow/issues/165
        auto taskFlow = std::make_shared<tf::Taskflow>();

        parallel_for_emplace( *taskFlow, beg, end, callable, chunk, name );

        // This captures the taskFlow ptr for fire & forget - it's a bit shakey and needs to be updated with a better approach in the future.
        // See https://github.com/taskflow/taskflow/issues/165 and https://github.com/taskflow/taskflow/issues/172
        return vaTF::GetInstance( ).Executor( ).run( *taskFlow, [taskFlow] () { int dbg = 0; dbg++; } );
    }

    template <typename C>
    inline std::future<void> vaTF::async( C && callable )
    {
#if 1
        return vaTF::GetInstance( ).Executor( ).async( callable );
#else
         // from https://github.com/taskflow/taskflow/issues/165
         auto taskFlow = std::make_shared<tf::Taskflow>( );
         
         taskFlow->emplace( callable );
         
         // This captures the taskFlow ptr for fire & forget - it's a bit shakey and needs to be updated with a better approach in the future.
         // See https://github.com/taskflow/taskflow/issues/165 and https://github.com/taskflow/taskflow/issues/172
         return vaTF::GetInstance( ).Executor( ).run( *taskFlow, [ taskFlow ]( ) { int dbg = 0; dbg++; } );
#endif
    }
}

namespace tf
{
    // if you get "error C2084: function 'tf::ObjectPool<tf::Node,65536> &tf::Graph::_node_pool(void)' already has a body" just comment out
    // this in taskflow - this is a hack to make sure any & all allocations are freed so we don't trigger our memory leak detection!
    inline tf::ObjectPool<Node>& tf::get_node_pool( )
    {
        return Vanilla::vaTF::GetInstance().m_pool;
    }
}

#endif // VA_TASKFLOW_INTEGRATION_ENABLED