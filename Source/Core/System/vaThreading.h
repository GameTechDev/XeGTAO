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

// old code dropped, replaced by much simpler stuff based on C++14
// for long tasks use vaBackgroundTaskManager
// there's no fine grained thread pool anymore, use EnkiTS or similar for that

#include "Core/vaCore.h"
#include "Core/vaSingleton.h"

#include <future>
#include <queue>
#include <shared_mutex>

namespace Vanilla
{
    class vaThreading
    {
        struct ThreadLocalProps
        {
            bool        MainThread          = false;
            bool        MainThreadSynced    = false;        // MainThread or guaranteed not to run in parallel with MainThread
        };

    private:
        vaThreading( )  { }
        ~vaThreading( ) { }

    public:
        static void                         Sleep( uint32 milliseconds );
        static void                         YieldProcessor( );
        static void                         GetCPUCoreCountInfo( int & physicalPackages, int & physicalCores, int & logicalCores );
        static bool                         IsMainThread( )                                                     { return s_threadLocal.MainThread || s_threadLocal.MainThreadSynced; }
        static int                          GetCPULogicalCores( )                                               { int physicalPackages, physicalCores, logicalCores; GetCPUCoreCountInfo( physicalPackages, physicalCores, logicalCores ); return logicalCores; }

        static void                         SetSyncedWithMainThread( )                                          { s_threadLocal.MainThreadSynced = true; }

        static void                         SetThreadName( const string & name );   // can only be called once and before any GetThreadName
        static const char *                 GetThreadName( );

    private:
        friend class vaCore;
        friend class vaTracer;

        static thread_local ThreadLocalProps  s_threadLocal;
        static const ThreadLocalProps &     ThreadLocal( )                                                      { return s_threadLocal; }

        static std::atomic<std::thread::id> s_mainThreadID;
        static void                         SetMainThread( );

        static void                         MainThreadSetup( );
    };


    // For multi-frame ongoing tasks like loading assets or recompiling shaders (possibly even long life stuff like audio threads?)
    // Plus some helpers for viewing the tasks. Due to overhead not intended to be used for any tasks that are required to complete 
    // within the single frame. Can either force spawning system thread for each task or use a pool.
    // TODO:
    //    [ ] dependencies (SpawnWithDependency)
    class vaBackgroundTaskManager : public vaSingletonBase<vaBackgroundTaskManager>
    {
        struct TaskInternal;

    public:
        // used as a 'handle' for spawned tasks (visible from outside)
        struct Task
        {
            const string                Name;
            bool                        Result;

        protected:
            Task( const string & name ) : Name(name), Result(false) { }
        };

        // used as a link between the vaBackgroundTaskManager and the running task (visible only from the task itself)
        struct TaskContext
        {
            std::atomic_bool            ForceStop   = false;        // used for manager to signal to the task that it should drop all work and exit when possible safely; task should probably not modify it (maybe safe? never checked)
            std::atomic<float>          Progress    = 0.0f;         // used to the task to indicate progress (mostly for UI purposes) - clamped [0, 1]
            std::atomic_bool            HideInUI    = false;        // used for when ShowInUI flag set but one wants to hide/show it at runtime
        };

        // creation flags
        enum class SpawnFlags : uint32
        { 
            None                        = 0,
            ShowInUI                    = (1 << 0),
            UseThreadPool               = (1 << 1),                     // this will spawn the task using a limited pool of threads (with somewhere between 'cores-1' and 'logical threads-1' thread count) which means the thread might have to wait before it will start running so be careful not to cause deadlocks with any internal dependencies 
        };

    private:
        struct TaskInternal : Task
        {
            TaskContext             Context;

            const SpawnFlags        Flags;                              // is it visible in UI? (InsertImGuiWindow/InsertImGuiContent)
            std::atomic_bool        IsFinished          = false;        // used to indicate the task function has finished (exited)
            //std::thread             Thread;                           // current implementation
            std::mutex              WaitFinishedMutex;                  // to wait for IsFinished and other stuff
            std::condition_variable WaitFinishedCV;                     // to wait for IsFinished and other stuff
            std::function< bool( TaskContext & context ) >  
                                    UserFunction;

            std::atomic_bool        PooledWaiting       = false;

            TaskInternal( const string & name, SpawnFlags flags, const std::function< bool( TaskContext & context ) > & taskFunction ) : Task(name), Flags( flags ), UserFunction( taskFunction ) { }

            TaskInternal( const TaskInternal & copy ) = delete;
            TaskInternal & operator =( const TaskInternal & copy ) = delete;
        };

        std::atomic_bool                            m_stopped           = false;

        std::atomic_int                             m_threadPoolSize;
        std::atomic_int                             m_currentThreadPoolUseCount = 0;


        std::vector<shared_ptr<TaskInternal>>       m_currentTasks;
        std::queue<shared_ptr<TaskInternal>>        m_waitingPooledTasks;
        mutex                                       m_currentTasksMutex;

        // used to block simultaneous spawning of new tasks while WaitUntilFinished or StopManager calls as I have not made sure they will logically work ok
        mutex                                       m_spawnMutex;

    public:
        vaBackgroundTaskManager( );
        ~vaBackgroundTaskManager( ) { ClearAndRestart(); }  // tasks probably need to be stopped 

        // Stop all tasks, wait for them to finish, place vaBackgroundTaskManager in a stopped state and then restart - ensures all background processing is done
        void                    ClearAndRestart( );
        bool                    IsManagerStopped( ) const  { return m_stopped; }

        // This Spawn version guarantees that the outTask will always receive the new handle BEFORE the taskFunction has started on another thread (is this actually useful?)
        bool                    Spawn( shared_ptr<Task> & outTask, const string & taskName, SpawnFlags flags, const std::function< bool( TaskContext & context ) > & taskFunction );
        // Version for when we don't care about getting the handle before the taskFunction could have started (in theory it might have finished by the time we get the handle)
        shared_ptr<Task>        Spawn( const string & taskName, SpawnFlags flags, const std::function< bool( TaskContext & context ) > & taskFunction ) { shared_ptr<Task> outTask; if( Spawn( outTask, taskName, flags, taskFunction ) ) return outTask; else return nullptr; }

        // SpawnWithDependency( shared_ptr<Task> & outTask, const string & taskName, SpawnFlags flags, const std::function< bool( TaskContext & context ) > & taskFunction );
        
        float                   GetProgress( const shared_ptr<Task> & task );
        bool                    IsFinished( const shared_ptr<Task> & task );
        void                    MarkForStopping( const shared_ptr<Task> & task ); // mark for force-stop which should cause interruption but does not guarantee it will stop soon; does not wait for it to get stopped either (use WaitUntilFinished for that)
        void                    WaitUntilFinished( const shared_ptr<Task> & task );

    public:
        // used internally to show task progress but can be used from any vaIOPanel::IOPanelDraw() or similar imgui-suitable location to insert ImGui commands showing progress/info on the task!
        void                    ImGuiTaskProgress( const shared_ptr<Task> & task );

    protected:
        friend class vaUIManager;
        void                    InsertImGuiContent( );
        void                    InsertImGuiWindow( const string & title = "Background tasks in progress" );

    private:
        void                    InsertImGuiContentInternal( const std::vector<shared_ptr<TaskInternal>> & tasks );

    private:
        void                    Run( const shared_ptr<TaskInternal> & task );
        void                    ClearFinishedTasks( );
        
    };

    BITFLAG_ENUM_CLASS_HELPER( vaBackgroundTaskManager::SpawnFlags );

    // Owner thread creates an instance of vaThreadSpecificCallbackQueue and calls Tick( ... )
    // Any other thread can call 
    template< typename ... ArgsType >
    class vaThreadSpecificAsyncCallbackQueue
    {
        // struct CallbackEntry
        // {
        //     std::function<void( ArgsType... )>  Callback;
        //     std::tuple<ArgsType...>             Args;
        // 
        //     CallbackEntry( std::function<void( ArgsType... )>&& callback, ArgsType&& ... args ) :
        //         Callback( std::forward<std::function<void( ArgsType... )>>( callback ) ),
        //         Args( std::forward<ArgsType>( args... ) ) { }
        // 
        //     void Invoke( )
        //     {   // oh my is this ugly
        //         _invoke( std::make_index_sequence < std::tuple_size< std::tuple< ArgsType... > >{} > {} );
        //     }
        // 
        // private:
        //     template <size_t... Is>
        //     constexpr auto              _invoke( std::index_sequence<Is...> )
        //     {
        //         Callback( pThisTask, ctx, std::get<Is>( Args )... );
        //     }
        // };

        std::thread::id const       m_ownerThreadID = std::this_thread::get_id();
        
        std::shared_mutex           m_mutex;
        bool                        m_active = true;

        std::deque< std::packaged_task<bool( ArgsType... )> >   
                                    m_entries;

    public:
        vaThreadSpecificAsyncCallbackQueue( )    { }
        ~vaThreadSpecificAsyncCallbackQueue( )   { }

    public:

        template< typename ... ArgsType >
        std::future<bool>           Enqueue( std::function<bool( ArgsType... )> && callback )
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if( /*m_ownerThreadID == std::this_thread::get_id() ||*/ !m_active )
            {
                assert( false );
                std::packaged_task<bool()> dummyTask( [ ]( ) { return false; } );
                dummyTask();
                return dummyTask.get_future();
            }
            m_entries.push_back( std::move(std::packaged_task<bool( ArgsType... )>(std::forward< decltype(callback) >( callback ))) );
            // m_entries.push_front( CallbackEntry( std::forward<std::function<void(ArgsType...)>>( callback ), std::forward<ArgsType>( args )... ) );
            return m_entries.back().get_future();
        }

        void                        Invoke( ArgsType... args )
        {
            assert( m_ownerThreadID == std::this_thread::get_id( ) );
            std::scoped_lock<std::shared_mutex> lock( m_mutex );
            while( !m_entries.empty( ) )
            {
                m_entries.front()( std::forward<ArgsType>( args )... );
                m_entries.pop_front();
            }
        }

        // invoke all remaining entries and prevent any more from being added
        void                        InvokeAndDeactivate( ArgsType&& ... args )
        {
            assert( m_ownerThreadID == std::this_thread::get_id() );
            std::scoped_lock<std::shared_mutex> lock( m_mutex );
            m_active = false;
            while( !m_entries.empty( ) )
            {
                m_entries.front()( std::forward<ArgsType>( args )... );
                m_entries.pop_front( );
            }
        }
    };

}

