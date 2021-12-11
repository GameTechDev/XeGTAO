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

#include "Rendering/vaRendering.h"

#include "Core/vaUI.h"

namespace Vanilla
{
    class vaSkybox;
    class vaRenderMaterial;
    class vaRenderMesh;
    class vaPixelShader;
    class vaGPUSort;

    class vaPathTracer : public vaRenderingModule//, public vaUIPanel
    {
    public:
        enum class Mode
        {
            StaticAccumulate,
            RealTime
        };

    protected:

        shared_ptr<vaConstantBuffer>                m_constantBuffer;

        shared_ptr<vaShaderLibrary>                 m_shaderLibrary                 = nullptr;
        shared_ptr<vaPixelShader>                   m_PSWriteToOutput               = nullptr;

        shared_ptr<vaComputeShader>                 m_CSFinalize                    = nullptr;
        //shared_ptr<vaComputeShader>                 m_CSTickCounters                = nullptr;

        shared_ptr<vaTexture>                       m_radianceAccumulation;         // there's also viewspace depth stored in .w
        //shared_ptr<vaTexture>                       m_viewspaceDepth;

        shared_ptr<vaRenderBuffer>                  m_pathTracerControl;            // at the moment only contains active number of paths
        shared_ptr<vaRenderBuffer>                  m_pathPayloads;
        shared_ptr<vaRenderBuffer>                  m_pathGeometryHitInfoPayloads;
        shared_ptr<vaRenderBuffer>                  m_pathListSorted;               // path indices, sorted
        shared_ptr<vaRenderBuffer>                  m_pathSortKeys;                 // also g_pathSortKeys[g_pathTracerConsts.MaxPathCount] contains current alive count, dropping with each PathTracerFinalize

        shared_ptr<vaGPUSort>                       m_GPUSort;

        Mode                                        m_mode                          = Mode::StaticAccumulate; // Mode::RealTime;
        int                                         m_staticAccumSampleTarget       = 256; //32768;     // stop at this number of samples (full paths) per pixel
        bool                                        m_staticEnableAA                = true;
        bool                                        m_realtimeEnableAA              = true;
        bool                                        m_realtimeEnableTemporalNoise   = false;            // noise between frames
        int                                         m_realtimeAccumSampleTarget     = 1;                // stop at this number of samples (full paths) per pixel
        int                                         m_realtimeAccumDenoiseType      = 0;                // 0 - disabled, 1 - OIDN (if enabled), 2 - OptiX (if enabled), 3 - OptiX temporal (if enabled)

        // these manage frame sample accumulation and track changes that require restarting accumulation
        vaCameraBase                                m_accumLastCamera;          
        int64                                       m_accumLastShadersID            = -1;
        int                                         m_accumSampleTarget             = 0;        // determined by m_staticAccumSampleTarget or m_realtimeAccumSampleTarget based on mode
        int                                         m_accumSampleIndex              = 0;        // current number of samples (full paths) per pixel
        bool                                        m_enableRussianRoulette         = true;
        int                                         m_minBounces                    = 2;
        int                                         m_maxBounces                    = 10;
        constexpr static int                        c_maxBounceUpperBound           = 32;
        int                                         m_sampleNoiseOffset             = 0;        // always 0 in Mode::StaticAccumulate, and varies by frame in Mode::RealTime

        bool                                        m_enableFireflyClamp            = true;
        float                                       m_fireflyClampThreshold         = 8.0f;
        bool                                        m_enableNextEventEstimation     = true;
        bool                                        m_enablePathRegularization      = true;
        int                                         m_lightSamplingMode             = 2;        // 0 - uniform sampling; 1 - power-weighted heuristic; 2 - light tree

        bool                                        m_enablePerBounceSort           = true;

        bool                                        m_debugPathUnderCursor          = false;
        int                                         m_debugPathVizDim               = 1;
        bool                                        m_debugLightsForPathUnderCursor = false;
        //bool                                        m_debugCrosshairUnderCursor = false;

        PathTracerDebugViewType                     m_debugViz                      = PathTracerDebugViewType::None;

        float                                       m_debugForcedDispatchDivergence = 0.0f;

        float                                       m_globalMIPOffset               = -2.0f;

        shared_ptr<vaRenderMesh>                    m_paintingsMesh;
        shared_ptr<vaRenderMaterial>                m_divergenceMirrorMaterial;
        shared_ptr<vaRenderMaterial>                m_paintingsMaterialBackup;
        bool                                        m_divergenceMirrorTestEnabled   = false;
        bool                                        m_divergenceMirrorCameraSetPos  = false;
        float                                       m_divergenceMirrorRoughness     = 0.0f;

        int                                         m_replaceAllBistroMaterials     = 0;    // 0 - not active; 1 - "are you sure"? 2 - replaced

        //////////////////////////////////////////////////////////////////////////
        /// denoiser stuff
#ifdef VA_OIDN_INTEGRATION_ENABLED
        shared_ptr<struct vaDenoiserOIDN>           m_oidn;
        shared_ptr<struct vaDenoiserOptiX>          m_optix;
#endif
        shared_ptr<vaTexture>                       m_denoiserAuxAlbedoGPU;
        shared_ptr<vaTexture>                       m_denoiserAuxNormalsGPU;
        shared_ptr<vaTexture>                       m_denoiserAuxMotionVectorsGPU;
        shared_ptr<vaComputeShader>                 m_CSPrepareDenoiserInputs;
        //////////////////////////////////////////////////////////////////////////


    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaPathTracer( const vaRenderingModuleParams & params );

    public:
        virtual ~vaPathTracer( );

    public:
        vaDrawResultFlags                           Draw( vaRenderDeviceContext & renderContext, vaDrawAttributes & drawAttributes, const shared_ptr<vaSkybox> & skybox, const shared_ptr<vaTexture> & outputColor, const shared_ptr<vaTexture> & outputDepth );

        PathTracerDebugViewType &                   DebugViz( )                                             { return m_debugViz; }

        Mode                                        Mode( ) const                                           { return m_mode; }
        bool                                        FullyAccumulated( ) const                               { return m_accumSampleIndex == m_accumSampleTarget; }

        void                                        UITick( vaApplicationBase & application ); // override;

        shared_ptr<std::pair<vaVector3, vaVector3>> GetNextCameraRequest( )                                 { if( !m_divergenceMirrorCameraSetPos ) return nullptr; m_divergenceMirrorCameraSetPos = false; return std::make_shared<std::pair<vaVector3, vaVector3>>( vaVector3( 10.40f, 0.78f, 2.37f ), vaVector3( 0.961f, 0.278f, -0.007f ) ); }

    private:
        void                                        UpdateDivergenceMirrorExperiment();

        vaDrawResultFlags                           Denoise( vaRenderDeviceContext & renderContext, vaDrawAttributes & drawAttributes, const shared_ptr<vaTexture> & outputColor, const shared_ptr<vaTexture> & outputDepth );

        vaDrawResultFlags                           InnerPass( vaRenderDeviceContext & renderContext, vaDrawAttributes & drawAttributes, const shared_ptr<vaSkybox> & skybox, /*const shared_ptr<vaTexture>& outputColor, const shared_ptr<vaTexture>& outputDepth,*/ uint totalPathCount, bool denoisingEnabled, vaRenderOutputs uavInputsOutputs );
    };

} // namespace Vanilla

