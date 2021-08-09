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

#include "Core/vaEvent.h"

#include "vaXMLSerialization.h"

//#include <optional>

namespace Vanilla
{
    class vaApplicationBase;
    class vaRenderDeviceContext;

    enum class vaMRSWidgetFlags : uint32
    {
        None                = 0,
        FocusOnAppear       = ( 1 << 0 ),       // make it active first time (instead of just a clickity-click button)
        FocusNow            = ( 1 << 1 ),       // make it active (instead of just a clickity-click button)
    };
    BITFLAG_ENUM_CLASS_HELPER( vaMRSWidgetFlags );

    // Intended to expose ui for instances of subsystem entities such as individual materials, meshes, etc.
    // These will not show up unless you call vaUIManager::GetInstance().SelectPropertyItem( this_item ) or manually display them
    class vaUIPropertiesItem
    {
        static int                          s_lastID;
        string const                        m_uniqueID;

    public:
        vaUIPropertiesItem( );
        vaUIPropertiesItem( const vaUIPropertiesItem & copy );
        ~vaUIPropertiesItem( ) { }

        vaUIPropertiesItem & operator = ( const vaUIPropertiesItem & copy )             { copy; return *this; }        // nothing to copy!
    
    public:
        const string &                      UIPropertiesItemGetUniqueID( ) const        { return m_uniqueID; }         // unique ID for the session (does not have to persist between sessions, only used for IMGUI purposes)
        
        virtual string                      UIPropertiesItemGetDisplayName( ) const     { return "unnamed"; }          // name of the props item - can be anything and doesn't have to be unique
        
        virtual void                        UIPropertiesItemTick( vaApplicationBase & application, bool openMenu, bool hovered ) = 0;

        void                                UIPropertiesItemTickCollapsable( vaApplicationBase & application, bool showFrame = true, bool defaultOpen = false, bool indent = false );

    public:
        static void                         DrawList( vaApplicationBase & application, const char * stringID, vaUIPropertiesItem ** objList, int objListCount, int & currentItem, float width = 0.0f, float listHeight = 0.0f, float selectedElementHeight = 0.0f );
    };

    // Intended to expose ui for a whole subsystem that usually has only one instance (i.e. gbuffer, tone mapping, material manager, ...)
    // vaUIPanel objects will be automatically tracked and DrawList() called once per frame just before the main tick
    class vaUIPanel
    {
    public:
        // Extend when needed
        enum class DockLocation : int32
        {
            NotDocked           = 0,
            DockedLeft          ,
            DockedLeftBottom    ,
            DockedRight         ,
            DockedRightBottom   ,
        };

    private:
        string const                        m_name                  = "error";
        string const                        m_familyName            = "";
        int const                           m_sortOrder             = 0;
        DockLocation const                  m_initialDock           = DockLocation::NotDocked;
        vaVector2 const                     m_initialSize           = vaVector2( 400, 400 );

        bool                                m_visible               = true;

        bool                                m_setFocusNextFrame     = false;

        bool                                m_resetDocking          = false;

    protected:
        // name must be unique (if it isn't, a number gets added to it) - override UIPanelGetDisplayName to return a custom name used for the window/tab title (does not have to be unique, can change every frame, etc.)
        // sortOrder is used to determine order the windows are displayed in menus and initialized as well as initial focus (lower has priority)
        // visible determines window visibility; override UIPanelIsListed to disable it from appearing 'View' menu and to remove 'close' button (so visibility is only changeable from the program)
        // set familyName if you want multiple vaUIPanels to show under one parent 'family' panel as individual tabs
        vaUIPanel( const string & name, int sortOrder = 0, bool initialVisible = true, DockLocation initialDock = DockLocation::NotDocked, const string & familyName = "", const vaVector2 & initialSize = vaVector2( 400, 400 ) );

    public:
        virtual ~vaUIPanel();

    private:
        // // this is for ImGui persistence - to avoid overlap with other object with identically named controls
        // virtual string                      UIPanelGetID( ) const               { return m_uniqueID; }


    public:
        const string &                      UIPanelGetName( ) const                 { return m_name; }                  // name of the panel has to be unique among all panels and cannot change later; constructor will add number to the end of the name if one with the same name already exists; persistence between sessions is recommended
        const string &                      UIPanelGetFamily( ) const               { return m_familyName; }

        const vaVector2 &                   UIPanelGetInitialSize( ) const          { return m_initialSize; }
        DockLocation                        UIPanelGetInitialDock( ) const          { return m_initialDock; }
        int                                 UIPanelGetSortOrder( ) const            { return m_sortOrder; }

        virtual bool                        UIPanelIsVisible( ) const               { return m_visible; }
        virtual void                        UIPanelSetVisible( bool visible )       { m_visible = visible; }
        virtual bool                        UIPanelIsListed( ) const                { return true; }                    // is listed in the top menu under 'Windows'? if false also disables close button - UI can't change opened/closed status

        virtual void                        UIPanelTickAlways( vaApplicationBase & application ) { application; }                          // (optional) - will get called even when panel is not visible or not an active tab - useful if a tool needs to respond to special keys or something
        virtual void                        UIPanelTick( vaApplicationBase & application ) = 0;

        virtual bool                        UIPanelIsDirty( ) const                 { return false; }
        virtual string                      UIPanelGetDisplayName( ) const          { return UIPanelGetName(); }        // use to override display name if using multiple instances - can change at runtime (animate)

        // virtual void                        UIPanelFileMenuItems( )                 { }                                 // use to insert items ('ImGui::MenuItem') into 'File' menu - for nicer separation add separator at the end
        //virtual void                        UIPanelStandaloneMenu( )                { }                                 // use to insert standalone menus ('ImGui::BeginMenu')

    public:
        void                                UIPanelSetFocusNextFrame( bool focus = true )  { m_setFocusNextFrame = focus; }
        bool                                UIPanelGetFocusNextFrame( ) const              { return m_setFocusNextFrame; }
        void                                UIPanelTickCollapsable( vaApplicationBase & application, bool showFrame = true, bool defaultOpen = false, bool indent = false );
        string                              UIPanelGetDisplayNameWithDirtyTag( ) const { string retVal = UIPanelGetDisplayName( ); if( UIPanelIsDirty( ) ) retVal += "*"; return retVal; }

    private:
        static string                       FindUniqueName( const string & name );
    };

    // if you just want to create a panel with a lambda callback without inheriting vaUIPanel, here's a simple wrapper
    class vaUISimplePanel : public vaUIPanel
    {
        std::function< void( vaApplicationBase & application ) > m_callback;
    public:
        vaUISimplePanel( std::function< void( vaApplicationBase & application ) > callback, const string & name, int sortOrder = 0, bool initialVisible = true, DockLocation initialDock = DockLocation::NotDocked, const string & familyName = "", const vaVector2 & initialSize = vaVector2( 400, 400 ) ) : vaUIPanel( name, sortOrder, initialVisible, initialDock, familyName, initialSize ), m_callback(callback) { }

        virtual void                        UIPanelTick( vaApplicationBase & application ) override { m_callback( application ); }
    };

    class vaUIPropertiesPanel;
    class vaUIFamilyPanel;
    class vaUITransientPropertiesItem;
    class vaUIMRSWidget;
    struct vaUIMRSWidgetGlobals;

    class vaUIManager final : public vaSingletonBase<vaUIManager>
    {
        friend class vaUIPanel;
        std::map< string, vaUIPanel *>      m_panels;

        // ImGui doesn't remember visibility (by design?) so we have to save/load it ourselves
        // this is loaded during serialization so all new panels can check if they were hidden when the app closed last time (& serialized settings)
        std::vector< pair<string, bool>>         m_initiallyPanelVisibility;

        std::vector<shared_ptr<vaUIPropertiesPanel>>  
                                            m_propPanels;
        int                                 m_propPanelCurrentlyDrawn   = -1;
        std::vector<shared_ptr<vaUIFamilyPanel>> m_familyPanels;

        //
        std::function< void( bool & uiVisible, bool & menuVisible, bool & consoleVisible ) > 
                                            m_visibilityOverrideCallback;

        std::map< string, shared_ptr<vaUITransientPropertiesItem> >
                                            m_transientProperties;

        std::map< string, shared_ptr<vaUIMRSWidget> >
                                            m_mrsWidgets;
        shared_ptr<vaUIMRSWidgetGlobals>    m_mrsWidgetGlobals;
        

        // various settings
        bool                                m_visible           = true;
        bool                                m_menuVisible       = true;
        bool                                m_consoleVisible    = true;

        // some additional initialization required first frame
        bool                                m_firstFrame        = true;

        // ImGui docking stuff
        uint32 /*ImGuiID*/                  m_dockSpaceIDRoot           = (uint32)-1;
        uint32 /*ImGuiID*/                  m_dockSpaceIDLeft           = (uint32)-1;
        uint32 /*ImGuiID*/                  m_dockSpaceIDLeftBottom     = (uint32)-1;
        uint32 /*ImGuiID*/                  m_dockSpaceIDRight          = (uint32)-1;
        uint32 /*ImGuiID*/                  m_dockSpaceIDRightBottom    = (uint32)-1;

        int                                 m_delayUIFewFramesDirtyHack = 2;    // this lets all the subsystems create their panels before we do the first run (no-imgui.ini-file) setup

        struct MenuItem
        {
            string                                      Title;
            weak_ptr<void>                              AliveToken;
            std::function<void( vaApplicationBase & )>  Handler;
        };
        std::vector< MenuItem >             m_userMenus;

        bool                                m_inTickUI          = false;        // used to prevent disallowed recursive calls that might mess with stuff that shouldn't change during TickUI

        // ImGui demo - useful for ImGui development
        bool                                m_showImGuiDemo     = false;

    public:
        // Useful location to update any last minute stuff like canvas2D/canvas3D stuff but when device render context is required
        vaEvent<void( vaRenderDeviceContext & renderContext )>
                                            e_BeforeDrawUI;


    private:
        friend class vaCore;
        vaUIManager( );
        ~vaUIManager( );

    private:
        friend class vaApplicationBase;
        void                                SerializeSettings( vaXMLSerializer & serializer );
        void                                UpdateUI( bool appHasFocus );
        void                                TickUI( vaApplicationBase & application );

        // ImGui doesn't remember visibility (by design?) so we have to save/load it ourselves
        bool                                FindInitialVisibility( const string & panelName, bool defVal );

    public:

        bool                                IsVisible( ) const                          { return m_visible; }
        void                                SetVisible( bool visible )                  { m_visible = visible; }

        bool                                IsMenuVisible( ) const                      { return m_menuVisible; }
        void                                SetMenuVisible( bool menuVisible )          { m_menuVisible = menuVisible; }

        void                                RegisterMenuItemHandler( const string & title, const shared_ptr<void> & aliveToken, const std::function<void( vaApplicationBase & )> & handler );
        void                                UnregisterMenuItemHandler( const string & title );

        bool                                IsConsoleVisible( ) const                   { return m_consoleVisible; }
        void                                SetConsoleVisible( bool consoleVisible )    { m_consoleVisible = consoleVisible; }

        // Use this to temporarily override UI visibility (such as when using a special UI tool and wanting to hide everything else)
        void                                SetVisibilityOverrideCallback( std::function< void(bool & uiVisible, bool & menuVisible, bool & consoleVisible) > callback )       { m_visibilityOverrideCallback = callback; }

        void                                SelectPropertyItem( const weak_ptr<vaUIPropertiesItem> & item, int preferredPropPanel = -1 ); // -1 will try to do something smart
        void                                UnselectPropertyItem( const weak_ptr<vaUIPropertiesItem> & item );
        bool                                IsPropertyItemSelected( const weak_ptr<vaUIPropertiesItem> & item );

        // This is for creating temporary UI in the property panels, without wanting to track it on the user side; 
        // It works like this: you provide unique ID (can't have two items with the same ID open at the same time), display name, callback handler and drawContext for any
        // temporary UI-related data that you don't want to store as a part of the callback lambda for whatever reason.
        // If the callback handler returns 'false', the panel gets removed and deleted from memory and the stored drawContext gets dereferenced.
        // If the user closes the panel, the panel gets removed and deleted from memory the stored drawContext gets dereferenced.
        void                                CreateTransientPropertyItem( const string & uniqueID, const string & displayName, const std::function< bool( vaApplicationBase & application, const shared_ptr<void> & drawContext ) > & drawCallback, const shared_ptr<void> & drawContext = nullptr, int preferredPropPanel = -1 );
        shared_ptr<void>                    FindTransientPropertyItem( const string & uniqueID, bool focusIfFound );
        //void                                FocusTransientPropertyItem( const string & uniqueID );

        // Place a 3D blob which, when clicked, opens an ImGuizmo + control panel for 3D scene object manipulation.
        // If not in a hierarchy, just provide vaMatrix4x4::Identity to parentWorldTransform.
        // This one works in the immediate mode, like ImGui. The upside is that you can call it from any UI part at any point without having to 
        // store any context (uniqueID must be the same every time, which can be just to_string(ImGui::GetID(some_name)) ). The downside is 
        // that it has to be called every frame or the UI will reset.
        // Returns true if this specific widget is currently selected.
        bool                                MoveRotateScaleWidget( const string & uniqueID, const string & displayName, const vaMatrix4x4 & parentWorldTransform, vaMatrix4x4 & localTransform, vaMRSWidgetFlags flags = vaMRSWidgetFlags::None, const vaBoundingBox * localBounds = nullptr );
    };

    // This is a half-baked helper class for displaying log in a form of a console, with additional support for 
    // commands and etc. Initially based on ImGui console examples.
    // !!! Requires additional work to be functional. !!!
    class vaUIConsole : public vaSingletonBase< vaUIConsole >
    {
        struct CommandInfo
        {
            string      Name;
            //some-kindo-of-callback

            CommandInfo( ) { }
            explicit CommandInfo( const string & name ) : Name(name) { }
        };

    private:
        char                    m_textInputBuffer[256];
        bool                    m_scrollToBottom;
        bool                    m_keyboardCaptureFocus = false;
        bool                    m_scrollByAddedLineCount = false;      // if already at bottom, scroll next frame to stay at bottom
        int                     m_lastDrawnLogLineCount = 0;
        
        std::vector<CommandInfo>     m_commands;

        std::vector<string>          m_commandHistory;
        int                     m_commandHistoryPos;    // -1: new line, 0..History.Size-1 browsing history.

        // show console with input text (otherwise just show log)
        bool                    m_consoleOpen;
        bool                    m_consoleWasOpen;

    private:
        friend class vaCore;
        vaUIConsole( ); 
        ~vaUIConsole( );

    public:
        // if open - shows full console and input box; if closed - shows only log for the messages not older than (some time)
        bool                    IsOpen( ) const                     { return m_consoleOpen; }
        void                    SetOpen( bool consoleOpen )         { m_consoleOpen = consoleOpen; }

        bool                    IsVisible( ) const                  { return vaUIManager::GetInstance().IsConsoleVisible(); }
        void                    SetVisible( bool visible    )       { vaUIManager::GetInstance().SetConsoleVisible( visible ); }
        // Todo:
        //void                    AddCommand( const string & name ... )
        //void                    RemoveCommand( const string & name )

        void                    Draw( int windowWidth, int windowHeight );

    private:
        void                    ExecuteCommand( const string & commandLine );

    private:
        static int              TextEditCallbackStub( void * data );
        int                     TextEditCallback( void * data );
    };

}