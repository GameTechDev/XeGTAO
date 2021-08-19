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

#include "Core/vaProfiler.h"
#include "Core/System/vaThreading.h"

#include "Core/vaApplicationBase.h"

#include "Rendering/vaGPUTimer.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/vaRenderDevice.h"


#include <sstream>

#ifdef VA_USE_PIX3
#define USE_PIX
#pragma warning ( push )
#pragma warning ( disable: 4100 )
#include <pix3.h>
#pragma warning ( pop )
#pragma comment(lib, "WinPixEventRuntime.lib")
#endif

using namespace Vanilla;

#ifdef VA_SCOPE_TRACE_ENABLED

void vaScopeTrace::BeginGPUTrace( vaMappedString name, int subID )
{
    assert( m_renderDeviceContext != nullptr );
    assert( m_renderDeviceContext->GetRenderDevice( ).IsFrameStarted( ) );
    m_GPUTraceHandle        = m_renderDeviceContext->GetTracer()->Begin( name, subID );
}

void vaScopeTrace::EndGPUTrace( )
{
    assert( m_renderDeviceContext != nullptr );
    assert( m_renderDeviceContext->GetRenderDevice( ).IsFrameStarted( ) );
    m_renderDeviceContext->GetTracer()->End( m_GPUTraceHandle );
}

void vaScopeTrace::BeginExternalCPUTrace( vaMappedString name, int subID )
{
    subID; name;
#ifdef VA_USE_PIX3  // this is CPU only events
    PIXBeginEvent( PIX_COLOR_INDEX( subID % 0xFF ), (const char*)name );
#endif
}

void vaScopeTrace::EndExternalCPUTrace( )
{
#ifdef VA_USE_PIX3
    PIXEndEvent( );
#endif
}

#endif

alignas( VA_ALIGN_PAD * 2 ) std::mutex                                  vaTracer::s_globalMutex;
//std::map< std::thread::id, std::weak_ptr<vaTracer::ThreadContext> >     vaTracer::s_threadContexts;
std::vector< std::weak_ptr<vaTracer::ThreadContext> >                   vaTracer::s_threadContexts;
std::weak_ptr<vaTracer::ThreadContext>                                  vaTracer::s_mainThreadContext;

vaTracer::ThreadContext::ThreadContext( const string & name, const std::thread::id & threadID, bool automaticFrameIncrement, bool isGPU ) : Name( name ), ThreadID( threadID ), AutomaticFrameIncrement( automaticFrameIncrement ), IsGPU( isGPU )
{
    m_UI_ProfilingThreadNamesDirty = true;
}

vaTracer::ThreadContext::~ThreadContext( )
{
    m_UI_ProfilingThreadNamesDirty = true;
}

void vaTracer::DumpChromeTracingReportToFile( double duration )
{
    string report = vaTracer::CreateChromeTracingReport( duration );
    if( report == "" )
    {
        VA_LOG_ERROR( "Could not generate tracing report" );
        return;
    }

    static int traceIndex = 0; traceIndex++;
    string traceFile = vaStringTools::SimpleNarrow( vaCore::GetExecutableDirectory( ) ) + vaStringTools::Format( "chrome_tracing_%03d.json", traceIndex );
    vaFileStream fileOut;
    if( !fileOut.Open( traceFile, FileCreationMode::Create, FileAccessMode::Write ) )
    {
        VA_LOG_ERROR( "Could not open tracing report file '%s'", traceFile.c_str( ) );
        return;
    }
    if( !fileOut.WriteTXT( report ) )
    {
        VA_LOG_ERROR( "Could not write tracing report to '%s'", traceFile.c_str( ) );
        return;
    };
    VA_LOG_SUCCESS( "Tracing report written to '%s' - to view open Chrome tab, navigate to 'chrome://tracing/' and drag & drop file into it", traceFile.c_str( ) );
}

string vaTracer::CreateChromeTracingReport( double duration )
{
    VA_TRACE_CPU_SCOPE( vaTracer_DumpJSONReport );

    struct ThreadData
    {
        string                  Name;
        std::vector<Entry>      Timeline;
    };
    std::list<ThreadData> threadsData;

    // collect all threads data
    {
        std::lock_guard lock( s_globalMutex ); // nobody can create thread contexts anymore
        for( auto it = s_threadContexts.begin( ); it != s_threadContexts.end( ); it++ )
        {
            std::shared_ptr<ThreadContext> context = it->lock();
            if( context != nullptr )
            {
                ThreadData data;
                data.Name   = context->Name;
                context->Capture( data.Timeline );
                threadsData.push_back( std::move(data) );
            }
        }
        for( int i = (int)s_threadContexts.size( ) - 1; i >= 0; i-- )
        {
            if( s_threadContexts[i].lock( ) == nullptr )
                s_threadContexts.erase( s_threadContexts.begin( ) + i );
        }
    }

    auto now = vaCore::TimeFromAppStart();
    auto oldest = now - duration;
    // sort and remove older than oldest
    {
        // int nameComp = a.Name.compare(b.Name); return (nameComp == 0)?(a.Depth<b.Depth):(nameComp < 0); }
        threadsData.sort( [] ( const ThreadData & a, const ThreadData & b ) { return a.Name < b.Name; } );

        for( auto threadIt = threadsData.begin( ); threadIt != threadsData.end( ); threadIt++ )
        {
            // no nead to sort anymore - sorted at insertion!
            // threadIt->Timeline.sort( [ ]( const Entry & a, const Entry & b ) { return a.Beginning < b.Beginning; } ); 
            int eraseCount = 0;
            while( eraseCount < threadIt->Timeline.size( ) && threadIt->Timeline[eraseCount].Beginning < oldest )
                eraseCount++;
            if( eraseCount > 0 )
                threadIt->Timeline.erase( threadIt->Timeline.begin(), threadIt->Timeline.begin()+eraseCount );
        }
    }

    //dump
    std::stringstream os;
    os.precision(12);
    os << '[';

    bool nextRequiresSeparator = false;
    for( auto threadIt = threadsData.begin( ); threadIt != threadsData.end( ); threadIt++ )
    {
        // if prev pass had data and this has data, add comma
        if( nextRequiresSeparator && threadIt->Timeline.size() > 0 )
        {
            os << ',';
            nextRequiresSeparator = false;  // used up, reset
        }
        nextRequiresSeparator = nextRequiresSeparator || (threadIt->Timeline.size() > 0);   // carry over separator if unused

        for( auto entryIt = threadIt->Timeline.begin(); entryIt != threadIt->Timeline.end(); ) 
        {
            os << '{'
               << "\"cat\":\"va\",";
            os << "\"name\":\"";
            os << entryIt->Name;
            os << "\",";
            os << "\"ph\":\"X\","
               << "\"pid\":1,";
            os << "\"tid\":\"" << threadIt->Name << "\",";
            os << "\"ts\":" << double( (entryIt->Beginning - now)*1000000.0 ) // -> microseconds (used to be std::chrono::duration_cast<std::chrono::microseconds>...)
               << ','
               << "\"dur\":" << double( (entryIt->End - entryIt->Beginning)*1000000.0 ) << ",";
            os << "\"args\":{\"subID\":" + std::to_string(entryIt->SubID) + "}";

            entryIt++;
            if( entryIt != threadIt->Timeline.end() )
                os << "},";
            else
                os << '}';
        }
    }
    os << "]\n";

    return os.str();
}

void vaTracer::ListAllThreadNames( std::vector<string> & outNames )
{
    outNames.clear();

    std::lock_guard lock( s_globalMutex ); // nobody can create thread contexts anymore
    for( int i = (int)s_threadContexts.size()-1; i >= 0; i-- )
    {
        std::shared_ptr<ThreadContext> context = s_threadContexts[i].lock( );
        if( context != nullptr )
            outNames.push_back( context->Name );
        else
            s_threadContexts.erase( s_threadContexts.begin()+i );
    }
}

float                       vaTracer::m_UI_ProfilingTimeToNextUpdate = 0.0f;
std::vector<string>              vaTracer::m_UI_ProfilingThreadNames;
atomic_bool                 vaTracer::m_UI_ProfilingThreadNamesDirty = false;
int                         vaTracer::m_UI_ProfilingSelectedThreadIndex = -1;
shared_ptr<vaTracerView>    vaTracer::m_UI_TracerViewActiveCollect;
shared_ptr<vaTracerView>    vaTracer::m_UI_TracerViewDisplay;
bool                        vaTracer::m_UI_TracerViewingEnabled = true;
std::vector<std::pair<string, int>> vaTracer::m_UI_SelectNodeRequest;

void vaTracer::Cleanup( bool soft )
{
    m_UI_TracerViewActiveCollect = nullptr;
    m_UI_TracerViewDisplay = nullptr;
    m_UI_ProfilingTimeToNextUpdate = 0.0f;
    m_UI_ProfilingThreadNames.clear();
    m_UI_ProfilingSelectedThreadIndex = -1;

    {
        std::lock_guard lock( s_globalMutex );
        if( !soft )
        {
            LocalThreadContextSharedPtr( ) = nullptr;
            s_mainThreadContext.reset();
            s_threadContexts.clear( );
            s_threadContexts.shrink_to_fit();
        }
        m_UI_ProfilingThreadNames.clear( );
        m_UI_ProfilingThreadNames.shrink_to_fit( );
    }
    m_UI_SelectNodeRequest.clear( );
    m_UI_SelectNodeRequest.shrink_to_fit( );
}

void vaTracer::TickImGui( vaApplicationBase & application, float deltaTime )
{
    VA_TRACE_CPU_SCOPE( Tracer_UpdateAndDrawAndAll );
    assert( vaThreading::IsMainThread() );

    if( !m_UI_TracerViewingEnabled )
    {
        ImGui::Text( "Stats viewing disabled - likely being captured from another tool" );
        return;
    }

    if( ImGui::Button( "Dump perf tracing report to file (CTRL+T)", {-1, 0} ) )
        vaTracer::DumpChromeTracingReportToFile();
    if( ImGui::IsItemHovered() )  ImGui::SetTooltip( "This writes out a chrome tracing report to a file located \nin the same folder as executable - to view open Chrome tab, \nnavigate to 'chrome://tracing/' and drag & drop file into it" );

    ImGui::Separator();
    
    // first time initialize 
    if( m_UI_TracerViewActiveCollect == nullptr )
    {
        m_UI_TracerViewActiveCollect = std::make_shared<vaTracerView>( );
        m_UI_TracerViewDisplay = std::make_shared<vaTracerView>( );
        application.Event_Tick.Add( m_UI_TracerViewActiveCollect, &vaTracerView::TickFrame );
        application.Event_Tick.Add( m_UI_TracerViewDisplay , &vaTracerView::TickFrame );

        m_UI_ProfilingTimeToNextUpdate = 0.0f;
        m_UI_ProfilingThreadNames.clear( );
        m_UI_ProfilingSelectedThreadIndex = -1;

        // shared_ptr<void> isInitializedToken = m_UI_TracerViewActiveCollect; // can be any of the above 
        // application.Event_Stopped.AddWithToken( isInitializedToken, [ &application, isInitializedToken ] 
        // {
        //     application.Event_BeforeStopped.Remove( isInitializedToken );
        //     m_UI_TracerViewActiveCollect    = nullptr;
        //     m_UI_TracerViewDisplay          = nullptr;
        // } );
    }

    if( m_UI_ProfilingThreadNamesDirty )
        m_UI_ProfilingThreadNames.clear();

    // is the list of threads empty? update it!
    if( m_UI_ProfilingThreadNames.size() == 0 )
    {
        //VA_TRACE_CPU_SCOPE( UpdateNames );

        // preserve default
        string prevName = "";
        if( m_UI_ProfilingSelectedThreadIndex >= 0 && m_UI_ProfilingSelectedThreadIndex < m_UI_ProfilingThreadNames.size( ) )
            prevName = m_UI_ProfilingThreadNames[m_UI_ProfilingSelectedThreadIndex];

        vaTracer::ListAllThreadNames( m_UI_ProfilingThreadNames );
        std::sort( m_UI_ProfilingThreadNames.begin( ), m_UI_ProfilingThreadNames.end( ), [ ]( const string& l, const string& r ) { return l < r; } );

        for( int i = 0; i < m_UI_ProfilingThreadNames.size( ); i++ )
            if( m_UI_ProfilingThreadNames[i] == prevName )
                m_UI_ProfilingSelectedThreadIndex = i;

        // this was achieved by sorting and adding !! to the GPU context
        // // stick to GPU profiling as a default
        // if( m_UI_ProfilingSelectedThreadIndex == -1 )
        // {
        //     for( int i = 0; i < m_UI_ProfilingThreadNames.size( ); i++ )
        //         if( m_UI_ProfilingThreadNames[i] == "GPUMainContext" )
        //             m_UI_ProfilingSelectedThreadIndex = i;
        // }
        if( m_UI_ProfilingSelectedThreadIndex == -1 )
            m_UI_ProfilingSelectedThreadIndex = 0;
        m_UI_ProfilingThreadNamesDirty = false;
    }

    // Do we need to disconnect/connect/swap?
    m_UI_ProfilingTimeToNextUpdate -= deltaTime;
    bool updateTriggered = false;
    if( m_UI_ProfilingTimeToNextUpdate < 0 )
    {
        //VA_TRACE_CPU_SCOPE( SwapAndConnect );

        m_UI_ProfilingTimeToNextUpdate = std::max( 0.0f, m_UI_ProfilingTimeToNextUpdate + c_UI_ProfilingUpdateFrequency );

        //
        // finish collecting with the last one...
        m_UI_TracerViewActiveCollect->Disconnect( m_UI_TracerViewDisplay );
        // ...and update 
        m_UI_TracerViewActiveCollect.swap( m_UI_TracerViewDisplay );

        // find valid viewing target context name
        string targetName;
        if( m_UI_ProfilingThreadNames.size( ) != 0 )
        {
            m_UI_ProfilingSelectedThreadIndex = vaMath::Clamp( m_UI_ProfilingSelectedThreadIndex, 0, (int)m_UI_ProfilingThreadNames.size( ) - 1 );
            m_UI_TracerViewActiveCollect->ConnectToThreadContext( m_UI_ProfilingThreadNames[m_UI_ProfilingSelectedThreadIndex], vaTracer::c_UI_ProfilingUpdateFrequency * 1.5f );
        }
        updateTriggered = true;
    }

    // Display UI
    if( m_UI_ProfilingThreadNames.size( ) != 0 )
    {
        ImGui::PushItemWidth( 0.0f ); 
        // VA_TRACE_CPU_SCOPE( DrawUI );
        string prevName = "";
        if( m_UI_ProfilingSelectedThreadIndex >= 0 && m_UI_ProfilingSelectedThreadIndex < m_UI_ProfilingThreadNames.size( ) )
            prevName = m_UI_ProfilingThreadNames[m_UI_ProfilingSelectedThreadIndex];
        m_UI_ProfilingSelectedThreadIndex = vaMath::Clamp( m_UI_ProfilingSelectedThreadIndex, 0, (int)m_UI_ProfilingThreadNames.size( ) - 1 );
        if( ImGuiEx_Combo( "CPU/GPU thread", m_UI_ProfilingSelectedThreadIndex, m_UI_ProfilingThreadNames ) )
        {
            if( m_UI_ProfilingSelectedThreadIndex >= 0 && m_UI_ProfilingSelectedThreadIndex < m_UI_ProfilingThreadNames.size( )
                && m_UI_ProfilingThreadNames[m_UI_ProfilingSelectedThreadIndex] != prevName )
                m_UI_ProfilingTimeToNextUpdate = 0; // trigger update next frame
        }

        m_UI_TracerViewDisplay->TickImGui( application );
        ImGui::PopItemWidth( );
    }

    // are you holding a pointer returned by GetViewableTracerView? you should not!
    assert( m_UI_TracerViewActiveCollect.use_count() == 1 );
    assert( m_UI_TracerViewDisplay.use_count() == 1 );

    // invalidate & update next frame
    if( updateTriggered )
        m_UI_ProfilingThreadNames.clear( );
}

void vaTracer::SetTracerViewingUIEnabled( bool enable )    
{
    if( !enable && m_UI_TracerViewingEnabled )
    {
        if( m_UI_TracerViewActiveCollect != nullptr )
            m_UI_TracerViewActiveCollect->Disconnect();
        Cleanup( true );
    }

    m_UI_TracerViewingEnabled = enable;   
}

void vaTracerView::ConnectToThreadContext( const string & name, float connectionTimeout )
{
    assert( vaThreading::IsMainThread( ) ); // these can only be called from one main thread (or UI thread if they get split or whatever)

    // verify we're not already connected
    {
        std::unique_lock lock( m_viewMutex );
        auto currentConnection = m_connectedThreadContext.lock( );
        if( currentConnection != nullptr )
        {
            lock.unlock( );
            assert( false );
            Disconnect( );
        }

        // if new name changed from old, need to reset so we don't show other thread context's data merged with the new
        if( name != m_connectionName )
        {
            m_nameChanged = true;
            lock.unlock();
            Reset();
        }
        else
        {
            m_nameChanged = false;
        }

    }

    // first find if we can even connect
    std::shared_ptr<vaTracer::ThreadContext> captureContext = nullptr;
    {
        string nameToSearch = name;
        size_t nameLengthToSearch = name.length();
        if( nameLengthToSearch == 0 )
        {
            assert( false );
            return;
        }
        if( name[nameLengthToSearch-1] == '*' )
        {
            nameLengthToSearch--;
            nameToSearch = name.substr(0, nameLengthToSearch);
        }

        std::unique_lock globalLock( vaTracer::s_globalMutex ); // nobody can create thread contexts anymore
        for( auto it = vaTracer::s_threadContexts.begin( ); it != vaTracer::s_threadContexts.end( ); it++ )
        {
            std::shared_ptr<vaTracer::ThreadContext> context = it->lock( );

            if( (context != nullptr) && (nameLengthToSearch <= context->Name.length()) && (nameToSearch == context->Name.substr(0, nameLengthToSearch)) )
            {
                // found it! we rule.
                captureContext      = context;
                m_connectionName    = context->Name;
                m_connectionIsGPU   = context->IsGPU;
                break;
            }
        }
        if( captureContext == nullptr )
            return;
    }

    {
        std::scoped_lock lock( m_viewMutex, captureContext->TimelineMutex );


        // once this goes out of scope, callback can't call us anymore!
        weak_ptr< vaTracerView > weakThis = this->shared_from_this();

        // are we replacing another viewer? could mess up things - shouldn't happen
        assert( captureContext->AttachedViewer.lock() == nullptr );

        // m_lastConnectionGUID        = vaCore::GUIDCreate();

        // capture us 
        captureContext->AttachedViewer = weakThis;

        m_connectedThreadContext    = captureContext;
        m_frameCountWhileConnected  = 0;
        m_lastConnectedTime         = vaCore::TimeFromAppStart();
        m_connectionTimeoutTime     = m_lastConnectedTime + connectionTimeout;
        PreUpdateRecursive();
    }
}

void vaTracerView::Disconnect( shared_ptr<vaTracerView> prevUIView )
{
    assert( vaThreading::IsMainThread( ) ); // these can only be called from one main thread (or UI thread if they get split or whatever)

    std::shared_ptr<vaTracer::ThreadContext> connectedContext;

    {
        std::lock_guard lock( m_viewMutex );
        connectedContext = m_connectedThreadContext.lock( );
    }
    if( connectedContext != nullptr )
    {
        std::scoped_lock lock( m_viewMutex, connectedContext->TimelineMutex );

        // is it attached to us? should be!
        assert( connectedContext->AttachedViewer.lock() == this->shared_from_this() );
        
        // disconnect!
        connectedContext->AttachedViewer.reset();

        // also remove link from our end
        m_connectedThreadContext.reset();
    }

    // the syncing is needed to collect all UI changes - otherwise there would be 2 UI states when swapping
    if( prevUIView != nullptr && prevUIView->GetConnectionName() == m_connectionName )
    {
        //assert( !m_nameChanged );
        //assert( !prevUIView->m_nameChanged );

        std::lock_guard lock( m_viewMutex );
        std::lock_guard lockExternal( prevUIView->m_viewMutex );

        for( int i = 0; i < prevUIView->m_rootNodes.size( ); i++ )
        {
            bool found = false;
            for( int j = 0; j < m_rootNodes.size( ); i++ )
            {
                if( m_rootNodes[j]->Name == prevUIView->m_rootNodes[i]->Name )
                {
                    prevUIView->m_rootNodes[i]->SyncRecursive( *prevUIView, m_rootNodes[j] );
                    found = true;
                    break;
                }
            }
            if( !found && prevUIView->m_rootNodes[i]->LastSeenAge < Node::LastSeenAgeToKeepAlive )
            {
                vaTracerView::Node* newNode;
                m_rootNodes.push_back( newNode = AllocateNode( ) );
                newNode->Reset( true );
                newNode->Name = prevUIView->m_rootNodes[i]->Name;
                newNode->RecursionDepth = prevUIView->m_rootNodes[i]->RecursionDepth;
                newNode->LastSeenAge = prevUIView->m_rootNodes[i]->LastSeenAge;
                prevUIView->m_rootNodes[i]->SyncRecursive( *this, newNode );
            }
        }
    }

    {
        std::lock_guard lock( m_viewMutex );
        
        PostUpdateRecursive();
        m_frameCountWhileConnected = 0;
    }
}

void vaTracerView::TickFrame( float )
{
    assert( vaThreading::IsMainThread( ) ); // these can only be called from one main thread (or UI thread if they get split or whatever)

    {
        std::lock_guard lock( m_viewMutex );
        auto connectedContext = m_connectedThreadContext.lock( );
        if( connectedContext != nullptr && connectedContext->AutomaticFrameIncrement )
        {
            // we freeze data collection if UI is disabled because there's no swapping then
            if( vaCore::TimeFromAppStart( ) <= m_connectionTimeoutTime )
            {
                m_frameCountWhileConnected++;
                m_frameSortCounter = 0;
            }
        }
    }
}

void vaTracerView::UpdateCallback( vaTracer::Entry * timelineChunk, int timelineChunkCount, bool incrementFrameCounter )
{
    double currentTime = vaCore::TimeFromAppStart( );
    if( currentTime > m_connectionTimeoutTime ) 
        return;

    // !!! this could trigger recursion when it closes and also submit incomplete data to timelineChunk which is why we don't instrument this function, ever!!!
    // VA_TRACE_CPU_SCOPE( vaTracerView_UpdateCallback ); 
    // !!! this could trigger recursion when it closes and also submit incomplete data to timelineChunk which is why we don't instrument this function, ever!!!

    {
        std::lock_guard lock( m_viewMutex );

        if( m_connectedThreadContext.lock() == nullptr )
            return;

        if( incrementFrameCounter )
        {
            m_frameCountWhileConnected++;
            m_frameSortCounter = 0;
        }

        std::vector< const vaTracer::Entry* > currentSrcStack;   // keeping pointers to these is ok only because timelineChunk is const
        std::vector< vaTracerView::Node* > currentDstStack;      // keeping pointers to these is ok because they are created as individual objects and added to vector as pointers

        double lastBeginTime = -1e9;
        for( int nodeIndex = 0; nodeIndex < timelineChunkCount; nodeIndex++ )
        {
            const vaTracer::Entry & srcNode = timelineChunk[nodeIndex];

            assert( currentSrcStack.size( ) == currentDstStack.size( ) );

            // pop out if our new srcNode is out of range of its parent nodes or same time but lower depth
            while( currentSrcStack.size( ) > 0 && (currentSrcStack.back( )->End < srcNode.End || (currentSrcStack.back( )->End == srcNode.End && currentSrcStack.back( )->Depth >= srcNode.Depth) ) )
            {
                assert( currentSrcStack.back( )->End <= srcNode.Beginning );
                currentSrcStack.pop_back( );

                // track the destinations
                currentDstStack.pop_back( );
            }
            // add current to the stack - we'll get out if the next one is outside of the current
            currentSrcStack.push_back( &srcNode );

            // find existing destination node if exists, otherwise add new
            {
                // dst container to search
                std::vector<vaTracerView::Node*>& dstNodes = ( ( currentDstStack.size( ) == 0 ) ? ( m_rootNodes ) : ( currentDstStack.back( )->ChildNodes ) );
                vaTracerView::Node* dstNode;

                auto it = std::find_if( dstNodes.begin( ), dstNodes.end( ), [&name = srcNode.Name]( const vaTracerView::Node* node ) { return name == node->Name; } );
                if( it == dstNodes.end( ) )
                {   // not found? create new
                    dstNodes.push_back( dstNode = AllocateNode( ) );
                    dstNode->Reset( true );
                    dstNode->Name = srcNode.Name;
                }
                else
                    dstNode = ( *it );

                currentDstStack.push_back( dstNode );

                double spanTime = srcNode.End - srcNode.Beginning;
                
                assert( srcNode.Beginning >= lastBeginTime );
                lastBeginTime = srcNode.Beginning;

                dstNode->TimeTotal += spanTime;
                dstNode->TimeTotalMax = ( dstNode->TimeTotalMax == -0.0 ) ? ( spanTime ) : ( std::max( dstNode->TimeTotalMax, spanTime ) );
                dstNode->TimeTotalMin = ( dstNode->TimeTotalMin == -0.0 ) ? ( spanTime ) : ( std::min( dstNode->TimeTotalMin, spanTime ) );
                dstNode->Instances++;
                dstNode->RecursionDepth = (int)currentDstStack.size( ) - 1;
                dstNode->LastSeenAge = 0;
                dstNode->SortOrder = m_frameSortCounter;
                m_frameSortCounter++;
            }
        }
    }
}

void vaTracerView::PreUpdateRecursive( )
{
    for( vaTracerView::Node* node : m_rootNodes )
        node->PreUpdateRecursive( );
}

void vaTracerView::PostUpdateRecursive( )
{
    for( int i = (int)m_rootNodes.size( ) - 1; i >= 0; i-- )
    {
        m_rootNodes[i]->PostUpdateRecursive( *this );
        if( m_rootNodes[i]->LastSeenAge > Node::LastSeenAgeToKeepAlive )
        {
            ReleaseNode( m_rootNodes[i] );
            m_rootNodes[i] = nullptr;
            if( m_rootNodes.size( ) - 1 > i )
                m_rootNodes[i] = m_rootNodes[m_rootNodes.size( ) - 1];
            m_rootNodes.pop_back( );
        }
    }
    std::sort( m_rootNodes.begin(), m_rootNodes.end(), [ ]( const Node* a, const Node* b ) { return a->SortOrder < b->SortOrder; } ); // { return a->Name < b->Name; } );
}

void vaTracerView::TickImGuiRecursive( Node * node, bool forceSelect )
{
    bool leaf = node->ChildNodes.size( ) == 0;

    forceSelect |= vaTracer::FindSelectNodeRequest( node->Name, true );
    if( forceSelect )
        node->Selected = true;

    const int indent = 2;

    string text;
    text.insert( text.end( ), (int)(node->RecursionDepth * indent), ' ' );
    text += (leaf)?(" "):((node->Opened)?("-"):("+"));
    text += " " + node->Name;

    //ImGui::SetNextTreeNodeOpen( node->Opened );
    if( ImGui::Selectable( text.c_str(), node->Selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick ) )
    { 
        if( ImGui::IsMouseDoubleClicked( 0 ) )
            node->Opened = !node->Opened;
        node->Selected = !node->Selected;
    }

    ImGui::NextColumn( );

#if 1
    string infoText = vaStringTools::Format( "%4.03f", node->TimeTotalAvgPerFrame * 1000.0f );
#else
    string infoText = vaStringTools::Format( "%6d", node->Instances );
#endif

    if( node->Instances == 0 )
        infoText = "<empty>";
    
    // align to right
    ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize( infoText.c_str() ).x - ImGui::GetStyle().ItemSpacing.x * 2.0f );
    
    ImGui::Text( infoText.c_str() );

    ImGui::NextColumn( );

    if( node->Opened )
    {
        for( int i = 0; i < node->ChildNodes.size( ); i++ )
        {
            TickImGuiRecursive( node->ChildNodes[i], forceSelect );
        }
    }

}

void vaTracerView::TickImGui( vaApplicationBase & application )
{
    application;
    assert( vaThreading::IsMainThread( ) ); // these can only be called from one main thread (or UI thread if they get split or whatever)
    std::lock_guard lock( m_viewMutex );

    if( m_connectedThreadContext.lock( ) != nullptr )
    {
        // we shouldn't be in this function as we're connected and receiving new data!
        assert( false );
        ImGui::Text( "ERROR IN TickImGuiRecursive" );
        return;
    }

    //ImGui::BeginChild( "profilerframe", {0, ImGui::GetTextLineHeightWithSpacing() * 10}, true, ImGuiWindowFlags_AlwaysAutoResize );

    // see imgui_demo.cpp
    // bool align_label_with_current_x_position = true;
    // if( align_label_with_current_x_position )
    //     ImGui::Unindent( ImGui::GetTreeNodeToLabelSpacing( ) );

    float mainPartWidth = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( "00000.000" ).x - ImGui::GetStyle().ItemSpacing.x; // (ImGui::GetContentRegionAvail().x * 3.0f) / 4.0f;

    ImGui::Columns( 2, "profilercolumns" ); // 4-ways, with border
    ImGui::SetColumnWidth( 0, mainPartWidth );
    ImGui::Separator( );
#ifdef _DEBUG
    ImGui::Text( "== DEBUG BUILD, metrics not reliable =="); 
#else
    ImGui::Text( application.GetVsync()?("== VSYNC ON, metrics not reliable =="):("") );          
#endif
    ImGui::NextColumn( );
    ImGui::Text( "ms/frame" );  ImGui::NextColumn( );
    ImGui::Separator( );

    for( int i = 0; i < m_rootNodes.size(); i++ )
    {
        TickImGuiRecursive( m_rootNodes[i], false );
    }

    ImGui::Columns( 1 );
    // ImGui::SetColumnWidth(0);

    // if( align_label_with_current_x_position )
    //     ImGui::Indent( ImGui::GetTreeNodeToLabelSpacing( ) );

    //ImGui::EndChild();
}

const vaTracerView::Node * vaTracerView::FindNodeRecursive( const string & name ) const
{
    assert( !IsConnected( ) );

    for( int i = 0; i < m_rootNodes.size(); i++ )
    {
        const Node * retVal = m_rootNodes[i]->FindRecursive( name );
        if( retVal != nullptr )
            return retVal;
    }
    return nullptr;
}

bool vaTracer::FindSelectNodeRequest( const string & name, bool removeIfFound )
{
    assert( vaThreading::IsMainThread() ); // <- can be upgraded to thread-safe but hasn't been done yet
    for( int i = 0; i < m_UI_SelectNodeRequest.size( ); i++ )
    {
        if( m_UI_SelectNodeRequest[i].first == name )
        {
            if( removeIfFound )
                m_UI_SelectNodeRequest.erase( m_UI_SelectNodeRequest.begin() + i );
            return true;
        }
    }
    return false;
}

void vaTracer::SelectNodeInUI( const string & name )
{
    assert( vaThreading::IsMainThread() ); // <- can be upgraded to thread-safe but hasn't been done yet

    // not already in?
    if( !FindSelectNodeRequest( name, false ) )
        m_UI_SelectNodeRequest.push_back( { name, 0 } );
    else
        { assert( false ); }
}
