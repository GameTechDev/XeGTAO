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

#include "Rendering/vaRenderGlobals.h"
#include "Rendering/vaRenderCamera.h"
#include "Rendering/vaRenderInstanceList.h"
#include "Rendering/vaIBL.h"

#include "Rendering/vaSceneRenderInstanceProcessor.h"

namespace Vanilla
{
    class vaSceneRenderer;

    // The idea behind 'Views' is that they handle rendering and lists and apply view-specific post-process
    class vaSceneRenderViewBase
    {
    protected:
        vaRenderDevice &                    m_renderDevice;
        std::weak_ptr<vaSceneRenderer>      m_parentRenderer;

        vaDrawResultFlags                   m_lastPreRenderDrawResults      = vaDrawResultFlags::None;
        vaDrawResultFlags                   m_lastRenderDrawResults         = vaDrawResultFlags::None;

        float                               m_lastDeltaTime                 = 0.0f;

        vaSceneSelectionFilterType          m_selectionFilter;

        struct BasicStats
        {
            int                 ItemsDrawn      = 0;
            int                 TrianglesDrawn  = 0;
            vaDrawResultFlags   DrawResultFlags = vaDrawResultFlags::None;
        }                                   m_basicStats;

    protected:
        vaSceneRenderViewBase( const shared_ptr<vaSceneRenderer> & parentRenderer );

    public:
        virtual ~vaSceneRenderViewBase( ) { }

    protected:
        vaRenderDevice &                GetRenderDevice( )                  { return m_renderDevice; }

        friend class vaSceneRenderer;

        void                            ResetDrawResults( )                 { m_lastPreRenderDrawResults = vaDrawResultFlags::None; m_lastRenderDrawResults = vaDrawResultFlags::None; m_basicStats.DrawResultFlags = vaDrawResultFlags::None; }
        vaDrawResultFlags               GetRenderDrawResults( ) const       { return m_lastRenderDrawResults; }

        // mostly for parsing the scene and updating internal lists - happens mostly in parallel
        virtual void                    PreRenderTick( float deltaTime )    { m_lastDeltaTime = deltaTime; m_basicStats = BasicStats(); }
        virtual vaDrawResultFlags       PreRenderTickParallelFinished( )    { return m_lastPreRenderDrawResults; }

        // do actual rendering
        virtual void                    RenderTick( float deltaTime, vaRenderDeviceContext & renderContext, vaDrawResultFlags & currentDrawResults ) { deltaTime; renderContext; currentDrawResults; }

        virtual void                    UIDisplayStats( );

        virtual void                    UITickAlways( vaApplicationBase & )                                     { }                 // for keyboard stuff that you want to happen even when UI is hidden
        virtual void                    UITick( vaApplicationBase & )                                       { }                 // for UI settings

        // this gets called from worker threads to provide chunks for processing!
        virtual void                    PrepareInstanceBatchProcessing( const shared_ptr<vaRenderInstanceStorage> & instanceStorage )                                                   { instanceStorage; }
        virtual void                    ProcessInstanceBatch( vaScene & scene, vaSceneRenderInstanceProcessor::SceneItem * sceneItems, uint32 itemCount, uint32 baseInstanceIndex )     { scene; sceneItems; itemCount; assert( false ); baseInstanceIndex; }

        virtual bool                    RequiresRaytracing( ) const                                                                 { return false; }

    protected:
        static void                     ProcessInstanceBatchCommon( entt::registry & registry, vaSceneRenderInstanceProcessor::SceneItem * items, uint32 itemCount, vaRenderInstanceList * opaqueList, vaRenderInstanceList * transparentList, const vaRenderInstanceList::FilterSettings & filter, const vaSceneSelectionFilterType & customFilter, uint32 baseInstanceIndex );
    };

    class vaPointShadowRV : public vaSceneRenderViewBase
    {
        vaRenderInstanceList                   m_selectionOpaque;
        //vaRenderInstanceList                   m_selectionTransparent;

        shared_ptr<vaShadowmap>             m_shadowmap;

    protected:
        friend class vaSceneRenderer;
        vaPointShadowRV( const shared_ptr<vaSceneRenderer> & parentRenderer );

    public:
        virtual ~vaPointShadowRV( ) { assert( m_shadowmap == nullptr ); assert( m_selectionOpaque.Count() == 0 ); }

        void                                SetActiveShadowmap( const shared_ptr<vaShadowmap> & shadowmap ) { assert( m_shadowmap == nullptr ); m_shadowmap = shadowmap; }

    protected:
        virtual void                        PreRenderTick( float deltaTime ) override;
        virtual vaDrawResultFlags           PreRenderTickParallelFinished( ) override;
        virtual void                        RenderTick( float deltaTime, vaRenderDeviceContext & renderContext, vaDrawResultFlags & currentDrawResults ) override;

        // this gets called from worker threads to provide chunks for processing!
        virtual void                        ProcessInstanceBatch( vaScene & scene, vaSceneRenderInstanceProcessor::SceneItem * items, uint32 itemCount, uint32 baseInstanceIndex ) override;
    };

    class vaLightProbeRV : public vaSceneRenderViewBase
    { 
        vaRenderInstanceList                   m_selectionOpaque;
        vaRenderInstanceList                   m_selectionTransparent;
        vaRenderInstanceList::SortHandle       m_sortDepthPrepass              = vaRenderInstanceList::EmptySortHandle;
        vaRenderInstanceList::SortHandle       m_sortOpaque                    = vaRenderInstanceList::EmptySortHandle;
        vaRenderInstanceList::SortHandle       m_sortTransparent               = vaRenderInstanceList::EmptySortHandle;

        shared_ptr<vaIBLProbe>              m_probe;         // this is the probe we're updating
        Scene::IBLProbe                     m_probeData;     // this is the probe data that we're updating the probe to

    protected:
        friend class vaSceneRenderer;
        vaLightProbeRV( const shared_ptr<vaSceneRenderer> & parentRenderer );

    public:
        virtual ~vaLightProbeRV( ) { }

        void                                SetActiveProbe( const shared_ptr<vaIBLProbe> & probe, const Scene::IBLProbe & probeData )     { assert( m_probe == nullptr ); m_probe = probe; m_probeData = probeData;  }

    protected:
        virtual void                        PreRenderTick( float deltaTime ) override;
        virtual vaDrawResultFlags           PreRenderTickParallelFinished( ) override;
        virtual void                        RenderTick( float deltaTime, vaRenderDeviceContext & renderContext, vaDrawResultFlags & currentDrawResults ) override;

        // this gets called from worker threads to provide chunks for processing!
        virtual void                        ProcessInstanceBatch( vaScene & scene, vaSceneRenderInstanceProcessor::SceneItem * items, uint32 itemCount, uint32 baseInstanceIndex ) override;
    };

}
