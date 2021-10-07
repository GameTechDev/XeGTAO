///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaScene.h"

#include "Core/vaApplicationBase.h"
#include "Core/vaInput.h"

#include "Rendering/vaDebugCanvas.h"

#include "Rendering/vaRenderDevice.h"
#include "Rendering/vaTexture.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"
#include "Rendering/vaSceneLighting.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Core/System/vaFileTools.h"

#include "vaSceneComponentsUI.h"

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
#include "IntegratedExternals/vaTaskflowIntegration.h"
#endif

namespace
{
    struct AllowScope
    {
        AllowScope( bool & variable ) : m_variable( variable )
        { 
            assert( !m_variable ); m_variable = true;
        }
        ~AllowScope() 
        { 
            assert( m_variable ); m_variable = false;
        }
    private:
        bool & m_variable;
    };
#ifdef _DEBUG
    #define ALLOW_SCOPE( ALLOW_VAR, COMMANDS )    { AllowScope VA_COMBINE(ALLOW_VAR, __LINE__)(ALLOW_VAR); COMMANDS; }
#else
    #define ALLOW_SCOPE( ALLOW_VAR, COMMANDS )    { COMMANDS; }
#endif
}

namespace Vanilla
{
    struct TransformsUpdateWorkNode : vaSceneAsync::WorkNode
    {
        vaScene &               Scene;

        TransformsUpdateWorkNode( const string & name, vaScene & scene, const std::vector<string> & predecessors, const std::vector<string> & successors ) : Scene( scene ),
            vaSceneAsync::WorkNode( name, predecessors, successors, Scene::AccessPermissions::ExportPairLists<
                const Scene::TransformLocal, Scene::TransformWorld, const Scene::Relationship, const Scene::WorldBounds, const Scene::TransformLocalIsWorldTag >() )
        { 
        }

        //virtual void                    ExecutePrologue( float deltaTime, int64 applicationTickIndex ) override     { deltaTime; applicationTickIndex; }
        //
        // Asynchronous narrow processing; called after ExecuteWide, returned std::pair<uint, uint> will be used to immediately repeat ExecuteWide if non-zero
        virtual std::pair<uint, uint>   ExecuteNarrow( const uint32 pass, vaSceneAsync::ConcurrencyContext & ) override
        {
            if( pass == 0 )
            {   // STEP 0, we're starting to consume all dirty transforms and prepare & fill in the per-hierarchy level dirty flags
                if( !Scene.m_listDirtyTransforms.IsConsuming( ) )
                    Scene.m_listDirtyTransforms.StartConsuming( );

                for( uint32 depth = 0; depth < Scene::Relationship::c_MaxDepthLevels; depth++ )
                    Scene.m_listHierarchyDirtyTransforms[depth].StartAppending( );

                return { (uint32)Scene.m_listDirtyTransforms.Count(), VA_GOOD_PARALLEL_FOR_CHUNK_SIZE * 4 };
            }
            else
            {
                int depth = pass-1;  // (we used pass 0 for updates and switching containers to 'consume' state)

                // that's it, we're done
                if( depth == Scene::Relationship::c_MaxDepthLevels )
                    return {0,0};

                assert( depth >= 0 && depth < Scene::Relationship::c_MaxDepthLevels );

                // Switch hierarchy transform dirty tag containers into 'readable'
                Scene.m_listHierarchyDirtyTransforms[depth].StartConsuming( );

                // set up wide parameters
                assert( depth >= 0 && depth < Scene::Relationship::c_MaxDepthLevels );
                return { (uint32)Scene.m_listHierarchyDirtyTransforms[depth].Count( ), VA_GOOD_PARALLEL_FOR_CHUNK_SIZE * 2 };
            }
        }
        //
        // Asynchronous wide processing; items run in chunks to minimize various overheads
        virtual void                    ExecuteWide( const uint32 pass, const uint32 itemBegin, const uint32 itemEnd, vaSceneAsync::ConcurrencyContext & ) override
        {
            if( pass == 0 )
            {   
                // continue from Narrow pass 0, we categorizes transform-dirty entities to hierarchy-depth-based buckets
                for( uint32 index = itemBegin; index < itemEnd; index++ )
                {
                    const auto& cregistry = Scene.CRegistry();
                    entt::entity entity = Scene.m_listDirtyTransforms[index];
                    const Scene::Relationship* relationship = cregistry.try_get<Scene::Relationship>( entity );
                    uint32 depth = ( relationship != nullptr ) ? ( relationship->Depth ) : ( 0 );
                    assert( depth < Scene::Relationship::c_MaxDepthLevels );
                    Scene.m_listHierarchyDirtyTransforms[depth].Append( entity );
                }
            }
            else
            {
                // continue from Narrow pass 1+, update transforms in layers
                int depth = pass-1;  // (we used pass 0 for updates and switching containers to 'consume' state)
                assert( depth >= 0 && depth < Scene::Relationship::c_MaxDepthLevels );

                auto & dirtyTransforms = Scene.m_listHierarchyDirtyTransforms[depth];

                for( uint32 i = itemBegin; i < itemEnd; i++ )
                    Scene::UpdateTransforms( Scene.Registry(), dirtyTransforms[i], Scene.m_listDirtyBounds );
            }
        }
        //
        // Wraps up things (if needed); called from main thread ( assert(vaThreading::IsMainThread( )) )
        // virtual void                    ExecuteEpilogue( ) override { }

    };

    struct DirtyBoundsUpdateWorkNode : vaSceneAsync::WorkNode
    {
        vaScene &               Scene;

        DirtyBoundsUpdateWorkNode( const string & name, vaScene & scene, const std::vector<string> & predecessors, const std::vector<string> & successors ) : Scene( scene ),
            vaSceneAsync::WorkNode( name, predecessors, successors, Scene::AccessPermissions::ExportPairLists<
                Scene::WorldBounds, Scene::WorldBoundsDirtyTag, const Scene::TransformWorld, const Scene::CustomBoundingBox, const Scene::RenderMesh >() )
        { 
        }

        // Asynchronous narrow processing; called after ExecuteWide, returned std::pair<uint, uint> will be used to immediately repeat ExecuteWide if non-zero
        virtual std::pair<uint, uint>   ExecuteNarrow( const uint32 pass, vaSceneAsync::ConcurrencyContext & ) override
        {
            if( pass == 0 )
            {   
                // prepare list states
                Scene.m_listDirtyBounds.StartConsuming( );
                Scene.m_listDirtyBoundsUpdatesFailed.StartAppending( );
                // return wide item counts
                return { (uint32)Scene.m_listDirtyBounds.Count(), VA_GOOD_PARALLEL_FOR_CHUNK_SIZE }; 
            }
            else
            {
                assert( pass == 1 );
                // this is exception handling (not sure why I even needed to bother...............)
                Scene.m_listDirtyBoundsUpdatesFailed.StartConsuming( );
                const int count = (int)Scene.m_listDirtyBoundsUpdatesFailed.Count( );
                for( int i = 0; i < count; i++ )
                    Scene.Registry().emplace<Scene::WorldBoundsDirtyTag>( Scene.m_listDirtyBoundsUpdatesFailed[i] ); // <- is this safe use of the entt registry?
                return {0,0};
            }
        }
        //
        // Asynchronous wide processing; items run in chunks to minimize various overheads
        virtual void                    ExecuteWide( const uint32 pass, const uint32 itemBegin, const uint32 itemEnd, vaSceneAsync::ConcurrencyContext & ) override
        {
            if( pass == 0 )
            {   
                for( uint32 index = itemBegin; index < itemEnd; index++ )
                {
                    auto entity = Scene.m_listDirtyBounds[index];
                    if( !Scene.Registry().get<Scene::WorldBounds>( entity ).Update( Scene.Registry(), entity ) )
                        Scene.m_listDirtyBoundsUpdatesFailed.Append( entity );
                }
            }
            else
            { assert ( false );}
        }
    };

    struct EmissiveMaterialDriverUpdateWorkNode : vaSceneAsync::WorkNode
    {
        vaScene &               Scene;
        entt::basic_view< entt::entity, entt::exclude_t<>, const Scene::EmissiveMaterialDriver>
                                View;

        EmissiveMaterialDriverUpdateWorkNode( const string & name, vaScene & scene, const std::vector<string> & predecessors, const std::vector<string> & successors ) 
            : Scene( scene ), View( scene.Registry().view<std::add_const_t<Scene::EmissiveMaterialDriver>>( ) ),
            vaSceneAsync::WorkNode( name, predecessors, successors, Scene::AccessPermissions::ExportPairLists<
                const Scene::TransformWorld, const Scene::LightPoint, Scene::EmissiveMaterialDriver >() )
        { 
        }

        // Asynchronous narrow processing; called after ExecuteWide, returned std::pair<uint, uint> will be used to immediately repeat ExecuteWide if non-zero
        virtual std::pair<uint, uint>   ExecuteNarrow( const uint32 pass, vaSceneAsync::ConcurrencyContext & ) override
        {
            if( pass == 0 )
            {   // first pass: start
                return std::make_pair( (uint32)View.size(), VA_GOOD_PARALLEL_FOR_CHUNK_SIZE * 4 );    // <- is this safe use of the entt View?
            }
            else return {0,0};
        }
        //
        // Asynchronous wide processing; items run in chunks to minimize various overheads
        virtual void                    ExecuteWide( const uint32 pass, const uint32 itemBegin, const uint32 itemEnd, vaSceneAsync::ConcurrencyContext & ) override
        {
            assert( pass == 0 ); pass;
            for( uint32 index = itemBegin; index < itemEnd; index++ )
            {
                entt::entity entity = View[index];                                          // <- is this safe use of the entt View?
                assert( Scene.Registry().valid( entity ) );                                 // if this fires, you've corrupted the data somehow - possibly destroying elements outside of DestroyTag path?
                Scene::EmissiveMaterialDriver & emissiveDriver = Scene.Registry().get<Scene::EmissiveMaterialDriver>( entity );
                if( emissiveDriver.ReferenceLightEntity != entt::null )
                {
                    const Scene::LightPoint * referenceLight = Scene.CRegistry().try_get<Scene::LightPoint>( emissiveDriver.ReferenceLightEntity );
                    if( referenceLight == nullptr )
                    {
                        // not sure how to usefully warn...
                        VA_WARN( "EmissiveMaterialDriver has a non-entt::null ReferenceLightEntity but it contains no LightPoint component" );
                    }
                    else
                    {
                        emissiveDriver.EmissiveMultiplier = referenceLight->Color * (referenceLight->Intensity * referenceLight->FadeFactor * emissiveDriver.ReferenceLightMultiplier);
                    }
                }
            }
        }
    };
}

using namespace Vanilla;

int vaScene::s_instanceCount = 0;

static std::unordered_map<uint64, vaScene*>         s_sceneInstances;

vaScene * vaScene::FindByRuntimeID( uint64 runtimeID )
{
    auto it = s_sceneInstances.find( runtimeID );
    if( it == s_sceneInstances.end() )
    {
        assert( false );
        return nullptr;
    }
    return it->second;
}

vaScene::vaScene( const string & name, const vaGUID UID )
    : vaUIPanel( "Scene", 1, !VA_MINIMAL_UI_BOOL, vaUIPanel::DockLocation::DockedLeft, "Scenes" ),
    m_async( *this )
{
    m_registry.set<Scene::UID>( UID );
    m_registry.set<Scene::Name>( name ); 
    m_registry.set<Scene::BeingDestroyed>( Scene::BeingDestroyed{entt::null} );

    // if this fires, some other systems (like identifying meshes being rendered) will not work; while this can be fixed with a simple upgrade
    // a good question is, why was vaScene created this many times at runtime? it might be a bug!
    assert( RuntimeIDGet() < 0xFFFFFFFF );

    // if this fires, you probably need to update vaRenderInstanceList::SceneEntityIDNull :)
    static_assert( DrawOriginInfo::NullSceneEntityID == to_integral(entt::entity(entt::null)) );

    s_instanceCount++;
    if( vaSceneComponentRegistry::GetInstancePtr() == nullptr )
    {
        assert( s_instanceCount == 1 );
        new vaSceneComponentRegistry();
    }

    m_registry.set<Scene::UIDRegistry>( m_registry );

    //m_registry.set<Scene::RuntimeIDContext>( m_registry );
    m_registry.set<Scene::AccessPermissions>();
    //m_registry.ctx<Scene::AccessPermissions>()

    // adding custom callback on component change
    //m_registry.on_construct<Scene::TransformLocal>().connect< &Scene::InitTransformLocalToIdentity >( );
    // m_registry.on_update<Scene::TransformLocal>().connect<&vaScene::OnTransformLocalChanged>( this );

    // automatic dirty flag on TransformLocalIsWorldTag - note, this could be source of performance issue
    m_registry.on_construct<Scene::TransformLocalIsWorldTag>().connect<&Scene::SetTransformDirtyRecursiveSafe>();
    m_registry.on_destroy<Scene::TransformLocalIsWorldTag>().connect<&Scene::SetTransformDirtyRecursiveSafe>();

    // automatic dirty flag on TransformLocal change - note, this isn't used because it can be too slow sometimes
    // m_registry.on_construct<Scene::TransformLocal>( ).connect<&Scene::SetTransformDirtyRecursiveSafe>( );
    // m_registry.on_destroy<Scene::TransformLocal>( ).connect<&Scene::SetTransformDirtyRecursiveSafe>( );

    // m_transformWorldObserver.connect( m_registry, entt::collector.update<Scene::TransformWorld>() );

    m_registry.on_destroy<Scene::Relationship>( ).connect<&vaScene::OnRelationshipDestroy>( this );
    m_registry.on_update<Scene::Relationship>( ).connect<&vaScene::OnDisallowedOperation>( this );
    m_registry.on_construct<Scene::Relationship>( ).connect<&vaScene::OnRelationshipEmplace>( this );

    // automatic assignment of BoundsDirtyTag for some cases
    m_registry.on_construct<Scene::CustomBoundingBox>( ).connect< &Scene::AutoEmplaceDestroy<Scene::WorldBounds> >( );
    m_registry.on_destroy<Scene::CustomBoundingBox>( ).connect< &Scene::AutoEmplaceDestroy<Scene::WorldBounds> >( );
    m_registry.on_construct<Scene::RenderMesh>( ).connect< &Scene::AutoEmplaceDestroy<Scene::WorldBounds> >( );
    m_registry.on_destroy<Scene::RenderMesh>( ).connect< &Scene::AutoEmplaceDestroy<Scene::WorldBounds> >( );
    
#ifdef _DEBUG
    m_registry.on_construct<Scene::TransformDirtyTag>( ).connect<&vaScene::OnTransformDirtyFlagEmplace>( this );
#endif

    // groups, views, custom lists
    auto groupTransforms = m_registry.group<Scene::TransformLocal, Scene::TransformWorld>( );
    //////////////////////////////////////////////////////////////////////////

    //m_asyncMarkers.push_back( std::make_shared<vaSceneAsync::WorkNode>( "dirtylists", {}, {}, Scene::AccessPermissions::ExportPairLists<Scene::WorldBoundsDirtyTag> ) );
    m_asyncWorkNodes.push_back( vaSceneAsync::MarkerWorkNodeMakeShared( "dirtylists_done_marker", {}, {} ) );
    m_asyncWorkNodes.push_back( vaSceneAsync::MarkerWorkNodeMakeShared( "motion_done_marker", {"dirtylists_done_marker"}, { } ) );
    m_asyncWorkNodes.push_back( vaSceneAsync::MarkerWorkNodeMakeShared( "transforms_done_marker", {"motion_done_marker"}, { } ) );
    m_asyncWorkNodes.push_back( vaSceneAsync::MarkerWorkNodeMakeShared( "bounds_done_marker", {"transforms_done_marker"}, { } ) );
    m_asyncWorkNodes.push_back( vaSceneAsync::MarkerWorkNodeMakeShared( "renderlists_done_marker", {"bounds_done_marker"}, { } ) );

    m_asyncWorkNodes.push_back( vaSceneAsync::MoveTagsToListWorkNodeMakeShared<Scene::WorldBoundsDirtyTag>( "WorldBoundsDirtyTag", *this, m_listDirtyBounds, { }, { "dirtylists_done_marker" } ) );
    m_asyncWorkNodes.push_back( vaSceneAsync::MoveTagsToListWorkNodeMakeShared<Scene::TransformDirtyTag>( "TransformDirtyTag", *this, m_listDirtyTransforms, { }, { "dirtylists_done_marker" } ) );



    typedef std::vector<std::string> strvec;

    m_asyncWorkNodes.push_back( std::make_shared<TransformsUpdateWorkNode>( "TransformsUpdate", *this, strvec{ "motion_done_marker" }, strvec{ "transforms_done_marker" } ) );

    m_asyncWorkNodes.push_back( std::make_shared<DirtyBoundsUpdateWorkNode>( "DirtyBoundsUpdate", *this, strvec{ "transforms_done_marker" }, strvec{ "bounds_done_marker" } ) );

    // no reason why this can't run in parallel with 'DirtyBoundsUpdate'?
    m_asyncWorkNodes.push_back( std::make_shared<EmissiveMaterialDriverUpdateWorkNode>( "EmissiveMaterialDriverUpdate", *this, strvec{ "transforms_done_marker" }, strvec{ "bounds_done_marker" } ) );

    for( auto & workNodes : m_asyncWorkNodes )
        m_async.AddWorkNode( workNodes );


    s_sceneInstances.insert( {RuntimeIDGet( ), this} );
}

void vaScene::ClearAll( )
{
    // A bit of a roundabout way of removing all entities, but it's safe with regards to how DestroyTag is used to avoid invalid component creation during reactive callbacks.
    // If this turns out to be too slow, use below codepath BUT you must solve the above problem, possibly by setting a global Scene::DestroyTag .ctx and make sure all
    // reactive parts honor it.
    m_registry.each( [ & ]( entt::entity entity ) 
    { 
        m_registry.emplace_or_replace<Scene::DestroyTag>( entity ); 
    } );
    Scene::DestroyTagged( m_registry );

    for( int i = 0; i < Scene::Components::TypeCount(); i++ )
    {
        // if this fires, there was a bug somewhere where in reactive system a component was added to the entity being deleted; see usage of Scene::IsBeingDestroyed on how to avoid this
        assert( Scene::Components::TypeUseCount( i, m_registry ) == 0 );
    }

    // assert( ( m_registry.try_ctx<Scene::AccessPermissions>( ) != nullptr ) && !m_registry.try_ctx<Scene::AccessPermissions>( )->CanDestroyEntity( ) );
    // m_registry.ctx<Scene::AccessPermissions>( ).SetState( Scene::AccessPermissions::State::SerializedDelete );
    // 
    // m_registry.clear( );
    // 
    // m_registry.ctx<Scene::AccessPermissions>( ).SetState( Scene::AccessPermissions::State::Serialized );
}

vaScene::~vaScene( )
{
    s_sceneInstances.erase( RuntimeIDGet() );
    if( s_sceneInstances.size() == 0 )
        std::swap( s_sceneInstances, std::unordered_map<uint64, vaScene*>() );

    if( IsTicking( ) )
        TickEnd( );

    m_registry.on_destroy<Scene::TransformLocalIsWorldTag>( ).disconnect<&Scene::SetTransformDirtyRecursiveSafe>( );
    m_registry.on_destroy<Scene::TransformLocalIsWorldTag>( ).disconnect<&Scene::SetTransformDirtyRecursiveSafe>( );

    m_registry.on_destroy<Scene::CustomBoundingBox>( ).disconnect< &Scene::AutoEmplaceDestroy<Scene::WorldBounds> >( );
    m_registry.on_destroy<Scene::RenderMesh>().disconnect< &Scene::AutoEmplaceDestroy<Scene::WorldBounds> >();
   
    // remove all entities
    ClearAll();

    // m_registry.unset<Scene::RuntimeIDContext>( );

    s_instanceCount--;
    if( s_instanceCount == 0 )
        delete vaSceneComponentRegistry::GetInstancePtr();

    m_registry.unset<Scene::UIDRegistry>( );
}

// the only purpose of these for now is learning exactly how these callbacks work - these will likely be removed
void vaScene::OnDestroyTagged( entt::registry & registry, entt::entity entity )
{
    assert( vaThreading::IsMainThread() && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );

    assert( &registry == &m_registry ); registry; entity;
    // m_toDestroyList.push_back( entity );
}

// the only purpose of these for now is learning exactly how these callbacks work - these will likely be removed
void vaScene::OnDestroyUntagged( entt::registry & registry, entt::entity entity )
{
    assert( vaThreading::IsMainThread() && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );

    assert( &registry == &m_registry ); registry; entity;
    // auto it = std::find( m_toDestroyList.begin(), m_toDestroyList.end(), entity );
    // assert( it != m_toDestroyList.end() );
    // if( it != m_toDestroyList.end() )
    //     m_toDestroyList.erase( it );
}

// the only purpose of these for now is learning exactly how these callbacks work - these will likely be removed
void vaScene::OnDestroyTagChanged( entt::registry & registry, entt::entity entity )
{
    assert( vaThreading::IsMainThread() && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );

    assert( &registry == &m_registry ); registry; entity;
    // assert( &registry == &m_registry );
    //assert( false );
}

entt::entity vaScene::CreateEntity( const string & name, const vaMatrix4x4 & localTransform, entt::entity parent, const vaGUID & renderMeshID, const vaGUID & renderMaterialID )
{
    auto & ap = m_registry.ctx<Scene::AccessPermissions>(); ap;
    assert( vaThreading::IsMainThread() && ap.GetState( ) != Scene::AccessPermissions::State::Concurrent );

    auto entity = m_registry.create();

    // Names are not unique and don't have to be part of an entity
    if( name.length() > 0 )
        m_registry.emplace<Scene::Name>( entity, name );

    // all "standard" entities have this tracker that is very similar to 'entity' itself - unique ID - but managed outside of registry for.. "reasons"
    // m_registry.emplace<Scene::RuntimeID>( entity );

    // all "standard" entities have transforms and bounds
    m_registry.emplace<Scene::TransformLocal>( entity, localTransform );
    m_registry.emplace<Scene::TransformWorld>( entity, vaMatrix4x4::Identity );
    
    // all "standard" entities have Relationship struct (not always required but then no relationships can ever be established)
    // and it can only be created from here.
    m_registry.emplace<Scene::Relationship>( entity );

    if( renderMeshID != vaGUID::Null )
    {
        m_registry.emplace<Scene::RenderMesh>( entity, renderMeshID, renderMaterialID );
    }

    if( parent != entt::null )
    {
        SetParent( entity, parent );
    }

    SetTransformDirtyRecursive( entity );

    return entity;
}

void vaScene::DestroyEntity( entt::entity entity, bool recursive ) 
{ 
    m_registry.emplace_or_replace<Scene::DestroyTag>( entity ); 

    if( recursive )
        VisitChildren( entity, [&]( entt::entity child ) { DestroyEntity( child, true ); } );
}

void vaScene::SetTransformDirtyRecursive( entt::entity entity )
{
    assert( vaThreading::IsMainThread( ) && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );

    Scene::SetTransformDirtyRecursive( m_registry, entity );
}

void vaScene::UnparentChildren( entt::entity parent )
{
    assert( vaThreading::IsMainThread( ) && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );

    Scene::DisconnectChildren( m_registry, parent );
}

void vaScene::OnRelationshipDestroy( entt::registry & registry, entt::entity entity )
{
    assert( vaThreading::IsMainThread( ) && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );

    assert( &registry == &m_registry ); registry;
    Scene::DisconnectRelationship( registry, entity );
}

void vaScene::SetParent( entt::entity child, entt::entity parent )
{
    assert( vaThreading::IsMainThread( ) && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );

    Scene::SetParent( m_registry, child, parent );
}

void vaScene::UIHighlight( entt::entity entity )
{
    assert( vaThreading::IsMainThread() && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );

    if( m_registry.valid( entity ) )
    {
        m_uiHighlight               = entity;
        m_uiHighlightRemainingTime  = 4.0f;
        this->UIPanelSetFocusNextFrame();

        Scene::VisitParents( m_registry, entity, [&registry=m_registry] ( entt::entity parent ) 
        { registry.emplace_or_replace<Scene::UIEntityTreeOpenedTag>( parent ); } );
    }
}

void vaScene::UIOpenProperties( entt::entity entity, int preferredPropPanel )
{
    assert( vaThreading::IsMainThread( ) && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );

    assert( m_registry.valid(entity) );
    if( !m_registry.any_of<Scene::UIEntityPropertiesPanel>( entity ) )
        m_registry.emplace<Scene::UIEntityPropertiesPanel>( entity, std::make_shared<Scene::vaEntityPropertiesPanel>( *this, entity ) );

    vaUIManager::GetInstance().SelectPropertyItem( m_registry.get<Scene::UIEntityPropertiesPanel>( entity ).Value, preferredPropPanel );
}

// void vaScene::UICloseProperties( entt::entity entity )
// {
// }

bool vaScene::SaveJSON( const string & filePath )
{
    Scene::DestroyTagged( m_registry ); // <- don't save any of the about to be destroyed ones
    bool ret = Scene::SaveJSON( m_registry, filePath );
    if( ret )
        m_storagePath = filePath;
    return ret;
}

bool vaScene::LoadJSON( const string & filePath )
{
    ClearAll();
    bool ret = Scene::LoadJSON( m_registry, filePath );
    if( ret )
        m_storagePath = filePath;
    return ret;
}

void vaScene::UIPanelTick( vaApplicationBase& application )
{
    assert( vaThreading::IsMainThread( ) && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );

    bool ctrlKeyIsDown = (application.GetInputKeyboard()!=nullptr)?(application.GetInputKeyboard()->IsKeyDown( KK_CONTROL )):(false);
    ctrlKeyIsDown;
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    auto textColorDisabled = ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled );

    Scene::UIHighlightRequest * nextHighlight = m_registry.try_ctx<Scene::UIHighlightRequest>( );
    if( nextHighlight != nullptr && nextHighlight->Entity != entt::null )
    {
        UIHighlight( nextHighlight->Entity );
        m_registry.unset<Scene::UIHighlightRequest>( );
    }

    ImGui::PushItemWidth( 200.0f );

    if( ImGui::Button( " Rename " ) )
        ImGuiEx_PopupInputStringBegin( "Rename scene", Name() );
    string newName;
    if( ImGuiEx_PopupInputStringTick( "Rename scene", newName ) )
    {
        m_registry.ctx_or_set<Scene::Name>().assign( newName );
        //m_name = vaStringTools::ToLower( m_name );
        VA_LOG( "Scene name changed to '%s'", newName.c_str( ) );
    }

    ImGui::SameLine( );

    if( ImGui::Button( " Clear all " ) )
    {
        assert( false ); //Clear( );
        return;
    }

    ImGui::SameLine( );

    if( ImGui::Button( " Save As... " ) )
    {
        string fileName = vaFileTools::SaveFileDialog( "", vaCore::GetExecutableDirectoryNarrow( ), ".vaScene scene files\0*.vaScene\0\0" );
        if( fileName != "" )
        {
            if( vaFileTools::SplitPathExt( fileName ) == "" ) // if no extension, add .xml
                fileName += ".vaScene";
            SaveJSON( fileName );
        }
    }

    ImGui::SameLine( );
    if( ImGui::Button( " Load... " ) )
    {
        string fileName = vaFileTools::OpenFileDialog( "", vaCore::GetExecutableDirectoryNarrow( ), ".vaScene scene files\0*.vaScene\0\0" );
        if( fileName != "" )
            LoadJSON( fileName );
    }

    if( ImGui::CollapsingHeader( "Systems", /*ImGuiTreeNodeFlags_DefaultOpen*/ 0 ) )
    {
        if( ImGui::Button( "Dump systems graph", {-1, 0} ) )
            m_async.ScheduleGraphDump( );
    }


    // if( ImGui::CollapsingHeader( "Globals", /*ImGuiTreeNodeFlags_DefaultOpen | */ImGuiTreeNodeFlags_Framed ) )
    // {
    //     if( ImGui::Button( "Distant IBL - import", { -1, 0 } ) )
    //     {
    //         string file = vaFileTools::OpenFileDialog( "", vaCore::GetExecutableDirectoryNarrow( ) );
    //         if( file != "" )
    //             DistantIBL( ).SetImportFilePath( file );
    //     }
    // }

    //if( ImGui::CollapsingHeader( "Entities", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        if( !m_uiEntitiesFilterByName && !m_uiEntitiesFilterByComponent )
            ImGui::PushStyleColor( ImGuiCol_Text, textColorDisabled );
        ImGui::InputText( "Filter", &m_uiEntitiesFilter, ImGuiInputTextFlags_AutoSelectAll );
        m_uiEntitiesFilter = vaStringTools::ToLower(m_uiEntitiesFilter);
        if( !m_uiEntitiesFilterByName && !m_uiEntitiesFilterByComponent )
            ImGui::PopStyleColor( );
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Filter entities by their Name and/or component, for ex. \"word1 word2 -word3\" means\nthe name has to include both word1 and word2 but not include word3." );

        ImGui::SameLine( std::max( 0.0f, ImGui::GetContentRegionAvail().x - m_uiEntitiesFilterCheckboxSize*2 - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().WindowPadding.x ) );
        ImGui::Checkbox( "N", &m_uiEntitiesFilterByName );
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Filter by Name" );        
        ImGui::SameLine( );
        ImGui::Checkbox( "C", &m_uiEntitiesFilterByComponent );
        m_uiEntitiesFilterCheckboxSize = ImGui::GetItemRectSize( ).x;
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Filter by Component" );
        m_uiEntitiesFilterByComponent = false;

        Scene::DragDropNodeData currentDragDrop    = { vaGUID::Null, entt::null };
        auto ddpld = ImGui::GetDragDropPayload();
        if( ddpld != nullptr && ddpld->IsDataType( Scene::DragDropNodeData::PayloadTypeName() ) )
        {
            assert( ddpld->DataSize == sizeof(Scene::DragDropNodeData) );
            if( UID() == reinterpret_cast<Scene::DragDropNodeData*>( ddpld->Data )->SceneUID )
                currentDragDrop = *reinterpret_cast<Scene::DragDropNodeData*>( ddpld->Data );
        }
        bool acceptedDragDrop                       = false;
        entt::entity acceptedDragDropTargetEntity   = entt::null;   // entt::null means ROOT!

        // ImGui::GetFrameHeightWithSpacing() * 7 + 30
        ImGui::BeginChild("scrolling", ImVec2(0, 0), true );//, ImGuiWindowFlags_HorizontalScrollbar);

        float mainPartWidth = ImGui::GetContentRegionAvail( ).x - ImGui::CalcTextSize( "0xFFFFFFFF" ).x - ImGui::GetStyle( ).ItemSpacing.x;
        ImGui::Columns( 2, "entitiescolumns", true ); // 4-ways, with border
        ImGui::SetColumnWidth( 0, mainPartWidth );

        m_uiHighlightRemainingTime = std::max( m_uiHighlightRemainingTime - application.GetLastDeltaTime( ), 0.0f );
        entt::entity uiFocus = m_registry.valid( m_uiHighlight )?(m_uiHighlight):(entt::null);
        float selCol = 1.0f + std::sinf( m_uiHighlightRemainingTime * 10 );

//        ImGui::Separator( );
//        ImGui::Text( "entity name" );   ImGui::NextColumn( );
//        ImGui::Text( "entity ID" );     ImGui::NextColumn( );
//        ImGui::Separator( );

        bool            rightClickContextMenu       = false;

        auto handleDragDrop = [&]( int depth, entt::entity entity )
        {
            if( currentDragDrop.Entity != entt::null && depth >= 0 )
            {
                if( Scene::CanSetParent( m_registry, currentDragDrop.Entity, entity ) )
                {
                    if( ImGui::BeginDragDropTarget( ) )
                    {
                        if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( Scene::DragDropNodeData::PayloadTypeName( ) ) )
                        {
                            assert( payload->DataSize == sizeof( Scene::DragDropNodeData ) );
                            assert( ( *reinterpret_cast<Scene::DragDropNodeData*>( payload->Data ) ) == currentDragDrop );
                            assert( reinterpret_cast<Scene::DragDropNodeData*>( payload->Data )->SceneUID == m_registry.ctx<Scene::UID>() );
                            acceptedDragDrop = true;
                            acceptedDragDropTargetEntity = entity;
                        }
                        ImGui::EndDragDropTarget( );
                    }
                }
            }
            else if( entity != entt::null && m_registry.any_of<Scene::Relationship>( entity ) )
            {
                if( ImGui::BeginDragDropSource( ImGuiDragDropFlags_None ) )
                {
                    Scene::DragDropNodeData ddsource{ UID(), entity };
                    ImGui::SetDragDropPayload( Scene::DragDropNodeData::PayloadTypeName( ), &ddsource, sizeof( ddsource ) );        // Set payload to carry the index of our item (could be anything)
                    ImGui::EndDragDropSource( );
                }
            }
        };

        auto handleRightClickMenu = [ & ]( int depth, entt::entity entity )
        {
            if( currentDragDrop.Entity != entt::null )  // if drag'n'dropping ignore this
                return;
            if( ImGui::IsItemClicked( 1 ) )
            {
                rightClickContextMenu       = true;
                m_uiEntityContextMenuEntity = entity;
                m_uiEntityContextMenuDepth  = depth;
            }
        };

        // depth 0 means ROOT, depth -1 means "non-hierarchical nodes"
        auto displayEntityUI = [&]( int depth, bool leaf, bool & opened, bool & selected, string textLeft, string textRight, entt::entity entity )
        {
            const int indent = 2;
            string prefix;
            prefix.insert( prefix.end( ), (int)( std::max(0,depth) * indent ), ' ' );
            prefix += ( leaf ) ? ( "-" ) : ( ( opened ) ? ( "-" ) : ( "+" ) );
            textLeft = prefix + " " + textLeft;

            bool highlight = m_uiHighlightRemainingTime > 0 && uiFocus == entity;

            {
                VA_GENERIC_RAII_SCOPE( if( highlight ) ImGui::PushStyleColor( ImGuiCol_Text, { selCol, selCol, selCol, 1.0f } ); , if( highlight ) ImGui::PopStyleColor( ); )
                string selectableName = textLeft + "###" + textLeft + Scene::GetIDString( m_registry, entity );
                if( ImGui::Selectable( (selectableName).c_str( ), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick ) )
                {
                    if( ImGui::IsMouseDoubleClicked( 0 ) )
                        opened = !opened;
                    selected = !selected;
                }
                if( highlight )
                    ImGui::SetScrollHereY( );
            }
            handleDragDrop( depth, entity );
            handleRightClickMenu( depth, entity );

            ImGui::NextColumn( );

            // align to right
            ImGui::SetCursorPosX( ImGui::GetCursorPosX( ) + ImGui::GetColumnWidth( ) - ImGui::CalcTextSize( textRight.c_str( ) ).x - ImGui::GetStyle( ).ItemSpacing.x * 2.0f );
            ImGui::Text( textRight.c_str( ) );
            ImGui::NextColumn( );
        };

        entt::entity removeAllSelectionsButThis = entt::null;

        auto entityShowFilter = [ & ]( entt::entity entity )
        {
            assert( !m_uiEntitiesFilterByComponent );
            //string filterString = vaStringTools::ToLower( m_uiEntitiesFilter );
            if( entity != m_uiHighlight && m_registry.any_of<Scene::Name>(entity) && m_uiEntitiesFilterByName )
                return vaStringTools::Filter( m_uiEntitiesFilter, vaStringTools::ToLower( m_registry.get<Scene::Name>(entity) ) );
            else
                return true;
        };

        // m_uiNumHierarchicalUnfiltered = 0;      // reset the UI counter (they actually visually lag by 1 frame but oh well)
        // m_uiNumNonHierarchicalUnfiltered = 0;   // reset the UI counter (they actually visually lag by 1 frame but oh well)

        std::function<void( entt::entity, int index, entt::entity parent )> filterEntityUIVisitor;
        filterEntityUIVisitor = [&]( entt::entity entity, int , entt::entity parent )
        {
            bool filteredOut = !entityShowFilter( entity );
            if( filteredOut )
                m_registry.emplace<Scene::UIEntityFilteredOutTag>( entity );
            
            bool nodeOpened = m_registry.any_of<Scene::UIEntityTreeOpenedTag>( entity );
            if( nodeOpened || filteredOut ) // <- this is going to be slow...
                VisitChildren( entity, filterEntityUIVisitor );

            // back-propagate that we're filtered but visible because have unfiltered children
            if( parent != entt::null && m_registry.any_of<Scene::UIEntityFilteredOutTag>(parent) )
            {
                Scene::UIEntityFilteredOutTag * filteredOutTag = m_registry.try_get<Scene::UIEntityFilteredOutTag>( entity );
                if( filteredOutTag == nullptr || filteredOutTag->UnfilteredChildren )
                    m_registry.emplace_or_replace<Scene::UIEntityFilteredOutTag>( parent, Scene::UIEntityFilteredOutTag{true} );
            }
        };


        std::function<void( entt::entity )> displayEntityUIVisitor;
        displayEntityUIVisitor = [ & ]( entt::entity entity )
        {
            Scene::UIEntityFilteredOutTag* filteredOutTag = m_registry.try_get<Scene::UIEntityFilteredOutTag>( entity );

            // filtered out and no unfiltered children 
            if( filteredOutTag != nullptr && !filteredOutTag->UnfilteredChildren )
                return;

            auto * relationshipInfo = m_registry.try_get<Scene::Relationship>( entity );

#ifdef _DEBUG
            if( relationshipInfo != nullptr && relationshipInfo->Parent != entt::null )
            {
                auto & parentInfo = m_registry.get<Scene::Relationship>( relationshipInfo->Parent );
                assert( parentInfo.Depth == relationshipInfo->Depth-1 );  parentInfo;
            }
#endif

            // UI part
            {
                bool nodeOpened = m_registry.any_of<Scene::UIEntityTreeOpenedTag>( entity );
                bool nodeOpenedBefore = nodeOpened;
                bool nodeSelected = m_registry.any_of<Scene::UIEntityTreeSelectedTag>( entity );
                bool nodeSelectedBefore = nodeSelected;

                if( filteredOutTag == nullptr )
                {
                    // "normal" codepath - not filtered out
                    displayEntityUI( 1+((relationshipInfo!=nullptr)?(relationshipInfo->Depth):(0)), (relationshipInfo==nullptr || relationshipInfo->ChildrenCount == 0), 
                                nodeOpened, nodeSelected, GetName( entity ), vaStringTools::Format( "%#010x", (int32)entity ), entity );
                }
                else
                {
                    assert( filteredOutTag->UnfilteredChildren );
                    // "filtered out" codepath but has visible children
                    ImGui::PushStyleColor( ImGuiCol_Text, textColorDisabled );
                    displayEntityUI( 1 + ( ( relationshipInfo != nullptr ) ? ( relationshipInfo->Depth ) : ( 0 ) ), ( relationshipInfo == nullptr || relationshipInfo->ChildrenCount == 0 ),
                        nodeOpened, nodeSelected, "<filtered-out>", vaStringTools::Format( "%#010x", (int32)entity ), entity );
                    ImGui::PopStyleColor( );
                    nodeSelected = false;
                }

                if( nodeSelected != nodeSelectedBefore )
                {
                    if( nodeSelected )
                    {
                        if( ctrlKeyIsDown )
                            m_registry.emplace<Scene::UIEntityTreeSelectedTag>( entity );
                        else
                            removeAllSelectionsButThis = entity;
                    }
                    else
                        m_registry.remove<Scene::UIEntityTreeSelectedTag>( entity );
                }
                if( nodeOpened != nodeOpenedBefore )
                {
                    if( nodeOpened )
                        m_registry.emplace<Scene::UIEntityTreeOpenedTag>( entity );
                    else
                        m_registry.remove<Scene::UIEntityTreeOpenedTag>( entity );
                }

                if( nodeOpened )
                    VisitChildren( entity, displayEntityUIVisitor );
            }
        };

        // Reset filtering
        m_registry.clear<Scene::UIEntityFilteredOutTag>( );

        // Show hierarchical entities (those with Scene::Relationship) under "ROOT" node
        int numHierarchyEntities = 0;
        {
            auto hierarchyEntities  = m_registry.view<const Scene::Relationship>( );
            numHierarchyEntities = (int)hierarchyEntities.size();
            bool rootSelected = false;
            ImGui::PushStyleColor( ImGuiCol_Text, textColorDisabled );
            displayEntityUI( 0, false, m_uiEntityTreeRootOpen, rootSelected, string("ROOT (") /*+ std::to_string(m_uiNumHierarchicalUnfiltered) + " out of "*/ + std::to_string(hierarchyEntities.size()) + ")", "", entt::null );
            ImGui::PopStyleColor();
            if( m_uiEntityTreeRootOpen )
                hierarchyEntities.each( [ & ]( entt::entity entity, const Scene::Relationship& relationship ) 
                    {
                        if( relationship.Depth == 0 )
                        {
                            filterEntityUIVisitor( entity, 0, entt::null );
                            displayEntityUIVisitor( entity );
                        }
                    }
            );
        }

        // Show non-hierarchical entities (those with Scene::Relationship) under "Non-hierarchical" node
        {
            auto unattachedEntities = m_registry.view<entt::exclude_t<const Scene::Relationship>>( );
            bool unattachedSelected = false;
            ImGui::PushStyleColor( ImGuiCol_Text, textColorDisabled );
            displayEntityUI( -1, false, m_uiEntityTreeUnRootOpen, unattachedSelected, string("Non-hierarchical (") /*+ std::to_string(m_uiNumNonHierarchicalUnfiltered) + " out of "*/ + std::to_string(unattachedEntities.size())+")", "", entt::null );
            ImGui::PopStyleColor( );
            if( m_uiEntityTreeUnRootOpen )
                unattachedEntities.each( [ & ]( entt::entity entity ) 
                    {
                        filterEntityUIVisitor( entity, 0, entt::null );
                        displayEntityUIVisitor( entity );
                    }
            );
        }

        // selection
        if( removeAllSelectionsButThis != entt::null )
        {
            m_registry.clear<Scene::UIEntityTreeSelectedTag>();
            m_registry.emplace<Scene::UIEntityTreeSelectedTag>( removeAllSelectionsButThis );
        }

        // drag and drop
        if( acceptedDragDrop )
        {
            assert( Scene::CanSetParent( m_registry, currentDragDrop.Entity, acceptedDragDropTargetEntity ) );
            SetParent( currentDragDrop.Entity, acceptedDragDropTargetEntity );
        }

        ImGui::Columns( 1 );

        if( rightClickContextMenu )
            ImGui::OpenPopup( "RightClickEntityContextMenu" );

        if( ImGui::BeginPopup( "RightClickEntityContextMenu" ) )
        {
            if( m_uiEntityContextMenuEntity != entt::null && !m_registry.valid( m_uiEntityContextMenuEntity ) )
                ImGui::CloseCurrentPopup( );
            else
            {
                // special case for ROOT (noot an actual entity)
                if( m_uiEntityContextMenuEntity == entt::null && m_uiEntityContextMenuDepth == 0 )
                {
                    ImGui::TextColored( ImVec4{ 1,1,0,1 }, "ROOT" );
                    ImGui::Separator( );
                    VA_GENERIC_RAII_SCOPE( ImGui::Indent( ); , ImGui::Unindent( ); );
                    if( ImGui::MenuItem( "Expand/collapse", NULL, false, true ) ) 
                    { 
                        ImGui::CloseCurrentPopup( );
                        if( m_uiEntityContextMenuEntity == entt::null )
                            m_uiEntityTreeRootOpen = !m_uiEntityTreeRootOpen;
                    }
                    ImGui::Separator( );
                    if( ImGui::MenuItem( "Create new", NULL, false, true ) ) 
                    { 
                        ImGui::CloseCurrentPopup( );
                        auto newEntity = CreateEntity( "New entity", vaMatrix4x4::Identity );
                        UIOpenRename( newEntity );
                    }
                }
                else
                {
                    bool popupExpandCollapse = false;
                    Scene::HandleRightClickContextMenuPopup( *this, m_uiEntityContextMenuEntity, true, false, [&]()
                    {
                        bool enableExpandCollapse = m_registry.any_of<Scene::Relationship>( m_uiEntityContextMenuEntity ) && m_registry.get<Scene::Relationship>( m_uiEntityContextMenuEntity ).ChildrenCount > 0;
                        bool enableUnparent = m_registry.any_of<Scene::Relationship>( m_uiEntityContextMenuEntity ) && m_registry.get<Scene::Relationship>( m_uiEntityContextMenuEntity ).Parent != entt::null;

                        if( ImGui::MenuItem( "Expand/collapse", NULL, false, enableExpandCollapse ) ) 
                        {
                            if( !m_registry.any_of<Scene::UIEntityTreeOpenedTag>( m_uiEntityContextMenuEntity ) )
                                m_registry.emplace<Scene::UIEntityTreeOpenedTag>( m_uiEntityContextMenuEntity );
                            else
                                m_registry.remove<Scene::UIEntityTreeOpenedTag>( m_uiEntityContextMenuEntity );
                            ImGui::CloseCurrentPopup( ); 
                            popupExpandCollapse = true; 
                        }
                        if( ImGui::MenuItem( "Create new child", NULL, false, true ) )
                        {
                            ImGui::CloseCurrentPopup( );
                            auto newEntity = CreateEntity( "New entity", vaMatrix4x4::Identity, m_uiEntityContextMenuEntity );
                            UIOpenRename( newEntity );
                        }
                        if( ImGui::BeginMenu( "Unparent", enableUnparent ) )
                        {
                            ImGui::TextDisabled( "Are you sure?" );
                            ImGui::Separator( );
                            if( ImGui::MenuItem( "Yes, unparent", NULL, false, true ) )
                            {
                                ImGui::CloseCurrentPopup( );
                                SetParent( m_uiEntityContextMenuEntity, entt::null );
                            }
                            if( ImGui::MenuItem( "No, cancel", NULL, false, true ) )
                                ImGui::CloseCurrentPopup( );
                            ImGui::EndMenu( );
                        }
                        ImGui::Separator( );

                    } );
                }
            }
            ImGui::EndPopup();
        }

        // if this entity got deleted from the popup context menu
        if( m_uiEntityContextMenuEntity != entt::null && (!m_registry.valid(m_uiEntityContextMenuEntity) || m_registry.any_of<Scene::DestroyTag>(m_uiEntityContextMenuEntity) ) )
        {
            m_uiEntityContextMenuEntity = entt::null;
            m_uiEntityContextMenuDepth = -1;
        }

        // if this got unparented from the popup context menu
        if( m_uiEntityContextMenuEntity != entt::null && m_registry.any_of<Scene::Relationship>(m_uiEntityContextMenuEntity) && m_registry.get<Scene::Relationship>(m_uiEntityContextMenuEntity).Parent == entt::null )
        {
            m_uiEntityContextMenuDepth = 1; // it's actually 'Scene::Relationship::Depth + 1' because this is a "visual" depth and root is 0 (don't ask...)
        }

        ImGui::EndChild();
    }

    ImGui::PopItemWidth( );
#endif
}

void vaScene::UIPanelTickAlways( vaApplicationBase & application )
{
    assert( vaThreading::IsMainThread( ) && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );

    application;

    // renaming
    {
        // if rename entity no longer valid or it has no Scene::Name component anymore, set it to null
        if( m_uiPopupRename != entt::null && ( !m_registry.valid( m_uiPopupRename ) || !m_registry.any_of<Scene::Name>( m_uiPopupRename ) ) )
            m_uiPopupRename = entt::null;

        const char * renameEntityPopup = "Rename entity";
        if( !ImGui::IsPopupOpen( renameEntityPopup ) && m_uiPopupRename != entt::null )
            ImGuiEx_PopupInputStringBegin( renameEntityPopup, m_registry.get<Scene::Name>( m_uiPopupRename ) );
        if( ImGui::IsPopupOpen( renameEntityPopup ) )
        {
            if( m_uiPopupRename == entt::null )
            {
                if( ImGui::BeginPopupModal( renameEntityPopup ) )
                {
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                else { assert(false); }
            }
            else
            {
                string newName;
                if( ImGuiEx_PopupInputStringTick( renameEntityPopup, newName ) )
                {
                    string oldName = m_registry.get<Scene::Name>( m_uiPopupRename );
                    m_registry.replace<Scene::Name>( m_uiPopupRename, newName );
                    VA_LOG( "Entity name changed from '%s' to '%s'", oldName.c_str( ), newName.c_str( ) );
                    m_uiPopupRename = entt::null;
                }
            }
        }
    }

    // 3D UI move tool
    {
        m_registry.view<const Scene::UIEntityTreeSelectedTag>( ).each( [ & ]( entt::entity entity )//, const Scene::UIMoveTool & moveTool )
        {
            Scene::TransformLocal * transformLocal = m_registry.try_get<Scene::TransformLocal>( entity );
            if( transformLocal == nullptr )
                return;
            
            string nameAndID = Scene::GetNameAndID( m_registry, entity );

            
            vaMatrix4x4 worldTransformParent = vaMatrix4x4::Identity;
            Scene::VisitParents( m_registry, entity, [&registry{m_registry}, &worldTransformParent](entt::entity parent)
            { 
                auto localTransform = registry.get<Scene::TransformLocal>( parent );
                worldTransformParent = localTransform * worldTransformParent;
            }, true );
            
            vaMatrix4x4 editableTransform =  static_cast<vaMatrix4x4&>(*transformLocal); 

            vaUIManager::GetInstance().MoveRotateScaleWidget( nameAndID, nameAndID, worldTransformParent, editableTransform, vaMRSWidgetFlags::None );
            if( editableTransform != static_cast<vaMatrix4x4&>(*transformLocal) )
            {
                *transformLocal = editableTransform;
                SetTransformDirtyRecursive( entity );
            }
        } );
    }

    // Draw 2D/3D UI of selected
    {
        vaDebugCanvas2D & canvas2D = application.GetCanvas2D( );
        vaDebugCanvas3D & canvas3D = application.GetCanvas3D( );

        m_registry.view<Scene::UIEntityTreeSelectedTag>( ).each( [ & ]( entt::entity entity )
        {
            for( int typeIndex = 0; typeIndex < Scene::Components::TypeCount( ); typeIndex++ )
            {
                // if the entity has this component type, and this component type supports UIDraw - draw it!
                if( Scene::Components::Has(typeIndex, m_registry, entity ) && Scene::Components::HasUIDraw(typeIndex) )
                    Scene::Components::UIDraw( typeIndex, m_registry, entity, canvas2D, canvas3D );
            }
        } );

        //canvas3D.DrawCylinder( {0,2,0}, {10,2,0}, 0.5f, 0.5f, 0x10000000, 0x80FF00FF );
        //canvas3D.DrawArrow( {0,0,0}, {10,0,0}, 0.5f, 0x10000000, 0x80FFFFFF, 0x8000FFFF );
    }


}

void vaScene::TickBegin( float deltaTime, int64 applicationTickIndex )
{
    VA_TRACE_CPU_SCOPE( SceneTick );

    m_time += deltaTime;

    assert( vaThreading::IsMainThread( ) && m_registry.ctx<Scene::AccessPermissions>().GetState( ) != Scene::AccessPermissions::State::Concurrent );

    // TickBegin/TickEnd mismatch?
    assert( deltaTime >= 0.0f );
    assert( m_currentTickDeltaTime == -1.0f );
    m_currentTickDeltaTime = std::max( 0.0f, deltaTime );
    assert( applicationTickIndex > m_lastApplicationTickIndex );
    m_lastApplicationTickIndex = applicationTickIndex;

    {
        VA_TRACE_CPU_SCOPE( DestroyTagged );
        // Delete all entities tagged with DestroyTag - THIS IS THE ONLY PLACE WHERE ENTITIES CAN GET DESTROYED (other than in the destructor and before saving.. and maybe some other place :P)!
        Scene::DestroyTagged( m_registry );
    }

    {
        VA_TRACE_CPU_SCOPE( BeginCallbacks );
        e_TickBegin( *this, deltaTime, applicationTickIndex );
    }

    m_async.Begin( deltaTime, applicationTickIndex );
}

void vaScene::TickEnd( )
{
    assert( IsTicking() );

    m_async.End( );

    {
        VA_TRACE_CPU_SCOPE( EndCallbacks );
        e_TickEnd( *this, m_currentTickDeltaTime, m_lastApplicationTickIndex );
    }

    m_currentTickDeltaTime     = -1.0f;
}
