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

// Perhaps consider using https://github.com/bombomby/optick - looks really good and MIT; there' also Tracy 
// (https://bitbucket.org/wolfpld/tracy/src/master/) 

#include "Core/vaCoreIncludes.h"

#include <unordered_map>

namespace Vanilla
{
    class vaRenderDeviceContext;

    class vaTracerView;


    // multithreaded timeline-based begin<->end tracing with built-in json output for chrome://tracing!
    // for details and extension ideas, see https://aras-p.info/blog/2017/01/23/Chrome-Tracing-as-Profiler-Frontend/ and 
    // https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU and
    // https://www.gamasutra.com/view/news/176420/Indepth_Using_Chrometracing_to_view_your_inline_profiling_data.php
    class vaTracer
    {

    public:

        struct Entry
        {
            double                                              Beginning;
            double                                              End;
            const char *                                        Name;
            int                                                 Depth;          // depth used to determine inner/outer if Beginning/End-s are same
            int                                                 SubID;          // used to track different entries with same names - for ex, to correlate CPU <-> GPU calls

            Entry( const char * name, int depth, const double & beginning, int subID = 0 ) : Name(name), Depth(depth), Beginning(beginning), End(beginning), SubID( subID ) { }
            Entry( ) { }

        };

        struct TimelineContainer
        {
            std::vector<Entry>                                  ContainerA;
            std::vector<Entry>                                  ContainerB;

            std::vector<Entry> *                                Front   = &ContainerA;  // <- earlier (older) part of the timeline - these are the ones that get old
            std::vector<Entry> *                                Back    = &ContainerB;  // <- later (newer) part of the timeline - this is where we're adding new
            int                                                 FrontFirstValidIndex = 0;

            void                                                AppendMove( std::vector<Entry> && entries )
            {
                Back->insert( Back->end(), std::make_move_iterator(std::begin(entries)), std::make_move_iterator(std::end(entries)) );
                entries.clear();
            }
            void                                                Append( const std::vector<Entry> & entries )
            {
                Back->insert( Back->end( ), entries.begin(), entries.end() );
            }
            void                                                Append( Entry * entries, int count )
            {
                Back->insert( Back->end( ), entries, &entries[count] );
            }

            void                                                Defrag( double oldest )
            {
                // loop through items and drop older
                while( FrontFirstValidIndex < Front->size( ) && (*Front)[FrontFirstValidIndex].Beginning < oldest )
                    FrontFirstValidIndex++;
                // if dropped everything from front, drop whole buffer, swap
                if( FrontFirstValidIndex == Front->size( ) && Back->size() != 0 )
                {
                    Front->clear();
                    FrontFirstValidIndex = 0;
                    std::swap( Front, Back );
                    Defrag( oldest );
                }
            }
        };

        struct ThreadContext
        {
            string                                              Name;
            std::thread::id                                     ThreadID;               // or uninitialized for 'virtual' contexts (such as used for GPU tracing)
            bool                                                AutomaticFrameIncrement;
            bool                                                IsGPU;

            vaStringDictionary                                  NameDictionary;

            std::shared_mutex                                   TimelineMutex;
            TimelineContainer                                   Timeline;

            int                                                 SortOrderCounter    = 0;

            std::vector<Entry>                                  LocalTimeline;
            std::vector<int>                                    CurrentOpenStack;       // stack of LocalTimeline indices
            double                                              NextDefragTime      = 0;

            std::weak_ptr<vaTracerView>                         AttachedViewer;         // secured with TimelineMutex!!!

            // just a marker saying that it needs to get re-created
            std::atomic_bool                                    Abandoned           = false;

            ThreadContext( const char * name, const std::thread::id & id = std::thread::id(), bool automaticFrameIncrement = true, bool isGPU = false );
            ~ThreadContext( );

            vaMappedString                                      MapName( const string & name )       { return NameDictionary.Map( name ); }
            vaMappedString                                      MapName( const char * name )         { return NameDictionary.Map( name ); }

            // inline void                                         OnEvent( const string & name )  { name; assert( false ); }

//            inline void                                         OnBegin( const string & name, int subID =0  )
//            {
//                OnBegin( MapName(name), subID );
//            }

            inline void                                         OnBegin( vaMappedString name, int subID = 0 )
            {
                auto now = vaCore::TimeFromAppStart( );
                LocalTimeline.emplace_back( Entry( name, (int)CurrentOpenStack.size( ), now, subID ) );
                CurrentOpenStack.push_back( (int)LocalTimeline.size( ) - 1 );
            }

#ifdef _DEBUG
            inline void                                         OnEnd( vaMappedString verifyName );
#else
            inline void                                         OnEnd( );
#endif

            inline void                                         BatchAddFrame( Entry * entries, int count );

            inline void                                         Capture( std::vector<Entry> & outEntries )
            {
                std::lock_guard lock( TimelineMutex );
                // outEntries;
                // move;
                // if( move )
                //     outEntries = std::move( Timeline );
                // else
                //     outEntries = Timeline;

                outEntries.insert( outEntries.end( ), Timeline.Front->begin( ) + Timeline.FrontFirstValidIndex, Timeline.Front->end( ) );
                outEntries.insert( outEntries.end( ), Timeline.Back->begin( ), Timeline.Back->end( ) );
            }
            inline void                                         CaptureLast( std::vector<Entry> & outEntries, double oldestAge )
            {
                std::lock_guard lock( TimelineMutex );
                // for( auto it = Timeline.rbegin(); it != Timeline.rend(); it++ )
                // {
                //     outEntries.emplace_back( *it );
                //     if( it->Depth == 0 && it->End < oldestAge ) // stop only at depth == 0 or otherwise it will be ugly to parse
                //         break;
                // }
                // 

                assert( false ); // warning, this is cleaning up Timeline for older than oldestAge permanently - is this what we really want?
                Timeline.Defrag( oldestAge );
                outEntries.insert( outEntries.end( ), Timeline.Front->begin( ) + Timeline.FrontFirstValidIndex, Timeline.Front->end( ) );
                outEntries.insert( outEntries.end( ), Timeline.Back->begin( ), Timeline.Back->end( ) );
            }
        };

    private:
        friend class vaTracerView;

        alignas( VA_ALIGN_PAD * 2 ) static std::mutex           s_globalMutex;
        static //std::map< std::thread::id, std::weak_ptr<ThreadContext> >
               std::vector< std::weak_ptr<ThreadContext> >
                                                                s_threadContexts;
        static weak_ptr<ThreadContext>                          s_mainThreadContext;
        static constexpr double                                 c_maxCaptureDuration  = 4.0; // seconds
//
//        static thread_local shared_ptr<Thread>                  s_threads;

    private:
        inline static shared_ptr<ThreadContext> &               LocalThreadContextSharedPtr( )
        {
            static thread_local shared_ptr<ThreadContext> localThreadContext = nullptr;

            return localThreadContext;
        }

    public:
        inline static ThreadContext *                           LocalThreadContext( )
        {
            shared_ptr<ThreadContext> & localThreadContext = LocalThreadContextSharedPtr();
            if( localThreadContext == nullptr )
            {
                std::lock_guard lock( s_globalMutex );
                if( vaThreading::ThreadLocal().MainThreadSynced )
                { 
                    assert( !vaThreading::ThreadLocal().MainThread );
                    localThreadContext = s_mainThreadContext.lock();
                    assert( localThreadContext != nullptr );
                }
                else
                {
                    localThreadContext = std::make_shared<ThreadContext>( vaThreading::GetThreadName(), std::this_thread::get_id() );
                    // s_threadContexts.emplace( std::this_thread::get_id(), localThreadContext );
                    s_threadContexts.push_back( localThreadContext );
                    if( vaThreading::ThreadLocal().MainThread )
                    {
                        assert( !vaThreading::ThreadLocal().MainThreadSynced );
                        s_mainThreadContext = localThreadContext;
                    }
                }
            }
            return localThreadContext.get();
        }

        // caller is responsible for keeping it alive! we only keep a weak reference and it gets deleted if not there
        inline static shared_ptr<ThreadContext>                 CreateVirtualThreadContext( const char* name, bool isGPU )
        {
            std::lock_guard lock( s_globalMutex );
            shared_ptr<ThreadContext> retContext = std::make_shared<ThreadContext>( name, std::thread::id(), false, isGPU );
            // s_threadContexts.emplace( std::this_thread::get_id(), localThreadContext );
            s_threadContexts.push_back( retContext );
            return retContext;
        }

        static void                                             DumpChromeTracingReportToFile( double duration = c_maxCaptureDuration );
        static string                                           CreateChromeTracingReport( double duration = c_maxCaptureDuration );
        static void                                             ListAllThreadNames( std::vector<string> & outNames );
        //static void                                             UpdateToView( vaTracerView & outView, float historyDuration );

        static constexpr float                                  c_UI_ProfilingUpdateFrequency   = 1.5f;
        static float                                            m_UI_ProfilingTimeToNextUpdate;
        static std::vector<string>                              m_UI_ProfilingThreadNames;
        static atomic_bool                                      m_UI_ProfilingThreadNamesDirty;
        static int                                              m_UI_ProfilingSelectedThreadIndex;
        
        static shared_ptr<vaTracerView>                         m_UI_TracerViewActiveCollect;       // this one collects the data while the other one 
        static shared_ptr<vaTracerView>                         m_UI_TracerViewDisplay;

        static bool                                             m_UI_TracerViewingEnabled;

        static std::vector<std::pair<string, int>>              m_UI_SelectNodeRequest;

    public:
        static bool                                             IsTracerViewingUIEnabled( )                 { return m_UI_TracerViewingEnabled;     }
        static void                                             SetTracerViewingUIEnabled( bool enable );

        static void                                             TickImGui( class vaApplicationBase & application, float deltaTime );

        // don't hold this pointer over TickImGui call or use it from non-main thread - none of these are supported!
        static shared_ptr<vaTracerView const>                   GetViewableTracerView( )            { assert( vaThreading::IsMainThread() ); return m_UI_TracerViewDisplay; };

        static void                                             SelectNodeInUI( const string & name );

    private:
        static bool                                             FindSelectNodeRequest( const string & name, bool removeIfFound );

    private:
        friend vaCore;
        static void                                             Cleanup( bool soft );
    };

    // A look into traces on a specific thread captured by vaTracer; 
    // will provide Imgui UI too and some tools for it.
    class vaTracerView : public std::enable_shared_from_this<vaTracerView>
    {
    public:
        struct Node
        {
            string              Name;
            int                 SortOrder       = 0;      // order in additions; just so the display is somewhat consistent

            // these times are relative to vaTracerReport::Beginning, converted to milliseconds
            // they represent total recorded during the vaTracerReport::end-beginning time span (excluding parts of the sections outside the time span)
            double              TimeTotal           = 0.0;
            double              TimeTotalAvgPerInst = 0.0;
            double              TimeTotalAvgPerFrame= 0.0;
            double              TimeTotalMax        = 0.0;   
            double              TimeTotalMin        = 0.0;   
            double              TimeSelfAvgPerFrame = 0.0;
            int                 Instances           = 0;        // how many times was recorded during the vaTracerReport::end-beginning time span

            int                 RecursionDepth  = 0;        // RootNode is 0

            std::vector<Node*>  ChildNodes;

            // UI parameters
            bool                Opened          = true;
            bool                Selected        = false;
            int                 LastSeenAge     = 0;        // how many last Update-s in a row it had 0 instances (will be removed after n)
            
            static const int    LastSeenAgeToKeepAlive      = 1;

        public:
            const Node * FindRecursive( const string & name ) const
            {
                if( name == this->Name )
                    return this;
                else
                {
                    for( int i = 0; i < ChildNodes.size(); i++ )
                    {
                        const Node * retVal = ChildNodes[i]->FindRecursive( name );
                        if( retVal != nullptr )
                            return retVal;
                    }
                }
                return nullptr;
            }

        private:
            friend vaTracerView;
            void Reset( bool fullReset )
            {
                if( fullReset )
                {
                    assert( ChildNodes.size() == 0 );
                    Name            = "";
                    Opened          = true;
                    Selected        = false;
                    LastSeenAge     = 0;
                }
                TimeTotal               = 0.0;
                TimeTotalAvgPerInst     = 0.0;
                TimeTotalAvgPerFrame    = 0.0;
                TimeTotalMin            = -0.0;
                TimeTotalMax            = -0.0;
                TimeSelfAvgPerFrame     = 0.0;
                Instances               = 0;
                SortOrder               = 0;
            }

            void ReleaseRecursive( vaTracerView & view )   
            { 
                for( int i = 0; i < ChildNodes.size(); i++ )
                    ChildNodes[i]->ReleaseRecursive( view );
                ChildNodes.clear();
                
                view.ReleaseNode( this );
            }

            void PreUpdateRecursive( )
            {
                for( int i = 0; i < ChildNodes.size( ); i++ )
                    ChildNodes[i]->PreUpdateRecursive( );

                Reset( false );
            }

            // this node has fresh UI settings, other node has latest data - exchange
            void SyncRecursive( vaTracerView & view, Node * freshDataNode )
            {
                // UI stuff goes the other way around
                freshDataNode->Opened   = Opened  ;
                freshDataNode->Selected = Selected;

                // LastSeenAge = freshDataNode->LastSeenAge+1;

                for( int j = 0; j < freshDataNode->ChildNodes.size( ); j++ )
                {
                    bool found = false;
                    for( int i = 0; i < ChildNodes.size( ); i++ )
                    {
                        if( ChildNodes[i]->Name == freshDataNode->ChildNodes[j]->Name )
                        {
                            ChildNodes[i]->SyncRecursive( view, freshDataNode->ChildNodes[j] );
                            found = true;
                            break;
                        }
                    }
                    if( !found && freshDataNode->ChildNodes[j]->LastSeenAge < Node::LastSeenAgeToKeepAlive )
                    {
                        vaTracerView::Node* newNode;
                        ChildNodes.push_back( newNode = view.AllocateNode( ) );
                        newNode->Reset( true );
                        newNode->Name = freshDataNode->ChildNodes[j]->Name;
                        newNode->RecursionDepth = freshDataNode->ChildNodes[j]->RecursionDepth;
                        newNode->LastSeenAge = freshDataNode->ChildNodes[j]->LastSeenAge;
                        freshDataNode->ChildNodes[j]->SyncRecursive( view, newNode );
                    }
                }
            }

            void PostUpdateRecursive( vaTracerView & view )
            {
                LastSeenAge++;
                double childrenTimeTotalAvgPerFrame = 0.0;
                for( int i = (int)ChildNodes.size()-1; i >= 0 ; i-- )
                {
                    ChildNodes[i]->PostUpdateRecursive( view );
                    childrenTimeTotalAvgPerFrame += ChildNodes[i]->TimeTotalAvgPerFrame;

                    if( ChildNodes[i]->LastSeenAge > Node::LastSeenAgeToKeepAlive )
                    {
                        ChildNodes[i]->ReleaseRecursive( view );
                        ChildNodes[i] = nullptr;
                        if( ChildNodes.size( ) - 1 > i )
                            ChildNodes[i] = ChildNodes[ChildNodes.size( ) - 1];
                        ChildNodes.pop_back( );
                    }

                }

                TimeTotalAvgPerInst     = TimeTotal / (double)Instances;
                TimeTotalAvgPerFrame    = TimeTotal / (double)view.m_frameCountWhileConnected;
                TimeSelfAvgPerFrame     = TimeTotalAvgPerFrame - childrenTimeTotalAvgPerFrame;

                std::sort( ChildNodes.begin( ), ChildNodes.end( ), [ ]( const Node* a, const Node* b ) { return a->SortOrder < b->SortOrder; } ); // { return a->Name < b->Name; } );
            }

            // there's probably a reason the above is const only
            //Node * FindRecursive( const string & name )
            //{
            //    if( name == this->Name )
            //        return this;
            //    else
            //    {
            //        for( int i = 0; i < ChildNodes.size(); i++ )
            //        {
            //            Node * retVal = ChildNodes[i]->FindRecursive( name );
            //            if( retVal != nullptr )
            //                return retVal;
            //        }
            //    }
            //    return nullptr;
            //}

        };
    private:
        string                  m_connectionName;
        bool                    m_connectionIsGPU = false;
        weak_ptr<vaTracer::ThreadContext>
                                m_connectedThreadContext;
        mutable std::shared_mutex m_viewMutex;

        bool                    m_nameChanged       = true;

        std::vector<Node*>      m_rootNodes;

        std::vector<vaTracer::Entry> CapturedTimeline;

        std::vector<Node*>      UnusedNodePool;

        int                     m_frameCountWhileConnected;
        int                     m_frameSortCounter;
        double                  m_lastConnectedTime;
        double                  m_connectionTimeoutTime;

//        vaGUID                  m_lastConnectionGUID    = vaCore::GUIDNull();

        void                    Reset( )                                            
        { 
            std::lock_guard lock(m_viewMutex);
            m_connectedThreadContext.reset();
            for( int i = 0; i < m_rootNodes.size(); i++ ) 
                m_rootNodes[i]->ReleaseRecursive( *this ); 
            m_rootNodes.clear(); 
            m_connectionName = ""; 
            m_nameChanged               = true;
            /*Beginning = End = std::chrono::time_point<std::chrono::steady_clock>();*/ 
            for( int i = 0; i < UnusedNodePool.size( ); i++ )
                delete UnusedNodePool[i];
            UnusedNodePool.clear();
            m_frameCountWhileConnected  = 0;
            m_frameSortCounter          = 0;
            m_lastConnectedTime         = 0;
            m_connectionTimeoutTime     = 0;
        }

    protected:
        friend Node;
        friend vaTracer::ThreadContext;
        Node *              AllocateNode( )                                         
        { 
            if( UnusedNodePool.size() == 0 ) 
                return new Node; 
            Node * ret = UnusedNodePool.back(); 
            UnusedNodePool.pop_back(); 
            return ret; 
        }
        void                ReleaseNode( Node * node )                              
        { 
            assert( node->ChildNodes.size() == 0 ); 
            if( UnusedNodePool.size() < 10000 )
                UnusedNodePool.push_back( node );
            else
                delete node;
        }
        void                PreUpdateRecursive( );
        void                PostUpdateRecursive( );
        void                UpdateCallback( vaTracer::Entry * timelineChunk, int timelineChunkCount, bool incrementFrameCounter = false );

        friend class vaTracer;
        void                TickFrame( float deltaTime );

    public:
        vaTracerView( )     { Reset(); }
        ~vaTracerView( )    { Reset(); }

        const string &      GetConnectionName( ) const      { return m_connectionName; }
        const bool          GetConnectionIsGPU( ) const     { return m_connectionIsGPU; }
//        const vaGUID &      GetLastConnectionGUID( ) const { return m_lastConnectionGUID; }

        void                ConnectToThreadContext( const string & name, float connectionTimeout );       // basic wildcards supported so "!!GPU*" will work with the first GPU context
        void                Disconnect( shared_ptr<vaTracerView> prevUIView = nullptr );
        bool                IsConnected( ) const                            { assert( vaThreading::IsMainThread( ) ); std::lock_guard lock( m_viewMutex ); return !m_connectedThreadContext.expired(); }

        void                TickImGuiRecursive( Node * node, bool forceSelect );
        void                TickImGui( vaApplicationBase & application );

        // don't hold this pointer or use it from non-main thread - none of these are supported!
        const Node *        FindNodeRecursive( const string & name ) const;
    };

#ifdef _DEBUG
    inline void vaTracer::ThreadContext::OnEnd( vaMappedString verifyName )
#else
    inline void vaTracer::ThreadContext::OnEnd( )
#endif
    {
        double now = vaCore::TimeFromAppStart( );
        assert( CurrentOpenStack.size( ) > 0 );
        if( CurrentOpenStack.size( ) == 0 )
            return;

#ifdef _DEBUG
        // if this triggers, you have overlapping scopes - shouldn't happen but it did so fix it please :)
        assert( verifyName == LocalTimeline[CurrentOpenStack.back( )].Name );
#endif
        LocalTimeline[CurrentOpenStack.back( )].End = now;
        CurrentOpenStack.pop_back( );

        const int itemsBeforeDefrag = 16;
        if( CurrentOpenStack.size( ) == 0 && ( LocalTimeline.size( ) >= itemsBeforeDefrag /*|| NextDefragTime > now*/ ) )
        {
            std::lock_guard lock( TimelineMutex );

            // if there's a viewer attached
            auto attachedViewer = AttachedViewer.lock( );
            if( attachedViewer != nullptr ) // if attached viewer callback returns false, we're no longer connected
                attachedViewer->UpdateCallback( LocalTimeline.data(), (int)LocalTimeline.size() );

#if 0
            Timeline.AppendMove( std::move(LocalTimeline) );
#else
            Timeline.Append( LocalTimeline );
            LocalTimeline.clear();
#endif

            // const int arrsize = (int)LocalTimeline.size( );
            // for( int i = 0; i < arrsize; i++ )
            //     Timeline.emplace_back( std::move(LocalTimeline[i]) );
            // 
            // // done with this, clear!
            // LocalTimeline.clear( );

            // remove older (do it here because this way there is no global locking and the cost is spread across all threads and is proportional to use)
            //if( NextDefragTime < now )
            //{
            //    NextDefragTime = now + 0.0333f;   // ~30 times per second sounds reasonable?
                auto oldest = now - c_maxCaptureDuration;
            //    // while( Timeline.size( ) > 0 && Timeline.begin( )->Beginning < oldest )
            //    //     Timeline.pop_front( );
            //
                Timeline.Defrag( oldest );
            //}
        }
    }

    inline void vaTracer::ThreadContext::BatchAddFrame( Entry* entries, int count )
    {
        assert( CurrentOpenStack.size( ) == 0 );
        if( CurrentOpenStack.size( ) != 0 )
            return;

        double now = vaCore::TimeFromAppStart( );

        std::lock_guard lock( TimelineMutex );

        // if there's a viewer attached
        {
            auto attachedViewer = AttachedViewer.lock( );
            if( attachedViewer != nullptr ) // if attached viewer callback returns false, we're no longer connected
            {
                assert( AutomaticFrameIncrement == false );
                attachedViewer->UpdateCallback( entries, count, true );
            }
        }

        Timeline.Append( entries, count );

        // cleanup always
        {
            auto oldest = now - c_maxCaptureDuration;

            Timeline.Defrag( oldest );
        }
    }

#ifdef VA_SCOPE_TRACE_ENABLED



    struct vaScopeTraceStaticPart
    {
        vaMappedString const                MappedName;

        alignas( VA_ALIGN_PAD )  char       m_fptPadding1[VA_ALIGN_PAD];
        atomic_int32                        LoopID = 0;
        alignas( VA_ALIGN_PAD )  char       m_fptPadding2[VA_ALIGN_PAD];
 
        vaScopeTraceStaticPart( const char * name, bool selectInUI ) : MappedName( vaCore::MapString( name ) ) { if( selectInUI ) vaTracer::SelectNodeInUI( name ); }
    };

    struct vaScopeTrace
    {
        vaRenderDeviceContext * const       m_renderDeviceContext   = nullptr;
        int                                 m_GPUTraceHandle        = -1;

#ifdef _DEBUG
        vaMappedString const                m_name;
#endif

        vaScopeTrace( const char * customName )
#ifdef _DEBUG
            : m_name( vaCore::MapString( customName ) )
#endif
        {
            vaMappedString mappedName = vaCore::MapString( customName );
#ifdef _DEBUG
            assert( mappedName == m_name );
#endif
            vaTracer::LocalThreadContext( )->OnBegin( mappedName, 0 );
        }

        vaScopeTrace( const string & customName )
#ifdef _DEBUG
            : m_name( vaCore::MapString( customName.c_str() ) )
#endif
        {
            vaMappedString mappedName = vaCore::MapString( customName.c_str() );
#ifdef _DEBUG
            assert( mappedName == m_name );
#endif
            vaTracer::LocalThreadContext( )->OnBegin( mappedName, 0 );
        }

        vaScopeTrace( vaScopeTraceStaticPart & info ) 
#ifdef _DEBUG
            : m_name( info.MappedName )
#endif
        { 
            int subID = info.LoopID++; //.fetch_add( 1 );
            vaTracer::LocalThreadContext( )->OnBegin( info.MappedName, subID ); 
#ifdef VA_USE_PIX3_HIGH_FREQUENCY_CPU_TIMERS
            BeginExternalCPUTrace( info.MappedName, subID );
#endif
        }
        vaScopeTrace( vaScopeTraceStaticPart & info, vaRenderDeviceContext * renderDeviceContext ) 
            : 
#ifdef _DEBUG
            m_name( info.MappedName ), 
#endif
            m_renderDeviceContext(renderDeviceContext)  
        { 
            int subID = info.LoopID++; //.fetch_add( 1 );
            vaTracer::LocalThreadContext( )->OnBegin( info.MappedName, subID ); 
            BeginGPUTrace( info.MappedName, subID );
        }
        ~vaScopeTrace( )                    
        { 
            if(m_renderDeviceContext != nullptr) 
                EndGPUTrace(); 
#ifdef VA_USE_PIX3_HIGH_FREQUENCY_CPU_TIMERS
            else
                EndExternalCPUTrace();
#endif
#ifdef _DEBUG
            vaTracer::LocalThreadContext( )->OnEnd( m_name ); 
#else
            vaTracer::LocalThreadContext( )->OnEnd( ); 
#endif
        }

    private:
        void                                BeginGPUTrace( vaMappedString name, int subID );
        void                                EndGPUTrace();
        void                                BeginExternalCPUTrace( vaMappedString name, int subID );
        void                                EndExternalCPUTrace();
    };
    #define VA_TRACE_CPU_SCOPE( name )                                          thread_local static vaScopeTraceStaticPart scopestatic_##name( #name, false ); vaScopeTrace scope_##name( scopestatic_##name );
    #define VA_TRACE_CPU_SCOPE_CUSTOMNAME( nameVar, customName )                vaScopeTrace scope_##nameVar( customName );
    #define VA_TRACE_CPUGPU_SCOPE( name, apiContext )                           thread_local static vaScopeTraceStaticPart scopestatic_##name( #name, false ); vaScopeTrace scope_##name( scopestatic_##name, &apiContext );
    #define VA_TRACE_CPUGPU_SCOPE_SELECT_BY_DEFAULT( name, apiContext )         thread_local static vaScopeTraceStaticPart scopestatic_##name( #name, true ); vaScopeTrace scope_##name( scopestatic_##name, &apiContext );
#else
    #define VA_TRACE_CPU_SCOPE( name )                             
    #define VA_TRACE_CPU_SCOPE_CUSTOMNAME( nameVar, customName )   
    #define VA_TRACE_CPUGPU_SCOPE( name, apiContext )              
    #define VA_TRACE_CPUGPU_SCOPE_CUSTOMNAME( nameVar, customName )
    #define VA_TRACE_CPUGPU_SCOPE_SELECT_BY_DEFAULT( name, apiContext )
#endif

}
