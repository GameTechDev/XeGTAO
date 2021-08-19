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

#include "vaSceneAsync.h"

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
#include "IntegratedExternals/vaTaskflowIntegration.h"
//#define VA_SCENE_USE_TASKFLOW
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

    protected:
        static int                                  s_instanceCount;
        // vaGUID                                      m_UID;
        // string                                      m_name;

        entt::registry                              m_registry;

        // last storage path, could be "" if SaveJSON/LoadJSON never called!
        string                                      m_storagePath;

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
        friend struct TransformsUpdateWorkNode;
        friend struct DirtyBoundsUpdateWorkNode;
        //
        Scene::UniqueStaticAppendConsumeList        m_listDirtyBounds;
        Scene::UniqueStaticAppendConsumeList        m_listDirtyTransforms;
        vaAppendConsumeList<entt::entity>           m_listDirtyBoundsUpdatesFailed;
        Scene::UniqueStaticAppendConsumeList        m_listDestroyEntities;
        // specialized for traversing dirty transform hierarchy - 32k total for 16 levels when unused, grows with used storage (no shrink to fit yet!)
        vaAppendConsumeList<entt::entity>           m_listHierarchyDirtyTransforms[Scene::Relationship::c_MaxDepthLevels];
        // 
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        float                                       m_currentTickDeltaTime              = -1.0f;
        int64                                       m_lastApplicationTickIndex          = -1;
        double                                      m_time                              = 0;                                // gets incremented on every TickBegin by deltaTime
        //////////////////////////////////////////////////////////////////////////
    
        shared_ptr<void> const                      m_aliveToken                        = std::make_shared<int>( 42 );    // this is only used to track object lifetime for callbacks and etc.

        vaSceneAsync                                m_async;
        std::vector<shared_ptr<vaSceneAsync::WorkNode>> 
                                                    m_asyncWorkNodes;

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
        vaScene( const string & name = "UnnamedScene", const vaGUID UID = vaGUID::Create() );
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

        // these are thread-safe lists used only for async processing
        auto &                                      ListDirtyTransforms( )                              { return m_listDirtyTransforms; }
        auto &                                      ListDirtyBounds( )                                  { return m_listDirtyBounds;     }
        auto &                                      ListDestroyEntities( )                              { return m_listDestroyEntities; }

        vaSceneAsync &                              Async( )                                            { return m_async; }

    public:
        // This is the main async execution part - wraps Async( ), might get removed for direct access instead
        void                                        TickBegin( float deltaTime, int64 applicationTickIndex );
        void                                        TickEnd( );
        bool                                        IsTicking( ) const                                  { return m_currentTickDeltaTime != -1.0f; }
        //
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
}
