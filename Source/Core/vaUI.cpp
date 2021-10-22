///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "Core/vaUI.h"

#include "Core/vaInput.h"
#include "Core/vaApplicationBase.h"

#include "Rendering/vaShader.h"

#include "Rendering/vaDebugCanvas.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "IntegratedExternals/imgui/imgui_internal.h"

namespace Vanilla
{
    class vaUIPropertiesPanel : public vaUIPanel
    {
        const int                               m_panelIndex;

        std::vector< weak_ptr<vaUIPropertiesItem> >  m_items;
        int                                     m_currentItem = -1;

    public:
        vaUIPropertiesPanel( const string & name, int sortOrder, int panelIndex ) : vaUIPanel( name, sortOrder, !VA_MINIMAL_UI_BOOL, vaUIPanel::DockLocation::DockedRightBottom ), m_panelIndex( panelIndex ) { }

        const int                           UIPanelGetIndex( ) const                                    { return m_panelIndex; }

        virtual void                        UIPanelTick( vaApplicationBase & application ) override;

        void                                Select( const weak_ptr<vaUIPropertiesItem> & item );
        void                                Unselect( const weak_ptr<vaUIPropertiesItem> & item );
        bool                                IsSelected( const weak_ptr<vaUIPropertiesItem> & item );

    private:
        void                                RemoveNulls( );
    };

    // this is for panels that always share a master 'family' top panel
    class vaUIFamilyPanel : public vaUIPanel
    {
        std::vector<vaUIPanel *>                 m_currentList;
        string                              m_currentlySelectedName;

    public:
        vaUIFamilyPanel( const string & name, int sortOrder, vaUIPanel::DockLocation initialDock ) : vaUIPanel( name, sortOrder, true, initialDock ) { assert( UIPanelGetName() == name ); }  // can't have multiple classes with the same name?

        void                                Clear( )                    { m_currentList.clear(); };
        int                                 GetCount( ) const           { return (int)m_currentList.size(); }
        void                                Add( vaUIPanel * panel )    { m_currentList.push_back( panel ); }
        const std::vector<vaUIPanel *> &         GetList( ) const            { return m_currentList; }
        virtual void                        UIPanelTick( vaApplicationBase & application ) override;

        void                                SortAndUpdateVisibility( )  
        { 
            std::sort( m_currentList.begin(), m_currentList.end(), 
                [](const vaUIPanel * a, const vaUIPanel * b) -> bool { if( a->UIPanelGetSortOrder() == b->UIPanelGetSortOrder() ) return a->UIPanelGetName() < b->UIPanelGetName(); return a->UIPanelGetSortOrder() < b->UIPanelGetSortOrder(); }); 
            
            bool visible = false;
            for( auto panel : m_currentList ) 
            {
                if( panel->UIPanelIsVisible() ) visible = true;
                if( panel->UIPanelGetFocusNextFrame( ) )
                {
                    UIPanelSetFocusNextFrame( true );
                    visible = true;
                }
            }

            UIPanelSetVisible( visible );
        }

        virtual bool                        UIPanelIsDirty( ) const
        {
            bool isDirty = false;
            for( int i = 0; i < m_currentList.size(); i++ )
                isDirty |= m_currentList[i]->UIPanelIsDirty();
            return isDirty;
        }
    };

    // this is for a simple way to create property item UI just by using a lambda (see vaUIManager::CreateTransientPropertyItem)
    class vaUITransientPropertiesItem : public vaUIPropertiesItem
    {
        const string                        m_displayName;
        const std::function< bool( vaApplicationBase & application, const shared_ptr<void> & drawContext ) >
            m_drawCallback;
        const shared_ptr<void>              m_drawContext;
        bool                                m_scheduledForDelete = false;

    public:
        vaUITransientPropertiesItem( const string& displayName, const std::function< bool( vaApplicationBase & application, const shared_ptr<void> & drawContext ) >& drawCallback, const shared_ptr<void>& drawContext )
            : m_displayName( displayName ), m_drawCallback( drawCallback ), m_drawContext( drawContext ) { }

        ~vaUITransientPropertiesItem( ) { }

        bool                                IsScheduledForDelete( ) const   { return m_scheduledForDelete; }

        virtual string                      UIPropertiesItemGetDisplayName( ) const override { return m_displayName; }
        virtual void                        UIPropertiesItemTick( vaApplicationBase& application, bool openMenu, bool hovered )
        {
            assert( !m_scheduledForDelete ); openMenu; hovered;

            m_scheduledForDelete = !m_drawCallback( application, m_drawContext );
        }

        const shared_ptr<void>              GetDrawContext( ) const         { return m_drawContext; }
    };

    struct vaUIMRSWidgetGlobals
    {
        weak_ptr<vaUIMRSWidget>             CurrentlyActive;

        ImGuizmo::OPERATION                 Operation;
    };

    class vaUIMRSWidget
    {
        string                              m_displayName           = "Unnamed";
        bool                                m_lastActive            = false;
        vaBoundingBox                       m_localBounds;
        vaMatrix4x4                         m_parentWorldTransform;
        vaMatrix4x4                         m_parentWorldTransformInv;
        vaMatrix4x4                         m_initialLocalTransform;
        vaMatrix4x4                         m_currentWorldTransform;    // <- this is the one being edited by the widget
        int                                 m_ageFromExternalTick   = 0;

    public:
        //vaUIMRSWidget( const vaMatrix4x4 & parentWorldTransform, const vaMatrix4x4 & initialLocalTransform, const vaBoundingBox * boundingBox ) 
        //    : m_parentWorldTransform( parentWorldTransform ), m_parentWorldTransformInv( parentWorldTransform.Inversed( nullptr, true ) ),
        //        m_initialLocalTransform( initialLocalTransform ), m_currentWorldTransform( initialLocalTransform * parentWorldTransform ),
        //        m_localBounds( (boundingBox == nullptr)?(vaBoundingBox::Degenerate):(*boundingBox) ) { }

    public:
        bool                                TickInternal( vaApplicationBase & application, bool & active, vaUIMRSWidgetGlobals & globals );
        void                                TickExternal( const string & displayName, const vaMatrix4x4 & parentWorldTransform, vaMatrix4x4 & localTransform, const vaBoundingBox * boundingBox, bool & active );

        void                                SetDisplayName( const string & displayName )        { m_displayName = displayName; }

        const vaMatrix4x4 &                 GetInitialParentWorldTransform( ) const             { return m_parentWorldTransform; }
        const vaMatrix4x4 &                 GetInitialLocalTransform( ) const                   { return m_initialLocalTransform; }
        const vaMatrix4x4 &                 GetCurrentWorldTransform( ) const                   { return m_currentWorldTransform; }
    };
}

using namespace Vanilla;

int vaUIPropertiesItem::s_lastID = 0;

vaUIPropertiesItem::vaUIPropertiesItem( ) : m_uniqueID( vaStringTools::Format( "vaUIPropertiesItem_%d", ++s_lastID ) )
{
}

vaUIPropertiesItem::vaUIPropertiesItem( const vaUIPropertiesItem & item ) : m_uniqueID( vaStringTools::Format( "vaUIPropertiesItem_%d", ++s_lastID ) )
{
    item; // nothing to copy
//    assert( false ); // ?
}

string vaUIPanel::FindUniqueName( const string & name )
{
    string uniqueName = name;
    int counter = 0;
    while( vaUIManager::GetInstance().m_panels.find( uniqueName ) != vaUIManager::GetInstance().m_panels.end() )
    {
        counter++;
        uniqueName = vaStringTools::Format( "%s (%d)", name.c_str(), counter );
    }
    return uniqueName;
}

vaUIPanel::vaUIPanel( const string & name, int sortOrder, bool initialVisible, DockLocation initialDock, const string & familyName, const vaVector2 & initialSize ) : m_name( FindUniqueName( name ) ), m_sortOrder( sortOrder ), m_initialDock( initialDock ), m_visible( initialVisible ), m_familyName( familyName ), m_initialSize( initialSize )
{ 
    assert( vaThreading::IsMainThread() );

    assert( vaUIManager::GetInstancePtr() != nullptr );
    if( vaUIManager::GetInstancePtr( ) != nullptr )
    {
        assert( vaUIManager::GetInstance().m_panels.find( m_name ) == vaUIManager::GetInstance().m_panels.end() );
        vaUIManager::GetInstance().m_panels.insert( std::make_pair( m_name, this) );

#ifndef VA_MINIMAL_UI
        // ImGui doesn't remember visibility (by design?) so we have to save/load it ourselves
        UIPanelSetVisible( vaUIManager::GetInstance().FindInitialVisibility( m_name, initialVisible ) );
#endif
    }
}  

vaUIPanel::~vaUIPanel( )
{ 
    assert( vaThreading::IsMainThread() );
    assert( vaUIManager::GetInstancePtr() != nullptr );
    if( vaUIManager::GetInstancePtr( ) != nullptr )
    {
        auto it = vaUIManager::GetInstance().m_panels.find( m_name );
        if( it != vaUIManager::GetInstance().m_panels.end() )
            vaUIManager::GetInstance().m_panels.erase( it );
        else
            { assert( false ); }
    }
}


void vaUIPanel::UIPanelTickCollapsable( vaApplicationBase & application, bool showFrame, bool defaultOpen, bool indent )
{
    application; showFrame; defaultOpen; indent;
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    // needed so that any controls in obj.IHO_Draw() are unique
    ImGui::PushID( ("collapsable_"+m_name).c_str( ) );

    ImGuiTreeNodeFlags headerFlags = 0;
    headerFlags |= (showFrame)?(ImGuiTreeNodeFlags_Framed):(0);
    headerFlags |= (defaultOpen)?(ImGuiTreeNodeFlags_DefaultOpen):(0);

    if( ImGui::CollapsingHeader( UIPanelGetDisplayNameWithDirtyTag().c_str( ), headerFlags ) )
    {
        if( indent )
            ImGui::Indent();
        
        UIPanelTick( application );
        
        if( indent )
            ImGui::Unindent();
    }

    ImGui::PopID();



#endif
}

void vaUIPropertiesItem::UIPropertiesItemTickCollapsable( vaApplicationBase & application, bool showFrame, bool defaultOpen, bool indent )
{
    application; showFrame; defaultOpen; indent;
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    // needed so that any controls in obj.IHO_Draw() are unique
    ImGui::PushID( ("collapsable_"+m_uniqueID).c_str( ) );

    ImGuiTreeNodeFlags headerFlags = 0;
    headerFlags |= (showFrame)?(ImGuiTreeNodeFlags_Framed):(0);
    headerFlags |= (defaultOpen)?(ImGuiTreeNodeFlags_DefaultOpen):(0);

    if( ImGui::CollapsingHeader( UIPropertiesItemGetDisplayName().c_str( ), headerFlags ) )
    {
        if( indent )
            ImGui::Indent();
        
        UIPropertiesItemTick( application, false, false );
        
        if( indent )
            ImGui::Unindent();
    }

    ImGui::PopID();

#endif
}

void vaUIPropertiesItem::DrawList( vaApplicationBase & application, const char * stringID, vaUIPropertiesItem ** objList, const int objListCount, int & currentItem, float width, float listHeight, float selectedElementHeight )
{
    application; stringID; objList; objListCount; currentItem; width; listHeight; selectedElementHeight;
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    ImGui::PushID( stringID );

    if( ImGui::BeginChild( "PropList", ImVec2( 0.0f, listHeight ), true ) )
    {
        ImGuiTreeNodeFlags defaultFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

        int objectClickedIndex = -1;
        for( int i = 0; i < objListCount; i++ )
        {
            ImGuiTreeNodeFlags nodeFlags = defaultFlags | ((i == currentItem)?(ImGuiTreeNodeFlags_Selected):(0)) | ImGuiTreeNodeFlags_Leaf;
            bool nodeOpen = ImGui::TreeNodeEx( objList[i]->UIPropertiesItemGetUniqueID().c_str(), nodeFlags, objList[i]->UIPropertiesItemGetDisplayName().c_str() );
                
            if( ImGui::IsItemClicked() )
                objectClickedIndex = i;
 
            if( nodeOpen )
                ImGui::TreePop();
        }

        if( objectClickedIndex != -1 )
        {
            if( currentItem == objectClickedIndex )
                currentItem = -1;   // if already selected, de-select
            else
                currentItem = objectClickedIndex;
        }
    }
    ImGui::EndChild();

    if( currentItem < 0 || currentItem >= objListCount )
        selectedElementHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;

    if( ImGui::BeginChild( "PropFrame", ImVec2( width, selectedElementHeight ), true ) )
    {
        if( currentItem >= 0 && currentItem < (int)objListCount )
        {
            vaUIPropertiesItem & obj = *objList[currentItem];
            ImGui::PushID( obj.UIPropertiesItemGetUniqueID().c_str( ) );
            obj.UIPropertiesItemTick( application, false, false );
            ImGui::PopID();
        }
        else
        {
            ImGui::TextColored( ImVec4( 0.5f, 0.5f, 0.5f, 1.0f ), "Select an item to display properties" );
        }
    }
    ImGui::EndChild();
    ImGui::PopID( );
#endif // #ifdef VA_IMGUI_INTEGRATION_ENABLED
}

void vaUIPropertiesPanel::UIPanelTick( vaApplicationBase & application ) 
{
    application;
    RemoveNulls( );

    if( m_items.size() > 0 )
        m_currentItem = vaMath::Clamp( m_currentItem, 0, (int)m_items.size()-1 );
    else
        m_currentItem = -1;

    auto currentItem = (m_currentItem >= 0)?(m_items[m_currentItem].lock()):(nullptr);
    if( currentItem == nullptr )
        m_currentItem = -1;

    string thisPanel = std::to_string( m_panelIndex+1 );
    int otherPanelAI = (m_panelIndex == 0)?(1):(0);
    int otherPanelBI = (m_panelIndex == 2)?(1):(2);
    string otherPanelA = vaStringTools::Format( "->%d", otherPanelAI+1 );
    string otherPanelB = vaStringTools::Format( "->%d", otherPanelBI+1 );

#ifdef VA_IMGUI_INTEGRATION_ENABLED

    static float smallButtonsWidth = 0;

    VA_GENERIC_RAII_SCOPE( ImGui::PushID( (currentItem != nullptr)?(currentItem->UIPropertiesItemGetUniqueID().c_str()):(this->UIPanelGetName().c_str()) );, ImGui::PopID(); );

    bool openMenu = false;
    bool hovered  = false;

    if( m_currentItem == -1 )
        ImGui::Text( "<no selected item>" );
    else
    {
        auto text = vaStringTools::Format( "%s [...]", currentItem->UIPropertiesItemGetDisplayName().c_str() );
        openMenu = ImGui::Button( text.c_str(), { ImGui::GetContentRegionAvail().x - smallButtonsWidth - ImGui::GetStyle().ItemSpacing.x * 2, 0} );
        hovered  = ImGui::IsItemHovered();
    }

    switch( ImGuiEx_SameLineSmallButtons( "Local IBL", { "<<", "x", ">>", otherPanelA, otherPanelB }, 
        { m_currentItem <= 0, m_currentItem == -1, m_currentItem == ((int)m_items.size()-1), m_currentItem == -1, m_currentItem == -1 }, true,
        { "Switch to previous properties item on Properties panel "+thisPanel, "Close this properties item", "Switch to next properties item on Properties panel "+thisPanel, "Move this properties item to Properties panel " + std::to_string(otherPanelAI+1), "Move this properties item to Properties panel " + std::to_string(otherPanelBI+1) },
        &smallButtonsWidth ) )
    {
    case( -1 ): break;
    case( 0 ): m_currentItem--; break;
    case( 1 ): Unselect( currentItem ); break;
    case( 2 ): m_currentItem++; break;
    case( 3 ): Unselect( currentItem ); vaUIManager::GetInstance().SelectPropertyItem( currentItem, otherPanelAI ); break;
    case( 4 ): Unselect( currentItem ); vaUIManager::GetInstance().SelectPropertyItem( currentItem, otherPanelBI ); break;
    default: assert( false ); break;
    }

    ImGui::Separator();

    if( currentItem != nullptr )
        currentItem->UIPropertiesItemTick( application, openMenu, hovered );
#endif
}

void vaUIPropertiesPanel::Select( const weak_ptr<vaUIPropertiesItem> & _item )
{
    shared_ptr<vaUIPropertiesItem> item = _item.lock(); if( item == nullptr ) return;

    for( int i = 0; i < m_items.size(); i++ )
        if( m_items[i].lock() == item )
        {
            // already in? just select it
            m_currentItem = i;
            return;
        }

    // add to the list
    m_items.push_back( item );
    m_currentItem = (int)m_items.size() - 1;
    UIPanelSetFocusNextFrame();
}

void vaUIPropertiesPanel::Unselect( const weak_ptr<vaUIPropertiesItem> & _item )
{
    shared_ptr<vaUIPropertiesItem> item = _item.lock( ); if( item == nullptr ) return;

    for( int i = 0; i < m_items.size(); i++ )
        if( m_items[i].lock() == item )
            m_items[i].reset();
}

bool vaUIPropertiesPanel::IsSelected( const weak_ptr<vaUIPropertiesItem> & _item )
{
    shared_ptr<vaUIPropertiesItem> item = _item.lock( ); if( item == nullptr ) return false;

    for( int i = 0; i < m_items.size( ); i++ )
        if( m_items[i].lock( ) == item )
            return true;
    return false;
}

void vaUIPropertiesPanel::RemoveNulls( )
{
    for( int i = (int)m_items.size()-1; i >= 0; i-- )
        if( m_items[i].expired() )
            m_items.erase( m_items.begin() + i );
}

void vaUIFamilyPanel::UIPanelTick( vaApplicationBase & application )
{
    application;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui::PushID( UIPanelGetName().c_str() );

#if 0
    std::vector<string> list;
    int index = -1;
    for( int i = 0; i < (int)m_currentList.size(); i++ )
    {
        list.push_back( m_currentList[i]->UIPanelGetDisplayName() );
        if( m_currentlySelectedName == m_currentList[i]->UIPanelGetName() )
            index = i;
    }
    //ImGui::Text( "Asset packs in memory:" );
    ImGui::PushItemWidth(-1);
    ImGuiEx_ListBox( "list", index, list );
    ImGui::PopItemWidth();
    if( index >= 0 && index < m_currentList.size() )
    {
        m_currentlySelectedName = m_currentList[index]->UIPanelGetName();
        m_currentList[index]->UIPanelTick( aplication );
    }
#else

    ImGuiTabBarFlags tabBarFlags = 0;
    tabBarFlags |= ImGuiTabBarFlags_Reorderable;
    //tabBarFlags |= ImGuiTabBarFlags_AutoSelectNewTabs;
    tabBarFlags |= ImGuiTabBarFlags_NoCloseWithMiddleMouseButton;
    tabBarFlags |= ImGuiTabBarFlags_FittingPolicyScroll;    // ImGuiTabBarFlags_FittingPolicyResizeDown

    if (ImGui::BeginTabBar("FamilyTabBar", tabBarFlags))
    {
        auto drawTabPanel = [ & ]( int index, bool setSelected )
        { 
            auto panel = m_currentList[index];
            bool isVisible = panel->UIPanelIsVisible( );
            //if( isVisible )
            {
                int tabItemFlags = ( setSelected ) ? ( ImGuiTabItemFlags_SetSelected ) : ( 0 );
                string windowName = panel->UIPanelGetDisplayNameWithDirtyTag( ) + "###" + panel->UIPanelGetName( );

                //bool isDocked = false; isDocked;
                //ImGuiWindow * imWin = ImGui::FindWindowByName( windowName.c_str() );
                //if( imWin != nullptr )
                //    isDocked = imWin->DockIsActive;
                bool isDocked = true;

                if( ImGui::BeginTabItem( windowName.c_str( ), ( panel->UIPanelIsListed( ) && !isDocked ) ? ( &isVisible ) : ( nullptr ), tabItemFlags ) ) //ImGuiWindowFlags_NoBackground ) )
                {
                    panel->UIPanelTick( application );
                    ImGui::EndTabItem( );
                    m_currentlySelectedName = panel->UIPanelGetName( );
                }
                if( !setSelected )
                    panel->UIPanelSetVisible( isVisible );
            }
        };

        for( int i = 0; i < (int)m_currentList.size( ); i++ )
        {
            auto panel = m_currentList[i];
            if( ( m_currentlySelectedName == "" && i == 0 ) || panel->UIPanelGetFocusNextFrame( ) )
            {
                if( panel->UIPanelGetFocusNextFrame( ) )
                {
                    panel->UIPanelSetVisible( true );
                    panel->UIPanelSetFocusNextFrame( false );
                }
                drawTabPanel( i, true );
            }
            else
                drawTabPanel( i, false );
        }

        ImGui::EndTabBar();
    }

#endif

    ImGui::PopID();
#endif
}

vaUIManager::vaUIManager( )
{ 
    m_propPanels.push_back( std::make_shared<vaUIPropertiesPanel>("Properties 1", 10, 0) );
    m_propPanels.push_back( std::make_shared<vaUIPropertiesPanel>("Properties 2", 11, 1) );
    m_propPanels.push_back( std::make_shared<vaUIPropertiesPanel>("Properties 3", 12, 2) );

    m_mrsWidgetGlobals = std::make_shared<vaUIMRSWidgetGlobals>();
}
vaUIManager::~vaUIManager( )
{ 
}

void vaUIManager::SerializeSettings( vaXMLSerializer & serializer )
{
    serializer.Serialize<bool>( "Visible", m_visible );
    serializer.Serialize<bool>( "MenuVisible", m_menuVisible );
    serializer.Serialize<bool>( "ConsoleVisible", m_consoleVisible );

    serializer.Serialize<uint32>( "ImGuiDockSpaceIDLeft",           m_dockSpaceIDLeft        );
    serializer.Serialize<uint32>( "ImGuiDockSpaceIDLeftBottom",     m_dockSpaceIDLeftBottom  );
    serializer.Serialize<uint32>( "ImGuiDockSpaceIDRight",          m_dockSpaceIDRight       );
    serializer.Serialize<uint32>( "ImGuiDockSpaceIDRightBottom",    m_dockSpaceIDRightBottom );

    // ImGui doesn't remember visibility (by design?) so we have to save/load it ourselves
    if( serializer.IsWriting() )
    {
        std::vector<pair<string, bool>>  panelVisibility;
        for( auto it : m_panels )
        {
            assert( it.first == it.second->UIPanelGetName() );
            panelVisibility.push_back( make_pair( it.first, it.second->UIPanelIsVisible() ) );
        }
        serializer.SerializeArray<pair<string, bool>>( "PanelVisibility", panelVisibility );
    }
    else
    {
        m_initiallyPanelVisibility.clear();
        serializer.SerializeArray<pair<string, bool>>( "PanelVisibility", m_initiallyPanelVisibility );
#ifndef VA_MINIMAL_UI
        for( auto it : m_panels )
            it.second->UIPanelSetVisible( FindInitialVisibility( it.first, it.second->UIPanelIsVisible() ) );
#endif
    }
}

bool vaUIManager::FindInitialVisibility( const string & panelName, bool defVal )
{
    for( auto & it : m_initiallyPanelVisibility )
        if( it.first == panelName )
            return it.second;
    return defVal;
}

void vaUIManager::UpdateUI( bool appHasFocus )
{
    if( appHasFocus )
    {
        if( !vaInputMouseBase::GetCurrent( )->IsCaptured( ) )
        {
            // I guess this is good place as any... it must not happen between ImGuiNewFrame and Draw though!
            if( vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( KK_F1 ) && !vaInputKeyboardBase::GetCurrent( )->IsKeyDownOrClicked( KK_CONTROL ) )
                m_visible = !m_visible;

            if( vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( KK_F1 ) && vaInputKeyboardBase::GetCurrent( )->IsKeyDownOrClicked( KK_CONTROL ) )
                m_menuVisible = !m_menuVisible;

            if( vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( KK_OEM_3 ) )
                // m_consoleVisible = !m_consoleVisible;
                vaUIConsole::GetInstance().SetOpen( !vaUIConsole::GetInstance().IsOpen() );
        }
    }
}

void vaUIManager::TickUI( vaApplicationBase & application )
{
    VA_TRACE_CPU_SCOPE( vaUIManager_TickUI );

    // some default style stuff
    ImGuiStyle & style = ImGui::GetStyle();
    style.IndentSpacing = (ImGui::GetFontSize() + style.FramePadding.x*2) * 0.5f;

    VA_GENERIC_RAII_SCOPE( assert(!m_inTickUI); m_inTickUI = true;, assert(m_inTickUI); m_inTickUI = false; );

    UpdateUI( application.HasFocus() );

    // call UIPanelTickAlways even if panels are invisible so we can handle keyboard inputs
    for( auto it = m_panels.begin( ); it != m_panels.end( ); it++ )
        it->second->UIPanelTickAlways( application );

#ifdef VA_IMGUI_INTEGRATION_ENABLED
    bool visible = m_visible;
    bool menuVisible = m_menuVisible;
    bool consoleVisible = m_consoleVisible;
#ifdef VA_MINIMAL_UI
    menuVisible = false;
    //consoleVisible = false;
#endif

    if( m_delayUIFewFramesDirtyHack > 0 )
    {
        m_delayUIFewFramesDirtyHack--;
        return;
    }

    if( m_visibilityOverrideCallback )
        m_visibilityOverrideCallback( visible, menuVisible, consoleVisible );

    if( !visible )
        return;

    // see ImGui::ShowDemoWindow(); / ShowExampleAppDockSpace
    if( m_showImGuiDemo )
        ImGui::ShowDemoWindow();

    // The negative constant on the right is how wide the text to the right of the controls will be; bigger number - more text can fit (but less left for the data)
    float styleItemWidth = ImGui::GetFontSize( ) * -10;

    if( m_panels.size() > 0 )
    {
        // collect panels
        std::vector<vaUIPanel *> panels;
        for( auto it : m_panels )
	        panels.push_back( it.second );

        // collect family panels
        {
            // reset subpanels collected by family panels
            for( int i = 0; i < m_familyPanels.size(); i++ ) 
                m_familyPanels[i]->Clear();

            // collect subpanels into corresponding family panels (and create new family panels if none exists)
            for( int i = 0; i < (int)panels.size(); i++ )
            {
                vaUIPanel * panel = panels[i];
                if( panel->UIPanelGetFamily() == "" || !panel->UIPanelIsListed() )
                    continue;

                bool found = false;

                for( auto familyPanel : m_familyPanels ) 
                    if( panel->UIPanelGetFamily() == familyPanel->UIPanelGetName() )
                    {
                        familyPanel->Add( panel );
                        found = true;
                        break;
                    }
                if( !found )
                {
                    m_familyPanels.push_back( std::make_shared<vaUIFamilyPanel>(panel->UIPanelGetFamily(), panel->UIPanelGetSortOrder(), panel->UIPanelGetInitialDock() ) );
                    m_familyPanels.back()->Add( panel );
                    panels.push_back( m_familyPanels.back().get() );    // have to update 'panels' too so we get picked up first frame
                }
            }

            // remove all panels from the main list that now belong to a family panel (since they're managed by them now, both for the menu and for the windows contents)
            for( int i = ((int)panels.size())-1; i>= 0; i-- )
                if( panels[i]->UIPanelGetFamily() != "" )
                    panels.erase( panels.begin()+i );

            // finally either remove empty family panels or let them sort their collected subpanels for later display (for correct menu and tab ordering)
            for( int i = ((int)m_familyPanels.size())-1; i>= 0; i-- )
            {
                if( m_familyPanels[i]->GetCount() == 0 )
                {
                    // first remove from the list of panels
                    for( int j = 0; j < panels.size(); j++ )
                        if( panels[j] == static_cast<vaUIPanel*>(m_familyPanels[i].get()) )
                        {
                            panels.erase( panels.begin()+j );
                            break;
                        }
                    // then remove from the list of family panels
                    m_familyPanels.erase( m_familyPanels.begin()+i );
                }
                else
                    m_familyPanels[i]->SortAndUpdateVisibility();
            }
        }

        // sort collected panels
        std::sort( panels.begin(), panels.end(), [](const vaUIPanel * a, const vaUIPanel * b) -> bool { if( a->UIPanelGetSortOrder() == b->UIPanelGetSortOrder() ) return a->UIPanelGetName() < b->UIPanelGetName(); return a->UIPanelGetSortOrder() < b->UIPanelGetSortOrder(); });

        // style stuff
        ImVec4 colorsDockspaceBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg); //( 0.0f, 0.0f, 0.0f, 0.5f );

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ImGuiDockNodeFlags dockNodeFlags = ImGuiDockNodeFlags_None;
        bool fullscreen = true;
    
        //dockNodeFlags |=  ImGuiDockNodeFlags_NoSplit;
        dockNodeFlags |=  ImGuiDockNodeFlags_NoDockingInCentralNode;
        //dockNodeFlags |=  ImGuiDockNodeFlags_NoResize;
        dockNodeFlags |=  ImGuiDockNodeFlags_PassthruCentralNode;
        //dockNodeFlags |=  ImGuiDockNodeFlags_CentralNode;
        //dockNodeFlags |=  ImGuiDockNodeFlags_DockSpace;
        // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
        // because it would be confusing to have two docking targets within each others.
        ImGuiWindowFlags dockWindowFlags = ImGuiWindowFlags_NoDocking;
        if( menuVisible )
            dockWindowFlags |= ImGuiWindowFlags_MenuBar ;
        if( fullscreen )
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            dockWindowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            dockWindowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }

        // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background and handle the pass-thru hole, so we ask Begin() to not render a background.
        if (dockNodeFlags & ImGuiDockNodeFlags_PassthruCentralNode)
            dockWindowFlags |= ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
        ImGui::PushStyleColor(ImGuiCol_WindowBg, colorsDockspaceBg);
        ImVec4 menuCol = ImGui::GetStyleColorVec4( ImGuiCol_MenuBarBg );
        menuCol.w = 0.75f;
        ImGui::PushStyleColor( ImGuiCol_MenuBarBg, menuCol );

        ImGui::Begin("VAUIRootDockspaceWindow", nullptr, dockWindowFlags);
        ImGui::PopStyleVar();
        if( fullscreen )
            ImGui::PopStyleVar(2);

        ImGui::PushItemWidth( styleItemWidth );           // Use fixed width for labels (by passing a negative value), the rest goes to widgets. We choose a width proportional to our font size.

        auto getPanelImguiDockSpaceID = [ & ]( const vaUIPanel* panel ) -> uint32
        {
            switch( panel->UIPanelGetInitialDock( ) )
            {
            case vaUIPanel::DockLocation::NotDocked:            return (uint32)-1; break;
            case vaUIPanel::DockLocation::DockedLeft:           return m_dockSpaceIDLeft;         break;
            case vaUIPanel::DockLocation::DockedLeftBottom:     return m_dockSpaceIDLeftBottom;   break;
            case vaUIPanel::DockLocation::DockedRight:          return m_dockSpaceIDRight;        break;
            case vaUIPanel::DockLocation::DockedRightBottom:    return m_dockSpaceIDRightBottom;  break;
            default: assert( false ); return (uint32)-1; break;
            };
        };

        // A programmatic initialization of docking windows when imgui.ini not available (app first run)
        // based on: https://github.com/ocornut/imgui/issues/2109#issuecomment-426204357
        {
            m_dockSpaceIDRoot = ImGui::GetID("VAUIRootDockspace");
            bool initFirstTime = false;

            // a hack but what can you do
            if( 
                ( ImGui::DockBuilderGetNode( m_dockSpaceIDLeft        ) == nullptr ) ||
                ( ImGui::DockBuilderGetNode( m_dockSpaceIDLeftBottom  ) == nullptr ) ||
                ( ImGui::DockBuilderGetNode( m_dockSpaceIDRight       ) == nullptr ) ||
                ( ImGui::DockBuilderGetNode( m_dockSpaceIDRightBottom ) == nullptr ) )
                initFirstTime = true;

            if( initFirstTime || ImGui::DockBuilderGetNode( m_dockSpaceIDRoot ) == nullptr ) // execute only first time
            {
                initFirstTime = true;

                ImGui::DockBuilderRemoveNode(m_dockSpaceIDRoot);
		        ImGui::DockBuilderAddNode(m_dockSpaceIDRoot, dockNodeFlags | ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(m_dockSpaceIDRoot, ImGui::GetMainViewport()->Size);

                ImGuiID dock_main_id        = m_dockSpaceIDRoot;
                m_dockSpaceIDLeft           = ImGui::DockBuilderSplitNode(dock_main_id,         ImGuiDir_Left, 0.20f, nullptr, &dock_main_id);
                m_dockSpaceIDLeftBottom     = ImGui::DockBuilderSplitNode(m_dockSpaceIDLeft,    ImGuiDir_Down, 0.20f, nullptr, &m_dockSpaceIDLeft);
                m_dockSpaceIDRight          = ImGui::DockBuilderSplitNode(dock_main_id,         ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
                m_dockSpaceIDRightBottom    = ImGui::DockBuilderSplitNode(m_dockSpaceIDRight,   ImGuiDir_Down, 0.20f, nullptr, &m_dockSpaceIDRight);

                for( auto panel : panels )
                {
                    if( !panel->UIPanelIsListed() )
                        continue;
                    string windowName = (panel->UIPanelGetDisplayNameWithDirtyTag( )+"###"+panel->UIPanelGetName());
                    uint32 dockID = getPanelImguiDockSpaceID( panel );
                    if( dockID != (uint32)-1 )
                        ImGui::DockBuilderDockWindow( windowName.c_str(), dockID );
                }

                ImGui::DockBuilderFinish( m_dockSpaceIDRoot );
            }
            else
            {
                assert( ImGui::DockBuilderGetNode( m_dockSpaceIDLeft        ) != nullptr );
                assert( ImGui::DockBuilderGetNode( m_dockSpaceIDLeftBottom  ) != nullptr );
                assert( ImGui::DockBuilderGetNode( m_dockSpaceIDRight       ) != nullptr );
                assert( ImGui::DockBuilderGetNode( m_dockSpaceIDRightBottom ) != nullptr );
            }
        }

        ImGui::DockSpace( m_dockSpaceIDRoot, ImVec2(0.0f, 0.0f), dockNodeFlags );

        if( menuVisible )
        {
            if( ImGui::BeginMenuBar() )
            {
                if( ImGui::BeginMenu("File") )
                {
                    //for( auto panel : panels )
                    //    panel->UIPanelFileMenuItems();

                    if( ImGui::MenuItem("Quit") )
                        vaCore::SetAppSafeQuitFlag(true);
                    ImGui::EndMenu();
                }
                if( ImGui::BeginMenu("View") )
                {
                    for( auto panel : panels )
                    {
                        if( !panel->UIPanelIsListed() )
                            continue;

                        vaUIFamilyPanel * familyPanel = dynamic_cast<vaUIFamilyPanel *>(panel);

                        if( familyPanel == nullptr )
                        {
                            bool isVisible = panel->UIPanelIsVisible();
                            if( ImGui::MenuItem( (panel->UIPanelGetDisplayNameWithDirtyTag( )+"###"+panel->UIPanelGetName()).c_str( ), "", &isVisible ) && isVisible )
                            {
                                panel->UIPanelSetFocusNextFrame( true );
                            }
                            panel->UIPanelSetVisible( isVisible );
                        }
                        else
                        {
                            if( ImGui::BeginMenu( (panel->UIPanelGetDisplayNameWithDirtyTag( )+"###"+panel->UIPanelGetName()).c_str( ) ) )
                            {
                                for( auto subPanel : familyPanel->GetList() )
                                {
                                    bool isVisible = subPanel->UIPanelIsVisible();
                                    if( ImGui::MenuItem( (subPanel->UIPanelGetDisplayNameWithDirtyTag( )+"###"+subPanel->UIPanelGetName()).c_str( ), "", &isVisible ) && isVisible )
                                    {
                                        subPanel->UIPanelSetFocusNextFrame( true );
                                        panel->UIPanelSetFocusNextFrame( true );
                                    }
                                    subPanel->UIPanelSetVisible( isVisible );
                                }
                                ImGui::EndMenu();
                            }
                        }
                    }

                    ImGui::EndMenu();
                }

                for( int i = (int)m_userMenus.size( ) - 1; i >= 0; i-- )
                {
                    shared_ptr<void> aliveToken = m_userMenus[i].AliveToken.lock();
                    if( aliveToken == nullptr )
                        m_userMenus.erase( m_userMenus.begin() + i );
                    else
                    {
                        if( ImGui::BeginMenu( m_userMenus[i].Title.c_str() ) )
                        {
                            m_userMenus[i].Handler( application );
                            ImGui::EndMenu();
                        }
                    }
                }

                // if (ImGui::BeginMenu("Help"))
                // {
                //     ImGui::MenuItem("ImGui::ShowDemoWindow", "", &m_showImGuiDemo);
                //     ImGui::Separator();
                //     if( ImGui::MenuItem("Yikes!",                "" ) ) {  }
                //     ImGui::EndMenu();
                // }

                //////////////////////////////////////////////////////////////////////////
                // just example code - remove
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                    ImGui::TextUnformatted( "Use F1 to show/hide UI" );
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                // just example code - remove
                //////////////////////////////////////////////////////////////////////////

                ImGui::EndMenuBar();
            }
        }

        ImGui::PopItemWidth( );

        ImGui::End();

        ImGui::PopStyleColor(); // ImGui::PushStyleColor(ImGuiCol_WindowBg, colorsDockspaceBg);

        // this is for panels when docked
        ImGui::PushStyleColor(ImGuiCol_ChildBg, (ImU32)0);
        // this is for panels when not docked
        //ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImU32)0);

        for( auto it = panels.begin(); it < panels.end(); it++ )
        {
            auto panel = *it;
            bool isVisible = panel->UIPanelIsVisible();
            if( isVisible )
            {
                ImGui::SetNextWindowSize( ImFromVA( panel->UIPanelGetInitialSize() ), ImGuiCond_Once );

                string windowName = ( panel->UIPanelGetDisplayNameWithDirtyTag( ) + "###" + panel->UIPanelGetName( ) );

                bool isDocked = false; isDocked;
                ImGuiWindow* imWin = ImGui::FindWindowByName( windowName.c_str( ) );
                if( imWin != nullptr )
                    isDocked = imWin->DockIsActive;
                else
                {
                    isDocked = panel->UIPanelGetInitialDock( ) != vaUIPanel::DockLocation::NotDocked;
                    uint32 dockID = getPanelImguiDockSpaceID( panel );
                    if( dockID != (uint32)-1 )
                    {
                        ImGui::SetNextWindowDockID( dockID, ImGuiCond_None );
                        assert( isDocked );
                    }
                    else
                    { assert( !isDocked ); }
                }
                
                if( ImGui::Begin( windowName.c_str( ), (panel->UIPanelIsListed() && !isDocked)?(&isVisible):(nullptr), ImGuiWindowFlags_NoFocusOnAppearing ) ) //ImGuiWindowFlags_NoBackground ) )
                {
                    ImGui::PushItemWidth( styleItemWidth );           // Use fixed width for labels (by passing a negative value), the rest goes to widgets. We choose a width proportional to our font size.
                    // panel->UIPanelSetDocked( ImGui::IsWindowDocked( ) );
                    vaUIPropertiesPanel * UIPropsPanel = dynamic_cast<vaUIPropertiesPanel *>(panel);
                    assert( m_propPanelCurrentlyDrawn == -1 );
                    if( UIPropsPanel )
                        m_propPanelCurrentlyDrawn = UIPropsPanel->UIPanelGetIndex();
                    panel->UIPanelTick( application );
                    m_propPanelCurrentlyDrawn = -1;
                    ImGui::PopItemWidth( );
                }
                ImGui::End( );
                panel->UIPanelSetVisible( isVisible );
            }
        }
        // set focus to those who requested and/or those with lowest sort order during the first frame
        for( auto it = panels.rbegin(); it < panels.rend(); it++ )
        {
            auto panel = *it;
            bool isVisible = panel->UIPanelIsVisible();
            if( (isVisible && m_firstFrame) || panel->UIPanelGetFocusNextFrame() )
            {
                ImGui::SetWindowFocus( (panel->UIPanelGetDisplayNameWithDirtyTag( )+"###"+panel->UIPanelGetName()).c_str( ) );
                panel->UIPanelSetFocusNextFrame( false );
                panel->UIPanelSetVisible( true );
            }
        }
        m_firstFrame = false;

        ImGui::PopStyleColor();

        ImGui::PopStyleColor( ); // for ImGuiCol_MenuBarBg

    }

    //ImGui::PushItemWidth( styleItemWidth );           // Use fixed width for labels (by passing a negative value), the rest goes to widgets. We choose a width proportional to our font size.

                                                      // handle 'transient' properties - delete those no longer needed
    for( auto it = m_transientProperties.cbegin( ); it != m_transientProperties.cend( ) /* not hoisted */; /* no increment */ )
    {
        auto currIt = it;
        it++;
        auto tpanel = currIt->second; //.lock();
        if( tpanel == nullptr || tpanel->IsScheduledForDelete() || !IsPropertyItemSelected( tpanel ) )
            m_transientProperties.erase(currIt);
    }

    //ImGui::PopItemWidth( );

    // handle MrsWidget
    {
        for( auto it = m_mrsWidgets.cbegin( ); it != m_mrsWidgets.cend( ) /* not hoisted */; /* no increment */ )
        {
            auto currIt = it;
            it++;
            bool currentlyActive = m_mrsWidgetGlobals->CurrentlyActive.lock( ) == currIt->second;
            bool wasActive = currentlyActive;
            bool shouldRemove = currIt->second->TickInternal( application, currentlyActive, *m_mrsWidgetGlobals );
            if( shouldRemove )
                m_mrsWidgets.erase( currIt );
            else 
            {
                if( !wasActive && currentlyActive )
                    m_mrsWidgetGlobals->CurrentlyActive = currIt->second;
                else if( wasActive && !currentlyActive )
                        m_mrsWidgetGlobals->CurrentlyActive.reset();
            }
        }
    }
    // m_mrsWidgetGlobals

    // console & log at the bottom
    if( consoleVisible )
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        vaUIConsole::GetInstance().Draw( (int)viewport->Size.x, (int)viewport->Size.y );
    }

    // loading bar progress
    vaBackgroundTaskManager::GetInstance().InsertImGuiWindow();
#endif
}

void vaUIManager::SelectPropertyItem( const weak_ptr<vaUIPropertiesItem> & _item, int preferredPropPanel )
{
    shared_ptr<vaUIPropertiesItem> item = _item.lock( ); if( item == nullptr ) return;

    if( preferredPropPanel < 0 )
        preferredPropPanel = (m_propPanelCurrentlyDrawn+1)%m_propPanels.size();
    preferredPropPanel = vaMath::Clamp( preferredPropPanel, 0, (int)m_propPanels.size()-1 );
    m_propPanels[preferredPropPanel]->Select( item );
}

void vaUIManager::UnselectPropertyItem( const weak_ptr<vaUIPropertiesItem> & _item )
{
    shared_ptr<vaUIPropertiesItem> item = _item.lock( ); if( item == nullptr ) return;

    for( int i = 0; i < m_propPanels.size(); i++ )
        m_propPanels[i]->Unselect( item );
}

bool vaUIManager::IsPropertyItemSelected( const weak_ptr<vaUIPropertiesItem> & _item )
{
    shared_ptr<vaUIPropertiesItem> item = _item.lock( ); if( item == nullptr ) return false;

    for( int i = 0; i < m_propPanels.size( ); i++ )
        if( m_propPanels[i]->IsSelected( item ) )
            return true;

    return false;
}

// void vaUIManager::SelectTransientPropertyItem( const string & uniqueID )
// {
//     auto it = m_transientProperties.find(uniqueID);
//     if( it != m_transientProperties.end() )
//     {
//         auto tp = it->second.lock();
//         if( tp != nullptr )
//             SelectPropertyItem( tp );
//     }
// }

shared_ptr<void> vaUIManager::FindTransientPropertyItem( const string & uniqueID, bool focusIfFound )  
{ 
    //assert( !m_inTickUI ); // not allowed because the function can actually delete the item
    auto it = m_transientProperties.find(uniqueID);
    if( it == m_transientProperties.end() )
        return nullptr;

    auto tp = it->second; //.lock();
    if( tp == nullptr || tp->IsScheduledForDelete() )
    {
        // m_transientProperties.erase( it );
        return nullptr;
    }
    if( focusIfFound )
    {
        for( int i = 0; i < m_propPanels.size( ); i++ )
            if( m_propPanels[i]->IsSelected( tp ) )
                m_propPanels[i]->UIPanelSetFocusNextFrame();
    }

    return tp->GetDrawContext( );
}

void vaUIManager::CreateTransientPropertyItem( const string & uniqueID, const string & displayName, const std::function< bool( vaApplicationBase & application, const shared_ptr<void> & drawContext ) > & drawCallback, const shared_ptr<void> & drawContext, int preferredPropPanel )
{
    auto ret = m_transientProperties.insert( { uniqueID, {} } );

    if( !ret.second )
    {
        assert( false ); // put a breakpoint in vaUITransientPropertiesItem destructor below to see if the old one gets deleted
    }
    auto tp = std::make_shared<vaUITransientPropertiesItem>( displayName, drawCallback, drawContext );
    ret.first->second = tp;
    SelectPropertyItem( tp, preferredPropPanel );
}

void vaUIManager::RegisterMenuItemHandler( const string & title, const shared_ptr<void> & aliveToken, const std::function<void( vaApplicationBase & )> & handler )
{
    auto it = std::find_if( m_userMenus.begin(), m_userMenus.end(), [&title]( const MenuItem & item ) { return title == item.Title; } );
    if( it != m_userMenus.end() )
    {
        VA_WARN( "vaUIManager::RegisterMenuItemHandler() - Menu handler '%s' already exists - deleting the old one, adding the new", title.c_str() );
        m_userMenus.erase(it);
    }
    m_userMenus.push_back( {title, aliveToken, handler} );

    std::sort( m_userMenus.begin(), m_userMenus.end(), [&title]( const MenuItem & left, const MenuItem & right ) { return left.Title < right.Title; } );
}

void vaUIManager::UnregisterMenuItemHandler( const string & title )
{
    auto it = std::find_if( m_userMenus.begin(), m_userMenus.end(), [&title]( const MenuItem & item ) { return title == item.Title; } );
    if( it != m_userMenus.end() )
        m_userMenus.erase( it );
}

bool vaUIMRSWidget::TickInternal( vaApplicationBase & application, bool & active, vaUIMRSWidgetGlobals & globals )
{
    auto & canvas2D = application.GetCanvas2D( );
    auto & canvas3D = application.GetCanvas3D( ); canvas3D;
    auto & camera   = application.GetUICamera( );
    auto mouse      = vaInputMouseBase::GetCurrent( );

    auto widgetPos = camera.WorldToScreen( m_currentWorldTransform.GetTranslation() );
    float circleSize = 6.0f * application.GetUIScaling();

    if( active )
    {
        //canvas2D.DrawCircle( widgetPos.x, widgetPos.y, circleSize, 0x80808080 );
        auto view = camera.GetViewMatrix( );
        auto proj = camera.GetProjMatrix( ); // ComputeNonReversedZProjMatrix( );

        //1.) ok draw most basic imgui control window here (see background thread and ibl stuff)
        //2.) only move/rotate/scale options for now
        //3.) only 'Cancel' needed, move is accepted by default otherwise

        ImGuiIO& io = ImGui::GetIO( );

        float lineHeight = ImGui::GetFrameHeightWithSpacing( );
        //float lineHeight  = ImGui::GetFrameHeight();
        ImVec2 windowSize = ImVec2( 900.0f, lineHeight * 3.5f
#ifdef VISUAL_DEBUGGING_ENABLED
            + 70.0f
#endif
        );

        ImGui::SetNextWindowPos( ImVec2( io.DisplaySize.x / 2.0f - windowSize.x / 2.0f, io.DisplaySize.y - windowSize.y - 2.0f * ImGui::GetStyle().FramePadding.y ), ImGuiCond_Always );
        ImGui::SetNextWindowSize( ImVec2( windowSize.x, windowSize.y ), ImGuiCond_Always );

        ImGuiWindowFlags windowFlags = 0;
        windowFlags |= ImGuiWindowFlags_NoResize;
        windowFlags |= ImGuiWindowFlags_NoMove                  ;
        windowFlags |= ImGuiWindowFlags_NoScrollbar;
        windowFlags |= ImGuiWindowFlags_NoScrollWithMouse;
        windowFlags |= ImGuiWindowFlags_NoCollapse;
        //windowFlags |= ImGuiWindowFlags_NoInputs                ;
        windowFlags |= ImGuiWindowFlags_NoFocusOnAppearing;
        //windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus   ;
        windowFlags |= ImGuiWindowFlags_NoDocking;
        windowFlags |= ImGuiWindowFlags_NoSavedSettings;

        string title = "Move Rotate Scale tool: '" + m_displayName +"'###MRSTool";

        if( ImGui::Begin( title.c_str(), nullptr, windowFlags ) )
        {
            ImGui::PushItemWidth( windowSize.x / 5.0f );
            
            bool hadChanges = ImGuiEx_Transform( "MRSTool", m_currentWorldTransform, true, false ); hadChanges;

            ImGui::SameLine( ); ImGuiEx_VerticalSeparator( ); ImGui::SameLine( );

            if( ImGui::Button( "Revert changes", { -1, 0 } ) )
                m_currentWorldTransform = m_initialLocalTransform * m_parentWorldTransform;

            ImGui::Combo( "Tool mode", (int*)&globals.Operation, "Move\0Rotate\0Scale\0\0" );

            ImGui::SameLine( ); 
            ImGui::Text( "Make the rot/pos/scale vertical; use two column setup; use radio buttons for rot/pos/scale mode selection on the right and separate local/world" ); 

            ImGui::PopItemWidth( );
        }
        ImGui::End( );

        if( ImGui::GetIO().MousePos.x != -FLT_MAX )
            ImGuizmo::Manipulate( &view._11, &proj._11, camera.GetUseReversedZ(), ( ImGuizmo::OPERATION )globals.Operation, ImGuizmo::WORLD, &m_currentWorldTransform._11 ); //, NULL, useSnap ? &snap.x : NULL );

        if( !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() && !ImGui::GetIO().WantCaptureMouse && ImGui::IsMouseClicked(0) )
            active = false;
    }
    else
    {
        bool highlighted = !mouse->IsCaptured( ) && ((mouse->GetCursorClientPosf()-widgetPos).Length() < circleSize);

        if( highlighted )
        {
            canvas2D.DrawCircle( widgetPos.x, widgetPos.y, circleSize - 1.5f, 0xFF000000 );
            canvas2D.DrawCircle( widgetPos.x, widgetPos.y, circleSize + 1.5f, 0xFF000000 );
            canvas2D.DrawCircle( widgetPos.x, widgetPos.y, circleSize - 0.5f, 0xFF80FFFF );
            canvas2D.DrawCircle( widgetPos.x, widgetPos.y, circleSize + 0.0f, 0xFFFFFFFF );
            canvas2D.DrawCircle( widgetPos.x, widgetPos.y, circleSize + 0.5f, 0xFFFFFF80 );

            if( mouse->IsKeyClicked(MK_Left) )
                active = true;
        }
        canvas2D.DrawCircle( widgetPos.x, widgetPos.y, circleSize - 1.0f, 0x80FFFFFF );
        canvas2D.DrawCircle( widgetPos.x, widgetPos.y, circleSize - 0.5f, 0x80FFFFFF );
        canvas2D.DrawCircle( widgetPos.x, widgetPos.y, circleSize + 0.0f, 0x80000000 );
        canvas2D.DrawCircle( widgetPos.x, widgetPos.y, circleSize + 0.5f, 0x80FFFFFF );
        canvas2D.DrawCircle( widgetPos.x, widgetPos.y, circleSize + 1.0f, 0x80FFFFFF );
    }

    m_ageFromExternalTick++;
    return m_ageFromExternalTick > 1;   // should remove if older than this
}

void vaUIMRSWidget::TickExternal( const string & displayName, const vaMatrix4x4 & parentWorldTransform, vaMatrix4x4 & localTransform, const vaBoundingBox * boundingBox, bool & active )
{
    m_displayName = displayName;
    m_ageFromExternalTick = 0;

    m_parentWorldTransform      = parentWorldTransform;
    m_parentWorldTransformInv   = parentWorldTransform.Inversed( nullptr, true );
    m_localBounds = ( boundingBox == nullptr ) ? ( vaBoundingBox::Degenerate ) : ( *boundingBox );

    auto reset = [ & ]( )
    {
        m_initialLocalTransform     = localTransform;
        m_currentWorldTransform     = localTransform * parentWorldTransform;
    };

    if( !m_lastActive && active )
    {
        reset( );
        m_lastActive                = true;
    }

    if( active )
        localTransform = m_currentWorldTransform * m_parentWorldTransformInv;
    else
    {
        reset( );
        m_lastActive = false;
    }
}

bool vaUIManager::MoveRotateScaleWidget( const string & uniqueID, const string & displayName, const vaMatrix4x4 & parentWorldTransform, vaMatrix4x4 & localTransform, vaMRSWidgetFlags flags, const vaBoundingBox * localBounds )
{
    auto it = m_mrsWidgets.find( uniqueID );
    if( it == m_mrsWidgets.end( ) )
    {
        auto added  = m_mrsWidgets.insert( {uniqueID, std::make_shared<vaUIMRSWidget>( )} );
        if( !added.second )
        {
            assert( false );    // well, this shouldn't have happened since we've just checked with a ::find() that no elements with the uniqueID are in?
            return false;
        }
        it = added.first;
        if( (flags & vaMRSWidgetFlags::FocusOnAppear) != 0 )
            m_mrsWidgetGlobals->CurrentlyActive = it->second;
    }
    else
    {
        if( (flags & vaMRSWidgetFlags::FocusNow) != 0 )
            m_mrsWidgetGlobals->CurrentlyActive = it->second;
    }

    bool currentlyActive = m_mrsWidgetGlobals->CurrentlyActive.lock() == it->second;
    it->second->TickExternal( displayName, parentWorldTransform, localTransform, localBounds, currentlyActive );
    if( currentlyActive )
        m_mrsWidgetGlobals->CurrentlyActive.lock() = it->second;
    return currentlyActive;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

vaUIConsole::vaUIConsole( )
{ 
    m_scrollToBottom = true;
    m_consoleOpen = false;
    m_consoleWasOpen = false;
    memset( m_textInputBuffer, 0, sizeof(m_textInputBuffer) );

    m_commands.push_back( CommandInfo( "HELP" ) );
    m_commands.push_back( CommandInfo( "HISTORY" ) );
    m_commands.push_back( CommandInfo( "CLEAR" ) );
    m_commands.push_back( CommandInfo( "QUIT" ) );
    //m_commands.push_back("CLASSIFY");  // "classify" is here to provide an example of "C"+[tab] completing to "CL" and displaying matches.
}

vaUIConsole::~vaUIConsole( )
{
}

void vaUIConsole::Draw( int windowWidth, int windowHeight )
{
    assert( vaUIManager::GetInstance().IsConsoleVisible() );

    windowHeight; windowWidth;
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    // Don't allow log access to anyone else while this is drawing
    vaRecursiveMutexScopeLock lock( vaLog::GetInstance( ).Mutex() );

    const float timerSeparatorX = 80.0f;
    const int linesToShowMax = 20; // (m_consoleOpen)?(25):(15); <- this variable approach doesn't work well due to scrolling mismatch

    const float secondsToShow = 8.0f;
    int showFrom = vaLog::GetInstance( ).FindNewest( secondsToShow );
    int showCount = vaMath::Min( linesToShowMax, (int)vaLog::GetInstance( ).Entries( ).size( ) - showFrom );
    if( m_consoleOpen )
    {
        showFrom = (int)vaLog::GetInstance( ).Entries( ).size( ) - linesToShowMax - 1; // vaMath::Max( 0, (int)vaLog::GetInstance( ).Entries( ).size( ) - linesToShowMax - 1 );
        showCount = linesToShowMax;
    }
    showFrom = (int)vaLog::GetInstance( ).Entries( ).size( ) - showCount;

    const float spaceToBorder = 2;
    float sizeX               = windowWidth - spaceToBorder*2;
    float sizeY               = (ImGui::GetTextLineHeightWithSpacing() * showCount + 10);
    bool opened = true;
    ImGuiWindowFlags windowFlags = 0;
    windowFlags |= ImGuiWindowFlags_NoTitleBar;
    windowFlags |= ImGuiWindowFlags_NoResize                ;
    windowFlags |= ImGuiWindowFlags_NoMove                  ;
    // windowFlags |= ImGuiWindowFlags_NoScrollbar             ;
    // windowFlags |= ImGuiWindowFlags_NoScrollWithMouse       ;
    windowFlags |= ImGuiWindowFlags_NoCollapse              ;
    windowFlags |= ImGuiWindowFlags_NoFocusOnAppearing      ;
    //windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus   ;
    windowFlags |= ImGuiWindowFlags_NoDocking      ;
    windowFlags |= ImGuiWindowFlags_NoSavedSettings      ;
    windowFlags |= ImGuiWindowFlags_NoScrollbar; //?
    windowFlags |= ImGuiWindowFlags_NoDecoration;

    const std::vector<vaLog::Entry> & logEntries = vaLog::GetInstance().Entries();

    float winAlpha = (m_consoleOpen)?(0.93f):(0.5f);
    if( showCount == 0 )
        winAlpha = 0.0f;
    ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.0f, 0.0f, 0.0f, 1.0f ) );

    bool showConsoleWindow = (!m_consoleOpen && showCount > 0) || m_consoleOpen;

    if( !m_consoleOpen )
        windowFlags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

    if( showConsoleWindow )
    {
        ImGui::SetNextWindowPos( ImVec2( (float)( windowWidth/2 - sizeX/2), (float)( windowHeight - sizeY) - spaceToBorder ), ImGuiCond_Always );
        ImGui::SetNextWindowSize( ImVec2( (float)sizeX, (float)sizeY ), ImGuiCond_Always );
        ImGui::SetNextWindowCollapsed( false, ImGuiCond_Always );
        ImGui::SetNextWindowBgAlpha(winAlpha);
        
        if( !m_consoleWasOpen && m_consoleOpen )
        {
            ImGui::SetNextWindowFocus( );
            m_keyboardCaptureFocus = true;
        }

        if( ImGui::Begin( "Console", &opened, windowFlags ) )
        {
            // if console not open, show just the log
            if( !m_consoleOpen )
            {
                if( showCount > 0 )
                {
                    // for( int i = 0; i < linesToShowMax - showCount; i++ )
                    //     ImGui::Text( "" );

                    // ImGui::Columns( 2 );
                    // float scrollbarSize = 24.0f;
                    // ImGui::SetColumnOffset( 1, (float)sizeX - scrollbarSize );

                    for( int i = showFrom; i < (int)logEntries.size(); i++ )
                    {
                        if( i < 0  )
                        {
                            ImGui::Text("");
                            continue;
                        }
                        const vaLog::Entry & entry = logEntries[i];
                        float lineCursorPosY = ImGui::GetCursorPosY( );

                        char buff[64];
                        #pragma warning ( suppress: 4996 )
                        strftime( buff, sizeof(buff), "%H:%M:%S: ", localtime( &entry.LocalTime ) );
                        ImGui::TextColored( ImVec4( 0.3f, 0.3f, 0.2f, 1.0f ), buff );

                        ImGui::SetCursorPosX( timerSeparatorX );
                        ImGui::SetCursorPosY( lineCursorPosY );
                        ImGui::TextColored( ImFromVA( entry.Color ), vaStringTools::SimpleNarrow( entry.Text ).c_str( ) );
                    }

                    // ImGui::NextColumn();
                    // 
                    // int smax = vaMath::Max( 1, (int)vaLog::GetInstance().Entries().size() - showFrom );
                    // int smin = 0;
                    // int currentPos = smax - smax; // inverted? todo, this is weird
                    // ImGui::VSliderInt( "", ImVec2( scrollbarSize-16, (float)sizeY - 16 ), &currentPos, smin, smax, " " );
                }
            }
            else
            {

                //ImGui::TextWrapped("This example implements a console with basic coloring, completion and history. A more elaborate implementation may want to store entries along with extra data such as timestamp, emitter, etc.");
                ImGui::TextWrapped("Enter 'HELP' for help, press TAB to use text completion.");

                // TODO: display items starting from the bottom

                // if (ImGui::SmallButton("Add Dummy Text")) { vaLog::GetInstance().Add("%d some text", (int)vaLog::GetInstance().Entries().size()); vaLog::GetInstance().Add("some more text"); vaLog::GetInstance().Add("display very important message here!"); } ImGui::SameLine();
                // if (ImGui::SmallButton("Add Dummy Error")) vaLog::GetInstance().Add("[error] something went wrong"); ImGui::SameLine();
                // if (ImGui::SmallButton("Clear")) vaLog::GetInstance().Clear(); ImGui::SameLine();
                // bool copy_to_clipboard = ImGui::SmallButton("Copy"); ImGui::SameLine();
                // if (ImGui::SmallButton("Scroll to bottom")) m_scrollToBottom = true;
                bool copy_to_clipboard = false;

                ImGui::Separator();

                // ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
                // static ImGuiTextFilter filter;
                // filter.Draw("Filter (\"incl,-excl\") (\"error\")", 180);
                // ImGui::PopStyleVar();
                // ImGui::Separator();

                ImGui::BeginChild("ScrollingRegion", ImVec2(0,-ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings );
                if (ImGui::BeginPopupContextWindow())
                {
                    if (ImGui::Selectable("Clear")) vaLog::GetInstance().Clear();
                    ImGui::EndPopup();
                }

                // if( m_scrollByAddedLineCount )
                // {
                //     int lineDiff = vaMath::Max( 0, (int)logEntries.size() - m_lastDrawnLogLineCount );
                //     if( lineDiff > 0 )
                //     {
                //         ImGuiWindow * window = ImGui::GetCurrentWindow();
                //         if( window != nullptr )
                //         {
                //             float newScrollY = window->Scroll.y + ImGui::GetTextLineHeightWithSpacing() * lineDiff;
                // 
                //             window->DC.CursorMaxPos.y += window->Scroll.y; // SizeContents is generally computed based on CursorMaxPos which is affected by scroll position, so we need to apply our change to it.
                //             window->Scroll.y = newScrollY;
                //             window->DC.CursorMaxPos.y -= window->Scroll.y;
                //         }
                //     }
                //     m_scrollByAddedLineCount = false;
                // }


                // Display every line as a separate entry so we can change their color or add custom widgets. If you only want raw text you can use ImGui::TextUnformatted(log.begin(), log.end());
                // NB- if you have thousands of entries this approach may be too inefficient and may require user-side clipping to only process visible items.
                // You can seek and display only the lines that are visible using the ImGuiListClipper helper, if your elements are evenly spaced and you have cheap random access to the elements.
                // To use the clipper we could replace the 'for (int i = 0; i < Items.Size; i++)' loop with:
                //     ImGuiListClipper clipper(Items.Size);
                //     while (clipper.Step())
                //         for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                // However take note that you can not use this code as is if a filter is active because it breaks the 'cheap random-access' property. We would need random-access on the post-filtered list.
                // A typical application wanting coarse clipping and filtering may want to pre-compute an array of indices that passed the filtering test, recomputing this array when user changes the filter,
                // and appending newly elements as they are inserted. This is left as a task to the user until we can manage to improve this example code!
                // If your items are of variable size you may want to implement code similar to what ImGuiListClipper does. Or split your data into fixed height items to allow random-seeking into your list.
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4,1)); // Tighten spacing
                // for (int i = 0; i < Items.Size; i++)
                // {
                //     const char* item = Items[i];
                //     if (!filter.PassFilter(item))
                //         continue;
                //     ImVec4 col = ImVec4(1.0f,1.0f,1.0f,1.0f); // A better implementation may store a type per-item. For the sample let's just parse the text.
                //     if (strstr(item, "[error]")) col = ImColor(1.0f,0.4f,0.4f,1.0f);
                //     else if (strncmp(item, "# ", 2) == 0) col = ImColor(1.0f,0.78f,0.58f,1.0f);
                //     ImGui::PushStyleColor(ImGuiCol_Text, col);
                //     ImGui::TextUnformatted(item);
                //     ImGui::PopStyleColor();
                // }
                float scrollY = ImGui::GetScrollY();
                float windowStartY = ImGui::GetCursorPosY();
                windowStartY;

                float availableDrawArea = (linesToShowMax+1) * ImGui::GetTextLineHeightWithSpacing( );
                int drawFromLine = vaMath::Clamp( (int)(scrollY / ImGui::GetTextLineHeightWithSpacing( )), 0, vaMath::Max( 0, (int)logEntries.size()-1) );
                int drawToLine = vaMath::Clamp( (int)((scrollY+availableDrawArea)/ ImGui::GetTextLineHeightWithSpacing( )), 0, (int)logEntries.size() );

                if( copy_to_clipboard )
                {
                    // below was never tested but it might work
                    assert( false );
                    drawFromLine = 0;
                    drawToLine = (int)logEntries.size();
                    ImGui::LogToClipboard();
                }

                // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                // these (and same below) are the only place that remains to be optimized for huge LOG buffers; it's not that complicated probably 
                // but requires going into NewLine and figuring out how to correctly make a MultiNewLine instead
                for( int i = 0; i < drawFromLine; i++ )
                    ImGui::NewLine();
                // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

                for( int i = drawFromLine; i < drawToLine; i++ )
                {
                    const vaLog::Entry & entry = logEntries[i];
                    //float lineCursorPosY = ImGui::GetCursorPosY( );

                    char buff[64];
                    #pragma warning ( suppress: 4996 )
                    strftime( buff, sizeof(buff), "%H:%M:%S: ", localtime( &entry.LocalTime ) );
                    ImGui::TextColored( ImVec4( 0.3f, 0.3f, 0.2f, 1.0f ), buff );

                    ImGui::SameLine( timerSeparatorX );
                    ImGui::TextColored( ImFromVA( entry.Color ), vaStringTools::SimpleNarrow( entry.Text ).c_str( ) );
                }

                // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                // see comments above
                for( int i = drawToLine; i < (int)logEntries.size( ); i++ )
                    ImGui::NewLine();
                ImGui::ItemSize( {0, 1} ); // add one more pixel spacing - avoids last line of text pixels getting clipped for some reason
                // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

                if( copy_to_clipboard )
                    ImGui::LogFinish();

                int addedLineCount = (int)logEntries.size() - m_lastDrawnLogLineCount;
                m_lastDrawnLogLineCount = (int)logEntries.size();

                // keep scrolling to bottom if we're at bottom and new lines were added (also reverse scroll if lines removed)
                if( addedLineCount != 0 && (drawToLine+vaMath::Abs(addedLineCount)) >= (int)logEntries.size( ) )
                    m_scrollToBottom = true;
           

                if( m_scrollToBottom )
                    ImGui::SetScrollHereY();
                m_scrollToBottom = false;

                // messes with the scrollbar for some reason
                if( ImGui::IsWindowHovered( 0 ) && ImGui::IsMouseReleased( 0 ) )
                    m_keyboardCaptureFocus = true;

                ImGui::PopStyleVar();
                ImGui::EndChild();
                ImGui::Separator();

                if( m_keyboardCaptureFocus )
                {
                    ImGui::SetKeyboardFocusHere( 0 );
                    m_keyboardCaptureFocus = false;
                }

                // Command-line
                if (ImGui::InputText("Input", m_textInputBuffer, _countof(m_textInputBuffer), ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CallbackCompletion|ImGuiInputTextFlags_CallbackHistory, (ImGuiInputTextCallback)&TextEditCallbackStub, (void*)this))
                {
                    char* input_end = m_textInputBuffer+strlen(m_textInputBuffer);
                    while (input_end > m_textInputBuffer && input_end[-1] == ' ') input_end--; *input_end = 0;
                    if (m_textInputBuffer[0])
                    {
                        ExecuteCommand( m_textInputBuffer );
                        m_keyboardCaptureFocus = true;
                    }
                    strcpy_s(m_textInputBuffer, "");
                }

            }
        }
        ImGui::End( );
    }
    ImGui::PopStyleColor( 1 );

#endif // #ifdef VA_IMGUI_INTEGRATION_ENABLED

    m_consoleWasOpen = m_consoleOpen;
}

int vaUIConsole::TextEditCallbackStub( void * _data )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGuiInputTextCallbackData * data = (ImGuiInputTextCallbackData *)_data;
    vaUIConsole* console = (vaUIConsole*)data->UserData;
    return console->TextEditCallback(data);
#else
    _data;
    return 0;
#endif
}

#ifdef VA_IMGUI_INTEGRATION_ENABLED
// Portable helpers
//static int   Stricmp(const char* str1, const char* str2)         { int d; while ((d = toupper(*str2) - toupper(*str1)) == 0 && *str1) { str1++; str2++; } return d; }
static int   Strnicmp(const char* str1, const char* str2, int n) { int d = 0; while (n > 0 && (d = toupper(*str2) - toupper(*str1)) == 0 && *str1) { str1++; str2++; n--; } return d; }
//static char* Strdup(const char *str)                             { size_t len = strlen(str) + 1; void* buff = malloc(len); return (char*)memcpy(buff, (const void*)str, len); }
#endif

int vaUIConsole::TextEditCallback( void * _data )
{
    _data;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGuiInputTextCallbackData * data = (ImGuiInputTextCallbackData *)_data;
    //AddLog("cursor: %d, selection: %d-%d", data->CursorPos, data->SelectionStart, data->SelectionEnd);
    switch (data->EventFlag)
    {
    case ImGuiInputTextFlags_CallbackCompletion:
        {
            // Example of TEXT COMPLETION

            // Locate beginning of current word
            const char* word_end = data->Buf + data->CursorPos;
            const char* word_start = word_end;
            while (word_start > data->Buf)
            {
                const char c = word_start[-1];
                if (c == ' ' || c == '\t' || c == ',' || c == ';')
                    break;
                word_start--;
            }

            // Build a list of candidates
            ImVector<const char*> candidates;
            for (int i = 0; i < (int)m_commands.size(); i++)
                if (Strnicmp( m_commands[i].Name.c_str(), word_start, (int)(word_end-word_start)) == 0)
                    candidates.push_back( m_commands[i].Name.c_str() );

            if (candidates.Size == 0)
            {
                // No match
                vaLog::GetInstance().Add("No match for \"%.*s\"!\n", (int)(word_end-word_start), word_start);
            }
            else if (candidates.Size == 1)
            {
                // Single match. Delete the beginning of the word and replace it entirely so we've got nice casing
                data->DeleteChars((int)(word_start-data->Buf), (int)(word_end-word_start));
                data->InsertChars(data->CursorPos, candidates[0]);
                data->InsertChars(data->CursorPos, " ");
            }
            else
            {
                // Multiple matches. Complete as much as we can, so inputing "C" will complete to "CL" and display "CLEAR" and "CLASSIFY"
                int match_len = (int)(word_end - word_start);
                for (;;)
                {
                    int c = 0;
                    bool all_candidates_matches = true;
                    for (int i = 0; i < candidates.Size && all_candidates_matches; i++)
                        if (i == 0)
                            c = toupper(candidates[i][match_len]);
                        else if (c == 0 || c != toupper(candidates[i][match_len]))
                            all_candidates_matches = false;
                    if (!all_candidates_matches)
                        break;
                    match_len++;
                }

                if (match_len > 0)
                {
                    data->DeleteChars((int)(word_start - data->Buf), (int)(word_end-word_start));
                    data->InsertChars(data->CursorPos, candidates[0], candidates[0] + match_len);
                }

                // List matches
                vaLog::GetInstance().Add("Possible matches:\n");
                for (int i = 0; i < candidates.Size; i++)
                    vaLog::GetInstance().Add("- %s\n", candidates[i]);
            }

            break;
        }
    case ImGuiInputTextFlags_CallbackHistory:
        {
            // Example of HISTORY
            const int prev_history_pos = m_commandHistoryPos;
            if (data->EventKey == ImGuiKey_UpArrow)
            {
                if (m_commandHistoryPos == -1)
                    m_commandHistoryPos = (int)m_commandHistory.size() - 1;
                else if (m_commandHistoryPos > 0)
                    m_commandHistoryPos--;
            }
            else if (data->EventKey == ImGuiKey_DownArrow)
            {
                if (m_commandHistoryPos != -1)
                    if (++m_commandHistoryPos >= m_commandHistory.size())
                        m_commandHistoryPos = -1;
            }

            // A better implementation would preserve the data on the current input line along with cursor position.
            if (prev_history_pos != m_commandHistoryPos)
            {
                data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen = (int)snprintf(data->Buf, (size_t)data->BufSize, "%s", (m_commandHistoryPos >= 0) ? m_commandHistory[m_commandHistoryPos].c_str() : "");
                data->BufDirty = true;
            }
        }
    }
#endif
    return 0;
}
void vaUIConsole::ExecuteCommand( const string & commandLine )
{
    vaLog::GetInstance().Add( "# %s\n", commandLine.c_str() );

    // Insert into history. First find match and delete it so it can be pushed to the back. This isn't trying to be smart or optimal.
    m_commandHistoryPos = -1;
    for( int i = (int)m_commandHistory.size() - 1; i >= 0; i-- )
        if( vaStringTools::CompareNoCase( m_commandHistory[i], commandLine ) == 0 )
        {
            //free( History[i] );
            //History.erase( History.begin( ) + i );
            m_commandHistory.erase( m_commandHistory.begin() + i );
            break;
        }
    m_commandHistory.push_back( commandLine );

    // Process command
    if( vaStringTools::CompareNoCase( commandLine, "CLEAR" ) == 0 )
    {
        vaLog::GetInstance().Clear();
    }
    else if( vaStringTools::CompareNoCase( commandLine, "HELP" ) == 0 )
    {
        vaLog::GetInstance().Add( "Commands:" );
        for( int i = 0; i < (int)m_commands.size(); i++ )
            vaLog::GetInstance().Add( "- %s", m_commands[i].Name.c_str() );
    }
    else if( vaStringTools::CompareNoCase( commandLine, "HISTORY" ) == 0 )
    {
        for( int i = (int)m_commandHistory.size() >= 10 ? (int)m_commandHistory.size() - 10 : 0; i < (int)m_commandHistory.size(); i++ )
            vaLog::GetInstance().Add( "%3d: %s\n", i, m_commandHistory[i].c_str() );
    }
    else if( vaStringTools::CompareNoCase( commandLine, "QUIT" ) == 0 )
    {
        vaCore::SetAppQuitFlag( true );
    }
    else
    {
        vaLog::GetInstance().Add( LOG_COLORS_ERROR, "Unknown command: '%s'\n", commandLine.c_str() );
    }
    m_scrollToBottom = true;
}
