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

#include "vaRendering.h"

#include "Rendering/vaSceneRenderViews.h"

#include "Rendering/vaSceneLighting.h"

#include "Rendering/vaSceneRenderInstanceProcessor.h"

namespace Vanilla
{
    class vaSceneRaytracing;

    class vaSceneRenderer : public vaRenderingModule, public vaUIPanel, public std::enable_shared_from_this<vaSceneRenderer>
    {
    private:
        std::vector< weak_ptr< vaSceneRenderViewBase > >
                                                m_allViews;

        std::vector< shared_ptr< vaSceneMainRenderView > >
                                                m_mainViews;
        shared_ptr<vaPointShadowRV>             m_viewPointShadow;
        shared_ptr<vaLightProbeRV>              m_viewLightProbe;

        shared_ptr<class vaScene>               m_scene;
        shared_ptr<void>                        m_sceneCallbacksToken = nullptr;    // used to connect to vaScene - changes every time there's a new scene

        vaSceneRenderInstanceProcessor          m_instanceProcessor;
        shared_ptr<vaRenderInstanceStorage>     m_instanceStorage;

        // should it be part of the vaSceneMainRenderView? probably not since in theory it's generic and can render any multiple different skies per frame
        shared_ptr<vaSkybox>                    m_skybox;   

        // all of the lighting for this scene, for this renderer!
        shared_ptr<vaSceneLighting>             m_lighting;

        shared_ptr<vaSceneRaytracing>           m_raytracer;


//        // this means there's no pending updates to shadows or IBLs - mostly needed for image comparisons / EnforceDeterminism option
//        bool                                m_shadowsStable     = false;
//        bool                                m_IBLsStable        = false;

    public:
        struct GeneralSettings
        {
            // this will (in theory) ensure every render of an identical scene is identical but at a significant performance expense (i.e. can be an order of magnitude slower)
            bool                                EnforceDeterminism  = false;    // warning, atm broken/unused!
            
            // without depth pre-pass there's no ASSAO and depth tested materials and some other stuff - it's not really intended to be used as it is now
            bool                                DepthPrepass        = true;

            // this makes depth rendering slightly faster on some GPUs but can be very costly on the CPU side
            bool                                SortDepthPrepass    = true;

            // we now always sort opaque due to decals, sorry.
            // // this makes rendering slightly faster on some GPUs / scenarios but can be very costly on the CPU side
            // bool                                SortOpaque          = true;
        };
        struct LODSettings
        {
            //shared_ptr<vaSceneMainRenderView>   LODReferenceView?
        }                                   m_settingsLOD;
        struct LightingSettings
        {
            // how many concurrent point shadow updates can we run per frame (we can have many more actual shadows)
            int                                 PointShadowViews    = 1;
        }                                   m_settingsLighting;
        struct UISettings
        {
            bool                                EnableContextMenu   = true;     // get context info for middle-mouse clicks for entities, meshes and assets; make sure to enable SetCursorHoverInfoEnabled on the main render view
        }                                   m_settingsUI;

    private:
        GeneralSettings                     m_settingsGeneral;

        vaDrawResultFlags                   m_sceneTickDrawResults  = vaDrawResultFlags::None;

        // this is part of LOD in effect (precision LOD :) )
        vaVector3                           m_worldBase             = { 0, 0, 0 };  // see vaDrawAttributes::GlobalSettings

    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaSceneRenderer( const vaRenderingModuleParams & params );
    public:
        virtual ~vaSceneRenderer( );

        const shared_ptr<vaScene> &         GetScene( ) const                   { return m_scene; }
        void                                SetScene( const shared_ptr<class vaScene>& scene ) { bool changed = m_scene != scene; m_scene = scene; if( changed ) OnNewScene( ); }

        const vaSceneRenderInstanceProcessor & GetInstanceProcessor( ) const    { return m_instanceProcessor; }
        const shared_ptr<vaRenderInstanceStorage> & GetInstanceStorage( ) const
                                                                                { return m_instanceStorage; }

        const shared_ptr<vaSceneRaytracing>& GetRaytracer( ) const              { return m_raytracer; }

        GeneralSettings &                   GeneralSettings( )                  { return m_settingsGeneral; }
        UISettings &                        UISettings( )                       { return m_settingsUI; }

        const shared_ptr<vaSceneLighting> & GetLighting( ) const                { return m_lighting; }
        const shared_ptr<vaSkybox> &        GetSkybox( ) const                  { return m_skybox; }
                                                                                    
        shared_ptr<vaSceneMainRenderView>   CreateMainView( );

        const vaCameraBase *                GetLODReferenceCamera( ) const;

        vaDrawResultFlags                   RenderTick( float deltaTime, int64 applicationTickIndex );

        // This is where the vaScene says "I'm starting my tick, what data do you need"
        void                                OnSceneTickBegin( vaScene & scene, float deltaTime, int64 applicationTickIndex );
        //void                                OnSceneTickEnd( vaScene & scene, float deltaTime, int64 applicationTickIndex );

    public:
        vaDrawResultFlags                   DrawDepthOnly( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaRenderInstanceList & renderSelection, vaRenderInstanceList::SortHandle renderSelectionSort, const vaCameraBase & camera, const vaDrawAttributes::GlobalSettings & globalSettings );
        vaDrawResultFlags                   DrawOpaque( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaRenderInstanceList & renderSelection, vaRenderInstanceList::SortHandle renderSelectionSort, const vaCameraBase & camera, const vaDrawAttributes::GlobalSettings & globalSettings, const shared_ptr<vaTexture> & ssaoTexture, bool drawSky );
        vaDrawResultFlags                   DrawTransparencies( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaRenderInstanceList & renderSelection, vaRenderInstanceList::SortHandle renderSelectionSort, const vaCameraBase & camera, const vaDrawAttributes::GlobalSettings & globalSettings );

    protected:
        void                                UpdateSettingsDependencies( );
        void                                OnNewScene( );
        void                                PreprocessViews( bool & raytracingRequired );

    protected:
        friend class vaSceneRenderInstanceProcessor;
        // this gets called from worker threads to provide chunks for processing! First one call to PreProcessInstanceBatch to prepare receiving buffers (if any) and then ProcessInstanceBatch per batch
        void                                PrepareInstanceBatchProcessing( uint32 maxInstances );
        void                                ProcessInstanceBatch( vaSceneRenderInstanceProcessor::SceneItem * items, uint32 itemCount, uint32 baseInstanceIndex );

    protected:
        virtual void                        UIPanelTick( vaApplicationBase & application ) override;
        virtual void                        UIPanelTickAlways( vaApplicationBase & application ) override;

    };

}
