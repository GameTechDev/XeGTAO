///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File changes (yyyy-mm-dd)
// 2016-09-07: filip.strugar@intel.com: first commit (extracted from Vanilla codebase, 2006-2016 by Filip Strugar)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#if 0

#include "Core/vaCoreIncludes.h"
#include "Core/vaXMLSerialization.h"

#include "Rendering/vaRendering.h"
#include "Rendering/vaSceneLighting.h"

#include "Scene/vaScene.h"

namespace Vanilla
{
    class vaSceneOld;
    class vaAssetPack;
    class vaDebugCanvas2D;
    class vaDebugCanvas3D;
    class vaRenderMesh;
    class vaRenderMaterial;
    class vaSceneObject;

    typedef std::function< bool( const vaSceneObject & obj, const vaMatrix4x4 & worldTransform, const vaOrientedBoundingBox & obb, const vaRenderMesh & mesh, const vaRenderMaterial & material, int & outBaseShadingRate, vaVector4 & outEmissiveAdd ) >   SelectionFilterCallback;

    class vaSceneObject : public std::enable_shared_from_this<vaSceneObject>, public vaXMLSerializable, public vaUIPropertiesItem//, public vaUIDObject
    {
    protected:
        string                                      m_name                                  = "Unnamed";

        weak_ptr<vaSceneOld>                           m_scene;

        vaMatrix4x4                                 m_localTransform                        = vaMatrix4x4::Identity;

        shared_ptr<vaSceneObject>                   m_parent;
        std::vector<shared_ptr<vaSceneObject>>           m_children;

        std::vector<vaGUID>                              m_renderMeshes;

        bool                                        m_createdButNotYetAddedToScene          = true;
        bool                                        m_destroyedButNotYetRemovedFromScene    = false;

        // derived/computed variable cache
        mutable int64                               m_lastSceneTickIndex                    = -1;                           // Updated each frame in TickRecursive - this way you can know if an object was already updated
        mutable vaMatrix4x4                         m_computedWorldTransform                = vaMatrix4x4::Identity;        // Updated each frame in TickRecursive
        mutable vaBoundingBox                       m_computedLocalBoundingBox              = vaBoundingBox::Degenerate;    // Updated in UpdateLocalBoundingBox from RenderMesh-es and other stuff
        mutable bool                                m_computedLocalBoundingBoxIncomplete    = true;
        mutable vaBoundingBox                       m_computedGlobalBoundingBox             = vaBoundingBox::Degenerate;    // Updated each frame in TickRecursive

        //mutable std::vector<weak_ptr<vaRenderMesh>>      m_cachedRenderMeshes;
    
    public:
        vaSceneObject( );
        virtual ~vaSceneObject( );

    private:
        friend class vaSceneOld;
        // only to be called from the scene itself (creation/deletion functions)
        void                                        SetScene( const shared_ptr<vaSceneOld> & scene )               { m_scene = scene; }
        void                                        SetAddedToScene( )                                          { assert( m_createdButNotYetAddedToScene ); m_createdButNotYetAddedToScene = false; }

        // never use except when deleting the whole subtree or scene!
        void                                        NukeGraph( )                                                { m_parent = nullptr; m_children.clear(); SetScene( nullptr ); }

        void                                        MarkAsDestroyed( bool markChildrenAsWell );
        void                                        RegisterChildAdded( const shared_ptr<vaSceneObject> & child );
        void                                        RegisterChildRemoved( const shared_ptr<vaSceneObject> & child );
        void                                        RegisterUsedAssetPacks( std::function<void( const vaAssetPack & )> registerFunction );

        // can't be changed except at creation/destruction time (although it could be added as additional 'ObjectModifier' action)
        void                                        SetParent( const shared_ptr<vaSceneObject> & parent );

        void                                        ToNewRecursive( vaScene & scene, vaSceneObject * parentObj = nullptr, entt::entity parentEntity = entt::null );

    public:
        shared_ptr<vaSceneOld>                         GetScene( ) const                                           { return m_scene.lock(); }

        void                                        SetName( const string & name )                              { m_name = name; }
        const string &                              GetName( ) const                                            { return m_name; }

        const shared_ptr<vaSceneObject> &           GetParent( ) const                                          { return m_parent; }

        const std::vector<shared_ptr<vaSceneObject>> &   GetChildren( ) const                                        { return m_children; }
    
        const vaMatrix4x4 &                         GetLocalTransform( ) const                                  { return m_localTransform; }
        void                                        SetLocalTransform( const vaMatrix4x4 & newTransform )       { m_localTransform = newTransform; }

        vaMatrix4x4                                 GetWorldTransform( ) const;
        
        // does not include children BB or any transformation - just represents combined vaRenderMesh-es
        const vaBoundingBox &                       GetLocalAABB( ) const                                       { return m_computedLocalBoundingBox; }

        // in world space, with all children bounding boxes added recursively
        const vaBoundingBox &                       GetGlobalAABB( ) const                                      { return m_computedGlobalBoundingBox; }

        // Warning: this will just an ID of the render mesh, which will not increase the shared_ptr<vaRenderMesh> reference count; the caller still 
        // needs to keep it alive; however it is safe for it to get destroyed without calling RemoveRenderMesh (although somewhat inefficient) - it's 
        // also ok to destroy and re-create with the same ID (unload one asset pack and load another one) as they are only tracked by the ID.
        void                                        AddRenderMeshRef( const shared_ptr<vaRenderMesh> & renderMesh );
        int                                         GetRenderMeshCount( )                                       { return (int)m_renderMeshes.size(); }
        shared_ptr<vaRenderMesh>                    GetRenderMesh( int index ) const;
        bool                                        RemoveRenderMeshRef( const shared_ptr<vaRenderMesh> & renderMesh );
        bool                                        RemoveRenderMeshRef( int index );

        void                                        FindClosestRecursive( const vaVector3 & worldLocation, shared_ptr<vaSceneObject> & currentClosest, float & currentDistance );

        void                                        TickRecursive( vaSceneOld & scene, float deltaTime );

        bool                                        IsDestroyed( ) const                                        { return m_destroyedButNotYetRemovedFromScene; }
        bool                                        IsBeingCreated( ) const                                     { return m_createdButNotYetAddedToScene; }

        // SplitMeshes will go through meshes and try splitting them up by the planes in a loop until they can't be split anymore
        // The "splitting" only happens if no single triangle intersects the mesh, so there's no cutting of geometry.
        // Returns the lists of old meshes and newly split meshes
        int                                         SplitMeshes( const std::vector<vaPlane> & splitPlanes, std::vector<shared_ptr<vaRenderMesh>> & outOldMeshes, std::vector<shared_ptr<vaRenderMesh>> & outNewMeshes, const int minTriangleCountThreshold = 512, const std::vector<shared_ptr<vaRenderMesh>> & candidateMeshes = {}, bool splitIfIntersectingTriangles = false );

    public:
        bool                                        Serialize( vaXMLSerializer & serializer ) override;

    protected:
        // we should have a recursive version of this - not yet implemented
        vaDrawResultFlags                           SelectForRendering( vaRenderInstanceList * opaqueList, vaRenderInstanceList * transparentList, const vaRenderInstanceList::FilterSettings & filter = vaRenderInstanceList::FilterSettings(), const SelectionFilterCallback & customFilter = nullptr );

        void                                        UpdateLocalBoundingBox( );

    protected:
        virtual string                              UIPropertiesItemGetDisplayName( ) const override                       { return m_name; }
        virtual void                                UIPropertiesItemTick( vaApplicationBase & application, bool openMenu, bool hovered ) override;

    public:
        // temp hack :)
        void                                        ReplaceMaterialsWithXXX( );

        void                                        EnumerateUsedAssets( const std::function<void(vaAsset * asset)> & callback );
    };

    class vaSceneOld : public vaUIPanel, public std::enable_shared_from_this<vaSceneOld>//, public vaXMLSerializable
    {
        // to support creation/destruction of objects from Tick!
        struct DeferredObjectAction
        {
            enum ActionType
            {
                AddObject,
                RemoveObject,
                RemoveObjectAndChildren,
                RemoveAllObjects,
            };

            shared_ptr<vaSceneObject>       Object;
            shared_ptr<vaSceneObject>       ParentObject;
            ActionType                      Action;
            
            // perhaps add stack trace here in _DEBUG, to make backtracking on asserts/bugs easier?

            DeferredObjectAction( const shared_ptr<vaSceneObject> & object, const shared_ptr<vaSceneObject> & parentObject, ActionType action ) : Object(object), ParentObject(parentObject), Action(action) { }
        };

    protected:
        //std::shared_ptr<vaScenePhysics>        m_physics;

        vaGUID                                      m_UID                   = vaGUID::Create();

        string                                      m_name                  = "Unnamed scene";

        // for now keep these here, in the future they will be object components (but maybe leave these as well - not sure)
        std::vector<shared_ptr<vaLight>>                 m_lights;

        std::vector<shared_ptr<vaSceneObject>>           m_allObjects;
        std::vector<shared_ptr<vaSceneObject>>           m_rootObjects;

        std::vector<string>                              m_assetPackNames;            // names of the below used asset packs
        std::vector<shared_ptr<vaAssetPack>>             m_assetPacks;                // currently used asset packs

        string                                      m_skyboxTexturePath;            // ideally should be asset ID but hack for now...
        vaMatrix3x3                                 m_skyboxRotation;
        float                                       m_skyboxColorMultiplier;
        shared_ptr<vaTexture>                       m_skyboxTexture;                // from m_skyboxTexturePath 
        // 
        // string                                      m_envmapTexturePath;            // ideally should be asset ID but hack for now...
        // vaMatrix3x3                                 m_envmapRotation;
        // float                                       m_envmapColorMultiplier;
        // shared_ptr<vaTexture>                       m_envmapTexture;                // from m_envmapTexturePath 

        std::vector<DeferredObjectAction>                m_deferredObjectActions;

        vaFogSphere                                 m_fog;


    protected:
        // debug UI stuff
        weak_ptr<vaLight>                           m_UI_SelectedLight;
        weak_ptr<vaSceneObject>                     m_UI_SelectedObject;
        bool                                        m_UI_ShowObjectsAsTree                  = true;
        bool                                        m_UI_ShowLights                         = false;
        vaVector3                                   m_UI_MouseClickIndicator;
        const float                                 m_UI_MouseClickIndicatorTotalTime       = 0.15f;
        float                                       m_UI_MouseClickIndicatorRemainingTime   = 0.0f;

        int64                                       m_tickIndex;
        double                                      m_sceneTime;

        bool                                        m_isInTick                                  = false;

        Scene::IBLProbe                             m_IBLProbeLocal;
        Scene::IBLProbe                             m_IBLProbeDistant;
        //string                                      m_IBLProbeDistantImagePath                  = "";       // if this is used, m_IBLProbeDistant is not used; path either absolute or, better, relative to vaCore::GetMediaRootPath()

    public:
        vaSceneOld( );
        explicit vaSceneOld( const string & name );
        virtual ~vaSceneOld( );

    public:
        bool                                        Serialize( vaXMLSerializer & serializer, bool mergeToExistingIfLoading );

        // Save to file
        bool                                        Save( const wstring & fileName );
        // Load from file
        bool                                        Load( const wstring & fileName, bool mergeToExisting = false );

        shared_ptr<vaSceneObject>                   CreateObject( const shared_ptr<vaSceneObject> & parent = nullptr );
        shared_ptr<vaSceneObject>                   CreateObject( const string & name, const vaMatrix4x4 & localTransform, const shared_ptr<vaSceneObject> & parent = nullptr );
        shared_ptr<vaSceneObject>                   CreateObject( const string & name, const vaVector3 & localScale, const vaQuaternion & localRot, const vaVector3 & localPos, const shared_ptr<vaSceneObject> & parent = nullptr );
        
        void                                        DestroyObject( const shared_ptr<vaSceneObject> & ptr, bool destroyChildrenRecursively, bool applyOwnTransformToChildren );
        void                                        DestroyAllObjects( );

        void                                        ApplyDeferredObjectActions( );

        void                                        SetSkybox( vaRenderDevice & device, const string & texturePath, const vaMatrix3x3 & rotation, float colorMultiplier );
        void                                        GetSkybox( shared_ptr<vaTexture> & outTexture, vaMatrix3x3 & outRotation, float & outColorMultiplier )  { outTexture = m_skyboxTexture; outRotation = m_skyboxRotation; outColorMultiplier = m_skyboxColorMultiplier; }
        // 
        // void                                        SetEnvmap( vaRenderDevice & device, const string & texturePath, const vaMatrix3x3 & rotation, float colorMultiplier );
        // void                                        GetEnvmap( shared_ptr<vaTexture> & outTexture, vaMatrix3x3 & outRotation, float & outColorMultiplier )  { outTexture = m_envmapTexture; outRotation = m_envmapRotation; outColorMultiplier = m_envmapColorMultiplier; }

        Scene::IBLProbe &                           LocalIBL( )                 { return m_IBLProbeLocal;   }
        Scene::IBLProbe &                           DistantIBL( )               { return m_IBLProbeDistant; }

        // Updates light setup to vaSceneLighting
        void                                        ApplyToLighting( vaSceneLighting & lighting );

        string &                                    Name( )                     { return m_name; }

        // temporary - should be SceneObject components in the future
        std::vector<shared_ptr<vaLight>> &               Lights( )                   { return m_lights; }

        int64                                       GetTickIndex( ) const       { return m_tickIndex; }

        void                                        Clear( );

        bool                                        PostLoadInit( );

        //void                                        UpdateUsedPackNames( );
        //void                                        LoadAndConnectAssets( );

        void                                        Tick( float deltaTime );
        bool                                        IsInTick( ) const           { return m_isInTick; }

        vaDrawResultFlags                           SelectForRendering( vaRenderInstanceList * opaqueList, vaRenderInstanceList * transparentList, const vaRenderInstanceList::FilterSettings & filter = vaRenderInstanceList::FilterSettings(), const SelectionFilterCallback & customFilter = nullptr );

        std::vector<shared_ptr<vaSceneObject>>           FindObjects( std::function<bool(vaSceneObject&obj)> searchCriteria );

        void                                        OnMouseClick( const vaVector3 & worldClickLocation );

        // various tools
        
        // SplitMeshes will go through all objects their meshes and try splitting them up by the planes in a loop until they can't be split anymore
        // The "splitting" only happens if no single triangle intersects the mesh, so there's no cutting of geometry.
        // See function on use example 
        int                                         SplitMeshes( const std::vector<vaPlane> & splitPlanes, std::vector<shared_ptr<vaRenderMesh>> & outOldMeshes, std::vector<shared_ptr<vaRenderMesh>> & outNewMeshes, const int minTriangleCountThreshold = 512, const std::vector<shared_ptr<vaRenderMesh>> & candidateMeshes = {}, bool splitIfIntersectingTriangles = false );

        shared_ptr<vaScene>                         ToNew( ) const;

    protected:
        void                                        RegisterUsedAssetPackNameCallback( const vaAssetPack & assetPack );

        void                                        RemoveAllUnusedAssets( const std::vector<shared_ptr<vaAssetPack>> & assetPacks );

    protected:
        // vaImguiHierarchyObject
        virtual string                              UIPanelGetDisplayName( ) const override     { return m_name; }
        virtual void                                UIPanelTick( vaApplicationBase & application ) override;

    private:
        friend class vaSceneObject;
        void                                        RegisterRootObjectAdded( const shared_ptr<vaSceneObject> & object );
        void                                        RegisterRootObjectRemoved( const shared_ptr<vaSceneObject> & object );

        bool                                        SerializeObjectsRecursive( vaXMLSerializer & serializer, const string & name, std::vector<shared_ptr<vaSceneObject>> & objectList, const shared_ptr<vaSceneObject> & parent );

        void                                        DestroyObjectImmediate( const shared_ptr<vaSceneObject> & obj, bool recursive );

        void                                        DrawUI( const vaCameraBase& camera, vaDebugCanvas2D& canvas2D, vaDebugCanvas3D& canvas3D );


    public:
        shared_ptr<vaSceneObject>                   CreateObjectWithSystemMesh( vaRenderDevice & device, const string & systemMeshName, const vaMatrix4x4 & transform );
        std::vector<shared_ptr<vaSceneObject>>           InsertAllPackMeshesToSceneAsObjects( vaSceneOld & scene, vaAssetPack & pack, const vaMatrix4x4 & transform );

    };

#if 0
    class vaSceneTools
    {
    public:
        template< typename InArrayElementType, typename OutArrayElementType >
        static int FrustumCull( InArrayElementType* arrayIn, size_t arrayInCount, std::vector<OutArrayElementType>& arrayOut, vaPlane frustumPlanes[], const int planeCount = 6 );
    };


    template< typename InArrayElementType, typename OutArrayElementType >
    inline int vaSceneTools::FrustumCull( InArrayElementType* arrayIn, size_t arrayInCount, std::vector<OutArrayElementType>& arrayOut, vaPlane frustumPlanes[], const int planeCount )
    {
        int addedCount = 0;

        for( size_t i = 0; i < arrayInCount; i++ )
        {
            vaOrientedBoundingBox obb;
            arrayIn[i]->GetBounds( obb );

            if( obb.IntersectFrustum( frustumPlanes, planeCount ) )
            {
                addedCount++;
                arrayOut.push_back( static_cast<OutArrayElementType>( arrayIn[i] ) );
            }
        }

        return addedCount;
    }
#endif
}
#endif