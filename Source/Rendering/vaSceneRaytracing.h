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

#include "Rendering/vaSceneRenderInstanceProcessor.h"

#include "Rendering/vaRenderBuffers.h"

#include "Rendering/Shaders/vaRaytracingShared.h"

namespace Vanilla
{
    class vaSceneRenderer;

    class vaSceneRaytracing : public vaRenderingModule, public vaUIPanel//, public std::enable_shared_from_this<vaSceneRaytracing>
    {
    public:
        // not sure if these settings should be here
        struct Settings
        {
            float                           MIPOffset         = -0.1f;       // raytracing-specific added sharpness (negative for sharper!)
            //float                           MIPSlopeModifier  = 0.4f;       // moved back to shader since it's no longer Raytracing-only property
        };

    protected:
        vaSceneRenderer &                   m_sceneRenderer;

        // scratch doesn't need multi-buffering because it's reused over and over
        shared_ptr<vaRenderBuffer>          m_scratchResource;

        // we've got to multi-buffer these because the older ones must be kept alive until they finish rendering
        shared_ptr<vaRenderBuffer>          m_topLevelAccelerationStructure[vaRenderDevice::c_BackbufferCount];

        int64                               m_lastFrameIndex                    = -1;
        int                                 m_currentBackbuffer                 = 0;

        // TODO: THESE COULD BE ONLY THE INDEX IN STORAGE AND AN INSTANCE ID!!!!!!!1!!!111!!1eleven!!!
        // any size reductions here will help a lot! inherit from vaSceneRenderInstanceProcessor::SceneItem to avoid copying
        struct InstanceItem //: public vaSceneRenderInstanceProcessor::SceneItem
        {
            vaMatrix4x4                     Transform;
            uint                            InstanceIndex;
        };
        
        // struct GeometryItem
        // {
        //     vaFramePtr<vaRenderMesh>        Mesh;
        //     // vaFramePtr<vaRenderMaterial>    Material;          // <- this will be needed for optimized alpha testing (opaque) flag but let's not optimize for that right now
        //     // vaFramePtr<vaRenderAnimation>   Animation;         // <- heheh this in the (far?) future
        // };

        // a list of ALL instances !! TODO: this can be significantly reduced
        std::vector< InstanceItem >         m_instanceList;
        std::atomic_uint32_t                m_instanceCount     = 0;
        //vaAppendConsumeList< GeometryItem > m_geometryList;

        // valid from PreProcess to PostRenderCleanup
        shared_ptr<vaRenderInstanceStorage> m_instanceStorage;

        Settings                            m_settings;
        float                               m_MIPOffset         = 0.0f;
        float                               m_MIPSlopeModifier  = 0.6f;


    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaSceneRaytracing( const vaRenderingModuleParams & params );
    public:
        ~vaSceneRaytracing( );

        friend class vaSceneRenderInstanceProcessor;

        // this is concurrently called from the LOD item processor and delivers all instances (transforms, meshes & materials).
        // it will be called many times in parallel so make sure it's all thread-safe!
        void                                PrepareInstanceBatchProcessing( const shared_ptr<vaRenderInstanceStorage> & instanceStorage );
        void                                ProcessInstanceBatch( vaScene & scene, vaSceneRenderInstanceProcessor::SceneItem * items, uint32 itemCount, uint32 baseInstanceIndex );
        void                                PreRenderUpdate( vaRenderDeviceContext & renderContext, const std::unordered_set<vaFramePtr<vaRenderMesh>> & meshes, const std::unordered_set<vaFramePtr<vaRenderMaterial>> & materials );
        void                                PostRenderCleanup( );

        //virtual void                        DoSomething( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & outResults, const vaDrawAttributes & drawAttributes ) = 0;

    public:
        void                                UpdateAndSetToGlobals( vaRenderDeviceContext & renderContext, vaShaderItemGlobals & shaderItemGlobals, const vaDrawAttributes & drawAttributes );

        Settings &                          Settings( )                                                 { return m_settings; }

    protected:
        virtual void                        UIPanelTick( vaApplicationBase & application ) override;
        virtual void                        UIPanelTickAlways( vaApplicationBase & application ) override;

    protected:
        virtual void                        PreRenderUpdateInternal( vaRenderDeviceContext & renderContext, const std::unordered_set<vaFramePtr<vaRenderMesh>> & meshes, const std::unordered_set<vaFramePtr<vaRenderMaterial>> & materials ) = 0;
        virtual void                        PostRenderCleanupInternal( ) = 0;

        const shared_ptr<vaRenderBuffer> &  GetScratch( uint64 minSize );
    };

}
