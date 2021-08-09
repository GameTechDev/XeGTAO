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

#include "Core/System/vaFileTools.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"

using namespace Vanilla;

bool Scene::IsBeingDestroyed( const entt::registry & registry, entt::entity entity )
{
    return registry.ctx<Scene::BeingDestroyed>( ).Entity == entity;
}

void Scene::DestroyTagged( entt::registry & registry )
{
    assert( vaThreading::IsMainThread() );

    // indicate that this is the only place & time where one can destroy entities
    assert( (registry.try_ctx<Scene::AccessPermissions>() != nullptr) && !registry.try_ctx<Scene::AccessPermissions>()->CanDestroyEntity() );
    registry.ctx<Scene::AccessPermissions>().SetState( AccessPermissions::State::SerializedDelete );

    registry.view<Scene::DestroyTag>( ).each( [&]( entt::entity entity )
    {
        assert( registry.ctx<Scene::BeingDestroyed>( ).Entity == entt::null );
        registry.ctx<Scene::BeingDestroyed>( ).Entity = entity;
        registry.destroy( entity );
        registry.ctx<Scene::BeingDestroyed>( ).Entity = entt::null;
    } );

    // indicate that no one can destroy component anymore
    registry.ctx<Scene::AccessPermissions>().SetState( AccessPermissions::State::Serialized );
}

void DisconnectParent( entt::registry& registry, entt::entity child, Scene::Relationship& childInfo )
{
    assert( registry.valid( child ) );
    if( childInfo.Parent == entt::null )
    {
        assert( false );
        return;
    }
    Scene::Relationship& oldParentInfo = registry.get<Scene::Relationship>( childInfo.Parent );
    assert( oldParentInfo.IsValid( registry ) );

    if( oldParentInfo.FirstChild == child )                     // are we old parent's first child? not anymore so need to update its link
    {
        assert( childInfo.PrevSibling == entt::null );          // if we're a parent's FirstChild, we can't have prev siblings!
        oldParentInfo.FirstChild = childInfo.NextSibling;       // update old parent's FirstChild to the next sibling (if any, or null)
        assert( oldParentInfo.FirstChild == entt::null || registry.valid( oldParentInfo.FirstChild ) );
    }
    else if( childInfo.PrevSibling != entt::null )              // is there a previous element in the list? make sure it no longer points at us
    {
        Scene::Relationship& prevSiblingInfo = registry.get<Scene::Relationship>( childInfo.PrevSibling );
        assert( prevSiblingInfo.IsValid( registry ) );
        assert( prevSiblingInfo.NextSibling == child );         // it should have pointed at us
        prevSiblingInfo.NextSibling = childInfo.NextSibling;    // update prev sibling's link to next sibling (if any, or null)
        assert( prevSiblingInfo.NextSibling == entt::null || registry.valid( prevSiblingInfo.NextSibling ) );
    }
    if( childInfo.NextSibling != entt::null )                  // is there a next element in the list? make sure it no longer points at us
    {
        Scene::Relationship* nextSiblingInfo = &registry.get<Scene::Relationship>( childInfo.NextSibling );
        assert( nextSiblingInfo->IsValid( registry ) );
        assert( nextSiblingInfo->PrevSibling == child );        // it should have pointed at us
        nextSiblingInfo->PrevSibling = childInfo.PrevSibling;  // update prev sibling's link to next sibling (if any, or null)
        assert( nextSiblingInfo->PrevSibling == entt::null || registry.valid( nextSiblingInfo->PrevSibling ) );
    }
    oldParentInfo.ChildrenCount--;
    childInfo.Parent        = entt::null;
    childInfo.PrevSibling   = entt::null;
    childInfo.NextSibling   = entt::null;
    childInfo.Depth         = 0;
    assert( oldParentInfo.ChildrenCount >= 0 );

    Scene::UpdateRelationshipDepthsRecursive( registry, child );

    Scene::SetTransformDirtyRecursive( registry, child );
}

void Scene::DisconnectChildren( entt::registry & registry, entt::entity parent )
{
    assert( parent != entt::null ); 
    assert( registry.valid(parent) );
    Scene::Relationship & parentInfo = registry.get<Scene::Relationship>( parent );

    if( parentInfo.FirstChild != entt::null )
    {
        assert( registry.valid(parentInfo.FirstChild) );
        int index = 0;
        for( entt::entity child = parentInfo.FirstChild; child != entt::null; index++ )
        {
            assert( registry.valid( child ) );
            Relationship & childInfo = registry.get<Scene::Relationship>( child );

            // backup the next since we'll clear it out
            entt::entity nextChild = childInfo.NextSibling;

            childInfo.Parent        = entt::null;
            childInfo.NextSibling   = entt::null;
            childInfo.PrevSibling   = entt::null;
            childInfo.Depth         = 0;
            Scene::UpdateRelationshipDepthsRecursive( registry, child );
            SetTransformDirtyRecursive( registry, child );

            // move to next
            child = nextChild;
        }
    }
    parentInfo.FirstChild       = entt::null;
    parentInfo.ChildrenCount    = 0;
}

void Scene::DisconnectRelationship( entt::registry & registry, entt::entity entity )
{
    assert( registry.valid( entity ) );
    Scene::Relationship & childInfo = registry.get<Scene::Relationship>( entity );
    assert( childInfo.IsValid( registry ) );

    if( childInfo.FirstChild != entt::null )
    {
        assert( childInfo.ChildrenCount != 0 );
        DisconnectChildren( registry, entity );
    }
    // must not have children now
    assert( childInfo.FirstChild == entt::null );
    assert( childInfo.ChildrenCount == 0 );

    if( childInfo.Parent != entt::null )
        DisconnectParent( registry, entity, childInfo );
    assert( childInfo.PrevSibling == entt::null );
    assert( childInfo.NextSibling == entt::null );
}

void Scene::VisitChildren( const entt::registry& registry, entt::entity parent, std::function<void( entt::entity child, int index, entt::entity parent )> visitor )
{
    // if parent == null, traverse all root nodes!
    if( parent == entt::null )
    {
        auto hierarchyEntities  = registry.view<const Scene::Relationship>( );
        int index = 0;
        hierarchyEntities.each( [ & ]( entt::entity entity, const Scene::Relationship& relationship ) 
        {
            if( relationship.Depth == 0 )
                visitor( entity, index++, parent );
        } );
    }
    else
    {
        // traverse children nodes
        assert( registry.valid( parent ) );
        const Relationship & parentInfo = registry.get<Scene::Relationship>( parent );
        if( parentInfo.FirstChild == entt::null )
        {
            assert( parentInfo.ChildrenCount == 0 );
            return;
        }
    #ifdef _DEBUG
        Scene::Relationship parentInfoPrev = parentInfo;
    #endif
        int index = 0;
        for( entt::entity child = parentInfo.FirstChild; child != entt::null; index++ )
        {
            assert( registry.valid( child ) );
            const Relationship & childInfo = registry.get<Scene::Relationship>( child );
    #ifdef _DEBUG
            Scene::Relationship prevChildInfo = childInfo;
    #endif
            visitor( child, index, parent );
            assert( prevChildInfo.PrevSibling == childInfo.PrevSibling && prevChildInfo.NextSibling == childInfo.NextSibling );            // recursive changes to the list not allowed because all implications not thought out & tested yet, sorry

            child = childInfo.NextSibling;
        }
        // this is both to test validity of the list but also to enforce no recursive changes to it
        assert( parentInfo.ChildrenCount == index );
        assert( parentInfoPrev == parentInfo );            // recursive changes to the list not allowed because all implications not thought out & tested yet, sorry
    }
}

void Scene::VisitParents( const entt::registry & registry, entt::entity entity, std::function<void( entt::entity parent )> visitor, bool fromRoot )
{
    int depth = 0;
    entt::entity lineage[Scene::Relationship::c_MaxDepthLevels];
    const Relationship * relationship = registry.try_get<Scene::Relationship>( entity );
    if( relationship == nullptr )
        return;
    while( relationship != nullptr && relationship->Parent != entt::null )
    {
        lineage[depth++] = relationship->Parent;
        relationship = registry.try_get<Scene::Relationship>( relationship->Parent );
    }
    assert( depth == registry.get<Scene::Relationship>( entity ).Depth );

    for( int i = 0; i < depth; i++ )
        visitor( lineage[ (fromRoot)?(depth-i-1):(i) ] );
}

bool Scene::CanSetParent( entt::registry& registry, entt::entity child, entt::entity parent )
{
    if( !registry.valid( child ) || !registry.any_of<Scene::Relationship>( child ) )
        return false;       // only valid entities with Relationship struct can be added/removed
    if( parent != entt::null && (!registry.valid( child ) || !registry.any_of<Scene::Relationship>( child )) )
        return false;       // only valid entities with Relationship struct can be added/removed
    if( child == parent )
        return false;       // child cannot be its own parent
    
    Scene::Relationship & childInfo = registry.get<Scene::Relationship>( child );   
    assert( childInfo.IsValid( registry ) );
    if( childInfo.Parent == parent )
        return false;       // in this case nothing needs to change, it's already set, so return false

    if( parent == entt::null )
        return true;        // we currently have a non-null parent but we want to set it to null - that's fine!

    // this is used only to count the depth of the deepest leaf node to prevent any overruns
    int currentChildRelativeDepth = UpdateRelationshipDepthsRecursive( registry, child ) - childInfo.Depth + 1;
    Scene::Relationship& parentInfo = registry.get<Scene::Relationship>( parent );    
    assert( parentInfo.IsValid( registry ) );

    if( (parentInfo.Depth+currentChildRelativeDepth) >= Scene::Relationship::c_MaxDepthValue )
        return false;       // changing to the new parent would cause the combined depth to go over the max value - can't be done!

    // we have to prevent circular dependencies; we've already checked that our parent isn't ourselves so go check all parents
    for( entt::entity upent = parentInfo.Parent; upent != entt::null; upent = registry.get<Relationship>( upent ).Parent )
    {
        if( upent == child )
            return false;   // circular dependency detected - can't do that!
    }

    return true;
}

bool Scene::SetParent( entt::registry & registry, entt::entity child, entt::entity parent )
{
    if( !CanSetParent( registry, child, parent ) )
    {
        VA_WARN( "Scene::SetParent('%s', '%s') can't proceed - Scene::CanSetParent returns false.", Scene::GetNameAndID( registry, child ).c_str(), Scene::GetNameAndID( registry, parent ).c_str() );
        return false;
    }

    Scene::Relationship & childInfo = registry.get<Scene::Relationship>( child );
    assert( childInfo.IsValid( registry ) );

    // we're definitely changing the parent so we have to disconnect current one
    if( childInfo.Parent != entt::null )
        DisconnectParent( registry, child, childInfo );
    assert( childInfo.Parent == entt::null );
    assert( childInfo.PrevSibling == entt::null );
    assert( childInfo.NextSibling == entt::null );
    assert( childInfo.Depth == 0 );

    if( parent == entt::null )  // if new parent is null then just disconnect current and we're done here
    {
        SetTransformDirtyRecursive( registry, child );
        return true;
    }

    // ok, we've got a new parent to set, and it's not null
    Scene::Relationship& parentInfo = registry.get<Scene::Relationship>( parent );
    assert( parentInfo.IsValid( registry ) );

    assert( childInfo.Parent == entt::null );  // we already asserted on this above but do it again for my peace of mind :)
    
    // update the new relationship
    childInfo.Parent = parent;

    assert( childInfo.PrevSibling == entt::null );  // we already asserted on this above but do it again for my peace of mind :)
    assert( childInfo.NextSibling == entt::null );  // we already asserted on this above but do it again for my peace of mind :)

    // if there's a list, insert ourselves into it
    if( parentInfo.FirstChild != entt::null )
    {
        Scene::Relationship & firstSiblingInfo = registry.get<Scene::Relationship>( parentInfo.FirstChild );
        assert( firstSiblingInfo.IsValid( registry ) );
        firstSiblingInfo.PrevSibling = child;
        childInfo.NextSibling = parentInfo.FirstChild;
    }
    parentInfo.FirstChild = child; // we're the first child now!
    parentInfo.ChildrenCount++;

    // if the depth changed, we've got to make sure the depths are correct for the whole tree
    if( childInfo.Depth != parentInfo.Depth+1 )
    {
        childInfo.Depth = parentInfo.Depth+1;
        Scene::UpdateRelationshipDepthsRecursive( registry, child );
    }

    SetTransformDirtyRecursive( registry, child );
    assert( parentInfo.ChildrenCount > 0 );
    assert( childInfo.Depth <= Scene::Relationship::c_MaxDepthValue );
    return true;
}

entt::entity Scene::GetParent( const entt::registry & registry, entt::entity entity )
{
    const Scene::Relationship * relationship = registry.try_get<Scene::Relationship>( entity );
    return ( relationship != nullptr )?(relationship->Parent):(entt::null);
}

namespace
{
    static string const g_nullString ="<null>";
    static string const g_emptyString = "";
}

const string & Scene::GetName( const entt::registry & registry, entt::entity entity )
{
    if( entity == entt::null )
        return g_nullString;
    auto nameComp = registry.try_get<Scene::Name>( entity ); return ( nameComp != nullptr ) ? ( *nameComp ) : ( g_emptyString );
}

string Scene::GetIDString( const entt::registry & , entt::entity entity )
{
    if( entity == entt::null )
        return g_nullString;
    return vaStringTools::Format( "%#010x", (int32)entity );
}

string Scene::GetNameAndID( const entt::registry & registry, entt::entity entity )
{
    if( entity == entt::null )
        return g_nullString;
    const string & name = GetName( registry, entity );
    string id = GetIDString( registry, entity );
    if( name != "" )
        return id + ":" + name;
    else
        return id + ":<noname>";
}

int Scene::UpdateRelationshipDepthsRecursive( entt::registry& registry, entt::entity entity )
{
    int maxDepth = registry.get<Scene::Relationship>( entity ).Depth;
    std::function<void(entt::entity, int, entt::entity parent)> updateVisitor;
    updateVisitor = [ &registry, &updateVisitor, &maxDepth ]( entt::entity child, int, entt::entity parent )
    {
        const Scene::Relationship & parentInfo = registry.get<Scene::Relationship>(parent);
        assert( parentInfo.Depth <= Scene::Relationship::c_MaxDepthValue );
        if( parentInfo.Depth == Scene::Relationship::c_MaxDepthValue )
        {
            assert( false );    // this should never have happened - investigate, figure out how we got here
            VA_WARN( "Scene::UpdateRelationshipDepthsRecursive - Scene::Relationship::c_MaxDepthValue reached! We're unparenting the children for this specific entity (%s).", Scene::GetNameAndID( registry, child ).c_str() );
            Scene::DisconnectChildren( registry, child );
        }
        else
        {
            auto & childInfo = registry.get<Scene::Relationship>( child );
            if( childInfo.Depth != parentInfo.Depth+1 )
            {
                childInfo.Depth = parentInfo.Depth+1;
                VisitChildren( registry, child, updateVisitor );
            }
            maxDepth = std::max( maxDepth, childInfo.Depth );
        }
    };

    VisitChildren( registry, entity, updateVisitor );
    return maxDepth;
}

void Scene::SetTransformDirtyRecursive( entt::registry & registry, entt::entity entity )
{
    if( registry.any_of<Scene::TransformDirtyTag>( entity ) )      // early out
        return;

    std::function<void( entt::entity, int, entt::entity )> updateVisitor;
    updateVisitor = [ &registry, &updateVisitor ]( entt::entity child, int, entt::entity parent )
    {
        assert( registry.get<Scene::Relationship>(parent).Depth <= Scene::Relationship::c_MaxDepthValue ); parent;
        if( !registry.any_of<Scene::TransformDirtyTag>( child ) )  // early out
        {
            if( !IsBeingDestroyed( registry, child ) )
                registry.emplace<Scene::TransformDirtyTag>( child );
            VisitChildren( registry, child, updateVisitor );
        }
    };

    if( !IsBeingDestroyed( registry, entity ) )
        registry.emplace<Scene::TransformDirtyTag>( entity );
    VisitChildren( registry, entity, updateVisitor );
}

void Scene::UpdateTransforms( entt::registry & registry, entt::entity entity, UniqueStaticAppendConsumeList & outBoundsDirtyList ) noexcept
{
    const auto & cregistry = std::as_const(registry);
    assert( cregistry.valid( entity ) );
    
    const Scene::TransformLocal &   localTransform = cregistry.get<Scene::TransformLocal>( entity );
    Scene::TransformWorld &         worldTransform = registry.get<Scene::TransformWorld>( entity );

    const Scene::Relationship & relationship = cregistry.get< Scene::Relationship >( entity );
    vaMatrix4x4 newWorldTransform;
    if( relationship.Parent == entt::null || cregistry.any_of< Scene::TransformLocalIsWorldTag >( entity ) )
    {
        assert( relationship.Parent != entt::null || relationship.Depth == 0 );
        newWorldTransform = localTransform;
    }
    else
    {
        const Scene::TransformWorld& worldTransformParent = cregistry.get< Scene::TransformWorld >( relationship.Parent );
        newWorldTransform = localTransform * worldTransformParent;
    }
    // update only if different
    if( newWorldTransform != worldTransform )
    {
        worldTransform = newWorldTransform;
        if( cregistry.any_of<Scene::WorldBounds>( entity ) )
            outBoundsDirtyList.Append( entity );
    }
}

bool Scene::UniqueStaticAppendConsumeList::StartAppending( uint32 maxCount ) noexcept
{
    // VA_TRACE_CPU_SCOPE( UniqueStaticAppendListReset );
    if( Capacity < maxCount )
    {
        delete[] InList;
        Capacity = maxCount;
        InList = new FlagBlock[Capacity];
        for( uint32 i = 0; i < CurrentMaxCount; i++ )
            InList[i].Flag.store( false, std::memory_order_release );
    }
    else
    {
#if 1
        // option 1: clear all
        for( uint32 i = 0; i < CurrentMaxCount; i++ )
            InList[i].Flag.store( false, std::memory_order_release );
#else
        // option 2: only clear flags for those used previously, if any
        const uint32 count = (uint32)List.Count();
        for( uint32 i = 0; i < count; i++ )
        {
            uint32 index = entity_to_index( List[i] );
            assert( InList[index].Flag.load() );
            InList[index].Flag.store( false, std::memory_order_release );
        }
#endif
    }
    CurrentMaxCount = maxCount;
    return List.StartAppending( );
}

struct EntitySerializeHelper
{
    entt::registry *    Registry;
    entt::entity        Entity;

    EntitySerializeHelper( entt::registry * registry )                      : Registry(registry), Entity(entt::null)        { }
    EntitySerializeHelper( entt::registry * registry, entt::entity entity ) : Registry(registry), Entity(entity)            { }

    static const char * S_Type( )                                           { return ""; }  // "Entity"; } <- could use this but it's cleaner without so since there's no real use case for having a type let's not use it

    bool                S_Serialize( vaSerializer & serializer )
    {
        std::vector< EntitySerializeHelper > childEntities;
        if( serializer.IsReading( ) )
        {
            Entity = Registry->create( );

            string name;
            if( serializer.Serialize( "Name", name ) )
                Registry->emplace<Scene::Name>( Entity, name );

            const int componentTypeCount = Scene::Components::TypeCount( );
            for( int i = 0; i < componentTypeCount; i++ )
            {
                if( Scene::Components::HasSerialize( i ) )
                {
                    const string & typeName = Scene::Components::TypeName(i);
                    if( serializer.Has( typeName ) )
                    {
                        assert( !Scene::Components::Has( i, *Registry, Entity ) ); // <- this is fine actually due to reactive nature
                        Scene::Components::EmplaceOrReplace( i, *Registry, Entity );
                        if( !serializer.Serialize( typeName, /*typeName*/"", [i, &r=*Registry, e=Entity]( vaSerializer & snode ) { return Scene::Components::Serialize( i, r, e, snode ); } ) )
                        {  
                            VA_WARN( "Error while trying to deserialize component name %s for entity name %s - skipping.", typeName.c_str(), name.c_str() );
                            Scene::Components::Remove( i, *Registry, Entity );
                        }
                    }
                }
            }

            if( serializer.SerializeVector( "[ChildEntities]", childEntities, EntitySerializeHelper( Registry ) ) )
            {
                Registry->emplace<Scene::Relationship>( Entity );
                for( int i = 0; i < childEntities.size( ); i++ )
                    Scene::SetParent( *Registry, childEntities[i].Entity, Entity );
                Scene::SetTransformDirtyRecursive( *Registry, Entity );
            }
        }
        else if( serializer.IsWriting( ) )
        {
            auto name = Registry->try_get<Scene::Name>( Entity );
            if( name != nullptr )
                serializer.Serialize( "Name", *static_cast<std::string*>(name) );

            bool hasRelationship = Registry->any_of<Scene::Relationship>( Entity );
            if( hasRelationship )
            {
                Scene::VisitChildren( *Registry, Entity, [&](entt::entity child) { childEntities.push_back( EntitySerializeHelper(Registry, child) ); } );
                std::reverse( childEntities.begin(), childEntities.end() ); // reverse because children get added in reverse so this preserves the original order
                serializer.SerializeVector( "[ChildEntities]", childEntities, EntitySerializeHelper( Registry ) );
            }

            const int componentTypeCount = Scene::Components::TypeCount( );
            for( int i = 0; i < componentTypeCount; i++ )
            {
                if( Scene::Components::HasSerialize( i ) && Scene::Components::Has( i, *Registry, Entity ) )
                {
                    const string & typeName = Scene::Components::TypeName(i);
                    if( !serializer.Serialize( typeName, /*typeName*/"", [i, &r=*Registry, e=Entity]( vaSerializer & snode ) { return Scene::Components::Serialize( i, r, e, snode ); } ) )
                        { assert( false ); return false; }
                }
            }
        }
        else
            { assert( false ); return false; }


        return true;
    }

};

// void SerializeEntityRecursive( entt::registry& registry, entt::entity entity )
// {
// 
// }

bool Scene::SaveJSON( entt::registry & registry, const string & filePath, std::function<bool( entt::entity entity )> filter )
{
    registry; filter;
    vaSerializer serializer = vaSerializer::OpenWrite( "VanillaScene" );

    bool retVal = true;

    // add unique IDs to all

    std::vector< EntitySerializeHelper > rootEntities, unrootEntities;
    registry.each( [&] ( entt::entity entity )
    { 
        const Scene::Relationship * relationship = registry.try_get<Scene::Relationship>( entity );
        if( relationship == nullptr )
            unrootEntities.push_back( EntitySerializeHelper( &registry, entity ) );
        else if( relationship->Parent == entt::null )
            rootEntities.push_back( EntitySerializeHelper( &registry, entity ) );
    } );
    std::reverse( rootEntities.begin(), rootEntities.end() );       // registry.each iterates them in the reverse order, so invert that to preserve ordering
    std::reverse( unrootEntities.begin(), unrootEntities.end() );   // registry.each iterates them in the reverse order, so invert that to preserve ordering

    retVal &= serializer.SerializeVector( "ROOT", rootEntities, EntitySerializeHelper( &registry ) );
    retVal &= serializer.SerializeVector( "UNROOT", unrootEntities, EntitySerializeHelper( &registry ) );
    
    retVal &= serializer.Write( filePath );

    assert( retVal );
    
    return retVal;
}

bool Scene::LoadJSON( entt::registry & registry, const string & filePath )
{
    vaSerializer serializer = vaSerializer::OpenRead( filePath, "VanillaScene" );

    registry; filePath;
    bool retVal = true;

    std::vector< EntitySerializeHelper > rootEntities, unrootEntities;
    retVal &= serializer.SerializeVector( "ROOT", rootEntities, EntitySerializeHelper( &registry ) );
    retVal &= serializer.SerializeVector( "UNROOT", unrootEntities, EntitySerializeHelper( &registry ) );
    
    assert( retVal );
    
    return retVal;
}

entt::entity Scene::FindFirstByName( const entt::registry & registry, const string & name, entt::entity startEntity, bool recursive )
{
    entt::entity found = entt::null;
    VisitChildren( registry, startEntity, [ &registry, &name, &found, recursive ] ( entt::entity entity )
    {
        if( found != entt::null ) return;   // already found, drop out
        const Scene::Name * nameComponent = registry.try_get<Scene::Name>( entity );
        if( nameComponent != nullptr && (*nameComponent) == name )
        {
            found = entity;
            return;
        }
        if( recursive )
            found = FindFirstByName( registry, name, entity, true );
    } );
    return found;
}
