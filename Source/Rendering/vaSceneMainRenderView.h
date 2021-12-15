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

#include "vaSceneRenderViews.h"

namespace Vanilla
{
    class vaPostProcessTonemap;
    class vaASSAOLite;
    class vaGTAO;
    class vaTAA;
    class vaCMAA2;
    class vaPathTracer;

    enum class vaRenderType : int32
    {
        Rasterization,
        PathTracing
    };

    enum class vaAAType : int32
    {
        None,
        TAA,
        CMAA2,
        SuperSampleReference,       // also used as the max value for automatic comparisons & benchmarking
        SuperSampleReferenceFast,   // just a less detailed version of SuperSampleReference but optimized for speed
                                    //            ExperimentalSlot1,

        MaxValue
    };
    string vaAATypeToUIName( vaAAType value );

    class vaSceneMainRenderView : public vaSceneRenderViewBase
    {
        vaMatrix4x4                         m_previousViewProj              = vaMatrix4x4::Identity;    // viewProj from the top of the previous RenderTick
        vaMatrix4x4                         m_lastViewProj                  = vaMatrix4x4::Identity;    // viewProj from the top of the current/last RenderTick
        vaMatrix4x4                         m_reprojectionMatrix            = vaMatrix4x4::Identity;    // from this RenderTick's clip space to prev RenderTick's clip space
        vaVector2                           m_previousCameraJitter          = {0,0};                    // camera jitter from the previous RenderTick
        vaVector2                           m_lastCameraJitter              = {0,0};
        vaVector2                           m_cameraJitterDelta             = {0,0};
        shared_ptr<vaRenderCamera>          m_camera;

        // Working textures - these are in the actual scene render resolution (higher in SS, lower in upscale scenarios)
        shared_ptr<vaTexture>               m_workingDepth;
        shared_ptr<vaTexture>               m_workingPreTonemapColor;
        shared_ptr<vaTexture>               m_workingPostTonemapColor;
        
        shared_ptr<vaTexture>               m_workingNormals;
        shared_ptr<vaTexture>               m_workingMotionVectors;         // a.k.a. velocity buffer
        shared_ptr<vaTexture>               m_workingViewspaceDepth;


        // These are in actual output resolution (as requested by/through Camera viewport settings)
        shared_ptr<vaTexture>               m_outputDepth;
        shared_ptr<vaTexture>               m_outputColor;

        shared_ptr<vaPostProcessTonemap>    m_postProcessTonemap;

        vaRenderInstanceList                m_selectionOpaque;
        vaRenderInstanceList                m_selectionTransparent;
        vaRenderInstanceList::SortHandle    m_sortDepthPrepass              = vaRenderInstanceList::EmptySortHandle;
        vaRenderInstanceList::SortHandle    m_sortOpaque                    = vaRenderInstanceList::EmptySortHandle;
        vaRenderInstanceList::SortHandle    m_sortTransparent               = vaRenderInstanceList::EmptySortHandle;

        shared_ptr<vaASSAOLite>             m_ASSAO;
        shared_ptr<vaGTAO>                  m_GTAO;
        //
        shared_ptr<vaTexture>               m_SSAOData;                     // output by various SSAO variants : either 1-channel AO or a R32 encoded bent normal + AO
        //        shared_ptr<vaDepthOfField>          m_DepthOfField;

        shared_ptr<vaPathTracer>            m_pathTracer                    = nullptr;

        bool                                m_enableCursorHoverInfo         = false;

        // sticking it here for now - not really where it should be but whatever
        shared_ptr<vaShaderLibrary>         m_referenceRTAO                 = nullptr;

        shared_ptr<vaCMAA2>                 m_CMAA2;
        shared_ptr<vaTAA>                   m_TAA;
        uint32                              m_TAASettingsHash               = 0;    // simple way of resetting TAA when other engine settings change; there is some chance of collision - in case it's an issue, upgrade to 64bit+ :)
        
#if 0
        //////////////////////////////////////////////////////////////////////////
        // playing around with upsampling - from the highresdepth branch
        vaAutoRMI<vaComputeShader>          m_CSSmartUpsampleFloat;
        vaAutoRMI<vaComputeShader>          m_CSSmartUpsampleUnorm;
        vaAutoRMI<vaPixelShader>            m_PSSmartUpsampleDepth;
        //////////////////////////////////////////////////////////////////////////
#endif

        //////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////
        // SuperSampling section - not sure exactly how I want to do this so
        // just ported from the old approach.
        struct SuperSampling
        {
            shared_ptr<vaTexture>               AccumulationColor;
            bool                                FastVersion                 = false;

            // 0 is SuperSampleReference, 1 is SuperSampleReferenceFast
            const int                           ResScale[2]                 = { 4    ,  2 };      // draw at ResScale times higher resolution (only 1 and 2 and 4 supported due to filtering support)
            int                                 GridRes[2]                  = { 4    ,  2 };      // multi-tap using GridRes x GridRes samples for each pixel
            float                               MIPBias[2]                  = { 1.60f,  0.60f };      // make SS sample textures from a bit lower MIPs to avoid significantly over-sharpening textures vs non-SS (for textures that have high res mip levels or at distance)
            float                               Sharpen[2]                  = { 0.0f,   0.0f };      // used to make texture-only view (for ex. looking at the painting) closer to non-SS (as SS adds a dose of blur due to tex sampling especially when no higher-res mip available for textures, which is the case here in most cases)
            float                               DDXDDYBias[2]               = { 0.20f,  0.30f };      // SS messes up with pixel size which messes up with specular as it is based on ddx/ddy so compensate a bit here (0.20 gave closest specular by PSNR diff from no-AA)

            int                                 GetSSResScale( ) const      { return ResScale[FastVersion ? 1 : 0]; }
            int                                 GetSSGridRes( ) const       { return GridRes[FastVersion ? 1 : 0]; }
            float                               GetSSMIPBias( ) const       { return MIPBias[FastVersion ? 1 : 0]; }
            float                               GetSSSharpen( ) const       { return Sharpen[FastVersion ? 1 : 0]; }
            float                               GetSSDDXDDYBias( ) const    { return DDXDDYBias[FastVersion ? 1 : 0]; }
        };
        shared_ptr<SuperSampling>           m_SS;
        //////////////////////////////////////////////////////////////////////////

        enum class DepthPrepassType
        {
            None,                   // don't use depth pre-pass
            UseExisting,            // use what's in m_workingDepth
            DrawAndUse,             // clear and do depth pre-pass into m_outputDepth and then use it
        };

    public:
        struct RenderSettings
        {
            bool                                ShowWireframe               = false;

            vaRenderType                        RenderPath                  = vaRenderType::Rasterization;

            // these are all rasterization settings
            int                                 AOOption                    = 3;        // 0 - disabled, 1 - ASSAO, 2 - GTAO, 3 - GTAO with bent normals
#if defined(VA_GTAO_SAMPLE) && !defined(VA_SAMPLE_BUILD_FOR_LAB)
            bool                                DebugShowAO                 = true;
#else
            bool                                DebugShowAO                 = false;
#endif
            vaAAType                            AAType                      = vaAAType::TAA;
        }                                   m_settings;

    protected:
        friend class vaSceneRenderer;
        vaSceneMainRenderView( const shared_ptr<vaSceneRenderer> & parentRenderer );

    public:
        virtual ~vaSceneMainRenderView( ) { }

    public:
        RenderSettings &                    Settings( )                             { return m_settings; }

        // don't forget to update the camera manually before calling vaSceneRenderer::Tick - things required are at least vaBaseCamera::SetViewport and vaBaseCamera::Tick
        const shared_ptr<vaRenderCamera> &  Camera( ) const                         { return m_camera; }

        const shared_ptr<vaASSAOLite> &     ASSAO( ) const                          { return m_ASSAO; }
        const shared_ptr<vaGTAO> &     	    GTAO( ) const                           { return m_GTAO; }

        const shared_ptr<vaPathTracer>      PathTracer( ) const                     { return m_pathTracer; }

        void                                EnableSuperSampling( bool enabled, bool fastVersion );
        
        const shared_ptr<vaTexture> &       GetOutputDepth( ) const                 { return m_outputDepth; } // (m_SS!=nullptr)?(m_SS->DownsampledDepth):(m_workingDepth); }
        const shared_ptr<vaTexture> &       GetOutputColor( ) const                 { return m_outputColor; }  // (m_SS!=nullptr)?(m_SS->DownsampledColor):(m_workingPostTonemapColorA); }
        //const shared_ptr<vaTexture> &       GetPostTonemapLuma( ) const             { return m_exportedLuma; }


        bool                                GetCursorHoverInfoEnabled( ) const      { return m_enableCursorHoverInfo; }
        void                                SetCursorHoverInfoEnabled( bool val )   { m_enableCursorHoverInfo = val; }

    protected:
        // beware, camera used is m_camera - didn't split it out as an argument because using a separate (temporary) would not work for obtaining average luminance so that needs solving first :)
        void                                RenderTickInternal( vaRenderDeviceContext & renderContext, vaDrawResultFlags & currentDrawResults, vaDrawAttributes::GlobalSettings & globalSettings, bool skipCameraLuminanceUpdate, DepthPrepassType depthPrepass );

        virtual void                        UIDisplayStats( ) override;

    public:
        virtual void                        UITickAlways( vaApplicationBase & application ) override;       // for keyboard stuff that you want to happen even when UI is hidden
        virtual void                        UITick( vaApplicationBase & application ) override;             // for UI settings


    protected:
        virtual void                        PreRenderTick( float deltaTime ) override;
        virtual vaDrawResultFlags           PreRenderTickParallelFinished( ) override;
        virtual void                        RenderTick( float deltaTime, vaRenderDeviceContext & renderContext, vaDrawResultFlags & currentDrawResults ) override;

        // this gets called from worker threads to provide chunks for processing!
        virtual void                        ProcessInstanceBatch( vaScene & scene, vaSceneRenderInstanceProcessor::SceneItem * items, uint32 itemCount, uint32 baseInstanceIndex ) override;

        virtual bool                        RequiresRaytracing( ) const override;
    };

}
