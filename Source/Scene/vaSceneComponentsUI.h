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

#include "Core/vaUI.h"

#include "vaSceneComponents.h"

// EnTT components must be both move constructible and move assignable!

namespace Vanilla
{
    class vaXMLSerializer;
    class vaScene;

    // entt-specific scene namespace
    namespace Scene
    {
        // this is the UI for the entity browser (tree)
        struct UIEntityTreeOpenedTag { };
        struct UIEntityTreeSelectedTag { };
        // entity browser UI - filter stuff
        struct UIEntityFilteredOutTag { bool UnfilteredChildren = false; };

        struct IBLProbeUIContext
        {
        };

        // holder for vaEntityPropertiesPanel
        struct UIEntityPropertiesPanel
        {
            shared_ptr<class vaEntityPropertiesPanel>   Value;
        };

        class vaEntityPropertiesPanel : public vaUIPropertiesItem
        {
            entt::registry &                    m_registry;
            entt::entity                        m_entity;
            vaScene * const                     m_scene;

            string                              m_uiComponentsFilter;

            std::vector<shared_ptr<void>>       m_uiContextRefs;

            int                                 m_uiMenuOpenedComponentID = -1;


        public:
            vaEntityPropertiesPanel( entt::registry & registry, entt::entity entity );
            vaEntityPropertiesPanel( vaScene & scene, entt::entity entity );
            virtual ~vaEntityPropertiesPanel( );

            entt::entity                        Entity() const                                                  { return m_entity; }

            virtual string                      UIPropertiesItemGetDisplayName( ) const override;
            virtual void                        UIPropertiesItemTick( vaApplicationBase& application, bool openMenu, bool hovered ) override;
        };

        // Functions
        void HandleRightClickContextMenuPopup( vaScene & scene, entt::entity entity, bool hasOpenProperties, bool hasFocusInScene, const std::function<void()> & customPopupsTop = {} );
    }
}
