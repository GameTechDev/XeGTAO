///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaSceneComponentsUI.h"

#include "vaSceneSystems.h"

#include "vaScene.h"

//#include "Core/vaXMLSerialization.h"
#include "Core/System/vaFileTools.h"

#include "IntegratedExternals/vaImguiIntegration.h"
#include "IntegratedExternals\imgui\imgui_internal.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"
#include "Rendering/vaAssetPack.h"

#include <tuple>

#include "Rendering/vaDebugCanvas.h"

using namespace Vanilla;

using namespace Vanilla::Scene;

void Scene::HandleRightClickContextMenuPopup( vaScene & scene, entt::entity entity, bool hasOpenProperties, bool hasFocusInScene, const std::function<void()> & customPopupsTop )
{
    assert( vaThreading::IsMainThread() );

    // entt::null means ROOT node special case but this only works if also hierarchyDepth == 0
    if( entity == entt::null || !scene.Registry().valid( entity ) )
        { assert( false ); return; }

    ImGui::TextColored( ImVec4{ 1,1,0,1 }, Scene::GetNameAndID( scene.Registry(), entity ).c_str( ) );
        
    ImGui::Separator( );
        
    VA_GENERIC_RAII_SCOPE( ImGui::Indent( );, ImGui::Unindent( ); );
        
    if( customPopupsTop )
        customPopupsTop( );

    bool enableDelete   = true;
    bool enableRename   = scene.Registry().any_of<Scene::Name>( entity );
    //bool enableMoveTool = scene.Registry().any_of<Scene::TransformLocal>( entity );

#if 0
    if( ImGui::MenuItem( "Rename", NULL, false, enableRename ) ) 
    { 
        scene.UIOpenRename( entity );
        ImGui::CloseCurrentPopup( ); 
        return;
    }
#else
    static char nameStorage[256] = {0}; // I used a string here but it triggers memory checker breakpoint even if cleared and shrink_to_fit-ed - can't google for it now, reverting to old school 
    if( ImGui::BeginMenu( "Rename", enableRename ) )
    {
        ImGui::TextDisabled( "Enter new name:" );
        ImGui::Separator( );

        if( nameStorage[0] == 0 ) 
            std::strncpy( nameStorage, scene.Registry().get<Scene::Name>( entity ).c_str(), countof(nameStorage)-1);

        ImGui::InputText( "##edit", nameStorage, sizeof(nameStorage) );

        if( ImGui::Button( "Set new name", { -1, 0 } ) )
        {
            string oldName = scene.Registry().get<Scene::Name>( entity );
            scene.Registry().replace<Scene::Name>( entity, nameStorage );
            VA_LOG( "Entity name changed from '%s' to '%s'", oldName.c_str( ), nameStorage );
            ImGui::CloseCurrentPopup( );
            nameStorage[0] = 0;
        }

        ImGui::EndMenu( );
    }
    else
        nameStorage[0] = 0;
#endif

    if( enableDelete && ImGui::BeginMenu( "Delete" ) )
    {
        VA_GENERIC_RAII_SCOPE( ,ImGui::EndMenu( ););
        ImGui::TextDisabled( "Delete entity: are you really sure? There is no 'Undo'" );
        ImGui::TextDisabled( "(Children, if any, will be unparented but not deleted)" );
        ImGui::Separator( );
        if( ImGui::MenuItem( "Yes, delete", NULL, false, true ) )
        {
            ImGui::CloseCurrentPopup( );
            scene.DestroyEntity( entity, false );
            return;
        }
        if( ImGui::MenuItem( "Uh oh no, cancel", NULL, false, true ) )
            ImGui::CloseCurrentPopup( );
    }

    ImGui::Separator( );

    // no longer using MoveTool this way (instead using selection)
    //{
    //    enableMoveTool;
    //    bool moveToolEngaged = scene.Registry().any_of<Scene::UIMoveTool>( entity );
    //    ImGui::MenuItem( "Move tool", 0, &moveToolEngaged, enableMoveTool );
    //    if( moveToolEngaged != scene.Registry().any_of<Scene::UIMoveTool>( entity ) )
    //    {
    //        if( moveToolEngaged )
    //            scene.Registry().emplace<Scene::UIMoveTool>( entity );
    //        else
    //            scene.Registry().remove<Scene::UIMoveTool>( entity );
    //    }
    //}

    // ImGui::Separator( );

    if( hasFocusInScene && ImGui::MenuItem( "Highlight in scene", nullptr, false, true ) )
    {
        ImGui::CloseCurrentPopup( );
        scene.UIHighlight( entity );
    }

    if( hasOpenProperties && ImGui::MenuItem( "Open properties", NULL, false, true ) ) 
    { 
        ImGui::CloseCurrentPopup( );
        scene.UIOpenProperties( entity );
    }
}

vaEntityPropertiesPanel::vaEntityPropertiesPanel( entt::registry & registry, entt::entity entity ) : m_registry( registry ), m_entity( entity ), m_scene( nullptr )
{
    assert( m_registry.valid( m_entity ) );
}

vaEntityPropertiesPanel::vaEntityPropertiesPanel( vaScene & scene, entt::entity entity ) : m_registry( scene.Registry() ), m_entity( entity ), m_scene( &scene )
{
    assert( m_registry.valid( m_entity ) );
}

vaEntityPropertiesPanel::~vaEntityPropertiesPanel( )
{
    assert( m_registry.valid( m_entity ) );
}

string vaEntityPropertiesPanel::UIPropertiesItemGetDisplayName( ) const
{
    return Scene::GetNameAndID( m_registry, m_entity );
}

std::tuple<bool, bool> ComponentHeaderUI( const string & name, bool has, bool hasUITick )
{
    float mainPartWidth = ImGui::GetContentRegionAvail( ).x - ImGui::CalcTextSize( "..." ).x; // - ImGui::GetStyle( ).FramePadding.x
    ImGui::Columns( 2, "ComponentsHeader", false ); // 4-ways, with border
    ImGui::SetColumnWidth( 0, mainPartWidth );
    
    bool collapsingHeaderOpen = false;

    if( has && hasUITick )
        collapsingHeaderOpen = ImGui::CollapsingHeader( name.c_str() );
    else
    {
        ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( (has)?(ImGuiCol_Text):(ImGuiCol_TextDisabled) ) );
        ImGui::PushStyleColor( ImGuiCol_Button, ImGui::GetStyleColorVec4( (has)?(ImGuiCol_Header):(ImGuiCol_PopupBg) ) );

        ImGui::SetCursorPosX( ImGui::GetCursorPosX() - ImGui::GetStyle( ).FramePadding.x + 1 );
        ImGui::ButtonEx( name.c_str(), {ImGui::GetContentRegionAvail( ).x+ImGui::GetStyle( ).FramePadding.x*2-1,0}, ImGuiButtonFlags_Disabled );

        ImGui::PopStyleColor( 2 );
    }

    ImGui::SetColumnOffset( 1, mainPartWidth - ImGui::GetStyle( ).FramePadding.x );
    ImGui::NextColumn( );
    ImGui::SetCursorPosX( ImGui::GetCursorPosX() - ImGui::GetStyle( ).FramePadding.x );
    bool menuOpen = ImGui::Button( "...###CompHeaderEllipsis", {ImGui::GetContentRegionAvail( ).x+ImGui::GetStyle( ).FramePadding.x*2,0} );
    ImGui::Columns( 1 );
    return {collapsingHeaderOpen, menuOpen};
}

void vaEntityPropertiesPanel::UIPropertiesItemTick( vaApplicationBase & application, bool openMenu, bool hovered )
{
    hovered;

    if( !m_registry.valid( m_entity ) )
    {
        assert( false );
        return;
    }

    if( m_scene != nullptr )
    {
        const char* popupName = "RightClickEntityContextMenuFromProperties";
        if( openMenu )
        {
            if( !ImGui::IsPopupOpen( popupName ) )
                ImGui::OpenPopup( popupName );
        }

        if( ImGui::BeginPopup( popupName ) )
        {
            HandleRightClickContextMenuPopup( *m_scene, m_entity, false, true );
            ImGui::EndPopup( );
        }

        // maybe we just got ourselves deleted!
        if( !m_registry.valid( m_entity ) )
            return;
    }

    m_uiComponentsFilter = "(not implemented yet)";
    ImGui::InputText( "Filter keywords", &m_uiComponentsFilter, ImGuiInputTextFlags_AutoSelectAll );
    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Filter components by name; for ex. \"word1 word2 -word3\" means the component name has\n to include both word1 and word2 but not include word3." );
    
    ImGui::Separator();
    //ImGui::Text( "Properties for entity %s", Scene::GetNameAndID( m_registry, m_entity ).c_str() );

    if( m_uiContextRefs.size() < Scene::Components::TypeCount() )
        m_uiContextRefs.resize( Scene::Components::TypeCount() );

    int visible = 0, invisible = 0;

    bool contextMenuClick = false;

    for( int i = 0; i < Scene::Components::TypeCount(); i++ )
    {
        if( !Scene::Components::UIVisible( i ) )
        {
            invisible++;
            continue;
        }
        visible++;

        // make sure all per-component UI is under unique imgui ID or different component items might overlap
        VA_GENERIC_RAII_SCOPE( ImGui::PushID( Scene::Components::TypeName(i).c_str() );, ImGui::PopID( ); );

        bool hasComponent = Scene::Components::Has( i, m_registry, m_entity );
        bool hasUIHandler = Scene::Components::HasUITick( i );

        auto [collapsingHeaderOpen, menuOpen] = ComponentHeaderUI( Scene::Components::TypeName(i), hasComponent, hasUIHandler );
        if( !hasComponent || !hasUIHandler )
            { assert( !collapsingHeaderOpen ); collapsingHeaderOpen = false; }

        if( menuOpen && !contextMenuClick )
        {
            contextMenuClick = true;
            m_uiMenuOpenedComponentID = i;
        }

        if( collapsingHeaderOpen )
        {
            Scene::UIArgs args{ application, m_uiContextRefs[i], true, false, m_registry, m_entity };

            Scene::Components::UITick( i, m_registry, m_entity, args );

            if( Scene::Components::HasValidate( i ) )
                Scene::Components::Validate( i, m_registry, m_entity );
        }
    }
    ImGui::Text( "Components: %d (%d visible, %d invisible)", visible+invisible, visible, invisible );

    if( contextMenuClick )
        ImGui::OpenPopup( "RightClickComponentContextMenu" );

    if( ImGui::BeginPopup( "RightClickComponentContextMenu" ) )
    {
        if( m_uiMenuOpenedComponentID == -1 )
            ImGui::CloseCurrentPopup( );

        bool hasComponent = Scene::Components::Has( m_uiMenuOpenedComponentID, m_registry, m_entity );
        bool UIAddRemoveResetDisabled = Scene::Components::UIAddRemoveResetDisabled( m_uiMenuOpenedComponentID );

        bool enableInfo     = true;

        ImGui::TextColored( ImVec4{ 1,1,0,1 }, "Component '%s', %s", Scene::Components::TypeName(m_uiMenuOpenedComponentID).c_str(), (hasComponent)?("present"):("not present") );
        ImGui::Separator( );
        ImGui::Indent( );

        if( ImGui::MenuItem( "Add", NULL, false, !hasComponent && !UIAddRemoveResetDisabled ) ) 
        { 
            Scene::Components::EmplaceOrReplace( m_uiMenuOpenedComponentID, m_registry, m_entity );
            ImGui::CloseCurrentPopup( ); 
        }
        if( ImGui::MenuItem( "Remove", NULL, false, hasComponent && !UIAddRemoveResetDisabled ) )
        {
            Scene::Components::Remove( m_uiMenuOpenedComponentID, m_registry, m_entity );
            ImGui::CloseCurrentPopup( );
        }
        if( ImGui::MenuItem( "Reset", NULL, false, hasComponent && !UIAddRemoveResetDisabled ) )
        {
            Scene::Components::Reset( m_uiMenuOpenedComponentID, m_registry, m_entity );
            ImGui::CloseCurrentPopup( );
        }
        ImGui::Separator( );
        if( ImGui::BeginMenu( "Component type info", enableInfo ) )
        {
            ImGui::Text( Scene::Components::DetailedTypeInfo( m_uiMenuOpenedComponentID ).c_str() );
            if( Scene::Components::HasUITypeInfo( m_uiMenuOpenedComponentID ) )
            {
                ImGui::NewLine();
                ImGui::TextWrapped( Scene::Components::UITypeInfo( m_uiMenuOpenedComponentID ) );
            }

            ImGui::EndMenu( );
        }
        ImGui::Unindent( );

        ImGui::EndPopup();
    }

}

void TransformLocal::UITick( UIArgs & uiArgs )
{
    bool hadChanges = ImGuiEx_Transform( "MRSTool", *this, false, false ); hadChanges;
    if( hadChanges )
        Scene::SetTransformDirtyRecursive( uiArgs.Registry, uiArgs.Entity );
}

void TransformWorld::UITick( UIArgs & /*uiArgs*/ )
{
    bool hadChanges = ImGuiEx_Transform( "MRSTool", *this, false, true ); hadChanges;
    assert( !hadChanges );
}


void RenderMesh::UITick( UIArgs & uiArgs )
{
    uiArgs;
    bool inputsChanged = vaAssetPackManager::UIAssetLinkWidget<vaAssetRenderMesh>( "mesh_asset", MeshUID );
    inputsChanged;

    ImGui::InputFloat( "VisibilityRange", &VisibilityRange, 1.0f );
    VisibilityRange = vaMath::Clamp( VisibilityRange, 0.0f, std::numeric_limits<float>::max() );
}

void WorldBounds::UIDraw( const entt::registry& registry, entt::entity entity, vaDebugCanvas2D& canvas2D, vaDebugCanvas3D& canvas3D )
{
    registry; entity; canvas2D;
    canvas3D.DrawBox( AABB, 0x80202020, 0x20101010 );
}

void CustomBoundingBox::UITick( UIArgs & uiArgs ) 
{ 
    uiArgs;
    bool hadChanges = false;
    hadChanges |= ImGui::InputFloat3( "Min", &Min.x );
    hadChanges |= ImGui::InputFloat3( "Size", &Size.x );
    if( hadChanges )
        uiArgs.Registry.emplace_or_replace<Scene::WorldBoundsDirtyTag>( uiArgs.Entity );
}

void CustomBoundingBox::UIDraw( const entt::registry& registry, entt::entity entity, vaDebugCanvas2D& canvas2D, vaDebugCanvas3D& canvas3D )
{
    registry; entity; canvas2D; canvas3D;

    assert( false ); // do the below pulse and use the below color, much nicer; to get m_sceneTime use a static component in registry
    // float pulse = 0.5f * (float)vaMath::Sin( m_sceneTime * VA_PIf * 2.0f ) + 0.5f;
    // 
    // auto selectedObject = m_UI_SelectedObject.lock( );
    // if( selectedObject != nullptr )
    // {
    //     canvas3D.DrawBox( selectedObject->GetGlobalAABB( ), vaVector4::ToBGRA( 0.0f, 0.0f + pulse, 0.0f, 1.0f ), vaVector4::ToBGRA( 0.5f, 0.5f, 0.5f, 0.1f ) );
    // }

    auto transformWorld = registry.try_get<Scene::TransformWorld>( entity );
    if( transformWorld != nullptr )
        canvas3D.DrawBox( *this, 0x80202020, 0x30A01010, transformWorld );
}

void LightBase::UITickColor( UIArgs & uiArgs )
{
    uiArgs;
    vaVector3 colorSRGB = vaVector3::LinearToSRGB( Color );
    if( ImGui::ColorEdit3( "Color", &colorSRGB.x, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float ) )
        Color = vaVector3::SRGBToLinear( colorSRGB );
    ImGui::InputFloat( "Intensity", &Intensity );

    ImGui::InputFloat( "FadeFactor (enable/disable)", &FadeFactor );

    if( FadeFactor == 0 )
    {
        ImGui::Text( "Light disabled (FadeFactor == 0)" );
    }

    /*
    float luminance = vaVector3::LinearToLuminance( Color );
    if( ImGuiEx_Button( "Normalize luminance", { -1, 0 }, std::abs( luminance - 1.0f ) <= 0.01f ) )
    {
    vaColor::Nor....
    }
    */
}

void LightAmbient::UITick( UIArgs & uiArgs )
{
    UITickColor( uiArgs );
}

//void LightDirectional::UITick( UIArgs & uiArgs )
//{
//    UITickColor( uiArgs );
//
//    ImGui::Separator( );
//
//    bool sunAreaLight = AngularRadius > 0;
//    if( ImGui::Checkbox( "Sun area light", &sunAreaLight ) )
//    {
//        if( sunAreaLight && AngularRadius == 0 )
//            AngularRadius = vaMath::DegreeToRadian( 0.545f );
//        else if( !sunAreaLight )
//            AngularRadius = 0.0f;
//    }
//    if( sunAreaLight )
//    {
//        float angularRadiusDeg = vaMath::RadianToDegree( AngularRadius );
//        ImGui::SliderFloat( "AngularRadius (deg)", &angularRadiusDeg, 1e-3f, 10.0f );
//        AngularRadius = vaMath::DegreeToRadian( angularRadiusDeg );
//        ImGui::SliderFloat( "HaloSize", &HaloSize, 0.0f, 100.0f, "%.3f", 2.0f );
//        ImGui::SliderFloat( "HaloFalloff", &HaloFalloff, 0.0f, 10000.0f, "%.3f", 4.0f );
//    }
//
//    ImGui::Separator();
//
//    ImGui::Checkbox( "CastShadows", &CastShadows );
//}
//
//void LightDirectional::UIDraw( const entt::registry& registry, entt::entity entity, vaDebugCanvas2D& canvas2D, vaDebugCanvas3D& canvas3D )
//{
//    registry; entity; canvas2D; canvas3D;
//
//    auto transformWorld = registry.try_get<Scene::TransformWorld>( entity );
//    if( transformWorld == nullptr )
//        return;
//
//    canvas3D.DrawArrow( { -1.0f ,0 ,0 }, { 1, 0, 0 }, 0.2f, 0x10000000, 0x80808080, 0x8000FFFF, transformWorld );
//}

void LightPoint::UITick( UIArgs & uiArgs )
{
    UITickColor( uiArgs );

    ImGui::Separator( );

    ImGui::InputFloat( "Size", &Size );
    ImGui::InputFloat( "RTSizeModifier", &RTSizeModifier );
    ImGui::InputFloat( "Range", &Range );

    bool spotLight = SpotInnerAngle != 0 || SpotOuterAngle != 0;
    if( ImGui::Checkbox( "Spotlight", &spotLight ) )
    {
        if( spotLight )
        {
            SpotInnerAngle = VA_PIf * 0.2f;
            SpotOuterAngle = VA_PIf * 0.3f;
        }
        else
        {
            SpotInnerAngle = 0.0f;
            SpotOuterAngle = 0.0f;
        }
    }
    if( spotLight )
    {
        float spotInnerAngleDeg = vaMath::RadianToDegree( SpotInnerAngle );
        float spotOuterAngleDeg = vaMath::RadianToDegree( SpotOuterAngle );
        ImGui::InputFloat( "SpotInnerAngle", &spotInnerAngleDeg );
        ImGui::InputFloat( "SpotOuterAngle", &spotOuterAngleDeg );
        SpotInnerAngle = vaMath::DegreeToRadian( spotInnerAngleDeg );
        SpotOuterAngle = vaMath::DegreeToRadian( spotOuterAngleDeg );
    }

    ImGui::Separator( );

    ImGui::Checkbox( "CastShadows", &CastShadows );
}

void LightPoint::UIDraw( const entt::registry & registry, entt::entity entity, vaDebugCanvas2D & canvas2D, vaDebugCanvas3D & canvas3D )
{
    registry; entity; canvas2D; canvas3D;

    auto transformWorld = registry.try_get<Scene::TransformWorld>( entity );
    if( transformWorld == nullptr )
        return;

    vaVector3 position = transformWorld->GetTranslation( );
    vaVector3 direction = transformWorld->GetAxisX( ).Normalized( );
    canvas3D.DrawLightViz( position, direction, this->Size, this->Range, std::max( 0.0001f, this->SpotInnerAngle ), std::max( 0.0001f, this->SpotOuterAngle ), this->Color );
    //        canvas3D.DrawBox( *this, 0x80202020, 0x30A01010, transformWorld );
}

void EmissiveMaterialDriver::UITick( UIArgs & uiArgs )
{
    ImGui::InputFloat3( "EmissiveMultiplier",       &EmissiveMultiplier.x );
    
    ReferenceLightEntity.DrawUI( uiArgs, "ReferenceLight" );

    if( ReferenceLightEntity != entt::null )
        ImGui::InputFloat( "ReferenceLightMultiplier",  &ReferenceLightMultiplier );
}

void EmissiveMaterialDriver::Validate( entt::registry& , entt::entity )
{
    vaVector3::Clamp( EmissiveMultiplier, {0,0,0}, {1e16f,1e16f,1e16f} );
    ReferenceLightMultiplier = vaMath::Clamp( ReferenceLightMultiplier, 0.0f, 1e16f );
}

void FogSphere::UITick( UIArgs & uiArgs )
{
    uiArgs;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui::Checkbox( "Enabled",             &Enabled );
    ImGui::Checkbox( "UseCustomCenter",     &UseCustomCenter );
    ImGui::InputFloat3( "Center",           &Center.x );
    
    ImVec4 col( Color.x, Color.y, Color.z, 1.0 );
    ImGui::ColorEdit3( "Color", &Color.x, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float );

    ImGui::InputFloat( "Inner radius",      &RadiusInner );
    ImGui::InputFloat( "Outer radius",      &RadiusOuter );

    ImGui::InputFloat( "Blend curve pow",   &BlendCurvePow );
    ImGui::InputFloat( "Blend multiplier",  &BlendMultiplier );
#endif

}

void SkyboxTexture::UITick( UIArgs & uiArgs )
{
    uiArgs;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui::Text( "No UI for skybox setup yet :)" );
#endif    
}

void IBLProbe::UITick( UIArgs& uiArgs )
{
    IBLProbeUIContext& uiContext = uiArgs.JazzUpContext<IBLProbeUIContext>( );
    uiContext;

    if( ImGui::InputText( "Input file", &ImportFilePath, ImGuiInputTextFlags_EnterReturnsTrue ) )
        SetImportFilePath( ImportFilePath, false );
    ImGui::SameLine( );
    if( ImGui::Button( "..." ) )
    {
        string fileName = vaFileTools::OpenFileDialog( ImportFilePath, vaCore::GetExecutableDirectoryNarrow( ) );
        if( fileName != "" )
            SetImportFilePath( fileName, false );
    }

    // this is the old UI code for editing IBL probes - might still be useful when this needs updating in the future!
#if 0
    {
        string uniqueID = idPrefix + std::to_string( i );
        if( vaUIManager::GetInstance( ).FindTransientPropertyItem( uniqueID, true ) == nullptr )
        {
            auto uiContext = std::make_shared<vaIBLProbe::UIContext>( shared_from_this( ) );

            vaUIManager::GetInstance( ).CreateTransientPropertyItem( uniqueID, m_name + " : " + probeName,
                [ &probe, uniqueID, probeName ]( vaApplicationBase& application, const shared_ptr<void>& drawContext ) -> bool
            {
                auto uiContext = std::static_pointer_cast<vaIBLProbe::UIContext>( drawContext ); assert( uiContext != nullptr );
                auto aliveToken = ( uiContext != nullptr ) ? ( uiContext->AliveToken.lock( ) ) : ( shared_ptr<void>( ) );
                if( !aliveToken )
                    return false;

                vaDebugCanvas3D& canvas3D = application.GetRenderDevice( ).GetCanvas3D( ); canvas3D;

                if( ImGui::InputText( "Input file", &probe.ImportFilePath ) )
                    probe.SetImportFilePath( probe.ImportFilePath, false );
                ImGui::SameLine( );
                if( ImGui::Button( "..." ) )
                {
                    string fileName = vaFileTools::OpenFileDialog( probe.ImportFilePath, vaCore::GetExecutableDirectoryNarrow( ) );
                    if( fileName != "" )
                        probe.SetImportFilePath( probe.ImportFilePath, false );
                }

                ImGui::Separator( );

                // capture position
#if 0 // disabled due to changes in the MoveRotateScaleWidget tool
                {
                    vaMatrix4x4 probeMat = vaMatrix4x4::FromTranslation( probe.Position );
                    bool mrsWidgetActive = vaUIManager::GetInstance( ).MoveRotateScaleWidget( uniqueID + "c", probeName + " [capture position]", probeMat );
                    if( mrsWidgetActive )
                    {
                        ImGui::Text( "<MRSWidget Active>" );
                        probe.Position = probeMat.GetTranslation( );
                        canvas3D.DrawSphere( probe.Position, 0.1f, 0xFF00FF00 );
                    }
                    else
                    {
                        vaVector3 pos = probe.Position;
                        if( ImGui::InputFloat3( "Translate", &pos.x, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue ) )
                            probe.Position = pos;
                    }
                }
#endif

                ImGui::Checkbox( "Use OBB geometry proxy", &probe.UseGeometryProxy );

                if( probe.UseGeometryProxy )
                {
                    vaMatrix4x4 probeMat = probe.GeometryProxy.ToScaledTransform( );

                    // activate move rotate scale widget
#if 0 // disabled due to changes in the MoveRotateScaleWidget tool
                    bool mrsWidgetActive = vaUIManager::GetInstance( ).MoveRotateScaleWidget( uniqueID + "p", probeName + " [geometry proxy]", probeMat );
                    if( mrsWidgetActive )
                    {
                        ImGui::Text( "<MRSWidget Active>" );

                        probe.GeometryProxy = vaOrientedBoundingBox::FromScaledTransform( probeMat );
                        canvas3D.DrawBox( probe.GeometryProxy, 0xFF00FF00, 0x10808000 );
                    }
                    else
                    {
                        if( ImGuiEx_Transform( uniqueID.c_str( ), probeMat, false, false ) )
                            probe.GeometryProxy = vaOrientedBoundingBox::FromScaledTransform( probeMat );
                    }
#endif

                    if( ImGui::Button( "Set capture center to geometry proxy center", { -1, 0 } ) )
                        probe.Position = probe.GeometryProxy.Center;
                }

                if( ImGui::CollapsingHeader( "Local to Global transition region", ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
                {
                    vaMatrix4x4 transform = probe.FadeOutProxy.ToScaledTransform( );
#if 0 // disabled due to changes in the MoveRotateScaleWidget tool
                    bool mrsWidgetActive = vaUIManager::GetInstance( ).MoveRotateScaleWidget( uniqueID + "lgtr", "Local to global IBL transition region", transform );
                    if( mrsWidgetActive )
                    {
                        ImGui::Text( "<MRSWidget Active>" );

                        probe.FadeOutProxy = vaOrientedBoundingBox::FromScaledTransform( transform );
                        canvas3D.DrawBox( probe.FadeOutProxy, 0xFF00FF00, 0x10008080 );
                    }
                    else
                    {
                        if( ImGuiEx_Transform( ( uniqueID + "lgtr" ).c_str( ), transform, false, false ) )
                            probe.FadeOutProxy = vaOrientedBoundingBox::FromScaledTransform( transform );
                    }
#endif
                }

                ImGui::Separator( );

                vaVector3 colorSRGB = vaVector3::LinearToSRGB( probe.AmbientColor );
                if( ImGui::ColorEdit3( "Ambient Color", &colorSRGB.x, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float ) )
                    probe.AmbientColor = vaVector3::SRGBToLinear( colorSRGB );

                ImGui::InputFloat( "Ambient Color Intensity", &probe.AmbientColorIntensity );


                return true;
            }, uiContext );
        }

    }; break;
#endif
}

void EntityReference::DrawUI( UIArgs & uiArgs, const string & name )
{
    // what gets displayed on the button
    string buttonName = vaStringTools::Format( "%s entity: %s", name.c_str(), Scene::GetName( uiArgs.Registry, entt::entity(*this) ).c_str() );
    
    // context menu
    const char* popupName = "ClickEntityReference";
    if( ImGui::Button( buttonName.c_str( ), { -1, 0 } ) )
        ImGui::OpenPopup( popupName );

    if( ImGui::BeginPopup( popupName ) )
    {
        if( m_entity == entt::null )
        {
            ImGui::Text( "No reference - drag and drop from Scene entity list" );
        }
        else
        {
            if( ImGui::MenuItem( "Disconnect" ) )
            {
                ImGui::CloseCurrentPopup( );
                Set( uiArgs.Registry, entt::null );
            }
            if( ImGui::MenuItem( "Highlight in scene" ) )
            {
                ImGui::CloseCurrentPopup();
                Scene::UIHighlight( uiArgs.Registry, m_entity );
            }
        }
        ImGui::EndPopup( );
    }

    if( ImGui::BeginDragDropTarget( ) )
    {
        if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( DragDropNodeData::PayloadTypeName( ) ) )
        {
            assert( payload->DataSize == sizeof( DragDropNodeData ) );
            const DragDropNodeData & data = *reinterpret_cast<DragDropNodeData*>( payload->Data );
            assert( data.SceneUID == uiArgs.Registry.ctx<Scene::UID>() );
            Set( uiArgs.Registry, data.Entity );
        }
        ImGui::EndDragDropTarget( );
    }

}