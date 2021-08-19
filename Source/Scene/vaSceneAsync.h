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

#include "Core/vaCoreIncludes.h"
#include "Core/vaConcurrency.h"

#include "vaSceneTypes.h"
#include "vaSceneComponents.h"

#include "Core/vaProfiler.h"

// Define this to serialize all vaSceneAsync calls for performance testing and/or threaded debugging
// #define VA_SCENE_ASYNC_FORCE_SINGLETHREADED

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
    #include "IntegratedExternals/vaTaskflowIntegration.h"
#else
    #ifndef VA_SCENE_ASYNC_FORCE_SINGLETHREADED
        #define VA_SCENE_ASYNC_FORCE_SINGLETHREADED
    #endif
#endif

namespace Vanilla
{
    class vaScene;

    // This manages all vaScene asynchronous operations - systems can add their work nodes; graph gets created, 
    // work gets executed 

    class vaSceneAsync
    {
    public:
        // For additional name info and (optional) additional access to task scheduling / custom task spawning.
        struct ConcurrencyContext
        {
#ifndef VA_SCENE_ASYNC_FORCE_SINGLETHREADED
            tf::Subflow *               Subflow;
#endif
        };

        struct WorkNode
        {
            const string                    Name;
            const std::vector<string>       Predecessors;               // list of predecessors
            const std::vector<string>       Successors;                 // list of successors

            // entt components to lock, either for read-write (unique lock) or read-only (shared lock)
            const std::vector<int>          ReadWriteComponents;
            const std::vector<int>          ReadComponents;

        protected:
            friend class vaSceneAsync;
            // 'active' part, valid between vaSceneAsync::Begin/vaSceneAsync::End
            std::vector<shared_ptr<WorkNode>>   ActivePredecessors;     // those currently found
            std::vector<shared_ptr<WorkNode>>   ActiveSuccessors;       // those currently found
            bool                            PrologueDone;
            bool                            AsyncDone;          // Narrow & Wide stages
            bool                            EpilogueDone;
            //
            vaMappedString                  MappedName;         // vaTracer name; valid only between Begin/End 
            //
#ifndef VA_SCENE_ASYNC_FORCE_SINGLETHREADED
            tf::Task                        TFTask;
            std::promise<void>              FinishedBarrier;
            std::future<void>               FinishedBarrierFuture;
#endif
            // mutable bool                    VisitedFlag;        // not for async use!
            // ***

        protected:
            WorkNode( const string & name, const std::vector<string> & predecessors, const std::vector<string> & successors, const std::pair<std::vector<int>, std::vector<int>> & locks) : Name(name), Successors( successors ), Predecessors( predecessors ), ReadWriteComponents(locks.first), ReadComponents(locks.second) { }
            virtual ~WorkNode() {}

        protected:

            // Work callbacks; graph is traversed 3 times, first serially (ExecutePrologue), then asynchronously (first Narrow then Wide if Narrow returns non-0 and continues looping), then serially again (ExecuteEpilogue)
            // 
            // Prepare work; returned std::pair<uint, uint> will be used to run first AsynchronousWide and if {0,0} ExecuteAsyncWide gets skipped and ExecuteAsyncNarrow gets called; called from main thread ( assert(vaThreading::IsMainThread( )) )
            virtual void                        ExecutePrologue( float deltaTime, int64 applicationTickIndex )              { deltaTime; applicationTickIndex; }
            //
            // Asynchronous narrow processing; returned std::pair<uint, uint> will be used to run ExecuteWide if .first != 0; if .first and .second == 0, then exits; if .first is zero but .second is non-zero, it will do another pass but skip wide and just run narrow again
            virtual std::pair<uint32, uint32>   ExecuteNarrow( const uint32 pass, ConcurrencyContext & concurrencyContext ) { pass; concurrencyContext; return {0,0}; }
            //
            // Asynchronous wide processing; items run in chunks to minimize various overheads
            virtual void                        ExecuteWide( const uint32 pass, const uint32 itemBegin, const uint32 itemEnd, ConcurrencyContext & concurrencyContext ) { pass, itemBegin; itemEnd; concurrencyContext; assert( false ); }
            //
            // Wraps up things (if needed); called from main thread ( assert(vaThreading::IsMainThread( )) )
            virtual void                        ExecuteEpilogue( )                                                      { }
        };

        struct MarkerWorkNode : WorkNode
        {
            MarkerWorkNode( const string & name, const std::vector<string> & predecessors, const std::vector<string> & successors = std::vector<std::string>{ }, const std::pair<std::vector<int>, std::vector<int>> & locks = {{},{}} ) : 
                WorkNode( name, predecessors, successors, locks ) { }
        };

        // Generic way to fill a thread-safe list (Scene::UniqueStaticAppendConsumeList / Scene::UniqueStaticAppendConsumeList) with (non-thread-safe) tag components and remove them from registry
        template< typename TagComponentType, typename ListType > 
        struct MoveTagsToListWorkNode : WorkNode
        {
            vaScene &       Scene;
            ListType &      DestList;
            entt::basic_view< entt::entity, entt::exclude_t<>, const TagComponentType>
                            View;

            MoveTagsToListWorkNode( const string & name, vaScene & scene, ListType & destList, const std::vector<string> & predecessors, const std::vector<string> & successors ) : 
                vaSceneAsync::WorkNode( name, predecessors, successors, Scene::AccessPermissions::ExportPairLists<TagComponentType>() ),
                    Scene( scene ), DestList( destList ), View( scene.Registry().view<std::add_const_t<TagComponentType>>( ) ) { }

            virtual void                        ExecuteWide( const uint32 pass, const uint32 itemBegin, const uint32 itemEnd, ConcurrencyContext & concurrencyContext ) override;
            virtual std::pair<uint32, uint32>   ExecuteNarrow( const uint32 pass, ConcurrencyContext & concurrencyContext ) override;
        };


    private:

        vaScene &               m_scene;

        // If more required, up the number. If a lot more required, maybe rethink things :)
        static const int        c_maxNodeCount                  = 1024;

        std::atomic_bool        m_isAsync                       = false;
        float                   m_currentDeltaTime              = 0.0f;
        int64                   m_currentApplicationTickIndex   = -1;

        // master list, where AddWorkNode writes into
        std::vector<weak_ptr<WorkNode>>     m_graphNodes;
        bool                                m_graphNodesDirty   = true;

        // 'active' list, alive between Begin and End
        std::vector<shared_ptr<WorkNode>>   m_graphNodesActive;

        // profiling and debugging
        bool                    m_graphDumpScheduled            = false;
        shared_ptr<vaTracer::ThreadContext> m_tracerContext     = nullptr;
        
        // these are for tracing async stuff - it can get added in any direction so requires sort and manual add
        std::shared_mutex               m_tracerAsyncEntriesMutex;
        std::vector<vaTracer::Entry>    m_tracerAsyncEntries;

#ifndef VA_SCENE_ASYNC_FORCE_SINGLETHREADED
        tf::Taskflow                    m_masterFlow;
        tf::Future<void>                m_masterFlowFuture;
#endif


    private:
        friend class vaScene;
        vaSceneAsync( vaScene & parentScene );
        ~vaSceneAsync();

    public:
        void                    Begin( float deltaTime, int64 applicationTickIndex );
        void                    End( );

        //bool                    WaitFinish( const string & nodeName );
        bool                    IsAsync( ) const                                 { return m_isAsync; }

        // This only holds a weak reference to node; if it gets destroyed that's fine, it gets self-removed. If "Remove" is required, add it below accordingly
        bool                    AddWorkNode( std::shared_ptr<WorkNode> newNode );
        
        // this only waits for the async part to complete; it can only be called in between Begin and End and Epilogue (which is ran at End) would have not finished
        bool                    WaitAsyncComplete( const string & nodeName );

        void                    ScheduleGraphDump( )            { m_graphDumpScheduled = true; }

    private:
        int                     FindActiveNodeIndex( const string & name );
        string                  DumpDOTGraph( );

        // are the two nodes in question guaranteed to be executed serially (there successor or predecessor links)
        enum class NodeRelationship { LeftPreceedsRight, RightPreceedsLeft, PossiblyConcurrent, Cyclic }
                                FindActiveNodeRelationship( const WorkNode & nodeLeft, const WorkNode & nodeRight );

    public:
        // helpers
        static shared_ptr<MarkerWorkNode>   MarkerWorkNodeMakeShared( const string& name, const std::vector<string>& predecessors, const std::vector<string>& successors = std::vector<std::string>{ }, const std::pair<std::vector<int>, std::vector<int>>& locks = { {},{} } )
        {
            return std::make_shared<MarkerWorkNode>( name, predecessors, successors, locks );
        }

        template< typename TagComponentType, typename ListType >
        static shared_ptr<MoveTagsToListWorkNode<TagComponentType, ListType>> MoveTagsToListWorkNodeMakeShared( const string & name, vaScene & scene, ListType & destList, const std::vector<string> & predecessors, const std::vector<string> & successors )
        {
            return std::make_shared<vaSceneAsync::MoveTagsToListWorkNode<TagComponentType, ListType>>( name, scene, destList, predecessors, successors );
        }

    };

    template< typename TagComponentType, typename ListType > 
    void vaSceneAsync::MoveTagsToListWorkNode< TagComponentType, ListType >::ExecuteWide( const uint32 pass, const uint32 itemBegin, const uint32 itemEnd, ConcurrencyContext & ) 
    { 
        assert( pass == 0 ); pass;
        for( uint32 index = itemBegin; index < itemEnd; index++ )
        {
            entt::entity entity = View[index];                                          // <- is this safe use of the entt View?
            assert( Scene.Registry().valid( entity ) );                                 // if this fires, you've corrupted the data somehow - possibly destroying elements outside of DestroyTag path?
            DestList.Append( entity );
        }
    }

    template< typename TagComponentType, typename ListType > 
    std::pair<uint32, uint32> vaSceneAsync::MoveTagsToListWorkNode< TagComponentType, ListType >::ExecuteNarrow( const uint32 pass, ConcurrencyContext & )
    { 
        if( pass == 0 )
        {   // first pass: start
            DestList.StartAppending( (uint32)Scene.Registry().size( ) );
            return std::make_pair( (uint32)View.size(), vaTF::c_chunkBaseSize * 8 );    // <- is this safe use of the entt View?
        }
        else if( pass == 1 )
        {
            // last pass: clear
            VA_TRACE_CPU_SCOPE( ClearEnTTTag );
            Scene.Registry().clear<TagComponentType>( );                                // <- is this safe use of the entt registry?
        }
        else { assert( false ); }
        return {0,0}; // no more processing needed
    }

}
