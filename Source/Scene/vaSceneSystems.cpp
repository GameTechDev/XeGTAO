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

    Scene::SetTransformDirtyRecursiveUnsafe( registry, child );
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
            SetTransformDirtyRecursiveUnsafe( registry, child );

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

void Scene::MoveToWorld( entt::registry & registry, entt::entity entity, vaMatrix4x4 & newWorldTransform )
{
    Scene::TransformLocal * transformLocal   = registry.try_get<Scene::TransformLocal>( entity );
    Scene::TransformWorld * transformWorld   = registry.try_get<Scene::TransformWorld>( entity );
    if( transformLocal == nullptr || transformWorld == nullptr )
    { assert( false ); return; }

    // update world - we know where we're setting it
    *transformWorld = newWorldTransform;

    // no hierarchy - unusual but possible
    if( !registry.any_of<Scene::Relationship>(entity) )
    {
        *transformLocal = newWorldTransform;
        return;
    }

    vaMatrix4x4 parentWorldTransform = vaMatrix4x4::Identity;
    entt::entity parent = GetParent( registry, entity );
    if( parent != entt::null && registry.any_of<Scene::TransformWorld>( parent ) )
        parentWorldTransform = registry.get<Scene::TransformWorld>( parent );
    vaMatrix4x4 parentWorldTransformInv = parentWorldTransform.InversedHighPrecision( );
    *transformLocal = *transformWorld * parentWorldTransformInv;
    SetTransformDirtyRecursiveUnsafe( registry, entity );
}

bool Scene::SetParent( entt::registry & registry, entt::entity child, entt::entity parent, bool maintainWorldTransform )
{
    if( !CanSetParent( registry, child, parent ) )
    {
        VA_WARN( "Scene::SetParent('%s', '%s') can't proceed - Scene::CanSetParent returns false.", Scene::GetNameAndID( registry, child ).c_str(), Scene::GetNameAndID( registry, parent ).c_str() );
        return false;
    }

    Scene::Relationship & childInfo = registry.get<Scene::Relationship>( child );
    assert( childInfo.IsValid( registry ) );

    if( maintainWorldTransform )
    {
        Scene::TransformLocal * childLocalTransform     = registry.try_get<Scene::TransformLocal>( child );
        Scene::TransformWorld * childWorldTransform     = registry.try_get<Scene::TransformWorld>( child );
        if( childLocalTransform != nullptr && childWorldTransform != nullptr )
        {
            vaMatrix4x4 parentWorldTransform = vaMatrix4x4::Identity;
            if( parent != entt::null && registry.any_of<Scene::TransformWorld>( parent ) )
                parentWorldTransform = registry.get<Scene::TransformWorld>( parent );
            vaMatrix4x4 parentWorldTransformInv = parentWorldTransform.InversedHighPrecision( );
            *childLocalTransform = *childWorldTransform * parentWorldTransformInv;
        }
    }


    // we're definitely changing the parent so we have to disconnect current one
    if( childInfo.Parent != entt::null )
        DisconnectParent( registry, child, childInfo );
    assert( childInfo.Parent == entt::null );
    assert( childInfo.PrevSibling == entt::null );
    assert( childInfo.NextSibling == entt::null );
    assert( childInfo.Depth == 0 );

    if( parent == entt::null )  // if new parent is null then just disconnect current and we're done here
    {
        SetTransformDirtyRecursiveUnsafe( registry, child );

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

    SetTransformDirtyRecursiveUnsafe( registry, child );
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

void Scene::SetTransformDirtyRecursiveUnsafe( entt::registry & registry, entt::entity entity )
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
    
    const Scene::TransformLocal &   localTransform      = cregistry.get<Scene::TransformLocal>( entity );
    Scene::TransformWorld &         worldTransform      = registry.get<Scene::TransformWorld>( entity );

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

void Scene::ListReferences( const entt::registry & registry, entt::entity entity, std::vector<Scene::EntityReference*> & referenceList )
{
    // go through all component types
    for( int typeIndex = 0; typeIndex < Scene::Components::TypeCount( ); typeIndex++ )
    {
        if( Scene::Components::HasListReferences( typeIndex ) && Scene::Components::Has( typeIndex, const_cast<entt::registry&>(registry), entity ) )
            Scene::Components::ListReferences( typeIndex, const_cast<entt::registry&>(registry), entity, referenceList );
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

namespace Vanilla::Scene
{
    struct EntitySerializeHelper
    {
        Scene::SerializeArgs *  SerializeArgs;
        entt::registry *        Registry;
        entt::entity            Entity;

        EntitySerializeHelper( Scene::SerializeArgs * serializeArgs, entt::registry * registry )                      : SerializeArgs(serializeArgs), Registry(registry), Entity(entt::null)        { }
        EntitySerializeHelper( Scene::SerializeArgs * serializeArgs, entt::registry * registry, entt::entity entity ) : SerializeArgs(serializeArgs), Registry(registry), Entity(entity)            { }

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
                vaGUID uid;
                if( serializer.Serialize( "UID", uid ) )
                {
                    // change to new UIDs during loading
                    if( SerializeArgs->UIDRemapper != nullptr )
                    {
                        vaGUID newUID = vaGUID::Create();
                        SerializeArgs->UIDRemapper->emplace( std::make_pair(uid, newUID) );
                        uid = newUID;
                    }
                    Registry->emplace<Scene::UID>( Entity, Scene::UID(uid) );
                }

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
                            if( !serializer.Serialize( typeName, /*typeName*/"", [i, &r=*Registry, args = SerializeArgs, e=Entity]( vaSerializer & snode ) { return Scene::Components::Serialize( i, r, e, *args, snode ); } ) )
                            {  
                                VA_WARN( "Error while trying to deserialize component name %s for entity name %s - skipping.", typeName.c_str(), name.c_str() );
                                Scene::Components::Remove( i, *Registry, Entity );
                            }
                        }
                    }
                }

                if( serializer.SerializeVector( "[ChildEntities]", childEntities, EntitySerializeHelper( SerializeArgs, Registry ) ) )
                {
                    Registry->emplace<Scene::Relationship>( Entity );
                    for( int i = 0; i < childEntities.size( ); i++ )
                        Scene::SetParent( *Registry, childEntities[i].Entity, Entity, false );
                    Scene::SetTransformDirtyRecursiveUnsafe( *Registry, Entity );
                }
            }
            else if( serializer.IsWriting( ) )
            {
                auto name = Registry->try_get<Scene::Name>( Entity );
                if( name != nullptr )
                    serializer.Serialize( "Name", *static_cast<std::string*>(name) );
                auto uid = Registry->try_get<Scene::UID>( Entity );
                if( uid != nullptr )
                    serializer.Serialize( "UID", *static_cast<vaGUID*>(uid) );

                bool hasRelationship = Registry->any_of<Scene::Relationship>( Entity );
                bool hasSkipChildren = Registry->any_of<Scene::SerializationSkipChildrenTag>( Entity );
                if( hasRelationship )
                {
                    if( !hasSkipChildren )
                    {
                        Scene::VisitChildren( *Registry, Entity, [&](entt::entity child) { if( !Registry->any_of<Scene::SerializationSkipTag>(child) ) childEntities.push_back( EntitySerializeHelper( SerializeArgs, Registry, child) ); } );
                        std::reverse( childEntities.begin(), childEntities.end() ); // reverse because children get added in reverse so this preserves the original order
                    }
                    serializer.SerializeVector( "[ChildEntities]", childEntities, EntitySerializeHelper( SerializeArgs, Registry ) );
                }

                const int componentTypeCount = Scene::Components::TypeCount( );
                for( int i = 0; i < componentTypeCount; i++ )
                {
                    if( Scene::Components::HasSerialize( i ) && Scene::Components::Has( i, *Registry, Entity ) )
                    {
                        const string & typeName = Scene::Components::TypeName(i);
                        if( !serializer.Serialize( typeName, /*typeName*/"", [i, &r=*Registry, args = SerializeArgs, e=Entity]( vaSerializer & snode ) { return Scene::Components::Serialize( i, r, e, *args, snode ); } ) )
                            { assert( false ); return false; }
                    }
                }
            }
            else
                { assert( false ); return false; }


            return true;
        }

    };
}

const char * c_JSONSubtreeID = "VanillaSceneSubtree";

bool Scene::JSONIsSubtree( const char * _text )
{
    if( _text == nullptr ) 
        return false;
    string text(_text);

    if( text.find( c_JSONSubtreeID ) == string::npos )
        return false;

    // maybe validate in more detail

    return true;
}

string Scene::JSONSaveSubtree( entt::registry & registry, entt::entity entity )
{
    vaSerializer serializer = vaSerializer::OpenWrite( ); //c_JSONSubtreeID );

    SerializeArgs serializeArgs( registry.ctx<Scene::UIDRegistry>() );

    // collect all entities to save
    std::vector< EntitySerializeHelper > subtreeEntities;
    subtreeEntities.push_back( EntitySerializeHelper( &serializeArgs, &registry, entity ) );
    // if( recursive )
    //     Scene::VisitChildren( registry, entity, [&]( entt::entity child ) { subtreeEntities.push_back(EntitySerializeHelper( &serializeArgs, &registry, child)); } );
    std::reverse( subtreeEntities.begin(), subtreeEntities.end() );       // registry.each iterates them in the reverse order, so invert that to preserve ordering

    bool retVal = true;
    retVal &= serializer.SerializeVector( c_JSONSubtreeID, subtreeEntities, EntitySerializeHelper( &serializeArgs, &registry ) );
    if( retVal )
        return serializer.Dump();
    else
    { assert( false ); return ""; }
}

bool Scene::JSONSave( entt::registry & registry, const string & filePath, std::function<bool( entt::entity entity )> filter )
{
    registry; filter;
    vaSerializer serializer = vaSerializer::OpenWrite( "VanillaScene" );

    bool retVal = true;

    SerializeArgs serializeArgs( registry.ctx<Scene::UIDRegistry>() );

    std::vector< EntitySerializeHelper > rootEntities, unrootEntities;
    registry.each( [&] ( entt::entity entity )
    { 
        const Scene::Relationship * relationship = registry.try_get<Scene::Relationship>( entity );
        if( relationship == nullptr )
            unrootEntities.push_back( EntitySerializeHelper( &serializeArgs, &registry, entity ) );
        else if( relationship->Parent == entt::null )
            rootEntities.push_back( EntitySerializeHelper( &serializeArgs, &registry, entity ) );
    } );
    std::reverse( rootEntities.begin(), rootEntities.end() );       // registry.each iterates them in the reverse order, so invert that to preserve ordering
    std::reverse( unrootEntities.begin(), unrootEntities.end() );   // registry.each iterates them in the reverse order, so invert that to preserve ordering

    serializer.Serialize<string>( "Name", registry.ctx<Scene::Name>( ) );

    retVal &= serializer.SerializeVector( "ROOT", rootEntities, EntitySerializeHelper( &serializeArgs, &registry ) );
    retVal &= serializer.SerializeVector( "UNROOT", unrootEntities, EntitySerializeHelper( &serializeArgs, &registry ) );
    
    retVal &= serializer.Write( filePath );

    assert( retVal );
    
    return retVal;
}

int Scene::JSONLoadSubtree( const string & jsonData, entt::registry & registry, entt::entity parentEntity, bool regenerateUIDs )
{
    jsonData; registry; regenerateUIDs;

    vaSerializer serializer = vaSerializer::OpenReadString( jsonData );
    if( !serializer.IsReading( ) )
        return -1;

    std::unordered_map< vaGUID, vaGUID, vaGUIDHasher > UIDRemapping;

    assert( regenerateUIDs ); // <- never tested without this, I'm not sure it will work
    SerializeArgs serializeArgs( registry.ctx<Scene::UIDRegistry>(), (regenerateUIDs)?(&UIDRemapping):(nullptr) );

    bool retVal = true;

    std::vector< EntitySerializeHelper > subtreeEntities;
    retVal &= serializer.SerializeVector( c_JSONSubtreeID, subtreeEntities, EntitySerializeHelper( &serializeArgs, &registry ) );

    if( !retVal )
    {
        assert( false ); // TODO: gracefully exit and cleanup all currently loaded entities
        return -1;
    }

    // connect references and update to remapped UID
    assert( serializeArgs.UIDRemapper != nullptr );
    for( int i = 0; i < serializeArgs.LoadedReferences.size( ); i++ )
    {
        vaGUID referenceID = serializeArgs.LoadedReferences[i].second;
        if( serializeArgs.UIDRemapper != nullptr )
        {
            auto it = serializeArgs.UIDRemapper->find( serializeArgs.LoadedReferences[i].second );
            if( it != serializeArgs.UIDRemapper->end() )
                referenceID = it->second;
        }
        (*serializeArgs.LoadedReferences[i].first) = Scene::EntityReference( serializeArgs.UIDRegistry, referenceID );
    }

    int totalCount = 0;
    for( EntitySerializeHelper & loadedEntity : subtreeEntities )
    {
        if( parentEntity != entt::null )
            Scene::SetParent( registry, loadedEntity.Entity, parentEntity, false );
        totalCount += 1 + Scene::CountChildren( registry, loadedEntity.Entity, true );
    }

    return totalCount;
}

bool Scene::JSONLoad( entt::registry & registry, const string & filePath )
{
    vaSerializer serializer = vaSerializer::OpenReadFile( filePath, "VanillaScene" );

    if( !serializer.IsReading( ) )
    {
        VA_LOG_WARNING( "Error opening '%s' as a JSON stream", filePath.c_str() );
        return false;
    }

    SerializeArgs serializeArgs( registry.ctx<Scene::UIDRegistry>() );

    // should sanitize name after this
    serializer.Serialize<string>( "Name", registry.ctx<Scene::Name>( ), "UnnamedScene" );

    bool retVal = true;

    std::vector< EntitySerializeHelper > rootEntities, unrootEntities;
    retVal &= serializer.SerializeVector( "ROOT", rootEntities, EntitySerializeHelper( &serializeArgs, &registry ) );
    retVal &= serializer.SerializeVector( "UNROOT", unrootEntities, EntitySerializeHelper( &serializeArgs, &registry ) );

    // connect references
    assert( serializeArgs.UIDRemapper == nullptr );
    for( int i = 0; i < serializeArgs.LoadedReferences.size( ); i++ )
        (*serializeArgs.LoadedReferences[i].first) = Scene::EntityReference( serializeArgs.UIDRegistry, serializeArgs.LoadedReferences[i].second );

#ifdef _DEBUG
    // validate 'ListReferences' coverage
    std::unordered_set<Scene::EntityReference*> referenceSet;
    // go through all entities
    registry.each( [ & ]( entt::entity entity ) 
    { 
        std::vector<Scene::EntityReference*> referenceList;
        Scene::ListReferences( registry, entity, referenceList );
        for( Scene::EntityReference * ref : referenceList )
        {
            auto ret = referenceSet.emplace( ref );
            assert( ret.second );   // if this fires, 
        }
    } );
    // now we should have a list of all EntityReference-s used by our components, let's make sure the ones we just serialized are in
    for( std::pair<class EntityReference *, UID> & loadedRef : serializeArgs.LoadedReferences )
    {
        // this most likely means you forgot to add ListReferences to your component that uses EntityReference
        assert( referenceSet.find( loadedRef.first ) != referenceSet.end() );
    }
#endif
    
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
        if( nameComponent != nullptr && vaStringTools::CompareNoCase(*nameComponent, name)==0 )
        {
            found = entity;
            return;
        }
        if( recursive )
            found = FindFirstByName( registry, name, entity, true );
    } );
    return found;
}

void Scene::UIHighlight( entt::registry& registry, entt::entity entity )
{
    registry.set<Scene::UIHighlightRequest>( Scene::UIHighlightRequest{entity} );
}

