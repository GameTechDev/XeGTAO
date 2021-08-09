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
    : vaUIPanel( "Scene", 1, !VA_MINIMAL_UI_BOOL, vaUIPanel::DockLocation::DockedLeft, "Scenes" )
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

    
    // If this is enabled, final rendering-related selections will be performed based on previous frame scene data
    // (but this frame's camera), introducing 1-frame lag to render data but allowing for more parallelization on 
    // the CPU.
    // #define VA_RENDER_SELECTIONS_FIRST // WARNING - static shadow maps will be broken by this!

    // set up basic staging
    {
#ifdef VA_RENDER_SELECTIONS_FIRST
        m_stages.push_back( "render_selections" );
#endif
        m_stages.push_back( "dirtylists" );
        m_stages.push_back( "logic" );
        m_stages.push_back( "motion" );
        m_stages.push_back( "transforms" );
        m_stages.push_back( "bounds" );
#ifndef VA_RENDER_SELECTIONS_FIRST
        m_stages.push_back( "render_selections" );
#endif
        m_stages.push_back( "late" );
        assert( m_stages.size() <= c_maxStageCount );
    }

    e_TickBegin.AddWithToken( m_aliveToken, this, &vaScene::InternalOnTickBegin );
    // e_TickEnd.AddWithToken( m_aliveToken, this, &vaScene::InternalOnTickEnd );

    // vector<int>                 ReadWriteComponents;
    // vector<int>                 ReadComponents;
    //
    //    Scene::AccessPermissions::Export<Scene::TransformDirtyTag, const Scene::CustomBoundingBox, Scene::Relationship, const Scene::TransformWorld>( ReadWriteComponents, ReadComponents );
    //    VA_LOG( "ReadWrite: " );
    //    for( int rw : ReadWriteComponents )
    //        VA_LOG( "  %s", Scene::Components::TypeName( rw ).c_str() );
    //    VA_LOG( "ReadOnly: " );
    //    for( int r : ReadComponents )
    //        VA_LOG( "  %s", Scene::Components::TypeName( r ).c_str( ) );

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


bool vaScene::Serialize( vaXMLSerializer & serializer )
{
    serializer;
    /*
    auto scene = m_scene.lock( );
    assert( scene != nullptr );

    // element opened by the parent, just fill in attributes

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "Name", m_name ) );

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector4>( "TransformR0", m_localTransform.Row( 0 ) ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector4>( "TransformR1", m_localTransform.Row( 1 ) ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector4>( "TransformR2", m_localTransform.Row( 2 ) ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector4>( "TransformR3", m_localTransform.Row( 3 ) ) );

    // VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector3>( "AABBMin", m_boundingBox.Min ) );
    // VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector3>( "AABBSize", m_boundingBox.Size ) );

    assert( serializer.GetVersion( ) > 0 );
    serializer.SerializeArray( "RenderMeshes", m_renderMeshes );

    VERIFY_TRUE_RETURN_ON_FALSE( scene->SerializeObjectsRecursive( serializer, "ChildObjects", m_children, this->shared_from_this( ) ) );

    if( serializer.IsReading( ) )
    {
        m_lastSceneTickIndex = -1;
        m_computedWorldTransform = vaMatrix4x4::Identity;
        m_computedLocalBoundingBox = vaBoundingBox::Degenerate;
        m_computedGlobalBoundingBox = vaBoundingBox::Degenerate;
        m_cachedRenderMeshes.resize( m_renderMeshes.size( ) );

        m_localTransform.Row( 0 ).w = 0.0f;
        m_localTransform.Row( 1 ).w = 0.0f;
        m_localTransform.Row( 2 ).w = 0.0f;
        m_localTransform.Row( 3 ).w = 1.0f;

        SetLocalTransform( m_localTransform );
    }
    */
    return true;
}


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

        DragDropNodeData currentDragDrop    = { vaGUID::Null, entt::null };
        auto ddpld = ImGui::GetDragDropPayload();
        if( ddpld != nullptr && ddpld->IsDataType( DragDropNodeData::PayloadTypeName() ) )
        {
            assert( ddpld->DataSize == sizeof(DragDropNodeData) );
            if( UID() == reinterpret_cast<DragDropNodeData*>( ddpld->Data )->SceneUID )
                currentDragDrop = *reinterpret_cast<DragDropNodeData*>( ddpld->Data );
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
                        if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( DragDropNodeData::PayloadTypeName( ) ) )
                        {
                            assert( payload->DataSize == sizeof( DragDropNodeData ) );
                            assert( ( *reinterpret_cast<DragDropNodeData*>( payload->Data ) ) == currentDragDrop );
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
                    DragDropNodeData ddsource{ UID(), entity };
                    ImGui::SetDragDropPayload( DragDropNodeData::PayloadTypeName( ), &ddsource, sizeof( ddsource ) );        // Set payload to carry the index of our item (could be anything)
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

void vaScene::InternalOnTickBegin( vaScene & scene, float deltaTime, int64 applicationTickIndex )
{
    deltaTime; applicationTickIndex;

    scene.AddWork_ComponentToList<Scene::WorldBoundsDirtyTag>( "UpdateDirtyBoundsList",    m_listDirtyBounds );
    
    // if the transform dirty flag needs to propagate to children, probably do it here
    scene.AddWork_ComponentToList<Scene::TransformDirtyTag>( "UpdateTransformDirtyList",   m_listDirtyTransforms );

    // process transforms with parent/child relationships
    {
        auto narrowBefore = [&]( ConcurrencyContext& ) noexcept
        {
            if( !m_listDirtyTransforms.IsConsuming( ) )
                m_listDirtyTransforms.StartConsuming( );

            for( uint32 depth = 0; depth < Scene::Relationship::c_MaxDepthLevels; depth++ )
                m_listHierarchyDirtyTransforms[depth].StartAppending( );

            return std::pair<uint32, uint32>( (uint32)m_listDirtyTransforms.Count(), vaTF::c_chunkBaseSize * 8 );
        };

        // this only categorizes transform-dirty entities to a depth-based bucket (this used to be one sorted list, but sorting is slow and harder to parallelize)
        auto wide = [ & ]( const int beg, const int end, ConcurrencyContext& ) noexcept
        {
            for( int index = beg; index < end; index++ )
            {
                const auto& cregistry = std::as_const( m_registry );
                entt::entity entity = m_listDirtyTransforms[index];
                const Scene::Relationship* relationship = cregistry.try_get<Scene::Relationship>( entity );
                uint32 depth = ( relationship != nullptr ) ? ( relationship->Depth ) : ( 0 );
                assert( depth < Scene::Relationship::c_MaxDepthLevels );
                m_listHierarchyDirtyTransforms[depth].Append( entity );
            }
        };

        // this actually goes wide again for each depth layer and does the main job of transform updates!
        auto narrowAfter = [ & ]( ConcurrencyContext & concurrencyContext )
        {
            // This switches all hierarchy transform dirty tag containers into 'readable'
            VA_TRACE_CPU_SCOPE( ContainersStartConsuming );
            for( uint32 depth = 0; depth < Scene::Relationship::c_MaxDepthLevels; depth++ )
                m_listHierarchyDirtyTransforms[depth].StartConsuming( );

#ifdef VA_SCENE_USE_TASKFLOW
            tf::Task prev = concurrencyContext.Subflow->placeholder( );
#else
            concurrencyContext;
#endif

            // Go from the top ('root') of the hierarchy to the bottom, so children entities pick up transforms of their parents
            for( uint32 depth = 0; depth < Scene::Relationship::c_MaxDepthLevels; depth++ )
            {
                if( m_listHierarchyDirtyTransforms[depth].Count( ) == 0 )
                    continue;
                string layerName = vaStringTools::Format( "TransformLayer_%02d", depth );
#ifdef VA_SCENE_USE_TASKFLOW
                auto [layerS, layerT] = vaTF::parallel_for_emplace( *concurrencyContext.Subflow, 0, (int)m_listHierarchyDirtyTransforms[depth].Count( ),
                    [&registry = m_registry, &dirtyTransforms = m_listHierarchyDirtyTransforms[depth], &listDirtyBounds = m_listDirtyBounds]( int begin, int end ) noexcept
                {
                    for( int i = begin; i < end; i++ )
                        Scene::UpdateTransforms( registry, dirtyTransforms[i], listDirtyBounds );
                }, vaTF::c_chunkBaseSize * 4, layerName.c_str() );
                prev.precede( layerS );
                prev = layerT;
                prev.name( std::move(layerName) );
#else
                auto & dirtyTransforms = m_listHierarchyDirtyTransforms[depth];
                for( int i = 0; i < (int)m_listHierarchyDirtyTransforms[depth].Count( ); i++ )
                    Scene::UpdateTransforms( m_registry, dirtyTransforms[i], m_listDirtyBounds );
                VA_TRACE_CPU_SCOPE_CUSTOMNAME( DispatchLayer, layerName );
#endif
            }
        };

        scene.AddWork<const Scene::TransformLocal, Scene::TransformWorld, const Scene::Relationship, const Scene::WorldBounds, const Scene::TransformLocalIsWorldTag>
            ( "TransformsUpdate", "transforms", "transforms", narrowBefore, wide, narrowAfter, nullptr ); 
    }

    // update bounds
    {
        auto narrowBefore = [ & ]( ConcurrencyContext& ) noexcept
        { 
            // prepare list states
            m_listDirtyBounds.StartConsuming( );
            m_listDirtyBoundsUpdatesFailed.StartAppending( );
            // return wide item counts
            return std::make_pair( (uint32)m_listDirtyBounds.Count(), vaTF::c_chunkBaseSize * 2 ); 
        };

        auto wide = [ & ]( int begin, int end, ConcurrencyContext& ) noexcept
        {
            for( int index = begin; index < end; index++ )
            {
                auto entity = m_listDirtyBounds[index];
                if( !m_registry.get<Scene::WorldBounds>( entity ).Update( m_registry, entity ) )
                    m_listDirtyBoundsUpdatesFailed.Append( entity );
            }
        };

        // this CAN'T be parallelized, but it's an exception handling anyway (not sure why I even needed to bother...............)
        auto finalizeFn = [ & ]( )
        {
            m_listDirtyBoundsUpdatesFailed.StartConsuming( );
            const int count = (int)m_listDirtyBoundsUpdatesFailed.Count( );
            for( int i = 0; i < count; i++ )
                m_registry.emplace<Scene::WorldBoundsDirtyTag>( m_listDirtyBoundsUpdatesFailed[i] );
        };

        scene.AddWork<Scene::WorldBounds, Scene::WorldBoundsDirtyTag, const Scene::TransformWorld, const Scene::CustomBoundingBox, const Scene::RenderMesh>(
            "DirtyBoundsUpdate", "bounds", "bounds", narrowBefore, wide, nullptr, finalizeFn );
    }
}

void vaScene::InternalOnTickEnd( vaScene & scene, float deltaTime, int64 applicationTickIndex )
{
    scene; deltaTime; applicationTickIndex;
}

#ifndef VA_SCENE_USE_TASKFLOW

void vaScene::StageDriver(  )
{
    Scene::AccessPermissions & accessPermissions = m_registry.ctx<Scene::AccessPermissions>( ); accessPermissions;

    for( int si = 0; si < m_stages.size( ); si++ )
    {
        for( int wi = 0; wi < m_workItemCount; wi++ )
        {
            WorkItem& item = m_workItemStorage[wi];
            if( item.StageStart == si )
            {
#ifdef _DEBUG
                // none of this is really needed for a singlethreaded approach but it's useful for testing the multithreaded components
                {
                    std::unique_lock<std::mutex> lk( accessPermissions.MasterMutex( ) );
                    if( !accessPermissions.TryAcquire( item.ReadWriteComponents, item.ReadComponents ) )
                    {
                        VA_ERROR( "Error trying to start work task '%s' - unable to acquire component access premissions", item.Name.c_str( ) ); //, m_stages[item.StageStart].c_str( ) );
                        return;
                    }
                }
#endif

                uint32 wideItemCount, wideItemChunkSize;
                {
                    VA_TRACE_CPU_SCOPE_CUSTOMNAME( NarrowBefore, item.NameNarrowBefore );
                    auto [_wideItemCount, _wideItemChunkSize] = item.NarrowBefore( vaScene::ConcurrencyContext{} );
                    item.NarrowBefore = nullptr;
                    wideItemCount = _wideItemCount; wideItemChunkSize = _wideItemChunkSize;
                }
                if( item.Wide && wideItemCount > 0 )
                {
                    assert( wideItemChunkSize > 0 );
                    VA_TRACE_CPU_SCOPE_CUSTOMNAME( WideItem, item.NameWide );
                    for( uint32 i = 0; i < wideItemCount; i += wideItemChunkSize )
                    {
                        VA_TRACE_CPU_SCOPE_CUSTOMNAME( block, item.NameWideBlock );
                        item.Wide( i, std::min( i + wideItemChunkSize, wideItemCount ), vaScene::ConcurrencyContext{} );
                    }
                    item.Wide = nullptr;
                }
                if( item.NarrowAfter )
                {
                    VA_TRACE_CPU_SCOPE_CUSTOMNAME( NarrowAfter, item.NameNarrowAfter );
                    item.NarrowAfter( vaScene::ConcurrencyContext{} );
                    item.NarrowAfter = nullptr;
                }
            }

            // none of this is really needed for a singlethreaded approach but it's useful for testing the multithreaded components
#ifdef _DEBUG
            if( item.StageStop == si )
            {
                std::unique_lock<std::mutex> lk( accessPermissions.MasterMutex( ) );
                accessPermissions.Release( item.ReadWriteComponents, item.ReadComponents );
            }
#endif
        }

        // none of this is really needed for a singlethreaded approach but it's useful for testing the multithreaded components
        {
            std::unique_lock<std::mutex> lk( m_stagingMutex );
            assert( m_finishedStageIndex < si );
            m_finishedStageIndex = si;
            lk.unlock( );
            m_stagingCV.notify_one( );
        }
    }
}

#else

void vaScene::StageDriver( tf::Subflow & subflow )
{
    assert( m_currentTickDeltaTime >= 0.0f );
    subflow;

    Scene::AccessPermissions & accessPermissions = m_registry.ctx<Scene::AccessPermissions>( );

    tf::Task stageCompleteTasks[c_maxStageCount];

    // create stage 'finalizer' tasks (updates the m_finishedStageIndex)
    for( int si = 0; si < m_stages.size( ); si++ )
    {
        auto finishStage = [ &, index = si ]( tf::Subflow & ) noexcept
        {
            std::unique_lock<std::mutex> lk( m_stagingMutex );
            assert( m_finishedStageIndex < index );
            m_finishedStageIndex = index;
            lk.unlock();
            m_stagingCV.notify_one();
        };
        stageCompleteTasks[si] = subflow.emplace( finishStage ).name( m_stages[si] );
        if( si > 0 )
            stageCompleteTasks[si-1].precede(stageCompleteTasks[si]);
    }

    for( int si = 0; si < m_stages.size( ); si++ )
    {
        for( int wi = 0; wi < m_workItemCount; wi++ )
        {
            WorkItem & item = m_workItemStorage[wi];
            if( item.StageStart == si )
            {
                auto itemDriver = [ &item, &accessPermissions ]( tf::Subflow & subflow ) noexcept
                {
                    // this could be under debug only
#ifdef _DEBUG
                    {
                        std::unique_lock<std::mutex> lk( accessPermissions.MasterMutex( ) );
                        if( !accessPermissions.TryAcquire( item.ReadWriteComponents, item.ReadComponents ) )
                        {
                            VA_ERROR( "Error trying to start work task '%s' - unable to acquire component access premissions", item.Name.c_str( ) ); //, m_stages[item.StageStart].c_str( ) );
                            return;
                        }
                    }
#endif

                    assert( item.NarrowBefore );    // work item has to have at least 'NarrowBefore' callback

                    auto [wideItemCount, wideItemChunkSize] = item.NarrowBefore( vaScene::ConcurrencyContext{ &subflow } );
                    item.NarrowBefore = nullptr;

                    auto T = subflow.placeholder( );
                    if( item.Wide && wideItemCount > 0 )
                    {
                        assert( wideItemChunkSize > 0 );
                        auto wideCB = [ &item ]( int beg, int end, tf::Subflow& subflow )
                        {
                            item.Wide( beg, end, vaScene::ConcurrencyContext{ &subflow } );
                        };
                        auto [_s, _t] = vaTF::parallel_for_emplace( subflow, 0, wideItemCount, std::move( wideCB ), wideItemChunkSize, item.NameWideBlock.c_str( ) );
                        _s.name( "wide_start" );
                        _t.name( "wide_terminate" );
                        T = _t;
                        //T.name( item.NameWide );
                    }
                    if( item.NarrowAfter )
                    {
                        auto afterCB = [ &item ]( tf::Subflow& subflow )
                        {
                            item.NarrowAfter( vaScene::ConcurrencyContext{ &subflow } );
                        };
                        auto N = subflow.emplace( afterCB );
                        N.name( item.NameNarrowAfter );
                        T.precede( N );
                    }

                    // this could be under debug only
#ifdef _DEBUG
                    subflow.join();
                    {
                        std::unique_lock<std::mutex> lk( accessPermissions.MasterMutex( ) );
                        accessPermissions.Release( item.ReadWriteComponents, item.ReadComponents );
                    };
#endif
                };

                // Connect dependencies;
                auto workTask = subflow.emplace( itemDriver ).name( item.Name );
                if( si > 0 )
                    stageCompleteTasks[si - 1].precede( workTask );
                stageCompleteTasks[item.StageStop].succeed( workTask );
            }
        }
    }
    subflow.join();
}
#endif

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

    // check that our reactive update of tags on Scene::Relationship change is correct
    // maybe put this under EXHAUSTIVE_DEBUG or something?
#ifdef _DEBUG
    {
        //        m_registry.view<const Scene::Relationship>( ).each( [ & ]( entt::entity entity, const Scene::Relationship & relationship )
        //        {
        //            assert( m_registry.has<Scene::RelationshipRootTag>( entity ) == (relationship.Parent != entt::null ) );
        //        } );

        //        m_registry.view<entt::exclude_t<const Scene::Relationship>>( ).each( [ & ]( entt::entity entity ) 
        //        {
        //            assert( !m_registry.has<Scene::RelationshipRootTag>( entity ) );
        //        } );
    }
#endif 

    {
        VA_TRACE_CPU_SCOPE( BeginCallbacks );

        assert( m_workItemCount == 0 );
        VA_GENERIC_RAII_SCOPE( m_canAddWork = true;, m_canAddWork = false; )
        e_TickBegin( *this, deltaTime, applicationTickIndex );
    }

    // concurrent stuff
    VA_TRACE_CPU_SCOPE( ConcurrentWork );

    Scene::AccessPermissions& accessPermissions = m_registry.ctx<Scene::AccessPermissions>( );
    accessPermissions.SetState( Scene::AccessPermissions::State::Concurrent );

    {
        std::unique_lock<std::mutex> lk( m_stagingMutex );
        m_finishedStageIndex = -1;
    }


#ifdef VA_SCENE_USE_TASKFLOW
    assert( !m_stageDriver.valid( ) );
    auto stageDriverProc = [ this ]( tf::Subflow& subflow ) { this->StageDriver( subflow ); };
#if 0
    m_stageDriver = vaTF::GetInstance( ).async( stageDriverProc );
#else
    m_tf.emplace( stageDriverProc );
    m_stageDriver = vaTF::Executor().run( m_tf );
#endif
#else
    StageDriver( );
#endif
}

void vaScene::TickEnd( )
{
    assert( IsTicking() );

    // Make sure everything is done
    TickWait( "" );

#ifdef VA_SCENE_USE_TASKFLOW
    assert( m_stageDriver.valid() );
    m_stageDriver.wait();
    m_stageDriver = {};
    // vaFileTools::WriteText( "C:\\temp\\aeiou.graphviz", m_tf.dump() );
   
    m_tf.clear( );
#endif

    Scene::AccessPermissions& accessPermissions = m_registry.ctx<Scene::AccessPermissions>( );
    accessPermissions.SetState( Scene::AccessPermissions::State::Serialized );

    // Cleanup!
    for( int wi = 0; wi < m_workItemCount; wi++ )
    {
        WorkItem & item = m_workItemStorage[wi];
            
        if( item.Finalizer )
        {
            VA_TRACE_CPU_SCOPE_CUSTOMNAME( Finalizer, item.NameFinalizer );
            item.Finalizer( );
            item.Finalizer  = nullptr;
        }

        // make sure we've cleared these up (avoids any unwanted lambda captures)
        assert( item.NarrowBefore == nullptr );
        item.Wide           = nullptr;
        item.NarrowAfter    = nullptr;
    }

    m_workItemCount     = 0;

    {
        VA_TRACE_CPU_SCOPE( EndCallbacks );

        assert( m_workItemCount == 0 );
        e_TickEnd( *this, m_currentTickDeltaTime, m_lastApplicationTickIndex );
    }

    m_currentTickDeltaTime     = -1.0f;
}

void vaScene::TickWait( const string & stageName )
{
    assert( vaThreading::IsMainThread() );

    int waitStageIndex = -1;

    if( stageName != "" )
    {
        for( int i = 0; i < m_stages.size(); i++ )
            if( m_stages[i] == stageName )
            {
                waitStageIndex = i;
                break;
            }
        assert( waitStageIndex != -1 );
    }

    if( m_workItemCount == 0 )
        return;

    {
        std::unique_lock<std::mutex> lk( m_stagingMutex );
        m_stagingCV.wait( lk, [ &finishedStageIndex = m_finishedStageIndex, waitStageIndex ] 
        { 
            return finishedStageIndex >= waitStageIndex; 
        } );
    }
}


