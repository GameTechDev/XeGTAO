///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaSceneComponents.h"

#include "vaScene.h"

#include "vaSceneAsync.h"

#include "Core/System/vaFileTools.h"

#include <locale>

using namespace Vanilla;

#if 0
struct TestWorkNode : vaSceneAsync::WorkNode
{
    std::vector<int>    DATA;
    int64               TESTAVG     = 0;
    std::atomic<int64>  AVG         = 0;

    TestWorkNode( const string & name, const std::vector<string> & predecessors, const std::vector<string> & successors, const std::pair<std::vector<int>, std::vector<int>> & locks = {{},{}} ) : 
        vaSceneAsync::WorkNode( name, predecessors, successors, locks ) 
    { 
        for( int i = 0; i < 1234567; i++ )
        {
            DATA.push_back( vaRandom::Singleton.NextINT32() );
            TESTAVG += DATA[i];
        }
        TESTAVG = TESTAVG / (int64)DATA.size();
    }
    virtual void                    ExecutePrologue( float, int64 ) override
    {
        AVG = 0;
    }
    virtual std::pair<uint32, uint32> ExecuteNarrow( const uint32 pass, vaSceneAsync::ConcurrencyContext & concurrencyContext ) override
    { 
        concurrencyContext; 
        if( pass == 0 )
            return {(uint32)DATA.size(),64};
        else if( pass == 1 )
            AVG = AVG / (int64)DATA.size();
        return {0,0}; 
    }
    virtual void                    ExecuteWide( const uint32 pass, const uint32 itemBegin, const uint32 itemEnd, vaSceneAsync::ConcurrencyContext & concurrencyContext ) override
    { 
        assert( pass == 0 ); pass;
        int64 localSum = 0;
        for( uint32 i = itemBegin; i < itemEnd; i++ )
        {
            localSum += DATA[i];
        }
        AVG += localSum;
        concurrencyContext;
    }
    virtual void                    ExecuteEpilogue( ) override
    { 
        if( AVG != TESTAVG )
        {
            VA_ERROR( "oompa loompa there's a bug" );
        }
    }

};
static shared_ptr<TestWorkNode> s_microTest;
#endif

vaSceneAsync::vaSceneAsync( vaScene & parentScene ) : m_scene( parentScene )
{
    //AddWorkNode( s_microTest = std::make_shared<TestWorkNode>( "OompaLoompaMicroTest",  std::vector<std::string>{ },  std::vector<std::string>{ } ) );
}

vaSceneAsync::~vaSceneAsync( )
{
    assert( vaThreading::IsMainThread( ) );
    assert( !m_isAsync );
}

bool vaSceneAsync::AddWorkNode( std::shared_ptr<WorkNode> newNode )
{
    assert( vaThreading::IsMainThread( ) );
    assert( !m_isAsync );

    if( m_graphNodes.size() >= c_maxNodeCount )
        { assert( false ); return false; }      // can't have that many nodes
    
    bool nameOk = true;
    for( char c : newNode->Name )
        nameOk &= std::isalnum(c, std::locale()) || (c == '_');
    if( !nameOk )
        { VA_ERROR( "vaSceneAsync::AddWorkNode - name invalid" ); assert( false ); return false; }

    for( int i = 0; i < m_graphNodes.size( ); i++ )
    {
        auto node = m_graphNodes[i].lock();
        if( node->Name == newNode->Name )
            { VA_ERROR( "vaSceneAsync::AddWorkNode - same name already exists" ); assert( false ); return false; }  // names must be unique
    }

    m_graphNodes.push_back( newNode );
    m_graphNodesDirty = true;

    return true;
}

vaSceneAsync::NodeRelationship vaSceneAsync::FindActiveNodeRelationship( const WorkNode & nodeLeft, const WorkNode & nodeRight )
{
    if( &nodeLeft == &nodeRight )
    { assert( &nodeLeft != &nodeRight ); return NodeRelationship::Cyclic; }   // not really cyclic but an error

    const WorkNode * workingStack[c_maxNodeCount]; 

    // return values: 1 - target is predecessor of master, 0 - isn't a predecessor, -1 graph cycle detected
    auto isPredecessor = [&] ( const WorkNode & master, const WorkNode & target ) -> int
    {
        //for( int i = 0; i < m_graphNodesActive.size(); i++ ) 
        //    m_graphNodesActive[i]->VisitedFlag = false;    // clear visited flags - we could use 'last visited' int to avoid clears

        int workingStackCount = 0; // empty stack
        int loopDetection = 0;

        // push all our predecessors on the stack, if any
        for( int i = 0; i < master.ActivePredecessors.size( ); i++ )
            workingStack[workingStackCount++] = master.ActivePredecessors[i].get(); 

        // traverse the graph
        while ( workingStackCount > 0 )
        {
            workingStackCount--;
            loopDetection++;
            const WorkNode & popped = *workingStack[workingStackCount];

            if( loopDetection > m_graphNodesActive.size()*m_graphNodesActive.size() ) // I'm pretty sure this number could be smaller....
                { VA_LOG( "Loop detected on node '%s'", popped.Name.c_str()  ); assert( false ); return -1; } // graph cycle detected on 'popped' - check your dependencies!

            if( &popped == &target )
                return true;

            for( int i = 0; i < popped.ActivePredecessors.size( ); i++ )
                workingStack[workingStackCount++] = popped.ActivePredecessors[i].get();
        };
        return false;
    };

    int rlIsPredecessor = isPredecessor( nodeLeft, nodeRight );
    int lrIsPredecessor = isPredecessor( nodeRight, nodeLeft );
    if( (lrIsPredecessor == -1 || rlIsPredecessor == -1) || (lrIsPredecessor == 1 && rlIsPredecessor == 1) )
        return vaSceneAsync::NodeRelationship::Cyclic;
    if( lrIsPredecessor == 1 && rlIsPredecessor == 0 )
        return vaSceneAsync::NodeRelationship::LeftPreceedsRight;
    if( lrIsPredecessor == 0 && rlIsPredecessor == 1 )
        return vaSceneAsync::NodeRelationship::RightPreceedsLeft;
    if( lrIsPredecessor == 0 && rlIsPredecessor == 0 )
        return vaSceneAsync::NodeRelationship::PossiblyConcurrent;
    assert( false ); // getting here actually indicates a bug in this algorithm
    return vaSceneAsync::NodeRelationship::Cyclic; 
}

void vaSceneAsync::Begin( float deltaTime, int64 applicationTickIndex )
{
    deltaTime; applicationTickIndex;
    assert( vaThreading::IsMainThread( ) );
    assert( !m_isAsync );

    {
        string tracerName = "!SceneAsync_" + m_scene.Name();
        if( m_tracerContext != nullptr && m_tracerContext->Name != tracerName )
            m_tracerContext = nullptr;
        if( m_tracerContext == nullptr )
            m_tracerContext = vaTracer::CreateVirtualThreadContext( tracerName, false, false );
    }

    m_tracerContext->OnBegin( m_tracerContext->MapName("BeginEndScope"), (int)(applicationTickIndex&0x8FFFFFFF) );

    m_isAsync = true;
    m_currentDeltaTime            = deltaTime;
    m_currentApplicationTickIndex = applicationTickIndex;

    assert( m_graphNodesActive.size() == 0 );
    for( int i = 0; i < m_graphNodes.size( ); i++ )
    {
        auto node = m_graphNodes[i].lock();
        if( node == nullptr )
        {
            m_graphNodes.erase( m_graphNodes.begin()+i );
            m_graphNodesDirty = true;
        }
        else
            m_graphNodesActive.push_back( node );
    }

    // reset active nodes
    for( int i = 0; i < m_graphNodesActive.size( ); i++ )
    {
        auto & node = m_graphNodesActive[i];
        assert( node->ActivePredecessors.size() == 0 );
        assert( node->ActiveSuccessors.size() == 0 );
        node->PrologueDone  = false;
        node->AsyncDone     = false;
        node->EpilogueDone  = false;
        node->MappedName      = m_tracerContext->MapName( node->Name );
    }

    // add active nodes actual predecessors/successors
    for( int i = 0; i < m_graphNodesActive.size( ); i++ )
    {
        auto & node = m_graphNodesActive[i];
        for( const string & pred : node->Predecessors )
        {
            int f = FindActiveNodeIndex( pred );
            if( f != -1 )
            {
                node->ActivePredecessors.push_back( m_graphNodesActive[f] );
                m_graphNodesActive[f]->ActiveSuccessors.push_back( node );        // this doesn't avoid duplicates but that's fine
            }
            else
            { 
                VA_ERROR( "vaSceneAsync::Begin - can't find predecessor '%s' for node '%s'!", pred.c_str(), node->Name.c_str() );
                m_graphNodesDirty = true;
                m_graphNodesActive.clear();
                return;
            }
        }
        for( const string & succ : node->Successors )
        {
            int f = FindActiveNodeIndex( succ );
            if( f != -1 )
            {
                m_graphNodesActive[f]->ActivePredecessors.push_back( node );        // this doesn't avoid duplicates but that's fine
                node->ActiveSuccessors.push_back( m_graphNodesActive[f] );
            }
            else
            { 
                VA_ERROR( "vaSceneAsync::Begin - can't find successor '%s' for node '%s'!", succ.c_str(), node->Name.c_str() );
                m_graphNodesDirty = true;
                m_graphNodesActive.clear();
                return;
            }
        }
    }

    // verify that in no way nodes locking/using same components can run asynchronously together (and for graph issues)
    if( m_graphNodesDirty )
    {
        VA_LOG( "vaSceneAsync (scene: '%s') work node graph dirty, initializing...", m_scene.Name().c_str() );
        // 1.) brute force test each node with another
        for( int il = 0; il < m_graphNodesActive.size( ); il++ )
            for( int ir = il+1; ir < m_graphNodesActive.size( ); ir++ )
            {
                auto & nodeLeft     = *m_graphNodesActive[il];
                auto & nodeRight    = *m_graphNodesActive[ir];
                NodeRelationship rel = FindActiveNodeRelationship( nodeLeft, nodeRight );
                bool unrecoverableError = false;
                // cyclic detected?
                if( rel == vaSceneAsync::NodeRelationship::Cyclic )
                {
                    VA_ERROR( "vaSceneAsync::Begin - cyclic graph detected while checking relationship between nodes '%s' and '%s'!", nodeLeft.Name.c_str(), nodeRight.Name.c_str() );
                    unrecoverableError = true;
                }
                // if concurrent execution possible, test any potential collisions 
                if( rel == vaSceneAsync::NodeRelationship::PossiblyConcurrent )
                {
                    VA_LOG( "  work nodes '%s' and '%s' can be concurrent, checking component access rights...", m_scene.Name().c_str(), nodeLeft.Name.c_str(), nodeRight.Name.c_str() );
                    // left ReadWrite vs right Read
                    for( int i = 0; i < nodeLeft.ReadWriteComponents.size( ) && !unrecoverableError; i++ )
                        for( int j = 0; j < nodeRight.ReadComponents.size( ); j++ )
                            if( nodeLeft.ReadWriteComponents[i] == nodeRight.ReadComponents[j] )
                            {
                                VA_ERROR( "vaSceneAsync::Begin - component rights collision detected between node '%s' ReadWriteComponent '%s' and '%s' ReadComponent '%s', ", 
                                    nodeLeft.Name.c_str(),  Scene::Components::TypeName(nodeLeft.ReadWriteComponents[i]).c_str(),
                                    nodeRight.Name.c_str(), Scene::Components::TypeName(nodeRight.ReadComponents[j]).c_str() );
                                assert( false ); unrecoverableError = true; break;
                            }
                    // left Read vs right ReadWrite
                    for( int i = 0; i < nodeLeft.ReadComponents.size( ) && !unrecoverableError; i++ )
                        for( int j = 0; j < nodeRight.ReadWriteComponents.size( ); j++ )
                            if( nodeLeft.ReadComponents[i] == nodeRight.ReadWriteComponents[j] )
                            {
                                VA_ERROR( "vaSceneAsync::Begin - component rights collision detected between node '%s' ReadComponent '%s' and '%s' ReadWriteComponent '%s', ", 
                                    nodeLeft.Name.c_str(),  Scene::Components::TypeName(nodeLeft.ReadComponents[i]).c_str(),
                                    nodeRight.Name.c_str(), Scene::Components::TypeName(nodeRight.ReadWriteComponents[j]).c_str() );
                                assert( false ); unrecoverableError = true; break;
                            }
                    // left ReadWrite vs right ReadWrite
                    for( int i = 0; i < nodeLeft.ReadWriteComponents.size( ) && !unrecoverableError; i++ )
                        for( int j = 0; j < nodeRight.ReadWriteComponents.size( ); j++ )
                            if( nodeLeft.ReadWriteComponents[i] == nodeRight.ReadWriteComponents[j] )
                            {
                                VA_ERROR( "vaSceneAsync::Begin - component rights collision detected between node '%s' ReadWriteComponent '%s' and '%s' ReadWriteComponent '%s', ", 
                                    nodeLeft.Name.c_str(),  Scene::Components::TypeName(nodeLeft.ReadWriteComponents[i]).c_str(),
                                    nodeRight.Name.c_str(), Scene::Components::TypeName(nodeRight.ReadWriteComponents[j]).c_str() );
                                assert( false ); unrecoverableError = true; break;
                            }
                }
                if( unrecoverableError )
                {
                    assert( false ); // no further processing can happen
                    m_graphNodesActive.clear();
                    return;
                }
            }
        m_graphNodesDirty = false;
    }

    // run through the graph for the prologue
    {
        m_tracerContext->OnBegin( m_tracerContext->MapName("Prologue") );
        int totalProloguesDone; 
        do 
        {
            totalProloguesDone = 0;
            bool anyDone = false;
            for( int i = 0; i < m_graphNodesActive.size( ); i++ )
            {
                auto & node = m_graphNodesActive[i];
                if( node->PrologueDone )
                    { totalProloguesDone++; continue; }
                bool canRun = true;
                for( auto & predNode : node->ActivePredecessors )
                    if( !predNode->PrologueDone )
                    {
                        canRun = false;
                        break;
                    }
                if( canRun )
                {
                    m_tracerContext->OnBegin( node->MappedName );
                    node->ExecutePrologue( deltaTime, applicationTickIndex );
                    m_tracerContext->OnEnd( node->MappedName );
                    node->PrologueDone = true;
                    totalProloguesDone++;
                    anyDone = true;
                }
            }
            // all done?
            if( totalProloguesDone == m_graphNodesActive.size( ) )
                break;
            // not all done?
            if( !anyDone )
            {
                assert( false );
                VA_ERROR( "vaSceneAsync graph is borked" );
                break;
            }
        } while ( true );
        m_tracerContext->OnEnd( m_tracerContext->MapName("Prologue") );
    }

    // Async part starts so enable threaded registry use validation
    Scene::AccessPermissions & accessPermissions = m_scene.Registry().ctx<Scene::AccessPermissions>( );
    accessPermissions.SetState( Scene::AccessPermissions::State::Concurrent );


    // run through the graph for the async part, in a single-threaded test
    assert( m_tracerAsyncEntries.size() == 0 );
#ifdef VA_SCENE_ASYNC_FORCE_SINGLETHREADED
    // run through the graph for the async stuff (but single-threaded)
    {
        m_tracerContext->OnBegin( m_tracerContext->MapName("SingleThreadedAsync") );
        int totalAsyncDone; 
        do 
        {
            totalAsyncDone = 0;
            bool anyDone = false;
            for( int i = 0; i < m_graphNodesActive.size( ); i++ )
            {
                auto & node = m_graphNodesActive[i];
                if( node->AsyncDone )
                { totalAsyncDone++; continue; }
                bool canRun = true;
                for( auto & predNode : node->ActivePredecessors )
                    if( !predNode->AsyncDone )
                    {
                        canRun = false;
                        break;
                    }
                if( canRun )
                {
#ifdef _DEBUG
                    
                    {   // for component lock validation
                        std::unique_lock<std::mutex> lk( accessPermissions.MasterMutex( ) );
                        if( !accessPermissions.TryAcquire( node->ReadWriteComponents, node->ReadComponents ) )
                            VA_ERROR( "Error trying to start work task '%s' - unable to acquire component access premissions", node->Name.c_str( ) );
                    }
#endif
                    int loopIndex = 0; std::pair<int, int> nextWideParams = {0,0};
                    do 
                    {
                        assert( loopIndex < 1024 ); // probably a bug
                        vaTracer::Entry tracerNarrowEntry( node->MappedName, -1, vaCore::TimeFromAppStart( ), 0 );
                        nextWideParams = node->ExecuteNarrow( loopIndex, ConcurrencyContext{} );
                        { tracerNarrowEntry.End = vaCore::TimeFromAppStart( ); std::unique_lock lock( m_tracerAsyncEntriesMutex ); m_tracerAsyncEntries.push_back( tracerNarrowEntry ); }
                        
                        if( nextWideParams.first > 0 )
                        {
                            const int totalItems  = nextWideParams.first;
                            const int chunkSize   = nextWideParams.second;
                            vaTracer::Entry tracerWideEntry( node->MappedName, -1, vaCore::TimeFromAppStart( ), 0 );
                            for( int j = 0; j < totalItems; j += chunkSize )
                                node->ExecuteWide( loopIndex, j, std::min( totalItems, j+chunkSize ), ConcurrencyContext{} );
                            { tracerWideEntry.End = vaCore::TimeFromAppStart( ); std::unique_lock lock( m_tracerAsyncEntriesMutex ); m_tracerAsyncEntries.push_back( tracerWideEntry ); }
                        }
                        loopIndex++;
                    } while( nextWideParams.second > 0 );
                    node->AsyncDone = true;
                    totalAsyncDone++;
                    anyDone = true;
#ifdef _DEBUG
                    {   // for component lock validation
                        std::unique_lock<std::mutex> lk( accessPermissions.MasterMutex( ) );
                        accessPermissions.Release( node->ReadWriteComponents, node->ReadComponents );
                    }
#endif
                }
            }
            // all done?
            if( totalAsyncDone == m_graphNodesActive.size( ) )
                break;
            // not all done?
            if( !anyDone )
            {
                assert( false );
                VA_ERROR( "vaSceneAsync graph is borked" );
                break;
            }
        } while ( true );
        m_tracerContext->OnEnd( m_tracerContext->MapName("SingleThreadedAsync") );
    }
#else
    m_tracerContext->OnBegin( m_tracerContext->MapName("Async") );
    assert( m_masterFlow.empty( ) );
    assert( !m_masterFlowFuture.valid() );  // i.e. empty

    // this here to allow for subflow recursion - hard with lambdas only :)
    struct Async
    {
        // run Narrow inline and emplace WideNarrow to subflow if needed
        static void NarrowPart( vaSceneAsync::WorkNode & node, tf::Subflow & subflow, int loopIndex ) 
        { 
            assert( loopIndex < 1024 ); // probably a bug
            vaTracer::LocalThreadContext( )->OnBegin( node.MappedName, loopIndex );
            std::pair<int, int> nextWideParams = node.ExecuteNarrow( loopIndex, ConcurrencyContext{ &subflow } );
            vaTracer::LocalThreadContext( )->OnEnd( node.MappedName );
            if( nextWideParams.second > 0 )
                subflow.emplace( [&node, loopIndex, nextWideParams] (tf::Subflow & subflow) { WideNarrowPart( node, subflow, loopIndex, nextWideParams ); } );
        }
        // emplace Wide if needed, otherwise just run Narrow
        static void WideNarrowPart( vaSceneAsync::WorkNode & node, tf::Subflow & subflow, int loopIndex, std::pair<int, int> nextWideParams )
        {
            auto [wideItemCount, wideItemChunkSize] = nextWideParams;
            assert( wideItemCount != 0 || wideItemChunkSize != 0 );
            if( wideItemCount > 0 )
            {
                auto wideCB = [ &node, loopIndex ]( int beg, int end, tf::Subflow& subflow )
                {
                    vaTracer::LocalThreadContext( )->OnBegin( node.MappedName, -1 );
                    node.ExecuteWide( loopIndex, beg, end, ConcurrencyContext{ &subflow } );
                    vaTracer::LocalThreadContext( )->OnEnd( node.MappedName );
                };
                auto [wideStart, wideStop] = vaTF::parallel_for_emplace( subflow, 0, wideItemCount, std::move( wideCB ), wideItemChunkSize/*, item.NameWideBlock.c_str( ) */ );
                // wideStart.name( "wide_start" );
                // wideStop.name( "wide_terminate" );

                auto narrowPartT = subflow.emplace( [&node, loopIndex] (tf::Subflow & subflow) { NarrowPart( node, subflow, loopIndex+1 ); } );
                narrowPartT.succeed( wideStop );
            }
            else
                NarrowPart( node, subflow, loopIndex+1 );
        }
    };

    // create taskflow tasks
    for( int i = 0; i < m_graphNodesActive.size( ); i++ )
    {
        auto & node = *m_graphNodesActive[i];
        assert( node.TFTask.empty() );
        node.FinishedBarrier = std::promise<void>( );  // new promise
        node.FinishedBarrierFuture = node.FinishedBarrier.get_future();
        node.TFTask = m_masterFlow.emplace(
            [&node, &accessPermissions, &tracerAsyncEntriesMutex = m_tracerAsyncEntriesMutex, &tracerAsyncEntries = m_tracerAsyncEntries]( tf::Subflow & subflow )
        {
            vaTracer::Entry tracerEntry( node.MappedName, -1, vaCore::TimeFromAppStart( ) );
#ifdef _DEBUG

            {   // for component lock validation
                std::unique_lock<std::mutex> lk( accessPermissions.MasterMutex( ) );
                if( !accessPermissions.TryAcquire( node.ReadWriteComponents, node.ReadComponents ) )
                    VA_ERROR( "Error trying to start work task '%s' - unable to acquire component access premissions", node.Name.c_str( ) );
            }
#endif
            // ****************** MASTER ASYNC PROC ******************
            Async::NarrowPart( node, subflow, 0 );
            subflow.join();
            node.AsyncDone = true;
            // ****************** END OF MASTER ASYNC PROC ******************
#ifdef _DEBUG
            {   // for component lock validation
                std::unique_lock<std::mutex> lk( accessPermissions.MasterMutex( ) );
                accessPermissions.Release( node.ReadWriteComponents, node.ReadComponents );
            }
#endif
            { tracerEntry.End = vaCore::TimeFromAppStart( ); std::unique_lock lock( tracerAsyncEntriesMutex ); tracerAsyncEntries.push_back( tracerEntry ); }
            node.FinishedBarrier.set_value(); // signal we're done
        }
        );
        node.TFTask.name( node.Name );
    }

    // create taskflow dependencies
    for( auto & node : m_graphNodesActive )
    {
        for( auto & predNode : node->ActivePredecessors )
            node->TFTask.succeed( predNode->TFTask );
    }

    m_masterFlowFuture = vaTF::Executor().run( m_masterFlow );
#endif
}

int vaSceneAsync::FindActiveNodeIndex( const string & name )
{
    assert( m_isAsync );
    for( int i = 0; i < m_graphNodesActive.size( ); i++ )
        if( m_graphNodesActive[i]->Name == name )
            return i;
    return -1;
}

// this only waits for the async part to complete; it can only be called in between Begin and End and Epilogue (which is ran at End) would have not finished
bool vaSceneAsync::WaitAsyncComplete( const string & nodeName )
{
    assert( vaThreading::IsMainThread( ) );
    if( !m_isAsync )    // I guess this is fine? 
        return true;
    int nodeIndex = FindActiveNodeIndex( nodeName );
    if( nodeIndex == -1 )
        { assert( false ); return false; } // node not found?

#ifndef VA_SCENE_ASYNC_FORCE_SINGLETHREADED
    assert( m_graphNodesActive[nodeIndex]->FinishedBarrierFuture.valid() );
    m_graphNodesActive[nodeIndex]->FinishedBarrierFuture.wait();
#endif
    assert( m_graphNodesActive[nodeIndex]->AsyncDone );
    return true;
}

void vaSceneAsync::End( )
{
    assert( vaThreading::IsMainThread( ) );
    assert( m_isAsync );
    m_isAsync = false;
    m_currentDeltaTime            = 0.0f;
    m_currentApplicationTickIndex = -1;

#ifndef VA_SCENE_ASYNC_FORCE_SINGLETHREADED
    assert( m_masterFlowFuture.valid() );
    m_masterFlowFuture.wait( );
    m_masterFlowFuture = tf::Future<void>();

    m_masterFlow.clear();

    for( int i = 0; i < m_graphNodesActive.size( ); i++ )
    {
        auto & node = m_graphNodesActive[i];
        node->TFTask.reset();
        node->FinishedBarrier = std::promise<void>();
        node->FinishedBarrierFuture = std::future<void>();
    }

    m_tracerContext->OnEnd( m_tracerContext->MapName("Async") );
#endif

    // update tracing stuff
    {
        std::unique_lock lockTry( m_tracerAsyncEntriesMutex, std::try_to_lock );
        assert( lockTry.owns_lock() ); // all async work should have been finished by now so this is an indication of a serious failure
        // we need these sorted by beginnings for correct ordering later
        std::sort( m_tracerAsyncEntries.begin(), m_tracerAsyncEntries.end(), []( const vaTracer::Entry & a, const vaTracer::Entry & b) -> bool { return a.Beginning < b.Beginning; } );
        // dump all at once
        m_tracerContext->BatchAddSingleLevelEntries( m_tracerAsyncEntries.data(), (int)m_tracerAsyncEntries.size() );
        m_tracerAsyncEntries.clear();
    }

    // Async part ends, so re-enable serialized registry access
    Scene::AccessPermissions & accessPermissions = m_scene.Registry().ctx<Scene::AccessPermissions>( );
    accessPermissions.SetState( Scene::AccessPermissions::State::Serialized );

    m_tracerContext->OnBegin( m_tracerContext->MapName("Epilogue") );
    {

        // run through the graph for the epilogue
        int totalEpiloguesDone; 
        do 
        {
            totalEpiloguesDone = 0;
            bool anyDone = false;
            for( int i = 0; i < m_graphNodesActive.size( ); i++ )
            {
                auto & node = m_graphNodesActive[i];
                if( node->EpilogueDone )
                { totalEpiloguesDone++; continue; }
                bool canRun = true;
                for( auto & predNode : node->ActivePredecessors )
                    if( !predNode->EpilogueDone )
                    {
                        canRun = false;
                        break;
                    }
                if( canRun )
                {
                    m_tracerContext->OnBegin( node->MappedName );
                    node->ExecuteEpilogue( );
                    m_tracerContext->OnEnd( node->MappedName );
                    node->EpilogueDone = true;
                    totalEpiloguesDone++;
                    anyDone = true;
                }
            }
            // all done?
            if( totalEpiloguesDone == m_graphNodesActive.size( ) )
                break;
            // not all done?
            if( !anyDone )
            {
                assert( false );
                VA_ERROR( "vaSceneAsync graph is borked" );
                break;
            }
        } while ( true );
    }
    m_tracerContext->OnEnd( m_tracerContext->MapName("Epilogue") );

    if( m_graphDumpScheduled )
    {
        m_graphDumpScheduled = false;
        string graph = DumpDOTGraph( );
        string filename = vaCore::GetExecutableDirectoryNarrow() + "SceneAsyncGraph.txt";
        VA_LOG( "Dumping SceneAsync graph to '%s'", filename.c_str() );
        vaFileTools::WriteText( filename, graph );
        
        // WARNING, THERE IS A HTTPS LINK LIMIT, if stuff gets cut off, it could be because of that
        VA_LOG( "Also attempting to open the browser and visualize online...", filename.c_str() );
        string cmdLine = "start \"\" \"";
        cmdLine += "https://dreampuf.github.io/GraphvizOnline/#" + vaStringTools::URLEncode(graph);

        vaCore::System( cmdLine );
       
    }

    // reset stuff
    for( int i = 0; i < m_graphNodesActive.size( ); i++ )
    {
        auto & node = m_graphNodesActive[i];
        node->ActivePredecessors.clear();
        node->ActiveSuccessors.clear();
    }

    m_graphNodesActive.clear();

    m_tracerContext->OnEnd( m_tracerContext->MapName("BeginEndScope") );
}

string vaSceneAsync::DumpDOTGraph( )
{
    string out = "digraph SceneAsync { \n";

    // TODO, to output lock components use this:
    // nodename [label=<motion<BR /><FONT POINT-SIZE="8">readonly: list of locked stuff<BR />readwrite: list of locked stuff </FONT>>]
    // (node relationships must be in a separate line though)

    auto componentNames = []( const std::vector<int>& list ) -> string
    {
        string ret;
        for( int i = 0; i < list.size(); i++ )
            ret += ((i==0)?(""):(", ")) + Scene::Components::TypeName( list[i] );
        return ret;
    };

    for( const auto & node : m_graphNodesActive )
    {
        bool markerNode = node->Name.find( "_marker" ) != string::npos;

        out += "   " + node->Name + " [";
        bool hadAttribute = false;
        if( node->ReadComponents.size() > 0 || node->ReadWriteComponents.size() > 0 )
        {
            out += " label=<" + node->Name + "<FONT POINT-SIZE=\"6\">";
            if( node->ReadComponents.size() > 0 )
                out += "<BR />readonly: " + componentNames( node->ReadComponents );
            if( node->ReadWriteComponents.size() > 0 )
                out += "<BR />readwrite: " + componentNames( node->ReadWriteComponents );
            out += "</FONT>>";
            hadAttribute = true;
        }
        {
            if( hadAttribute )
                out += ", ";
            out += (!markerNode)?("style=filled, fillcolor=gray95"):("style=filled, fillcolor=aquamarine");
            hadAttribute = true;
        }
        out += "]\n";

        out += "   " + node->Name;
        if( node->ActiveSuccessors.size() == 0 )
        { out += "\n"; continue; }
        out += " -> { "; bool noneBefore = true;
        for( const auto nodeSucc : node->ActiveSuccessors )
        { 
            out += (noneBefore)?(""):(", "); noneBefore = false;
            out += nodeSucc->Name;
        }
        out += " }\n";
    }
    out += "}\n";

    return out;
}
