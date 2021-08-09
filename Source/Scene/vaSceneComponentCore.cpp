///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaSceneComponentCore.h"
#include "vaSceneComponents.h"
// #include "vaSceneComponentsIO.h"
// #include "vaSceneSystems.h"
// 
// #include "Rendering/vaRenderMesh.h"
// #include "Rendering/vaRenderMaterial.h"

#include "Core/System/vaFileTools.h"


using namespace Vanilla;

using namespace Vanilla::Scene;

int Components::TypeIndex( const string & name )
{
    return vaSceneComponentRegistry::GetInstance().FindComponentTypeIndex( name );
}

int Components::TypeCount( )
{
    return (int)vaSceneComponentRegistry::GetInstance().m_components.size();
}

int Components::TypeUseCount( int typeIndex, entt::registry & registry )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount( ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].TotalCountCallback(registry);
}

const string & Components::TypeName( int typeIndex )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount( ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].NameID;
}

string Components::DetailedTypeInfo( int typeIndex )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount( ) );
    auto & typeInfo = vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex];
    
    string outInfo;
    outInfo += "Component name:     " + typeInfo.NameID;                                                                        
    outInfo += "\n";
    outInfo += "C++ type name:      " + typeInfo.TypeName + ", type index: " + std::to_string( typeInfo.TypeIndex );
    outInfo += "\n";
    outInfo += "Visible in UI:      " + std::to_string( typeInfo.UIVisible );
    outInfo += "\n";
    outInfo += "Modifiable in UI:   " + std::to_string( typeInfo.UIAddRemoveResetDisabled );
    outInfo += "\n";
    outInfo += "Has serializer:     " + std::to_string( typeInfo.SerializerCallback.operator bool() );
    outInfo += "\n";
    outInfo += "Has UI handler:     " + std::to_string( typeInfo.UITickCallback.operator bool() );
    return outInfo;
}

bool Components::Has( int typeIndex, entt::registry & registry, entt::entity entity )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount() );
    return vaSceneComponentRegistry::GetInstance().m_components[typeIndex].HasCallback( registry, entity );
}

void Components::EmplaceOrReplace( int typeIndex, entt::registry & registry, entt::entity entity )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount( ) );
    vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].EmplaceOrReplaceCallback( registry, entity );
}

void Components::Remove( int typeIndex, entt::registry & registry, entt::entity entity )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount( ) );
    vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].RemoveCallback( registry, entity );
}

bool Components::HasSerialize( int typeIndex )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount( ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].SerializerCallback != nullptr;
}

bool Components::HasUITick( int typeIndex )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount( ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].UITickCallback != nullptr;
}

bool Components::HasUITypeInfo( int typeIndex )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount( ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].UITypeInfoCallback != nullptr;
}

bool Components::UIVisible( int typeIndex )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount( ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].UIVisible;
}

bool Components::UIAddRemoveResetDisabled( int typeIndex )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount( ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].UIAddRemoveResetDisabled;
}

bool Components::Serialize( int typeIndex, entt::registry & registry, entt::entity entity, class vaSerializer & serializer )
{
    assert( HasSerialize( typeIndex ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].SerializerCallback( registry, entity, serializer );
}

void Components::UITick( int typeIndex, entt::registry & registry, entt::entity entity, Scene::UIArgs & uiArgs )
{
    assert( HasUITick( typeIndex ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].UITickCallback( registry, entity, uiArgs );
}

const char * Components::UITypeInfo( int typeIndex )
{
    assert( HasUITypeInfo( typeIndex ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].UITypeInfoCallback( );
}

bool Components::HasValidate( int typeIndex )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount( ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].ValidateCallback != nullptr;
}

void Components::Validate( int typeIndex, entt::registry & registry, entt::entity entity )
{
    assert( HasValidate( typeIndex ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].ValidateCallback( registry, entity );
}

bool Components::HasUIDraw( int typeIndex )
{
    assert( typeIndex >= 0 && typeIndex < TypeCount( ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].UIDrawCallback != nullptr;
}

void Components::UIDraw( int typeIndex, entt::registry & registry, entt::entity entity, vaDebugCanvas2D & canvas2D, vaDebugCanvas3D & canvas3D )
{
    assert( HasUIDraw( typeIndex ) );
    return vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].UIDrawCallback( registry, entity, canvas2D, canvas3D );
}

void Components::Reset( int typeIndex, entt::registry& registry, entt::entity entity )
{
    if( vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].ResetCallback != nullptr )
        vaSceneComponentRegistry::GetInstance( ).m_components[typeIndex].ResetCallback( registry, entity );
    else
    {
        // "dumb" reset - replace component with default constructed
        EmplaceOrReplace( typeIndex, registry, entity );
    }
}

vaSceneComponentRegistry::~vaSceneComponentRegistry( )
{

}

AccessPermissions::AccessPermissions( )
{
}

void AccessPermissions::SetState( State newState )
{
    assert( vaThreading::IsMainThread() );

    assert( newState != m_state );

    if( newState == AccessPermissions::State::SerializedDelete )
        { assert( m_state == AccessPermissions::State::Serialized ); }
    if( newState == AccessPermissions::State::Serialized )
        { assert( m_state == AccessPermissions::State::SerializedDelete || m_state == AccessPermissions::State::Concurrent ); }
    if( newState == AccessPermissions::State::Concurrent )
        { assert( m_state == AccessPermissions::State::Serialized ); }

    if( newState == AccessPermissions::State::Concurrent || m_state == AccessPermissions::State::Concurrent )
    {
#ifdef _DEBUG
        for( int lock : m_locks )
            { assert( lock == 0 ); };
#endif
    }

    if( m_locks.size() != Components::TypeCount( ) )
        m_locks.resize( Components::TypeCount( ), 0 );

    m_state = newState;
}

bool AccessPermissions::TryAcquire( std::vector<int> & readWriteComponents, std::vector<int> & readComponents )
{
    for( int i = 0; i < readWriteComponents.size( ); i++ )
    {
        int typeIndex = readWriteComponents[i];
        if( m_locks[typeIndex] != 0 )
        {
            if( m_locks[typeIndex] == -1 )
                VA_ERROR( "  Can't read-write lock component '%s' because it's already locked for read-write", Scene::Components::TypeName( typeIndex ).c_str() );
            else
                VA_ERROR( "  Can't read-write lock component '%s' because it's already locked for read", Scene::Components::TypeName( typeIndex ).c_str() );
            
            // unroll read-write
            for( int j = i-1; j >= 0; j-- )
                m_locks[readComponents[j]] = 0;

            return false;
        }
        m_locks[typeIndex] = -1;
    }

    for( int i = 0; i < readComponents.size( ); i++ )
    {
        int typeIndex = readComponents[i];
        if( m_locks[typeIndex] == -1 )
        {
            VA_ERROR( "  Can't read-only lock component '%s' because it's already locked for read-write", Scene::Components::TypeName( typeIndex ).c_str( ) );

            // unroll read-only
            for( int j = i - 1; j >= 0; j-- )
                m_locks[readComponents[j]]--;
            // unroll read-write
            for( int j = 0; j < readWriteComponents.size( ); j++ )
                m_locks[readWriteComponents[j]] = 0;

            return false;
        }
        m_locks[typeIndex]++;
    }
    return true;
}

void AccessPermissions::Release( std::vector<int> & readWriteComponents, std::vector<int>& readComponents )
{
    for( int i = 0; i < readWriteComponents.size( ); i++ )
    {
        assert( m_locks[readWriteComponents[i]] == -1 );
        m_locks[readWriteComponents[i]] = 0;
    }

    for( int i = 0; i < readComponents.size( ); i++ )
    {
        assert( m_locks[readComponents[i]] > 0 );
        m_locks[readComponents[i]]--;
    }
}


vaSceneComponentRegistry::vaSceneComponentRegistry( )
{
    // No need to register components if they don't need to get serialized, visible in the UI, accessed dynamically or etc.

    //int a = Components::RuntimeID<Relationship>( );

    RegisterComponent< Name >( "Name" );            // <- just an example on how you can use a custom name - perhaps you want to shorten the name to something more readable or even substitute a component for another
    RegisterComponent< Relationship >( );
    RegisterComponent< TransformLocalIsWorldTag >( );
    RegisterComponent< TransformDirtyTag >( );
    RegisterComponent< TransformLocal >( );
    RegisterComponent< TransformWorld >( );
    RegisterComponent< WorldBounds >( );
    RegisterComponent< RenderMesh >( );
    RegisterComponent< LocalIBLProbe >( );
    RegisterComponent< DistantIBLProbe >( );
    RegisterComponent< FogSphere >( );
    RegisterComponent< CustomBoundingBox >( );
    RegisterComponent< WorldBoundsDirtyTag >( );
    RegisterComponent< LightAmbient >( );
    //RegisterComponent< LightDirectional >( );
    RegisterComponent< LightPoint >( );
    RegisterComponent< MaterialPicksLightEmissive >( );
    RegisterComponent< SkyboxTexture >( );
    RegisterComponent< IgnoreByIBLTag >( );
    RegisterComponent< RenderCamera >( );
}

