///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XeGTAO is based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion", 
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
// 
// Implementation:  Filip Strugar (filip.strugar@intel.com), Steve Mccalla <stephen.mccalla@intel.com>         (\_/)
// Version:         (see XeGTAO.h)                                                                            (='.'=)
// Details:         https://github.com/GameTechDev/XeGTAO                                                     (")_(")
//
// Version history: see XeGTAO.h
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Rendering/Shaders/vaShaderCore.h"

#include "Rendering/vaRendering.h"

#include "Rendering/vaShader.h"
#include "Rendering/vaRenderBuffers.h"

#include "Rendering/vaTexture.h"

#include "Core/vaUI.h"

#include "IntegratedExternals/vaImguiIntegration.h"
#include "Rendering/Shaders/XeGTAO.h"

namespace Vanilla
{
    class vaGTAO : public vaRenderingModule, public vaUIPanel
    {
    protected:
        mutable bool                                m_debugShowNormals          = false;
        mutable bool                                m_debugShowEdges            = false;
        mutable bool                                m_debugShowGTAODebugViz     = false;

        bool                                        m_use32bitDepth             = false;
        bool                                        m_use16bitMath              = true;
        bool                                        m_generateNormals           = false;
        vaVector2i                                  m_size;

        shared_ptr<vaTexture>                       m_workingDepths;
        shared_ptr<vaTexture>                       m_workingDepthsMIPViews[XE_GTAO_DEPTH_MIP_LEVELS];
        shared_ptr<vaTexture>                       m_workingVisibility;
        shared_ptr<vaTexture>                       m_workingVisibilityPong;                    // only required to support "blurry" denoise level 
        shared_ptr<vaTexture>                       m_workingEdges;
        shared_ptr<vaTexture>                       m_debugImage;
        shared_ptr<vaTexture>                       m_workingNormals;

        shared_ptr<vaTexture>                       m_hilbertLUT;

        mutable XeGTAO::GTAOSettings                m_settings;

        bool                                        m_constantsMatchDefaults    = false;        // just an optimization thing - see XE_GTAO_USE_DEFAULT_CONSTANTS

        // MSAA versions include 1-sample for non-MSAA
        vaAutoRMI<vaComputeShader>                  m_CSGenerateNormals;                        // optional screen space normal generation from depth (and results could be reused elsewhere)
        vaAutoRMI<vaComputeShader>                  m_CSPrefilterDepths16x16;                   // pass 1
        vaAutoRMI<vaComputeShader>                  m_CSGTAOLow;                                // pass 2 - low quality
        vaAutoRMI<vaComputeShader>                  m_CSGTAOMedium;                             // pass 2 - medium quality
        vaAutoRMI<vaComputeShader>                  m_CSGTAOHigh;                               // pass 2 - high quality
        vaAutoRMI<vaComputeShader>                  m_CSGTAOUltra;                              // pass 2 - ultra quality
        vaAutoRMI<vaComputeShader>                  m_CSPreDenoise;                             // pass 3a - only required to support "blurry" denoise level 
        vaAutoRMI<vaComputeShader>                  m_CSDenoise;                                // pass 3

        bool                                        m_shadersDirty                  = true;

        vaTypedConstantBufferWrapper< XeGTAO::GTAOConstants, true >
                                                    m_constantBuffer;

        std::vector< pair< string, string > >       m_staticShaderMacros;

        // **************************************** Reference AO raytracer ****************************************
        mutable bool                                m_enableReferenceRTAO           = false;
        //
        XeGTAO::ReferenceRTAOConstants              m_referenceRTAOConstants;
        shared_ptr<vaRenderBuffer>                  m_referenceRTAOConstantsBuffer;
        shared_ptr<vaTexture>                       m_referenceRTAOBuffer;
        shared_ptr<vaTexture>                       m_referenceRTAONormalsDepths;               // RGB are normal xyz, A is viewspace Z (linear depth buffer)
        shared_ptr<vaShaderLibrary>                 m_referenceRTAOShaders;
        vaCameraBase                                m_referenceRTAOLastCamera;                  // these are for accumulating 
        const int                                   m_referenceRTAOAccumFrameGoal   = 512;      // these are for accumulating 
        int                                         m_referenceRTAOAccumFrameCount  = 0;        // these are for accumulating 
        //
        string                                      m_referenceRTAOAutoTrainingDumpTarget   = "";       // if this is != "", we'll automatically dump data when ready (all 
        bool                                        m_referenceRTAOAutoTrainingDumpDone     = false;    // if the above was used, and data was dumped, 
        // *****************************************************************************************************

    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaGTAO( const vaRenderingModuleParams & params );

    public:
        virtual ~vaGTAO( ) { }

    public:
        vaDrawResultFlags                           Compute( vaRenderDeviceContext & renderContext, const vaCameraBase & cameraBase, bool usingTAA, const shared_ptr<vaTexture> & outputAO, const shared_ptr<vaTexture> & inputDepth, const shared_ptr<vaTexture> & inputNormals = nullptr );
        vaDrawResultFlags                           ComputeReferenceRTAO( vaRenderDeviceContext & renderContext, const vaCameraBase & cameraBase, vaSceneRaytracing * sceneRaytracing, const shared_ptr<vaTexture> & inputDepth );

    public:
        XeGTAO::GTAOSettings &                      Settings( )                                         { return m_settings; }
        bool &                                      Use16bitMath( )                                     { return m_use16bitMath; }

        bool &                                      DebugShowNormals( )                                 { return m_debugShowNormals;        }
        bool &                                      DebugShowEdges( )                                   { return m_debugShowEdges;          }
        const shared_ptr<vaTexture>                 DebugImage( )                                       { return m_debugImage;              }

        bool &                                      ReferenceRTAOEnabled( )                             { return m_enableReferenceRTAO;  }

        // use "" to reset / abort
        //void                                        ReferenceRTAORecordWhenReady( const string & path ) { assert( m_enableReferenceRTAO ); assert( m_referenceRTAOAutoTrainingDumpTarget == "" ); m_referenceRTAOAutoTrainingDumpTarget = path; m_referenceRTAOAutoTrainingDumpDone = false; }
        //bool                                        ReferenceRTAORecorded( )                            { return m_referenceRTAOAutoTrainingDumpDone; }
        int                                         ReferenceRTAOSampleCount( ) const                   { return m_referenceRTAOAccumFrameCount; }
        int                                         ReferenceRTAOSampleGoal( ) const                    { return m_referenceRTAOAccumFrameGoal; }

        // this is a signal that ComputeReferenceRTAO needs to get called
        bool                                        RequiresRaytracing( ) const                         { return m_enableReferenceRTAO; }

    public:
        virtual void                                UIPanelTick( vaApplicationBase & application ) override;

    private:
        bool                                        UpdateTexturesAndShaders( int width, int height );
        void                                        UpdateConstants( vaRenderDeviceContext & renderContext, const vaMatrix4x4 & projMatrix, bool usingTAA );
    };

} // namespace Vanilla

