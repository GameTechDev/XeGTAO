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
#include "Core/vaSerializer.h"

#include "Rendering/vaRenderInstanceList.h"

#include "vaSceneTypes.h"
#include "vaSceneComponents.h"

namespace Vanilla
{
    class vaScene;
    class vaSceneLighting;

    // entt-specific scene namespace
    namespace Scene
    {
        void                        DestroyTagged( entt::registry & registry );
        bool                        IsBeingDestroyed( const entt::registry & registry, entt::entity entity );

        // For these to work, entities must have Scene::Relationsip; if maintainWorldTransform, will attempt to readjust child's local transform with regards to new parent's world to maintains previous world transform. 
        bool                        SetParent( entt::registry & registry, entt::entity child, entt::entity parent, bool maintainWorldTransform );
        entt::entity                GetParent( const entt::registry & registry, entt::entity entity );
        bool                        CanSetParent( entt::registry & registry, entt::entity child, entt::entity parent ); 
        void                        DisconnectChildren( entt::registry & registry, entt::entity parent );
        void                        DisconnectRelationship( entt::registry & registry, entt::entity entity );
        // Visit all entity's children but this does not recursively visit children's children (add if required)
        // TODO: for performance, templated 'CallableType' version to avoid std::function overhead
        void                        VisitChildren( const entt::registry & registry, entt::entity entity, std::function<void( entt::entity child, int index, entt::entity parent )> visitor );
        void                        VisitChildren( const entt::registry & registry, entt::entity entity, std::function<void( entt::entity child )> visitor );
        // This will visit all the parents, and the direction is defined by 'fromRoot' (if 'true', it starts from the root and stops with the entity's parent)
        void                        VisitParents( const entt::registry & registry, entt::entity entity, std::function<void( entt::entity parent )> visitor, bool fromRoot = false );
        int                         UpdateRelationshipDepthsRecursive( entt::registry & registry, entt::entity entity );    // return max Depth

        // Set (also sets dirty)
        void                        MoveToWorld( entt::registry & registry, entt::entity entity, vaMatrix4x4 & newWorldTransform );

        // Check if the entity or any of its parents recursively have ComponentType
        template<typename ComponentType>
        bool                        HasOrParentsHave( const entt::registry & registry, entt::entity entity );

        // goes through all components and add all references to the list
        void                        ListReferences( const entt::registry & registry, entt::entity entity, std::vector<Scene::EntityReference*> & referenceList );

        // Search by Scene::Name; Names are not unique and don't have to be part of an entity; if entt::null then search from the root; not case-sensitive; this function is not particularly fast (there is no 'index' of names)
        entt::entity                FindFirstByName( const entt::registry & registry, const string & name, entt::entity startEntity = entt::null, bool recursive = false );

        // If changing local transform, call this to make sure it's propagated to TransformWorld as well as child entities
        void                        SetTransformDirtyRecursive( entt::registry & registry, entt::entity entity );   
        void                        SetTransformDirtyRecursiveUnsafe( entt::registry & registry, entt::entity entity );     // same as SetTransformDirtyRecursive but does not check for relationship

        // Load/Save to JSON format using a hierarchical representation; entities can be skipped by returning false from the 'filter', but it will also skip their children.
        bool                        JSONSave( entt::registry & registry, const string & filePath, std::function<bool( entt::entity entity )> filter = nullptr );
        bool                        JSONLoad( entt::registry & registry, const string & filePath );

        // Load/Save a subtree to JSON format using a hierarchical representation
        string                      JSONSaveSubtree( entt::registry & registry, entt::entity entity );
        int                         JSONLoadSubtree( const string & jsonData, entt::registry & registry, entt::entity parentEntity, bool regenerateUIDs = true );
        // Test whether a text looks like a subtree JSON format
        bool                        JSONIsSubtree( const char * text );
        // Name helpers
        const string &              GetName( const entt::registry & registry, entt::entity entity );
        string                      GetNameAndID( const entt::registry & registry, entt::entity entity );
        string                      GetIDString( const entt::registry & registry, entt::entity entity );
        
        // Returns Identity if no transform
        inline vaMatrix4x4          GetWorldTransform( const entt::registry & registry, entt::entity entity )                   { auto trans = registry.try_get<Scene::TransformWorld>(entity); if( trans == nullptr ) return vaMatrix4x4::Identity; return *trans; }

        // reactive handling of components needed based on other components
        // template<typename ComponentType>
        inline void                 AutoEmplaceDestroyWorldBounds( entt::registry & registry, entt::entity entity );
        inline void                 AutoEmplaceDestroyPreviousTransformWorld( entt::registry & registry, entt::entity entity );

        // used for concurrent processing of entities
        void                        UpdateTransforms( entt::registry & registry, entt::entity entity, class UniqueStaticAppendConsumeList & outBoundsDirtyList ) noexcept;

        void                        UIHighlight( entt::registry & registry, entt::entity entity );

        // Use VisitChildren to count all children recursively (not including parent entity)
        inline int                  CountChildren( const entt::registry & registry, entt::entity entity, bool recursive )       { int count = 0; Scene::VisitChildren( registry, entity, [&]( entt::entity child ) { count++; if(recursive) count += CountChildren( registry, child, recursive ); } ); return count; }
        // Use VisitChildren to tag all children for destruction : you still need to DestroyTagged(registry) if you want immediate destruction
        inline void                 TagDestroyChildren( entt::registry & registry, entt::entity entity, bool recursive )        { Scene::VisitChildren( registry, entity, [&]( entt::entity child ) { registry.emplace_or_replace<Scene::DestroyTag>( child ); if(recursive) TagDestroyChildren( registry, child, recursive ); } ); }

        // Allows adding entities to this list in a multithreaded way; have to reset it before use to pre-allocate storage
        // and reset values to 0.
        // Main feature (compared to just StaticAppendList) is that it will stop you from adding an item twice!
        // Then you can just iterate through List from 0 to CurrentCount (but don't do it concurrently while appending).
        class UniqueStaticAppendConsumeList
        {
            vaAppendConsumeList<entt::entity>
                                        List;

            struct FlagBlock
            {
                //char                    padding[60];
                std::atomic_bool        Flag            = false;
            };
            FlagBlock *                 InList          = nullptr;          // indicates if the entity (addressed with entity_to_index(entity)) is in the list - can be used to access registry.data()[]

            uint32                      CurrentMaxCount = 0;
            uint32                      Capacity        = 0;

        public:
            ~UniqueStaticAppendConsumeList( )       { delete[] InList; }

            bool                        IsConsuming( ) const noexcept               { return List.IsConsuming(); }

            bool                        StartAppending( uint32 maxCount ) noexcept;
            bool                        StartConsuming( ) noexcept                  { return List.StartConsuming(); }

            size_t                      Count( ) const noexcept                     { return List.Count(); }
            entt::entity                operator [] ( size_t index ) const noexcept { return List[index]; }
            
            bool                        Append( entt::entity );
        };

        //////////////////////////////////////////////////////////////////////////
        // Inlines
        //////////////////////////////////////////////////////////////////////////

        // since I don't know how to do a templated visitor, let's just do one overload...
        inline void                     VisitChildren( const entt::registry & registry, entt::entity parent, std::function<void( entt::entity child )> visitor )
        {
            return VisitChildren( registry, parent, [visitor](entt::entity child, int, entt::entity ) { visitor(child); } );
        }

        inline void                     SetTransformDirtyRecursive( entt::registry & registry, entt::entity entity )
        {
            assert( !IsBeingDestroyed( registry, entity ) );
            if( registry.any_of<Scene::Relationship>(entity) )
                SetTransformDirtyRecursiveUnsafe( registry, entity );
        }

        inline void                     AutoEmplaceDestroyWorldBounds( entt::registry& registry, entt::entity entity )
        {
            if( registry.ctx<Scene::AccessPermissions>().CanDestroyEntity() )
                return;
            assert( !IsBeingDestroyed( registry, entity ) );

            assert( registry.valid(entity) );
            bool hasPrimary = registry.any_of<WorldBounds>( entity );
            bool hasAnyOf   = registry.any_of<CustomBoundingBox, RenderMesh>( entity );
            if( hasPrimary && !hasAnyOf )
                registry.remove<WorldBounds>( entity );
            else if( !hasPrimary && hasAnyOf )
            {
                registry.emplace_or_replace<WorldBounds>( entity );
                registry.emplace_or_replace<WorldBoundsDirtyTag>( entity );
            }
        }

        inline void                     AutoEmplaceDestroyPreviousTransformWorld( entt::registry & registry, entt::entity entity )
        {
            if( registry.ctx<Scene::AccessPermissions>().CanDestroyEntity() )
                return;
            assert( !IsBeingDestroyed( registry, entity ) );

            assert( registry.valid(entity) );
            bool hasPrimary = registry.any_of<PreviousTransformWorld>( entity );
            bool hasAnyOf   = registry.any_of<TransformWorld>( entity );
            if( hasPrimary && !hasAnyOf )
                registry.remove<PreviousTransformWorld>( entity );
            else if( !hasPrimary && hasAnyOf )
                registry.emplace_or_replace<PreviousTransformWorld>( entity );
        }

        inline bool Scene::UniqueStaticAppendConsumeList::Append( entt::entity entity )
        {
            assert( !List.IsConsuming() );
            uint32 index = entity_to_index( entity );
            assert( index < CurrentMaxCount );
            bool wasIn = InList[index].Flag.exchange( true, std::memory_order_acq_rel );
            if( !wasIn )
                List.Append( entity );
            return !wasIn;
        }

        template<typename ComponentType>
        inline bool HasOrParentsHave( const entt::registry & registry, entt::entity entity )
        {
            int typeIndex = Scene::Components::TypeIndex<ComponentType>( );
            if( typeIndex == -1 )
            { assert( false ); return false; }
            if( Scene::Components::Has( typeIndex, registry, entity ) )
                return true;
            bool retVal = false;
            VisitParents( registry, entity, [&](entt::entity parent )
            {
                retVal |= Scene::Components::Has( typeIndex, registry, parent );
            } );
            return retVal;
        }

    }
}
