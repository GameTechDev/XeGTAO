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

#include "Scene/vaSceneComponentCore.h"

#include "Scene/vaSceneAsync.h"

#include "Rendering/vaRendering.h"

namespace Vanilla
{
    class vaRenderMesh;
    class vaRenderMaterial;

    class vaSceneRenderer;
    class vaScene;

    // Used by vaSceneRenderer to takes scene render instances and fill them into (multiple) vaRenderInstanceLists as well as filling in
    // vaRenderInstanceStorage and updating render meshes, materials and etc.
    class vaSceneRenderInstanceProcessor
    {
    public:

        // These are the initial selection coming from vaScene that gets further processed and filtered down
        // the chain. Anyone who actually needs to draw an instance needs to mark the "Used" field to "true", so that
        // dependencies (meshes, materials) are processed. Used items get processed into a list of "RenderInstance"-s
        struct SceneItem
        {
            entt::entity                  Entity;
            vaFramePtr<vaRenderMesh>      Mesh;
            vaFramePtr<vaRenderMaterial>  Material;           // while this is actually part of material, we resolve the reference during insertion, also allowing for it to be overridden

            float                         DistanceFromRef;    // world distance from LOD reference point (which is usually just main camera position)
            float                         MeshLOD;
                        
            // this could be a bit field; non-const are modifiable by selection
            bool                          IsUsed;             // any selections that will use this instance need to mark it as 'Used'
            bool                          IsDecal;            // 'renderMaterial->GetMaterialSettings( ).LayerMode == vaLayerMode::Decal'
            bool                          ShowAsSelected;     // will highlight the instance (for selection/UI purposes)
        };

    private:
        vaSceneRenderer &                   m_sceneRenderer;

        vaAppendConsumeSet<vaFramePtr<vaRenderMesh>>
                                            m_uniqueMeshes;
        vaAppendConsumeSet<vaFramePtr<vaRenderMaterial>>
                                            m_uniqueMaterials;

        std::atomic_uint32_t                m_selectResults;        // actual type is vaDrawResultFlags

        vaLODSettings                       m_LODSettings;
        
        // just for asserting
        std::atomic_bool                    m_asyncFinalized        = false;
        std::atomic_bool                    m_inAsync               = false;
        
        std::atomic_bool                    m_canConsume            = false;

        std::atomic_uint32_t                m_instanceCount         = 0;

        shared_ptr<class vaRenderInstanceStorage> m_currentInstanceStorage;
        int64                               m_currentApplicationTickIndex   = -1;

        shared_ptr<vaScene>           m_scene                 = nullptr;
        std::vector<shared_ptr<vaSceneAsync::WorkNode>> 
                                            m_asyncWorkNodes;

    public:
        static constexpr uint32             c_ConcurrentChuckMaxItemCount = 512;

    public:
                                            vaSceneRenderInstanceProcessor( vaSceneRenderer & sceneRenderer );
                                            ~vaSceneRenderInstanceProcessor( );

        // const shared_ptr<vaScene> &         GetScene( ) const                                   { return m_scene; }
        void                                SetScene( const shared_ptr<class vaScene> & scene );

        void                                SetSelectionParameters( const vaLODSettings & LODSettings, const shared_ptr<class vaRenderInstanceStorage> & instanceStorage, int64 applicationTickIndex );
        
        // this updates meshes and materials and updates the GPU instance buffer
        void                                FinalizeSelectionAndPreRenderUpdate( vaRenderDeviceContext & renderContext, const shared_ptr<vaSceneRaytracing> & raytracer );

        void                                PostRenderCleanup( );

        const vaLODSettings &               LastLODSettings( )                                  { return m_LODSettings; }

        vaDrawResultFlags                   ResultFlags( ) const                                { assert( !m_inAsync ); return (vaDrawResultFlags)m_selectResults.load(); }

    protected:
        friend struct MainWorkNode;
        struct MainWorkNode : vaSceneAsync::WorkNode
        {
            vaSceneRenderInstanceProcessor &        Processor;
            vaScene &                               Scene;
            entt::basic_view< entt::entity, entt::exclude_t<>, const Scene::WorldBounds>
                BoundsView;

            ShaderInstanceConstants *               UploadConstants = nullptr;
            struct vaRenderInstance *               InstanceArray   = nullptr;
            std::atomic_uint32_t                    InstanceCounter = 0;
            uint32                                  MaxInstances    = 0;
            int64                                   ApplicationTickIndex = -1;
            MainWorkNode( vaSceneRenderInstanceProcessor & processor, vaScene & scene );
            virtual void                    ExecutePrologue( float deltaTime, int64 applicationTickIndex ) override { ApplicationTickIndex = applicationTickIndex; deltaTime; assert( Processor.m_currentApplicationTickIndex == applicationTickIndex ); }
            virtual std::pair<uint, uint>   ExecuteNarrow( const uint32 pass, vaSceneAsync::ConcurrencyContext & ) override;
            virtual void                    ExecuteWide( const uint32 pass, const uint32 itemBegin, const uint32 itemEnd, vaSceneAsync::ConcurrencyContext & ) override;
        };

    protected:
        void                                PreSelectionProc( MainWorkNode & workNode );
        void                                SelectionProc( MainWorkNode & workNode, uint32 entityBegin, uint32 entityEnd );
        void                                Report( vaDrawResultFlags flags )                   { m_selectResults.fetch_or( (uint32)flags ); }
    };




}
