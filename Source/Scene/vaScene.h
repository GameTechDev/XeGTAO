///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Notes for future refactoring 
// * add asserts for using scene (even access like m_currentScene->Registry( ) ) between TickBegin and TickEnd

#pragma once

#include "Core/vaCoreIncludes.h"

#include "Core/vaUI.h"

#include "IntegratedExternals/vaEnTTIntegration.h"

#include "vaSceneTypes.h"
#include "vaSceneSystems.h"
#include "vaSceneComponents.h"

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
#include "IntegratedExternals/vaTaskflowIntegration.h"
#define VA_SCENE_USE_TASKFLOW
#endif


namespace Vanilla
{
    // Few notes:
    // * I'm basing my new scene system around EnTT because I have neither the time or knowledge to write something
    //   like this from scratch. Previous scene system was a custom scenegraph but at one point I realized that I'm
    //   just reinventing the wheel.
    // * The other candidate was https://github.com/SanderMertens/flecs - I went down the EnTT route only because
    //   it was header-only, so I tried it out first due to convenience and stuck with it. But 'flecs' seems
    //   equally excellent.
    // * Tick will start various parallel tasks that can continue even to next frame. To manually manipulate 
    //   the scene, a call to WaitSystemTasks needs to be done, preferably as late as possible from Tick.
    //   (Doing it just before Tick is fine - will stop any tasks from previous frame). You can chose to
    //   either synchronize coarsely with WaitSystemTasks before doing custom scene manipulation, or wait on
    //   system-specific futures (not really worked out in detail yet).
    //
    //
    // Ideas:
    // * for Prefabs - a scene exported as a prefab no global parts, only the registry, goes into Media/Prefabs
    //
    // Some questions / things to find out
    //
    // * how to use type to create a component class but retain functionality of original built-in type or even a custom type (for ex, vaMatrix4x4 - assignment operators are borked) 
    // * 'globals' have inconsistent API compared to components (for ex. there's try_ctx but there's no has_ctx)
    // * (to confirm and report as bug): getting const .ctx global is different from non-const?


    namespace Scene
    {
        class EntityPropertiesPanel;
    }

    class vaScene : public vaUIPanel, public std::enable_shared_from_this<vaScene>, public vaRuntimeID<vaScene>
    {
    public:
        // For additional name info and (optional) additional access to task scheduling / custom task spawning.
        struct ConcurrencyContext
        {
#ifdef VA_SCENE_USE_TASKFLOW
            tf::Subflow *               Subflow;
#endif
        };

    private:
        struct DragDropNodeData
        {
            vaGUID                      SceneUID;
            entt::entity                Entity;
            static const char*      PayloadTypeName( ) { return "DND_SCENE_NODE"; };
            bool operator == ( const DragDropNodeData & other ) { return this->SceneUID == other.SceneUID && this->Entity == other.Entity; }
        };

        struct WorkItem
        {
            // entt components to lock, either for read-write (unique lock) or read-only (shared lock)
            std::vector<int>            ReadWriteComponents;
            std::vector<int>            ReadComponents;

            string                      Name;
            string                      NameNarrowBefore;
            string                      NameWide;
            string                      NameWideBlock;
            string                      NameNarrowAfter;
            string                      NameFinalizer;

            // This is the first callback; it executes on a worker thread and returns the pair of int-s where .first is the total number of items to be 
            // processed in the next (Wide) step and .second defined the 'chunk size' (number of items processed per one Wide call) - use 1 to process
            // single item per Wide call.
            std::function<std::pair<uint32, uint32>( ConcurrencyContext & )>
                                        NarrowBefore;
            // This is the second (optional) callback that gets called multiple times from worker threads (for_each) to process all items, with the
            // total item count defined by .first returned from NarrowBefore, and the max number of items performed by each Wide defined by the .second.
            std::function<void ( int begin, int end, ConcurrencyContext & )>
                                        Wide;
            // This is the third (optional) callback that gets called after all Wide items have been processed, from the worker thread.
            std::function<void ( ConcurrencyContext & )>     
                                        NarrowAfter;
            // This is the fourth (optional) callback that gets called from the main thread, after all concurrent work has finished.
            std::function<void( )>      Finalizer;
            
            // Stage indices, corresponding to vaScene::m_stages
            int                         StageStart;
            int                         StageStop;
        };

    protected:
        static int                                  s_instanceCount;
        // vaGUID                                      m_UID;
        // string                                      m_name;

        entt::registry                              m_registry;

        // last storage path, could be "" if SaveJSON/LoadJSON never called!
        string                                      m_storagePath;

        // Scene::Staging                              m_staging;

        // the only purpose of these for now is learning exactly how these callbacks work - these will likely be removed
        // vector<entt::entity>                        m_toDestroyList;

        // vector<string>                              m_assetPackNames;            // names of the below used asset packs
        // vector<shared_ptr<vaAssetPack>>             m_assetPacks;                // currently used asset packs

        // also used as a skybox :)
        Scene::IBLProbe                             m_IBLProbeDistant;
        // will be removed in the future
        Scene::IBLProbe                             m_IBLProbeLocal;

        //////////////////////////////////////////////////////////////////////////
        // UI - Globals
        shared_ptr<void>                            m_IBLProbeDistantUIContext;
        // UI - Entities
        string                                      m_uiEntitiesFilter;
        bool                                        m_uiEntitiesFilterByName            = true;
        bool                                        m_uiEntitiesFilterByComponent       = false;
        float                                       m_uiEntitiesFilterCheckboxSize      = 40.0f;
        bool                                        m_uiEntityTreeRootOpen              = true;
        bool                                        m_uiEntityTreeUnRootOpen            = false;
        // int                                         m_uiNumHierarchicalUnfiltered       = 0;
        // int                                         m_uiNumNonHierarchicalUnfiltered    = 0;
        entt::entity                                m_uiEntityContextMenuEntity         = entt::null;
        int                                         m_uiEntityContextMenuDepth          = 0;

        entt::entity                                m_uiHighlight                       = entt::null;
        float                                       m_uiHighlightRemainingTime          = 0.0f;

        entt::entity                                m_uiPopupRename                     = entt::null;


        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // reactive systems
        // entt::observer                              m_transformWorldObserver;       // this is effectively a list of 'moved' objects - I have no idea why I need this for the moment, but I wanted to test the observer pattern
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // 
        Scene::UniqueStaticAppendConsumeList        m_listDirtyTransforms;
        Scene::UniqueStaticAppendConsumeList        m_listDirtyBounds;
        vaAppendConsumeList<entt::entity>           m_listDirtyBoundsUpdatesFailed;
        Scene::UniqueStaticAppendConsumeList        m_listDestroyEntities;
        //
        // specialized for traversing dirty transform hierarchy - 32k total for 16 levels when unused, grows with used storage (no shrink to fit yet!)
        vaAppendConsumeList<entt::entity>           m_listHierarchyDirtyTransforms[Scene::Relationship::c_MaxDepthLevels];
        // 
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        static constexpr int                        c_maxStageCount                     = 16;
        std::vector<string>                         m_stages;
        std::vector<WorkItem>                       m_workItemStorage;                  // contains both active and inactive - use m_workItemCount
        int                                         m_workItemCount                     = 0;
        bool                                        m_canAddWork                        = false;
        //////////////////////////////////////////////////////////////////////////
#ifdef VA_SCENE_USE_TASKFLOW
        tf::Taskflow                                m_tf;
        std::future<void>                           m_stageDriver;
#endif
        std::mutex                                  m_stagingMutex;
        std::condition_variable                     m_stagingCV;
        int                                         m_finishedStageIndex                = -1;
        //////////////////////////////////////////////////////////////////////////
        float                                       m_currentTickDeltaTime              = -1.0f;
        int64                                       m_lastApplicationTickIndex          = -1;
        double                                      m_time                              = 0;                                // gets incremented on every TickBegin by deltaTime
        //////////////////////////////////////////////////////////////////////////
    
        shared_ptr<void> const                      m_aliveToken                        = std::make_shared<int>( 42 );    // this is only used to track object lifetime for callbacks and etc.

    public:
        //////////////////////////////////////////////////////////////////////////
        // Signals
        // 
        // Systems are supposed to only add work through e_TickBegin, which is invoked during TickBegin()
        vaEvent<void( vaScene & scene, float deltaTime, int64 applicationTickIndex )>
                                                    e_TickBegin;
        // Invoked during TickEnd - all background processing finished by now.
        vaEvent<void( vaScene & scene, float deltaTime, int64 applicationTickIndex )>
                                                    e_TickEnd;
        //////////////////////////////////////////////////////////////////////////
    public:
        vaScene( const string & name = "Unnamed scene", const vaGUID UID = vaGUID::Create() );
        virtual ~vaScene( );

    public:
        const vaGUID &                              UID( ) const                                        { return m_registry.ctx<Scene::UID>(); }
        const string &                              Name( ) const                                       { return m_registry.ctx<Scene::Name>(); }

        entt::registry &                            Registry( )                                         { return m_registry; }
        const entt::registry &                      Registry( ) const                                   { return m_registry; }
        // const Scene::Staging &                      Staging( )                                          { return m_staging; }

        entt::entity                                CreateEntity( const string & name, const vaMatrix4x4 & localTransform = vaMatrix4x4::Identity, entt::entity parent = entt::null, const vaGUID & renderMeshID = vaGUID::Null, const vaGUID & renderMaterialID = vaGUID::Null );
        
        // This does not actually destroy the entity but puts a destroy tag (DestroyTag) to it; actual destruction is deferred to when vaScene::DestroyTagged is called
        // If Scene::Relationship exists and 'recursive' is ture, it will also tag children for destruction recursively
        void                                        DestroyEntity( entt::entity entity, bool recursive );

        // removes all entities
        void                                        ClearAll( );

        bool                                        Serialize( class vaXMLSerializer & serializer );

        bool                                        SaveJSON( const string & filePath );
        bool                                        LoadJSON( const string & filePath );
        string                                      LastJSONFilePath( )                                 { return m_storagePath; }

        int64                                       GetLastApplicationTickIndex( ) const                { return m_lastApplicationTickIndex; }
        double                                      GetTime( ) const                                    { return m_time; }


        auto &                                      ListDirtyTransforms( )                              { return m_listDirtyTransforms; }
        auto &                                      ListDirtyBounds( )                                  { return m_listDirtyBounds;     }
        auto &                                      ListDestroyEntities( )                              { return m_listDestroyEntities; }

    public:
        //////////////////////////////////////////////////////////////////////////
        // "Systems" part
        //
        // This is the main async execution part
        void                                        TickBegin( float deltaTime, int64 applicationTickIndex );
        void                                        TickWait( const string & stageName );
        void                                        TickEnd( );
        bool                                        IsTicking( ) const                                  { return m_currentTickDeltaTime != -1.0f; }
        //
        //////////////////////////////////////////////////////////////////////////
        // 
        // This is a convenient way to safely run multithreaded workloads on the scene.
        //  - stageStart:   start work when this stage starts
        //  - stageStop:    stop work when this stage stops (or -1 to stop in the same it started)
        // Example: if stageStart == "render_selections" and stageStop == "render_selections" then it will start with all other work assigned to "selections", 
        // execute in parallel with them, and finish before the next stage.
        //  - narrowBefore, wide and narrowAfter are callbacks that do the work in parallel; see below for details
        template< 
            // AccessedComponents is the lock list (const for read-only components, non-const for read-write)
            typename... AccessedComponents,
            //
            // This is the first callback; it executes on a worker thread and returns the pair of int-s where .first is the total number of items to be processed in the 
            // next (Wide) step and .second defined the 'chunk size' (number of items processed per one Wide call) - use 1 to process single item per Wide call.
            typename _NarrowBefore,     // callback type: std::pair<uint32, uint32>( ConcurrencyContext & )
            //
            // This is the second (optional) callback that gets called multiple times from worker threads (for_each) to process all items, with the
            // total item count defined by .first returned from NarrowBefore, and the max number of items performed by each Wide defined by the .second.
            typename _Wide,             // callback type: void ( int begin, int end, ConcurrencyContext & )
            //
            // This is the third (optional) callback that gets called after all Wide items have been processed, from the worker thread.
            typename _NarrowAfter,      // callback type: void ( ConcurrencyContext & )
            //
            // This is the fourth (optional) callback that gets called from the main thread from vaScene::TickEnd, after ALL concurrent work has finished.
            typename _Finalizer         // callback type: void ( void )
        >
        bool                                        AddWork( const string & workName, const string & stageStart, const string & stageStop, 
                                                        _NarrowBefore && narrowBefore, _Wide && wide, _NarrowAfter && narrowAfter, _Finalizer && finalizer );

        // Generic way to convert tag components to more thread-safe AppendConsume traversal lists
        template< typename TagComponent, typename ListType > 
        bool                                        AddWork_ComponentToList( const string & workName, ListType & list );

    public:
        // Generic helpers
        const string &                              GetName( entt::entity entity )                      { return Scene::GetName( m_registry, entity ); }
        void                                        UIOpenProperties( entt::entity entity, int preferredPropPanel = -1 );
        //void                                        UICloseProperties( entt::entity entity );
        void                                        UIHighlight( entt::entity entity );
        void                                        UIOpenRename( entt::entity entity )                 { if( m_uiPopupRename != entt::null || !m_registry.valid(entity) ) { assert(false); return; }; m_uiPopupRename = entity; }

    public:
        // Transforms
        // const vaMatrix4x4 *                         GetTransformWorld( )
        void                                        SetTransformDirtyRecursive( entt::entity entity );

    public:
        // Parent/child relationships
        void                                        SetParent( entt::entity child, entt::entity parent );   // parent can be null - this then just breaks existing parent<->child link
        void                                        UnparentChildren( entt::entity parent );                // breaks all children links from this parent
        void                                        VisitChildren( entt::entity parent, std::function<void( entt::entity child )> visitor )                                                     { return Scene::VisitChildren( m_registry, parent, visitor ); }
        void                                        VisitChildren( entt::entity parent, std::function<void( entt::entity child, int index, entt::entity parent )> visitor )                     { return Scene::VisitChildren( m_registry, parent, visitor ); }

    protected:
        // Callbacks
        // void                                        OnTransformLocalChanged( entt::registry &, entt::entity ); <- handled by the short circuit instead, see constructor
        void                                        OnRelationshipDestroy( entt::registry &, entt::entity );
        void                                        OnDisallowedOperation( entt::registry &, entt::entity );
        void                                        OnRelationshipEmplace( entt::registry &, entt::entity );
        void                                        OnTransformDirtyFlagEmplace( entt::registry & registry, entt::entity );

        void                                        InternalOnTickBegin( vaScene & scene, float deltaTime, int64 tickCounter );
        void                                        InternalOnTickEnd( vaScene & scene, float deltaTime, int64 tickCounter );
#ifdef VA_SCENE_USE_TASKFLOW
        void                                        StageDriver( tf::Subflow & subflow );
#else
        void                                        StageDriver( );
#endif

    protected:
        virtual string                              UIPanelGetDisplayName( ) const override { return Name(); }
        virtual void                                UIPanelTick( vaApplicationBase& application ) override;
        virtual void                                UIPanelTickAlways( vaApplicationBase& application ) override;

    protected:
        // the only purpose of these for now is learning exactly how these callbacks work - these will likely be removed
        void                                        OnDestroyTagged(entt::registry &, entt::entity);
        void                                        OnDestroyUntagged(entt::registry &, entt::entity);
        void                                        OnDestroyTagChanged(entt::registry &, entt::entity);

    public:
        static vaScene *                            FindByRuntimeID( uint64 runtimeID );
    };

    // inline void vaScene::OnTransformLocalChanged( entt::registry & registry, entt::entity entity )
    // {
    //     assert( &registry == &m_registry ); 
    //     m_registry.emplace_or_replace<Scene::TransformDirtyTag>( entity );
    // }

    inline void vaScene::OnDisallowedOperation( entt::registry & registry, entt::entity )
    {
        assert( &registry == &m_registry ); registry;
        assert( false );    // you're doing something that's not allowed
    }

    inline void vaScene::OnRelationshipEmplace( entt::registry & registry, entt::entity )
    {
        assert( &registry == &m_registry ); registry;
        // TODO: handle this with Component states
        // assert( m_canEmplaceRelationship );    // you're doing something that's not allowed
    }

    inline void vaScene::OnTransformDirtyFlagEmplace( entt::registry& registry, entt::entity )
    {
        assert( &registry == &m_registry ); registry;
        // TODO: handle this with Component states
        // assert( m_canEmplaceTransformDirtyFlag );    // you're doing something that's not allowed
    }

    template< typename... AccessedComponents, typename _NarrowBefore, typename _Wide, typename _NarrowAfter, typename _Finalizer >
    inline bool vaScene::AddWork( const string & workName, const string & stageStart, const string & stageStop, _NarrowBefore && narrowBefore, _Wide && wide, _NarrowAfter && narrowAfter, _Finalizer && finalizer )
    {
        assert( vaThreading::IsMainThread( ) && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );
        if( !m_canAddWork )
        {
            assert( false ); return false;
        }

        auto findStr = []( const std::vector<string> & vec, const string & name ) -> int
        {
            for( int i = 0; i < (int)vec.size( ); i++ )
                if( vaStringTools::CompareNoCase( vec[i], name ) == 0 )
                    return i;
            return -1;
        };

        int stageStartIndex = findStr( m_stages, stageStart );
        int stageStopIndex = findStr( m_stages, stageStop );
        if( stageStopIndex == -1 )
            stageStopIndex = (int)m_stages.size()-1;

        // must know where to start
        if( stageStartIndex == -1 || stageStopIndex < stageStartIndex )
            { assert( false ); return false; }

        // enlarge storage if needed
        if( m_workItemCount >= m_workItemStorage.size( ) )
            m_workItemStorage.push_back( {} );

        WorkItem & item = m_workItemStorage[m_workItemCount];
        m_workItemCount++;

        item.ReadWriteComponents.clear(); item.ReadComponents.clear();
        Scene::AccessPermissions::Export< AccessedComponents... >( item.ReadWriteComponents, item.ReadComponents );
        vaAssertSits( narrowBefore );       // lambda captures too large to fit into std::function with no dynamic allocations?
        vaAssertSits( wide );               // lambda captures too large to fit into std::function with no dynamic allocations?
        vaAssertSits( narrowAfter );        // lambda captures too large to fit into std::function with no dynamic allocations?
        vaAssertSits( finalizer );          // lambda captures too large to fit into std::function with no dynamic allocations?
        item.Name               = workName;
        item.NameNarrowBefore   = workName + "_NB";
        item.NameWide           = workName + "_W";
        item.NameWideBlock      = workName + "_WB";
        item.NameNarrowAfter    = workName + "_NA";
        item.NameFinalizer      = workName + "_FIN";
        item.NarrowBefore       = narrowBefore;
        item.Wide               = wide;
        item.NarrowAfter        = narrowAfter;
        item.Finalizer          = finalizer;
        item.StageStart         = stageStartIndex;
        item.StageStop          = stageStopIndex;
        assert( item.NarrowBefore );  // must have at least narrowBefore callback

        return true;
    }

    template< typename TagComponentType, typename ListType >
    inline bool vaScene::AddWork_ComponentToList( const string & workName, ListType & dstList )
    {
        struct LocalContext
        {
            entt::registry& Registry;
            ListType& DstList;
            entt::basic_view< entt::entity, entt::exclude_t<>, const TagComponentType>     View = Registry.view<std::add_const_t<TagComponentType>>( );
            //bool const              ClearComponent;
            LocalContext( entt::registry& registry, ListType& dstList ) : Registry( registry ), DstList( dstList ) { }
        };

        // TODO: use some kind of object pool here
        LocalContext* localContext = new LocalContext( m_registry, dstList );

        auto narrowBefore   = [localContext]( ConcurrencyContext & ) noexcept
        { 
            localContext->DstList.StartAppending( (uint32)localContext->Registry.size( ) );
            return std::make_pair( (uint32)localContext->View.size(), vaTF::c_chunkBaseSize * 8 ); 
        };

        auto wide           = [localContext]( int begin, int end, ConcurrencyContext & ) noexcept
        {
            for( int index = begin; index < end; index++ )
            {
                entt::entity entity = localContext->View[index];
                assert( localContext->Registry.valid( entity ) );   // if this fires, you've corrupted the data somehow - possibly destroying elements outside of DestroyTag path?
                localContext->DstList.Append( entity );
            }
        };

        auto narrowAfter    = [localContext]( ConcurrencyContext & ) noexcept
        {
            // localContext->DstList.StartConsuming( ); <- don't switch to consuming because others might want to keep appending later!
            {
                VA_TRACE_CPU_SCOPE( ClearEnTTTag );
                localContext->Registry.clear<TagComponentType>( );
            }
            delete localContext;
        };

        return AddWork<TagComponentType>( workName, "dirtylists", "dirtylists", std::move(narrowBefore), std::move(wide), std::move(narrowAfter), nullptr );
    }
}
